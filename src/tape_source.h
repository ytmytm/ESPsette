#pragma once

#include <Arduino.h>

// Browser files use a bounded window; the built-in PRG reads flash directly.
// releaseBefore() is called only after DMA has played data.
class TapeSource {
 public:
  static constexpr uint16_t BUFFER_SIZE = 12288;
  enum class Kind : uint8_t { EMPTY, EMBEDDED, STREAMED };

  void useEmbedded(const uint8_t* data, uint32_t size);
  // Standard PRG loading repeats only the payload, without allocating a copy.
  bool useEmbeddedPrg(const uint8_t* data, uint32_t size, bool repeat);
  void useStream(const uint8_t header[20], uint32_t size, uint32_t id);
  void usePrg(const uint8_t loadAddress[2], uint32_t logicalSize, uint32_t id);
  bool seek(uint32_t offset, uint32_t newId);
  void eject();
  bool read(uint32_t offset, uint8_t& value) const;
  bool append(uint32_t offset, const uint8_t* data, size_t length);
  void releaseBefore(uint32_t offset);
  uint32_t size() const { return size_; }
  uint32_t nextOffset() const { return nextOffset_; }
  uint32_t retainedFrom() const { return retainedFrom_; }
  uint32_t sessionId() const { return sessionId_; }
  uint8_t prefixSize() const { return prefixSize_; }
  uint16_t freeBytes() const;
  uint16_t bufferedBytes() const;
  // Also read directly by the WRITE ISR's ownership guard. Force a volatile
  // inline load so it never calls flash code or caches an earlier deck state.
  Kind __attribute__((always_inline)) kind() const volatile { return kind_; }

 private:
  Kind kind_ = Kind::EMPTY;
  const uint8_t* embedded_ = nullptr;
  uint32_t embeddedRepeat_ = 0;
  uint8_t header_[20] = {};
  uint8_t prefixSize_ = 20;
  uint8_t buffer_[BUFFER_SIZE] = {};
  uint32_t size_ = 0;
  uint32_t retainedFrom_ = 20;
  uint32_t nextOffset_ = 20;
  uint32_t sessionId_ = 0;
};
