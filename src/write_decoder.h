#pragma once

#include <stddef.h>
#include <stdint.h>

// Diagnostic C64/C16 ROM SAVE header decoder. No command is executed.
// feedMicros() accepts time between successive WRITE rising edges (full pulses).
class WriteDecoder {
 public:
  enum class Machine : uint8_t { UNKNOWN, C64, C16 };
  void motorStart();
  void motorStop();
  void feedMicros(uint16_t duration);
  void overflow();
  const char* status() const { return status_; }
  const char* name() const { return name_; }
  const char* machineName() const {
    return machine_ == Machine::C64 ? "c64" : machine_ == Machine::C16 ? "c16" : "unknown";
  }
  uint8_t fileType() const { return header_[0]; }
  uint16_t startAddress() const { return header_[1] | (uint16_t(header_[2]) << 8); }
  uint16_t endAddress() const { return header_[3] | (uint16_t(header_[4]) << 8); }
  uint32_t receivedCount() const { return receivedCount_; }
  uint32_t errorCount() const { return errorCount_; }
  uint32_t overflowCount() const { return overflowCount_; }

 private:
  enum class Pulse : uint8_t { INVALID, SHORT, MEDIUM, LONG };
  enum class Stage : uint8_t {
    SEARCH, FIRST_SYNC, FIRST_HEADER, FIRST_CHECKSUM, FIRST_END,
    SECOND_SYNC, SECOND_HEADER, SECOND_CHECKSUM, SECOND_END, DONE
  };
  static constexpr uint16_t HEADER_BYTES = 192;
  Pulse firstPulse_ = Pulse::INVALID;
  bool byteBits_ = false;
  uint8_t bitIndex_ = 0;
  uint8_t byte_ = 0;
  uint8_t parity_ = 1;
  Stage stage_ = Stage::SEARCH;
  Machine machine_ = Machine::UNKNOWN;
  uint8_t expectedSync_ = 0;
  uint16_t headerIndex_ = 0;
  uint8_t checksum_ = 0;
  uint8_t header_[HEADER_BYTES] = {};
  char name_[17] = {};
  const char* status_ = "idle";
  uint32_t receivedCount_ = 0;
  uint32_t errorCount_ = 0;
  uint32_t overflowCount_ = 0;

  Pulse classify(uint16_t duration) const;
  void resetFrame();
  void fail(const char* reason);
  void onByte(uint8_t value);
  void onTerminator();
  void finishHeader();
};
