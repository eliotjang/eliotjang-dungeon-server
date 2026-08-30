#pragma once

#include <cstdint>
#include <vector>

namespace ejd::core {

struct SessionPacket {
  uint64_t session_id;
  std::vector<char> packet;
};

}  // namespace ejd::core
