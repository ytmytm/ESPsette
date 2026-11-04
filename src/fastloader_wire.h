#pragma once
#include "fastloader_settings.h"
// Output provides data(WRITE,SENSE), read(level), wait(microseconds).
// Schedule relative to completed writes; interruptions can only extend timing.
template<class Output>
inline __attribute__((always_inline)) void fastloaderSendByte(Output& out, uint8_t value, bool ted,
                                      bool bootstrap, const FastloaderSettings& settings) {
  const uint16_t hold = settings.us[bootstrap ? 6 : ted ? 2 : 1];
  const uint16_t pause = settings.us[bootstrap ? 7 : ted ? 5 : 4];
  for (unsigned pair = 0; pair < 4; ++pair) {
    const unsigned writeBit = bootstrap ? 7 - 2 * pair : pair < 2 ? 4 + 2 * pair : 2 * (pair - 2);
    const unsigned senseBit = bootstrap ? writeBit - 1 : writeBit + 1;
    out.data((value >> writeBit) & 1, (value >> senseBit) & 1);
    out.wait(settings.us[0]);
    out.read(ted ? (pair & 1) : false);
    out.wait(hold);
    if (!ted) out.read(true);
    out.wait(pair == 3 ? pause : settings.us[3]);
  }
}
