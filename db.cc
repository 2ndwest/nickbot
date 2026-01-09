#include "db.h"
#include <iostream>

using namespace std;

sqlite3* db::init() {
    sqlite3* database;
    int rc = sqlite3_open("workrequests.db", &database);

    if (rc) {
        cerr << "[!] Can't open database: " << sqlite3_errmsg(database) << "\n";
        return nullptr;
    }

    char* err_msg = nullptr;
    rc = sqlite3_exec(database, R"(
        CREATE TABLE IF NOT EXISTS pending_work_requests (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            room_id TEXT NOT NULL,
            details TEXT NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        cerr << "[!] sqlite: SQL error: " << err_msg << "\n";
        sqlite3_free(err_msg);
        sqlite3_close(database);
        return nullptr;
    }

    cout << "[*] sqlite: db initialized successfully.\n";
    return database;
}

bool db::insert_pending_work_request(sqlite3* database, const string& room_id, const string& details) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(
        database,
        "INSERT INTO pending_work_requests (room_id, details) VALUES (?, ?);",
        -1,
        &stmt,
        nullptr
    );
    if (rc != SQLITE_OK) {
        cerr << "[!] sqlite: failed to prepare statement: " << sqlite3_errmsg(database) << "\n";
        return false;
    }

    sqlite3_bind_text(stmt, 1, room_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, details.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        cerr << "[!] sqlite: failed to insert pending work request: " << sqlite3_errmsg(database) << "\n";
        return false;
    }

    cout << "[*] sqlite: created pending work request #" << sqlite3_last_insert_rowid(database) << " for room " << room_id << "\n";
    return true;
}
