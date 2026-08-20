#pragma once

#include <vector>

#include "net/ring_buffer.h"

namespace ejd::net {

enum class ExtractResult { kNeedMore, kPacket, kMalformed };

ExtractResult ExtractPacket(RingBuffer& ring, std::vector<char>& out);

}  // namespace ejd::net
