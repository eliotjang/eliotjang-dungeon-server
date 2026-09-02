#include "net/epoll_reactor.h"

#include <sys/epoll.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstdio>
#include <format>
#include <iostream>
#include <memory>

#include "core/session_packet.h"
#include "net/session.h"
#include "net/unique_fd.h"

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
      if (errno == EINTR)
        continue;
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
      if (errno == EAGAIN)
        return;
      else if (errno == EINTR)
        continue;
      else if (errno == EMFILE || errno == ENFILE) {
        perror("accept4 fd 한도 도달");
        return;
      } else {
        perror("accept4");
        return;
      }
    }

    auto session = std::make_unique<Session>(UniqueFd(raw));

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = raw;

    if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, raw, &ev) == -1) {
      perror("epoll_ctl ADD EPOLLIN");
      continue;  // 세션 정리
    }

    sessions_[raw] = SessionEntry{
        .session_id = next_session_id_++,
        .session = std::move(session),
        .registered_events = EPOLLIN,
    };

    std::cout << "connected fd=" << raw << "\n";
  }
}

void EpollReactor::HandleSessionEvent(int fd, uint32_t events) {
  auto it = sessions_.find(fd);
  if (it == sessions_.end()) return;

  if (events & (EPOLLERR | EPOLLHUP)) {
    CloseSession(fd);
    return;
  }

  if (events & EPOLLOUT) {
    if (it->second.session->OnWritable() == Session::IoResult::kClose) {
      CloseSession(fd);
      return;
    }
  }

  if (events & EPOLLIN) {
    std::vector<std::vector<char>> out{};
    auto result = it->second.session->OnReadable(out);

    for (size_t i = 0; i < out.size(); ++i) {
      auto session_packet =
          core::SessionPacket{it->second.session_id, std::move(out[i])};
      shard_worker_.Push(std::move(session_packet));
    }

    if (result == Session::IoResult::kClose) {
      CloseSession(fd);
      return;
    }
  }

  if (!UpdateInterest(fd, it->second)) {
    CloseSession(fd);
    return;
  }
}

void EpollReactor::CloseSession(int fd) {
  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr) == -1)
    perror("epoll_ctl DEL");

  sessions_.erase(fd);
  std::cout << "closed fd=" << fd << "\n";
}

bool EpollReactor::UpdateInterest(int fd, SessionEntry& se) {
  uint32_t want = EPOLLIN | (se.session->WantsWrite() ? EPOLLOUT : 0u);
  if (se.registered_events == want) return true;

  epoll_event ev{};
  ev.events = want;
  ev.data.fd = fd;
  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_MOD, fd, &ev) == -1) {
    perror("epoll_ctl MOD");
    return false;
  }

  se.registered_events = want;

  std::cout << std::format(
      "{} fd={}\n", (want & EPOLLOUT) ? "EPOLLOUT 등록" : "EPOLLOUT 해제", fd);

  return true;
}

}  // namespace ejd::net
