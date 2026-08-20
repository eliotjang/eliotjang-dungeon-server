#pragma once

#include <cstddef>
#include <vector>

namespace ejd::net {

class RingBuffer {
 public:
  explicit RingBuffer(size_t capacity)
      : capacity_(capacity), storage_(capacity) {}

  size_t size() const { return size_; }
  size_t free_space() const { return capacity_ - size_; }
  size_t tail() const { return (head_ + size_) % capacity_; }

  bool Write(const char* data, size_t len);
  bool Peek(char* out, size_t len) const;
  void Consume(size_t len);

 private:
  size_t capacity_;
  size_t size_ = 0;
  size_t head_ = 0;
  std::vector<char> storage_;
};

}  // namespace ejd::net
