#include "db/transaction.h"

#include <format>
#include <iostream>

#include "db/connection.h"

namespace ejd::db {

Transaction::Transaction(Connection& conn) : conn_(conn) {
  if (conn_.Execute("START TRANSACTION")) {
    active_ = true;
  }
}

Transaction::~Transaction() {
  if (active_ && !committed_) {
    if (!conn_.Execute("ROLLBACK")) {
      std::cerr << std::format("errno={}, error={}\n", conn_.last_errno(),
                               conn_.last_error());
    }
  }
}

bool Transaction::Commit() {
  if (!active_ || committed_) {
    return false;
  }

  if (!conn_.Execute("COMMIT")) {
    return false;
  }
  committed_ = true;

  return true;
}

}  // namespace ejd::db
