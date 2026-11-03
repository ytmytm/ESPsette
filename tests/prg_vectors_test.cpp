// macOS: c++ -std=c++17 -Itests/support -Isrc tests/prg_vectors_test.cpp
//        src/prg_encoder.cpp src/tape_source.cpp -o /tmp/prg-vectors && /tmp/prg-vectors
#include <assert.h>
#include <stdint.h>

#include <array>
#include <vector>

#include "prg_encoder.h"

struct Timings { uint32_t shortPulse, mediumPulse, longPulse; };

// Independent sequential reference, following Pi1551's RecordedBit/Byte
// fixture for C16 and the matching C64 ROM bit-pair convention. Pi1551 uses
// 240/480/960 us halfwaves; WAV-PRG chooses nearby PAL cycle values.
static void referenceByte(std::vector<uint32_t>& out, uint8_t value, Timings t) {
  out.push_back(t.longPulse);
  out.push_back(t.mediumPulse);
  uint8_t parity = 1;
  for (uint8_t i = 0; i < 8; ++i) {
    const bool one = (value >> i) & 1;
    out.push_back(one ? t.mediumPulse : t.shortPulse);
    out.push_back(one ? t.shortPulse : t.mediumPulse);
    parity ^= one;
  }
  out.push_back(parity ? t.mediumPulse : t.shortPulse);
  out.push_back(parity ? t.shortPulse : t.mediumPulse);
}

static void referenceBlock(std::vector<uint32_t>& out, const uint8_t* bytes,
                           size_t length, bool first, bool header, Timings t,
                           bool c16) {
  const size_t pilot = first ? (header ? 20000 : 5000) : 79;
  out.insert(out.end(), pilot, t.shortPulse);
  for (uint8_t i = 0; i < 9; ++i)
    referenceByte(out, (first ? 137 : 9) - i, t);
  uint8_t checksum = 0;
  for (size_t i = 0; i < length; ++i) {
    referenceByte(out, bytes[i], t);
    checksum ^= bytes[i];
  }
  referenceByte(out, checksum, t);
  out.push_back(t.longPulse);
  out.push_back(c16 ? t.mediumPulse : t.shortPulse);
}

static void checkMachine(PrgEncoder::Machine machine, Timings t, bool ntsc = false) {
  const bool c16 = machine == PrgEncoder::Machine::C16;
  const std::array<uint8_t, 3> program = {0xaa, 0xbb, 0xcc};
  const uint8_t address[2] = {1, 8};
  TapeSource source;
  source.usePrg(address, 2 + 2 * program.size(), 1);
  assert(source.append(2, program.data(), program.size()));
  assert(source.append(2 + program.size(), program.data(), program.size()));
  PrgEncoder encoder;
  assert(encoder.begin(0x0801, program.size(), String("TEST"), machine, ntsc));
  encoder.observeFirstCopy(2, program.data(), program.size());

  std::array<uint8_t, 192> header;
  header.fill(0x20);
  header[0] = 1;
  header[1] = 1; header[2] = 8;
  header[3] = 4; header[4] = 8;
  header[5] = 'T'; header[6] = 'E'; header[7] = 'S'; header[8] = 'T';
  std::vector<uint32_t> expected;
  referenceBlock(expected, header.data(), header.size(), true, true, t, c16);
  referenceBlock(expected, header.data(), header.size(), false, true, t, c16);
  expected.push_back(200000);
  const size_t dataStart = expected.size();
  referenceBlock(expected, program.data(), program.size(), true, false, t, c16);
  referenceBlock(expected, program.data(), program.size(), false, false, t, c16);

  assert(encoder.totalPulses() == expected.size());
  assert(encoder.dataStartPulse() == dataStart);
  uint64_t total = 0;
  for (size_t i = 0; i < expected.size(); ++i) {
    uint32_t cycles = 0, offset = 0;
    assert(encoder.pulseAt(i, source, cycles, offset) == PrgEncoder::Result::PULSE);
    assert(cycles == expected[i]);
    total += cycles;
  }
  assert(encoder.totalCycles() == total);
  uint32_t cycles = 0, offset = 0;
  assert(encoder.pulseAt(expected.size(), source, cycles, offset) ==
         PrgEncoder::Result::END);

  if (c16) {
    // Pi1551's synthetic native SAVE pulses have 240/480/960 us halfwaves.
    // Compare wholewave durations with our PAL cycle values (WAV-PRG).
    const uint32_t clock = encoder.clockHz();
    assert(clock == (ntsc ? 894886 : 886724));
    assert(t.shortPulse * 1000000ULL / clock >= 480);
    assert(t.shortPulse * 1000000ULL / clock <= 510);
    assert(t.mediumPulse * 1000000ULL / clock >= 960);
    assert(t.mediumPulse * 1000000ULL / clock <= 1020);
    assert(t.longPulse * 1000000ULL / clock >= 1920);
    assert(t.longPulse * 1000000ULL / clock <= 2040);
  } else assert(encoder.clockHz() == (ntsc ? 1022730 : 985248));
}

int main() {
  checkMachine(PrgEncoder::Machine::C16, {448, 896, 1792});
  checkMachine(PrgEncoder::Machine::C64, {384, 536, 680});
  checkMachine(PrgEncoder::Machine::C16, {448, 896, 1792}, true);
  checkMachine(PrgEncoder::Machine::C64, {384, 536, 680}, true);
}
