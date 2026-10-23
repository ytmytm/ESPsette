// Run on macOS with: c++ -std=c++17 -Isrc tests/write_decoder_test.cpp
//                       src/write_decoder.cpp -o /tmp/write-decoder-test &&
//                       /tmp/write-decoder-test
#include <assert.h>
#include <stdint.h>

#include "write_decoder.h"

static void pulse(WriteDecoder& decoder, uint16_t duration) {
  decoder.feedMicros(duration);
}

static void byte(WriteDecoder& decoder, uint8_t value) {
  pulse(decoder, 690);
  pulse(decoder, 544);
  uint8_t parity = 1;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    const bool one = (value >> bit) & 1;
    pulse(decoder, one ? 544 : 390);
    pulse(decoder, one ? 390 : 544);
    parity ^= one;
  }
  pulse(decoder, parity ? 544 : 390);
  pulse(decoder, parity ? 390 : 544);
}

static void block(WriteDecoder& decoder, const uint8_t* header,
                  bool first, bool corruptCopy = false) {
  for (int value = first ? 137 : 9; value >= (first ? 129 : 1); --value)
    byte(decoder, value);
  uint8_t checksum = 0;
  for (int i = 0; i < 192; ++i) {
    const uint8_t value = corruptCopy && i == 10 ? header[i] ^ 1 : header[i];
    byte(decoder, value);
    checksum ^= value;
  }
  byte(decoder, checksum);
  pulse(decoder, 690);
  pulse(decoder, 390);
}

int main() {
  uint8_t header[192];
  for (auto& value : header) value = ' ';
  header[0] = 1;
  header[1] = 1; header[2] = 8; // $0801
  header[3] = 16; header[4] = 8; // $0810
  const char name[] = "ESP:TEST";
  for (int i = 0; i < 8; ++i) header[5 + i] = name[i];

  WriteDecoder good;
  good.motorStart();
  block(good, header, true);
  for (int i = 0; i < 79; ++i) pulse(good, 390);
  block(good, header, false);
  good.motorStop();
  assert(good.receivedCount() == 1);
  assert(good.errorCount() == 0);
  assert(good.startAddress() == 0x0801);
  assert(good.endAddress() == 0x0810);
  assert(good.name()[0] == 'E');

  WriteDecoder damaged;
  damaged.motorStart();
  block(damaged, header, true);
  block(damaged, header, false, true);
  damaged.motorStop();
  assert(damaged.receivedCount() == 0);
  assert(damaged.errorCount() > 0);
}
