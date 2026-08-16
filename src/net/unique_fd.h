#pragma once

#include <unistd.h>

namespace ejd::net {

class UniqueFd {
 public:
  UniqueFd() = default;
  explicit UniqueFd(int fd) : fd_(fd) {}
  ~UniqueFd() { reset(); }

  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;

  UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  UniqueFd& operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }

 private:
  void reset() {
    if (valid()) {
      close(fd_);
      fd_ = -1;
    }
  }

  int fd_ = -1;
};

}  // namespace ejd::net
