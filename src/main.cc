#include <sys/socket.h>

#include <cerrno>
#include <csignal>
#include <iostream>
#include <utility>

#include "common/version.h"
#include "net/epoll_reactor.h"
#include "net/socket_util.h"
#include "net/unique_fd.h"

int main() {
  std::cout << ejd::common::Version() << "\n";

  signal(SIGPIPE, SIG_IGN);
  auto listen_fd = ejd::net::CreateListenSocket(5555);
  if (!listen_fd.valid()) {
    std::cerr << "failed to create listen socket" << "\n";
    return 1;
  }

  auto reactor = ejd::net::EpollReactor(std::move(listen_fd));

  if (!reactor.Init()) {
    std::cerr << "failed to init epoll socket" << "\n";
    return 1;
  }

  std::cout << "listening on port 5555" << "\n";

  reactor.Run();

  return 0;
}
