#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "net/unique_fd.h"
#include "proto/messages.h"
#include "proto/packet_header.h"

namespace {

using ejd::net::UniqueFd;
using ejd::proto::kHeaderSize;
using ejd::proto::kMaxPacketLength;
using ejd::proto::PacketHeader;

constexpr uint16_t kPort = 5555;
constexpr size_t kPayloadLen = kMaxPacketLength - kHeaderSize;

// ----- 공용 구조체/enum -----
enum class SendResult { kOk, kPeerClosed, kError };

// ----- 공용 헬퍼 -----

UniqueFd ConnectTo(uint16_t port, int rcvbuf = 0, int rcv_timeout_ms = 0) {
  // 봇 클라는 블로킹 소켓으로 전송 시, EAGAIN 반환 없음 (SOCK_NONBLOCK X)
  int raw = socket(AF_INET, SOCK_STREAM, 0);
  if (raw == -1) {
    perror("socket");
    return UniqueFd();
  }

  auto fd = UniqueFd(raw);

  int opt = 1;
  if (setsockopt(fd.get(), IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
    perror("setsocket(TCP_NODELAY)");
    return UniqueFd();
  }

  if (rcvbuf > 0 && setsockopt(fd.get(), SOL_SOCKET, SO_RCVBUF, &rcvbuf,
                               sizeof(rcvbuf)) == -1) {
    perror("setsockopt(SO_RCVBUF)");
    return UniqueFd();
  }

  if (rcv_timeout_ms > 0) {
    timeval tv{};
    tv.tv_sec = rcv_timeout_ms / 1000;
    tv.tv_usec = (rcv_timeout_ms % 1000) * 1000;

    if (setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
      perror("setsockopt(SO_RCVTIMEO)");
      return UniqueFd();
    }
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);

  constexpr int kMaxAttempts = 20;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    if (connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr),
                sizeof(addr)) == 0) {
      return fd;  // 연결 성공
    }

    if (errno != ECONNREFUSED) break;
    usleep(100'000);  // 100ms
  }

  perror("connect");
  return UniqueFd{};
}

std::vector<char> MakePacket(uint16_t msg_id, std::span<const char> payload) {
  PacketHeader h{};
  h.length = static_cast<uint32_t>(kHeaderSize + payload.size());
  h.msg_id = msg_id;

  std::vector<char> result(h.length);
  std::memcpy(result.data(), &h, kHeaderSize);
  std::memcpy(result.data() + kHeaderSize, payload.data(), payload.size());

  return result;
}

SendResult SendAll(int fd, std::span<const char> data, int flags = 0) {
  size_t sent = 0;
  while (sent < data.size()) {
    ssize_t w = send(fd, data.data() + sent, data.size() - sent, flags);
    // INFO: 봇 클라는 블로킹 소켓으로 EAGAIN 반환 없고, 그 자리에서 sleep
    if (w == -1) {
      if (errno == EINTR) continue;
      else if (errno == EPIPE) {
        return SendResult::kPeerClosed;
      }
      else if (errno == ECONNRESET) {
        return SendResult::kPeerClosed;
      } else {
        perror("send");
        return SendResult::kError;
      }
    }

    sent += static_cast<size_t>(w);
  }

  return SendResult::kOk;
}

bool ReadExact(int fd, char* buf, size_t len) {
  size_t received = 0;
  while (received < len) {
    ssize_t r = read(fd, buf + received, len - received);
    if (r <= 0) {
      perror("read");
      return false;
    }

    received += static_cast<size_t>(r);
  }

  return true;
}

bool ReadEcho(int fd, uint16_t expected_msg_id,
              std::span<const char> expected_payload) {
  PacketHeader h{};
  if (!ReadExact(fd, reinterpret_cast<char*>(&h), kHeaderSize)) return false;

  if (h.length < kHeaderSize || h.length > kMaxPacketLength) {
    std::cerr << std::format("invalid packet length: {}\n", h.length);
    return false;
  }
  if (h.msg_id != expected_msg_id) {
    std::cerr << std::format("msg_id mismatch: 기대 {}, 실제 {}\n",
                             expected_msg_id, h.msg_id);
    return false;
  }

  const size_t payload_len = h.length - kHeaderSize;
  std::vector<char> buf(payload_len);
  if (!ReadExact(fd, buf.data(), payload_len)) return false;

  return payload_len == expected_payload.size() &&
         std::memcmp(buf.data(), expected_payload.data(), payload_len) == 0;
}

// SO_RCVTIMEO 설정 소켓 전용
bool WaitPeerClose(int fd) {
  char chunk[4096];
  while (true) {
    ssize_t r = read(fd, chunk, sizeof(chunk));
    if (r == 0) return true;               // FIN 패킷 수신
    if (r > 0) continue;
    if (errno == EINTR) continue;
    if (errno == ECONNRESET) return true;  // RST 패킷 수신
    if (errno == EAGAIN) return false;     // 타임아웃

    perror("read");
    return false;
  }
}

// ----- echo 모드 : 커넥션 count개, 각각 왕복 검증 -----

int Echo(int count) {
  std::vector<UniqueFd> conns;
  for (int i = 0; i < count; ++i) {
    auto fd = ConnectTo(kPort, 0, 500);
    if (fd.valid()) conns.push_back(std::move(fd));
  }

  int ok = 0;
  for (size_t i = 0; i < conns.size(); ++i) {
    const auto& conn = conns[i];
    auto msg = std::format("Hello Client: {}, fd: {}", i, conn.get());
    auto payload = std::span(msg.data(), msg.size());
    auto packet = MakePacket(static_cast<uint16_t>(ejd::proto::MsgId::kEcho), payload);

    if (SendAll(conn.get(), packet) != SendResult::kOk) continue;
    if (ReadEcho(conn.get(), static_cast<uint16_t>(ejd::proto::MsgId::kEcho), payload)) {
      ++ok;
    }
  }

  std::cout << std::format("echo ok: {}/{}\n", ok, conns.size());
  return ok == static_cast<int>(conns.size()) ? 0 : 1;
}

// ----- drain 모드: EPOLLOUT 등록 --> 드레인 --> 해제 사이클 -----

int Drain() {
  constexpr int packet_count = 4;

  auto fd = ConnectTo(kPort, static_cast<int>(kMaxPacketLength), 1000);
  if (!fd.valid()) return 1;

  // 1) 읽지 않고 최대 크기 패킷 packet_count개 전량 송신
  std::vector<std::vector<char>> payloads;
  size_t total = 0;
  for (int i = 0; i < packet_count; ++i) {
    std::vector<char> payload(kPayloadLen);
    for (size_t j = 0; j < kPayloadLen; ++j) {
      payload[j] = static_cast<char>('A' + (i + j) % 26);
    }
    auto packet = MakePacket(static_cast<uint16_t>(ejd::proto::MsgId::kEcho), payload);
    if (SendAll(fd.get(), packet) != SendResult::kOk) return 1;

    total += packet.size();
    payloads.push_back(std::move(payload));
  }

  std::cout << std::format("drain: {} 바이트 전송\n", total);

  // breakpoint) 서버 로그에 EPOLLOUT 등록 확인
  // $> ss -tn 명령으로 송수신 커널 버퍼 확인 가능 (Send-Q, Recv-Q)

  // 2) 드레인
  int ok = 0;
  for (int i = 0; i < packet_count; ++i) {
    if (ReadEcho(fd.get(), static_cast<uint16_t>(ejd::proto::MsgId::kEcho), payloads[i])) {
      ++ok;
    }
  }

  std::cout << std::format("drain ok: {}/{}\n", ok, packet_count);
  return ok == packet_count ? 0 : 1;
}

// ----- bomb 모드 : 커넥션 count개, 백프레셔 상한 검사 및 강제 종료 -----

int Bomb(int count) {
  std::vector<UniqueFd> conns;
  for (int i = 0; i < count; ++i) {
    auto fd = ConnectTo(kPort, static_cast<int>(kMaxPacketLength), 500);
    if (fd.valid()) conns.push_back(std::move(fd));
  }

  int ok = 0;
  for (size_t i = 0; i < conns.size(); ++i) {
    const auto& conn = conns[i];
    std::vector<char> payload(kPayloadLen);
    for (size_t j = 0; j < kPayloadLen; ++j) {
      payload[j] = static_cast<char>('A' + (i + j) % 26);
    }
    auto packet = MakePacket(static_cast<uint16_t>(ejd::proto::MsgId::kEcho), payload);
    constexpr size_t kMaxSendLength = 300'000;
    size_t total = 0;
    SendResult result = SendResult::kOk;
    // 300KB 연속 송신
    while (total <= kMaxSendLength) {
      result = SendAll(conn.get(), packet, MSG_NOSIGNAL);
      if (result != SendResult::kOk) break;

      total += packet.size();
    }

    if (result == SendResult::kError) {
      std::cerr << std::format("session {}: 송신 에러\n", i);
      continue;
    }

    if (result == SendResult::kPeerClosed || WaitPeerClose(conn.get()))
      ++ok;
    else
      std::cerr << std::format("session {}: 컷 감지 실패\n", i);
  }

  std::cout << std::format("bomb ok: {}/{}\n", ok, conns.size());
  return ok == static_cast<int>(conns.size()) ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
  //  ./ejd_bot [mode] [count]
  //  count = 커넥션 수
  std::string_view mode = (argc > 1) ? argv[1] : "echo";
  int count = (argc >= 3) ? std::stoi(argv[2]) : 10;

  if (mode == "echo") return Echo(count);
  if (mode == "drain") return Drain();
  if (mode == "bomb") return Bomb(count);

  std::cerr << "usage: ejd_bot [mode] [count]\n";
  return 1;
}
