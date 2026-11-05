// Periodic sampling of the original physical VICE events. No GPIO-edge ISR
// timestamps or main-loop drains are used by this model.
#include "write_sampler.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static_assert(sizeof(WriteSampler) <= 384, "Sampler must have bounded small state");
struct Event { uint64_t tick; char signal; bool high; };

int main(int argc, char** argv) {
  uint32_t phase = 0, jitter = 0, gapDuration = 0;
  uint64_t gapAt = UINT64_MAX;
  bool allowed = true;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--suspended")) allowed = false;
    else if (i + 1 < argc) {
      const auto value = std::strtoull(argv[i + 1], nullptr, 0);
      if (!std::strcmp(argv[i], "--phase")) phase = value;
      else if (!std::strcmp(argv[i], "--jitter")) jitter = value;
      else if (!std::strcmp(argv[i], "--gap-at")) gapAt = value;
      else if (!std::strcmp(argv[i], "--gap-us")) gapDuration = value;
      else return 2;
      ++i;
    } else return 2;
  }
  unsigned long hz;
  if (std::scanf("hz,%lu\n", &hz) != 1 || !hz || jitter > 10) return 2;
  std::vector<Event> events;
  unsigned long long tick;
  char signal;
  int high;
  while (std::scanf("%llu,%c,%d\n", &tick, &signal, &high) == 3)
    events.push_back({tick, signal, bool(high)});
  if (events.empty()) return 2;
  WriteSampler sampler;
  sampler.begin();
  bool motor = false, write = true;
  size_t index = 0;
  const uint64_t end = events.back().tick * 1000000 / hz + 80;
  uint32_t random = 12345;
  for (uint64_t scheduled = phase; scheduled <= end; scheduled += WriteSampler::PERIOD_US) {
    if (scheduled >= gapAt && scheduled < gapAt + gapDuration) continue;
    random = random * 1664525U + 1013904223U;
    const uint64_t now = scheduled + (jitter ? random % (jitter + 1) : 0);
    const uint64_t sampleTick = now * hz / 1000000;
    while (index < events.size() && events[index].tick <= sampleTick) {
      const auto& event = events[index++];
      if (event.signal == 'M') motor = event.high;
      else if (event.signal == 'W') write = event.high;
    }
    sampler.sample(uint32_t(now), motor, write, allowed);
  }
  const auto& decoder = sampler.decoder();
  const auto& stats = sampler.stats();
  std::printf("{\"name\":\"%s\",\"command\":\"%s\",\"status\":\"%s\","
              "\"headers\":%u,\"commands\":%u,\"errors\":%u,\"start\":%u,\"end\":%u,"
              "\"samples\":%u,\"maxGapUs\":%u,\"gaps\":%u}\n",
              decoder.name(), decoder.command(), decoder.status(), decoder.receivedCount(),
              decoder.commandCount(), decoder.errorCount(), decoder.startAddress(), decoder.endAddress(),
              stats.samples, stats.maxGapUs, stats.gaps);
}
