// Soft TAP playback must release ring bytes as the decoder advances; otherwise
// freeBytes hits 0 after one complete ring fill and the next read underruns.
#include "tape_source.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

int main() {
  uint8_t header[20] = {};
  memcpy(header, "C16-TAPE-RAW", 12);
  header[12] = 1;
  header[13] = 2;
  const uint32_t logical = 20 + TapeSource::BUFFER_SIZE + 2048;

  TapeSource src;
  src.useStream(header, logical, 1);

  std::vector<uint8_t> chunk(1024, 0x2f);
  while (src.freeBytes() > 0) {
    const uint16_t n = src.freeBytes() < chunk.size() ? src.freeBytes() : chunk.size();
    assert(src.append(src.nextOffset(), chunk.data(), n));
  }
  assert(src.freeBytes() == 0);
  assert(src.bufferedBytes() == TapeSource::BUFFER_SIZE);

  // Without release, the playhead cannot open space for the next HTTP chunk.
  uint8_t value = 0;
  assert(src.read(20, value));
  assert(src.freeBytes() == 0);

  // Soft pumpTapeSoft must releaseBefore(decoder.offset) like the I2S path.
  src.releaseBefore(20 + 4096);
  assert(src.freeBytes() == 4096);
  assert(src.append(src.nextOffset(), chunk.data(), 1024));
  assert(src.read(20 + 4096, value));
  assert(!src.read(20, value)); // Released window is no longer readable.

  // A non-power-of-two ring must preserve byte order through repeated wraps.
  TapeSource wrapped;
  const uint32_t end = 20 + 6 * TapeSource::BUFFER_SIZE + 73;
  wrapped.useStream(header, end, 2);
  std::vector<uint8_t> patterned(3079);
  while (wrapped.nextOffset() < end) {
    const uint32_t start = wrapped.nextOffset();
    size_t n = wrapped.freeBytes();
    if (n > patterned.size()) n = patterned.size();
    if (n > end - start) n = end - start;
    for (size_t i = 0; i < n; ++i) {
      const uint32_t offset = start + i;
      patterned[i] = uint8_t(offset * 73u + (offset >> 8));
    }
    assert(wrapped.append(start, patterned.data(), n));
    for (uint32_t offset = wrapped.retainedFrom(); offset < wrapped.nextOffset(); ++offset) {
      assert(wrapped.read(offset, value));
      assert(value == uint8_t(offset * 73u + (offset >> 8)));
    }
    const uint32_t release = wrapped.bufferedBytes() < 1777 ? wrapped.bufferedBytes() : 1777;
    wrapped.releaseBefore(wrapped.retainedFrom() + release);
  }

  std::puts("ok");
  return 0;
}
