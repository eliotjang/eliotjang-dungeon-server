#include "net/session.h"

#include <unistd.h>

#include <cerrno>
#include <iostream>

#include "net/framing.h"
#include "net/ring_buffer.h"

namespace ejd::net {

Session::IoResult Session::OnReadable() {
  while (true) {
    char buf[4096];
    ssize_t n = read(fd_.get(), buf, sizeof(buf));
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

    if (!recv_buffer_.Write(buf, n)) {
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

      case ExtractResult::kPacket: {
        // 에코. 송신 큐로 대체 예정
        size_t sent = 0;
        size_t n = packet.size();
        while (sent < n) {
          ssize_t w = write(fd_.get(), packet.data() + sent, n - sent);
          if (w == -1) {
            if (errno == EINTR)
              continue;
            else if (errno == EAGAIN) {
              std::cerr << "송신 버퍼 초과\n";
              return IoResult::kKeepAlive;
            } else
              return IoResult::kClose;
          }
          sent += w;
        }
      } break;
    }
  }
}

}  // namespace ejd::net
