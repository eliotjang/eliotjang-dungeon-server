#include "net/session.h"

#include <unistd.h>

#include <cerrno>
#include <iostream>

namespace ejd::net {

Session::IoResult Session::OnReadable() {
  while (true) {
    char buf[4096];
    ssize_t n = read(fd_.get(), buf, sizeof(buf));
    if (n == 0) return IoResult::kClose;
    if (n == -1) {
      if (errno == EAGAIN)
        return IoResult::kKeepAlive;
      else if (errno == EINTR)
        continue;
      else {
        perror("read");
        return IoResult::kClose;
      }
    }

    // 에코. 송신큐로 대체 예정
    ssize_t sent = 0;
    while (sent < n) {
      ssize_t w = write(fd_.get(), buf + sent, n - sent);
      if (w == -1) {
        if (errno == EINTR)
          continue;
        else if (errno == EAGAIN) {
          std::cerr << "송신버퍼 가득차서 드랍\n";
          return IoResult::kKeepAlive;
        } else
          return IoResult::kClose;
      }

      sent += w;
    }
  }
}

}  // namespace ejd::net
