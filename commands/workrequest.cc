#include "commands.h"
#include "db.h"

using namespace std;

void commands::workrequest(const dpp::slashcommand_t& event, sqlite3* database) {
    string details = get<string>(event.get_parameter("details"));
    string room_id = to_string(event.command.channel_id);
    int donlan_id = 0; // TODO: get mit request id from touchstone

    if (db::insert_work_request(database, room_id, details, donlan_id)) {
        event.reply("Work request submitted successfully! Details: " + details);
    } else {
        event.reply("Failed to submit work request. Please try again later.");
    }
}
