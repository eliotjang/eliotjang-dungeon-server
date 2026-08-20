#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "net/framing.h"
#include "net/unique_fd.h"
#include "proto/packet_header.h"

std::vector<char> MakePacket(uint16_t msg_id, std::vector<char> payload) {
  ejd::proto::PacketHeader h{};
  h.length = ejd::proto::kHeaderSize + payload.size();
  h.msg_id = msg_id;

  std::vector<char> result{};
  result.resize(h.length);
  std::memcpy(result.data(), &h, ejd::proto::kHeaderSize);
  std::memcpy(result.data() + ejd::proto::kHeaderSize, payload.data(),
              payload.size());

  return result;
}

bool ReadExact(int fd, char* buf, size_t len) {
  size_t received = 0;
  while (received < len) {
    int r = read(fd, buf + received, len - received);
    if (r <= 0) {
      perror("read");
      return false;
    }

    received += r;
  }
  return true;
}

int main(int argc, char* argv[]) {
  int count = (argc >= 2) ? std::stoi(argv[1]) : 10;

  auto conns = std::vector<ejd::net::UniqueFd>();
  // 커넥션 연결
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

    // char send_buf[4096];
    auto msg = std::format("Hello Client: {}, fd: {}", i, conn.get());
    // size_t n = msg.copy(send_buf, msg.size(), 0);

    auto payload = std::vector<char>(msg.begin(), msg.end());
    auto packet = MakePacket(i, payload);

    size_t n = packet.size();
    size_t sent = 0;
    // 전량 송신
    while (sent < n) {
      ssize_t w = write(conn.get(), packet.data() + sent, n - sent);
      if (w <= 0) {
        perror("write");
        break;
      }

      sent += w;
    }

    char recv_buf[4096];
    ejd::proto::PacketHeader h{};
    // 전량 수신
    if (!ReadExact(conn.get(), reinterpret_cast<char*>(&h),
                   ejd::proto::kHeaderSize)) {
      perror("read packet header");
      continue;
    }

    if (h.length < ejd::proto::kHeaderSize ||
        h.length > ejd::proto::kMaxPacketLength) {
      std::cerr << "신뢰할 수 없는 데이터\n";
      continue;
    }

    auto payload_len = h.length - ejd::proto::kHeaderSize;
    if (!ReadExact(conn.get(), recv_buf, payload_len)) {
      std::cerr << "페이로드 오류\n";
      continue;
    }

    if (payload_len == msg.size() && std::string_view(recv_buf, payload_len) == msg) {
      ok++;
    }
  }

  std::cout << std::format("ok: {}, conns.size(): {}\n", ok, conns.size());

  return 0;
}
