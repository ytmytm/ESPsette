"""Run the actual firmware producer against a clocked, eight-descriptor DMA model.

Optional full-file replay: ESPSETTE_DMA_TAP=/path/file.tap python3 -m unittest
discover -s tests -p test_tap_dma.py -v
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature + ' {')
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


class TapDmaTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='espsette-tap-dma-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        main = (ROOT / 'src/main.cpp').read_text()
        declarations = main[main.index('struct DecoderState {'):main.index('// Each checkpoint')]
        functions = ''.join(function(main, signature) for signature in (
            'void resetPlayer(uint32_t offset)', 'void IRAM_ATTR onDmaBufferDone()',
            'static uint32_t currentRunBits()', 'void stopI2S()', 'void startTape()',
            'static bool missingSource(uint32_t offset)',
            'bool loadPulse()', 'bool nextTapeBit(uint8_t &bit)', 'void pauseTape()',
            'static void pausePlaybackNow()', 'void pumpTape()',
            'static bool serviceTapeDma()'))
        support = r'''
#include "tape_source.h"
#include "prg_encoder.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#define IRAM_ATTR
#define ESPSETTE_GPIO_DISABLED 0
#define debugE(...) ((void)sizeof(std::snprintf(nullptr, 0, __VA_ARGS__)))
#define debugI(...) ((void)sizeof(std::snprintf(nullptr, 0, __VA_ARGS__)))
enum class State { WAITING, STREAMING, DRAINING, FINISHED, ERROR };
enum class SourceFormat { TAP, PRG };
State state = State::STREAMING;
SourceFormat sourceFormat = SourceFormat::TAP;
TapeSource tapeSource;
PrgEncoder prgEncoder;
constexpr unsigned CYCLES_PER_BIT = 16, DMA_WORDS = 64, DMA_BUFFERS = 8;
constexpr unsigned CHECKPOINT_SLOTS = 16, DRAIN_WORDS = 64, DRAIN_GUARD_US = 40000;
uint32_t tapEnd, sourceUnderrunCount = 0, i2sUnderrunCount = 0;
bool dmaPumpBusy = false, dmaPumpFromYield = false, dmaServiceReady = true;
volatile bool otaUpdating = false;
bool sourceWaiting = false, readDmaActive = true;
uint32_t sourceWaitCount = 0, sourceReadOffset = 0, sourceWindowStart = 0, sourceReceivedUntil = 0;
uint32_t stoppedRunBits = 0, sourceClockHz = 985248;
uint32_t lastSourceDataMs = 0, sourceDataAgeAtErrorMs = 0;
uint32_t lastDmaPumpUs = 0, maxDmaPumpGapUs = 0;
bool motor = true, play = true, fast = false;
bool motorIsOn() { return motor; }
bool motorFell = false, tapeLoaded = true;
uint32_t motorFallCallbacks = 0, motorFallElapsedUs = 0;
using std::min;
void noInterrupts() {}
void interrupts() {}
bool tapeShouldRun() { return motor && play; }
bool fastloaderBusy() { return fast; }
uint32_t generatedTapBits = 0, scheduledWords = 0, committedWords = 0;
volatile uint32_t dmaCallbacks = 0, lastDmaCallbackUs = 0;
volatile bool dmaUnderrun = false;
uint64_t totalTapeCycles = 0, playedBaseCycles = 0, nowUs = 0, wordsPlayed = 0, runBeginUs = 0;
bool tapHalfWaves = false, playbackComplete = false, dmaActive = true;
unsigned freeWords = 0;
uint32_t realBitRate = 985248 / CYCLES_PER_BIT;
const char* playerError = nullptr;
std::vector<uint32_t> output;
uint32_t micros() { return uint32_t(nowUs); }
uint32_t millis() { return uint32_t(nowUs / 1000); }
bool i2s_is_full() { return freeWords == 0; }
bool i2s_write_sample_nb(uint32_t word) {
  if (!freeWords) return false;
  --freeWords;
  output.push_back(word);
  return true;
}
bool i2s_is_empty() { return freeWords >= (DMA_BUFFERS - 1) * DMA_WORDS; }
void i2s_end() { dmaActive = false; }
void idleReadPin() {}
void scopeClear() {}
void i2s_set_rate(uint32_t) {}
bool i2s_rxtxdrive_begin(bool, bool, bool, bool) {
  dmaActive = true; wordsPlayed = 0; freeWords = 0; runBeginUs = nowUs; return true;
}
void i2s_set_callback(void (*)()) {}
float i2s_get_real_rate() { return float(sourceClockHz / CYCLES_PER_BIT) / 32; }
void scopePush(uint8_t) {}
'''
        simulation = r'''
// The core begins with eight silent descriptors. An EOF frees 64 words and
// invokes the firmware callback. No application code runs inside this ISR.
bool backgroundService = false;
uint64_t nextServiceUs = 2000;
void advance(uint32_t us) {
  const uint64_t end = nowUs + us;
  while (nowUs < end) {
    nowUs = std::min(end, nowUs + 1000);
    const uint64_t target = (nowUs - runBeginUs) * realBitRate / (1000000ULL * 32);
    while (dmaActive && wordsPlayed < target) {
      ++wordsPlayed;
      if (wordsPlayed % DMA_WORDS == 0) {
        freeWords = std::min(freeWords + DMA_WORDS, DMA_BUFFERS * DMA_WORDS);
        onDmaBufferDone();
      }
    }
    if (backgroundService && nowUs >= nextServiceUs) {
      serviceTapeDma();
      nextServiceUs = nowUs + 2000;
    }
  }
}

uint64_t referenceBits = 0;
std::vector<uint32_t> reference(const std::vector<uint8_t>& data) {
  std::vector<uint32_t> words;
  uint32_t word = 0, used = 0, remainder = 0, level = 0;
  for (size_t offset = 20; offset < data.size();) {
    uint32_t cycles = data[offset++] * 8;
    if (!cycles) {
      assert(offset + 3 <= data.size());
      cycles = data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16);
      offset += 3;
    }
    assert(cycles);
    cycles += remainder;
    const uint32_t ticks = cycles / CYCLES_PER_BIT;
    remainder = cycles % CYCLES_PER_BIT;
    referenceBits += ticks;
    for (uint32_t tick = 0; tick < ticks; ++tick) {
      const uint32_t bit = tapHalfWaves ? level : tick >= ticks / 2;
      word = (word << 1) | bit;
      if (++used == 32) { words.push_back(word); used = word = 0; }
    }
    if (tapHalfWaves) level ^= 1;
  }
  if (used) words.push_back(word << (32 - used));
  words.insert(words.end(), DRAIN_WORDS, 0);
  return words;
}

int main(int argc, char** argv) {
  assert(argc == 3 || argc == 4);
  const bool isPrg = !strncmp(argv[2], "prg", 3);
  const bool streamedPrg = !strcmp(argv[2], "prg-stream");
  const bool slowNetwork = !strncmp(argv[2], "slow", 4);
  const bool oldUpload = !strcmp(argv[2], "slow1k");
  const bool motorCycles = !strcmp(argv[2], "motor-cycles");
  const bool splitSource = !strncmp(argv[2], "split", 5);
  const bool splitPause = splitSource && strstr(argv[2], "pause");
  const bool lateSource = !strcmp(argv[2], "late-source");
  const bool lostWindow = !strcmp(argv[2], "lost-window");
  const bool eagerSource = !strcmp(argv[2], "eager-source");
  const bool motorBlip = !strcmp(argv[2], "motor-blip");
  const bool pauseTest = !strcmp(argv[2], "motor-pause") || motorBlip;
  const bool oldLimit = !strcmp(argv[2], "old-limit");
  const bool fullOnly = !strcmp(argv[2], "full-only");
  const bool shortBurst = oldLimit || !strcmp(argv[2], "full-refill-80ms");
  const bool expectUnderrun = oldLimit || fullOnly;
  backgroundService = !oldLimit && !fullOnly && !shortBurst;
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
  std::vector<uint8_t> referenceData = data;
  if (isPrg) {
    assert(argc == 4 && data.size() >= 3);
    sourceFormat = SourceFormat::PRG;
    const uint16_t address = data[0] | (data[1] << 8);
    assert(prgEncoder.begin(address, data.size() - 2, "CIRCLES", PrgEncoder::Machine::C64));
    if (streamedPrg) {
      const auto copy = data;
      data.insert(data.end(), copy.begin() + 2, copy.end());
      tapeSource.usePrg(data.data(), data.size(), 1);
    } else {
      prgEncoder.observeFirstCopy(2, data.data() + 2, data.size() - 2);
      assert(tapeSource.useEmbeddedPrg(data.data(), data.size(), true));
    }
    tapEnd = tapeSource.size();
    resetPlayer(2);
    std::ifstream tap(argv[3], std::ios::binary);
    referenceData.assign(std::istreambuf_iterator<char>(tap), {});
    assert(referenceData.size() > 20);
  } else {
    assert(data.size() > 20 && data[12] >= 1 && data[12] <= 2);
    tapEnd = data.size();
    tapHalfWaves = data[12] == 2;
    tapeSource.useStream(data.data(), data.size(), 1);
    resetPlayer(20);
  }
  auto upload = [&]() {
    for (int request = 0; request < 4; ++request) {
      size_t n = std::min(size_t(1024), size_t(tapeSource.freeBytes()));
      n = std::min(n, data.size() - tapeSource.nextOffset());
      if (!n) break;
      const auto offset = tapeSource.nextOffset();
      assert(tapeSource.append(offset, data.data() + offset, n));
      if (streamedPrg) prgEncoder.observeFirstCopy(offset, data.data() + offset, n);
    }
  };
  if (splitSource || lateSource || eagerSource) {
    const size_t n = 4095 + (splitSource ? unsigned(argv[2][5] - '0') : 1);
    assert(tapeSource.append(20, data.data() + 20, n));
  } else {
    // Preload the entire configured source ring (or the short file).
    while (tapeSource.freeBytes() && tapeSource.nextOffset() < data.size()) upload();
  }
  startTape();
  // Yield service must neither re-enter the producer nor run while the motor,
  // PLAY or fastloader ownership prevents ordinary READ.
  freeWords = 64;
  for (int guard = 0; guard < 3; ++guard) {
    dmaPumpBusy = guard == 0; fast = guard == 1; otaUpdating = guard == 2;
    serviceTapeDma();
    assert(output.empty() && freeWords == 64 && !dmaPumpFromYield);
  }
  dmaPumpBusy = fast = otaUpdating = false; motor = play = true; freeWords = 0;
  const auto expected = reference(referenceData);
  uint64_t nextUploadUs = 80000;
  size_t runOutputStart = 0;
  uint64_t runBaseBits = 0;
  unsigned motorStops = 0;
  bool sourceResumed = false;
  auto checkPlayed = [&](uint64_t endBits) {
    for (uint64_t index = runBaseBits; index < endBits; ++index) {
      const uint64_t relative = index - runBaseBits;
      assert(runOutputStart + relative / 32 < output.size());
      assert(((output[runOutputStart + relative / 32] >> (31 - relative % 32)) & 1) ==
             ((expected[index / 32] >> (31 - index % 32)) & 1));
    }
  };
  if (lostWindow) {
    tapeSource.releaseBefore(21); freeWords = 64;
    pumpTape();
    assert(state == State::ERROR && sourceUnderrunCount == 1 && i2sUnderrunCount == 0);
    assert(!strcmp(playerError, "source_window_lost"));
    assert(sourceReadOffset == 20 && sourceWindowStart == 21);
    return 0;
  }
  const uint64_t burstAtUs = 49000000 + 1000000ULL * DMA_BUFFERS * DMA_WORDS * 32 / realBitRate;
  unsigned burst = 0;
  while (state != State::FINISHED && state != State::ERROR && nowUs < 1000000000ULL) {
    pumpTape();
    if (state == State::ERROR || state == State::FINISHED) break;
    if ((splitSource || lateSource) && sourceWaiting && !sourceResumed) {
      const auto before = player.decoder;
      const auto pending = player.pendingWord;
      const auto bits = player.pendingBits;
      const auto words = output.size();
      advance(20000);
      assert(state == State::STREAMING && sourceWaiting && sourceUnderrunCount == 0);
      assert(output.size() == words && pending == player.pendingWord && bits == player.pendingBits);
      assert(before.offset == player.decoder.offset && before.pulseIndex == player.decoder.pulseIndex);
      if (lateSource) {
        advance(1017556);
        assert(state == State::ERROR && sourceUnderrunCount == 1 && i2sUnderrunCount == 0);
        assert(!strcmp(playerError, "buffer_underrun"));
        const auto frozenBits = currentRunBits();
        assert(frozenBits > 0);
        advance(1000000);
        assert(currentRunBits() == frozenBits);
        assert(sourceReadOffset >= sourceReceivedUntil);
        std::puts("late source: actual DMA deadline gives 0/1 and keeps the failure position");
        return 0;
      }
      if (splitPause) {
        const auto endBits = runBaseBits + currentRunBits();
        motorFallCallbacks = dmaCallbacks;
        motorFallElapsedUs = micros() - lastDmaCallbackUs;
        motor = false; motorFell = true;
        advance(17000);
        assert(state == State::WAITING && playedBaseCycles == endBits * CYCLES_PER_BIT);
        checkPlayed(endBits);
        runBaseBits = endBits;
        ++motorStops;
      }
      // Deliver an extended TAP record a byte at a time without padding READ.
      for (int i = 0; i < 4; ++i) {
        const auto offset = tapeSource.nextOffset();
        assert(tapeSource.append(offset, data.data() + offset, 1));
        advance(2000);
      }
      sourceResumed = true;
      upload();
      if (splitPause) {
        runOutputStart = output.size();
        motor = true; startTape();
      }
    }
    if (motorCycles && nowUs - runBeginUs >= 3000000 + (motorStops % 7) * 137000) {
      const auto endBits = runBaseBits + currentRunBits();
      motorFallCallbacks = dmaCallbacks;
      motorFallElapsedUs = micros() - lastDmaCallbackUs;
      motor = false; motorFell = true;
      advance(17000);
      assert(state == State::WAITING && !dmaActive);
      assert(playedBaseCycles == endBits * CYCLES_PER_BIT);
      checkPlayed(endBits);
      advance(1000 + (motorStops % 4) * 70000);
      upload();
      ++motorStops;
      runOutputStart = output.size(); runBaseBits = endBits;
      motor = true; startTape();
      assert(state == State::STREAMING);
    }
    if (pauseTest && nowUs >= 2000000) {
      motorFallCallbacks = dmaCallbacks;
      motorFallElapsedUs = micros() - lastDmaCallbackUs;
      const uint32_t expectedBits = (dmaCallbacks - DMA_BUFFERS) * DMA_WORDS * 32 +
        std::min(uint32_t(DMA_WORDS * 32 - 1), uint32_t(
          uint64_t(motorFallElapsedUs) * realBitRate / 1000000));
      motor = false; motorFell = true; // Physical edge while HTTP is waiting.
      if (motorBlip) motor = true; // Both edges happened before the next yield.
      advance(1017556);
      assert(state == State::WAITING && !dmaActive);
      assert(playedBaseCycles == uint64_t(expectedBits) * CYCLES_PER_BIT);
      assert(!dmaPumpBusy && !dmaPumpFromYield);
      assert(i2sUnderrunCount == 0 && sourceUnderrunCount == 0);
      // The restored decoder must continue at the edge, not at HTTP return.
      for (uint32_t i = 0; i < 2048; ++i) {
        uint8_t bit;
        assert(nextTapeBit(bit));
        const uint32_t index = expectedBits + i;
        assert(bit == ((expected[index / 32] >> (31 - index % 32)) & 1));
      }
      std::puts("MOTOR edge during HTTP: exact pause/decoder restore, I2S/source=0/0");
      return 0;
    }
    const uint64_t slowAt = referenceBits / realBitRate > 400 ? 340000000 : 8000000;
    if (slowNetwork && nowUs >= slowAt && nowUs < slowAt + 20000000) {
      unsigned credit = tapeSource.freeBytes();
      const bool wasWaiting = sourceWaiting;
      advance(1017556); // State response can wait while READ continues.
      for (int request = 0; request < 4 && state == State::STREAMING && credit; ++request) {
        size_t n = std::min(size_t(oldUpload ? 1024 : 4096), size_t(credit));
        n = std::min(n, data.size() - tapeSource.nextOffset());
        if (!n || (!wasWaiting && credit < std::min(size_t(1024), data.size() - tapeSource.nextOffset()))) break;
        advance(500000);
        if (state == State::ERROR) break;
        // HTTPUpload keeps its existing 2048-byte parsing buffer. Appends
        // are incremental; no 4 KB temporary buffer is allocated on the ESP.
        size_t first = std::min(n, size_t(2048));
        auto offset = tapeSource.nextOffset();
        assert(tapeSource.append(offset, data.data() + offset, first));
        advance(517556);
        if (state == State::ERROR) break;
        offset = tapeSource.nextOffset();
        if (first != n) assert(tapeSource.append(offset, data.data() + offset, n - first));
        credit = tapeSource.freeBytes();
      }
      continue;
    }
    // Reproduce both measured HTTP durations: 81197 us in a burst, or
    // 1017556 us while TCP waits. No main-loop producer runs inside HTTP.
    if (!slowNetwork && nowUs >= burstAtUs && burst < 5) {
      advance(shortBurst ? 81197 : 1017556);
      ++burst;
    } else {
      if (nowUs >= nextUploadUs && (!(splitSource || lateSource || eagerSource) || sourceResumed)) {
        upload(); nextUploadUs = nowUs + 80000;
      }
      advance(1000);
    }
  }
  if (slowNetwork && oldUpload) {
    assert(state == State::ERROR && sourceUnderrunCount == 1 && i2sUnderrunCount == 0);
    std::printf("slow 1 KB POSTs: genuine source deadline, I2S/source=0/1 at %.3f s\n", nowUs / 1000000.0);
    return 0;
  }
  if (eagerSource) {
    assert(state == State::ERROR && sourceUnderrunCount == 1 && i2sUnderrunCount == 0);
    std::puts("old eager source failure: I2S/source=0/1 despite queued DMA output");
    return 0;
  }
  assert(sourceUnderrunCount == 0);
  if (motorCycles || splitPause) {
    assert(state == State::FINISHED && i2sUnderrunCount == 0);
    assert(motorStops >= (motorCycles ? 4u : 1u));
    assert(playedBaseCycles == referenceBits * CYCLES_PER_BIT);
    checkPlayed(referenceBits);
    std::printf("%u MOTOR stop/resume cycles: every played bit matches, I2S/source=0/0\n", motorStops);
    return 0;
  }
  if (expectUnderrun) {
    assert(state == State::ERROR && i2sUnderrunCount == 1);
    assert(!strcmp(playerError, "i2s_underrun"));
    std::printf("%s: I2S/source=1/0 at %.3f s\n", argv[2], nowUs / 1000000.0);
  } else {
    assert(state == State::FINISHED && i2sUnderrunCount == 0);
    assert(output == expected); // Every word, including the long pause.
    std::printf("%s: %zu exact words, I2S/source=0/0, %.3f s, pump gap %u us\n",
                argv[2], output.size(), nowUs / 1000000.0, maxDmaPumpGapUs);
  }
}
'''
        eager = functions.replace('sourceWaiting = true;\n    return false;',
                                  'state = State::ERROR; playerError = \"buffer_underrun\";\n'
                                  '    ++sourceUnderrunCount; return false;')
        assert eager != functions
        for name, code in [('current', functions), ('eager-source', eager), ('old-limit', functions.replace(
                'const uint16_t budget = dmaPumpFromYield ? DMA_WORDS : DMA_BUFFERS * DMA_WORDS;',
                'const uint16_t budget = 32;'))]:
            source = cls.directory / (name + '.cpp')
            source.write_text(support + declarations + '\nDecoderState checkpoints[16];\n'
                              'DecoderState runStartDecoder;\nuint32_t checkpointNumber[16];\n' + code + simulation)
            subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-Itests/support', '-Isrc', str(source), 'src/tape_source.cpp',
                            'src/prg_encoder.cpp', '-o', str(cls.directory / name)],
                           cwd=ROOT, check=True)

    def replay(self, path):
        for binary, mode in [('old-limit', 'old-limit'), ('current', 'full-refill-80ms'),
                             ('current', 'full-only'), ('current', 'current'),
                             ('current', 'motor-pause'), ('current', 'motor-blip'), ('current', 'motor-cycles'),
                             ('current', 'slow1k'), ('current', 'slow4k')]:
            subprocess.run([str(self.directory / binary), str(path), mode], check=True)

    def test_http_burst_during_long_tap_pause(self):
        data = bytearray(20)
        data[:12] = b'C64-TAPE-RAW'
        data[12] = 1
        self.assertEqual(len(data), 20)
        # Simple synthetic pulses and the measured pause duration from Rambo;
        # no game program bytes or copyrighted tape image are kept in tests.
        data.extend(bytes([35, 70]) * 57467)
        data.extend(b'\0' + (6302297).to_bytes(3, 'little'))
        data.extend(bytes([35, 70]) * 12000)
        data[16:20] = (len(data) - 20).to_bytes(4, 'little')
        path = self.directory / 'pause.tap'
        path.write_bytes(data)
        self.replay(path)

    def test_partial_record_refill_and_real_source_deadline(self):
        data = bytearray(20)
        data[:12] = b'C64-TAPE-RAW'
        data[12] = 1
        data.extend(bytes([35, 70]) * 2047 + bytes([35]))  # 4095-byte prefix.
        data.extend(b'\0' + (560).to_bytes(3, 'little'))
        data.extend(bytes([35, 70]) * 6000)
        data[16:20] = (len(data) - 20).to_bytes(4, 'little')
        path = self.directory / 'split.tap'
        path.write_bytes(data)
        for version in (1, 2):
            data[12] = version
            path.write_bytes(data)
            for split in (1, 2, 3, '1-pause'):
                with self.subTest(version=version, split=split):
                    subprocess.run([str(self.directory / 'current'), str(path),
                                    f'split{split}'], check=True)
        for binary, mode in [('current', 'late-source'), ('current', 'lost-window'),
                             ('eager-source', 'eager-source')]:
            subprocess.run([str(self.directory / binary), str(path), mode], check=True)

    def test_standard_embedded_prg_during_http_wait(self):
        exporter = self.directory / 'export-demo'
        tap = self.directory / 'circles.tap'
        subprocess.run(['c++', '-std=c++17', '-O2', '-Itests/support', '-Isrc',
                        'tests/embedded_demo_test.cpp', 'src/prg_encoder.cpp',
                        'src/tape_source.cpp', '-o', str(exporter)], cwd=ROOT, check=True)
        subprocess.run([str(exporter), str(tap)], cwd=ROOT, check=True)
        for mode in ('prg', 'prg-stream'):
            subprocess.run([str(self.directory / 'current'),
                            str(ROOT / 'assets/demo/circles.prg'), mode, str(tap)], check=True)

    @unittest.skipUnless(os.environ.get('ESPSETTE_DMA_TAP'), 'optional local TAP replay')
    def test_local_tap(self):
        self.replay(Path(os.environ['ESPSETTE_DMA_TAP']).resolve())


if __name__ == '__main__':
    unittest.main()
