#pragma once

#include <utility>

#include "net/unique_fd.h"

namespace ejd::net {

// 역할 : 본인 fd 읽고, 지속할지 끝낼지 판단
class Session {
 public:
  explicit Session(UniqueFd fd) : fd_(std::move(fd)) {}
  int fd() const { return fd_.get(); }
  enum class IoResult { kKeepAlive, kClose };
  IoResult OnReadable();

 private:
  UniqueFd fd_;
};

}  // namespace ejd::net
