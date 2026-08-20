#pragma once

#include <cstdint>

namespace ejd::proto {

#pragma pack(push, 1)
struct PacketHeader {
  uint32_t length;
  uint16_t msg_id;
  uint16_t flags;
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 8);

inline constexpr uint32_t kHeaderSize = sizeof(PacketHeader);
inline constexpr uint32_t kMaxPacketLength = 4096;  // 실제 최대 메시지(수백KB) 여유 및 악성 클라 방지

}  // namespace ejd::proto
