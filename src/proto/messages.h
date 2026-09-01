#pragma once

#include <cstdint>

namespace ejd::proto {

enum class MsgId : uint16_t {
  kEcho = 1,
  kPing = 2,
  kPong = 3,
};

}  // namespace ejd::proto
