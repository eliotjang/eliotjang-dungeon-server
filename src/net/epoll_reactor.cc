#include "net/epoll_reactor.h"

#include <sys/epoll.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstdio>
#include <memory>
#include <iostream>

#include "net/unique_fd.h"
#include "net/session.h"

namespace ejd::net {

namespace {
constexpr int kMaxEvents = 64;  // 관례값. epoll_wait가 매번 꽉 채우면 조정 신호
}  // namespace

bool EpollReactor::Init() {
  epoll_fd_ = UniqueFd(epoll_create1(0));
  if (!epoll_fd_.valid()) {
    perror("epoll_create");
    return false;
  }

  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = listen_fd_.get();
  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, listen_fd_.get(), &ev) == -1) {
    perror("epoll_ctl");
    return false;
  }

  return true;
}

void EpollReactor::Run() {
  epoll_event events[kMaxEvents];

  while (true) {
    int n = epoll_wait(epoll_fd_.get(), events, kMaxEvents, -1);
    if (n == -1) {
      if (errno == EINTR) continue;
      else {
        perror("epoll_wait");
        return;
      }
    }

    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;
      if (fd == listen_fd_.get())
        AcceptAll();
      else
        HandleSessionEvent(fd, events[i].events);
    }
  }
}

void EpollReactor::AcceptAll() {
  while (true) {
    int raw = accept4(listen_fd_.get(), nullptr, nullptr, SOCK_NONBLOCK);
    if (raw == -1) {
      if (errno == EAGAIN) return;
      else if (errno == EINTR) continue;
      else if (errno == EMFILE || errno == ENFILE) {
        perror("fd 한도 도달");
        return;
      }
      else {
        perror("accept4");
        return;
      }
    }

    auto session = std::make_unique<Session>(UniqueFd(raw));
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = raw;

    if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, raw, &ev) == -1) {
      perror("epoll_ctl");
      continue; // 세션 정리
    }

    sessions_[raw] = std::move(session);
    std::cout << "connected fd=" << raw << "\n";
  }
}

void EpollReactor::HandleSessionEvent(int fd, uint32_t events) {
  auto it = sessions_.find(fd);
  if (it == sessions_.end())
    return;

  if (events & (EPOLLERR | EPOLLHUP)) {
    CloseSession(fd);
    return;
  }

  if (events & EPOLLIN) {
    if (it->second->OnReadable() == Session::IoResult::kClose)
      CloseSession(fd);
  }
}

void EpollReactor::CloseSession(int fd) {
  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr) == -1)
    perror("close epoll_ctl");

  sessions_.erase(fd);
  std::cout << "closed fd=" << fd << "\n";
}

}  // namespace ejd::net
