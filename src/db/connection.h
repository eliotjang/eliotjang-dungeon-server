#pragma once

#include <mysql/mysql.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ejd::db {

struct ConnectParams {
  std::string host;
  std::string user;
  std::string password;
  std::string database;
  uint16_t port;
};

class Connection {
 public:
  Connection();
  ~Connection();
  Connection(const Connection&) = delete;

  [[nodiscard]] bool Connect(const ConnectParams& cp);
  [[nodiscard]] bool Execute(std::string_view sql);
  [[nodiscard]] std::optional<int64_t> QueryScalar(std::string_view sql);

  uint32_t last_errno() const { return mysql_errno(handle_); }
  const char* last_error() const { return mysql_error(handle_); }

 private:
  friend class Transaction;
  MYSQL* handle_{};
};

}  // namespace ejd::db
