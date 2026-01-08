#pragma once
#include <string>
#include <ctime>
#include <cstdlib>
#include <algorithm>

namespace utils {

// Copying to-uppercase utility for strings.
inline std::string uppercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// Parse ISO 8601 datetime (UTC) and format as ET time string.
inline std::string parse_time(const std::string& iso) {
    struct tm tm = {};
    strptime(iso.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
    // Treat the parsed time as UTC
    setenv("TZ", "UTC", 1);
    tzset();
    time_t t = mktime(&tm);
    // Now convert to ET timezone for display
    setenv("TZ", "America/New_York", 1);
    tzset();
    struct tm* local_tm = localtime(&t);
    char buf[16];
    strftime(buf, sizeof(buf), "%I:%M %p", local_tm);
    return buf;
}

} // namespace utils
