#pragma once
#include <stdint.h>

// C64 stores reach RAM beneath BASIC ROM, stopping before I/O at $D000.
// TED retains the common 16 KB C16 limit for the combined C16/Plus4 profile.
constexpr bool fastloaderPrgFits(uint16_t address, uint32_t length, bool ted) {
  return length > 0 && address >= (ted ? 0x1001u : 0x0801u) &&
         address < (ted ? 0x4000u : 0xd000u) &&
         length <= (ted ? 0x4000u : 0xd000u) - address;
}
