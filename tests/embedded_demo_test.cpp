// Read the real demo through both logical layouts, and compare every standard
// pulse with a physically duplicated reference. Optional TAP export for VICE.
#include "prg_encoder.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstring>

int main(int argc, char** argv) {
  std::ifstream input("assets/demo/circles.prg", std::ios::binary);
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
  assert(data.size() == 7744 && data[0] == 1 && data[1] == 8);
  TapeSource source;
  assert(!source.useEmbeddedPrg(data.data(), 2, true));
  for (bool repeat : {false, true}) {
    assert(source.useEmbeddedPrg(data.data(), data.size(), repeat));
    assert(source.kind() == TapeSource::Kind::EMBEDDED);
    assert(source.prefixSize() == 2 && source.sessionId() == 0);
    const size_t length = data.size() + (repeat ? data.size() - 2 : 0);
    assert(source.size() == length && source.nextOffset() == length);
    assert(source.freeBytes() == 0 && source.bufferedBytes() == 0);
    source.releaseBefore(length); // Embedded PRGs remain replayable.
    for (size_t i = 0; i < length; ++i) {
      uint8_t value;
      assert(source.read(i, value));
      assert(value == data[i < data.size() ? i : i - (data.size() - 2)]);
    }
    uint8_t value;
    assert(!source.read(length, value));
  }
  // Last iteration selected the repeated standard layout.
  auto copied = data;
  copied.insert(copied.end(), data.begin() + 2, data.end());
  TapeSource reference;
  reference.useEmbedded(copied.data(), copied.size());
  PrgEncoder encoder;
  assert(encoder.begin(0x0801, data.size() - 2, "CIRCLES", PrgEncoder::Machine::C64));
  encoder.observeFirstCopy(2, data.data() + 2, data.size() - 2);
  std::vector<uint8_t> tap;
  for (uint32_t i = 0; i < encoder.totalPulses(); ++i) {
    uint32_t actual, expected, a, b;
    assert(encoder.pulseAt(i, source, actual, a) == PrgEncoder::Result::PULSE);
    assert(encoder.pulseAt(i, reference, expected, b) == PrgEncoder::Result::PULSE);
    assert(actual == expected && a == b);
    if (actual % 8 == 0 && actual / 8 <= 255) tap.push_back(actual / 8);
    else {
      tap.push_back(0);
      for (unsigned j = 0; j < 3; ++j) tap.push_back(actual >> (8 * j));
    }
  }
  if (argc == 2) {
    std::ofstream output(argv[1], std::ios::binary);
    uint8_t header[20] = {};
    std::memcpy(header, "C64-TAPE-RAW", 12);
    header[12] = 1;
    for (unsigned i = 0; i < 4; ++i) header[16 + i] = tap.size() >> (8 * i);
    output.write(reinterpret_cast<const char*>(header), sizeof(header));
    output.write(reinterpret_cast<const char*>(tap.data()), tap.size());
    assert(output.good());
  }
}
