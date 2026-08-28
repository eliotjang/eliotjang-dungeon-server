#pragma once

#include <cstdint>

#include "net/unique_fd.h"

namespace ejd::net {

UniqueFd CreateListenSocket(uint16_t port, int sendbuf_size = 0, int recvbuf_size = 0);

}  // namespace ejd::net
