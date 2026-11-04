#pragma once

#include <Arduino.h>

#include "tape_source.h"
#include "computer_profile.h"

// Standard C64 or C16/Plus4 PAL/NTSC cassette blocks. The encoder emits pulse lengths directly;
// it never materializes a TAP file. Data for both ROM copies is read from the
// browser-backed logical stream (load address, copy 1, copy 2).
class PrgEncoder {
 public:
  enum class Machine : uint8_t { C64, C16 };
  enum class Result : uint8_t { PULSE, END, MISSING_DATA };

  bool begin(uint16_t loadAddress, uint32_t dataLength, const String& name,
             Machine machine, bool ntsc = false);
  // Absolute two-byte load at $0302; code lives in the header's spare tail.
  // Vector bytes are generated internally; observeFirstCopy receives that
  // vector once for its checksum. The PRG ring remains untouched.
  bool beginAutostart(const uint8_t* code, size_t length, const String& name,
                      Machine machine, bool ntsc = false);
  void observeFirstCopy(uint32_t offset, const uint8_t* bytes, size_t length);
  Result pulseAt(uint32_t index, const TapeSource& source,
                 uint32_t& cycles, uint32_t& sourceOffset) const;
  uint32_t dataLength() const { return dataLength_; }
  uint32_t totalPulses() const { return totalPulses_; }
  uint32_t dataStartPulse() const { return dataStartPulse_; }
  uint64_t totalCycles() const { return totalCycles_; }
  uint32_t clockHz() const { return profileClockHz(computerProfile(machine_ == Machine::C16, ntsc_)); }
  Machine machine() const { return machine_; }

 private:
  static constexpr uint16_t HEADER_BYTES = 192;
  static constexpr uint16_t BYTE_PULSES = 20;
  uint8_t header_[HEADER_BYTES] = {};
  uint8_t headerChecksum_ = 0;
  uint8_t dataChecksum_ = 0;
  bool autostart_ = false;
  uint8_t autostartEntry_[2] = {};
  uint32_t dataLength_ = 0;
  uint32_t dataStartPulse_ = 0;
  uint32_t totalPulses_ = 0;
  uint64_t totalCycles_ = 0;
  Machine machine_ = Machine::C64;
  bool ntsc_ = false;

  static uint32_t blockPulses(uint32_t pilotPulses, uint32_t bodyBytes);
  uint32_t bytePulse(uint8_t value, uint8_t position) const;
  uint32_t shortCycles() const { return machine_ == Machine::C16 ? 448 : 384; }
  uint32_t mediumCycles() const { return machine_ == Machine::C16 ? 896 : 536; }
  uint32_t longCycles() const { return machine_ == Machine::C16 ? 1792 : 680; }
  Result blockPulse(uint32_t index, uint32_t pilotPulses, bool firstCopy,
                    bool headerBlock, uint8_t dataCopy,
                    const TapeSource& source, uint32_t& cycles,
                    uint32_t& sourceOffset) const;
};
