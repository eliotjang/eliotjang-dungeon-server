#include "net/framing.h"

#include <cassert>
#include <vector>

#include "net/ring_buffer.h"
#include "proto/packet_header.h"

namespace ejd::net {

ExtractResult ExtractPacket(RingBuffer& ring, std::vector<char>& out) {
  if (ring.size() < proto::kHeaderSize) return ExtractResult::kNeedMore;

  proto::PacketHeader h{};
  (void)ring.Peek(reinterpret_cast<char*>(&h), proto::kHeaderSize);

  if (h.length < proto::kHeaderSize) return ExtractResult::kMalformed;
  if (h.length > proto::kMaxPacketLength) return ExtractResult::kMalformed;

  if (ring.size() < h.length) return ExtractResult::kNeedMore;

  out.resize(h.length);
  (void)ring.Peek(out.data(), h.length);
  ring.Consume(h.length);

  return ExtractResult::kPacket;
}

}  // namespace ejd::net
