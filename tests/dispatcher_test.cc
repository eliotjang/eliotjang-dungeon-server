#include "core/dispatcher.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/session_packet.h"
#include "proto/messages.h"
#include "proto/packet_header.h"

namespace ejd::core {

namespace {
std::vector<char> MakePayload() {
  size_t data = 7;
  std::vector<char> result{};
  result.resize(sizeof(data));

  std::memcpy(result.data(), &data, sizeof(data));

  return result;
}

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

// Echo 등록 후 디스패치
TEST(DispatcherTest, DispatchEcho) {
  constexpr uint64_t session_id = 0;

  auto packet =
      MakePacket(static_cast<uint16_t>(proto::MsgId::kEcho), MakePayload());
  auto expected_packet = SessionPacket{
      session_id,
      std::move(packet),
  };

  std::vector<SessionPacket> result{};

  Dispatcher dispatcher;
  dispatcher.Register(proto::MsgId::kEcho, [](const SessionPacket& in,
                                              std::vector<SessionPacket>& out) {
    out.push_back(in);
  });

  dispatcher.Dispatch(expected_packet, result);
  ASSERT_EQ(result.size(), 1);
  auto actual_packet = result[0];

  EXPECT_EQ(actual_packet.session_id, expected_packet.session_id);
  EXPECT_EQ(actual_packet.packet, expected_packet.packet);
}

// 미등록 MsgId 케이스
TEST(DispatcherTest, NoRegisterEcho) {
  constexpr uint64_t session_id = 0;

  auto packet =
      MakePacket(static_cast<uint16_t>(proto::MsgId::kEcho), MakePayload());
  auto session_packet = SessionPacket{
      session_id,
      std::move(packet),
  };

  std::vector<SessionPacket> result{};

  Dispatcher dispatcher;
  dispatcher.Dispatch(session_packet, result);

  EXPECT_TRUE(result.empty());
}

}  // namespace ejd::core
