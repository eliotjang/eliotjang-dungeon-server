#include <sys/socket.h>

#include <cerrno>
#include <csignal>
#include <iostream>
#include <string>
#include <utility>

#include "common/version.h"
#include "net/epoll_reactor.h"
#include "net/socket_util.h"
#include "net/unique_fd.h"

int main(int argc, char* argv[]) {
  int sndbuf_size = (argc > 1) ? std::stoul(argv[1]) : 0;

  std::cout << "Version: " << ejd::common::Version() << "\n";

  signal(SIGPIPE, SIG_IGN);
  auto listen_fd = ejd::net::CreateListenSocket(5555, sndbuf_size);
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
