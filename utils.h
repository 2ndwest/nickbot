#pragma once
#include <string>
#include <ctime>
#include <cstdlib>
#include <algorithm>
#include <variant>
#include <mutex>

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

// Point the process timezone at ET. Every *_et helper below relies on this. main() calls it before
// starting any threads, since setenv() racing with localtime_r() on other threads isn't safe.
inline void use_et() {
    static std::once_flag once;
    std::call_once(once, [] { setenv("TZ", "America/New_York", 1); tzset(); });
}

// Broken-down ET time for a unix timestamp.
inline struct tm local_et(time_t t) {
    use_et();
    struct tm local_tm;
    localtime_r(&t, &local_tm);
    return local_tm;
}

// Format a unix timestamp in ET with strftime.
inline std::string strftime_et(time_t t, const char* format) {
    struct tm local_tm = local_et(t);
    char buf[32];
    strftime(buf, sizeof(buf), format, &local_tm);
    return buf;
}

// Format a unix timestamp as an ET time string ("HH:MM AM/PM").
inline std::string format_time_et(time_t t) { return strftime_et(t, "%I:%M %p"); }

// Format a unix timestamp as an ET date string ("YYYY-MM-DD").
inline std::string date_et(time_t t) { return strftime_et(t, "%Y-%m-%d"); }

// Day of the week in ET, 0 = Monday ... 6 = Sunday.
inline int weekday_et(time_t t) { return (local_et(t).tm_wday + 6) % 7; }

// `minute` minutes past midnight (ET) on the day `day_offset` days after the one containing `t`.
// Goes through mktime so DST transitions land on the right wall-clock time.
inline time_t at_minute_et(time_t t, int minute, int day_offset = 0) {
    struct tm local_tm = local_et(t);
    local_tm.tm_mday += day_offset;
    local_tm.tm_hour = 0;
    local_tm.tm_min = minute;
    local_tm.tm_sec = 0;
    local_tm.tm_isdst = -1;
    return mktime(&local_tm);
}

// Parse a naive ISO 8601 datetime in ET ("2026-09-23T14:00:00") into a unix timestamp.
inline time_t parse_iso_et(const std::string& iso) {
    use_et();
    struct tm local_tm = {};
    strptime(iso.c_str(), "%Y-%m-%dT%H:%M:%S", &local_tm);
    local_tm.tm_isdst = -1;
    return mktime(&local_tm);
}

} // namespace utils
