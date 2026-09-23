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
//   - classrooms.mit.edu roomBookings: authoritative, re-fetched for every room about once an hour.
//   - Hydrant's class schedule: loaded once on startup. Some classes (e.g. recitations in departmental
//     rooms) are missing from roomBookings, so a Hydrant class inside a free window is flagged as a warning.
namespace room_schedule {

// A window during which a room has no bookings.
struct free_room {
    std::string room;      // Room number, e.g. "W41-1119".
    std::string building;  // Building on the building graph, e.g. "W41" (or "14" for room "14N-112").
    time_t begin;          // Start of the free window (unix timestamp).
    time_t end;            // End of the free window (unix timestamp).
    bool until_end_of_day; // No more bookings today; `end` is midnight.
    // Hydrant classes meeting during the window that roomBookings doesn't know about, e.g. "11.220 at 01:00 PM".
    std::vector<std::string> hydrant_conflicts;
};

// Loads cached bookings from the database, then starts a background thread that loads Hydrant and
// re-fetches every room's bookings about once an hour. on_auth_failure is called with the error
// message when Touchstone auth fails (once per failure streak, not on every sweep).
void start(std::function<void(const std::string&)> on_auth_failure);

// Wakes the background thread to start a new sweep now, e.g. after a successful reauth.
void refresh_now();

// Every room free now or opening up soon, or std::nullopt if no recent bookings are available
// (the bot just started with an empty cache, or fetching has been failing for a while).
std::optional<std::vector<free_room>> find_free_rooms(time_t now);

// Maps a room number's building ("14N", "W41") onto a building in mit_buildings.h where possible
// ("14", "W41"). Buildings the graph doesn't know are returned unchanged.
std::string graph_building(const std::string& building);

// Whether any tracked room is in `building` (as returned by graph_building).
bool has_rooms_in(const std::string& building);

} // namespace room_schedule
