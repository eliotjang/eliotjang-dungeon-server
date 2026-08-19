#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

#include "net/session.h"
#include "net/unique_fd.h"

namespace ejd::net {

class EpollReactor {
 public:
  // 이동 비용이 낮은 타입은 값 전달이 표준형 (int 복사 1, 쓰기 1)
  explicit EpollReactor(UniqueFd listen_fd) : listen_fd_(std::move(listen_fd)) {}
  bool Init();
  void Run();

 private:
  void AcceptAll();
  void HandleSessionEvent(int fd, uint32_t events);
  void CloseSession(int fd);

  UniqueFd epoll_fd_;
  UniqueFd listen_fd_;
  std::unordered_map<int, std::unique_ptr<Session>> sessions_;
};

}  // namespace ejd::net
