#pragma once

#include <cstdlib>

namespace config {

inline const char* token() { return std::getenv("BOT_TOKEN"); }
inline const char* kerb() { return std::getenv("TOUCHSTONE_USERNAME"); }
inline const char* kerb_password() { return std::getenv("TOUCHSTONE_PASSWORD"); }
inline const char* admin_user_id() { return std::getenv("ADMIN_USER_ID"); }
inline const char* cookiefile() { return std::getenv("COOKIEFILE"); }

// Optional: wokenet's Convex HTTP actions URL (e.g. https://happy-animal-123.convex.site) and the shared secret
// its /ingest-room-availability endpoint checks. Room open times are pushed there after every sweep when both are set.
inline const char* convex_site_url() { return std::getenv("CONVEX_SITE_URL"); }
inline const char* rooms_webhook_secret() { return std::getenv("ROOMS_WEBHOOK_SECRET"); }

}
