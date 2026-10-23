// Run on macOS with: c++ -std=c++17 -Isrc tests/write_decoder_test.cpp
//                       src/write_decoder.cpp -o /tmp/write-decoder-test &&
//                       /tmp/write-decoder-test
#include <assert.h>
#include <stdint.h>

#include "write_decoder.h"

static void pulse(WriteDecoder& decoder, uint16_t duration) {
  decoder.feedMicros(duration);
}

static void byte(WriteDecoder& decoder, uint8_t value, bool c16 = false) {
  const uint16_t shortPulse = c16 ? 505 : 390;
  const uint16_t mediumPulse = c16 ? 1010 : 544;
  const uint16_t longPulse = c16 ? 2021 : 690;
  pulse(decoder, longPulse);
  pulse(decoder, mediumPulse);
  uint8_t parity = 1;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    const bool one = (value >> bit) & 1;
    pulse(decoder, one ? mediumPulse : shortPulse);
    pulse(decoder, one ? shortPulse : mediumPulse);
    parity ^= one;
  }
  pulse(decoder, parity ? mediumPulse : shortPulse);
  pulse(decoder, parity ? shortPulse : mediumPulse);
}

static void block(WriteDecoder& decoder, const uint8_t* header,
                  bool first, bool c16 = false, bool corruptCopy = false) {
  for (int value = first ? 137 : 9; value >= (first ? 129 : 1); --value)
    byte(decoder, value, c16);
  uint8_t checksum = 0;
  for (int i = 0; i < 192; ++i) {
    const uint8_t value = corruptCopy && i == 10 ? header[i] ^ 1 : header[i];
    byte(decoder, value, c16);
    checksum ^= value;
  }
  byte(decoder, checksum, c16);
  pulse(decoder, c16 ? 2021 : 690);
  pulse(decoder, c16 ? 1010 : 390);
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
  assert(good.machineName()[1] == '6'); // c64
  assert(good.errorCount() == 0);
  assert(good.startAddress() == 0x0801);
  assert(good.endAddress() == 0x0810);
  assert(good.name()[0] == 'E');

  WriteDecoder damaged;
  damaged.motorStart();
  block(damaged, header, true);
  block(damaged, header, false, false, true);
  damaged.motorStop();
  assert(damaged.receivedCount() == 0);
  assert(damaged.errorCount() > 0);

  WriteDecoder plus4;
  plus4.motorStart();
  block(plus4, header, true, true);
  for (int i = 0; i < 79; ++i) pulse(plus4, 505);
  block(plus4, header, false, true);
  plus4.motorStop();
  assert(plus4.receivedCount() == 1);
  assert(plus4.errorCount() == 0);
  assert(plus4.machineName()[1] == '1'); // c16 / Plus4
}
