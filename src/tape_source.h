#pragma once

#include <Arduino.h>

// The browser owns the file. Only a bounded, rewindable window of TAP bytes
// lives on the ESP. releaseBefore() is called only after DMA has played data.
class TapeSource {
 public:
  static constexpr uint16_t BUFFER_SIZE = 8192;
  enum class Kind : uint8_t { EMPTY, EMBEDDED, STREAMED };

  void useEmbedded(const uint8_t* data, uint32_t size);
  void useStream(const uint8_t header[20], uint32_t size, uint32_t id);
  void eject();
  bool read(uint32_t offset, uint8_t& value) const;
  bool append(uint32_t offset, const uint8_t* data, size_t length);
  void releaseBefore(uint32_t offset);
  uint32_t size() const { return size_; }
  uint32_t nextOffset() const { return nextOffset_; }
  uint32_t retainedFrom() const { return retainedFrom_; }
  uint32_t sessionId() const { return sessionId_; }
  uint16_t freeBytes() const;
  uint16_t bufferedBytes() const;
  Kind kind() const { return kind_; }

 private:
  Kind kind_ = Kind::EMPTY;
  const uint8_t* embedded_ = nullptr;
  uint8_t header_[20] = {};
  uint8_t buffer_[BUFFER_SIZE] = {};
  uint32_t size_ = 0;
  uint32_t retainedFrom_ = 20;
  uint32_t nextOffset_ = 20;
  uint32_t sessionId_ = 0;
};
