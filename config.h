#pragma once

#include <cstdlib>

namespace config {

inline const char* token() { return std::getenv("BOT_TOKEN"); }
inline const char* kerb() { return std::getenv("TOUCHSTONE_USERNAME"); }
inline const char* kerb_password() { return std::getenv("TOUCHSTONE_PASSWORD"); }

}
