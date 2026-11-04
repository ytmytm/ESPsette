#include "fastloader_settings.h"
#include <cassert>
#include <climits>
int main() {
  FastloaderSettings settings;
  assert(!settings.enabled);
  for (unsigned i = 0; i < FASTLOADER_FIELD_COUNT; ++i) {
    const auto& field = FASTLOADER_FIELDS[i];
    assert(settings.us[i] == field.initial);
    assert(settings.set(i, field.minimum));
    assert(settings.set(i, field.maximum));
    assert(!settings.set(i, field.maximum + 1));
    assert(!settings.set(i, UINT_MAX));
    assert(settings.us[i] == field.maximum);
    if (field.minimum) assert(!settings.set(i, field.minimum - 1));
  }
  assert(!settings.set(FASTLOADER_FIELD_COUNT, 0));
}
