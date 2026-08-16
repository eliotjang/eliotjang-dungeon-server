#include <sys/socket.h>

#include <cerrno>
#include <csignal>
#include <iostream>

#include "common/version.h"
#include "net/socket_util.h"
#include "net/unique_fd.h"

void HandleClient(ejd::net::UniqueFd client);

int main() {
  std::cout << ejd::common::Version() << "\n";

  signal(SIGPIPE, SIG_IGN);
  auto listen_fd = ejd::net::CreateListenSocket(5555);
  if (!listen_fd.valid()) {
    std::cerr << "failed to create listen socket" << "\n";
    return 1;
  }
  std::cout << "listening on port 5555" << "\n";

  while (true) {
    int raw = accept(listen_fd.get(), nullptr, nullptr);
    if (raw == -1) {
      if (errno == EINTR) {
        continue;
      }
      perror("accept");
      continue;
    }

    HandleClient(ejd::net::UniqueFd(raw));
  }

  return 0;
}

void HandleClient(ejd::net::UniqueFd client) {
  while (true) {
    char buf[4096];
    ssize_t n = read(client.get(), buf, sizeof(buf));
    if (n == 0) {
      std::cout << "client closed connection" << "\n";
      return;
    }
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("read");
      return;
    }

    ssize_t sent = 0;
    while (sent < n) {
      ssize_t w = write(client.get(), buf + sent, n - sent);
      if (w < 0) {
        if (errno == EINTR) {
          continue;
        }
        perror("write");
        return;
      }

      sent += w;
    }
  }
}
