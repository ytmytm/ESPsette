// IDE settings

// Board: ESP01
// IDE settings: Generic ESP8266 module, 80MHz, vtables Flash, reset method: ck, crystal 26MHz, flash 40MHZ, flash mode QIO, 921600 on COM5
// as per https://arduino-esp8266.readthedocs.io/en/latest/boards.html#generic-esp8266-module 

// 1MB

// revised on 27.4, 1.5
#define PIN_MOTOR 0 // GPIO0       <- MOTOR (C,3) $01 bit 5 (through optocoupler with gnd pulldown 10k) // 0 = motor off, 1 = motor on
#define PIN_SENSE 1 // GPIO1 (TxD) -> SENSE (F,6) $01 bit 4 (with pullup to 3.3V, 10k) // 0 = pushed, 1 = released
#define PIN_READ  3 // GPIO3 (RxD) -> READ  (D,4) CIA#1 /FLAG, I2S, pulled to 5V in C64, normal high (input pullup), impulse low

#include <Arduino.h>

#if __has_include("local_ota.h")
#include "local_ota.h" // Optional local OTA secret; ignored by Git.
#endif

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include "network_config.h"
#include "tape_source.h"
#include "player_page.h"
#include <ArduinoJson.h>
#include <string.h>

#include <core_esp8266_i2s.h>

#include "draxcaught.h"

#include <RemoteDebug.h>

RemoteDebug Debug;
ESP8266WebServer HTTPServer(80);
NetworkConfig networkConfig(HTTPServer, PIN_MOTOR);
TapeSource tapeSource;

enum class State : uint8_t { WAITING, STREAMING, DRAINING, FINISHED, ERROR };

// One I2S sample contains 32 serial output bits. One bit represents 16 PAL cycles.
static const uint32_t TAP_CLOCK_HZ = 985248UL;
static const uint32_t CYCLES_PER_BIT = 16;
static const uint32_t I2S_WORD_RATE = TAP_CLOCK_HZ / CYCLES_PER_BIT / 32;
static const uint8_t DMA_WORDS = 64; // ESP8266 core: 8 DMA descriptors, 64 words each.
static const uint8_t DMA_BUFFERS = 8;
static const uint8_t CHECKPOINT_SLOTS = 16; // Keep played and queued buffers separate.
static const uint8_t DRAIN_WORDS = 64;
static const uint32_t DRAIN_GUARD_US = 40000;

State state = State::WAITING;
uint32_t tapEnd = 0;
bool tapeLoaded = false;
String tapeName = "draxcaught (demo)";
uint64_t totalTapeCycles = 0;
uint64_t reportedTotalCycles = 0;
uint64_t playedBaseCycles = 0;
const char* playerError = nullptr;
uint8_t uploadError = 0;
uint16_t uploadBytes = 0;
bool uploadSeen = false;
static const uint16_t MAX_HTTP_CHUNK = 1024;
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
// buffer. Sixteen slots retain checkpoints across the eight DMA buffers.
DecoderState checkpoints[CHECKPOINT_SLOTS];
DecoderState runStartDecoder;
uint32_t checkpointNumber[CHECKPOINT_SLOTS];
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
  totalTapeCycles = 0;
  while (cursor < tapEnd) {
    uint8_t value = readRomByte(draxcaught_tap + cursor);
    if (value != 0) {
      totalTapeCycles += (uint32_t)value * 8;
      ++cursor;
    } else {
      if (tapEnd - cursor < 4) return false;
      uint32_t cycles = (uint32_t)readRomByte(draxcaught_tap + cursor + 1) |
                        ((uint32_t)readRomByte(draxcaught_tap + cursor + 2) << 8) |
                        ((uint32_t)readRomByte(draxcaught_tap + cursor + 3) << 16);
      if (cycles == 0) return false;
      totalTapeCycles += cycles;
      cursor += 4;
    }
  }
  reportedTotalCycles = totalTapeCycles;
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
    if (totalTapeCycles) playedBaseCycles = totalTapeCycles;
    else playedBaseCycles += (uint64_t)generatedTapBits * CYCLES_PER_BIT;
    if (tapeSource.kind() == TapeSource::Kind::STREAMED) {
      tapeSource.eject();
      tapeLoaded = false;
      tapEnd = 0;
    }
    resetPlayer(20);
    state = State::WAITING;
    debugI("TAP completed at MOTOR stop");
    return;
  }
  if (playedBits > generatedTapBits) {
    state = State::ERROR; // DMA ran ahead of the source; LOAD is invalid.
    playerError = "i2s_underrun";
    debugE("I2S underrun before MOTOR stop");
    return;
  }
  playedBaseCycles += (uint64_t)playedBits * CYCLES_PER_BIT;
  if (playedBits == 0) player.decoder = runStartDecoder;
  if (playedBits != 0) {
    const uint32_t buffer = playedBits / (DMA_WORDS * 32);
    const uint32_t withinBuffer = playedBits % (DMA_WORDS * 32);
    const uint8_t slot = buffer % CHECKPOINT_SLOTS;
    if (checkpointNumber[slot] != buffer) {
      state = State::ERROR;
      playerError = "missing_checkpoint";
      debugE("Missing DMA checkpoint at %lu", (unsigned long)buffer);
      return;
    }
    player.decoder = checkpoints[slot];
    uint8_t bit;
    for (uint32_t i = 0; i < withinBuffer; ++i) {
      if (!nextTapeBit(bit)) {
        state = State::ERROR;
        playerError = "restore_failed";
        debugE("Cannot restore TAP bit %lu", (unsigned long)playedBits);
        return;
      }
    }
  }
  tapeSource.releaseBefore(player.decoder.offset);
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
    playerError = "i2s_alloc_failed";
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
  uint8_t value;
  if (!tapeSource.read(player.decoder.offset, value)) {
    state = State::ERROR;
    playerError = "buffer_underrun";
    debugE("TAP source exhausted at %lu", (unsigned long)recordOffset);
    return false;
  }
  ++player.decoder.offset;
  if (value != 0) {
    cycles = (uint32_t)value * 8;
  } else {
    if (tapEnd - player.decoder.offset < 3) {
      state = State::ERROR;
      playerError = "truncated_pulse";
      debugE("Truncated TAP pulse at %lu", (unsigned long)recordOffset);
      return false;
    }
    uint8_t lo, mid, hi;
    if (!tapeSource.read(player.decoder.offset, lo) ||
        !tapeSource.read(player.decoder.offset + 1, mid) ||
        !tapeSource.read(player.decoder.offset + 2, hi)) {
      state = State::ERROR;
      playerError = "buffer_underrun";
      debugE("TAP source exhausted at %lu", (unsigned long)recordOffset);
      return false;
    }
    cycles = (uint32_t)lo | ((uint32_t)mid << 8) | ((uint32_t)hi << 16);
    player.decoder.offset += 3;
  }
  if (cycles == 0) {
    state = State::ERROR;
    playerError = "zero_pulse";
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
    playerError = "i2s_underrun";
    debugE("I2S underrun during TAP");
    return;
  }
  // Release only bytes preceding the earliest DMA checkpoint that can still
  // be needed when MOTOR falls. The browser can refill that space immediately.
  const uint32_t callbacks = dmaCallbacks;
  if (tapeSource.kind() == TapeSource::Kind::STREAMED && callbacks >= DMA_BUFFERS) {
    const uint32_t buffer = callbacks - DMA_BUFFERS;
    const uint8_t slot = buffer % CHECKPOINT_SLOTS;
    if (checkpointNumber[slot] == buffer)
      tapeSource.releaseBefore(checkpoints[slot].offset);
  }
  // Limit one loop iteration so HTTP and debug handlers remain responsive.
  for (uint8_t words = 0; words < 32; ++words) {
    if (i2s_is_full()) return;
    if (state == State::STREAMING && !player.wordPending) {
      if (scheduledWords % DMA_WORDS == 0) {
        const uint32_t buffer = scheduledWords / DMA_WORDS;
        const uint8_t slot = buffer % CHECKPOINT_SLOTS;
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
        const uint8_t slot = buffer % CHECKPOINT_SLOTS;
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
    if (totalTapeCycles) playedBaseCycles = totalTapeCycles;
    else playedBaseCycles += (uint64_t)generatedTapBits * CYCLES_PER_BIT;
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

void setupNetwork() {
  networkConfig.begin();
  networkReady = networkConfig.staConnected();
}

void setupWebserver() {
  HTTPServer.on("/", handleRoot);
  HTTPServer.on("/player", HTTP_GET, handlePlayer);
  HTTPServer.on("/state", HTTP_GET, handleState);
  HTTPServer.on("/session", HTTP_POST, handleSession);
  HTTPServer.on("/seek", HTTP_POST, handleSeek);
  HTTPServer.on("/chunk", HTTP_POST, handleChunk, handleChunkUpload);
  HTTPServer.on("/eject", HTTP_POST, handleEject);
  HTTPServer.on("/demo", HTTP_POST, handleDemo);
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
  ArduinoOTA.setHostname(networkConfig.hostname().c_str());
  ArduinoOTA.setPassword(ESPSETTE_OTA_PASSWORD);
  // Begin
  ArduinoOTA.begin();
  otaReady = true;
#endif
}

void setupDebug() {
  if (!networkReady) return;
  Debug.begin(networkConfig.hostname().c_str()); // Initialize the WiFi server
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
  tapeSource.useEmbedded(draxcaught_tap, draxcaught_tap_len);
  resetPlayer(20);
  if (!validateTap()) {
    state = State::ERROR;
    playerError = "invalid_demo_tap";
    tapeSource.eject();
    debugE("Invalid embedded C64 PAL TAP v1");
  } else {
    state = State::WAITING;
    tapeLoaded = true;
  }
  digitalWrite(PIN_SENSE, LOW); // PLAY stays pressed even with an empty deck.
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
      if (tapeSource.kind() == TapeSource::Kind::STREAMED) {
        tapeSource.eject();
        tapeLoaded = false;
        tapEnd = 0;
      }
      resetPlayer(20);
      playedBaseCycles = 0;
      state = State::WAITING;
    }
  } else if (state == State::ERROR && motor == LOW &&
             tapeSource.kind() == TapeSource::Kind::STREAMED) {
    tapeSource.eject();
    tapeLoaded = false;
    tapEnd = 0;
  } else if (state == State::WAITING) {
    if (motor == HIGH && tapeLoaded) startTape();
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
  HTTPServer.handleClient();
  networkConfig.handle(motor == HIGH);
  if (networkReady) Debug.handle();
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
  if (networkConfig.apActive()) {
    HTTPServer.sendHeader("Location", "/wifi");
    HTTPServer.send(302, "text/plain", "");
    return;
  }
  handlePlayer();
}

void handlePlayer() {
  if (state == State::STREAMING || state == State::DRAINING) {
    HTTPServer.send(503, "text/plain", "Open the player before LOAD");
    return;
  }
  HTTPServer.sendHeader("Cache-Control", "no-store");
  HTTPServer.send_P(200, "text/html; charset=utf-8", PLAYER_PAGE);
}

 void handleNotFound(){
     if (networkConfig.apActive()) {
       HTTPServer.sendHeader("Location", "/wifi");
       HTTPServer.send(302, "text/plain", "");
       return;
     }
     HTTPServer.send(404, "text/plain", "File Not Found\n");
 }

// The session protocol uses short requests. HTTPUpload buffers at most 2048
// bytes; the browser deliberately sends at most MAX_HTTP_CHUNK per request.
static bool parseDecimal(const String& input, uint32_t& output) {
  if (input.length() == 0 || input.length() > 10) return false;
  uint32_t value = 0;
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    if (c < '0' || c > '9' || value > (UINT32_MAX - (c - '0')) / 10) return false;
    value = value * 10 + (c - '0');
  }
  output = value;
  return true;
}

static bool parseDecimal64(const String& input, uint64_t& output) {
  if (input.length() == 0 || input.length() > 20) return false;
  uint64_t value = 0;
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    if (c < '0' || c > '9' || value > (UINT64_MAX - (c - '0')) / 10) return false;
    value = value * 10 + (c - '0');
  }
  output = value;
  return true;
}

static uint32_t newSessionId(uint32_t previous) {
  uint32_t id = (micros() ^ ESP.getChipId() ^
                 (uint32_t)random(1, 0x7fffffff)) | 1U;
  if (id == previous) id += 2;
  return id;
}

static int8_t hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool decodeHeader(const String& hex, uint8_t header[20]) {
  if (hex.length() != 40) return false;
  for (uint8_t i = 0; i < 20; ++i) {
    const int8_t high = hexDigit(hex[i * 2]);
    const int8_t low = hexDigit(hex[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    header[i] = (high << 4) | low;
  }
  return true;
}

static bool validStreamHeader(const uint8_t header[20], uint32_t size) {
  static const char signature[] = "C64-TAPE-RAW";
  if (size <= 20 || memcmp(header, signature, 12) != 0 ||
      header[12] != 1 || header[13] != 0 || header[14] != 0) return false;
  const uint32_t dataSize = (uint32_t)header[16] |
                            ((uint32_t)header[17] << 8) |
                            ((uint32_t)header[18] << 16) |
                            ((uint32_t)header[19] << 24);
  return dataSize == size - 20;
}

static uint32_t currentRunBits() {
  if (state != State::STREAMING && state != State::DRAINING) return 0;
  noInterrupts();
  const uint32_t callbacks = dmaCallbacks;
  const uint32_t lastCallbackUs = lastDmaCallbackUs;
  interrupts();
  if (callbacks < DMA_BUFFERS || lastCallbackUs == 0) return 0;
  const uint32_t within = min((uint32_t)(DMA_WORDS * 32 - 1),
                              (uint32_t)(((uint64_t)(micros() - lastCallbackUs) *
                                          realBitRate) / 1000000ULL));
  return min(generatedTapBits,
             (callbacks - DMA_BUFFERS) * DMA_WORDS * 32 + within);
}

void handleState() {
  StaticJsonDocument<384> doc;
  const bool motorOn = digitalRead(PIN_MOTOR) == HIGH;
  doc["tapeState"] = tapeLoaded ? "loaded" : "ejected";
  doc["fileName"] = tapeLoaded ? tapeName : "";
  doc["motorOn"] = motorOn;
  doc["sensePressed"] = digitalRead(PIN_SENSE) == LOW;
  doc["playing"] = motorOn && tapeLoaded &&
      (state == State::STREAMING || state == State::DRAINING);
  const char* phase = "idle";
  if (state == State::STREAMING || state == State::DRAINING) phase = "data";
  else if (state == State::FINISHED) phase = "done";
  else if (state == State::ERROR) phase = "error";
  else if (playedBaseCycles != 0 && !motorOn) phase = "paused";
  doc["phase"] = phase;
  uint64_t played = playedBaseCycles + (uint64_t)currentRunBits() * CYCLES_PER_BIT;
  if (totalTapeCycles && played > totalTapeCycles) played = totalTapeCycles;
  doc["playedCycles"] = (double)played;
  if (reportedTotalCycles) doc["totalCycles"] = (double)reportedTotalCycles;
  else doc["totalCycles"] = nullptr;
  doc["bufferedMillis"] = nullptr; // TAP bytes have variable pulse durations.
  doc["fileOffsetReceived"] = tapeSource.kind() == TapeSource::Kind::STREAMED ?
      tapeSource.nextOffset() : 0;
  doc["freeBytes"] = tapeSource.freeBytes();
  doc["sessionId"] = tapeSource.sessionId();
  doc["error"] = playerError;
  String response;
  serializeJson(doc, response);
  HTTPServer.sendHeader("Cache-Control", "no-store");
  HTTPServer.send(200, "application/json", response);
}

void handleSession() {
  uint32_t size;
  uint64_t cycles = 0;
  uint8_t header[20];
  if (digitalRead(PIN_MOTOR) == HIGH ||
      state == State::STREAMING || state == State::DRAINING) {
    HTTPServer.send(409, "text/plain", "Stop MOTOR before changing the tape");
    return;
  }
  if (!parseDecimal(HTTPServer.arg("size"), size) ||
      !decodeHeader(HTTPServer.arg("header"), header) ||
      !validStreamHeader(header, size) ||
      HTTPServer.arg("name").length() > 64 ||
      (HTTPServer.hasArg("cycles") &&
       !parseDecimal64(HTTPServer.arg("cycles"), cycles))) {
    HTTPServer.send(400, "text/plain", "Only C64 PAL TAP v1 is supported");
    return;
  }
  const uint32_t id = newSessionId(0);
  tapeSource.useStream(header, size, id);
  tapeName = HTTPServer.arg("name");
  tapEnd = size;
  tapeLoaded = false;
  totalTapeCycles = 0;
  reportedTotalCycles = cycles; // Browser metadata is display-only.
  playedBaseCycles = 0;
  playerError = nullptr;
  state = State::WAITING;
  resetPlayer(20);
  StaticJsonDocument<128> reply;
  reply["sessionId"] = id;
  reply["nextOffset"] = 20;
  reply["freeBytes"] = tapeSource.freeBytes();
  String response;
  serializeJson(reply, response);
  HTTPServer.send(201, "application/json", response);
}

void handleSeek() {
  uint32_t id, offset;
  uint64_t remainingCycles;
  if (digitalRead(PIN_MOTOR) == HIGH ||
      state == State::STREAMING || state == State::DRAINING) {
    HTTPServer.send(409, "text/plain", "Stop MOTOR before seeking");
    return;
  }
  if (!parseDecimal(HTTPServer.arg("sid"), id) ||
      !parseDecimal(HTTPServer.arg("offset"), offset) ||
      !parseDecimal64(HTTPServer.arg("cycles"), remainingCycles) ||
      remainingCycles == 0 || tapeSource.kind() != TapeSource::Kind::STREAMED ||
      id != tapeSource.sessionId() || offset < 20 || offset >= tapEnd) {
    HTTPServer.send(400, "text/plain", "Invalid session or TAP offset");
    return;
  }
  // The browser has scanned the source TAP and checked that this IDX offset
  // is a record boundary. The ESP only holds a sliding window of that file.
  const uint32_t nextId = newSessionId(id);
  if (!tapeSource.seek(offset, nextId)) {
    HTTPServer.send(400, "text/plain", "Invalid TAP offset");
    return;
  }
  tapeLoaded = false;
  totalTapeCycles = 0;
  reportedTotalCycles = remainingCycles; // Display metadata, not timing control.
  playedBaseCycles = 0;
  playerError = nullptr;
  uploadSeen = false;
  state = State::WAITING;
  resetPlayer(offset);
  StaticJsonDocument<128> reply;
  reply["sessionId"] = nextId;
  reply["nextOffset"] = offset;
  reply["freeBytes"] = tapeSource.freeBytes();
  reply["tapeState"] = "ejected";
  String response;
  serializeJson(reply, response);
  HTTPServer.send(200, "application/json", response);
}

void handleChunkUpload() {
  HTTPUpload& upload = HTTPServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadBytes = 0;
    uploadSeen = true;
    uint32_t id, offset;
    uploadError = !parseDecimal(HTTPServer.arg("sid"), id) ||
                  !parseDecimal(HTTPServer.arg("offset"), offset) ||
                  tapeSource.kind() != TapeSource::Kind::STREAMED ||
                  id != tapeSource.sessionId() || offset != tapeSource.nextOffset() ? 1 : 0;
  } else if (upload.status == UPLOAD_FILE_WRITE && uploadError == 0) {
    if (upload.currentSize == 0 ||
        uploadBytes + upload.currentSize > MAX_HTTP_CHUNK ||
        !tapeSource.append(tapeSource.nextOffset(), upload.buf, upload.currentSize)) {
      uploadError = 2;
    } else {
      uploadBytes += upload.currentSize;
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadError = 3;
  }
}

void handleChunk() {
  if (!uploadSeen || (uploadError == 0 && uploadBytes == 0)) uploadError = 3;
  if (uploadError == 0 && !tapeLoaded &&
      (tapeSource.bufferedBytes() >= 4096 || tapeSource.nextOffset() == tapEnd)) {
    tapeLoaded = true;
  }
  StaticJsonDocument<128> reply;
  reply["nextOffset"] = tapeSource.nextOffset();
  reply["freeBytes"] = tapeSource.freeBytes();
  reply["loaded"] = tapeLoaded;
  if (uploadError) reply["error"] = uploadError;
  String response;
  serializeJson(reply, response);
  HTTPServer.send(uploadError ? 409 : 200, "application/json", response);
  uploadSeen = false;
}

void handleEject() {
  if (digitalRead(PIN_MOTOR) == HIGH) {
    HTTPServer.send(409, "text/plain", "Stop MOTOR before ejecting");
    return;
  }
  tapeSource.eject();
  tapeLoaded = false;
  tapEnd = 0;
  totalTapeCycles = 0;
  reportedTotalCycles = 0;
  playedBaseCycles = 0;
  playerError = nullptr;
  state = State::WAITING;
  resetPlayer(20);
  HTTPServer.send(200, "application/json", "{\"tapeState\":\"ejected\"}");
}

void handleDemo() {
  if (digitalRead(PIN_MOTOR) == HIGH) {
    HTTPServer.send(409, "text/plain", "Stop MOTOR before inserting the demo");
    return;
  }
  tapeSource.useEmbedded(draxcaught_tap, draxcaught_tap_len);
  tapeName = "draxcaught (demo)";
  playedBaseCycles = 0;
  playerError = nullptr;
  resetPlayer(20);
  tapeLoaded = validateTap();
  state = tapeLoaded ? State::WAITING : State::ERROR;
  HTTPServer.send(tapeLoaded ? 200 : 500, "application/json",
                  tapeLoaded ? "{\"tapeState\":\"loaded\"}" : "{\"error\":\"invalid_demo\"}");
}
