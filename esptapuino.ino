// IDE settings

// Board: ESP01
// IDE settings: Generic ESP8266 module, 80MHz, vtables Flash, reset method: ck, crystal 26MHz, flash 40MHZ, flash mode QIO, 921600 on COM5
// as per https://arduino-esp8266.readthedocs.io/en/latest/boards.html#generic-esp8266-module 

// 1MB

// revised on 27.4, 1.5
#define PIN_MOTOR 0 // GPIO0       <- MOTOR (C,3) $01 bit 5 (through optocoupler with gnd pulldown 10k) // 0 = motor off, 1 = motor on
#define PIN_SENSE 1 // GPIO1 (TxD) -> SENSE (F,6) $01 bit 4 (with pullup to 3.3V, 10k) // 0 = pushed, 1 = released
#define PIN_WRITE 2 // GPIO2       <- WRITE (E,5) $01 bit 3
#define PIN_READ  3 // GPIO3 (RxD) -> READ  (D,4) CIA#1 /FLAG, I2S, pulled to 5V in C64, normal high (input pullup), impulse low

#define HOST_NAME "esptapuino"
#define WIFI_SSID ""
#define WIFI_PASS ""

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>

#include <i2s.h>
#include <i2s_reg.h>

#include "draxcaught.h"

#include <RemoteDebug.h>

RemoteDebug Debug;
ESP8266WebServer HTTPServer(80);

enum class State : uint8_t { INIT, MOTOR_WAIT_LOW, MOTOR_WAIT_HIGH, STREAMING };

State state = State::INIT;

///////////////////////////////////
// i2s

static inline uint8_t readRomByte(const uint8_t* addr)
{
    uint32_t bytes;
    bytes = *(uint32_t*)((uint32_t)addr & ~3);
    return ((uint8_t*)&bytes)[(uint32_t)addr & 3];
}

struct I2S_status_s {
  int playing;
  int input_index;
  int input_len;
} I2S_WAV;

void wav_setup()
{
  debugI("wav_setup");
  I2S_WAV.playing = false;
}

void wav_stopPlaying()
{
  i2s_end();
  I2S_WAV.playing = false;
  state = State::MOTOR_WAIT_LOW;
}

void wav_startPlayingFile()
{
  debugI("start playing");

  I2S_WAV.input_index = 0x14; // skip over TAP header
  I2S_WAV.input_len = draxcaught_tap_len; // length of data
  I2S_WAV.playing = true;

  i2s_begin();
  i2s_set_rate(985248 / 8 / 2);    // bit rate = PAL / 8 (tap div) / 2 (our div), must be >40312

  debugI("enter loop for playing");
  wav_loop();
}

int readPulse(void) {
  if (I2S_WAV.input_index == I2S_WAV.input_len) {
    debugI("EOF");
    return -1;
  }
  int a = I2S_WAV.input_index;
  int pulseLen = readRomByte(draxcaught_tap + a);
  if (pulseLen == 0) {
      a++;
      pulseLen = readRomByte(draxcaught_tap + a);
      a++;
      pulseLen |= readRomByte(draxcaught_tap + a) << 8;
      a++;
      pulseLen |= readRomByte(draxcaught_tap + a) << 16;
  }
  a++;
  I2S_WAV.input_index = a;
  pulseLen /= 2;
  if (a % 10240 == 0) {
    debugI("index=%i",a);
  }
  return pulseLen;
}

void ICACHE_FLASH_ATTR tapTask(void)
{
    uint32_t bitStream = 0;
    int bitStreamLen = 0;

    debugW("Start stream");

    i2s_begin();
    i2s_set_rate(985248 / 8 / 2);    // bit rate = PAL / 8 (tap div) / 2 (our div), must be >40312, czemu tylko 3.7s? ~61000
    //i2s_set_rate(8000);    // bit rate = PAL / 8 (tap div) / 2 (our div), must be >40312 <- ma wplyw, jest 30s zamiast 4s, 7x dłużej

        for (int a = 0x14; a < draxcaught_tap_len; a++) {
            int pulseLen = readRomByte(draxcaught_tap + a);
            if (pulseLen == 0) {
                a++;
                pulseLen = readRomByte(draxcaught_tap + a);
                a++;
                pulseLen |= readRomByte(draxcaught_tap + a) << 8;
                a++;
                pulseLen |= readRomByte(draxcaught_tap + a) << 16;
            }
            pulseLen /= 2;

            for (int b = 0; b < pulseLen; b++) {
                int outBit = 0; // 1; // reverse logic b/c we are driving through a transistor
                if (b > pulseLen / 2) outBit = 1; //0; reverse logic
                bitStream = (bitStream << 1) | outBit;
                bitStreamLen++;
                if (bitStreamLen == 32) {
                    bitStreamLen = 0;
                    i2s_write_sample(bitStream);
                }
            }
            if (digitalRead(PIN_MOTOR)==LOW) {
              debugW("Stop playing while wavpulseloop");
              break;
            }
    }
    i2s_end();
    state = State::MOTOR_WAIT_LOW;
    debugW("Completed stream");
}

bool ICACHE_FLASH_ATTR wavPulse(int pulseLen) {
  static uint32_t bitStream = 0; // static so we can pick up from here
  static int bitStreamLen = 0; // static so we can pick up from here
  static int lastindex = 0;

  // complete previous unfinished job
  if (bitStreamLen == 32) {
      if (!(i2s_write_sample(bitStream))) {
        debugV("Buffer still full");
        return true; // play needs to continue
      }
      bitStreamLen = 0;
  }

  for (int b = 0; b < pulseLen; b++) {
      int outBit = 1; // 1=reverse logic b/c we are driving through a transistor
      if (b > pulseLen / 2) outBit = 0; //0=reversed logic
      bitStream = (bitStream << 1) | outBit;
      bitStreamLen++;
      if (bitStreamLen == 32) {
          if (i2s_is_full()) {
            if (lastindex == 0) {
              debugV("Buffer full, yielding at %i",I2S_WAV.input_index);
              lastindex = I2S_WAV.input_index;
            }
            yield();
          }
          if (!(i2s_write_sample(bitStream))) {
            debugV("Buffer full");
            return true; // play needs to continue
          }
          lastindex = 0;
          bitStreamLen = 0;
      }
  }
  return false; // pulse completed
}

void wav_loop() {
  int i = 0;

  while (I2S_WAV.playing) {
    int pulseLen = readPulse();
    if (pulseLen<0 || digitalRead(PIN_MOTOR)==LOW) {
      debugW("Stop playing");
      wav_stopPlaying();
      debugI("EOF");
      break;
    }
    while (wavPulse(pulseLen)) {
      i++;
      if (i==10000) {
        debugW("%i loop counts, index is %i", i, I2S_WAV.input_index );
        i=0;
        debugV("i2s available: %i", i2s_available());
        debugV("i2s is empty %i", i2s_is_empty());
        debugV("i2s is full %i", i2s_is_full());
      }
      yield();
      if (digitalRead(PIN_MOTOR)==LOW) {
        debugW("Stop playing while wavpulseloop");
        wav_stopPlaying();
        break;
      }
    }; // repeat through full buffer until pulse completed
    // read next pulse
    // break? // handle something every pulse?
  }
}

///////////////////////////////////


void setupPins() {
  //GPIO 1 (TX) swap the pin to a GPIO.
  pinMode(1, FUNCTION_3);
  pinMode(1, INPUT);
  //GPIO 3 (RX) swap the pin to a GPIO.
//  pinMode(3, FUNCTION_3);
//  pinMode(3, FUNCTION_1);
  pinMode(3, OUTPUT);
  // GPIO
  pinMode(0, INPUT);
  pinMode(2, INPUT);
  // setup
  pinMode(PIN_SENSE, OUTPUT);
  digitalWrite(PIN_SENSE, LOW); // always pushed so motor can be controlled
  pinMode(PIN_READ, OUTPUT);
  digitalWrite(PIN_READ, LOW); // high is default
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t timeout = millis() + 20000; // Time out
  while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
    delay(250);
  }
}

void setupNetwork() {
  connectWiFi();
  WiFi.hostname(HOST_NAME);
}

void setupWebserver() {
  HTTPServer.on("/", handleRoot);
  HTTPServer.onNotFound(handleNotFound);
  HTTPServer.begin();
}

void initializeOTA() {
  ArduinoOTA.onStart([]() {
    debugI("* OTA: Start");
  });
  ArduinoOTA.onEnd([]() {
    debugI("\n*OTA: End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    debugV("*OTA: Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    debugE("*OTA: Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) debugE("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) debugE("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) debugE("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) debugE("Receive Failed");
    else if (error == OTA_END_ERROR) debugE("End Failed");
  });
  ArduinoOTA.setHostname(HOST_NAME);
  // Begin
  ArduinoOTA.begin();
}

void setupDebug() {
  Debug.begin(HOST_NAME); // Initialize the WiFi server
  //Debug.setPassword("r3m0t0."); // Password on telnet connection ?
  Debug.setResetCmdEnabled(true); // Enable the reset command
  Debug.showProfiler(true); // Profiler (Good to measure times, to optimize codes)
  Debug.showColors(true); // Colors
}

uint32_t lastcall;
uint8_t lastMotor=0;
uint8_t lastSense=0;
uint8_t lastWrite=0;

void setup() {
  Serial.begin(115200); // will lock up without this :/
  setupPins();
  setupNetwork();
  initializeOTA();
  setupDebug();
  setupWebserver();
  wav_setup();
  digitalWrite(PIN_SENSE, LOW); // always pushed so motor can be controlled
  state = State::INIT;
  debugV("setup completed");
  lastcall = 0;
  lastMotor = digitalRead(PIN_MOTOR);
  lastSense = digitalRead(PIN_SENSE);
  lastWrite = digitalRead(PIN_WRITE);
  state = State::MOTOR_WAIT_LOW;
}

void debugGPIOs() {
  uint8_t g0, g1, g2, g3;
  g0 = digitalRead(0);
  g1 = digitalRead(1);
  g2 = digitalRead(2);
  g3 = digitalRead(3);
  debugI("* GPIOS=[%i,%i,%i,%i]\n",g0,g1,g2,g3);
}

void loop() {
  // put your main code here, to run repeatedly:
  uint32_t timeBeginLoop = millis();
  // do stuff
  uint8_t motor, sense, wr;
  motor = digitalRead(PIN_MOTOR);
  sense = digitalRead(PIN_SENSE);
  wr = digitalRead(PIN_WRITE);

  switch (state) {
    case State::MOTOR_WAIT_LOW:
      if ((motor!=lastMotor) && (motor==LOW)) {
        state = State::MOTOR_WAIT_HIGH;
        debugW("* transition into MOTOR_WAIT_HIGH");
      }
      break;
   case State::MOTOR_WAIT_HIGH:
     if ((motor!=lastMotor) && (motor==HIGH)) {
      state = State::STREAMING;
      debugW("* transition into STREAMING");
      tapTask();
      //wav_startPlayingFile();
     }
     break;
   case State::STREAMING:
     break;
   default:
     break;
  }

  if (motor!=lastMotor) {
    lastMotor=motor;
    debugW("* motor state changed to %u",motor);
  }
  if (sense!=lastSense) {
    lastSense=sense;
    debugW("* sense state changed to %u",sense);
  }
  if (wr!=lastWrite) {
    lastWrite = wr;
    debugW("* write state changed to %u",wr);
  }

  if ((millis()-lastcall)>10000) {
    lastcall = millis();
    debugV("* heartbeat millis=%u",lastcall);
    debugGPIOs();
  }
  // ...
  // housekeeping
  wav_loop();
  ArduinoOTA.handle();
  HTTPServer.handleClient();
  Debug.handle();
  yield();
  uint32_t time = (millis() - timeBeginLoop);

  if (time > 100) {
    debugI("* Time elapsed for the loop: %u ms.", time);
  } else if (time > 200) {
    debugW("* Time elapsed for the loop: %u ms.", time);
  }
}

//// web

 void handleRoot() {
     // Root web page
     debugV("* http root request");

  String doc;
  uint8_t g0, g1, g2, g3;
  g0 = digitalRead(0);
  g1 = digitalRead(1);
  g2 = digitalRead(2);
  g3 = digitalRead(3);

  doc = "["+String(g0)+","+String(g1)+","+String(g2)+","+String(g3)+"]";

  debugV("* reply: [%s]",doc.c_str());
     HTTPServer.send(200, "text/plain", doc);

}

 void handleNotFound(){
     debugW("* page not found");
     String message = "File Not Found\n\n";
     message.concat("URI: ");
     message.concat(HTTPServer.uri());
     message.concat("\nMethod: ");
     message.concat((HTTPServer.method() == HTTP_GET)?"GET":"POST");
     message.concat("\nArguments: ");
     message.concat(HTTPServer.args());
     message.concat("\n");
     for (uint8_t i=0; i<HTTPServer.args(); i++){
         message.concat(" " + HTTPServer.argName(i) + ": " + HTTPServer.arg(i) + "\n");
     }
     debugW("* reply:[%s]", message.c_str());
     HTTPServer.send(404, "text/plain", message);
 }
 
