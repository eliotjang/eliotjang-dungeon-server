#include "core/mpsc_queue.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/session_packet.h"
#include "proto/packet_header.h"

namespace ejd::core {

namespace {
constexpr size_t kPayloadLen = proto::kMaxPacketLength - proto::kHeaderSize;

std::vector<char> MakePacket(uint16_t msg_id, std::vector<char> payload) {
  proto::PacketHeader h{};
  h.length = proto::kHeaderSize + payload.size();
  h.msg_id = msg_id;

  std::vector<char> result{};
  result.resize(h.length);
  std::memcpy(result.data(), &h, proto::kHeaderSize);
  std::memcpy(result.data() + proto::kHeaderSize, payload.data(),
              payload.size());

  return result;
}
}  // namespace

TEST(MpscQueueTest, ReturnsSessionPacket) {
  constexpr int test_count = 3;

  std::vector<SessionPacket> session_packets;
  for (int i = 0; i < test_count; ++i) {
    std::vector<char> payload(kPayloadLen);
    for (size_t j = 0; j < kPayloadLen; ++j)
      payload[j] = static_cast<char>('A' + (i + j) % 26);

    auto packet = MakePacket(0, payload);
    session_packets.emplace_back(SessionPacket(i, std::move(packet)));
  }

  auto queue = MpscQueue<SessionPacket>();

  for (int i = 0; i < test_count; ++i) {
    queue.Push(session_packets[i]);
  }

  std::vector<SessionPacket> out{};
  ASSERT_EQ(test_count, queue.TryDrain(out));

  for (int i = 0; i < test_count; ++i) {
    EXPECT_EQ(session_packets[i].session_id, out[i].session_id);
    EXPECT_EQ(session_packets[i].packet, out[i].packet);
  }
}

TEST(MpscQueueTest, ReturnsEmptyDrain) {
  auto queue = MpscQueue<SessionPacket>();

  std::vector<SessionPacket> out{};
  EXPECT_EQ(0, queue.TryDrain(out));
}

TEST(MpscQueueTest, ReturnsEmptyAfterDrain) {
  constexpr int test_count = 1;

  std::vector<SessionPacket> session_packets;
  for (int i = 0; i < test_count; ++i) {
    std::vector<char> payload(kPayloadLen);
    for (size_t j = 0; j < kPayloadLen; ++j)
      payload[j] = static_cast<char>('A' + (i + j) % 26);

    auto packet = MakePacket(0, payload);
    session_packets.emplace_back(SessionPacket(i, std::move(packet)));
  }

  auto queue = MpscQueue<SessionPacket>();

  for (int i = 0; i < test_count; ++i) {
    queue.Push(session_packets[i]);
  }

  std::vector<SessionPacket> out{};
  ASSERT_EQ(test_count, queue.TryDrain(out));

  for (int i = 0; i < test_count; ++i) {
    EXPECT_EQ(session_packets[i].session_id, out[i].session_id);
    EXPECT_EQ(session_packets[i].packet, out[i].packet);
  }

  std::vector<SessionPacket> out2{};
  EXPECT_EQ(0, queue.TryDrain(out2));
  EXPECT_TRUE(out2.empty());
}

}  // namespace ejd::core
