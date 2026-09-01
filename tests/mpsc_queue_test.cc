#include "core/mpsc_queue.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stop_token>
#include <thread>
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

std::vector<SessionPacket> MakeSessionPackets(uint64_t session_id,
                                              size_t packet_count) {
  std::vector<SessionPacket> result{};
  result.reserve(packet_count);

  for (size_t seq = 0; seq < packet_count; ++seq) {
    std::vector<char> payload{};
    payload.resize(sizeof(seq));

    std::memcpy(payload.data(), &seq, sizeof(seq));
    auto packet = MakePacket(0, payload);
    result.emplace_back(SessionPacket(session_id, std::move(packet)));
  }

  return result;
}

uint64_t DecodeSeq(const std::vector<char>& packet) {
  uint64_t seq{};
  std::memcpy(&seq, packet.data() + proto::kHeaderSize, sizeof(seq));
  return seq;
}
}  // namespace

// 세션패킷 정상 반환
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

// 빈 큐 반환
TEST(MpscQueueTest, ReturnsEmptyDrain) {
  auto queue = MpscQueue<SessionPacket>();

  std::vector<SessionPacket> out{};
  EXPECT_EQ(0, queue.TryDrain(out));
}

// 드레인 후 재드레인
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

// 타임아웃 반환
TEST(MpscQueueTest, TimeoutReturnsZeroAtDeadline) {
  auto queue = MpscQueue<SessionPacket>();

  std::vector<SessionPacket> out{};

  auto now = std::chrono::steady_clock::now();
  auto deadline = now + std::chrono::milliseconds(50);

  std::stop_source sc{};
  EXPECT_EQ(0, queue.WaitDrainUntil(out, sc.get_token(), deadline));
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - now);

  EXPECT_GE(elapsed.count(), 50);
  EXPECT_LE(elapsed.count(), 2000);
}

// 다중 생산자
TEST(MpscQueueTest, FourProducersNoLossNoDuplication) {
  constexpr int producer_count = 4;
  constexpr size_t packet_count = 10'000;

  std::vector<std::vector<SessionPacket>> session_packets(producer_count);
  for (int i = 0; i < producer_count; ++i) {
    session_packets[i] = MakeSessionPackets(i, packet_count);
  }

  auto queue = MpscQueue<SessionPacket>();

  auto PushSessionPackets = [&](int producer_idx) {
    for (size_t i = 0; i < session_packets[producer_idx].size(); ++i) {
      queue.Push(session_packets[producer_idx][i]);
    }
  };

  std::array<int, producer_count> expected_next{};
  size_t total{};
  std::stop_source sc{};

  std::vector<std::jthread> threads{};
  for (int i = 0; i < producer_count; ++i)
    threads.emplace_back(PushSessionPackets, i);

  auto overall_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  std::vector<SessionPacket> result{};
  while (true) {
    auto now = std::chrono::steady_clock::now();
    ASSERT_TRUE(now < overall_deadline);

    auto deadline = now + std::chrono::milliseconds(10);
    queue.WaitDrainUntil(result, sc.get_token(), deadline);
    for (size_t i = 0; i < result.size(); ++i) {
      auto producer_idx = result[i].session_id;
      ASSERT_TRUE(producer_idx < producer_count);
      auto seq = DecodeSeq(result[i].packet);
      ASSERT_EQ(seq, static_cast<size_t>(expected_next[producer_idx]));
      ++expected_next[producer_idx];
      ++total;
    }
    result.clear();

    if (total == producer_count * packet_count) break;
  }

  for (int i = 0; i < producer_count; ++i)
    EXPECT_EQ(expected_next[i], packet_count);

  EXPECT_EQ(total, producer_count * packet_count);
}

// push가 대기자 wake
TEST(MpscQueueTest, PushWakesWaiterBeforeDeadline) {
  constexpr size_t packet_count = 1;

  auto queue = MpscQueue<SessionPacket>();
  std::vector<SessionPacket> result{};
  std::stop_source sc{};
  auto start = std::chrono::steady_clock::now();
  auto deadline = start + std::chrono::seconds(5);

  auto producer = std::jthread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    auto session_packets = MakeSessionPackets(0, packet_count);
    queue.Push(std::move(session_packets[0]));
  });

  size_t n = queue.WaitDrainUntil(result, sc.get_token(), deadline);
  auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(n, 1);
  EXPECT_LT(elapsed, std::chrono::seconds(1));
}

// stop이 대기자 wake
TEST(MpscQueueTest, StopRequestWakesWaiter) {
  auto queue = MpscQueue<SessionPacket>();
  std::vector<SessionPacket> result{};
  std::stop_source sc{};
  auto start = std::chrono::steady_clock::now();
  auto deadline = start + std::chrono::seconds(5);

  auto stopper = std::jthread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_TRUE(sc.request_stop());
  });

  size_t n = queue.WaitDrainUntil(result, sc.get_token(), deadline);
  auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(n, 0);
  EXPECT_LT(elapsed, std::chrono::seconds(1));
}

}  // namespace ejd::core
