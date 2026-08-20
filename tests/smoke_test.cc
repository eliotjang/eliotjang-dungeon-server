#include <gtest/gtest.h>

#include "common/version.h"

namespace ejd::common {

TEST(SmokeTest, Version) {
  EXPECT_FALSE(Version().empty());
  EXPECT_EQ(Version(), "0.1.0");
}

}  // namespace ejd::common
