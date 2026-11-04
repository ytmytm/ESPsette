// Replay physical tape-port events through the firmware's decoder.
// The ESP timestamps both direct WRITE edge polarities while MOTOR is high.
#include "write_decoder.h"
#include <cstdio>
#include <cstdint>

int main() {
  unsigned long hz = 0;
  if (std::scanf("hz,%lu\n", &hz) != 1 || !hz) return 2;
  WriteDecoder decoder;
  bool motor = false, write = true, haveEdge[2] = {};
  uint32_t lastUs[2] = {};
  uint32_t motorOffUs = 0;
  bool motorStopped = false;
  unsigned long long ticks;
  char signal;
  int high;
  while (std::scanf("%llu,%c,%d\n", &ticks, &signal, &high) == 3) {
    if (signal == 'M') {
      if (bool(high) != motor) {
        motor = high;
        const uint32_t us = uint64_t(ticks) * 1000000 / hz;
        if (motor) decoder.motorStart(motorStopped ? us - motorOffUs : UINT32_MAX);
        else {
          decoder.motorStop();
          motorOffUs = us;
          motorStopped = true;
        }
        haveEdge[0] = haveEdge[1] = false;
      }
    } else if (signal == 'W') {
      const bool edge = bool(high) != write;
      write = high;
      if (!motor || !edge) continue;
      const uint32_t us = uint64_t(ticks) * 1000000 / hz;
      if (haveEdge[high]) {
        const uint32_t duration = us - lastUs[high];
        decoder.feedEdgeMicros(duration > 0x7fff ? 0x7fff : duration, high);
      }
      lastUs[high] = us;
      haveEdge[high] = true;
    }
  }
  std::printf("{\"name\":\"%s\",\"command\":\"%s\",\"machine\":\"%s\","
              "\"status\":\"%s\",\"headers\":%u,\"commands\":%u,\"errors\":%u,"
              "\"start\":%u,\"end\":%u,\"type\":%u}\n",
              decoder.name(), decoder.command(), decoder.machineName(), decoder.status(),
              decoder.receivedCount(), decoder.commandCount(), decoder.errorCount(),
              decoder.startAddress(), decoder.endAddress(), decoder.fileType());
  return 0;
}
