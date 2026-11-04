#include "fastloader_wire.h"
#include <cassert>
#include <vector>
struct Sample { unsigned time; bool write, sense; };
struct Output {
  bool w = true, s = true, r = true, ted;
  unsigned time = 0, lastData = 0;
  std::vector<Sample> samples;
  void data(bool write, bool sense) { w = write; s = sense; lastData = time; }
  void wait(unsigned us) { time += us; }
  void read(bool high) {
    if (high != r && (ted || !high)) {
      assert(time - lastData >= 3);
      samples.push_back({time,w,s});
    }
    r = high;
  }
};
int main() {
  for (bool ted : {false,true}) for (bool boot : {false,true}) {
    FastloaderSettings settings;
    // Configured timings, including values longer than the defaults.
    settings.us[0] = 7; settings.us[3] = 9;
    settings.us[1] = 47; settings.us[2] = 53; settings.us[6] = 101;
    settings.us[4] = 81; settings.us[5] = 93; settings.us[7] = 115;
    Output output; output.ted = ted;
    for (unsigned byte = 0; byte < 256; ++byte) fastloaderSendByte(output, byte, ted, boot, settings);
    assert(output.samples.size() == 1024);
    for (unsigned byte = 0; byte < 256; ++byte) {
      unsigned decoded = 0;
      for (unsigned pair = 0; pair < 4; ++pair) {
        auto sample = output.samples[byte*4+pair];
        if (boot) decoded = (decoded << 2) | (sample.write << 1) | sample.sense;
        else {
          const unsigned shift = pair < 2 ? 4 + pair*2 : (pair-2)*2;
          decoded |= (unsigned(sample.write) | (unsigned(sample.sense) << 1)) << shift;
        }
        if (byte || pair) {
          const auto previous = output.samples[byte*4+pair-1];
          const unsigned hold = settings.us[boot ? 6 : ted ? 2 : 1];
          const unsigned gap = pair ? settings.us[3] : settings.us[boot ? 7 : ted ? 5 : 4];
          assert(sample.time - previous.time == hold + gap + settings.us[0]);
        }
      }
      assert(decoded == byte);
    }
  }
}
