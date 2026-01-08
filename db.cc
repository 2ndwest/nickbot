#include "db.h"
#include <iostream>

using namespace std;

namespace db {
    sqlite3* init() {
        sqlite3* database;
        int rc = sqlite3_open("workrequests.db", &database);

        if (rc) {
            cerr << "[!] Can't open database: " << sqlite3_errmsg(database) << "\n";
            return nullptr;
        }

        char* err_msg = nullptr;
        rc = sqlite3_exec(database, R"(
            CREATE TABLE IF NOT EXISTS work_requests (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                room_id TEXT NOT NULL,
                details TEXT NOT NULL,
                mit_request_id INTEGER,
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

    bool insert_work_request(sqlite3 *database, const string &room_id, const string &details, int mit_request_id) {
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(
            database,
            "INSERT INTO work_requests (room_id, details, mit_request_id) VALUES (?, ?, ?);",
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
        sqlite3_bind_int(stmt, 3, mit_request_id);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            cerr << "[!] sqlite: failed to insert work request: " << sqlite3_errmsg(database) << "\n";
            return false;
        }

        cout << "[*] sqlite: created work request #" << sqlite3_last_insert_rowid(database) << " for room " << room_id << "\n";
        return true;
    }

    void close(sqlite3* database) {
        sqlite3_close(database);
        cout << "[~] sqlite: db closed.\n";
    }
}
