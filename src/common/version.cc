#include "common/version.h"

#ifndef EJD_VERSION
#define EJD_VERSION "0.0.0-unknown"
#endif

namespace ejd::common {
std::string_view Version() { return EJD_VERSION; }

}  // namespace ejd::common
