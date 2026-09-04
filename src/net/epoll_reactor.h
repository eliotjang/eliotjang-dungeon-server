#pragma once

#include <sys/epoll.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

#include "core/mpsc_queue.h"
#include "core/session_packet.h"
#include "core/shard_worker.h"
#include "net/session.h"
#include "net/unique_fd.h"

namespace ejd::net {

class EpollReactor {
 public:
  explicit EpollReactor(UniqueFd listen_fd, int event_fd, int signal_fd,
                        core::MpscQueue<core::SessionPacket>& outbound,
                        core::ShardWorker& shard_worker)
      : listen_fd_(std::move(listen_fd)),
        event_fd_(event_fd),
        signal_fd_(signal_fd),
        outbound_(outbound),
        shard_worker_(shard_worker) {}

  bool Init();
  void Run();

 private:
  struct SessionEntry {
    uint64_t session_id;
    std::unique_ptr<Session> session;
    uint32_t registered_events;
  };

  void AcceptAll();
  void HandleWakeup(uint32_t events);
  void HandleSignal();
  void HandleSessionEvent(int fd, uint32_t events);
  void CloseSession(uint64_t session_id, int fd);
  [[nodiscard]] bool UpdateInterest(int fd, SessionEntry& se);

  UniqueFd epoll_fd_;
  UniqueFd listen_fd_;
  int event_fd_;
  int signal_fd_;
  bool running_{};
  std::unordered_map<int, SessionEntry> sessions_;
  std::unordered_map<uint64_t, int> id_to_fd_;
  core::MpscQueue<core::SessionPacket>& outbound_;
  core::ShardWorker& shard_worker_;
  uint64_t next_session_id_{};
};

}  // namespace ejd::net
