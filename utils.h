#pragma once
#include <string>
#include <ctime>
#include <cstdlib>
#include <algorithm>
#include <variant>

namespace utils {

// Extract a value from a variant, returning a fallback if not present.
template <typename T, typename Variant>
inline T get_or(const Variant& v, const T& fallback) {
    return std::holds_alternative<T>(v) ? std::get<T>(v) : fallback;
}

// Copying to-uppercase utility for strings.
inline std::string uppercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// Get current local time formatted as "HH:MM:SS AM/PM".
inline std::string current_time() {
    time_t now = time(nullptr);
    struct tm* local_tm = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%I:%M:%S %p", local_tm);
    return buf;
}

// Parse ISO 8601 datetime (UTC) into a unix timestamp.
inline time_t parse_iso_utc(const std::string& iso) {
    struct tm tm = {};
    strptime(iso.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
    return timegm(&tm);
}

// Format a unix timestamp as an ET time string ("HH:MM AM/PM").
inline std::string format_time_et(time_t t) {
    setenv("TZ", "America/New_York", 1);
    tzset();
    struct tm* local_tm = localtime(&t);
    char buf[16];
    strftime(buf, sizeof(buf), "%I:%M %p", local_tm);
    return buf;
}

} // namespace utils
