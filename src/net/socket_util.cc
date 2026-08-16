#include "net/socket_util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>

namespace ejd::net {
UniqueFd CreateListenSocket(uint16_t port) {
  int raw = socket(AF_INET, SOCK_STREAM, 0);
  if (raw == -1) {
    perror("socket");
    return UniqueFd();
  }

  auto fd = UniqueFd(raw);

  int opt = 1;
  if (setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
    perror("setsockopt");
    return UniqueFd();

    
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) ==
      -1) {
    perror("bind");
    return UniqueFd();
  }

  if (listen(fd.get(), SOMAXCONN) == -1) {
    perror("listen");
    return UniqueFd();
  }

  return fd;
}
}  // namespace ejd::net
