#include "net/session.h"

#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>

#include "net/framing.h"
#include "net/ring_buffer.h"

namespace ejd::net {

Session::IoResult Session::OnReadable() {
  while (true) {
    char chunk[4096];
    ssize_t n = read(fd_.get(), chunk, sizeof(chunk));
    if (n == 0) return IoResult::kClose;
    if (n < 0) {
      if (errno == EAGAIN)
        break;
      else if (errno == EINTR)
        continue;
      else {
        perror("read");
        return IoResult::kClose;
      }
    }

    if (!recv_buffer_.Write(chunk, n)) {
      std::cerr << "수신 버퍼 초과\n";
      return IoResult::kClose;
    }
  }

  std::vector<char> packet{};
  while (true) {
    switch (ExtractPacket(recv_buffer_, packet)) {
      case ExtractResult::kNeedMore:
        return IoResult::kKeepAlive;

      case ExtractResult::kMalformed:
        perror("ExtractPacket Malformed");
        return IoResult::kClose;

      case ExtractResult::kPacket:
        if (!Send(packet.data(), packet.size())) {
          perror("Session::Send");
          return IoResult::kClose;
        }
        break;
    }
  }
}

bool Session::Send(const char* data, size_t len) {
  // 1) 큐 적재
  if (!send_buffer_.Write(data, len)) return false;

  // 2) flush
  char chunk[4096];
  while (send_buffer_.size()) {
    size_t n = std::min(send_buffer_.size(), sizeof(chunk));
    send_buffer_.Peek(chunk, n);
    ssize_t w = write(fd_.get(), chunk, n);
    if (w == -1) {
      if (errno == EINTR)
        continue;
      else if (errno == EAGAIN) {
        break;
      } else
        return false;
    }
    // 3) 큐 소비
    send_buffer_.Consume(w);
  }

  return true;
}

Session::IoResult Session::OnWritable() {
  char chunk[4096];
  while (send_buffer_.size()) {
    size_t n = std::min(send_buffer_.size(), sizeof(chunk));
    send_buffer_.Peek(chunk, n);
    ssize_t w = write(fd_.get(), chunk, n);
    if (w == -1) {
      if (errno == EINTR)
        continue;
      else if (errno == EAGAIN)
        return IoResult::kKeepAlive;
      else
        return IoResult::kClose;
    }
    send_buffer_.Consume(w);
  }

  return IoResult::kKeepAlive;
}

}  // namespace ejd::net
