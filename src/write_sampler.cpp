#include "write_sampler.h"

void WriteSampler::begin() {
  haveSample_ = haveEdge_[0] = haveEdge_[1] = false;
  motor_ = allowed_ = false;
}

void WriteSampler::suspend() {
  decoder_.suspend("idle");
  motor_ = allowed_ = false;
  haveSample_ = haveEdge_[0] = haveEdge_[1] = false;
}

void WriteSampler::sample(uint32_t now, bool motor, bool write, bool allowed) {
  ++stats_.samples;
  const uint32_t gap = now - lastSample_;
  if (haveSample_ && gap > stats_.maxGapUs) stats_.maxGapUs = gap;
  if (haveSample_ && motor_ && allowed_ && motor && allowed && gap > MAX_GAP_US) {
    ++stats_.gaps;
    decoder_.captureGap();
    haveEdge_[0] = haveEdge_[1] = false;
    write_ = write; // Do not fabricate an edge after a sampling blackout.
  }
  lastSample_ = now;
  const bool first = !haveSample_;
  haveSample_ = true;
  if (!allowed) {
    if (allowed_) decoder_.suspend("idle");
    motor_ = allowed_ = false;
    haveEdge_[0] = haveEdge_[1] = false;
    write_ = write;
    return;
  }
  if (!allowed_) {
    decoder_.motorStart();
    allowed_ = true;
    motor_ = false;
    haveEdge_[0] = haveEdge_[1] = false;
  }
  if (motor != motor_) {
    motor_ = motor;
    if (motor) decoder_.motorStart(motorStopped_ ? uint32_t(now - motorOff_) : UINT32_MAX);
    else {
      decoder_.motorStop();
      motorOff_ = now;
      motorStopped_ = true;
    }
    haveEdge_[0] = haveEdge_[1] = false;
    write_ = write;
    return;
  }
  if (first || !motor || write == write_) { write_ = write; return; }
  write_ = write;
  const auto machine = decoder_.machine();
  if (machine != WriteDecoder::Machine::UNKNOWN &&
      write != (machine == WriteDecoder::Machine::C64)) return;
  if (haveEdge_[write]) {
    const uint32_t period = now - lastEdge_[write];
    decoder_.feedEdgeMicros(period > 0x7fff ? 0x7fff : period, write);
  }
  lastEdge_[write] = now;
  haveEdge_[write] = true;
}
