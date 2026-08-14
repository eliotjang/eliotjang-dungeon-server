#include <iostream>

#include "common/version.h"

int main() {
  std::cout << ejd::common::Version() << std::endl;
  return 0;
}