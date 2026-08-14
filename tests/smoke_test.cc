#include <gtest/gtest.h>

#include "common/version.h"

TEST(SmokeTest, Version) {
  EXPECT_FALSE(ejd::common::Version().empty());
  EXPECT_EQ(ejd::common::Version(), "0.1.0");
}
