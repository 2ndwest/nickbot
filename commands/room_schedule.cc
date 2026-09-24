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

// A room's bookings are re-fetched once they're this old. Fetch times are kept in SQLite, so restarting the bot
// doesn't trigger a refresh, and a refresh interrupted by a restart picks up where it left off.
constexpr time_t REFRESH_AFTER = 6 * 60 * 60;

// Delay between roomBookings requests, so a refresh trickles out rather than hammering classrooms.mit.edu.
constexpr auto REQUEST_INTERVAL = std::chrono::seconds(3);

// After a Touchstone failure, wait this long before trying again (a successful reauth wakes the thread sooner).
constexpr auto AUTH_RETRY_INTERVAL = std::chrono::hours(1);

// The refresh thread never sleeps longer than this, so it rechecks wall-clock time after the machine sleeps.
constexpr auto MAX_NAP = std::chrono::minutes(10);

// Refreshed rooms are pushed to wokenet in batches of this many, so progress shows up as a refresh goes.
constexpr size_t PUSH_BATCH = 25;

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
room_schedule::sweep_status progress{false, 0, 0};

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

// A fetch covers the day it ran and the next (see fetch_room). Past this midnight its bookings are unknown, so the room
// counts as booked rather than free.
time_t covered_until(const db::RoomBookings& fetched) {
    return utils::at_minute_et(fetched.fetched_at, 0, 2);
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

// POSTs the given rooms' open times to wokenet's Convex backend, if configured. Every push also lists all tracked
// rooms, so wokenet can drop rooms that were removed from mit_rooms.h.
void push_open_times(const std::vector<std::string>& rooms) {
    if (rooms.empty() || !config::convex_site_url() || !config::classrooms_webhook_secret()) return;

    // Shaped like {"classrooms": [{"room": "W41-1119", "building": "W41", "capacity": 25, "updatedAt": ms,
    // "open": [{"start": ms, "end": ms}, ...]}, ...], "tracked": ["1-131", ...]}, with "capacity" left out when unknown.
    jt::Json body;
    body["classrooms"].setArray();
    for (const auto& room : room_schedule::open_times(time(nullptr), rooms)) {
        jt::Json entry;
        entry["room"] = room.room;
        entry["building"] = room.building;
        if (room.capacity) entry["capacity"] = *room.capacity;
        entry["updatedAt"] = (long long)room.fetched_at * 1000;
        entry["open"].setArray();
        for (const auto& [start, end] : room.open) {
            jt::Json window;
            window["start"] = (long long)start * 1000;
            window["end"] = (long long)end * 1000;
            entry["open"].getArray().push_back(std::move(window));
        }
        body["classrooms"].getArray().push_back(std::move(entry));
    }
    body["tracked"].setArray();
    for (const auto& room : mit_rooms::rooms) body["tracked"].getArray().push_back(room.number);

    cpr::Response r = cpr::Post(
        cpr::Url{std::string(config::convex_site_url()) + "/ingest-classroom-availability"},
        cpr::Header{{"Content-Type", "application/json"}, {"x-webhook-secret", config::classrooms_webhook_secret()}},
        cpr::Body{body.toString()},
        cpr::Timeout{std::chrono::seconds(30)}
    );
    if (r.error || r.status_code != 200) {
        std::cout << "[!] Failed to push room open times to wokenet (status " << r.status_code << "): " << (r.error ? r.error.message : r.text) << "\n";
    } else {
        std::cout << "[*] Pushed open times for " << body["classrooms"].getArray().size() << " rooms to wokenet.\n";
    }
}

// Rooms due for a refresh (oldest data first, never-fetched rooms before all others), and when the next room comes
// due if none are. A room is due REFRESH_AFTER its last fetch or last failed attempt, whichever is later.
std::pair<std::vector<std::string>, time_t> rooms_due(time_t now, const std::map<std::string, time_t>& last_attempt) {
    std::lock_guard lock(mutex);
    std::vector<std::pair<time_t, std::string>> due;
    time_t next_due = now + REFRESH_AFTER;
    for (const auto& room : mit_rooms::rooms) {
        auto fetched = bookings.find(room.number);
        auto attempted = last_attempt.find(room.number);
        time_t fetched_at = fetched == bookings.end() ? 0 : fetched->second.fetched_at;
        time_t last = std::max(fetched_at, attempted == last_attempt.end() ? 0 : attempted->second);
        if (now - last >= REFRESH_AFTER) due.push_back({fetched_at, room.number});
        else next_due = std::min(next_due, last + REFRESH_AFTER);
    }
    std::sort(due.begin(), due.end());
    std::vector<std::string> rooms;
    for (auto& [_, room] : due) rooms.push_back(std::move(room));
    return {rooms, next_due};
}

// Re-fetches the given rooms' bookings, slowly, pushing them to wokenet in batches as it goes. Returns false if it
// stopped early on a Touchstone failure.
bool refresh(sqlite3* database, const std::vector<std::string>& rooms, const std::function<void(const std::string&)>& on_auth_failure,
             bool& alerted, std::map<std::string, time_t>& last_attempt) {
    std::cout << "[?] Refreshing room bookings for " << rooms.size() << " rooms...\n";
    size_t fetched = 0;
    {
        std::lock_guard lock(mutex);
        progress = {true, 0, rooms.size()};
    }

    // One session for the whole refresh keeps the connection alive between requests. It writes the cookie
    // jar when destroyed, at the end of the refresh.
    cpr::Session s = libtouchstone::session(config::cookiefile());
    std::vector<std::string> unpushed;
    for (const auto& room : rooms) {
        std::string auth_error;
        auto result = fetch_room(s, room, auth_error);

        if (!auth_error.empty()) {
            std::cout << "[!] Touchstone auth failed while refreshing room bookings: " << auth_error << "\n";
            if (!alerted) on_auth_failure(auth_error);
            alerted = true;
            {
                std::lock_guard lock(mutex);
                progress.running = false;
            }
            push_open_times(unpushed);
            return false;
        }
        alerted = false;
        // Recorded even on failure, so a room that keeps failing waits REFRESH_AFTER instead of being retried in a loop.
        last_attempt[room] = time(nullptr);

        if (result && database) db::replace_room_bookings(database, room, *result);
        {
            std::lock_guard lock(mutex);
            if (result) bookings[room] = std::move(*result);
            progress.done++;
        }
        if (result) {
            fetched++;
            unpushed.push_back(room);
        }
        if (unpushed.size() >= PUSH_BATCH) {
            push_open_times(unpushed);
            unpushed.clear();
        }
        std::this_thread::sleep_for(REQUEST_INTERVAL);
    }
    push_open_times(unpushed);

    {
        std::lock_guard lock(mutex);
        progress.running = false;
    }
    std::cout << "[*] Refreshed room bookings for " << fetched << "/" << rooms.size() << " rooms.\n";
    return true;
}

} // namespace

void room_schedule::start(std::function<void(const std::string&)> on_auth_failure) {
    // The refresh thread gets its own connection: sharing one would let its transactions swallow other threads' writes.
    sqlite3* database = db::init();
    if (database) {
        auto cached = db::get_room_bookings(database);
        // Drop rooms cached before they were removed from mit_rooms.h.
        std::set<std::string> tracked;
        for (const auto& room : mit_rooms::rooms) tracked.insert(room.number);
        for (auto it = cached.begin(); it != cached.end();) it = tracked.count(it->first) ? std::next(it) : cached.erase(it);
        std::lock_guard lock(mutex);
        bookings = std::move(cached);
    }

    std::thread([database, on_auth_failure = std::move(on_auth_failure)] {
        bool alerted = false; // Whether on_auth_failure was already called for the current failure streak.
        bool hydrant_loaded = false;
        std::map<std::string, time_t> last_attempt; // Last fetch attempt per room, including failed ones.
        for (;;) {
            if (!hydrant_loaded) hydrant_loaded = load_hydrant();

            time_t now = time(nullptr);
            auto [due, next_due] = rooms_due(now, last_attempt);
            std::chrono::seconds nap = std::min<std::chrono::seconds>(MAX_NAP, std::chrono::seconds(next_due - now));
            if (!due.empty()) {
                // Recheck right after a finished refresh, in case more rooms came due while it ran.
                if (refresh(database, due, on_auth_failure, alerted, last_attempt)) continue;
                nap = AUTH_RETRY_INTERVAL;
            }

            std::unique_lock lock(mutex);
            wake.wait_for(lock, nap, [] { return refresh_requested; });
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
        // Coverage always ends at a midnight, so a room covering now covers the rest of today.
        if (now >= covered_until(fetched)) continue;
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

std::vector<room_schedule::room_open_times> room_schedule::open_times(time_t now, const std::vector<std::string>& only) {
    std::lock_guard lock(mutex);

    std::set<std::string> wanted(only.begin(), only.end());
    std::vector<room_open_times> rooms;
    for (const auto& tracked : mit_rooms::rooms) {
        const std::string& room = tracked.number;
        if (!wanted.count(room)) continue;
        auto it = bookings.find(room);
        if (it == bookings.end()) continue;
        const auto& fetched = it->second;
        time_t covered = covered_until(fetched);

        room_open_times entry{room, graph_building(building_of(room)), tracked.capacity, fetched.fetched_at, {}};
        for (int day = 0; day < 2; day++) {
            time_t day_start = utils::at_minute_et(now, 0, day), day_end = utils::at_minute_et(now, 0, day + 1);
            if (day_start >= covered) break; // Not fetched yet, so left without open windows.
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
        for (const auto& room : mit_rooms::rooms) out.insert(graph_building(building_of(room.number)));
        return out;
    }();
    return buildings.count(building);
}
