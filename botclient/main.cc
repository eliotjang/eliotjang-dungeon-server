#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "net/unique_fd.h"

int main(int argc, char* argv[]) {
  int count = (argc >= 2) ? std::stoi(argv[1]) : 10;
  auto conns = std::vector<ejd::net::UniqueFd>();

  while (count--) {
    int raw = socket(AF_INET, SOCK_STREAM, 0);
    if (raw == -1) {
      perror("socket");
      continue;
    }

    auto fd = ejd::net::UniqueFd(raw);

    int opt = 1;
    if (setsockopt(fd.get(), IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) ==
        -1) {
      perror("setsockopt");
      continue;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(5555);

    if (connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr),
                sizeof(addr)) == -1) {
      perror("connect");
      continue;
    }

    conns.push_back(std::move(fd));
  }

  int ok = 0;
  for (size_t i = 0; i < conns.size(); ++i) {
    const auto& conn = conns[i];

    char send_buf[4096];
    auto msg = std::format("Hello Client: {}, fd: {}", i, conn.get());
    size_t n = msg.copy(send_buf, msg.size(), 0);
    size_t sent = 0;

    while (sent < n) {
      ssize_t w = write(conn.get(), send_buf + sent, n - sent);
      if (w <= 0) {
        perror("write");
        break;
      }

      sent += w;
    }

    char recv_buf[4096];
    size_t received = 0;

    while (received < n) {
      ssize_t r = read(conn.get(), recv_buf + received, n - received);
      if (r <= 0) {
        perror("read");
        break;
      }

      received += r;
    }

    if (received == msg.size() && std::string_view(recv_buf, received) == msg) {
      ok++;
    }
  }

  std::cout << std::format("ok: {}, conns.size(): {}\n", ok, conns.size());

  return 0;
}
