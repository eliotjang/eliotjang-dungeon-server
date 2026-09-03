#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <csignal>
#include <format>
#include <iostream>
#include <string_view>
#include <thread>
#include <utility>

#include "common/version.h"
#include "core/dispatcher.h"
#include "core/mpsc_queue.h"
#include "core/session_packet.h"
#include "core/shard_worker.h"
#include "net/epoll_reactor.h"
#include "net/socket_util.h"
#include "net/unique_fd.h"

int main(int argc, char* argv[]) {
  int sndbuf_size = 0;
  int rcvbuf_size = 0;
  if (argc > 1) {
    std::string_view sv(argv[1]);
    auto [_, ec] =
        std::from_chars(sv.data(), sv.data() + sv.size(), sndbuf_size);

    if (ec != std::errc{}) {
      std::cerr << std::format("커널 송신버퍼 크기 입력 에러: {}\n", sv);
      return 1;
    }
  }

  if (argc > 2) {
    std::string_view sv(argv[2]);
    auto [_, ec] =
        std::from_chars(sv.data(), sv.data() + sv.size(), rcvbuf_size);

    if (ec != std::errc{}) {
      std::cerr << std::format("커널 수신버퍼 크기 입력 에러: {}\n", sv);
      return 1;
    }
  }

  std::cout << "Version: " << ejd::common::Version() << "\n";

  signal(SIGPIPE, SIG_IGN);
  auto listen_fd = ejd::net::CreateListenSocket(5555, sndbuf_size, rcvbuf_size);
  if (!listen_fd.valid()) {
    std::cerr << "failed to create listen socket" << "\n";
    return 1;
  }

  auto dispatcher = ejd::core::Dispatcher();
  dispatcher.Register(
      ejd::proto::MsgId::kEcho,
      [](const ejd::core::SessionPacket& in,
         std::vector<ejd::core::SessionPacket>& out) { out.push_back(in); });

  auto outbound = ejd::core::MpscQueue<ejd::core::SessionPacket>();
  auto event_fd = ejd::net::UniqueFd(eventfd(0, EFD_NONBLOCK));
  if (!event_fd.valid()) {
    perror("eventfd");
    return 1;
  }

  auto shard_worker = ejd::core::ShardWorker(
      dispatcher, [&outbound, efd = event_fd.get()](
                      std::vector<ejd::core::SessionPacket> out) {
        for (auto& session_packet : out)
          outbound.Push(std::move(session_packet));
        uint64_t one = 1;
        ssize_t n = write(efd, &one, sizeof(one));
        if (n == -1) {
          perror("write");
        }
      });

  auto reactor = ejd::net::EpollReactor(std::move(listen_fd), event_fd.get(),
                                        outbound, shard_worker);

  if (!reactor.Init()) {
    std::cerr << "failed to init epoll socket" << "\n";
    return 1;
  }

  std::cout << "listening on port 5555" << "\n";

  reactor.Run();

  return 0;
}
