#include "prg_encoder.h"

#include <string.h>

namespace {
constexpr uint32_t FIRST_HEADER_PILOT = 20000;
constexpr uint32_t FIRST_DATA_PILOT = 5000;
constexpr uint32_t SECOND_PILOT = 79;
constexpr uint32_t HEADER_DATA_GAP = 200000;
}  // namespace

uint32_t PrgEncoder::blockPulses(uint32_t pilotPulses, uint32_t bodyBytes) {
  return pilotPulses + (9 + bodyBytes + 1) * BYTE_PULSES + 2;
}

bool PrgEncoder::begin(uint16_t loadAddress, uint32_t dataLength,
                       const String& name, Machine machine, bool ntsc) {
  if (!dataLength || (uint32_t)loadAddress + dataLength > 65535) return false;
  machine_ = machine;
  ntsc_ = ntsc;
  dataLength_ = dataLength;
  dataChecksum_ = 0;
  memset(header_, 0x20, sizeof(header_));
  header_[0] = 1; // Standard relocatable ROM tape header.
  header_[1] = loadAddress & 0xff;
  header_[2] = loadAddress >> 8;
  const uint16_t endAddress = loadAddress + dataLength;
  header_[3] = endAddress & 0xff;
  header_[4] = endAddress >> 8;
  uint8_t nameLength = 0;
  for (size_t i = 0; i < name.length() && nameLength < 16; ++i) {
    char c = name[i];
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ') {
      header_[5 + nameLength++] = (uint8_t)c;
    }
  }
  if (!nameLength) memcpy(header_ + 5, "PROGRAM", 7);
  headerChecksum_ = 0;
  for (uint8_t value : header_) headerChecksum_ ^= value;

  const uint32_t header1 = blockPulses(FIRST_HEADER_PILOT, HEADER_BYTES);
  const uint32_t header2 = blockPulses(SECOND_PILOT, HEADER_BYTES);
  const uint32_t data1 = blockPulses(FIRST_DATA_PILOT, dataLength_);
  const uint32_t data2 = blockPulses(SECOND_PILOT, dataLength_);
  dataStartPulse_ = header1 + header2 + 1; // The gap is one long pulse.
  totalPulses_ = dataStartPulse_ + data1 + data2;
  const uint32_t byteCycles = longCycles() + mediumCycles() +
                              9 * (shortCycles() + mediumCycles());
  totalCycles_ = (uint64_t)(FIRST_HEADER_PILOT + SECOND_PILOT +
                            FIRST_DATA_PILOT + SECOND_PILOT) * shortCycles() +
                 (uint64_t)(2 * (9 + HEADER_BYTES + 1) +
                            2 * (9 + dataLength_ + 1)) * byteCycles +
                 4 * (longCycles() + (machine_ == Machine::C16 ?
                                     mediumCycles() : shortCycles())) + HEADER_DATA_GAP;
  return true;
}

void PrgEncoder::observeFirstCopy(uint32_t offset, const uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    const uint32_t logicalOffset = offset + i;
    if (logicalOffset >= 2 && logicalOffset < 2 + dataLength_) dataChecksum_ ^= bytes[i];
  }
}

uint32_t PrgEncoder::bytePulse(uint8_t value, uint8_t position) const {
  if (position == 0) return longCycles();
  if (position == 1) return mediumCycles();
  uint8_t bit;
  if (position < 18) bit = (value >> ((position - 2) / 2)) & 1;
  else {
    bit = 1;
    for (uint8_t i = 0; i < 8; ++i) bit ^= (value >> i) & 1;
  }
  const bool firstHalf = (position & 1) == 0;
  return firstHalf ? (bit ? mediumCycles() : shortCycles())
                   : (bit ? shortCycles() : mediumCycles());
}

PrgEncoder::Result PrgEncoder::blockPulse(uint32_t index, uint32_t pilotPulses,
                                          bool firstCopy, bool headerBlock,
                                          uint8_t dataCopy, const TapeSource& source,
                                          uint32_t& cycles,
                                          uint32_t& sourceOffset) const {
  const uint32_t bodyBytes = headerBlock ? HEADER_BYTES : dataLength_;
  const uint32_t dataBase = 2 + (uint32_t)dataCopy * dataLength_;
  sourceOffset = headerBlock ? 2 : dataBase;
  if (index < pilotPulses) { cycles = shortCycles(); return Result::PULSE; }
  index -= pilotPulses;
  if (index < 9 * BYTE_PULSES) {
    const uint8_t sequence = firstCopy ? 137 - index / BYTE_PULSES
                                        : 9 - index / BYTE_PULSES;
    cycles = bytePulse(sequence, index % BYTE_PULSES);
    return Result::PULSE;
  }
  index -= 9 * BYTE_PULSES;
  if (index < (bodyBytes + 1) * BYTE_PULSES) {
    const uint32_t byteIndex = index / BYTE_PULSES;
    uint8_t value;
    if (byteIndex == bodyBytes) {
      value = headerBlock ? headerChecksum_ : dataChecksum_;
      sourceOffset = headerBlock ? 2 : dataBase + bodyBytes;
    } else if (headerBlock) {
      value = header_[byteIndex];
    } else {
      sourceOffset = dataBase + byteIndex;
      if (!source.read(sourceOffset, value)) return Result::MISSING_DATA;
    }
    cycles = bytePulse(value, index % BYTE_PULSES);
    return Result::PULSE;
  }
  index -= (bodyBytes + 1) * BYTE_PULSES;
  sourceOffset = headerBlock ? 2 : dataBase + bodyBytes;
  cycles = index == 0 ? longCycles() :
           (machine_ == Machine::C16 ? mediumCycles() : shortCycles());
  return Result::PULSE;
}

PrgEncoder::Result PrgEncoder::pulseAt(uint32_t index, const TapeSource& source,
                                       uint32_t& cycles, uint32_t& sourceOffset) const {
  if (index >= totalPulses_) return Result::END;
  const uint32_t header1 = blockPulses(FIRST_HEADER_PILOT, HEADER_BYTES);
  if (index < header1)
    return blockPulse(index, FIRST_HEADER_PILOT, true, true, 0,
                      source, cycles, sourceOffset);
  index -= header1;
  const uint32_t header2 = blockPulses(SECOND_PILOT, HEADER_BYTES);
  if (index < header2)
    return blockPulse(index, SECOND_PILOT, false, true, 0,
                      source, cycles, sourceOffset);
  index -= header2;
  if (index == 0) {
    cycles = HEADER_DATA_GAP;
    sourceOffset = 2;
    return Result::PULSE;
  }
  --index;
  const uint32_t data1 = blockPulses(FIRST_DATA_PILOT, dataLength_);
  if (index < data1)
    return blockPulse(index, FIRST_DATA_PILOT, true, false, 0,
                      source, cycles, sourceOffset);
  index -= data1;
  return blockPulse(index, SECOND_PILOT, false, false, 1,
                    source, cycles, sourceOffset);
}
