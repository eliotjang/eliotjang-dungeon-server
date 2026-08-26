#include "net/socket_util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <format>
#include <iostream>

namespace ejd::net {
UniqueFd CreateListenSocket(uint16_t port, int sndbuf_size) {
  int raw = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (raw == -1) {
    perror("socket");
    return UniqueFd();
  }

  auto fd = UniqueFd(raw);

  int opt = 1;
  if (setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
    perror("setsockopt(SO_REUSEADDR)");
    return UniqueFd();
  }

  // 커널 송신버퍼 수동 설정 (drain 테스트 : EPOLLOUT 등록 --> 드레인 --> 해제)
  if (sndbuf_size) {
    if (setsockopt(fd.get(), SOL_SOCKET, SO_SNDBUF, &sndbuf_size,
                   sizeof(sndbuf_size)) == -1) {
      perror("setsockopt(SO_SNDBUF)");
    } else {
      int checked_sndbuf_size = 0;
      socklen_t len = sizeof(checked_sndbuf_size);
      getsockopt(fd.get(), SOL_SOCKET, SO_SNDBUF, &checked_sndbuf_size, &len);
      std::cout << std::format(
          "수동 송신 버퍼 : 요청 {} 바이트, 실제 {} 바이트\n", sndbuf_size,
          checked_sndbuf_size);
    }
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
