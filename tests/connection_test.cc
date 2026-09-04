#include "db/connection.h"

#include <gtest/gtest.h>
#include <mysql/mysql.h>

#include <cerrno>
#include <iostream>

namespace ejd::db {

// MySQL 버전
TEST(ConnectionTest, PrintVersion) {
  std::cout << "mysqlclient version : " << mysql_get_client_info() << "\n";
}

// 스칼라 쿼리 정상 조회
TEST(ConnectionTest, QueryScalar) {
  ConnectParams cp{};
  cp.host = "127.0.0.1";
  cp.user = "root";
  cp.password = "13504";
  cp.database = "ejd_game";
  cp.port = 3306;

  auto connection = Connection();
  EXPECT_TRUE(connection.Connect(cp));
  auto n = connection.QueryScalar("SELECT 1");
  ASSERT_TRUE(n.has_value());
  EXPECT_EQ(*n, 1);
}

// 접속 불가 검증
TEST(ConnectionTest, ConnectWrongPort) {
  ConnectParams cp{};
  cp.host = "127.0.0.1";
  cp.user = "root";
  cp.password = "13504";
  cp.database = "ejd_game";
  cp.port = 3307;

  auto connection = Connection();
  EXPECT_FALSE(connection.Connect(cp));
  EXPECT_NE(connection.last_errno(), 0);
  EXPECT_NE(connection.last_error(), nullptr);
  EXPECT_NE(connection.last_error()[0], '\0');
}

}  // namespace ejd::db
