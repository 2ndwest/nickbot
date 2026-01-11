#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>

namespace db {
    sqlite3* init();

    struct PendingWorkRequest {
        std::string room_number;
        std::string details;
        int sqlite_id; // id in the sqlite database.
    };

    bool insert_pending_work_request(sqlite3* db, const PendingWorkRequest& request);
    std::vector<PendingWorkRequest> get_pending_work_requests(sqlite3* db);
    bool delete_pending_work_request(sqlite3* db, int id);
}
