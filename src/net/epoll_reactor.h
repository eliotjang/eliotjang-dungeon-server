#pragma once

#include <sys/epoll.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

#include "net/session.h"
#include "net/unique_fd.h"

namespace ejd::net {

class EpollReactor {
 public:
  explicit EpollReactor(UniqueFd listen_fd)
      : listen_fd_(std::move(listen_fd)) {}
  bool Init();
  void Run();

 private:
  struct SessionEntry {
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
};

}  // namespace ejd::net
