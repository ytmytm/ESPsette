#include <assert.h>
#include <string.h>
#include <initializer_list>
#include "computer_profile.h"

int main() {
  struct Case { const char* name; ComputerProfile profile; bool c16, ntsc; uint32_t clock; };
  const Case cases[] = {
    {"c64-pal", ComputerProfile::C64_PAL, false, false, 985248},
    {"c64-ntsc", ComputerProfile::C64_NTSC, false, true, 1022730},
    {"c16-pal", ComputerProfile::C16_PAL, true, false, 886724},
    {"c16-ntsc", ComputerProfile::C16_NTSC, true, true, 894886}
  };
  for (const auto& test : cases) {
    ComputerProfile profile = ComputerProfile::C64_PAL;
    assert(parseComputerProfile(test.name, profile));
    assert(profile == test.profile);
    assert(strcmp(profileName(profile), test.name) == 0);
    assert(profileIsC16(profile) == test.c16);
    assert(profileIsNtsc(profile) == test.ntsc);
    assert(profileClockHz(profile) == test.clock);
    assert(computerProfile(test.c16, test.ntsc) == profile);
  }
  ComputerProfile profile = ComputerProfile::C16_NTSC;
  assert(parseComputerProfile("c64", profile) && profile == ComputerProfile::C64_PAL);
  assert(parseComputerProfile("c16", profile) && profile == ComputerProfile::C16_PAL);
  for (const char* invalid : {"", "vic20", "c16-secam", "c64-pal-n"}) {
    assert(!parseComputerProfile(invalid, profile));
    assert(profile == ComputerProfile::C16_PAL);
  }
  assert(!parseComputerProfile(nullptr, profile));
}
