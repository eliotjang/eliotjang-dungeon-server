#pragma once

#include <cstdint>

#include "net/unique_fd.h"

namespace ejd::net {

// 실패 시 빈 UniqueFd 반환 + stderr 로그
UniqueFd CreateListenSocket(uint16_t port);

}  // namespace ejd::net
