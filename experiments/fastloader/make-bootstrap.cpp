// Export the production standard encoder as CPU-tick periods for the VICE
// adapter, and as a TAP image for inspecting the ordinary tape phase.
#include "prg_encoder.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <iostream>

static void little32(std::ostream& out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) out.put((char)(value >> (8 * i)));
}

int main(int argc, char** argv) {
  if (argc != 7) {
    std::cerr << "usage: make-bootstrap c64|c16 pal|ntsc stub.prg periods.bin bootstrap.tap name\n";
    return 2;
  }
  const bool c16 = std::string(argv[1]) == "c16";
  const bool ntsc = std::string(argv[2]) == "ntsc";
  std::ifstream input(argv[3], std::ios::binary);
  const std::vector<uint8_t> stub((std::istreambuf_iterator<char>(input)), {});
  const uint16_t entry = c16 ? 0x0348 : 0x0351;
  if (stub.size() < 3 || (stub[0] | (stub[1] << 8)) != entry) return 2;
  const uint8_t stream[] = {2, 3, (uint8_t)entry, (uint8_t)(entry >> 8),
                                 (uint8_t)entry, (uint8_t)(entry >> 8)};
  TapeSource source;
  source.useEmbedded(stream, sizeof(stream));
  PrgEncoder encoder;
  if (!encoder.beginAutostart(stub.data() + 2, stub.size() - 2, argv[6],
      c16 ? PrgEncoder::Machine::C16 : PrgEncoder::Machine::C64, ntsc)) return 2;
  encoder.observeFirstCopy(2, stream + 2, 2);
  std::ofstream periods(argv[4], std::ios::binary);
  std::vector<uint8_t> tap;
  auto record = [&](uint32_t cycles) {
    if (cycles % 8 == 0 && cycles / 8 <= 255) tap.push_back(cycles / 8);
    else {
      tap.push_back(0);
      for (unsigned i = 0; i < 3; ++i) tap.push_back(cycles >> (8 * i));
    }
  };
  for (uint32_t i = 0; i < encoder.totalPulses(); ++i) {
    uint32_t cycles = 0, offset = 0;
    if (encoder.pulseAt(i, source, cycles, offset) != PrgEncoder::Result::PULSE) return 2;
    little32(periods, c16 ? 2 * cycles : cycles);
    if (c16) { record(cycles / 2); record(cycles / 2); }
    else record(cycles);
  }
  std::ofstream output(argv[5], std::ios::binary);
  output.write(c16 ? "C16-TAPE-RAW" : "C64-TAPE-RAW", 12);
  output.put(c16 ? 2 : 1);
  output.put(c16 ? 2 : 0);
  output.put(ntsc ? 1 : 0);
  output.put(0);
  little32(output, tap.size());
  output.write((const char*)tap.data(), tap.size());
  return periods && output ? 0 : 2;
}
