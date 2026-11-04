#pragma once
#include <stdint.h>

// Microseconds, shared by persistent configuration, HTTP validation and sender.
struct FastloaderSetting {
  const char* name;
  uint16_t initial, minimum, maximum;
};
static constexpr FastloaderSetting FASTLOADER_FIELDS[] = {
  {"setupUs", 3, 1, 1000},
  {"holdC64Us", 16, 1, 1000},
  {"holdTedUs", 16, 1, 1000},
  {"gapUs", 3, 1, 1000},
  {"bytePauseC64Us", 19, 0, 5000},
  {"bytePauseTedUs", 11, 0, 5000},
  {"bootHoldUs", 80, 1, 1000},
  {"bootPauseUs", 80, 0, 5000},
  {"motorOnUs", 5, 0, 10000},
  {"motorOffUs", 1000, 0, 10000},
};
static constexpr unsigned FASTLOADER_FIELD_COUNT = sizeof(FASTLOADER_FIELDS) / sizeof(FASTLOADER_FIELDS[0]);
struct FastloaderSettings {
  bool enabled = false;
  uint16_t us[FASTLOADER_FIELD_COUNT];
  FastloaderSettings() {
    for (unsigned i = 0; i < FASTLOADER_FIELD_COUNT; ++i) us[i] = FASTLOADER_FIELDS[i].initial;
  }
  bool set(unsigned index, uint32_t value) {
    if (index >= FASTLOADER_FIELD_COUNT || value < FASTLOADER_FIELDS[index].minimum ||
        value > FASTLOADER_FIELDS[index].maximum) return false;
    us[index] = value;
    return true;
  }
};
