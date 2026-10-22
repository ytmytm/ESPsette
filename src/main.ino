// IDE settings

// Board: ESP01
// IDE settings: Generic ESP8266 module, 80MHz, vtables Flash, reset method: ck, crystal 26MHz, flash 40MHZ, flash mode QIO, 921600 on COM5
// as per https://arduino-esp8266.readthedocs.io/en/latest/boards.html#generic-esp8266-module 

// 1MB

// revised on 27.4, 1.5
#define PIN_MOTOR 0 // GPIO0       <- MOTOR (C,3) $01 bit 5 (through optocoupler with gnd pulldown 10k) // 0 = motor off, 1 = motor on
#define PIN_SENSE 1 // GPIO1 (TxD) -> SENSE (F,6) $01 bit 4 (with pullup to 3.3V, 10k) // 0 = pushed, 1 = released
#define PIN_READ  3 // GPIO3 (RxD) -> READ  (D,4) CIA#1 /FLAG, I2S, pulled to 5V in C64, normal high (input pullup), impulse low

#define HOST_NAME "espsette"

#if __has_include("local_wifi.h")
#include "local_wifi.h" // Local build-only credentials; ignored by Git.
#endif

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>

#include <core_esp8266_i2s.h>

#include "draxcaught.h"

#include <RemoteDebug.h>

RemoteDebug Debug;
ESP8266WebServer HTTPServer(80);

enum class State : uint8_t { WAITING, STREAMING, DRAINING, FINISHED, ERROR };

// One I2S sample contains 32 serial output bits. One bit represents 16 PAL cycles.
static const uint32_t TAP_CLOCK_HZ = 985248UL;
static const uint32_t CYCLES_PER_BIT = 16;
static const uint32_t I2S_WORD_RATE = TAP_CLOCK_HZ / CYCLES_PER_BIT / 32;
static const uint8_t DMA_WORDS = 64; // ESP8266 core: 8 DMA descriptors, 64 words each.
static const uint8_t DMA_BUFFERS = 8;
static const uint8_t DRAIN_WORDS = 64;
static const uint32_t DRAIN_GUARD_US = 40000;

State state = State::WAITING;
uint32_t tapEnd = 0;
bool networkReady = false;
bool otaReady = false;

static inline uint8_t readRomByte(const uint8_t* addr) {
  uint32_t bytes = *(const uint32_t*)((uintptr_t)addr & ~3U);
  return ((const uint8_t*)&bytes)[(uintptr_t)addr & 3U];
}

struct DecoderState {
  uint32_t offset;       // Next TAP record in the original image.
  uint32_t pulseTicks;
  uint32_t pulseIndex;
  uint8_t cycleRemainder;
};

struct TapPlayer {
  DecoderState decoder;
  uint32_t pendingWord;
  bool wordPending;
  uint8_t drainWordsLeft;
  uint32_t emptySince;
} player;

// Each checkpoint describes the decoder at the start of one submitted DMA
// buffer. The producer cannot get more than 8 buffers ahead of the output.
DecoderState checkpoints[DMA_BUFFERS];
DecoderState runStartDecoder;
uint32_t checkpointNumber[DMA_BUFFERS];
volatile uint32_t dmaCallbacks = 0;
volatile uint32_t committedWords = 0;
volatile bool dmaUnderrun = false;
volatile uint32_t lastDmaCallbackUs = 0;
volatile uint32_t motorFallCallbacks = 0;
volatile uint32_t motorFallElapsedUs = 0;
volatile bool motorFell = false;
uint32_t scheduledWords = 0; // Includes idle padding.
uint32_t generatedTapBits = 0;
uint32_t realBitRate = 0;

void ICACHE_RAM_ATTR onDmaBufferDone() {
  ++dmaCallbacks;
  lastDmaCallbackUs = micros();
  if (dmaCallbacks >= DMA_BUFFERS &&
      committedWords + 1 < (dmaCallbacks - DMA_BUFFERS + 1) * DMA_WORDS) {
    dmaUnderrun = true;
  }
}

void ICACHE_RAM_ATTR onMotorFalling() {
  motorFallCallbacks = dmaCallbacks;
  motorFallElapsedUs = micros() - lastDmaCallbackUs;
  motorFell = true;
}

void resetPlayer(uint32_t offset) {
  player.decoder.offset = offset;
  player.decoder.pulseTicks = 0;
  player.decoder.pulseIndex = 0;
  player.decoder.cycleRemainder = 0;
  player.pendingWord = 0;
  player.wordPending = false;
  player.drainWordsLeft = 0;
  player.emptySince = 0;
}

bool validateTap() {
  static const char signature[] = "C64-TAPE-RAW";
  if (draxcaught_tap_len < 20) return false;
  for (uint8_t i = 0; i < 12; ++i) {
    if (readRomByte(draxcaught_tap + i) != (uint8_t)signature[i]) return false;
  }
  if (readRomByte(draxcaught_tap + 12) != 1 ||
      readRomByte(draxcaught_tap + 13) != 0 ||
      readRomByte(draxcaught_tap + 14) != 0) return false; // TAP v1, C64 PAL
  uint32_t dataSize = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    dataSize |= (uint32_t)readRomByte(draxcaught_tap + 16 + i) << (8 * i);
  }
  if (dataSize != draxcaught_tap_len - 20) return false;
  tapEnd = draxcaught_tap_len;
  uint32_t cursor = 20;
  while (cursor < tapEnd) {
    uint8_t value = readRomByte(draxcaught_tap + cursor);
    if (value != 0) {
      ++cursor;
    } else {
      if (tapEnd - cursor < 4) return false;
      uint32_t cycles = (uint32_t)readRomByte(draxcaught_tap + cursor + 1) |
                        ((uint32_t)readRomByte(draxcaught_tap + cursor + 2) << 8) |
                        ((uint32_t)readRomByte(draxcaught_tap + cursor + 3) << 16);
      if (cycles == 0) return false;
      cursor += 4;
    }
  }
  return true;
}

void idleReadPin() {
  pinMode(PIN_READ, OUTPUT);
  digitalWrite(PIN_READ, LOW); // The external transistor makes this READ high.
}

void stopI2S() {
  i2s_end();
  idleReadPin();
}

bool nextTapeBit(uint8_t &bit);

// A MOTOR edge is timestamped in an ISR. Callback count identifies the DMA
// buffer currently being sent; elapsed time estimates the bit within it.
// The final bit still depends on FIFO/interrupt latency and must be measured
// on hardware. No TAP record is skipped merely because MOTOR stopped.
void pauseTape() {
  const uint32_t callbacks = motorFallCallbacks;
  const uint32_t elapsedUs = motorFallElapsedUs;
  uint32_t playedBits = 0;
  if (callbacks >= DMA_BUFFERS) {
    const uint32_t withinBuffer = min(
        (uint32_t)(DMA_WORDS * 32 - 1),
        (uint32_t)(((uint64_t)elapsedUs * realBitRate) / 1000000ULL));
    playedBits = (callbacks - DMA_BUFFERS) * DMA_WORDS * 32 + withinBuffer;
  }
  stopI2S();
  if (state == State::DRAINING && playedBits >= generatedTapBits) {
    resetPlayer(20);
    state = State::WAITING;
    debugI("TAP completed; rewound at MOTOR stop");
    return;
  }
  if (playedBits > generatedTapBits) {
    state = State::ERROR; // DMA ran ahead of the source; LOAD is invalid.
    digitalWrite(PIN_SENSE, HIGH);
    debugE("I2S underrun before MOTOR stop");
    return;
  }
  if (playedBits == 0) player.decoder = runStartDecoder;
  if (playedBits != 0) {
    const uint32_t buffer = playedBits / (DMA_WORDS * 32);
    const uint32_t withinBuffer = playedBits % (DMA_WORDS * 32);
    const uint8_t slot = buffer % DMA_BUFFERS;
    if (checkpointNumber[slot] != buffer) {
      state = State::ERROR;
      digitalWrite(PIN_SENSE, HIGH);
      debugE("Missing DMA checkpoint at %lu", (unsigned long)buffer);
      return;
    }
    player.decoder = checkpoints[slot];
    uint8_t bit;
    for (uint32_t i = 0; i < withinBuffer; ++i) {
      if (!nextTapeBit(bit)) {
        state = State::ERROR;
        digitalWrite(PIN_SENSE, HIGH);
        debugE("Cannot restore TAP bit %lu", (unsigned long)playedBits);
        return;
      }
    }
  }
  player.pendingWord = 0;
  player.wordPending = false;
  player.drainWordsLeft = 0;
  player.emptySince = 0;
  state = State::WAITING;
  debugI("Paused TAP after %lu output bits at offset %lu",
         (unsigned long)playedBits, (unsigned long)player.decoder.offset);
}

void startTape() {
  if (player.decoder.offset >= tapEnd &&
      player.decoder.pulseIndex == player.decoder.pulseTicks) resetPlayer(20);
  noInterrupts();
  dmaCallbacks = 0;
  committedWords = 0;
  dmaUnderrun = false;
  lastDmaCallbackUs = 0;
  motorFell = false;
  interrupts();
  scheduledWords = 0;
  generatedTapBits = 0;
  runStartDecoder = player.decoder;
  // This rate is remembered by the core and applied during i2s_begin(),
  // before the first (otherwise fast) DMA descriptor can finish.
  i2s_set_rate(I2S_WORD_RATE);
#ifdef I2S_HAS_BEGIN_RXTX_DRIVE_CLOCKS
  if (!i2s_rxtxdrive_begin(false, true, false, false)) {
    state = State::ERROR;
    digitalWrite(PIN_SENSE, HIGH);
    debugE("I2S allocation failed");
    return;
  }
#else
  i2s_begin(); // GPIO2 is not connected to C64 WRITE.
#endif
  i2s_set_callback(onDmaBufferDone);
  realBitRate = (uint32_t)(i2s_get_real_rate() * 32.0f + 0.5f);
  state = State::STREAMING;
  debugI("Start TAP at %lu; I2S %lu bits/s",
         (unsigned long)player.decoder.offset, (unsigned long)realBitRate);
}

bool loadPulse() {
  if (player.decoder.offset >= tapEnd) return false;
  const uint32_t recordOffset = player.decoder.offset;
  uint32_t cycles;
  uint8_t value = readRomByte(draxcaught_tap + player.decoder.offset++);
  if (value != 0) {
    cycles = (uint32_t)value * 8;
  } else {
    if (tapEnd - player.decoder.offset < 3) {
      state = State::ERROR;
      debugE("Truncated TAP pulse at %lu", (unsigned long)recordOffset);
      return false;
    }
    cycles = (uint32_t)readRomByte(draxcaught_tap + player.decoder.offset) |
             ((uint32_t)readRomByte(draxcaught_tap + player.decoder.offset + 1) << 8) |
             ((uint32_t)readRomByte(draxcaught_tap + player.decoder.offset + 2) << 16);
    player.decoder.offset += 3;
  }
  if (cycles == 0) {
    state = State::ERROR;
    debugE("Zero-length TAP pulse at %lu", (unsigned long)recordOffset);
    return false;
  }
  // The remainder keeps the average length exact, including TAP v1 long gaps.
  cycles += player.decoder.cycleRemainder;
  player.decoder.pulseTicks = cycles / CYCLES_PER_BIT;
  player.decoder.cycleRemainder = cycles % CYCLES_PER_BIT;
  player.decoder.pulseIndex = 0;
  return true;
}

bool nextTapeBit(uint8_t &bit) {
  while (player.decoder.pulseIndex == player.decoder.pulseTicks) {
    if (!loadPulse()) return false;
  }
  bit = player.decoder.pulseIndex >= player.decoder.pulseTicks / 2 ? 1 : 0;
  ++player.decoder.pulseIndex;
  return true;
}

void pumpTape() {
  if (state != State::STREAMING && state != State::DRAINING) return;
  if (state == State::STREAMING && dmaUnderrun) {
    stopI2S();
    state = State::ERROR;
    digitalWrite(PIN_SENSE, HIGH);
    debugE("I2S underrun during TAP");
    return;
  }
  // Limit one loop iteration so HTTP and debug handlers remain responsive.
  for (uint8_t words = 0; words < 32; ++words) {
    if (i2s_is_full()) return;
    if (state == State::STREAMING && !player.wordPending) {
      if (scheduledWords % DMA_WORDS == 0) {
        const uint32_t buffer = scheduledWords / DMA_WORDS;
        const uint8_t slot = buffer % DMA_BUFFERS;
        checkpoints[slot] = player.decoder;
        checkpointNumber[slot] = buffer;
      }
      uint32_t word = 0;
      uint8_t bits = 0;
      uint8_t bit;
      while (bits < 32 && nextTapeBit(bit)) {
        word = (word << 1) | bit;
        ++bits;
        ++generatedTapBits;
      }
      if (state == State::ERROR) {
        stopI2S();
        digitalWrite(PIN_SENSE, HIGH);
        return;
      }
      if (bits == 0) {
        state = State::DRAINING;
        player.drainWordsLeft = DRAIN_WORDS;
      } else {
        player.pendingWord = word << (32 - bits);
        player.wordPending = true;
      }
    }
    if (player.wordPending) {
      if (!i2s_write_sample_nb(player.pendingWord)) return;
      player.wordPending = false;
      ++scheduledWords;
      committedWords = scheduledWords;
    } else if (state == State::DRAINING && player.drainWordsLeft != 0) {
      if (scheduledWords % DMA_WORDS == 0) {
        const uint32_t buffer = scheduledWords / DMA_WORDS;
        const uint8_t slot = buffer % DMA_BUFFERS;
        checkpoints[slot] = player.decoder;
        checkpointNumber[slot] = buffer;
      }
      if (!i2s_write_sample_nb(0)) return;
      ++scheduledWords;
      committedWords = scheduledWords;
      --player.drainWordsLeft;
    } else {
      break;
    }
  }
  if (state != State::DRAINING || player.drainWordsLeft != 0) return;
  if (!i2s_is_empty()) {
    player.emptySince = 0;
    return;
  }
  if (player.emptySince == 0) player.emptySince = micros();
  if ((uint32_t)(micros() - player.emptySince) >= DRAIN_GUARD_US) {
    stopI2S();
    state = State::FINISHED;
    debugI("TAP completed");
  }
}

void setupPins() {
  pinMode(PIN_MOTOR, INPUT);
  pinMode(2, INPUT_PULLUP); // GPIO2 must stay high at boot; WRITE is not connected.
  pinMode(PIN_SENSE, FUNCTION_3); // Release UART TX for GPIO1.
  digitalWrite(PIN_SENSE, HIGH); // Release PLAY until the embedded TAP is ready.
  pinMode(PIN_SENSE, OUTPUT);
  idleReadPin();
}

void connectWiFi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
#if defined(ESPSETTE_WIFI_SSID) && defined(ESPSETTE_WIFI_PASS)
  if (ESPSETTE_WIFI_SSID[0] == '\0') return;
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOST_NAME);
  WiFi.begin(ESPSETTE_WIFI_SSID, ESPSETTE_WIFI_PASS);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && (uint32_t)(millis() - started) < 20000) {
    delay(250);
  }
  networkReady = WiFi.status() == WL_CONNECTED;
#endif
}

void setupNetwork() {
  connectWiFi();
}

void setupWebserver() {
  if (!networkReady) return;
  HTTPServer.on("/", handleRoot);
  HTTPServer.onNotFound(handleNotFound);
  HTTPServer.begin();
}

void initializeOTA() {
  if (!networkReady) return;
#if defined(ESPSETTE_OTA_PASSWORD)
  if (ESPSETTE_OTA_PASSWORD[0] == '\0') return;
  ArduinoOTA.onStart([]() {
    debugI("* OTA: Start");
  });
  ArduinoOTA.onEnd([]() {
    debugI("\n*OTA: End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    const unsigned int percent = total ? (unsigned int)((uint64_t)progress * 100 / total) : 0;
    debugV("*OTA: Progress: %u%%\r", percent);
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
  ArduinoOTA.setPassword(ESPSETTE_OTA_PASSWORD);
  // Begin
  ArduinoOTA.begin();
  otaReady = true;
#endif
}

void setupDebug() {
  if (!networkReady) return;
  Debug.begin(HOST_NAME); // Initialize the WiFi server
  //Debug.setPassword("r3m0t0."); // Password on telnet connection ?
  Debug.setResetCmdEnabled(false); // Do not expose remote reset without authentication.
  Debug.showProfiler(true); // Profiler (Good to measure times, to optimize codes)
  Debug.showColors(true); // Colors
}

uint32_t lastcall;
uint8_t lastMotor = 0;

void setup() {
  setupPins();
  setupNetwork();
  initializeOTA();
  setupDebug();
  setupWebserver();
  resetPlayer(20);
  if (!validateTap()) {
    state = State::ERROR;
    digitalWrite(PIN_SENSE, HIGH);
    debugE("Invalid embedded C64 PAL TAP v1");
  } else {
    state = State::WAITING;
    digitalWrite(PIN_SENSE, LOW); // Virtual PLAY is pressed when data is ready.
  }
  lastMotor = digitalRead(PIN_MOTOR);
  attachInterrupt(digitalPinToInterrupt(PIN_MOTOR), onMotorFalling, FALLING);
  lastcall = millis();
  debugI("setup completed; TAP bytes=%lu", (unsigned long)tapEnd);
}

void debugGPIOs() {
  debugI("* GPIO0=%i GPIO1=%i GPIO2=%i GPIO3=%i",
         digitalRead(0), digitalRead(1), digitalRead(2), digitalRead(3));
}

void loop() {
  const uint32_t timeBeginLoop = millis();
  const uint8_t motor = digitalRead(PIN_MOTOR);
  if (motor != lastMotor) {
    lastMotor = motor;
    debugI("Motor state changed to %u", motor);
  }

  if (state == State::FINISHED) {
    if (motor == LOW) {
      resetPlayer(20);
      state = State::WAITING;
    }
  } else if (state == State::WAITING) {
    if (motor == HIGH) startTape();
  } else if (state == State::STREAMING || state == State::DRAINING) {
    if (motor == LOW) {
      if (!motorFell) { // Also handle a missed edge during startup.
        noInterrupts();
        motorFallCallbacks = dmaCallbacks;
        motorFallElapsedUs = micros() - lastDmaCallbackUs;
        interrupts();
      }
      pauseTape();
    } else {
      pumpTape();
    }
  }

  if (otaReady && state != State::STREAMING && state != State::DRAINING) ArduinoOTA.handle();
  if (networkReady) {
    HTTPServer.handleClient();
    Debug.handle();
  }
  yield();
  if ((uint32_t)(millis() - lastcall) >= 10000) {
    lastcall = millis();
    debugGPIOs();
  }
  const uint32_t elapsed = millis() - timeBeginLoop;
  if (elapsed > 200) debugW("Loop took %lu ms", (unsigned long)elapsed);
  else if (elapsed > 100) debugI("Loop took %lu ms", (unsigned long)elapsed);
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

  doc = "GPIO=["+String(g0)+","+String(g1)+","+String(g2)+","+String(g3)+"]\n";
  doc += "IP=" + WiFi.localIP().toString() + "\n";
  doc += "TAP bytes=" + String(tapEnd) + "\n";
  doc += "motor=" + String(g0) + "\n";
  doc += "player state=" + String(static_cast<uint8_t>(state)) + "\n";
  doc += "debug=RemoteDebug (TCP 23)\n";

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
 
