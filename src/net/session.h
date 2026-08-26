#pragma once

#include <utility>

#include "net/ring_buffer.h"
#include "net/unique_fd.h"

namespace ejd::net {

// TCP 연결 하나의 수명(fd)과 송수신 버퍼 소유
class Session {
 public:
  explicit Session(UniqueFd fd)
      : fd_(std::move(fd)),
        recv_buffer_(kRecvBufferCapacity),
        send_buffer_(kSendBufferCapacity) {}

  int fd() const { return fd_.get(); }
  enum class IoResult { kKeepAlive, kClose };

  IoResult OnReadable();
  [[nodiscard]] bool Send(const char* data, size_t len);
  IoResult OnWritable();
  [[nodiscard]] bool WantsWrite() const { return send_buffer_.size(); }

 private:
  static constexpr size_t kRecvBufferCapacity = 64 * 1024;
  // 최대 크기 패킷 64개 분량
  static constexpr size_t kSendBufferCapacity = 256 * 1024;

  UniqueFd fd_;
  RingBuffer recv_buffer_;
  RingBuffer send_buffer_;
};

}  // namespace ejd::net
