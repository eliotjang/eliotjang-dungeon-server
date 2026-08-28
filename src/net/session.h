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
  // 비대칭(수신 1 : 송신 4): MMORPG는 다운링크 (브로드캐스트 편중)
  // 목표 동접 1만 세션 기준 3.2GB : 버퍼 예산 내 수용
  // 초과 시, 연결 종료
  static constexpr size_t kRecvBufferCapacity = 64 * 1024;
  static constexpr size_t kSendBufferCapacity = 256 * 1024;

  UniqueFd fd_;
  RingBuffer recv_buffer_;
  RingBuffer send_buffer_;
};

}  // namespace ejd::net
