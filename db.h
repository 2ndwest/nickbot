#pragma once

#include <sqlite3.h>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace db {
    sqlite3* init();

    // Struct representing a work request to be submitted to Atlas.
    struct AtlasWorkRequest {
        std::string room_number;
        std::string short_description;
        std::string additional_information;
    };

    // Struct representing a pending work request stored in the database.
    struct PendingWorkRequest {
        AtlasWorkRequest request;
        int sqlite_id; // id in the sqlite database.
    };

    bool insert_pending_work_request(sqlite3* db, const AtlasWorkRequest& request);
    std::vector<PendingWorkRequest> get_pending_work_requests(sqlite3* db);
    bool delete_pending_work_request(sqlite3* db, int id);

    // A room's bookings as last fetched from classrooms.mit.edu.
    struct RoomBookings {
        time_t fetched_at;
        std::vector<std::pair<time_t, time_t>> bookings; // (start, end) unix timestamps.
    };

    // Replaces everything stored for `room` with the given bookings.
    bool replace_room_bookings(sqlite3* db, const std::string& room, const RoomBookings& bookings);
    std::map<std::string, RoomBookings> get_room_bookings(sqlite3* db);
}
