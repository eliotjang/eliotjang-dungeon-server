#include "net/framing.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "net/ring_buffer.h"
#include "proto/packet_header.h"

namespace ejd::net {

class FramingTest : public ::testing::Test {
 protected:
  static constexpr std::string_view kPayloadText = "hello world";
  std::vector<char> payload_{kPayloadText.begin(), kPayloadText.end()};
};

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

TEST_F(FramingTest, ExtractsCompletePacket) {
  auto packet = MakePacket(0, payload_);

  auto ring = RingBuffer(64);
  ASSERT_TRUE(ring.Write(packet.data(), packet.size()));

  std::vector<char> out{};
  EXPECT_EQ(ExtractPacket(ring, out), ExtractResult::kPacket);
  EXPECT_EQ(std::memcmp(packet.data(), out.data(), packet.size()), 0);
}

TEST_F(FramingTest, ReturnsNeedMoreUntilPacketComplete) {
  auto packet = MakePacket(0, payload_);

  auto ring = RingBuffer(64);

  std::vector<char> out{};
  for (size_t i = 0; i < packet.size() - 1; ++i) {
    ASSERT_TRUE(ring.Write(&packet[i], 1));
    EXPECT_EQ(ExtractPacket(ring, out), ExtractResult::kNeedMore);
  }

  ASSERT_TRUE(ring.Write(&packet[packet.size() - 1], 1));
  EXPECT_EQ(ExtractPacket(ring, out), ExtractResult::kPacket);
}

TEST_F(FramingTest, ExtractsMultiplePacketsInOrder) {
  auto packet1 = MakePacket(0, payload_);
  auto packet2 = MakePacket(1, payload_);
  auto packet3 = MakePacket(2, payload_);

  std::vector<char> data{};
  data.reserve(packet1.size() + packet2.size() + packet3.size());
  data.insert(data.end(), packet1.begin(), packet1.end());
  data.insert(data.end(), packet2.begin(), packet2.end());
  data.insert(data.end(), packet3.begin(), packet3.end());

  auto ring = RingBuffer(64);

  std::vector<char> out{};
  ASSERT_TRUE(ring.Write(data.data(), data.size()));

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(ExtractPacket(ring, out), ExtractResult::kPacket);
    auto& header = *reinterpret_cast<proto::PacketHeader*>(out.data());
    switch (header.msg_id) {
      case 0:
        EXPECT_EQ(std::memcmp(packet1.data(), out.data(), packet1.size()), 0);
        break;
      case 1:
        EXPECT_EQ(std::memcmp(packet2.data(), out.data(), packet2.size()), 0);
        break;
      case 2:
        EXPECT_EQ(std::memcmp(packet3.data(), out.data(), packet3.size()), 0);
        break;
      default:
        break;
    }
  }

  EXPECT_EQ(ExtractPacket(ring, out), ExtractResult::kNeedMore);
}

TEST_F(FramingTest, RejectsLengthSmallerThanHeader) {
  auto packet = MakePacket(0, payload_);
  auto& header = *reinterpret_cast<proto::PacketHeader*>(packet.data());
  header.length = 0;

  auto ring = RingBuffer(64);
  ASSERT_TRUE(ring.Write(packet.data(), packet.size()));

  std::vector<char> out{};
  EXPECT_EQ(ExtractPacket(ring, out), ExtractResult::kMalformed);
}

TEST_F(FramingTest, RejectsLengthOverMaxPacketLength) {
  auto packet = MakePacket(0, payload_);
  auto& header = *reinterpret_cast<proto::PacketHeader*>(packet.data());
  header.length = proto::kMaxPacketLength + 1;

  auto ring = RingBuffer(64);
  ASSERT_TRUE(ring.Write(packet.data(), packet.size()));

  std::vector<char> out{};
  EXPECT_EQ(ExtractPacket(ring, out), ExtractResult::kMalformed);
}

TEST_F(FramingTest, ExtractPacketSpanningWrapBoundary) {
  auto packet = MakePacket(0, payload_);
  // header : 8B
  // payload : 11B
  // need 9B more (total 28B)

  std::vector<char> dummy{};
  dummy.resize(9);
  packet.insert(packet.end(), dummy.begin(), dummy.end());
  auto& header = *reinterpret_cast<proto::PacketHeader*>(packet.data());
  header.length += dummy.size();

  auto ring = RingBuffer(32);

  ASSERT_TRUE(ring.Write(packet.data(), packet.size()));
  ring.Consume(packet.size());

  // wrap 경계에서 패킷 주입 테스트
  ASSERT_TRUE(ring.Write(packet.data(), packet.size()));

  std::vector<char> out{};
  EXPECT_EQ(ExtractPacket(ring, out), ExtractResult::kPacket);
  EXPECT_EQ(std::memcmp(packet.data(), out.data(), packet.size()), 0);
}

}  // namespace ejd::net
