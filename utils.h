#pragma once
#include <string>
#include <ctime>
#include <cstdlib>
#include <algorithm>

using namespace std;

namespace utils {

// Copying to-uppercase utility for strings.
inline string uppercase(string s) {
    transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// Get current local time formatted as "HH:MM:SS AM/PM".
inline string current_time() {
    time_t now = time(nullptr);
    struct tm* local_tm = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%I:%M:%S %p", local_tm);
    return buf;
}

// Parse ISO 8601 datetime (UTC) and format as ET time string.
inline string parse_time(const string& iso) {
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
