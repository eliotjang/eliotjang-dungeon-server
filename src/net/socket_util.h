#pragma once

#include <cstdint>

#include "net/unique_fd.h"

namespace ejd::net {

UniqueFd CreateListenSocket(uint16_t port, int sndbuf_size);

}  // namespace ejd::net
