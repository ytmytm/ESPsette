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

  assert(encoder.standardDataCycles(0) == 0);
  PrgEncoder longer;
  assert(longer.begin(0x1001, encoder.dataLength() + 256, "PROGRAM", machine, ntsc));
  assert(longer.totalCycles() - encoder.totalCycles() == encoder.standardDataCycles(256));

  if (c16) {
    // Pi1551's synthetic native tape pulses have 240/480/960 us halfwaves.
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

static void checkAutostart(bool c16, bool ntsc, size_t codeLength,
                           const char* name, const char* expectedName) {
  const Timings t = c16 ? Timings{448, 896, 1792} : Timings{384, 536, 680};
  const auto machine = c16 ? PrgEncoder::Machine::C16 : PrgEncoder::Machine::C64;
  const uint16_t entry = c16 ? 0x0348 : 0x0351;
  const uint8_t stream[] = {2, 3, (uint8_t)entry, (uint8_t)(entry >> 8),
                                  (uint8_t)entry, (uint8_t)(entry >> 8)};
  TapeSource source;
  source.useEmbedded(stream, sizeof(stream));
  std::array<uint8_t, 172> code;
  for (size_t i = 0; i < code.size(); ++i) code[i] = (uint8_t)(i * 73);
  PrgEncoder encoder;
  assert(!encoder.beginAutostart(nullptr, 1, "BOOT", machine, ntsc));
  assert(!encoder.beginAutostart(code.data(), 0, "BOOT", machine, ntsc));
  assert(!encoder.beginAutostart(code.data(), c16 ? 171 : 172, "BOOT", machine, ntsc));
  assert(encoder.beginAutostart(code.data(), codeLength, name, machine, ntsc));
  encoder.observeFirstCopy(2, stream + 2, 2);
  std::array<uint8_t, 192> header;
  header.fill(0x20);
  header[0] = 3;
  header[1] = 2; header[2] = 3;
  header[3] = 4; header[4] = 3;
  header[5] = 147;
  if (!c16) header[6] = 5;
  const size_t nameStart = c16 ? 6 : 7;
  for (size_t i = 0; expectedName[i] && nameStart + i < 21; ++i)
    header[nameStart + i] = expectedName[i];
  for (size_t i = 0; i < codeLength; ++i) header[(c16 ? 22 : 21) + i] = code[i];
  std::vector<uint32_t> expected;
  referenceBlock(expected, header.data(), header.size(), true, true, t, c16);
  referenceBlock(expected, header.data(), header.size(), false, true, t, c16);
  expected.push_back(200000);
  referenceBlock(expected, stream + 2, 2, true, false, t, c16);
  referenceBlock(expected, stream + 4, 2, false, false, t, c16);
  expected.insert(expected.end(), 200, t.shortPulse);
  // The browser ring now contains a real PRG, not the autostart vector.
  // Bootstrap must emit its own vector and retain all payload bytes.
  const uint8_t address[] = {1, 8};
  const uint8_t payload[] = {0xde, 0xad, 0xbe, 0xef};
  source.usePrg(address, 6, 1);
  assert(source.append(2, payload, sizeof(payload)));
  uint64_t total = 0;
  assert(encoder.totalPulses() == expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    uint32_t cycles = 0, offset = 0;
    assert(encoder.pulseAt(i, source, cycles, offset) == PrgEncoder::Result::PULSE);
    assert(cycles == expected[i]);
    assert(offset == 2);
    total += cycles;
  }
  assert(encoder.totalCycles() == total);
}

int main() {
  checkMachine(PrgEncoder::Machine::C16, {448, 896, 1792});
  checkMachine(PrgEncoder::Machine::C64, {384, 536, 680});
  checkMachine(PrgEncoder::Machine::C16, {448, 896, 1792}, true);
  checkMachine(PrgEncoder::Machine::C64, {384, 536, 680}, true);
  const struct { const char* name; const char* expected; } names[] = {
    {"BOOT", "BOOT"}, {"abcdefghijklmnop", "ABCDEFGHIJKLMNOP"},
    {"", "PROGRAM"}, {"!!!", "PROGRAM"}
  };
  for (bool c16 : {false, true}) for (bool ntsc : {false, true})
    for (size_t length : {size_t(1), size_t(c16 ? 170 : 171)})
      for (const auto& name : names) checkAutostart(c16, ntsc, length, name.name, name.expected);
}
