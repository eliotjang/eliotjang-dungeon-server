#pragma once

#include <sys/epoll.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

#include "core/shard_worker.h"
#include "net/session.h"
#include "net/unique_fd.h"

namespace ejd::net {

class EpollReactor {
 public:
  explicit EpollReactor(UniqueFd listen_fd, core::ShardWorker& shard_worker)
      : listen_fd_(std::move(listen_fd)), shard_worker_(shard_worker) {}
  bool Init();
  void Run();

 private:
  struct SessionEntry {
    uint64_t session_id;
    std::unique_ptr<Session> session;
    uint32_t registered_events;
  };

  void AcceptAll();
  void HandleSessionEvent(int fd, uint32_t events);
  void CloseSession(int fd);
  [[nodiscard]] bool UpdateInterest(int fd, SessionEntry& se);

  UniqueFd epoll_fd_;
  UniqueFd listen_fd_;
  std::unordered_map<int, SessionEntry> sessions_;
  core::ShardWorker& shard_worker_;
  uint64_t next_session_id_{};
};

}  // namespace ejd::net
