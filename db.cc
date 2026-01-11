#include "db.h"
#include <iostream>

using namespace std;

sqlite3* db::init() {
    sqlite3* database;
    int rc = sqlite3_open("workrequests.db", &database);

    if (rc) {
        cerr << "[!] sqlite: couldn't open database: " << sqlite3_errmsg(database) << "\n";
        return nullptr;
    }

    char* err_msg = nullptr;
    rc = sqlite3_exec(database, R"(
        CREATE TABLE IF NOT EXISTS pending_work_requests (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            room_number TEXT NOT NULL,
            short_description TEXT NOT NULL,
            additional_information TEXT NOT NULL DEFAULT '',
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

bool db::insert_pending_work_request(sqlite3* database, const AtlasWorkRequest& request) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(
        database,
        "INSERT INTO pending_work_requests (room_number, short_description, additional_information) VALUES (?, ?, ?);",
        -1,
        &stmt,
        nullptr
    );
    if (rc != SQLITE_OK) {
        cerr << "[!] sqlite: failed to prepare statement: " << sqlite3_errmsg(database) << "\n";
        return false;
    }

    sqlite3_bind_text(stmt, 1, request.room_number.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, request.short_description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, request.additional_information.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        cerr << "[!] sqlite: failed to insert pending work request: " << sqlite3_errmsg(database) << "\n";
        return false;
    }

    cout << "[*] sqlite: created pending work request id=" << sqlite3_last_insert_rowid(database) << " for room " << request.room_number << "\n";
    return true;
}

std::vector<db::PendingWorkRequest> db::get_pending_work_requests(sqlite3* database) {
    std::vector<PendingWorkRequest> requests;
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(
        database,
        "SELECT id, room_number, short_description, additional_information FROM pending_work_requests ORDER BY created_at ASC;",
        -1,
        &stmt,
        nullptr
    );
    if (rc != SQLITE_OK) {
        cerr << "[!] sqlite: failed to prepare select statement: " << sqlite3_errmsg(database) << "\n";
        return requests;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* room = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* short_desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* additional_info = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        requests.push_back({
            {
                room ? room : "<UNKNOWN ROOM NUMBER>",
                short_desc ? short_desc : "<UNKNOWN SHORT DESCRIPTION>",
                additional_info ? additional_info : "<UNKNOWN ADDITIONAL INFORMATION>",
            },
            sqlite3_column_int(stmt, 0),
        });
    }

    sqlite3_finalize(stmt);
    cout << "[~] sqlite: retrieved " << requests.size() << " pending work request(s).\n";
    return requests;
}

bool db::delete_pending_work_request(sqlite3* database, int id) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(
        database,
        "DELETE FROM pending_work_requests WHERE id = ?;",
        -1,
        &stmt,
        nullptr
    );
    if (rc != SQLITE_OK) {
        cerr << "[!] sqlite: failed to prepare delete statement: " << sqlite3_errmsg(database) << "\n";
        return false;
    }

    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        cerr << "[!] sqlite: failed to delete pending work request id=" << id << ": " << sqlite3_errmsg(database) << "\n";
        return false;
    }

    cout << "[*] sqlite: deleted pending work request id=" << id << "\n";
    return true;
}
