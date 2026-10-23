#pragma once

#include <Arduino.h>

#include "tape_source.h"

// Standard C64 PAL cassette blocks. The encoder emits pulse lengths directly;
// it never materializes a TAP file. Data for both ROM copies is read from the
// browser-backed logical stream (load address, copy 1, copy 2).
class PrgEncoder {
 public:
  enum class Result : uint8_t { PULSE, END, MISSING_DATA };

  bool begin(uint16_t loadAddress, uint32_t dataLength, const String& name);
  void observeFirstCopy(uint32_t offset, const uint8_t* bytes, size_t length);
  Result pulseAt(uint32_t index, const TapeSource& source,
                 uint32_t& cycles, uint32_t& sourceOffset) const;
  uint32_t dataLength() const { return dataLength_; }
  uint32_t totalPulses() const { return totalPulses_; }
  uint32_t dataStartPulse() const { return dataStartPulse_; }
  uint64_t totalCycles() const { return totalCycles_; }

 private:
  static constexpr uint16_t HEADER_BYTES = 192;
  static constexpr uint16_t BYTE_PULSES = 20;
  uint8_t header_[HEADER_BYTES] = {};
  uint8_t headerChecksum_ = 0;
  uint8_t dataChecksum_ = 0;
  uint32_t dataLength_ = 0;
  uint32_t dataStartPulse_ = 0;
  uint32_t totalPulses_ = 0;
  uint64_t totalCycles_ = 0;

  static uint32_t blockPulses(uint32_t pilotPulses, uint32_t bodyBytes);
  static uint32_t bytePulse(uint8_t value, uint8_t position);
  Result blockPulse(uint32_t index, uint32_t pilotPulses, bool firstCopy,
                    bool headerBlock, uint8_t dataCopy,
                    const TapeSource& source, uint32_t& cycles,
                    uint32_t& sourceOffset) const;
};
