#include "net/ring_buffer.h"

#include <gtest/gtest.h>

#include <cstring>

namespace ejd::net {

TEST(RingBufferTest, WriteThenPeekReturnsSameBytes) {
  auto ring = RingBuffer(16);
  const auto in = "hello";
  size_t len = strlen(in);

  ASSERT_TRUE(ring.Write(in, len));
  char out[5]{};
  ASSERT_TRUE(ring.Peek(out, len));

  EXPECT_EQ(std::memcmp(in, out, len), 0);
  EXPECT_EQ(ring.size(), len);
}

TEST(RingBufferTest, WriteRejectedWhenFull) {
  constexpr size_t kLen = 8;
  auto ring = RingBuffer(kLen);
  const char data[kLen]{};
  ASSERT_TRUE(ring.Write(data, kLen));

  auto ch = 'x';
  EXPECT_FALSE(ring.Write(&ch, 1));
  EXPECT_EQ(ring.size(), kLen);
}

TEST(RingBufferTest, WriteWrapAround) {
  auto ring = RingBuffer(8);
  const auto first_msg = "hello";
  size_t first_len = strlen(first_msg);

  ASSERT_TRUE(ring.Write(first_msg, first_len));

  ring.Consume(first_len);

  const auto second_msg = "world";
  size_t second_len = strlen(second_msg);

  ASSERT_TRUE(ring.Write(second_msg, second_len));
  char out[5]{};
  ASSERT_TRUE(ring.Peek(out, second_len));
  EXPECT_EQ(std::memcmp(second_msg, out, second_len), 0);
}

TEST(RingBufferTest, ConsumeRestoresFreeSpace) {
  auto ring = RingBuffer(8);
  const auto data = "abcdef";
  ASSERT_TRUE(ring.Write(data, 6));

  ring.Consume(4);

  EXPECT_EQ(ring.size(), 2u);
  EXPECT_EQ(ring.free_space(), 6u);
  char out[2]{};
  ASSERT_TRUE(ring.Peek(out, 2));
  EXPECT_EQ(out[0], 'e');
  EXPECT_EQ(out[1], 'f');
}

TEST(RingBufferDeathTest, ConsumeMoreThanSizeDies) {
  auto ring = RingBuffer(8);
  const char data[2]{};
  ASSERT_TRUE(ring.Write(data, 2));
  EXPECT_DEATH(ring.Consume(3), "");
}
}  // namespace ejd::net
