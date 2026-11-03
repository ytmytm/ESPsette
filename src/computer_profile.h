#pragma once

#include <stdint.h>
#include <string.h>

// VICE vice/src/tape/tap.c clklist. C16 timers use the slow (half CPU) clock.
// https://github.com/VICE-Team/svn-mirror/blob/main/vice/src/tape/tap.c
// C16 PAL: (1773447 + 1) / 2; C16 NTSC: (1789772 + 1) / 2.
enum class ComputerProfile : uint8_t { C64_PAL, C64_NTSC, C16_PAL, C16_NTSC };

constexpr bool profileIsC16(ComputerProfile profile) {
  return profile == ComputerProfile::C16_PAL || profile == ComputerProfile::C16_NTSC;
}
constexpr bool profileIsNtsc(ComputerProfile profile) {
  return profile == ComputerProfile::C64_NTSC || profile == ComputerProfile::C16_NTSC;
}
constexpr ComputerProfile computerProfile(bool c16, bool ntsc) {
  return c16 ? (ntsc ? ComputerProfile::C16_NTSC : ComputerProfile::C16_PAL)
             : (ntsc ? ComputerProfile::C64_NTSC : ComputerProfile::C64_PAL);
}
constexpr uint32_t profileClockHz(ComputerProfile profile) {
  return profileIsC16(profile) ? (profileIsNtsc(profile) ? 894886UL : 886724UL)
                               : (profileIsNtsc(profile) ? 1022730UL : 985248UL);
}
constexpr const char* profileName(ComputerProfile profile) {
  return profileIsC16(profile) ? (profileIsNtsc(profile) ? "c16-ntsc" : "c16-pal")
                               : (profileIsNtsc(profile) ? "c64-ntsc" : "c64-pal");
}
inline bool parseComputerProfile(const char* name, ComputerProfile& profile) {
  if (!name) return false;
  if (strcmp(name, "c64") == 0 || strcmp(name, "c64-pal") == 0)
    profile = ComputerProfile::C64_PAL;
  else if (strcmp(name, "c64-ntsc") == 0) profile = ComputerProfile::C64_NTSC;
  else if (strcmp(name, "c16") == 0 || strcmp(name, "c16-pal") == 0)
    profile = ComputerProfile::C16_PAL;
  else if (strcmp(name, "c16-ntsc") == 0) profile = ComputerProfile::C16_NTSC;
  else return false;
  return true;
}
