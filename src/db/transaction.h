#pragma once

#include "db/connection.h"

namespace ejd::db {

class Transaction {
 public:
  explicit Transaction(Connection& conn);
  ~Transaction();
  Transaction(const Transaction&) = delete;

  [[nodiscard]] bool active() const { return active_; };
  [[nodiscard]] bool Commit();

 private:
  Connection& conn_;
  bool active_{};
  bool committed_{};
};

}  // namespace ejd::db
