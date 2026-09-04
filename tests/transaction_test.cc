#include "db/transaction.h"

#include <gtest/gtest.h>

#include "db/connection.h"

namespace ejd::db {

class TransactionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ConnectParams cp{};
    cp.host = "127.0.0.1";
    cp.user = "root";
    cp.password = "13504";
    cp.database = "ejd_game";
    cp.port = 3306;

    if (!conn_.Connect(cp)) {
      GTEST_SKIP() << "MySQL 미기동: docker start ejd-mysql";
    }

    auto transaction = Transaction(conn_);
    ASSERT_TRUE(
        conn_.Execute("DELETE FROM account WHERE name IN ('user1','user2')"));
    ASSERT_TRUE(transaction.Commit());
  }

  Connection conn_;
};

// 소멸자 롤백 검증
TEST_F(TransactionTest, ExpectRollback) {
  {
    auto transaction = Transaction(conn_);
    ASSERT_TRUE(conn_.Execute("INSERT INTO account VALUES(NULL, 'user1', 1)"));
  }
  auto n =
      conn_.QueryScalar("SELECT COUNT(*) FROM account WHERE name = 'user1'");
  EXPECT_TRUE(n.has_value());
  EXPECT_EQ(n.value(), 0);
}

// 트랜잭션 정상 커밋
TEST_F(TransactionTest, ExpectCommit) {
  auto transaction = Transaction(conn_);
  ASSERT_TRUE(conn_.Execute("INSERT INTO account VALUES(NULL, 'user2', 1)"));
  EXPECT_TRUE(transaction.Commit());

  auto n =
      conn_.QueryScalar("SELECT COUNT(*) FROM account WHERE name = 'user2'");
  EXPECT_TRUE(n.has_value());
  EXPECT_EQ(n.value(), 1);
}

}  // namespace ejd::db
