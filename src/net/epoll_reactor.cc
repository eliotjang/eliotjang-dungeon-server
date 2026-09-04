#include "net/epoll_reactor.h"

#include <sys/epoll.h>
#include <sys/signalfd.h>
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
    perror("epoll_ctl ADD EPOLLIN listen_fd_");
    return false;
  }

  ev = epoll_event{};
  ev.events = EPOLLIN;
  ev.data.fd = event_fd_;
  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, event_fd_, &ev) == -1) {
    perror("epoll_ctl ADD EPOLLIN event_fd_");
    return false;
  }

  ev = epoll_event{};
  ev.events = EPOLLIN;
  ev.data.fd = signal_fd_;
  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, signal_fd_, &ev) == -1) {
    perror("epoll_ctl ADD EPOLLIN signal_fd_");
    return false;
  }

  running_ = true;

  return true;
}

void EpollReactor::Run() {
  epoll_event events[kMaxEvents];

  while (running_) {
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
      else if (fd == event_fd_)
        HandleWakeup(events[i].events);
      else if (fd == signal_fd_)
        HandleSignal();
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

    auto session_id = next_session_id_++;

    auto [session_entry_it, sessions_inserted] =
        sessions_.try_emplace(raw, session_id, std::move(session), EPOLLIN);
    assert(sessions_inserted && "fd already tracked");

    auto [fd_it, fd_inserted] = id_to_fd_.try_emplace(session_id, raw);
    assert(fd_inserted && "duplicate session_id");

    std::cout << "connected fd=" << raw << "\n";
  }
}

void EpollReactor::HandleWakeup(uint32_t events) {
  if (events & (EPOLLIN)) {
    uint64_t cnt{};
    // read 이후 드레인으로 유실 방지
    ssize_t n = read(event_fd_, &cnt, sizeof(cnt));
    if (n == -1) {
      if (errno != EAGAIN) {  // EAGAIN은 cnt가 0인 정상 분기
        perror("read");
        return;
      }
    }

    std::vector<core::SessionPacket> drained{};
    outbound_.TryDrain(drained);

    for (const auto& session_packet : drained) {
      auto fd_it = id_to_fd_.find(session_packet.session_id);
      // 강제종료된 클라의 남은 패킷 폐기
      if (fd_it == id_to_fd_.end()) continue;

      auto session_entry_it = sessions_.find(fd_it->second);
      if (session_entry_it == sessions_.end()) {
        std::cerr << "not find SessionEntry. fd=" << fd_it->second << "\n";
        continue;
      }

      if (!session_entry_it->second.session->Send(
              session_packet.packet.data(), session_packet.packet.size())) {
        CloseSession(session_entry_it->second.session_id, fd_it->second);
        continue;
      }

      // 송신버퍼 잔량 EPOLLOUT 처리
      if (!UpdateInterest(fd_it->second, session_entry_it->second)) {
        CloseSession(session_entry_it->second.session_id, fd_it->second);
        continue;
      }
    }
  }
}

void EpollReactor::HandleSignal() {
  signalfd_siginfo info{};
  ssize_t n = read(signal_fd_, &info, sizeof(info));
  if (n <= 0) {
    perror("read");
    return;
  }

  std::cout << std::format("signal {} 수신, 종료 시작\n", info.ssi_signo);

  running_ = false;
}

void EpollReactor::HandleSessionEvent(int fd, uint32_t events) {
  auto it = sessions_.find(fd);
  if (it == sessions_.end()) return;

  if (events & (EPOLLERR | EPOLLHUP)) {
    CloseSession(it->second.session_id, fd);
    return;
  }

  if (events & EPOLLOUT) {
    if (it->second.session->OnWritable() == Session::IoResult::kClose) {
      CloseSession(it->second.session_id, fd);
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
      CloseSession(it->second.session_id, fd);
      return;
    }
  }

  if (!UpdateInterest(fd, it->second)) {
    CloseSession(it->second.session_id, fd);
    return;
  }
}

void EpollReactor::CloseSession(uint64_t session_id, int fd) {
  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr) == -1)
    perror("epoll_ctl DEL");

  id_to_fd_.erase(session_id);
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
