#include "db/connection.h"

#include <mysql/mysql.h>

#include <cstdlib>
#include <optional>
#include <string_view>

namespace ejd::db {

Connection::Connection() { handle_ = mysql_init(nullptr); }

Connection::~Connection() {
  if (handle_) mysql_close(handle_);
}

bool Connection::Connect(const ConnectParams& cp) {
  if (!mysql_real_connect(handle_, cp.host.c_str(), cp.user.c_str(),
                          cp.password.c_str(), cp.database.c_str(), cp.port,
                          nullptr, 0)) {
    return false;
  }

  return true;
}

bool Connection::Execute(std::string_view sql) {
  if (mysql_real_query(handle_, sql.data(),
                       static_cast<unsigned long>(sql.size())) != 0) {
    return false;
  }

  return true;
}

std::optional<int64_t> Connection::QueryScalar(std::string_view sql) {
  std::optional<int64_t> return_value = std::nullopt;

  if (!Execute(sql)) return std::nullopt;

  MYSQL_RES* result = mysql_store_result(handle_);
  if (!result) return std::nullopt;

  MYSQL_ROW row = mysql_fetch_row(result);
  if (row && row[0] != nullptr) {
    return_value = std::atoll(row[0]);
  }

  mysql_free_result(result);

  return return_value;
}

}  // namespace ejd::db
