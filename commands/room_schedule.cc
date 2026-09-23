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

using interval = std::pair<time_t, time_t>;

// Every gap between `busy` intervals within [from, to).
std::vector<interval> free_intervals(std::vector<interval> busy, time_t from, time_t to) {
    std::sort(busy.begin(), busy.end());
    std::vector<interval> free;
    time_t begin = from;
    for (const auto& [start, end] : busy) {
        if (begin >= to) break;
        if (start > begin) free.push_back({begin, std::min(start, to)});
        begin = std::max(begin, end);
    }
    if (begin < to) free.push_back({begin, to});
    return free;
}

// A room's busy time on the ET day starting at `day_start`. It's busy whenever either source says so:
// roomBookings misses some classes (mostly in departmental rooms) that Hydrant knows about. Caller holds `mutex`.
std::vector<interval> busy_on_day(const std::string& room, const db::RoomBookings& fetched, time_t day_start) {
    std::vector<interval> busy = fetched.bookings;
    std::string day = utils::date_et(day_start);
    int weekday = day == term.monday_schedule ? 0 : utils::weekday_et(day_start);
    auto meetings = hydrant_meetings.find(room);
    if (term.holidays.count(day) || meetings == hydrant_meetings.end()) return busy;

    for (const auto& meeting : meetings->second) {
        // Term bounds are checked per meeting, since first_day/last_day start out as the term's.
        if (meeting.weekday != weekday || day < meeting.first_day || day > meeting.last_day) continue;
        // Classes only meet on weekdays, so no DST switch (2 AM Sunday) falls between midnight and a meeting.
        busy.push_back({day_start + meeting.start_minute * 60, day_start + meeting.end_minute * 60});
    }
    return busy;
}

// POSTs every room's open times to wokenet's Convex backend, if configured.
void push_open_times() {
    if (!config::convex_site_url() || !config::rooms_webhook_secret()) return;

    auto rooms = room_schedule::open_times(time(nullptr));
    if (rooms.empty()) return; // Nothing fresh; leave wokenet's last data (and its timestamps) in place.

    // Shaped like {"rooms": [{"room": "W41-1119", "building": "W41", "open": [{"start": ms, "end": ms}, ...]}, ...]}.
    jt::Json body;
    body["rooms"].setArray();
    for (const auto& room : rooms) {
        jt::Json entry;
        entry["room"] = room.room;
        entry["building"] = room.building;
        entry["open"].setArray();
        for (const auto& [start, end] : room.open) {
            jt::Json window;
            window["start"] = (long long)start * 1000;
            window["end"] = (long long)end * 1000;
            entry["open"].getArray().push_back(std::move(window));
        }
        body["rooms"].getArray().push_back(std::move(entry));
    }

    cpr::Response r = cpr::Post(
        cpr::Url{std::string(config::convex_site_url()) + "/ingest-room-availability"},
        cpr::Header{{"Content-Type", "application/json"}, {"x-webhook-secret", config::rooms_webhook_secret()}},
        cpr::Body{body.toString()},
        cpr::Timeout{std::chrono::seconds(30)}
    );
    if (r.error || r.status_code != 200) {
        std::cout << "[!] Failed to push room open times to wokenet (status " << r.status_code << "): " << (r.error ? r.error.message : r.text) << "\n";
    } else {
        std::cout << "[*] Pushed open times for " << rooms.size() << " rooms to wokenet.\n";
    }
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
            push_open_times();

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

    time_t midnight = utils::at_minute_et(now, 0);
    time_t day_end = utils::at_minute_et(now, 0, 1);

    std::vector<free_room> rooms;
    bool any_fresh = false;
    for (const auto& [room, fetched] : bookings) {
        if (now - fetched.fetched_at > MAX_STALENESS) continue;
        any_fresh = true;

        // The first window of at least MIN_FREE that starts within LOOKAHEAD of now.
        for (const auto& [begin, end] : free_intervals(busy_on_day(room, fetched, midnight), now, day_end)) {
            if (begin >= now + LOOKAHEAD) break;
            if (end - begin < MIN_FREE) continue;
            rooms.push_back({room, graph_building(building_of(room)), begin, end, end == day_end});
            break;
        }
    }

    if (!any_fresh) return std::nullopt;
    return rooms;
}

std::vector<room_schedule::room_open_times> room_schedule::open_times(time_t now) {
    std::lock_guard lock(mutex);

    std::vector<room_open_times> rooms;
    for (const auto& [room, fetched] : bookings) {
        if (now - fetched.fetched_at > MAX_STALENESS) continue;

        room_open_times entry{room, graph_building(building_of(room)), {}};
        for (int day = 0; day < 2; day++) {
            time_t day_start = utils::at_minute_et(now, 0, day), day_end = utils::at_minute_et(now, 0, day + 1);
            for (const auto& window : free_intervals(busy_on_day(room, fetched, day_start), day_start, day_end)) {
                // Join windows that run across midnight.
                if (!entry.open.empty() && entry.open.back().second == window.first) entry.open.back().second = window.second;
                else entry.open.push_back(window);
            }
        }
        rooms.push_back(std::move(entry));
    }
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
