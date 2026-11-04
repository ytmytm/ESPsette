// Run on macOS with: c++ -std=c++17 -Isrc tests/write_decoder_test.cpp
//                       src/write_decoder.cpp -o /tmp/write-decoder-test &&
//                       /tmp/write-decoder-test
#include <assert.h>
#include <stdint.h>
#include <cstring>
#include <initializer_list>

#include "write_decoder.h"

static void pulse(WriteDecoder& decoder, uint16_t duration) {
  decoder.feedMicros(duration);
}

static void byte(WriteDecoder& decoder, uint8_t value, bool c16 = false,
                 bool pi1551Timings = false, bool badParity = false) {
  const uint16_t shortPulse = c16 ? (pi1551Timings ? 480 : 505) : 390;
  const uint16_t mediumPulse = c16 ? (pi1551Timings ? 960 : 1010) : 544;
  const uint16_t longPulse = c16 ? (pi1551Timings ? 1920 : 2021) : 690;
  pulse(decoder, longPulse);
  pulse(decoder, mediumPulse);
  uint8_t parity = 1;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    const bool one = (value >> bit) & 1;
    pulse(decoder, one ? mediumPulse : shortPulse);
    pulse(decoder, one ? shortPulse : mediumPulse);
    parity ^= one;
  }
  const bool parityBit = bool(parity) ^ badParity;
  pulse(decoder, parityBit ? mediumPulse : shortPulse);
  pulse(decoder, parityBit ? shortPulse : mediumPulse);
}

static void block(WriteDecoder& decoder, const uint8_t* header,
                  bool first, bool c16 = false, bool corruptCopy = false,
                  bool terminator = true, bool pi1551Timings = false,
                  bool badChecksum = false, bool badParity = false) {
  for (int value = first ? 137 : 9; value >= (first ? 129 : 1); --value)
    byte(decoder, value, c16, pi1551Timings);
  uint8_t checksum = 0;
  for (int i = 0; i < 192; ++i) {
    const uint8_t value = corruptCopy && i == 10 ? header[i] ^ 1 : header[i];
    byte(decoder, value, c16, pi1551Timings, badParity && i == 10);
    checksum ^= value;
  }
  byte(decoder, checksum ^ (badChecksum ? 1 : 0), c16, pi1551Timings);
  if (terminator) {
    pulse(decoder, c16 ? (pi1551Timings ? 1920 : 2021) : 690);
    pulse(decoder, c16 ? (pi1551Timings ? 960 : 1010) : 390);
  }
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

  // Pi1551's native C16 SAVE fixture places the two copies consecutively.
  WriteDecoder plus4WithoutTerminator;
  plus4WithoutTerminator.motorStart();
  block(plus4WithoutTerminator, header, true, true, false, false, true);
  for (int i = 0; i < 64; ++i) pulse(plus4WithoutTerminator, 480);
  block(plus4WithoutTerminator, header, false, true, false, false, true);
  plus4WithoutTerminator.motorStop();
  assert(plus4WithoutTerminator.receivedCount() == 1);
  assert(plus4WithoutTerminator.errorCount() == 0);

  for (int machine = 0; machine < 2; ++machine) {
    const bool c16 = machine == 1;
    WriteDecoder badParity;
    badParity.motorStart();
    block(badParity, header, true, c16, false, true, false, false, true);
    badParity.motorStop();
    assert(badParity.receivedCount() == 0);
    assert(badParity.errorCount() > 0);

    WriteDecoder badChecksum;
    badChecksum.motorStart();
    block(badChecksum, header, true, c16, false, true, false, true);
    badChecksum.motorStop();
    assert(badChecksum.receivedCount() == 0);
    assert(badChecksum.errorCount() > 0);
  }
  assert(std::strcmp(good.command(), "TEST") == 0);
  assert(good.commandCount() == 1);
  assert(std::strcmp(good.status(), "command_received_no_action") == 0);
  // Payload after a verified header is never interpreted as another request.
  WriteDecoder dataIgnored;
  dataIgnored.motorStart();
  block(dataIgnored, header, true);
  block(dataIgnored, header, false);
  block(dataIgnored, header, true);
  block(dataIgnored, header, false);
  assert(dataIgnored.commandCount() == 1);
  dataIgnored.overflow();
  block(dataIgnored, header, true);
  block(dataIgnored, header, false);
  assert(dataIgnored.commandCount() == 1);
  assert(dataIgnored.overflowCount() == 0); // Lost discarded payload is not a command-frame error.

  // Fastloader isolation discards partial frames and ignores even valid frames.
  WriteDecoder suspended;
  suspended.motorStart();
  block(suspended, header, true);
  suspended.suspend();
  block(suspended, header, false);
  block(suspended, header, true);
  block(suspended, header, false);
  assert(suspended.commandCount() == 0);
  assert(suspended.receivedCount() == 0);
  assert(std::strcmp(suspended.status(), "suspended_fastloader") == 0);
  suspended.motorStart();
  block(suspended, header, true);
  block(suspended, header, false);
  assert(suspended.commandCount() == 1);

  // Ordinary filenames and an empty reserved prefix do not publish commands.
  for (const char* ordinary : {"PROGRAM", "ESP:"}) {
    std::memset(header + 5, ' ', 16);
    std::memcpy(header + 5, ordinary, std::strlen(ordinary));
    suspended.motorStart();
    block(suspended, header, true);
    block(suspended, header, false);
    assert(suspended.commandCount() == 1);
    assert(std::strcmp(suspended.command(), "TEST") == 0);
    assert(std::strcmp(suspended.status(), "header_ignored") == 0);
  }
  std::memcpy(header + 5, "ESP:ABCDEFGHIJKL", 16);
  suspended.motorStart();
  block(suspended, header, true);
  block(suspended, header, false);
  assert(suspended.commandCount() == 2);
  assert(std::strcmp(suspended.command(), "ABCDEFGHIJKL") == 0);
  suspended.motorStart();
  block(suspended, header, true, false, false, true, false, true);
  block(suspended, header, false);
  assert(suspended.commandCount() == 2); // Damaged headers cannot replace the last command.
  assert(std::strcmp(suspended.command(), "ABCDEFGHIJKL") == 0);
  suspended.overflow();
  assert(suspended.overflowCount() == 1);
  assert(suspended.commandCount() == 2);

}
