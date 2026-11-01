// IDE settings

// Board: ESP01
// IDE settings: Generic ESP8266 module, 80MHz, vtables Flash, reset method: ck, crystal 26MHz, flash 40MHZ, flash mode QIO, 921600 on COM5
// as per https://arduino-esp8266.readthedocs.io/en/latest/boards.html#generic-esp8266-module 

// 1MB

// revised on 27.4, 1.5
#define PIN_MOTOR 0 // GPIO0       <- MOTOR (C,3) $01 bit 5 (through optocoupler with gnd pulldown 10k) // 0 = motor off, 1 = motor on
#define PIN_SENSE 1 // GPIO1 (TxD) -> SENSE (F,6) $01 bit 4 (with pullup to 3.3V, 10k) // 0 = pushed, 1 = released
#define PIN_WRITE 2 // GPIO2 <- buffered C64 WRITE; optional diagnostic input only
#define PIN_READ  3 // GPIO3 (RxD) -> READ  (D,4) CIA#1 /FLAG, I2S, pulled to 5V in C64, normal high (input pullup), impulse low

#include <Arduino.h>

#if __has_include("local_ota.h")
#include "local_ota.h" // Optional local OTA secret; ignored by Git.
#endif

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#ifndef ESPSETTE_OTA
#define ESPSETTE_OTA 0
#endif
#if ESPSETTE_OTA
#include <ArduinoOTA.h>
#endif
#if ESPSETTE_WIFI_DISABLED
#include <user_interface.h>
#endif
#include "network_config.h"
#include "tape_source.h"
#include "prg_encoder.h"
#include "write_decoder.h"
#include "player_page.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <math.h>
#include <pgmspace.h>
#include <string.h>

#include <core_esp8266_i2s.h>

#include "draxcaught.h"

#include <RemoteDebug.h>

#ifndef ESPSETTE_GPIO_DISABLED
#define ESPSETTE_GPIO_DISABLED 0
#endif
#ifndef ESPSETTE_WIFI_DISABLED
#define ESPSETTE_WIFI_DISABLED 0
#endif
#if ESPSETTE_GPIO_DISABLED
// Bench: soft SENSE via PLAY/STOP; soft MOTOR stays on (as if the C64 asserted
// it). Tape only runs while both motor and PLAY are active.
static bool softSensePressed = false;
static bool softMotorOn = true;

static inline void setSensePressed(bool pressed) { softSensePressed = pressed; }
static inline bool senseIsPressed() { return softSensePressed; }
static inline bool motorIsOn() { return softMotorOn; }
static inline bool motorBlocksEdit() { return false; }
#else
static inline void setSensePressed(bool pressed) {
  digitalWrite(PIN_SENSE, pressed ? LOW : HIGH);
}
static inline bool senseIsPressed() { return digitalRead(PIN_SENSE) == LOW; }
static inline bool motorIsOn() { return digitalRead(PIN_MOTOR) == HIGH; }
static inline bool motorBlocksEdit() { return motorIsOn(); }
#endif

static inline bool tapeShouldRun() {
  return motorIsOn() && senseIsPressed();
}

RemoteDebug Debug;
ESP8266WebServer HTTPServer(80);
NetworkConfig networkConfig(HTTPServer, PIN_MOTOR);
TapeSource tapeSource;
PrgEncoder prgEncoder;
WriteDecoder writeDecoder;
#ifndef ESPSETTE_WRITE_CAPTURE
#define ESPSETTE_WRITE_CAPTURE 0
#endif
#if ESPSETTE_WRITE_CAPTURE && !defined(I2S_HAS_BEGIN_RXTX_DRIVE_CLOCKS)
#error "WRITE capture requires I2S without GPIO2 clock output"
#endif
#if ESPSETTE_WRITE_CAPTURE
static constexpr uint16_t WRITE_RING_SIZE = 512;
volatile uint16_t writeRing[WRITE_RING_SIZE];
volatile uint16_t writeHead = 0, writeTail = 0;
volatile uint32_t writeLastEdgeUs = 0, writeCaptureOverflows = 0;
volatile bool writeActive = false;
uint32_t reportedWriteOverflows = 0;

void IRAM_ATTR onWriteEdge() {
  if (!writeActive) return;
  const uint32_t now = micros();
  const uint32_t previous = writeLastEdgeUs;
  writeLastEdgeUs = now;
  if (!previous) return;
  const uint16_t next = (writeHead + 1) % WRITE_RING_SIZE;
  if (next == writeTail) { ++writeCaptureOverflows; return; }
  const uint32_t duration = now - previous;
  writeRing[writeHead] = duration > UINT16_MAX ? UINT16_MAX : duration;
  writeHead = next;
}

void drainWriteCapture() {
  uint32_t overflows;
  noInterrupts(); overflows = writeCaptureOverflows; interrupts();
  if (overflows != reportedWriteOverflows) {
    reportedWriteOverflows = overflows;
    noInterrupts(); writeTail = writeHead; interrupts();
    writeDecoder.overflow();
  }
  for (uint16_t count = 0; count < WRITE_RING_SIZE; ++count) {
    noInterrupts();
    if (writeTail == writeHead) { interrupts(); break; }
    const uint16_t duration = writeRing[writeTail];
    writeTail = (writeTail + 1) % WRITE_RING_SIZE;
    interrupts();
    writeDecoder.feedMicros(duration);
  }
}
#endif
enum class SourceFormat : uint8_t { TAP, PRG };
SourceFormat sourceFormat = SourceFormat::TAP;

enum class State : uint8_t { WAITING, STREAMING, DRAINING, FINISHED, ERROR };

// One I2S sample contains 32 serial output bits. One bit represents 16 PAL cycles.
static const uint32_t C64_CLOCK_HZ = 985248UL;
static const uint32_t C16_CLOCK_HZ = 886724UL;
uint32_t sourceClockHz = C64_CLOCK_HZ;
bool tapHalfWaves = false; // TAP v2 uses successive halfwaves.
static const uint32_t CYCLES_PER_BIT = 16;
static const uint8_t DMA_WORDS = 64; // ESP8266 core: 8 DMA descriptors, 64 words each.
static const uint8_t DMA_BUFFERS = 8;
static const uint8_t CHECKPOINT_SLOTS = 16; // Keep played and queued buffers separate.
static const uint8_t DRAIN_WORDS = 64;
static const uint32_t DRAIN_GUARD_US = 40000;

State state = State::WAITING;
uint32_t tapEnd = 0;
bool tapeLoaded = false;
bool autoPlay = true;
bool defaultC16 = false;
bool playbackComplete = false;
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
uint32_t i2sUnderrunCount = 0;
uint32_t sourceUnderrunCount = 0;
uint32_t uploadErrorCount = 0;
uint32_t maxLoopUs = 0;
uint32_t maxHttpUs = 0;

static inline uint8_t readRomByte(const uint8_t* addr) {
  return pgm_read_byte(addr);
}

struct DecoderState {
  uint32_t offset;       // Next TAP record or PRG logical byte still needed.
  uint32_t prgPulseIndex; // Next pulse in the generated PRG cassette stream.
  uint32_t pulseTicks;
  uint32_t pulseIndex;
  uint8_t cycleRemainder;
  uint8_t halfWaveLevel; // Checkpointed across MOTOR pauses.
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
#if ESPSETTE_GPIO_DISABLED
uint32_t softPlayLastUs = 0;
#endif

// Downsampled READ levels for the web quasi-oscilloscope (newest at the end).
static constexpr uint16_t SCOPE_LEN = 128;
static constexpr uint8_t SCOPE_EVERY_N_BITS = 16; // ~130 µs/sample at C64 rate
static uint8_t scopeBuf[SCOPE_LEN];
static uint16_t scopePos = 0;
static uint16_t scopeFilled = 0;
static uint8_t scopeBitSkip = 0;

static void scopePush(uint8_t bit) {
  if (++scopeBitSkip < SCOPE_EVERY_N_BITS) return;
  scopeBitSkip = 0;
  scopeBuf[scopePos] = bit ? 1 : 0;
  scopePos = (scopePos + 1) % SCOPE_LEN;
  if (scopeFilled < SCOPE_LEN) ++scopeFilled;
}

static void scopeClear() {
  scopePos = 0;
  scopeFilled = 0;
  scopeBitSkip = 0;
}

static bool stopPlaybackForEdit() {
  if (state != State::STREAMING && state != State::DRAINING) return true;
#if ESPSETTE_GPIO_DISABLED
  playedBaseCycles += (uint64_t)generatedTapBits * CYCLES_PER_BIT;
  generatedTapBits = 0;
  if (tapeSource.kind() == TapeSource::Kind::STREAMED)
    tapeSource.releaseBefore(player.decoder.offset);
  state = State::WAITING;
  return true;
#else
  return false;
#endif
}

void pauseTape();

// Pause READ output at the current position (MOTOR off or PLAY released).
static void pausePlaybackNow() {
  if (state != State::STREAMING && state != State::DRAINING) return;
#if ESPSETTE_GPIO_DISABLED
  playedBaseCycles += (uint64_t)generatedTapBits * CYCLES_PER_BIT;
  generatedTapBits = 0;
  if (tapeSource.kind() == TapeSource::Kind::STREAMED)
    tapeSource.releaseBefore(player.decoder.offset);
  state = State::WAITING;
#else
  noInterrupts();
  motorFallCallbacks = dmaCallbacks;
  motorFallElapsedUs = lastDmaCallbackUs ? micros() - lastDmaCallbackUs : 0;
  interrupts();
  pauseTape();
#endif
}

void handleRoot();
void handlePlayer();
void handleNotFound();
void handleState();
void handleSession();
void handleSeek();
void handleNav();
void handleChunk();
void handleChunkUpload();
void handleEject();
void handleSense();
void handleAutoPlay();
void handleDefaultMachine();
void handleDemo();

void IRAM_ATTR onDmaBufferDone() {
  ++dmaCallbacks;
  lastDmaCallbackUs = micros();
  if (dmaCallbacks >= DMA_BUFFERS &&
      committedWords + 1 < (dmaCallbacks - DMA_BUFFERS + 1) * DMA_WORDS) {
    dmaUnderrun = true;
  }
}

void IRAM_ATTR onMotorFalling() {
  motorFallCallbacks = dmaCallbacks;
  motorFallElapsedUs = micros() - lastDmaCallbackUs;
  motorFell = true;
}

void resetPlayer(uint32_t offset) {
  player.decoder.offset = offset;
  player.decoder.prgPulseIndex = 0;
  player.decoder.pulseTicks = 0;
  player.decoder.pulseIndex = 0;
  player.decoder.cycleRemainder = 0;
  player.decoder.halfWaveLevel = 0;
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
  totalTapeCycles = 0;
  reportedTotalCycles = 0;
  return true;
}

// Full PROGMEM walk only from the deferred demo path (never from setup/HTTP).
static bool scanEmbeddedTotalCycles() {
  uint32_t offset = 20;
  uint64_t acc = 0;
  uint16_t steps = 0;
  while (offset < tapEnd) {
    uint8_t value;
    if (!tapeSource.read(offset, value)) return false;
    ++offset;
    uint32_t cycles;
    if (value != 0) {
      cycles = (uint32_t)value * 8;
    } else {
      if (tapEnd - offset < 3) return false;
      uint8_t lo, mid, hi;
      if (!tapeSource.read(offset, lo) || !tapeSource.read(offset + 1, mid) ||
          !tapeSource.read(offset + 2, hi))
        return false;
      offset += 3;
      cycles = (uint32_t)lo | ((uint32_t)mid << 8) | ((uint32_t)hi << 16);
    }
    if (cycles == 0) return false;
    acc += cycles;
    if ((++steps & 127) == 0) {
      ESP.wdtFeed();
      yield();
    }
  }
  totalTapeCycles = acc;
  reportedTotalCycles = acc;
  return true;
}

static bool pendingDemoInsert = false;
static bool demoInsertFailed = false;

static void insertEmbeddedDemo() {
  // Keep this off the HTTP stack: PROGMEM reads thrash ICACHE and must yield
  // so the Wi-Fi/soft WDT keep running.
  ESP.wdtFeed();
  yield();
  sourceFormat = SourceFormat::TAP;
  sourceClockHz = C64_CLOCK_HZ;
  tapHalfWaves = false;
  tapeSource.useEmbedded(draxcaught_tap, draxcaught_tap_len);
  tapeName = "draxcaught (demo)";
  playedBaseCycles = 0;
  playbackComplete = false;
  playerError = nullptr;
  ESP.wdtFeed();
  yield();
  resetPlayer(20);
  ESP.wdtFeed();
  yield();
  bool ok = validateTap();
  ESP.wdtFeed();
  yield();
  if (ok) {
    ok = scanEmbeddedTotalCycles();
    ESP.wdtFeed();
    yield();
  }
  tapeLoaded = ok;
  setSensePressed(autoPlay && tapeLoaded);
  state = ok ? State::WAITING : State::ERROR;
  if (!ok) {
    playerError = "invalid_demo_tap";
    tapeSource.eject();
    demoInsertFailed = true;
  }
  debugI("Embedded demo %s; TAP bytes=%lu cycles=%lu",
         ok ? "loaded" : "invalid", (unsigned long)tapEnd,
         (unsigned long)reportedTotalCycles);
}

void idleReadPin() {
#if !ESPSETTE_GPIO_DISABLED
  pinMode(PIN_READ, OUTPUT);
  digitalWrite(PIN_READ, LOW); // The external transistor makes this READ high.
#endif
}

void stopI2S() {
#if !ESPSETTE_GPIO_DISABLED
  i2s_end();
  idleReadPin();
#endif
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
    playbackComplete = true;
    state = State::WAITING;
    debugI("TAP completed at MOTOR stop");
    return;
  }
  if (playedBits > generatedTapBits) {
    state = State::ERROR; // DMA ran ahead of the source; LOAD is invalid.
    playerError = "i2s_underrun";
    ++i2sUnderrunCount;
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
  if (playbackComplete) {
    playedBaseCycles = 0;
    playbackComplete = false;
  }
  if (sourceFormat == SourceFormat::TAP && player.decoder.offset >= tapEnd &&
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
  scopeClear();
  realBitRate = sourceClockHz / CYCLES_PER_BIT;
#if ESPSETTE_GPIO_DISABLED
  // Soft READ stream: advance the decoder on wall time for counter/scope UI.
  softPlayLastUs = micros();
  state = State::STREAMING;
  debugI("Start soft tape at %lu; %lu bits/s",
         (unsigned long)player.decoder.offset, (unsigned long)realBitRate);
  return;
#else
  // This rate is remembered by the core and applied during i2s_begin(),
  // before the first (otherwise fast) DMA descriptor can finish.
  i2s_set_rate(realBitRate / 32);
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
  debugI("Start tape at %lu; I2S %lu bits/s",
         (unsigned long)player.decoder.offset, (unsigned long)realBitRate);
#endif
}

bool loadPulse() {
  uint32_t cycles;
  if (sourceFormat == SourceFormat::PRG) {
    uint32_t sourceOffset;
    const PrgEncoder::Result result = prgEncoder.pulseAt(
        player.decoder.prgPulseIndex, tapeSource, cycles, sourceOffset);
    if (result == PrgEncoder::Result::END) return false;
    if (result == PrgEncoder::Result::MISSING_DATA) {
      state = State::ERROR;
      playerError = "buffer_underrun";
      ++sourceUnderrunCount;
      debugE("PRG source exhausted at logical offset %lu",
             (unsigned long)sourceOffset);
      return false;
    }
    player.decoder.offset = sourceOffset;
    ++player.decoder.prgPulseIndex;
  } else {
    if (player.decoder.offset >= tapEnd) return false;
    const uint32_t recordOffset = player.decoder.offset;
    uint8_t value;
    if (!tapeSource.read(player.decoder.offset, value)) {
      state = State::ERROR;
      playerError = "buffer_underrun";
      ++sourceUnderrunCount;
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
        ++sourceUnderrunCount;
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
  }
  // Keep the average timing exact across pulses, including PRG half cycles.
  cycles += player.decoder.cycleRemainder;
  player.decoder.pulseTicks = cycles / CYCLES_PER_BIT;
  player.decoder.cycleRemainder = cycles % CYCLES_PER_BIT;
  player.decoder.pulseIndex = 0;
  if (tapHalfWaves && player.decoder.pulseTicks == 0)
    player.decoder.halfWaveLevel ^= 1;
  return true;
}

bool nextTapeBit(uint8_t &bit) {
  while (player.decoder.pulseIndex == player.decoder.pulseTicks) {
    if (!loadPulse()) return false;
  }
  // GPIO3=0 is READ=1 through the external transistor. TAP v2 begins
  // after a 0->1 transition, so halfWaveLevel=0 is the first READ level.
  bit = tapHalfWaves ? player.decoder.halfWaveLevel :
      (player.decoder.pulseIndex >= player.decoder.pulseTicks / 2 ? 1 : 0);
  ++player.decoder.pulseIndex;
  if (tapHalfWaves && player.decoder.pulseIndex == player.decoder.pulseTicks)
    player.decoder.halfWaveLevel ^= 1;
  scopePush(bit);
  return true;
}

#if ESPSETTE_GPIO_DISABLED
void pumpTapeSoft() {
  if (state != State::STREAMING && state != State::DRAINING) return;
  const uint32_t now = micros();
  uint32_t elapsedUs = now - softPlayLastUs;
  softPlayLastUs = now;
  if (elapsedUs > 40000UL) elapsedUs = 40000UL; // Cap catch-up after HTTP stalls.
  if (realBitRate == 0) return;
  uint32_t wantBits = (uint32_t)(((uint64_t)elapsedUs * realBitRate) / 1000000ULL);
  if (wantBits > 4096) wantBits = 4096;
  for (uint32_t i = 0; i < wantBits; ++i) {
    uint8_t bit;
    if (!nextTapeBit(bit)) {
      if (state == State::ERROR) return;
      state = State::FINISHED;
      playbackComplete = true;
      if (totalTapeCycles) playedBaseCycles = totalTapeCycles;
      else playedBaseCycles += (uint64_t)generatedTapBits * CYCLES_PER_BIT;
      generatedTapBits = 0;
      debugI("Soft TAP completed");
      return;
    }
    ++generatedTapBits;
  }
  // Same ring contract as I2S: free consumed TAP bytes so HTTP can refill.
  if (tapeSource.kind() == TapeSource::Kind::STREAMED)
    tapeSource.releaseBefore(player.decoder.offset);
}
#endif

void pumpTape() {
  if (state != State::STREAMING && state != State::DRAINING) return;
#if ESPSETTE_GPIO_DISABLED
  pumpTapeSoft();
  return;
#endif
  if (state == State::STREAMING && dmaUnderrun) {
    stopI2S();
    state = State::ERROR;
    playerError = "i2s_underrun";
    ++i2sUnderrunCount;
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
    playbackComplete = true;
    if (totalTapeCycles) playedBaseCycles = totalTapeCycles;
    else playedBaseCycles += (uint64_t)generatedTapBits * CYCLES_PER_BIT;
    debugI("TAP completed");
  }
}

void setupPins() {
#if ESPSETTE_GPIO_DISABLED
  softSensePressed = false;
#else
  pinMode(PIN_MOTOR, INPUT);
  pinMode(PIN_WRITE, INPUT_PULLUP); // GPIO2 must stay high at boot.
  pinMode(PIN_SENSE, FUNCTION_3); // Release UART TX for GPIO1.
  digitalWrite(PIN_SENSE, HIGH); // Release PLAY until the embedded TAP is ready.
  pinMode(PIN_SENSE, OUTPUT);
  idleReadPin();
#endif
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
  HTTPServer.on("/nav", HTTP_POST, handleNav);
  HTTPServer.on("/chunk", HTTP_POST, handleChunk, handleChunkUpload);
  HTTPServer.on("/eject", HTTP_POST, handleEject);
  HTTPServer.on("/sense", HTTP_POST, handleSense);
  HTTPServer.on("/autoplay", HTTP_POST, handleAutoPlay);
  HTTPServer.on("/default-machine", HTTP_POST, handleDefaultMachine);
  HTTPServer.on("/demo", HTTP_POST, handleDemo);
  HTTPServer.onNotFound(handleNotFound);
  HTTPServer.begin();
}

void initializeOTA() {
#if !ESPSETTE_OTA
  (void)networkReady;
  otaReady = false;
#else
  if (!networkReady) return;
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
    debugE("*OTA: Error[%u]", error);
  });
  ArduinoOTA.setHostname(networkConfig.hostname().c_str());
#if defined(ESPSETTE_OTA_PASSWORD)
  if (ESPSETTE_OTA_PASSWORD[0] != '\0')
    ArduinoOTA.setPassword(ESPSETTE_OTA_PASSWORD);
#endif
  ArduinoOTA.begin();
  otaReady = true;
#endif
}

void setupDebug() {
  if (!networkReady) return;
  Debug.begin(networkConfig.hostname().c_str()); // Initialize the WiFi server
  Debug.setResetCmdEnabled(false); // Do not expose remote reset without authentication.
  Debug.showProfiler(true); // Profiler (Good to measure times, to optimize codes)
  Debug.showColors(true); // Colors
}

uint32_t lastcall;
uint8_t lastMotor = 0;

#if ESPSETTE_WIFI_DISABLED
// Linking ESP8266WiFi replaces the core's weak disable with a no-op that leaves
// RF on. Override it (needs -Wl,--allow-multiple-definition in platformio.ini).
extern "C" void __disableWiFiAtBootTime(void) {
  wifi_set_opmode_current(NULL_MODE);
  wifi_fpm_set_sleep_type(MODEM_SLEEP_T);
  wifi_fpm_open();
  wifi_fpm_do_sleep(0xFFFFFFF);
}
#endif

void setup() {
#if ESPSETTE_GPIO_DISABLED
  // Keep UART0 so bench can see a boot line at the ROM baud rate.
  Serial.begin(74880);
  delay(50);
  Serial.println();
  Serial.printf_P(PSTR("espsette boot gpio_dis=%d wifi_dis=%d\r\n"),
                  (int)ESPSETTE_GPIO_DISABLED, (int)ESPSETTE_WIFI_DISABLED);
#endif
  setupPins();
  setupNetwork();
  File autoPlayFile = LittleFS.open("/autoplay.cfg", "r");
  if (autoPlayFile) {
    autoPlay = autoPlayFile.read() != '0';
    autoPlayFile.close();
  }
  File machineFile = LittleFS.open("/machine.cfg", "r");
  if (machineFile) {
    if (machineFile.size() == 3) {
      char machine[4] = {};
      if (machineFile.readBytes(machine, 3) == 3)
        defaultC16 = strcmp(machine, "c16") == 0;
    }
    machineFile.close();
  }
#if !ESPSETTE_WIFI_DISABLED
  initializeOTA();
  setupDebug();
  setupWebserver();
#endif
  // Do not touch the ~250 KB PROGMEM demo in setup() — ICACHE thrash + WDT.
  // Demo is inserted from loop() after /demo (or auto-queued once below).
  state = State::WAITING;
  tapeLoaded = false;
  setSensePressed(false);
#if ESPSETTE_GPIO_DISABLED
  lastMotor = LOW;
#else
  lastMotor = digitalRead(PIN_MOTOR);
  attachInterrupt(digitalPinToInterrupt(PIN_MOTOR), onMotorFalling, FALLING);
#if ESPSETTE_WRITE_CAPTURE
  writeActive = lastMotor == HIGH;
  if (writeActive) writeDecoder.motorStart();
  attachInterrupt(digitalPinToInterrupt(PIN_WRITE), onWriteEdge, RISING);
#endif
#endif
  lastcall = millis();
  debugI("setup completed; TAP bytes=%lu", (unsigned long)tapEnd);
#if ESPSETTE_GPIO_DISABLED
  Serial.println(F("espsette setup done"));
#endif
}

void debugGPIOs() {
#if ESPSETTE_GPIO_DISABLED
  debugI("* GPIO disabled (soft sense=%i motor=%i)",
         senseIsPressed() ? 1 : 0, motorIsOn() ? 1 : 0);
#else
  debugI("* GPIO0=%i GPIO1=%i GPIO2=%i GPIO3=%i",
         digitalRead(0), digitalRead(1), digitalRead(2), digitalRead(3));
#endif
}

void loop() {
#if ESPSETTE_WIFI_DISABLED
  {
    static uint32_t lastHb;
    const uint32_t now = millis();
    if ((uint32_t)(now - lastHb) >= 1000) {
      lastHb = now;
      Serial.printf_P(PSTR("espsette hb %lu\r\n"), (unsigned long)now);
    }
  }
#endif
  if (pendingDemoInsert) {
    pendingDemoInsert = false;
    insertEmbeddedDemo();
  }
  const uint32_t loopStartUs = micros();
  const uint32_t timeBeginLoop = millis();
  const uint8_t motor = motorIsOn() ? HIGH : LOW;
  if (motor != lastMotor) {
    lastMotor = motor;
    debugI("Motor state changed to %u", motor);
#if ESPSETTE_WRITE_CAPTURE
    if (motor == HIGH) {
      noInterrupts(); writeLastEdgeUs = 0; writeActive = true; interrupts();
      writeDecoder.motorStart();
    } else {
      writeActive = false;
      drainWriteCapture();
      writeDecoder.motorStop();
    }
#endif
  }

  if (state == State::FINISHED) {
    // After end of tape, release PLAY (or MOTOR) before a fresh run from the start.
    if (!tapeShouldRun()) {
      if (tapeSource.kind() == TapeSource::Kind::STREAMED && !motorIsOn()) {
        tapeSource.eject();
        tapeLoaded = false;
        tapEnd = 0;
      }
      resetPlayer(20);
      playedBaseCycles = 0;
      playbackComplete = false;
      state = State::WAITING;
    }
  } else if (state == State::ERROR && !motorIsOn() &&
             tapeSource.kind() == TapeSource::Kind::STREAMED) {
    tapeSource.eject();
    tapeLoaded = false;
    tapEnd = 0;
  } else if (state == State::WAITING) {
    if (tapeShouldRun() && tapeLoaded) startTape();
  } else if (state == State::STREAMING || state == State::DRAINING) {
    if (!tapeShouldRun()) {
      if (motor == LOW && !motorFell) {
        noInterrupts();
        motorFallCallbacks = dmaCallbacks;
        motorFallElapsedUs = lastDmaCallbackUs ? micros() - lastDmaCallbackUs : 0;
        interrupts();
      }
      pausePlaybackNow();
    } else {
      pumpTape();
    }
  }

#if !ESPSETTE_WIFI_DISABLED
#if ESPSETTE_OTA
  if (otaReady && state != State::STREAMING && state != State::DRAINING) ArduinoOTA.handle();
#endif
  const uint32_t httpStartUs = micros();
  HTTPServer.handleClient();
  const uint32_t httpElapsedUs = micros() - httpStartUs;
  if (httpElapsedUs > maxHttpUs) maxHttpUs = httpElapsedUs;
  networkConfig.handle(motor == HIGH);
  if (networkReady) Debug.handle();
#endif
#if ESPSETTE_WRITE_CAPTURE
  drainWriteCapture();
#endif
  yield();
  if ((uint32_t)(millis() - lastcall) >= 10000) {
    lastcall = millis();
    debugGPIOs();
  }
  const uint32_t loopElapsedUs = micros() - loopStartUs;
  if (loopElapsedUs > maxLoopUs) maxLoopUs = loopElapsedUs;
  const uint32_t elapsed = millis() - timeBeginLoop;
  if (elapsed > 200) {
    debugW("Loop took %lu ms", (unsigned long)elapsed);
  } else if (elapsed > 100) {
    debugI("Loop took %lu ms", (unsigned long)elapsed);
  }
}

//// web

void handleRoot() {
  handlePlayer();
}

void handlePlayer() {
#if !ESPSETTE_GPIO_DISABLED
  // Serving the PROGMEM page while I2S drains can thrash ICACHE on ESP-01.
  if (state == State::STREAMING || state == State::DRAINING) {
    HTTPServer.send(503, "text/plain", "Open the player before LOAD");
    return;
  }
#endif
  HTTPServer.sendHeader("Cache-Control", "no-store");
  HTTPServer.send_P(200, "text/html; charset=utf-8", PLAYER_PAGE);
}

void handleNotFound() {
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
  const bool c64 = memcmp(header, "C64-TAPE-RAW", 12) == 0 && header[13] == 0;
  const bool c16 = memcmp(header, "C16-TAPE-RAW", 12) == 0 && header[13] == 2;
  if (size <= 20 || header[14] != 0 ||
      !((c64 || c16) && (header[12] == 1 || header[12] == 2))) return false;
  const uint32_t dataSize = (uint32_t)header[16] |
                            ((uint32_t)header[17] << 8) |
                            ((uint32_t)header[18] << 16) |
                            ((uint32_t)header[19] << 24);
  return dataSize == size - 20;
}

static uint32_t currentRunBits() {
  if (state != State::STREAMING && state != State::DRAINING) return 0;
#if ESPSETTE_GPIO_DISABLED
  return generatedTapBits;
#else
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
#endif
}

// VICE / Pi1551 datasette counter model (same constants as the player page).
static uint32_t tapeCounterAtCycles(uint64_t cycles, uint32_t clockHz) {
  if (clockHz == 0) return 0;
  const double seconds = (double)cycles / (double)clockHz;
  const double d = 1.27e-5, r = 1.07e-2, v = 4.76e-2, g = 0.525;
  const double turns = sqrt((r / d) * (r / d) + seconds * v / (d * 3.141592653589793));
  if (turns <= r / d) return 0;
  return (uint32_t)(g * (turns - r / d));
}

static uint64_t cyclesAtTapeCounter(uint32_t counter, uint32_t clockHz) {
  const double d = 1.27e-5, r = 1.07e-2, v = 4.76e-2, g = 0.525;
  const double turns = (double)counter / g + r / d;
  const double seconds = (turns * turns - (r / d) * (r / d)) * d * 3.141592653589793 / v;
  if (seconds <= 0) return 0;
  return (uint64_t)(seconds * (double)clockHz + 0.5);
}

// Walk TAP records until accumulated cycles reach target; yields for WDT/Wi-Fi.
static bool scanTapToCycles(uint64_t targetCycles, uint32_t& outOffset,
                            uint64_t& outCycles) {
  uint32_t offset = 20;
  uint64_t acc = 0;
  uint16_t steps = 0;
  while (offset < tapEnd && acc < targetCycles) {
    uint8_t value;
    if (!tapeSource.read(offset, value)) return false;
    ++offset;
    uint32_t cycles;
    if (value != 0) {
      cycles = (uint32_t)value * 8;
    } else {
      if (tapEnd - offset < 3) return false;
      uint8_t lo, mid, hi;
      if (!tapeSource.read(offset, lo) || !tapeSource.read(offset + 1, mid) ||
          !tapeSource.read(offset + 2, hi))
        return false;
      offset += 3;
      cycles = (uint32_t)lo | ((uint32_t)mid << 8) | ((uint32_t)hi << 16);
    }
    if (cycles == 0) return false;
    acc += cycles;
    if ((++steps & 63) == 0) {
      ESP.wdtFeed();
      yield();
    }
  }
  outOffset = offset;
  outCycles = acc;
  return true;
}

void handleState() {
  StaticJsonDocument<1536> doc;
  const bool motorOn = motorIsOn();
  doc["tapeState"] = pendingDemoInsert ? "loading"
      : (demoInsertFailed && !tapeLoaded) ? "error"
      : (tapeLoaded ? "loaded" : "ejected");
  doc["sourceType"] = sourceFormat == SourceFormat::PRG ? "prg" : "tap";
  doc["machine"] = sourceClockHz == C16_CLOCK_HZ ? "c16" : "c64";
  doc["clockHz"] = sourceClockHz;
  doc["fileName"] = tapeLoaded ? tapeName : "";
  doc["motorOn"] = motorOn;
  doc["sensePressed"] = senseIsPressed();
  doc["gpioDisabled"] = bool(ESPSETTE_GPIO_DISABLED);
  doc["autoPlay"] = autoPlay;
  doc["defaultMachine"] = defaultC16 ? "c16" : "c64";
  doc["playing"] = motorOn && senseIsPressed() && tapeLoaded &&
      (state == State::STREAMING || state == State::DRAINING);
  const char* phase = "idle";
  if (state == State::STREAMING || state == State::DRAINING)
    phase = sourceFormat == SourceFormat::PRG &&
            player.decoder.prgPulseIndex < prgEncoder.dataStartPulse() ? "header" : "data";
  else if (state == State::FINISHED || playbackComplete) phase = "done";
  else if (state == State::ERROR) phase = "error";
  else if (playedBaseCycles != 0 && !tapeShouldRun())
    phase = sourceFormat == SourceFormat::PRG &&
            player.decoder.prgPulseIndex >= prgEncoder.dataStartPulse() ?
            "headerPause" : "paused";
  doc["phase"] = phase;
  uint64_t played = playedBaseCycles + (uint64_t)currentRunBits() * CYCLES_PER_BIT;
  if (totalTapeCycles && played > totalTapeCycles) played = totalTapeCycles;
  doc["playedCycles"] = (double)played;
  if (reportedTotalCycles) doc["totalCycles"] = (double)reportedTotalCycles;
  else doc["totalCycles"] = nullptr;
  doc["bufferedMillis"] = nullptr; // TAP bytes have variable pulse durations.
  doc["embedded"] = tapeSource.kind() == TapeSource::Kind::EMBEDDED;
  if (tapeSource.kind() == TapeSource::Kind::STREAMED) {
    doc["fileOffsetReceived"] = tapeSource.nextOffset();
    doc["freeBytes"] = tapeSource.freeBytes();
    doc["logicalSize"] = tapeSource.size();
  } else if (tapeSource.kind() == TapeSource::Kind::EMBEDDED) {
    doc["fileOffsetReceived"] = tapEnd;
    doc["freeBytes"] = 0;
    doc["logicalSize"] = tapEnd;
  } else {
    doc["fileOffsetReceived"] = 0;
    doc["freeBytes"] = tapeSource.freeBytes();
    doc["logicalSize"] = 0;
  }
  doc["sessionId"] = tapeSource.sessionId();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["maxFreeBlock"] = ESP.getMaxFreeBlockSize();
  doc["maxLoopUs"] = maxLoopUs;
  doc["maxHttpUs"] = maxHttpUs;
  doc["i2sUnderruns"] = i2sUnderrunCount;
  doc["sourceUnderruns"] = sourceUnderrunCount;
  doc["uploadErrors"] = uploadErrorCount;
  doc["error"] = playerError;
  doc["writeEnabled"] = bool(ESPSETTE_WRITE_CAPTURE);
  doc["writeStatus"] = writeDecoder.status();
  doc["writeMachine"] = writeDecoder.machineName();
  doc["writeName"] = writeDecoder.name();
  doc["writeType"] = writeDecoder.fileType();
  doc["writeStart"] = writeDecoder.startAddress();
  doc["writeEnd"] = writeDecoder.endAddress();
  doc["writeHeaders"] = writeDecoder.receivedCount();
  doc["writeErrors"] = writeDecoder.errorCount();
  doc["writeOverflows"] = writeDecoder.overflowCount();
  doc["otaReady"] = otaReady;
  {
    char scopeStr[SCOPE_LEN + 1];
    const uint16_t n = scopeFilled;
    for (uint16_t i = 0; i < n; ++i) {
      const uint16_t idx = (scopePos + SCOPE_LEN - n + i) % SCOPE_LEN;
      scopeStr[i] = scopeBuf[idx] ? '1' : '0';
    }
    scopeStr[n] = '\0';
    doc["scope"] = scopeStr;
    doc["scopeHz"] = realBitRate ? realBitRate / SCOPE_EVERY_N_BITS : 0;
  }
  if (doc.overflowed()) {
    HTTPServer.send(500, "application/json", "{\"error\":\"state_overflow\"}");
    return;
  }
  String response;
  serializeJson(doc, response);
  HTTPServer.sendHeader("Cache-Control", "no-store");
  HTTPServer.send(200, "application/json", response);
}

void handleSession() {
  uint32_t fileSize;
  uint64_t cycles = 0;
  uint8_t prefix[20] = {};
  if (motorBlocksEdit() || !stopPlaybackForEdit()) {
    HTTPServer.send(409, "text/plain", "Stop MOTOR before changing the tape");
    return;
  }
  const String type = HTTPServer.arg("type");
  const bool isPrg = type == "prg";
  if ((type.length() && type != "tap" && !isPrg) ||
      !parseDecimal(HTTPServer.arg("size"), fileSize) ||
      HTTPServer.arg("name").length() > 64) {
    HTTPServer.send(400, "text/plain", "Invalid file type or size");
    return;
  }
  uint32_t logicalSize;
  uint32_t firstOffset;
  if (isPrg) {
    const String addressHex = HTTPServer.arg("header");
    if (fileSize < 3 || addressHex.length() != 4 ||
        (HTTPServer.arg("machine") != "c64" && HTTPServer.arg("machine") != "c16") ||
        HTTPServer.arg("turbo") != "0") {
      HTTPServer.send(400, "text/plain", "Only standard C64/C16 PRG is supported");
      return;
    }
    for (uint8_t i = 0; i < 2; ++i) {
      const int8_t high = hexDigit(addressHex[i * 2]);
      const int8_t low = hexDigit(addressHex[i * 2 + 1]);
      if (high < 0 || low < 0) {
        HTTPServer.send(400, "text/plain", "Invalid PRG load address");
        return;
      }
      prefix[i] = (high << 4) | low;
    }
    const uint32_t dataLength = fileSize - 2;
    const uint16_t loadAddress = prefix[0] | ((uint16_t)prefix[1] << 8);
    const String displayName = HTTPServer.arg("displayName");
    const PrgEncoder::Machine machine = HTTPServer.arg("machine") == "c16" ?
        PrgEncoder::Machine::C16 : PrgEncoder::Machine::C64;
    if (displayName.length() > 64 ||
        !prgEncoder.begin(loadAddress, dataLength, displayName, machine)) {
      HTTPServer.send(400, "text/plain", "PRG address range is invalid");
      return;
    }
    logicalSize = 2 + 2 * dataLength;
    firstOffset = 2;
    totalTapeCycles = prgEncoder.totalCycles();
    reportedTotalCycles = totalTapeCycles;
    sourceClockHz = prgEncoder.clockHz();
    tapHalfWaves = false;
  } else {
    if (!decodeHeader(HTTPServer.arg("header"), prefix) ||
        !validStreamHeader(prefix, fileSize) ||
        (HTTPServer.hasArg("cycles") &&
         !parseDecimal64(HTTPServer.arg("cycles"), cycles))) {
      HTTPServer.send(400, "text/plain", "Supported: C64/C16 PAL TAP v1/v2");
      return;
    }
    logicalSize = fileSize;
    firstOffset = 20;
    totalTapeCycles = 0;
    reportedTotalCycles = cycles; // Browser metadata is display-only.
    sourceClockHz = prefix[13] == 2 ? C16_CLOCK_HZ : C64_CLOCK_HZ;
    tapHalfWaves = prefix[12] == 2;
  }
  const uint32_t id = newSessionId(0);
  if (isPrg) {
    tapeSource.usePrg(prefix, logicalSize, id);
    sourceFormat = SourceFormat::PRG;
    tapeName = HTTPServer.arg("displayName");
    if (!tapeName.length()) tapeName = HTTPServer.arg("name");
  } else {
    tapeSource.useStream(prefix, logicalSize, id);
    sourceFormat = SourceFormat::TAP;
    tapeName = HTTPServer.arg("name");
  }
  tapEnd = logicalSize;
  tapeLoaded = false;
  playedBaseCycles = 0;
  playbackComplete = false;
  playerError = nullptr;
  state = State::WAITING;
  resetPlayer(firstOffset);
  setSensePressed(autoPlay);
  StaticJsonDocument<160> reply;
  reply["sessionId"] = id;
  reply["nextOffset"] = firstOffset;
  reply["logicalSize"] = logicalSize;
  reply["freeBytes"] = tapeSource.freeBytes();
  String response;
  serializeJson(reply, response);
  HTTPServer.send(201, "application/json", response);
}

void handleNav() {
  if (motorBlocksEdit() || !stopPlaybackForEdit()) {
    HTTPServer.send(409, "text/plain", "Stop MOTOR before seeking");
    return;
  }
  if (!tapeLoaded || sourceFormat != SourceFormat::TAP || tapEnd <= 20) {
    HTTPServer.send(400, "text/plain", "No TAP loaded");
    return;
  }
  const String dirArg = HTTPServer.arg("dir");
  const int dir = dirArg == "1" ? 1 : dirArg == "-1" ? -1 : 0;
  if (dir == 0) {
    HTTPServer.send(400, "text/plain", "dir must be 1 or -1");
    return;
  }
  const uint64_t played = playedBaseCycles;
  const uint32_t counter = tapeCounterAtCycles(played, sourceClockHz);
  uint32_t targetCounter;
  if (dir > 0) {
    targetCounter = (counter / 10U) * 10U + 10U;
  } else if (counter % 10U == 0) {
    targetCounter = counter >= 10U ? counter - 10U : 0;
  } else {
    targetCounter = (counter / 10U) * 10U;
  }
  const uint64_t targetCycles = cyclesAtTapeCounter(targetCounter, sourceClockHz);
  uint32_t offset = 20;
  uint64_t atCycles = 0;
  if (targetCycles > 0) {
    if (!scanTapToCycles(targetCycles, offset, atCycles)) {
      HTTPServer.send(500, "text/plain", "TAP scan failed");
      return;
    }
  }
  if (tapeSource.kind() == TapeSource::Kind::STREAMED) {
    if (!tapeSource.seek(offset, tapeSource.sessionId())) {
      HTTPServer.send(409, "text/plain",
                      "Streamed TAP window missed that offset; use IDX/timeline FF");
      return;
    }
    tapeLoaded = tapeSource.bufferedBytes() >= 4096 ||
                 tapeSource.nextOffset() >= tapEnd;
  }
  playedBaseCycles = atCycles;
  playbackComplete = false;
  playerError = nullptr;
  state = State::WAITING;
  resetPlayer(offset);
  StaticJsonDocument<192> reply;
  reply["offset"] = offset;
  reply["playedCycles"] = (double)atCycles;
  reply["tapeState"] = tapeLoaded ? "loaded" : "ejected";
  String response;
  serializeJson(reply, response);
  HTTPServer.send(200, "application/json", response);
}

void handleSeek() {
  uint32_t id, offset, phase;
  uint64_t remainingCycles;
  if (motorBlocksEdit() || !stopPlaybackForEdit()) {
    HTTPServer.send(409, "text/plain", "Stop MOTOR before seeking");
    return;
  }
  const bool toEnd = HTTPServer.arg("end") == "1";
  if (sourceFormat != SourceFormat::TAP ||
      !parseDecimal(HTTPServer.arg("sid"), id) ||
      !parseDecimal(HTTPServer.arg("offset"), offset) ||
      !parseDecimal(HTTPServer.arg("phase"), phase) || phase > 1 ||
      (!tapHalfWaves && phase != 0) ||
      !parseDecimal64(HTTPServer.arg("cycles"), remainingCycles) ||
      tapeSource.kind() != TapeSource::Kind::STREAMED ||
      id != tapeSource.sessionId() ||
      (!toEnd && (remainingCycles == 0 || offset < 20 || offset >= tapEnd)) ||
      (toEnd && remainingCycles == 0)) {
    HTTPServer.send(400, "text/plain", "Invalid session or TAP offset");
    return;
  }
  if (toEnd) {
    // IDX/FF past the last marker: park at end of tape (session kept for REW).
    tapeLoaded = true;
    totalTapeCycles = remainingCycles;
    reportedTotalCycles = remainingCycles;
    playedBaseCycles = remainingCycles;
    playbackComplete = true;
    playerError = nullptr;
    uploadSeen = false;
    state = State::FINISHED;
    resetPlayer(tapEnd);
    StaticJsonDocument<160> reply;
    reply["sessionId"] = id; // Keep the same session; browser source stays valid.
    reply["nextOffset"] = tapEnd;
    reply["freeBytes"] = tapeSource.freeBytes();
    reply["tapeState"] = "loaded";
    reply["atEnd"] = true;
    String response;
    serializeJson(reply, response);
    HTTPServer.send(200, "application/json", response);
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
  playbackComplete = false;
  playerError = nullptr;
  uploadSeen = false;
  state = State::WAITING;
  resetPlayer(offset);
  player.decoder.halfWaveLevel = phase;
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
      if (sourceFormat == SourceFormat::PRG)
        prgEncoder.observeFirstCopy(tapeSource.nextOffset() - upload.currentSize,
                                    upload.buf, upload.currentSize);
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
  if (uploadError) {
    ++uploadErrorCount;
    reply["error"] = uploadError;
  }
  String response;
  serializeJson(reply, response);
  HTTPServer.send(uploadError ? 409 : 200, "application/json", response);
  uploadSeen = false;
}

void handleSense() {
  const String pressed = HTTPServer.arg("pressed");
  if (pressed != "0" && pressed != "1") {
    HTTPServer.send(400, "application/json", "{\"error\":\"invalid_sense\"}");
    return;
  }
  setSensePressed(pressed == "1");
  HTTPServer.send(200, "application/json", pressed == "1" ?
      "{\"sensePressed\":true}" : "{\"sensePressed\":false}");
}

void handleAutoPlay() {
  if (motorBlocksEdit()) {
    HTTPServer.send(409, "application/json", "{\"error\":\"motor_on\"}");
    return;
  }
  const String enabled = HTTPServer.arg("enabled");
  if (enabled != "0" && enabled != "1") {
    HTTPServer.send(400, "application/json", "{\"error\":\"invalid_autoplay\"}");
    return;
  }
  File file = LittleFS.open("/autoplay.cfg", "w");
  if (!file) {
    HTTPServer.send(500, "application/json", "{\"error\":\"autoplay_save_failed\"}");
    return;
  }
  const bool saved = file.write(enabled[0]) == 1;
  file.close();
  if (!saved) {
    HTTPServer.send(500, "application/json", "{\"error\":\"autoplay_save_failed\"}");
    return;
  }
  autoPlay = enabled == "1";
  HTTPServer.send(200, "application/json", autoPlay ?
      "{\"autoPlay\":true}" : "{\"autoPlay\":false}");
}

void handleDefaultMachine() {
  const String machine = HTTPServer.arg("machine");
  if (machine != "c64" && machine != "c16") {
    HTTPServer.send(400, "application/json", "{\"error\":\"invalid_machine\"}");
    return;
  }
  File file = LittleFS.open("/machine.cfg", "w");
  if (!file) {
    HTTPServer.send(500, "application/json", "{\"error\":\"machine_save_failed\"}");
    return;
  }
  const bool saved = file.print(machine) == machine.length();
  file.close();
  if (!saved) {
    HTTPServer.send(500, "application/json", "{\"error\":\"machine_save_failed\"}");
    return;
  }
  defaultC16 = machine == "c16";
  HTTPServer.send(200, "application/json", defaultC16 ?
      "{\"defaultMachine\":\"c16\"}" : "{\"defaultMachine\":\"c64\"}");
}

void handleEject() {
  if (motorBlocksEdit() || !stopPlaybackForEdit()) {
    HTTPServer.send(409, "text/plain", "Stop MOTOR before ejecting");
    return;
  }
  tapeSource.eject();
  tapeLoaded = false;
  setSensePressed(false);
  tapEnd = 0;
  totalTapeCycles = 0;
  reportedTotalCycles = 0;
  playedBaseCycles = 0;
  playbackComplete = false;
  playerError = nullptr;
  scopeClear();
  state = State::WAITING;
  resetPlayer(20);
  HTTPServer.send(200, "application/json", "{\"tapeState\":\"ejected\"}");
}

void handleDemo() {
  if (motorBlocksEdit() || !stopPlaybackForEdit()) {
    HTTPServer.send(409, "text/plain", "Stop MOTOR before inserting the demo");
    return;
  }
  if (pendingDemoInsert) {
    HTTPServer.send(202, "application/json", "{\"tapeState\":\"loading\"}");
    return;
  }
  demoInsertFailed = false;
  pendingDemoInsert = true;
  // Finish in loop() so this handler returns quickly and Wi-Fi stays alive.
  HTTPServer.send(202, "application/json", "{\"tapeState\":\"loading\"}");
}
