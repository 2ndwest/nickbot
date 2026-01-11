#pragma once

#include <sqlite3.h>
#include <string>
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
}
