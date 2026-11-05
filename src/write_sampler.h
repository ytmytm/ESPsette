#pragma once
#include "write_decoder.h"

// Experimental Timer1/NMI receiver. sample() has one writer: the NMI callback.
// Main-loop access uses a sequence-checked snapshot, not noInterrupts().
class WriteSampler {
 public:
  static constexpr uint32_t PERIOD_US = 40;
  static constexpr uint32_t MAX_GAP_US = 80;
  struct Statistics {
    uint32_t samples = 0;
    uint32_t maxGapUs = 0;
    uint32_t gaps = 0;
    uint32_t maxWorkCycles80 = 0;
  };
  void WRITE_IRAM begin();
  void WRITE_IRAM suspend();
  void WRITE_IRAM sample(uint32_t now, bool motor, bool write, bool allowed);
  void __attribute__((always_inline)) recordWork(uint32_t cycles80) {
    if (cycles80 > stats_.maxWorkCycles80) stats_.maxWorkCycles80 = cycles80;
  }
  const WriteDecoder& decoder() const { return decoder_; }
  const Statistics& stats() const { return stats_; }
  bool active() const { return motor_ && allowed_; }

 private:
  WriteDecoder decoder_;
  Statistics stats_;
  uint32_t lastSample_ = 0, lastEdge_[2] = {}, motorOff_ = 0;
  bool haveSample_ = false, haveEdge_[2] = {}, motorStopped_ = false;
  bool motor_ = false, write_ = true, allowed_ = false;
};
