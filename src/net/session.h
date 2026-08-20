#pragma once

#include <utility>

#include "net/ring_buffer.h"
#include "net/unique_fd.h"

namespace ejd::net {

// 역할 : 본인 fd 읽고, 지속할지 끝낼지 판단
class Session {
 public:
  explicit Session(UniqueFd fd)
      : fd_(std::move(fd)), recv_buffer_(kBufferCapacity) {}

  int fd() const { return fd_.get(); }
  enum class IoResult { kKeepAlive, kClose };

  IoResult OnReadable();

 private:
  static constexpr size_t kBufferCapacity = 64 * 1024; // 세션 1만개 : 640MB 여유

  UniqueFd fd_;
  RingBuffer recv_buffer_;
};

}  // namespace ejd::net
