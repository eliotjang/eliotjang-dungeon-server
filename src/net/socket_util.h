#pragma once

#include <cstdint>

#include "net/unique_fd.h"

namespace ejd::net {

UniqueFd CreateListenSocket(uint16_t port);

}  // namespace ejd::net
