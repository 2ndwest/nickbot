#pragma once

#include <sqlite3.h>
#include <string>

namespace db {
    sqlite3* init();
    bool insert_pending_work_request(sqlite3* db, const std::string& room_id, const std::string& details);
}
