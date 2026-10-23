#include "tape_source.h"

#include <string.h>

namespace {
uint8_t flashByte(const uint8_t* address) {
  const uint32_t word = *reinterpret_cast<const uint32_t*>(
      reinterpret_cast<uintptr_t>(address) & ~3U);
  return reinterpret_cast<const uint8_t*>(&word)[reinterpret_cast<uintptr_t>(address) & 3U];
}
}  // namespace

void TapeSource::useEmbedded(const uint8_t* data, uint32_t size) {
  kind_ = Kind::EMBEDDED;
  embedded_ = data;
  size_ = size;
  retainedFrom_ = 20;
  nextOffset_ = size;
  sessionId_ = 0;
}

void TapeSource::useStream(const uint8_t header[20], uint32_t size, uint32_t id) {
  kind_ = Kind::STREAMED;
  embedded_ = nullptr;
  memcpy(header_, header, sizeof(header_));
  size_ = size;
  retainedFrom_ = 20;
  nextOffset_ = 20;
  sessionId_ = id;
}

bool TapeSource::seek(uint32_t offset, uint32_t newId) {
  if (kind_ != Kind::STREAMED || offset < 20 || offset >= size_ || newId == 0) return false;
  retainedFrom_ = offset;
  nextOffset_ = offset;
  sessionId_ = newId;
  return true;
}

void TapeSource::eject() {
  kind_ = Kind::EMPTY;
  embedded_ = nullptr;
  size_ = 0;
  retainedFrom_ = 20;
  nextOffset_ = 20;
  sessionId_ = 0;
}

bool TapeSource::read(uint32_t offset, uint8_t& value) const {
  if (offset >= size_) return false;
  if (kind_ == Kind::EMBEDDED) {
    value = flashByte(embedded_ + offset);
    return true;
  }
  if (kind_ != Kind::STREAMED) return false;
  if (offset < sizeof(header_)) {
    value = header_[offset];
    return true;
  }
  if (offset < retainedFrom_ || offset >= nextOffset_) return false;
  value = buffer_[offset % BUFFER_SIZE];
  return true;
}

uint16_t TapeSource::bufferedBytes() const {
  return kind_ == Kind::STREAMED ? nextOffset_ - retainedFrom_ : 0;
}

uint16_t TapeSource::freeBytes() const {
  return kind_ == Kind::STREAMED ? BUFFER_SIZE - bufferedBytes() : 0;
}

bool TapeSource::append(uint32_t offset, const uint8_t* data, size_t length) {
  if (kind_ != Kind::STREAMED || offset != nextOffset_ ||
      length > freeBytes() || length > size_ - nextOffset_) return false;
  for (size_t i = 0; i < length; ++i) buffer_[(nextOffset_ + i) % BUFFER_SIZE] = data[i];
  nextOffset_ += length;
  return true;
}

void TapeSource::releaseBefore(uint32_t offset) {
  if (kind_ != Kind::STREAMED) return;
  if (offset > nextOffset_) offset = nextOffset_;
  if (offset > retainedFrom_) retainedFrom_ = offset;
}
