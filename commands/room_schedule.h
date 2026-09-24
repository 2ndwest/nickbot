#pragma once

#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Tracks when every room in mit_rooms.h is booked, so /quickroom and /quicknear can list free rooms
// of any size in any building (QuickRoom itself only covers a small, curated set of rooms).
//
// Two sources are combined:
//   - classrooms.mit.edu roomBookings: authoritative, re-fetched for each room once it's REFRESH_AFTER old.
//   - Hydrant's class schedule: loaded once on startup. Some classes (mostly in departmental rooms) are
//     missing from roomBookings, so a room counts as busy whenever either source says it is.
namespace room_schedule {

// Rooms with at least this many seats count as lecture halls, the same line wokenet draws.
inline constexpr int LECTURE_HALL_SEATS = 60;

// A room's bookings are re-fetched once they're this old. Fetch times are kept in SQLite, so restarting the bot
// doesn't trigger a refresh, and a refresh interrupted by a restart picks up where it left off.
inline constexpr time_t REFRESH_AFTER = 6 * 60 * 60;

// A room not refreshed in this long missed a refresh, and is flagged as stale (wokenet uses the same line).
inline constexpr time_t STALE_AFTER = REFRESH_AFTER + 2 * 60 * 60;

// A window during which a room has no bookings.
struct free_room {
    std::string room;             // Room number, e.g. "W41-1119".
    std::string building;         // Building on the building graph, e.g. "W41" (or "14" for room "14N-112").
    std::optional<int> capacity;  // Seats, or std::nullopt if unknown (see mit_rooms.h).
    time_t begin;                 // Start of the free window (unix timestamp).
    time_t end;                   // End of the free window (unix timestamp). May be a later day; bookings are only
                                  // fetched through tomorrow, so it's midnight after tomorrow at the latest.

    bool lecture_hall() const { return capacity.value_or(0) >= LECTURE_HALL_SEATS; }
};

// Loads cached bookings from the database, then starts a background thread that loads Hydrant and
// re-fetches each room's bookings once they're REFRESH_AFTER old. on_auth_failure is called with the error
// message when Touchstone auth fails (once per failure streak, not on every retry).
void start(std::function<void(const std::string&)> on_auth_failure);

// Wakes the background thread to refresh any stale rooms now, e.g. after a successful reauth.
void refresh_now();

// Progress of the current refresh of stale rooms' bookings.
struct sweep_status {
    bool running; // A refresh is in progress.
    size_t done;  // Rooms the current refresh has gotten through.
    size_t total; // Rooms the current refresh covers.
};
sweep_status get_sweep_status();

// Every room free now or opening up soon, or std::nullopt if no room's fetched bookings cover today
// (the bot just started with an empty cache, or fetching has been failing for a while).
std::optional<std::vector<free_room>> find_free_rooms(time_t now);

// How many tracked rooms haven't been refreshed in over STALE_AFTER (including any never fetched).
size_t stale_rooms(time_t now);

// A room's open (unbooked) windows over today and tomorrow, as pushed to wokenet.
struct room_open_times {
    std::string room;     // Room number, e.g. "W41-1119".
    std::string building; // Building on the building graph, e.g. "W41".
    std::optional<int> capacity; // Seats, or std::nullopt if unknown (see mit_rooms.h).
    time_t fetched_at;    // When the room's bookings were last fetched.
    std::vector<std::pair<time_t, time_t>> open; // (start, end) unix timestamps, sorted.
};

// Open windows for the given rooms, from midnight today (ET) to midnight after tomorrow, but only over days their
// fetched bookings cover.
std::vector<room_open_times> open_times(time_t now, const std::vector<std::string>& rooms);

// Maps a room number's building ("14N", "W41") onto a building in mit_buildings.h where possible
// ("14", "W41"). Buildings the graph doesn't know are returned unchanged.
std::string graph_building(const std::string& building);

// Whether any tracked room is in `building` (as returned by graph_building).
bool has_rooms_in(const std::string& building);

} // namespace room_schedule
