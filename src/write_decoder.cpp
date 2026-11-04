#include "write_decoder.h"
#include <string.h>

WriteDecoder::Pulse WriteDecoder::classify(uint16_t duration) const {
  // C16/Plus4 ROM whole waves are roughly 478/956/1913 us. WAV-PRG uses
  // 505/1010/2021 us. The 2:4:8 spacing separates them from C64 pulses.
  if (machine_ == Machine::C16) {
    if (duration >= 380 && duration <= 680) return Pulse::SHORT;
    if (duration >= 800 && duration <= 1250) return Pulse::MEDIUM;
    if (duration >= 1600 && duration <= 2400) return Pulse::LONG;
    return Pulse::INVALID;
  }
  // C64 ROM SAVE is approximately 352/512/672 us; generated test tapes
  // commonly use 390/544/690 us. Boundaries leave room for clock variation.
  if (duration >= 260 && duration <= 455) return Pulse::SHORT;
  if (duration > 455 && duration <= 615) return Pulse::MEDIUM;
  if (duration > 615 && duration <= 850) return Pulse::LONG;
  return Pulse::INVALID;
}

void WriteDecoder::resetFrame() {
  stage_ = Stage::SEARCH;
  expectedSync_ = 0;
  headerIndex_ = 0;
  checksum_ = 0;
  byteBits_ = false;
  ambiguousEnd_ = false;
  firstPulse_ = Pulse::INVALID;
}

void WriteDecoder::motorStart(uint32_t quietMicros) {
  // SAVE pulses MOTOR off between the header and its data record (about
  // 32 ms on C64, a fraction of a millisecond on TED). Keep discarding that
  // record across the pause. A second of MOTOR off recovers an aborted SAVE.
  if (stage_ == Stage::DISCARD && quietMicros < 1000000) return;
  suspended_ = false;
  resetFrame();
  discardBytes_ = 0;
  machine_ = Machine::UNKNOWN;
  c64Pilot_ = tedPilot_ = 0;
  status_ = "listening";
}

void WriteDecoder::motorStop() {
  if (stage_ == Stage::DISCARD || stage_ == Stage::DONE) return;
  if (stage_ != Stage::SEARCH) status_ = "incomplete";
  resetFrame();
}

void WriteDecoder::suspend() {
  resetFrame();
  suspended_ = true;
  status_ = "suspended_fastloader";
}

void WriteDecoder::fail(const char* reason) {
  if (stage_ == Stage::DISCARD) {
    // Program contents are neither retained nor validated as command data.
    byteBits_ = ambiguousEnd_ = false;
    firstPulse_ = Pulse::INVALID;
    return;
  }
  ++errorCount_;
  status_ = reason;
  resetFrame();
}

void WriteDecoder::overflow() {
  if (suspended_ || stage_ == Stage::DISCARD || stage_ == Stage::DONE) return;
  ++overflowCount_;
  fail("capture_overflow");
}

void WriteDecoder::finishHeader() {
  if ((header_[0] != 1 && header_[0] != 3) ||
      endAddress() <= startAddress()) {
    fail("invalid_header");
    return;
  }
  for (uint8_t i = 0; i < 16; ++i) {
    const uint8_t value = header_[5 + i];
    name_[i] = value >= 32 && value <= 126 ? char(value) : '?';
  }
  name_[16] = '\0';
  for (int8_t i = 15; i >= 0 && name_[i] == ' '; --i) name_[i] = '\0';
  ++receivedCount_;
  if (strncmp(name_, "ESP:", 4) == 0 && name_[4]) {
    memcpy(command_, name_ + 4, sizeof(command_));
    ++commandCount_;
    status_ = "command_received_no_action";
  } else {
    status_ = "header_ignored";
  }
  // Count (but never store) both data copies, including their countdowns and
  // checksum bytes. MOTOR is not a reliable header/data record boundary.
  discardBytes_ = 2UL * (9UL + endAddress() - startAddress() + 1UL);
  stage_ = Stage::DISCARD;
}

void WriteDecoder::onByte(uint8_t value) {
  switch (stage_) {
    case Stage::SEARCH:
      if (value == 137) {
        stage_ = Stage::FIRST_SYNC;
        expectedSync_ = 136;
        status_ = "receiving_header";
      }
      break;
    case Stage::FIRST_SYNC:
      if (value != expectedSync_) {
        resetFrame();
        if (value == 137) onByte(value);
      } else if (expectedSync_-- == 129) {
        stage_ = Stage::FIRST_HEADER;
        headerIndex_ = 0;
        checksum_ = 0;
      }
      break;
    case Stage::FIRST_HEADER:
      header_[headerIndex_++] = value;
      checksum_ ^= value;
      if (headerIndex_ == HEADER_BYTES) stage_ = Stage::FIRST_CHECKSUM;
      break;
    case Stage::FIRST_CHECKSUM:
      if (value != checksum_) fail("checksum_error");
      else stage_ = Stage::FIRST_END;
      break;
    case Stage::SECOND_SYNC:
      if (expectedSync_ == 0) {
        if (value == 9) expectedSync_ = 8;
      } else if (value != expectedSync_) {
        fail("sync_error");
      } else if (expectedSync_-- == 1) {
        stage_ = Stage::SECOND_HEADER;
        headerIndex_ = 0;
        checksum_ = 0;
      }
      break;
    case Stage::SECOND_HEADER:
      if (value != header_[headerIndex_]) { fail("copy_mismatch"); break; }
      checksum_ ^= value;
      if (++headerIndex_ == HEADER_BYTES) stage_ = Stage::SECOND_CHECKSUM;
      break;
    case Stage::SECOND_CHECKSUM:
      if (value != checksum_) fail("checksum_error");
      // Real C64 SAVE drops MOTOR before the last rising edge of the final
      // terminator. Both complete copies, parity and checksums already agree;
      // publish now so capture can stop strictly at MOTOR low.
      else finishHeader();
      break;
    case Stage::FIRST_END:
      // Plus/4 recordings may go straight from the first block checksum to
      // the repeat pilot and 9..1 sync, without a separate terminator pulse.
      if (machine_ == Machine::C16 && value == 9) {
        stage_ = Stage::SECOND_SYNC;
        expectedSync_ = 8;
        break;
      }
      fail("missing_terminator");
      break;
    case Stage::DISCARD:
      if (--discardBytes_ == 0) stage_ = Stage::DONE;
      break;
    case Stage::DONE:
      break;
  }
}

void WriteDecoder::onTerminator() {
  if (stage_ == Stage::FIRST_END) {
    stage_ = Stage::SECOND_SYNC;
    expectedSync_ = 0;
  } else if (stage_ != Stage::SEARCH && stage_ != Stage::DISCARD && stage_ != Stage::DONE) {
    fail("unexpected_terminator");
  }
}

void WriteDecoder::feedEdgeMicros(uint16_t duration, bool high) {
  if (suspended_ || stage_ == Stage::DONE) return;
  if (machine_ == Machine::UNKNOWN) {
    // Distinguish the long ROM pilot, not a single data pulse: TED's rising
    // edges straddle adjacent halfwaves and can look like C64 byte markers.
    // These non-overlapping windows cover captured PAL and NTSC ROM pilots.
    uint8_t& count = high ? c64Pilot_ : tedPilot_;
    const bool shortPilot = high ? duration >= 260 && duration <= 440
                                 : duration >= 450 && duration <= 680;
    count = shortPilot ? count + 1 : 0;
    if (count < 32) return;
    machine_ = high ? Machine::C64 : Machine::C16;
  }
  // The physical WRITE wire is not inverted. VICE SAVE traces verify that
  // C64 complete symbols end on rising edges, TED symbols on falling edges.
  if (high == (machine_ == Machine::C64)) feedMicros(duration);
}

void WriteDecoder::feedMicros(uint16_t duration) {
  if (suspended_ || stage_ == Stage::DONE) return;
  if (machine_ == Machine::UNKNOWN && stage_ == Stage::SEARCH &&
      !byteBits_ && firstPulse_ == Pulse::INVALID) {
    if (duration > 615 && duration <= 850) machine_ = Machine::C64;
    else if (duration >= 1600 && duration <= 2400) machine_ = Machine::C16;
    else return; // Full-period unit-test input can omit the physical pilot.
  }
  const Pulse pulse = classify(duration);
  if (ambiguousEnd_) {
    ambiguousEnd_ = false;
    if (pulse == Pulse::MEDIUM) {
      // Sync byte 9 starts with a one bit: its first pulse is medium.
      byteBits_ = true;
      bitIndex_ = byte_ = 0;
      parity_ = 1;
      firstPulse_ = pulse;
      return;
    }
    onTerminator();
    if (pulse == Pulse::INVALID || pulse == Pulse::SHORT) return;
  }
  if (pulse == Pulse::INVALID) {
    if (byteBits_) { fail("pulse_error"); return; }
    firstPulse_ = Pulse::INVALID;
    return; // Inter-block gaps may be much longer than a data pulse.
  }
  if (!byteBits_) {
    if (firstPulse_ == Pulse::LONG) {
      firstPulse_ = Pulse::INVALID;
      if (pulse == Pulse::MEDIUM) {
        if (machine_ == Machine::C16 &&
            stage_ == Stage::FIRST_END)
          ambiguousEnd_ = true;
        else {
          byteBits_ = true;
          bitIndex_ = byte_ = 0;
          parity_ = 1;
        }
      } else if (pulse == Pulse::SHORT) onTerminator();
      else if (pulse == Pulse::LONG) firstPulse_ = Pulse::LONG;
    } else {
      firstPulse_ = pulse == Pulse::LONG ? pulse : Pulse::INVALID;
    }
    return;
  }
  if (firstPulse_ == Pulse::INVALID) {
    if (pulse != Pulse::SHORT && pulse != Pulse::MEDIUM) {
      fail("pulse_error");
      if (pulse == Pulse::LONG) firstPulse_ = Pulse::LONG;
      return;
    }
    firstPulse_ = pulse;
    return;
  }
  const bool zero = firstPulse_ == Pulse::SHORT && pulse == Pulse::MEDIUM;
  const bool one = firstPulse_ == Pulse::MEDIUM && pulse == Pulse::SHORT;
  firstPulse_ = Pulse::INVALID;
  if (!zero && !one) { fail("pulse_error"); return; }
  if (bitIndex_ < 8) {
    if (one) byte_ |= uint8_t(1U << bitIndex_);
    parity_ ^= one;
  } else if (one != bool(parity_)) {
    fail("parity_error");
    return;
  }
  if (++bitIndex_ == 9) {
    byteBits_ = false;
    onByte(byte_);
  }
}
