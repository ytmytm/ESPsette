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

#include <RemoteDebug.h>

RemoteDebug Debug;
ESP8266WebServer HTTPServer(80);

void setupPins() {
  //GPIO 1 (TX) swap the pin to a GPIO.
  pinMode(1, FUNCTION_3);
  pinMode(1, INPUT_PULLUP); 
  //GPIO 3 (RX) swap the pin to a GPIO.
  pinMode(3, FUNCTION_3);
  pinMode(3, INPUT_PULLUP);
  // GPIO
  pinMode(0, INPUT_PULLUP);
  pinMode(2, INPUT_PULLUP);
  // setup
  pinMode(PIN_SENSE, OUTPUT);
  digitalWrite(PIN_SENSE, LOW); // always pushed so motor can be controlled
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
  Serial.begin(115200);
  setupPins();
  setupNetwork();
  initializeOTA();
  setupDebug();
  setupWebserver();
  debugV("setup completed");
  lastcall = 0;
  lastMotor = digitalRead(PIN_MOTOR);
  lastSense = digitalRead(PIN_SENSE);
  lastWrite = digitalRead(PIN_WRITE);
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
  if (motor!=lastMotor) {
    lastMotor=motor;
    debugW("* motor state changed to %u",motor);
  }
  sense = digitalRead(PIN_SENSE);
  if (sense!=lastSense) {
    lastSense=sense;
    debugW("* sense state changed to %u",sense);
  }
  wr = digitalRead(PIN_WRITE);
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
 
