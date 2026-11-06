// IDE settings

// Board: ESP01
// IDE settings: Generic ESP8266 module, 80MHz, vtables Flash, reset method: ck, crystal 26MHz, flash 40MHZ, flash mode QIO, 921600 on COM5
// as per https://arduino-esp8266.readthedocs.io/en/latest/boards.html#generic-esp8266-module 

// 1MB

// revised on 27.4, 1.5
#define PIN_MOTOR 0 // GPIO0       <- MOTOR (C,3) $01 bit 5 (through optocoupler with gnd pulldown 10k) // 0 = motor off, 1 = motor on
#define PIN_SENSE 1 // GPIO1 (TxD) -> SENSE (F,6) $01 bit 4 (with pullup to 3.3V, 10k) // 0 = pushed, 1 = released
#define PIN_WRITE 2
#define PIN_READ  3 // GPIO3 (RxD) -> READ  (D,4) CIA#1 /FLAG, I2S, pulled to 5V in C64, normal high (input pullup), impulse low

#include <Arduino.h>
#include <Schedule.h>

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
#include "fastloader_settings.h"
#include "fastloader_native.h"
#include "fastloader_wire.h"
#include "write_decoder.h"
#include "build_version.h"
#include "player_page.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <math.h>
#include <pgmspace.h>
#include <string.h>

#include <core_esp8266_i2s.h>

#include "circles_demo.h"

#ifndef ESPSETTE_GPIO_DISABLED
#define ESPSETTE_GPIO_DISABLED 0
#endif
#ifndef ESPSETTE_WIFI_DISABLED
#define ESPSETTE_WIFI_DISABLED 0
#endif
static bool fastloaderBusy();

#if ESPSETTE_GPIO_DISABLED
// Bench: soft SENSE via PLAY/STOP; soft MOTOR stays on (as if the C64 asserted
// it). Tape only runs while both motor and PLAY are active.
static bool softSensePressed = false;
static bool softMotorOn = true;

static inline void setSensePressed(bool pressed) { softSensePressed = pressed; }
static inline bool senseIsPressed() { return softSensePressed; }
static inline bool motorIsOn() { return softMotorOn; }
static inline bool motorBlocksEdit() { return fastloaderBusy(); }
#else
static inline void setSensePressed(bool pressed) {
  digitalWrite(PIN_SENSE, pressed ? LOW : HIGH);
}
static inline bool senseIsPressed() { return digitalRead(PIN_SENSE) == LOW; }
static inline bool motorIsOn() { return digitalRead(PIN_MOTOR) == HIGH; }
static inline bool motorBlocksEdit() { return motorIsOn() || fastloaderBusy(); }
#endif

static inline bool tapeShouldRun() {
  return motorIsOn() && senseIsPressed();
}

ESP8266WebServer HTTPServer(80);
NetworkConfig networkConfig(HTTPServer, PIN_MOTOR);
TapeSource tapeSource;
PrgEncoder prgEncoder;
FastloaderSettings fastloaderSettings;
FastloaderSettings fastSessionSettings;
enum class FastPhase : uint8_t { OFF, TAPE, RECEIVER, DESCRIPTOR, PAYLOAD, DONE };
volatile FastPhase fastPhase = FastPhase::OFF;
volatile uint16_t fastSignature = 0;
volatile bool fastSignatureSeen = false, fastRequestPending = false, fastSelecting = false;
bool fastDmaStopped = false;
volatile bool fastRequestOn = false, fastHeaderWait = false;
volatile uint32_t fastRequestUs = 0;
bool fastTed = false;
uint16_t fastAddress = 0, fastLength = 0, fastEntry = 0;
uint32_t fastSent = 0, fastStageSent = 0;
uint32_t fastDataStartedMs = 0, fastDataFinishedMs = 0;
bool fastDataStarted = false, fastWaitingForData = false;
uint8_t fastBlock[256];
static bool fastloaderBusy() {
  return (fastPhase == FastPhase::TAPE && fastSelecting) || fastPhase == FastPhase::RECEIVER ||
         fastPhase == FastPhase::DESCRIPTOR || fastPhase == FastPhase::PAYLOAD;
}
WriteDecoder writeDecoder;
volatile bool otaUpdating = false;
// Decode in IRAM at the edge, independently of synchronous HTTP writes.
// No pulse queue, heap allocation or logging is used by this ISR.
volatile uint32_t writeLastEdgeUs[2] = {};
volatile bool writeActive = false;
bool writeWasAllowed = true;
volatile bool writeInterruptAttached = false;
volatile uint8_t writeInterruptMode = CHANGE;
volatile uint32_t writeMotorOffUs = 0;
volatile bool writeMotorStopped = false;

// WRITE capture requires an empty deck, including after paused or completed
// playback. READ/DMA and command reception must not compete.
static bool IRAM_ATTR writeCommandsAllowed() {
  return tapeSource.kind() == TapeSource::Kind::EMPTY && !otaUpdating &&
         (fastPhase == FastPhase::OFF || fastPhase == FastPhase::DONE);
}

static void IRAM_ATTR setWriteInterruptMode(uint8_t mode) {
  if (mode == writeInterruptMode) return;
  // The Arduino callback remains CHANGE (no software polarity filtering).
  // Only the hardware edge detector changes; no flash/attachInterrupt call.
  GPC(PIN_WRITE) = (GPC(PIN_WRITE) & ~(0xFu << GPCI)) | (uint32_t(mode) << GPCI);
  writeInterruptMode = mode;
}

static void IRAM_ATTR updateWriteMotor(bool on, uint32_t now) {
  if (on == writeActive) return;
  if (on) {
    writeDecoder.motorStart(writeMotorStopped ? uint32_t(now - writeMotorOffUs) : UINT32_MAX);
    writeLastEdgeUs[0] = writeLastEdgeUs[1] = 0;
    const auto machine = writeDecoder.machine();
    setWriteInterruptMode(machine == WriteDecoder::Machine::UNKNOWN ? CHANGE :
                          machine == WriteDecoder::Machine::C64 ? RISING : FALLING);
    writeActive = true;
  } else {
    writeActive = false;
    writeDecoder.motorStop();
    writeMotorOffUs = now;
    writeMotorStopped = true;
  }
}

void IRAM_ATTR onWriteEdge() {
  if (!writeActive || !writeCommandsAllowed() || !(GPI & (1u << PIN_MOTOR))) return;
  const uint32_t now = micros();
  const bool high = (GPI & (1u << PIN_WRITE)) != 0;
  const uint32_t previous = writeLastEdgeUs[high];
  writeLastEdgeUs[high] = now;
  if (!previous) return;
  const uint32_t duration = now - previous;
  writeDecoder.feedEdgeMicros(duration > 0x7fff ? 0x7fff : duration, high);
  const auto machine = writeDecoder.machine();
  if (machine != WriteDecoder::Machine::UNKNOWN)
    setWriteInterruptMode(machine == WriteDecoder::Machine::C64 ? RISING : FALLING);
}

void processWriteCommands() {
#if !ESPSETTE_GPIO_DISABLED
  if (!writeCommandsAllowed()) {
    noInterrupts();
    writeActive = false;
    writeLastEdgeUs[0] = writeLastEdgeUs[1] = 0;
    interrupts();
    if (writeInterruptAttached) {
      detachInterrupt(digitalPinToInterrupt(PIN_WRITE));
      writeInterruptAttached = false;
    }
    if (writeWasAllowed) writeDecoder.suspend();
    writeWasAllowed = false;
    return;
  }
  if (!writeInterruptAttached) {
    attachInterrupt(digitalPinToInterrupt(PIN_WRITE), onWriteEdge, CHANGE);
    writeInterruptAttached = true;
    writeInterruptMode = CHANGE;
  }
  if (!writeWasAllowed) {
    noInterrupts();
    writeActive = false;
    writeDecoder.motorStart();
    writeWasAllowed = true;
    interrupts();
  }
  // Also synchronize the initial physical state when capture is attached.
  // Subsequent MOTOR edges are handled directly by onMotorChange(), even
  // while the main loop is blocked sending HTTP data.
  noInterrupts();
  updateWriteMotor((GPI & (1u << PIN_MOTOR)) != 0, micros());
  interrupts();
#endif
}

enum class SourceFormat : uint8_t { TAP, PRG };
SourceFormat sourceFormat = SourceFormat::TAP;

enum class State : uint8_t { WAITING, STREAMING, DRAINING, FINISHED, ERROR };

// One I2S sample contains 32 serial output bits. One bit represents 16 source cycles.
ComputerProfile sourceProfile = ComputerProfile::C64_PAL;
uint32_t sourceClockHz = profileClockHz(sourceProfile);
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
ComputerProfile defaultProfile = ComputerProfile::C64_PAL;
bool playbackComplete = false;
String tapeName = "Circles (demo)";
uint64_t totalTapeCycles = 0;
uint64_t reportedTotalCycles = 0;
uint64_t playedBaseCycles = 0;
const char* playerError = nullptr;
uint8_t uploadError = 0;
uint16_t uploadBytes = 0;
bool uploadSeen = false;
static const uint16_t MAX_HTTP_CHUNK = 4096;
bool networkReady = false;
bool otaReady = false;
uint32_t i2sUnderrunCount = 0;
uint32_t sourceUnderrunCount = 0;
bool sourceWaiting = false;
uint32_t sourceWaitCount = 0, sourceReadOffset = 0;
uint32_t sourceWindowStart = 0, sourceReceivedUntil = 0;
uint32_t uploadErrorCount = 0;
uint32_t lastSourceDataMs = 0, sourceDataAgeAtErrorMs = 0;
// Cooperative refill runs on the normal CONT stack during network yields.
// No timer, ISR, extra sample buffer or Wi-Fi masking is involved.
bool dmaPumpBusy = false, dmaPumpFromYield = false, dmaServiceReady = false;
uint32_t lastDmaPumpUs = 0, maxDmaPumpGapUs = 0;

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
  uint8_t pendingBits; // Unpadded partial word retained across source waits.
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
bool readDmaActive = false;
uint32_t stoppedRunBits = 0;
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
  if (fastloaderBusy()) return false;
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
  // Keep the actual MOTOR edge captured by the ISR, even if HTTP delayed us.
  // The edge still matters if MOTOR went back on before HTTP yielded.
  // A browser STOP without an edge instead needs the current position.
  if (!motorFell) {
    motorFallCallbacks = dmaCallbacks;
    motorFallElapsedUs = lastDmaCallbackUs ? micros() - lastDmaCallbackUs : 0;
  }
  interrupts();
  pauseTape();
#endif
}

void handleRoot();
void handlePlayer();
void handleNotFound();
void handleState();
void handleBuffer();
void handleSession();
void handleSeek();
void handleNav();
void handleChunk();
void handleChunkUpload();
void handleEject();
void handleSense();
void handleAutoPlay();
void handleDefaultMachine();
void handleFastloaderConfig();
static bool parseDecimal(const String&, uint32_t&);
void handleDemo();
void pumpFastloader();
void resetFastloader();

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

void IRAM_ATTR onMotorChange() {
  const bool on = (GPI & (1u << PIN_MOTOR)) != 0;
  if (!on) onMotorFalling();
  if (writeInterruptAttached && writeCommandsAllowed()) updateWriteMotor(on, micros());
  if (fastPhase == FastPhase::TAPE && !fastSignatureSeen) {
    if (on) {
      fastSignature = (fastSignature << 1) | ((GPI >> PIN_WRITE) & 1);
      if ((fastSignature & 7) == 6 && !fastSelecting) {
        fastSelecting = true;
#if !ESPSETTE_GPIO_DISABLED
        // Stop at prefix 110, well before the stub clears FLAG. No allocation
        // or flash code in this ISR; release DMA later from loop().
        I2SC &= ~I2STXS;
        GPOS = 1u << PIN_READ;
        GPF3 = GPFFS(GPFFS_GPIO(3));
        GPC3 &= 0xFu << GPCI;
        GPES = 1u << PIN_READ;
#endif
      }
      if (fastSignature == 0xca65) fastSignatureSeen = true;
    }
    return;
  }
  if (fastSignatureSeen && fastPhase != FastPhase::DONE && fastPhase != FastPhase::OFF) {
    // The low-RAM receiver first normalizes MOTOR to ON; its first request is OFF.
    if (fastHeaderWait && on) return;
    fastHeaderWait = false;
    fastRequestOn = on;
    fastRequestUs = micros();
    fastRequestPending = true;
  }
}

void resetPlayer(uint32_t offset) {
  lastSourceDataMs = sourceDataAgeAtErrorMs = 0;
  sourceWaiting = false;
  sourceReadOffset = sourceWindowStart = sourceReceivedUntil = 0;
  player.decoder.offset = offset;
  player.decoder.prgPulseIndex = 0;
  player.decoder.pulseTicks = 0;
  player.decoder.pulseIndex = 0;
  player.decoder.cycleRemainder = 0;
  player.decoder.halfWaveLevel = 0;
  player.pendingWord = 0;
  player.wordPending = false;
  player.pendingBits = 0;
  player.drainWordsLeft = 0;
  player.emptySince = 0;
}

// Both uploaded PRGs and the embedded demo use the same header bootstrap.
static bool prepareFastPrg(uint16_t address, uint32_t length, const String& name,
                           PrgEncoder::Machine machine, bool ntsc, uint16_t entry) {
  const bool ted = machine == PrgEncoder::Machine::C16;
  if (address < (ted ? 0x1001 : 0x0801) ||
      uint32_t(address) + length > (ted ? 0x4000u : 0x8000u)) return false;
  uint8_t stub[171];
  const uint8_t* code = ted ? tedBootstrap : c64Bootstrap;
  const size_t count = ted ? sizeof(tedBootstrap) : sizeof(c64Bootstrap);
  memcpy_P(stub, code, count);
  if (!prgEncoder.beginAutostart(stub, count, name, machine, ntsc)) return false;
  const uint8_t vector[] = {uint8_t(ted ? 0x47 : 0x51), 3};
  prgEncoder.observeFirstCopy(2, vector, sizeof(vector));
  fastTed = ted; fastAddress = address; fastLength = length; fastEntry = entry;
  return true;
}

static bool pendingDemoInsert = false;
static bool demoInsertFailed = false;

static void insertEmbeddedDemo() {
  resetFastloader();
  sourceFormat = SourceFormat::PRG;
  sourceProfile = ComputerProfile::C64_PAL; // Circles is C64 PAL only.
  sourceClockHz = profileClockHz(sourceProfile);
  tapHalfWaves = false;
  tapeName = "Circles (demo)";
  const uint16_t address = pgm_read_byte(circlesPrg) | (uint16_t(pgm_read_byte(circlesPrg + 1)) << 8);
  const uint32_t length = sizeof(circlesPrg) - 2;
  const bool fast = fastloaderSettings.enabled;
  bool ok = prgEncoder.begin(address, length, "CIRCLES", PrgEncoder::Machine::C64);
  if (ok && fast) ok = prepareFastPrg(address, length, "CIRCLES", PrgEncoder::Machine::C64, false, 0);
  if (ok) ok = tapeSource.useEmbeddedPrg(circlesPrg, sizeof(circlesPrg), !fast);
  if (ok && !fast) {
    // Compute only the first ROM copy's XOR. The second copy reads the same
    // flash bytes through TapeSource; no duplicate PRG or generated TAP.
    uint8_t bytes[128];
    for (uint32_t offset = 2; offset < sizeof(circlesPrg); offset += sizeof(bytes)) {
      const size_t n = min(uint32_t(sizeof(bytes)), uint32_t(sizeof(circlesPrg) - offset));
      memcpy_P(bytes, circlesPrg + offset, n);
      prgEncoder.observeFirstCopy(offset, bytes, n);
      yield();
    }
  }
  totalTapeCycles = prgEncoder.totalCycles();
  reportedTotalCycles = totalTapeCycles + (fast ? prgEncoder.standardDataCycles(length) : 0);
  tapEnd = tapeSource.size();
  if (ok && fast) {
    fastSessionSettings = fastloaderSettings;
    fastPhase = FastPhase::TAPE;
  }
  playedBaseCycles = 0;
  playbackComplete = false;
  playerError = ok ? nullptr : "invalid_demo_prg";
  scopeClear();
  resetPlayer(2);
  tapeLoaded = ok;
  setSensePressed(autoPlay && ok);
  state = ok ? State::WAITING : State::ERROR;
  demoInsertFailed = !ok;
  if (!ok) tapeSource.eject();
}

void idleReadPin() {
#if !ESPSETTE_GPIO_DISABLED
  pinMode(PIN_READ, OUTPUT);
  digitalWrite(PIN_READ, LOW);
#endif
}

static uint32_t currentRunBits();

void stopI2S() {
#if !ESPSETTE_GPIO_DISABLED
  if (readDmaActive) stoppedRunBits = currentRunBits();
  i2s_end();
  readDmaActive = false;
  idleReadPin();
#endif
}

struct FastGpioOutput {
  void IRAM_ATTR data(bool write, bool sense) {
#if !ESPSETTE_GPIO_DISABLED
    const uint32_t pins = (1u << PIN_WRITE) | (1u << PIN_SENSE);
    const uint32_t high = (write ? 1u << PIN_WRITE : 0) | (sense ? 1u << PIN_SENSE : 0);
    GPOC = pins & ~high;
    GPOS = high;
#endif
  }
  void IRAM_ATTR read(bool high) {
#if !ESPSETTE_GPIO_DISABLED
    if (high) GPOS = 1u << PIN_READ;
    else GPOC = 1u << PIN_READ;
#endif
  }
  void IRAM_ATTR wait(uint16_t us) {
    const uint32_t start = ESP.getCycleCount();
    const uint32_t cycles = uint32_t(us) * (F_CPU / 1000000u);
    while (uint32_t(ESP.getCycleCount() - start) < cycles) {}
  }
};

// Keep a concrete IRAM entry point: template COMDAT sections can otherwise
// be placed in flash by the Xtensa linker even with an IRAM attribute.
void IRAM_ATTR __attribute__((noinline, noclone)) sendFastByte(
    uint8_t value, bool ted, bool bootstrap, const FastloaderSettings& settings) {
  FastGpioOutput output;
  fastloaderSendByte(output, value, ted, bootstrap, settings);
}

void resetFastloader() {
  noInterrupts();
  fastPhase = FastPhase::OFF;
  fastSignature = 0;
  fastSignatureSeen = fastRequestPending = fastHeaderWait = fastSelecting = false;
  fastDmaStopped = false;
  interrupts();
  fastSent = fastStageSent = 0;
  fastDataStartedMs = fastDataFinishedMs = 0;
  fastDataStarted = fastWaitingForData = false;
#if !ESPSETTE_GPIO_DISABLED
  pinMode(PIN_WRITE, INPUT_PULLUP);
#endif
}

void pumpFastloader() {
  if (fastPhase == FastPhase::TAPE && fastSelecting && !fastDmaStopped) {
#if !ESPSETTE_GPIO_DISABLED
    i2s_end();
    digitalWrite(PIN_READ, HIGH);
    pinMode(PIN_READ, OUTPUT);
#endif
    fastDmaStopped = true;
  }
  if (fastPhase == FastPhase::TAPE && fastSignatureSeen) {
#if !ESPSETTE_GPIO_DISABLED
    digitalWrite(PIN_READ, HIGH);
#endif
    motorFell = false;
    fastPhase = FastPhase::RECEIVER;
    state = State::STREAMING;
  }
  if (fastPhase != FastPhase::RECEIVER && fastPhase != FastPhase::DESCRIPTOR &&
      fastPhase != FastPhase::PAYLOAD) return;
  if (!fastRequestPending) return;
  if (fastPhase == FastPhase::PAYLOAD && !fastDataStarted) {
    fastDataStarted = true;
    fastDataStartedMs = millis();
  }
  const bool requestOn = fastRequestOn;
  const uint32_t requestedAt = fastRequestUs;
  if (uint32_t(micros() - requestedAt) < fastSessionSettings.us[requestOn ? 8 : 9]) return;
  const bool bootstrap = fastPhase == FastPhase::RECEIVER;
  uint16_t count;
  if (bootstrap) {
    const uint8_t* code = fastTed ? tedReceiver : c64Receiver;
    memcpy_P(fastBlock, code + fastStageSent, 256);
    count = 256;
  } else if (fastPhase == FastPhase::DESCRIPTOR) {
    const uint16_t words[] = {fastAddress, fastLength, fastEntry};
    for (unsigned i = 0; i < 3; ++i) {
      fastBlock[i*2] = words[i]; fastBlock[i*2+1] = words[i] >> 8;
    }
    count = 6;
  } else {
    count = (fastLength - fastSent) < 256 ? fastLength - fastSent : 256;
    for (uint16_t i = 0; i < count; ++i)
      if (!tapeSource.read(2 + fastSent + i, fastBlock[i])) {
        fastWaitingForData = true;
        return;
      }
    fastWaitingForData = false;
  }
  noInterrupts();
  fastRequestPending = false;
  if (bootstrap && fastStageSent + count == (fastTed ? sizeof(tedReceiver) : sizeof(c64Receiver)))
    fastHeaderWait = true;
  interrupts();
#if !ESPSETTE_GPIO_DISABLED
  // The first request follows the computer's release of WRITE after the signature.
  digitalWrite(PIN_WRITE, HIGH);
  pinMode(PIN_WRITE, OUTPUT);
#endif
  FastGpioOutput output;
  output.wait(fastSessionSettings.us[bootstrap ? 7 : fastTed ? 5 : 4]);
  for (uint16_t i = 0; i < count; ++i) {
    sendFastByte(fastBlock[i], fastTed, bootstrap, fastSessionSettings);
    ESP.wdtFeed();
  }
  if (bootstrap) {
    fastStageSent += count;
    if (fastStageSent == (fastTed ? sizeof(tedReceiver) : sizeof(c64Receiver)))
      fastPhase = FastPhase::DESCRIPTOR;
  } else if (fastPhase == FastPhase::DESCRIPTOR) {
    fastPhase = FastPhase::PAYLOAD;
  } else {
    fastSent += count;
    tapeSource.releaseBefore(2 + fastSent);
    if (fastSent == fastLength) {
      fastDataFinishedMs = millis();
      fastPhase = FastPhase::DONE;
#if !ESPSETTE_GPIO_DISABLED
      pinMode(PIN_WRITE, INPUT_PULLUP);
#endif
      setSensePressed(false);
      state = State::FINISHED;
      playbackComplete = true;
      playedBaseCycles = totalTapeCycles;
    }
  }
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
    resetPlayer(sourceFormat == SourceFormat::PRG ? 2 : 20);
    playbackComplete = true;
    state = State::WAITING;
    return;
  }
  if (playedBits > generatedTapBits) {
    state = State::ERROR; // DMA ran ahead of the source; LOAD is invalid.
    playerError = "i2s_underrun";
    ++i2sUnderrunCount;
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
      return;
    }
    player.decoder = checkpoints[slot];
    uint8_t bit;
    for (uint32_t i = 0; i < withinBuffer; ++i) {
      if (!nextTapeBit(bit)) {
        state = State::ERROR;
        playerError = "restore_failed";
        return;
      }
    }
  }
  tapeSource.releaseBefore(player.decoder.offset);
  player.pendingWord = 0;
  player.wordPending = false;
  player.pendingBits = 0;
  player.drainWordsLeft = 0;
  player.emptySince = 0;
  state = State::WAITING;
}

void startTape() {
  if (otaUpdating) return;
#if !ESPSETTE_GPIO_DISABLED
  if (!dmaServiceReady) {
    state = State::ERROR;
    playerError = "dma_service_alloc_failed";
    return;
  }
#endif
  lastDmaPumpUs = maxDmaPumpGapUs = 0;
  sourceWaiting = false;
  if (playbackComplete) {
    playedBaseCycles = 0;
    playbackComplete = false;
  }
  if (sourceFormat == SourceFormat::TAP && player.decoder.offset >= tapEnd &&
      player.decoder.pulseIndex == player.decoder.pulseTicks) resetPlayer(sourceFormat == SourceFormat::PRG ? 2 : 20);
  noInterrupts();
  dmaCallbacks = 0;
  committedWords = 0;
  dmaUnderrun = false;
  lastDmaCallbackUs = 0;
  motorFell = false;
  interrupts();
  scheduledWords = 0;
  generatedTapBits = stoppedRunBits = 0;
  runStartDecoder = player.decoder;
  scopeClear();
  realBitRate = sourceClockHz / CYCLES_PER_BIT;
#if ESPSETTE_GPIO_DISABLED
  // Soft READ stream: advance the decoder on wall time for counter/scope UI.
  softPlayLastUs = micros();
  state = State::STREAMING;
  return;
#else
  // This rate is remembered by the core and applied during i2s_begin(),
  // before the first (otherwise fast) DMA descriptor can finish.
  i2s_set_rate(realBitRate / 32);
  if (!i2s_rxtxdrive_begin(false, true, false, false)) {
    state = State::ERROR;
    playerError = "i2s_alloc_failed";
    return;
  }
  readDmaActive = true;
  i2s_set_callback(onDmaBufferDone);
  realBitRate = (uint32_t)(i2s_get_real_rate() * 32.0f + 0.5f);
  state = State::STREAMING;
#endif
}

// A producer may reach bytes not yet received while DMA still has output.
// Preserve the decoder/partial word and use that lead for the next HTTP chunk.
static bool missingSource(uint32_t offset) {
  sourceReadOffset = offset;
  sourceWindowStart = tapeSource.retainedFrom();
  sourceReceivedUntil = tapeSource.nextOffset();
  if (tapeSource.kind() == TapeSource::Kind::STREAMED &&
      offset >= sourceReceivedUntil && offset < tapeSource.size()) {
    if (!sourceWaiting) ++sourceWaitCount;
    sourceWaiting = true;
    return false;
  }
  sourceWaiting = false;
  state = State::ERROR;
  playerError = offset < sourceWindowStart ? "source_window_lost" : "buffer_underrun";
  ++sourceUnderrunCount;
  sourceDataAgeAtErrorMs = millis() - lastSourceDataMs;
  return false;
}

bool loadPulse() {
  uint32_t cycles;
  if (sourceFormat == SourceFormat::PRG) {
    uint32_t sourceOffset;
    const PrgEncoder::Result result = prgEncoder.pulseAt(
        player.decoder.prgPulseIndex, tapeSource, cycles, sourceOffset);
    if (result == PrgEncoder::Result::END) { sourceWaiting = false; return false; }
    if (result == PrgEncoder::Result::MISSING_DATA) return missingSource(sourceOffset);
    player.decoder.offset = sourceOffset;
    ++player.decoder.prgPulseIndex;
  } else {
    const uint32_t recordOffset = player.decoder.offset;
    if (recordOffset >= tapEnd) { sourceWaiting = false; return false; }
    uint8_t value;
    if (!tapeSource.read(recordOffset, value)) return missingSource(recordOffset);
    if (value != 0) {
      cycles = (uint32_t)value * 8;
      player.decoder.offset = recordOffset + 1;
    } else {
      if (tapEnd - recordOffset < 4) {
        sourceWaiting = false;
        state = State::ERROR;
        playerError = "truncated_pulse";
        return false;
      }
      uint8_t lo, mid, hi;
      if (!tapeSource.read(recordOffset + 1, lo) ||
          !tapeSource.read(recordOffset + 2, mid) ||
          !tapeSource.read(recordOffset + 3, hi)) return missingSource(recordOffset + 3);
      cycles = (uint32_t)lo | ((uint32_t)mid << 8) | ((uint32_t)hi << 16);
      player.decoder.offset = recordOffset + 4; // Commit only a complete record.
    }
    if (cycles == 0) {
      sourceWaiting = false;
      state = State::ERROR;
      playerError = "zero_pulse";
      return false;
    }
  }
  sourceWaiting = false;
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
      if (state == State::ERROR || sourceWaiting) {
        if (sourceWaiting) tapeSource.releaseBefore(player.decoder.offset);
        return;
      }
      state = State::FINISHED;
      playbackComplete = true;
      if (totalTapeCycles) playedBaseCycles = totalTapeCycles;
      else playedBaseCycles += (uint64_t)generatedTapBits * CYCLES_PER_BIT;
      generatedTapBits = 0;
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
  if (dmaPumpBusy || (state != State::STREAMING && state != State::DRAINING)) return;
  struct PumpGuard {
    PumpGuard() { dmaPumpBusy = true; }
    ~PumpGuard() { dmaPumpBusy = false; }
  } guard;
  const uint32_t now = micros();
  if (lastDmaPumpUs && uint32_t(now - lastDmaPumpUs) > maxDmaPumpGapUs)
    maxDmaPumpGapUs = now - lastDmaPumpUs;
  lastDmaPumpUs = now;
#if ESPSETTE_GPIO_DISABLED
  pumpTapeSoft();
  return;
#endif
  if (state == State::STREAMING && dmaUnderrun) {
    stopI2S();
    state = State::ERROR;
    playerError = sourceWaiting ? "buffer_underrun" : "i2s_underrun";
    if (sourceWaiting) {
      ++sourceUnderrunCount;
      sourceDataAgeAtErrorMs = millis() - lastSourceDataMs;
    } else ++i2sUnderrunCount;
    sourceWaiting = false;
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
  // Refill every available DMA word before servicing HTTP. At C64 PAL, the
  // old 32-word limit produced only 16.6 ms of signal per loop: consecutive
  // ~80 ms HTTP requests could drain DMA despite a full TAP source buffer.
  // Bound work by the existing DMA capacity; no extra buffers are allocated.
  const uint16_t budget = dmaPumpFromYield ? DMA_WORDS : DMA_BUFFERS * DMA_WORDS;
  for (uint16_t words = 0; words < budget; ++words) {
    if (i2s_is_full()) return;
    if (state == State::STREAMING && !player.wordPending) {
      if (player.pendingBits == 0 && scheduledWords % DMA_WORDS == 0) {
        const uint32_t buffer = scheduledWords / DMA_WORDS;
        const uint8_t slot = buffer % CHECKPOINT_SLOTS;
        checkpoints[slot] = player.decoder;
        checkpointNumber[slot] = buffer;
      }
      uint8_t bit;
      while (player.pendingBits < 32 && nextTapeBit(bit)) {
        player.pendingWord = (player.pendingWord << 1) | bit;
        ++player.pendingBits;
        ++generatedTapBits;
      }
      if (state == State::ERROR) {
        stopI2S();
        return;
      }
      if (sourceWaiting) return; // DMA continues with already committed words.
      if (player.pendingBits == 0) {
        state = State::DRAINING;
        player.drainWordsLeft = DRAIN_WORDS;
      } else {
        player.pendingWord <<= 32 - player.pendingBits;
        player.wordPending = true;
        player.pendingBits = 0;
      }
    }
    if (player.wordPending) {
      if (!i2s_write_sample_nb(player.pendingWord)) return;
      player.wordPending = false;
      player.pendingWord = 0;
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
  }
}

// ESP8266 invokes this on the user stack after yield()/delay(), including
// TCP send/receive waits. Keep it bounded, nonblocking and free of logging.
static bool serviceTapeDma() {
#if !ESPSETTE_GPIO_DISABLED
  if (!dmaPumpBusy && !otaUpdating && !fastloaderBusy() &&
      (state == State::STREAMING || state == State::DRAINING)) {
    dmaPumpFromYield = true;
    if (tapeShouldRun() && !motorFell) pumpTape();
    else pausePlaybackNow();
    dmaPumpFromYield = false;
  }
#endif
  return true;
}

void setupPins() {
#if ESPSETTE_GPIO_DISABLED
  softSensePressed = false;
#else
  pinMode(PIN_MOTOR, INPUT);
  pinMode(PIN_WRITE, INPUT_PULLUP);
  pinMode(PIN_SENSE, FUNCTION_3); // Release UART TX for GPIO1.
  digitalWrite(PIN_SENSE, HIGH); // Release PLAY until a tape or demo is ready.
  pinMode(PIN_SENSE, OUTPUT);
  idleReadPin();
#endif
}

void setupNetwork() {
  networkConfig.begin();
  networkReady = networkConfig.staConnected();
#if !ESPSETTE_WIFI_DISABLED
  // Streaming needs prompt packet delivery, including across loader MOTOR pauses.
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
}

void setupWebserver() {
  HTTPServer.on("/", handleRoot);
  HTTPServer.on("/player", HTTP_GET, handlePlayer);
  HTTPServer.on("/state", HTTP_GET, handleState);
  HTTPServer.on("/buffer", HTTP_GET, handleBuffer);
  HTTPServer.on("/session", HTTP_POST, handleSession);
  HTTPServer.on("/seek", HTTP_POST, handleSeek);
  HTTPServer.on("/nav", HTTP_POST, handleNav);
  HTTPServer.on("/chunk", HTTP_POST, handleChunk, handleChunkUpload);
  HTTPServer.on("/eject", HTTP_POST, handleEject);
  HTTPServer.on("/sense", HTTP_POST, handleSense);
  HTTPServer.on("/autoplay", HTTP_POST, handleAutoPlay);
  HTTPServer.on("/default-machine", HTTP_POST, handleDefaultMachine);
  HTTPServer.on("/fastloader-config", HTTP_POST, handleFastloaderConfig);
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
    otaUpdating = true;
    setSensePressed(false);
    stopI2S();
    processWriteCommands();
  });
  ArduinoOTA.onError([](ota_error_t) {
    otaUpdating = false;
  });
  ArduinoOTA.setHostname(networkConfig.hostname().c_str());
#if defined(ESPSETTE_OTA_PASSWORD)
  if (ESPSETTE_OTA_PASSWORD[0] != '\0')
    ArduinoOTA.setPassword(ESPSETTE_OTA_PASSWORD);
#endif
  // Use the device IP for OTA; keep mDNS traffic out of tape streaming.
  ArduinoOTA.begin(false);
  otaReady = true;
#endif
}

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

void writeFastloaderConfig(JsonObject object, const FastloaderSettings& settings) {
  object["version"] = 1;
  object["enabled"] = settings.enabled;
  for (unsigned i = 0; i < FASTLOADER_FIELD_COUNT; ++i)
    object[FASTLOADER_FIELDS[i].name] = settings.us[i];
}

bool readFastloaderConfig(JsonObjectConst object, FastloaderSettings& settings) {
  if (object["version"] != 1 || !object["enabled"].is<bool>()) return false;
  FastloaderSettings candidate;
  candidate.enabled = object["enabled"];
  for (unsigned i = 0; i < FASTLOADER_FIELD_COUNT; ++i) {
    const JsonVariantConst value = object[FASTLOADER_FIELDS[i].name];
    if (!value.is<uint32_t>() || !candidate.set(i, value.as<uint32_t>())) return false;
  }
  settings = candidate;
  return true;
}

void loadFastloaderConfig() {
  File file = LittleFS.open("/fastloader.json", "r");
  if (!file || file.size() > 2048) return;
  StaticJsonDocument<512> doc;
  if (!deserializeJson(doc, file)) readFastloaderConfig(doc.as<JsonObjectConst>(), fastloaderSettings);
}

void handleFastloaderConfig() {
  if (motorBlocksEdit() || (fastPhase != FastPhase::OFF && fastPhase != FastPhase::DONE && fastPhase != FastPhase::TAPE)) {
    HTTPServer.send(409, "application/json", "{\"error\":\"transfer_active\"}");
    return;
  }
  FastloaderSettings requested = fastloaderSettings;
  const bool changeEnabled = HTTPServer.hasArg("enabled");
  bool changeTimings = false;
  for (unsigned i = 0; i < FASTLOADER_FIELD_COUNT; ++i)
    changeTimings |= HTTPServer.hasArg(FASTLOADER_FIELDS[i].name);
  if (!changeEnabled && !changeTimings) {
    HTTPServer.send(400, "application/json", "{\"error\":\"invalid_fastloader_config\"}");
    return;
  }
  if (changeEnabled) {
    const String enabled = HTTPServer.arg("enabled");
    if (enabled != "0" && enabled != "1") {
      HTTPServer.send(400, "application/json", "{\"error\":\"invalid_fastloader_config\"}");
      return;
    }
    requested.enabled = enabled == "1";
  }
  if (changeTimings) {
    for (unsigned i = 0; i < FASTLOADER_FIELD_COUNT; ++i) {
      uint32_t value;
      if (!parseDecimal(HTTPServer.arg(FASTLOADER_FIELDS[i].name), value) || !requested.set(i, value)) {
        HTTPServer.send(400, "application/json", "{\"error\":\"invalid_fastloader_config\"}");
        return;
      }
    }
  }
  StaticJsonDocument<512> doc;
  writeFastloaderConfig(doc.to<JsonObject>(), requested);
  File file = LittleFS.open("/fastloader.tmp", "w");
  const size_t expected = measureJson(doc);
  const bool written = file && serializeJson(doc, file) == expected;
  if (file) { file.flush(); file.close(); }
  if (!written || !LittleFS.rename("/fastloader.tmp", "/fastloader.json")) {
    LittleFS.remove("/fastloader.tmp");
    HTTPServer.send(500, "application/json", "{\"error\":\"fastloader_save_failed\"}");
    return;
  }
  const bool reloadDemo = changeEnabled && requested.enabled != fastloaderSettings.enabled &&
      tapeSource.kind() == TapeSource::Kind::EMBEDDED && sourceFormat == SourceFormat::PRG;
  fastloaderSettings = requested;
  if (reloadDemo) { demoInsertFailed = false; pendingDemoInsert = true; }
  String response;
  serializeJson(doc, response);
  HTTPServer.send(200, "application/json", response);
}

void setup() {
#if ESPSETTE_GPIO_DISABLED
  // Keep UART0 so bench can see a boot line at the ROM baud rate.
  Serial.begin(74880);
  delay(50);
  Serial.println();
  Serial.printf_P(PSTR("ESPsette boot gpio_dis=%d wifi_dis=%d\r\n"),
                  (int)ESPSETTE_GPIO_DISABLED, (int)ESPSETTE_WIFI_DISABLED);
#endif
  setupPins();
  setupNetwork();
  loadFastloaderConfig();
  File autoPlayFile = LittleFS.open("/autoplay.cfg", "r");
  if (autoPlayFile) {
    autoPlay = autoPlayFile.read() != '0';
    autoPlayFile.close();
  }
  File machineFile = LittleFS.open("/machine.cfg", "r");
  if (machineFile) {
    if (machineFile.size() <= 16) {
      String name = machineFile.readString();
      name.trim();
      parseComputerProfile(name.c_str(), defaultProfile); // Legacy c64/c16 means PAL.
    }
    machineFile.close();
  }
#if !ESPSETTE_WIFI_DISABLED
  initializeOTA();
  setupWebserver();
#endif
  // Demo insertion is deferred to loop() after /demo.
  state = State::WAITING;
  tapeLoaded = false;
  setSensePressed(false);
#if !ESPSETTE_GPIO_DISABLED
  attachInterrupt(digitalPinToInterrupt(PIN_MOTOR), onMotorChange, CHANGE);
  processWriteCommands();
  dmaServiceReady = schedule_recurrent_function_us(serviceTapeDma, 2000);
#endif
#if ESPSETTE_GPIO_DISABLED
  Serial.println(F("ESPsette setup done"));
#endif
}

void loop() {
#if ESPSETTE_WIFI_DISABLED
  {
    static uint32_t lastHb;
    const uint32_t now = millis();
    if ((uint32_t)(now - lastHb) >= 1000) {
      lastHb = now;
      Serial.printf_P(PSTR("ESPsette hb %lu\r\n"), (unsigned long)now);
    }
  }
#endif
  if (pendingDemoInsert) {
    pendingDemoInsert = false;
    insertEmbeddedDemo();
  }
  const uint8_t motor = motorIsOn() ? HIGH : LOW;

  processWriteCommands();
  pumpFastloader();
  const bool gpioTransfer = fastPhase == FastPhase::RECEIVER || fastPhase == FastPhase::DESCRIPTOR ||
                            fastPhase == FastPhase::PAYLOAD;
  if (gpioTransfer || (fastPhase == FastPhase::TAPE && (fastSelecting || state == State::FINISHED))) {
    // The receiver uses MOTOR as a block request and SENSE as data.
  } else if (state == State::FINISHED && fastPhase == FastPhase::DONE) {
    // Preserve the completed PRG and 100% progress until explicit rewind/eject.
  } else if (state == State::FINISHED) {
    // After end of tape, release PLAY (or MOTOR) before a fresh run from the start.
    if (!tapeShouldRun()) {
      if (tapeSource.kind() == TapeSource::Kind::STREAMED && !motorIsOn()) {
        tapeSource.eject();
        tapeLoaded = false;
        tapEnd = 0;
      }
      resetPlayer(sourceFormat == SourceFormat::PRG ? 2 : 20);
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
    if (!tapeShouldRun() || motorFell) {
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
  if (otaReady && !motorIsOn() && !fastloaderBusy() &&
      state != State::STREAMING && state != State::DRAINING) ArduinoOTA.handle();
#endif
  HTTPServer.handleClient();
  networkConfig.handle(motor == HIGH);
#endif
  processWriteCommands();
  yield();
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
  HTTPServer.sendHeader("Content-Encoding", "gzip");
  HTTPServer.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(PLAYER_PAGE), sizeof(PLAYER_PAGE));
}

void handleNotFound() {
  HTTPServer.send(404, "text/plain", "File Not Found\n");
}

// The session protocol uses short requests. HTTPUpload buffers at most 2048
// bytes at a time; larger requests are appended incrementally to the ring.
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
  if (size <= 20 || header[14] > 1 ||
      !((c64 || c16) && (header[12] == 1 || header[12] == 2))) return false;
  const uint32_t dataSize = (uint32_t)header[16] |
                            ((uint32_t)header[17] << 8) |
                            ((uint32_t)header[18] << 16) |
                            ((uint32_t)header[19] << 24);
  return dataSize == size - 20;
}

static uint32_t currentRunBits() {
  // Error reporting must retain this run, not jump back to the last MOTOR pause.
  if (state == State::ERROR && !readDmaActive) return stoppedRunBits;
  if (state != State::STREAMING && state != State::DRAINING &&
      !(state == State::ERROR && readDmaActive)) return 0;
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

// Shared live transport fields; configuration, scope and debug data stay
// in /state. The small /buffer reply keeps uploads independent of UI polling.
static void writeTransportState(JsonDocument& doc) {
  doc["bufferCapacity"] = TapeSource::BUFFER_SIZE;
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  doc["wifiConnected"] = wifiConnected;
  const int rssi = wifiConnected ? WiFi.RSSI() : 0;
  if (rssi < 0 && rssi >= -127) doc["wifiRssi"] = rssi;
  else doc["wifiRssi"] = nullptr;
  const bool motorOn = motorIsOn();
  doc["tapeState"] = pendingDemoInsert ? "loading"
      : (demoInsertFailed && !tapeLoaded) ? "error"
      : (tapeLoaded ? "loaded" : "ejected");
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["sourceType"] = sourceFormat == SourceFormat::PRG ? "prg" : "tap";
  doc["clockHz"] = sourceClockHz;
  doc["motorOn"] = motorOn;
  doc["sensePressed"] = fastloaderBusy() || senseIsPressed();
  doc["fastloaderActive"] = fastPhase != FastPhase::OFF;
  doc["fastloaderBytes"] = fastSent;
  doc["fastloaderTotalBytes"] = fastPhase == FastPhase::OFF ? 0 : fastLength;
  doc["fastloaderElapsedMs"] = fastDataStarted ?
      uint32_t((fastPhase == FastPhase::DONE ? fastDataFinishedMs : millis()) - fastDataStartedMs) : 0;
  doc["fastloaderWaitingForData"] = fastWaitingForData;
  doc["fastloaderWaitingForMotor"] = fastPhase == FastPhase::PAYLOAD && !fastRequestPending;
  doc["playing"] = (fastPhase == FastPhase::RECEIVER || fastPhase == FastPhase::DESCRIPTOR || fastPhase == FastPhase::PAYLOAD) || (motorOn && senseIsPressed() && tapeLoaded &&
      (state == State::STREAMING || state == State::DRAINING));
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
  if (fastPhase == FastPhase::RECEIVER || (fastPhase == FastPhase::TAPE && fastSelecting)) phase = "bootstrap";
  if (fastPhase == FastPhase::DESCRIPTOR || fastPhase == FastPhase::PAYLOAD) phase = "fastload";
  doc["phase"] = phase;
  uint64_t played = playedBaseCycles + (uint64_t)currentRunBits() * CYCLES_PER_BIT;
  if (totalTapeCycles && played > totalTapeCycles) played = totalTapeCycles;
  if (fastPhase != FastPhase::OFF && fastPhase != FastPhase::TAPE)
    played = totalTapeCycles + prgEncoder.standardDataCycles(fastSent);
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
  doc["error"] = playerError;
  doc["sourceWaiting"] = sourceWaiting;
  doc["sourceWaits"] = sourceWaitCount;
}

static void sendStateJson(JsonDocument& doc) {
  if (doc.overflowed()) {
    HTTPServer.send(500, "application/json", "{\"error\":\"state_overflow\"}");
    return;
  }
  String response;
  if (!response.reserve(measureJson(doc))) {
    HTTPServer.send(503, "application/json", "{\"error\":\"low_memory\"}");
    return;
  }
  serializeJson(doc, response);
  HTTPServer.sendHeader("Cache-Control", "no-store");
  HTTPServer.send(200, "application/json", response);
}

void handleBuffer() {
  DynamicJsonDocument doc(768);
  if (doc.capacity() == 0) {
    HTTPServer.send(503, "application/json", "{\"error\":\"low_memory\"}");
    return;
  }
  writeTransportState(doc);
  sendStateJson(doc);
}

void handleState() {
  // The ESP8266 continuation stack is only 4 KB. Keep the JSON arena off
  // that stack so HTTP/TCP calls plus a GPIO ISR still have room.
  DynamicJsonDocument doc(2048);
  if (doc.capacity() == 0) {
    HTTPServer.send(503, "application/json", "{\"error\":\"low_memory\"}");
    return;
  }
  noInterrupts();
  const WriteDecoder writeSnapshot = writeDecoder;
  interrupts();
  writeTransportState(doc);
  doc["firmwareCommit"] = FIRMWARE_COMMIT;
  doc["wifiSleepMode"] = int(WiFi.getSleepMode());
  doc["machine"] = profileIsC16(sourceProfile) ? "c16" : "c64";
  doc["videoStandard"] = profileIsNtsc(sourceProfile) ? "ntsc" : "pal";
  doc["fileName"] = tapeLoaded ? tapeName : "";
  doc["gpioDisabled"] = bool(ESPSETTE_GPIO_DISABLED);
  doc["autoPlay"] = autoPlay;
  doc["defaultMachine"] = profileName(defaultProfile);
  writeFastloaderConfig(doc.createNestedObject("fastloader"), fastloaderSettings);
  const auto* reset = ESP.getResetInfoPtr();
  doc["resetReason"] = reset->reason;
  if (reset->reason == REASON_EXCEPTION_RST) {
    doc["resetException"] = reset->exccause;
    doc["resetPc"] = reset->epc1;
  }
  doc["freeStack"] = ESP.getFreeContStack();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["maxFreeBlock"] = ESP.getMaxFreeBlockSize();
  doc["sourceDataAgeMs"] = millis() - lastSourceDataMs;
  doc["sourceDataAgeAtErrorMs"] = sourceDataAgeAtErrorMs;
  doc["dmaServiceReady"] = dmaServiceReady;
  doc["maxDmaPumpGapUs"] = maxDmaPumpGapUs;
  doc["i2sUnderruns"] = i2sUnderrunCount;
  doc["sourceUnderruns"] = sourceUnderrunCount;
  doc["sourceWaiting"] = sourceWaiting;
  doc["sourceWaits"] = sourceWaitCount;
  doc["sourceReadOffset"] = sourceReadOffset;
  doc["sourceWindowStart"] = sourceWindowStart;
  doc["sourceReceivedUntil"] = sourceReceivedUntil;
  doc["uploadErrors"] = uploadErrorCount;
  doc["error"] = playerError;
  doc["writeEnabled"] = !bool(ESPSETTE_GPIO_DISABLED);
  doc["writeListening"] = bool(writeActive) && writeCommandsAllowed();
  doc["writeStatus"] = otaUpdating ? "suspended_ota" :
      tapeSource.kind() != TapeSource::Kind::EMPTY ? "requires_empty_deck" :
      writeCommandsAllowed() ? writeSnapshot.status() : "suspended_fastloader";
  doc["writeCommand"] = writeSnapshot.command();
  doc["writeCommandId"] = writeSnapshot.commandCount();
  doc["writeName"] = writeSnapshot.name();
  doc["writeMachine"] = writeSnapshot.machineName();
  doc["writeType"] = writeSnapshot.fileType();
  doc["writeStart"] = writeSnapshot.startAddress();
  doc["writeEnd"] = writeSnapshot.endAddress();
  doc["writeHeaders"] = writeSnapshot.receivedCount();
  doc["writeErrors"] = writeSnapshot.errorCount();
  doc["writeOverflows"] = writeSnapshot.overflowCount();
  doc["otaReady"] = otaReady;
  doc["otaFreeBytes"] = ESP.getFreeSketchSpace();
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
  sendStateJson(doc);
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
  bool useFast = false;
  if ((type.length() && type != "tap" && !isPrg) ||
      !parseDecimal(HTTPServer.arg("size"), fileSize) ||
      HTTPServer.arg("name").length() > 64) {
    HTTPServer.send(400, "text/plain", "Invalid file type or size");
    return;
  }
  uint32_t logicalSize;
  uint32_t firstOffset;
  if (isPrg) {
    const String video = HTTPServer.arg("video");
    const String addressHex = HTTPServer.arg("header");
    if (fileSize < 3 || addressHex.length() != 4 ||
        (HTTPServer.arg("machine") != "c64" && HTTPServer.arg("machine") != "c16") ||
        (video.length() && video != "pal" && video != "ntsc") ||
        (HTTPServer.arg("turbo") != "0" && HTTPServer.arg("turbo") != "1")) {
      HTTPServer.send(400, "text/plain", "Invalid C64/C16 PRG options");
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
        !prgEncoder.begin(loadAddress, dataLength, displayName, machine, video == "ntsc")) {
      HTTPServer.send(400, "text/plain", "PRG address range is invalid");
      return;
    }
    useFast = HTTPServer.arg("turbo") == "1" && fastloaderSettings.enabled;
    if (useFast) {
      const bool ted = machine == PrgEncoder::Machine::C16;
      uint32_t entry = 0;
      if ((HTTPServer.hasArg("entry") && !parseDecimal(HTTPServer.arg("entry"), entry)) ||
          entry > 65535 || loadAddress < (ted ? 0x1001 : 0x0801) ||
          (uint32_t)loadAddress + dataLength > (ted ? 0x4000u : 0x8000u)) {
        HTTPServer.send(400, "text/plain", "Fast PRG must fit BASIC RAM; invalid entry/address");
        return;
      }
      if (!prepareFastPrg(loadAddress, dataLength, displayName, machine, video == "ntsc", entry)) {
        HTTPServer.send(400, "text/plain", "Invalid fast PRG");
        return;
      }
    }
    logicalSize = useFast ? fileSize : 2 + 2 * dataLength;
    firstOffset = 2;
    totalTapeCycles = prgEncoder.totalCycles();
    reportedTotalCycles = totalTapeCycles + (useFast ? prgEncoder.standardDataCycles(dataLength) : 0);
    sourceProfile = computerProfile(machine == PrgEncoder::Machine::C16, video == "ntsc");
    sourceClockHz = prgEncoder.clockHz();
    tapHalfWaves = false;
  } else {
    if (!decodeHeader(HTTPServer.arg("header"), prefix) ||
        !validStreamHeader(prefix, fileSize) ||
        (HTTPServer.hasArg("cycles") &&
         !parseDecimal64(HTTPServer.arg("cycles"), cycles))) {
      HTTPServer.send(400, "text/plain", "Supported: C64/C16 PAL/NTSC TAP v1/v2");
      return;
    }
    logicalSize = fileSize;
    firstOffset = 20;
    totalTapeCycles = 0;
    reportedTotalCycles = cycles; // Browser metadata is display-only.
    sourceProfile = computerProfile(prefix[13] == 2, prefix[14] == 1);
    sourceClockHz = profileClockHz(sourceProfile);
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
  resetFastloader();
  if (useFast) {
    fastSessionSettings = fastloaderSettings;
    fastPhase = FastPhase::TAPE;
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
    tapeLoaded = tapeSource.freeBytes() == 0 ||
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
      if (sourceFormat == SourceFormat::PRG && fastPhase == FastPhase::OFF)
        prgEncoder.observeFirstCopy(tapeSource.nextOffset() - upload.currentSize,
                                    upload.buf, upload.currentSize);
      lastSourceDataMs = millis();
      uploadBytes += upload.currentSize;
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadError = 3;
  }
}

void handleChunk() {
  if (!uploadSeen || (uploadError == 0 && uploadBytes == 0)) uploadError = 3;
  if (uploadError == 0 && !tapeLoaded &&
      (tapeSource.freeBytes() == 0 || tapeSource.nextOffset() == tapEnd)) {
    tapeLoaded = true;
  }
  // Successful uploads carry the compact live state, avoiding a follow-up GET.
  DynamicJsonDocument reply(uploadError ? 128 : 768);
  if (reply.capacity() == 0) {
    HTTPServer.send(503, "application/json", "{\"error\":\"low_memory\"}");
    uploadSeen = false;
    return;
  }
  if (!uploadError) writeTransportState(reply);
  reply["nextOffset"] = tapeSource.nextOffset();
  reply["freeBytes"] = tapeSource.freeBytes();
  reply["loaded"] = tapeLoaded;
  if (uploadError) {
    ++uploadErrorCount;
    reply["error"] = uploadError;
  }
  if (uploadError) {
    String response;
    serializeJson(reply, response);
    HTTPServer.send(409, "application/json", response);
  } else sendStateJson(reply);
  uploadSeen = false;
}

void handleSense() {
  const String pressed = HTTPServer.arg("pressed");
  if (pressed != "0" && pressed != "1") {
    HTTPServer.send(400, "application/json", "{\"error\":\"invalid_sense\"}");
    return;
  }
  if (pressed == "0" && fastloaderBusy()) {
    resetFastloader();
    state = State::ERROR;
    playerError = "fastloader_cancelled";
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
  ComputerProfile requested;
  if (!parseComputerProfile(HTTPServer.arg("machine").c_str(), requested)) {
    HTTPServer.send(400, "application/json", "{\"error\":\"invalid_machine\"}");
    return;
  }
  // Compare normalized profiles so legacy PAL names also skip identical writes.
  if (requested != defaultProfile) {
    const char* name = profileName(requested);
    File file = LittleFS.open("/machine.cfg", "w");
    if (!file) {
      HTTPServer.send(500, "application/json", "{\"error\":\"machine_save_failed\"}");
      return;
    }
    const bool saved = file.print(name) == strlen(name);
    file.close();
    if (!saved) {
      HTTPServer.send(500, "application/json", "{\"error\":\"machine_save_failed\"}");
      return;
    }
    defaultProfile = requested;
  }
  char reply[48];
  snprintf(reply, sizeof(reply), "{\"defaultMachine\":\"%s\"}", profileName(defaultProfile));
  HTTPServer.send(200, "application/json", reply);
}

void handleEject() {
  if (motorBlocksEdit() || !stopPlaybackForEdit()) {
    HTTPServer.send(409, "text/plain", "Stop MOTOR before ejecting");
    return;
  }
  resetFastloader();
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
  resetPlayer(sourceFormat == SourceFormat::PRG ? 2 : 20);
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
