#include "net/ring_buffer.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>

namespace ejd::net {

bool RingBuffer::Write(const char* data, size_t len) {
  if (free_space() < len) return false;

  size_t first = std::min(len, capacity_ - tail());
  std::memcpy(storage_.data() + tail(), data, first);
  std::memcpy(storage_.data(), data + first, len - first);

  size_ += len;

  return true;
}

bool RingBuffer::Peek(char* out, size_t len) const {
  if (size_ < len) return false;

  size_t first = std::min(len, capacity_ - head_);
  std::memcpy(out, storage_.data() + head_, first);
  std::memcpy(out + first, storage_.data(), len - first);

  return true;
}

void RingBuffer::Consume(size_t len) {
  assert(len <= size_);

  head_ = (head_ + len) % capacity_;
  size_ -= len;
}

}  // namespace ejd::net
