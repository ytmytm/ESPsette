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

void WriteDecoder::motorStart() {
  suspended_ = false;
  resetFrame();
  machine_ = Machine::UNKNOWN;
  status_ = "listening";
}

void WriteDecoder::motorStop() {
  if (stage_ != Stage::SEARCH && stage_ != Stage::DONE) status_ = "incomplete";
  resetFrame();
}

void WriteDecoder::suspend() {
  resetFrame();
  suspended_ = true;
  status_ = "suspended_fastloader";
}

void WriteDecoder::fail(const char* reason) {
  ++errorCount_;
  status_ = reason;
  resetFrame();
}

void WriteDecoder::overflow() {
  if (suspended_ || stage_ == Stage::DONE) return; // Discarded payload must never reopen a completed frame.
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
  stage_ = Stage::DONE;
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
      else if (machine_ == Machine::C16) finishHeader();
      else stage_ = Stage::SECOND_END;
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
    case Stage::SECOND_END:
      fail("missing_terminator");
      break;
    case Stage::DONE:
      break;
  }
}

void WriteDecoder::onTerminator() {
  if (stage_ == Stage::FIRST_END) {
    stage_ = Stage::SECOND_SYNC;
    expectedSync_ = 0;
  } else if (stage_ == Stage::SECOND_END) {
    finishHeader();
  } else if (stage_ != Stage::SEARCH && stage_ != Stage::DONE) {
    fail("unexpected_terminator");
  }
}

void WriteDecoder::feedMicros(uint16_t duration) {
  if (suspended_ || stage_ == Stage::DONE) return; // Never accept program data or our fastloader output.
  if (stage_ == Stage::SEARCH && !byteBits_ && firstPulse_ == Pulse::INVALID) {
    if (duration > 615 && duration <= 850) machine_ = Machine::C64;
    else if (duration >= 1600 && duration <= 2400) machine_ = Machine::C16;
    else return; // Ignore the pilot until a byte marker is seen.
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
