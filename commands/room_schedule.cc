#include "room_schedule.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <cpr/cpr.h>
#include <json.h>
#include <libtouchstone.h>
#include "config.h"
#include "db.h"
#include "utils.h"
#include "mit_buildings.h"
#include "mit_rooms.h"

namespace {

// How often every room's bookings are re-fetched.
constexpr auto SWEEP_INTERVAL = std::chrono::hours(1);

// Delay between roomBookings requests, so a sweep trickles out over ~20 minutes rather than hammering classrooms.mit.edu.
constexpr auto REQUEST_INTERVAL = std::chrono::seconds(3);

// A room whose bookings were fetched longer ago than this is left out rather than trusted.
constexpr time_t MAX_STALENESS = 6 * 60 * 60;

// Only list rooms whose free window starts within this long from now...
constexpr time_t LOOKAHEAD = 2 * 60 * 60;

// ...and lasts at least this long.
constexpr time_t MIN_FREE = 30 * 60;

// Hydrant encodes meeting times as half-hour slots, 34 per weekday starting at 6 AM, Monday first.
constexpr int HYDRANT_SLOTS_PER_DAY = 34;
constexpr int HYDRANT_FIRST_SLOT_MINUTE = 6 * 60;

// A weekly class meeting from Hydrant.
struct hydrant_meeting {
    int weekday;              // 0 = Monday ... 4 = Friday.
    int start_minute;         // Minutes past midnight ET.
    int end_minute;
    std::string first_day;    // "YYYY-MM-DD", inclusive. Half-term and quarter classes don't span the whole term.
    std::string last_day;
};

struct hydrant_term {
    std::string start, end;      // "YYYY-MM-DD", inclusive.
    std::string monday_schedule; // A Tuesday that runs Monday's schedule, if any.
    std::set<std::string> holidays;
};

std::mutex mutex; // Guards everything below.
std::map<std::string, db::RoomBookings> bookings; // Keyed by room number.
hydrant_term term;
std::map<std::string, std::vector<hydrant_meeting>> hydrant_meetings; // Keyed by room number.
bool refresh_requested = false;
std::condition_variable wake;
room_schedule::sweep_status progress{false, 0, mit_rooms::rooms.size()};

// "W41-1119" -> "W41".
std::string building_of(const std::string& room) {
    return utils::uppercase(room.substr(0, room.find('-')));
}

// Fetches Hydrant's class schedule and indexes the meetings in every room.
bool load_hydrant() {
    std::cout << "[?] Fetching Hydrant class schedule...\n";
    cpr::Response r = cpr::Get(cpr::Url{"https://hydrant.mit.edu/latest.json"}, cpr::Timeout{std::chrono::seconds(60)});
    if (r.error || r.status_code != 200) {
        std::cout << "[!] Failed to fetch Hydrant (status " << r.status_code << "): " << r.error.message << "\n";
        return false;
    }

    auto [status, json] = jt::Json::parse(r.text);
    if (status != jt::Json::success) {
        std::cout << "[!] Failed to parse Hydrant JSON: " << jt::Json::StatusToString(status) << "\n";
        return false;
    }

    hydrant_term new_term;
    std::map<std::string, std::vector<hydrant_meeting>> new_meetings;
    try {
        jt::Json& info = json["termInfo"];
        new_term.start = info["startDate"].getString();
        new_term.end = info["endDate"].getString();
        if (info["mondayScheduleDate"].isString()) new_term.monday_schedule = info["mondayScheduleDate"].getString();
        for (const auto& day : info["holidayDates"].getArray()) new_term.holidays.insert(day.getString());
        std::string year = new_term.start.substr(0, 4);

        for (auto& [_, cls] : json["classes"].getObject()) {
            std::string first_day = new_term.start, last_day = new_term.end;

            // Half-term classes meet in only one half of the term.
            if (cls["half"].isLong()) {
                if (cls["half"].getLong() == 1) last_day = info["h1EndDate"].getString();
                if (cls["half"].getLong() == 2) first_day = info["h2StartDate"].getString();
            }
            // Some classes start or end partway through the term, given as [month, day].
            if (cls["quarterInfo"].isObject()) {
                auto date = [&](const jt::Json& month_day) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%s-%02lld-%02lld", year.c_str(), month_day[0].getLong(), month_day[1].getLong());
                    return std::string(buf);
                };
                if (cls["quarterInfo"]["start"].isArray()) first_day = date(cls["quarterInfo"]["start"]);
                if (cls["quarterInfo"]["end"].isArray()) last_day = date(cls["quarterInfo"]["end"]);
            }

            for (const char* kind : {"lectureSections", "recitationSections", "labSections", "designSections"}) {
                if (!cls[kind].isArray()) continue;
                // Each section is [[[slot, slot count], ...], room].
                for (const auto& section : cls[kind].getArray()) {
                    std::string room = utils::uppercase(section[1].getString());
                    for (const auto& time : section[0].getArray()) {
                        int slot = (int)time[0].getLong(), length = (int)time[1].getLong();
                        int start_minute = HYDRANT_FIRST_SLOT_MINUTE + (slot % HYDRANT_SLOTS_PER_DAY) * 30;
                        new_meetings[room].push_back({
                            slot / HYDRANT_SLOTS_PER_DAY,
                            start_minute,
                            start_minute + length * 30,
                            first_day,
                            last_day
                        });
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cout << "[!] Unexpected Hydrant JSON structure: " << e.what() << "\n";
        return false;
    }

    std::cout << "[*] Loaded Hydrant schedule for " << new_meetings.size() << " rooms.\n";
    std::lock_guard lock(mutex);
    term = std::move(new_term);
    hydrant_meetings = std::move(new_meetings);
    return true;
}

// Fetches a room's bookings for today and tomorrow from classrooms.mit.edu. Returns std::nullopt on
// failure, additionally setting auth_error if the failure was Touchstone's.
std::optional<db::RoomBookings> fetch_room(cpr::Session& s, const std::string& room, std::string& auth_error) {
    time_t now = time(nullptr);
    std::string url = "https://classrooms.mit.edu/classrooms/roomBookings?roomNumber=" + room +
        "&startDate=" + utils::date_et(now) + "&endDate=" + utils::date_et(utils::at_minute_et(now, 0, 1));

    cpr::Response r = libtouchstone::authenticate(s, url.c_str(),
        config::kerb(), config::kerb_password(),
        // block = false is critical, we don't want to be stuck waiting for a 2FA prompt
        {config::cookiefile(), false, false}
    );
    if (r.error) {
        auth_error = r.error.message;
        return std::nullopt;
    }

    auto [status, json] = jt::Json::parse(r.text);
    if (r.status_code != 200 || status != jt::Json::success || !json.isObject() || json.contains("error")) {
        std::cout << "[!] Bad roomBookings response for " << room << " (status " << r.status_code << "): " << r.text.substr(0, 100) << "\n";
        return std::nullopt;
    }

    // Shaped like {"W41-1119": {"2026-09-23": [{"startTime": "2026-09-23T14:00:00", "durationInMinutes": 180, ...}]}},
    // or {} if the room has no bookings (or doesn't exist).
    db::RoomBookings result{now, {}};
    try {
        for (const auto& [room_number, dates] : json.getObject()) {
            for (const auto& [date, day] : dates.getObject()) {
                for (const auto& booking : day.getArray()) {
                    time_t start = utils::parse_iso_et(booking["startTime"].getString());
                    result.bookings.push_back({start, start + (time_t)booking["durationInMinutes"].getNumber() * 60});
                }
            }
        }
    } catch (const std::exception& e) {
        std::cout << "[!] Unexpected roomBookings JSON structure for " << room << ": " << e.what() << "\n";
        return std::nullopt;
    }
    return result;
}

// Re-fetches every tracked room's bookings, slowly. Gives up at the first Touchstone failure.
void sweep(sqlite3* database, const std::function<void(const std::string&)>& on_auth_failure, bool& alerted) {
    std::cout << "[?] Refreshing room bookings for " << mit_rooms::rooms.size() << " rooms...\n";
    size_t fetched = 0;
    {
        std::lock_guard lock(mutex);
        progress.running = true;
        progress.done = 0;
    }

    // One session for the whole sweep keeps the connection alive between requests. It writes the cookie
    // jar when destroyed, at the end of the sweep.
    cpr::Session s = libtouchstone::session(config::cookiefile());
    for (const auto& room : mit_rooms::rooms) {
        std::string auth_error;
        auto result = fetch_room(s, room, auth_error);

        if (!auth_error.empty()) {
            std::cout << "[!] Touchstone auth failed while refreshing room bookings: " << auth_error << "\n";
            if (!alerted) on_auth_failure(auth_error);
            alerted = true;
            std::lock_guard lock(mutex);
            progress.running = false;
            return;
        }
        alerted = false;

        if (result && database) db::replace_room_bookings(database, room, *result);
        {
            std::lock_guard lock(mutex);
            if (result) bookings[room] = std::move(*result);
            progress.done++;
        }
        fetched += result.has_value();
        std::this_thread::sleep_for(REQUEST_INTERVAL);
    }

    {
        std::lock_guard lock(mutex);
        progress.running = false;
    }

    std::cout << "[*] Refreshed room bookings for " << fetched << "/" << mit_rooms::rooms.size() << " rooms.\n";
}

// Earliest free window of at least MIN_FREE that starts within LOOKAHEAD of now, capped at day_end.
std::optional<std::pair<time_t, time_t>> first_free_window(std::vector<std::pair<time_t, time_t>> busy, time_t now, time_t day_end) {
    std::sort(busy.begin(), busy.end());
    time_t begin = now;
    for (const auto& [start, end] : busy) {
        if (begin >= now + LOOKAHEAD || begin >= day_end) return std::nullopt;
        if (start > begin && std::min(start, day_end) - begin >= MIN_FREE) return std::make_pair(begin, std::min(start, day_end));
        begin = std::max(begin, end);
    }
    if (begin < now + LOOKAHEAD && day_end - begin >= MIN_FREE) return std::make_pair(begin, day_end);
    return std::nullopt;
}

} // namespace

void room_schedule::start(std::function<void(const std::string&)> on_auth_failure) {
    // The sweep gets its own connection: sharing one would let its transactions swallow other threads' writes.
    sqlite3* database = db::init();
    if (database) {
        auto cached = db::get_room_bookings(database);
        // Drop rooms cached before they were removed from mit_rooms.h.
        std::set<std::string> tracked(mit_rooms::rooms.begin(), mit_rooms::rooms.end());
        for (auto it = cached.begin(); it != cached.end();) it = tracked.count(it->first) ? std::next(it) : cached.erase(it);
        std::lock_guard lock(mutex);
        bookings = std::move(cached);
    }

    std::thread([database, on_auth_failure = std::move(on_auth_failure)] {
        bool alerted = false; // Whether on_auth_failure was already called for the current failure streak.
        bool hydrant_loaded = false;
        for (;;) {
            if (!hydrant_loaded) hydrant_loaded = load_hydrant();

            auto next_sweep = std::chrono::steady_clock::now() + SWEEP_INTERVAL;
            sweep(database, on_auth_failure, alerted);

            std::unique_lock lock(mutex);
            wake.wait_until(lock, next_sweep, [] { return refresh_requested; });
            refresh_requested = false;
        }
    }).detach();
}

void room_schedule::refresh_now() {
    std::lock_guard lock(mutex);
    refresh_requested = true;
    wake.notify_one();
}

room_schedule::sweep_status room_schedule::get_sweep_status() {
    std::lock_guard lock(mutex);
    return progress;
}

std::optional<std::vector<room_schedule::free_room>> room_schedule::find_free_rooms(time_t now) {
    std::lock_guard lock(mutex);

    std::string today = utils::date_et(now);
    time_t day_end = utils::at_minute_et(now, 0, 1);
    time_t midnight = utils::at_minute_et(now, 0);
    int weekday = today == term.monday_schedule ? 0 : utils::weekday_et(now);
    bool classes_today = !term.holidays.count(today); // Term bounds are checked per meeting.

    std::vector<free_room> rooms;
    bool any_fresh = false;
    for (const auto& [room, fetched] : bookings) {
        if (now - fetched.fetched_at > MAX_STALENESS) continue;
        any_fresh = true;

        // A room is busy whenever either source says so: roomBookings misses some classes (mostly in
        // departmental rooms) that Hydrant knows about.
        std::vector<std::pair<time_t, time_t>> busy = fetched.bookings;
        auto meetings = hydrant_meetings.find(room);
        if (classes_today && meetings != hydrant_meetings.end()) {
            for (const auto& meeting : meetings->second) {
                if (meeting.weekday != weekday || today < meeting.first_day || today > meeting.last_day) continue;
                // Classes only meet on weekdays, so no DST switch (2 AM Sunday) falls between midnight and a meeting.
                busy.push_back({midnight + meeting.start_minute * 60, midnight + meeting.end_minute * 60});
            }
        }

        auto window = first_free_window(busy, now, day_end);
        if (!window) continue;
        auto [begin, end] = *window;
        rooms.push_back({room, graph_building(building_of(room)), begin, end, end == day_end});
    }

    if (!any_fresh) return std::nullopt;
    return rooms;
}

std::string room_schedule::graph_building(const std::string& building) {
    if (mit_buildings::neighbors.count(building)) return building;
    // Wings like "14N" are lettered off a numbered building ("14").
    std::string base = building;
    while (!base.empty() && std::isalpha((unsigned char)base.back())) base.pop_back();
    return mit_buildings::neighbors.count(base) ? base : building;
}

bool room_schedule::has_rooms_in(const std::string& building) {
    static const std::set<std::string> buildings = [] {
        std::set<std::string> out;
        for (const auto& room : mit_rooms::rooms) out.insert(graph_building(building_of(room)));
        return out;
    }();
    return buildings.count(building);
}
