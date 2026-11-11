#pragma once
#include <stdint.h>

// C64 stores reach RAM beneath BASIC ROM, stopping before I/O at $D000.
// TED stores beneath ROM through $FCFF; larger programs need expanded RAM.
constexpr bool fastloaderPrgFits(uint16_t address, uint32_t length, bool ted) {
  return length > 0 && address >= (ted ? 0x1001u : 0x0801u) &&
         address < (ted ? 0xfd00u : 0xd000u) &&
         length <= (ted ? 0xfd00u : 0xd000u) - address;
}
