#include "commands.h"
#include <iostream>
#include <libtouchstone.h>
#include <json.h>
#include "config.h"
#include "db.h"
#include "utils.h"

using namespace std;

static libtouchstone::AuthOptions libtouchstone_opts = {"cookies.txt", true};

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

void commands::quickroom(const dpp::slashcommand_t& event) {
    string building_query = get<string>(event.get_parameter("building"));
    event.reply("Looking up available rooms in building **" + building_query + "**...");

    cout << "[?] Authenticating to Quickroom API...\n";

    cpr::Session s = libtouchstone::session(libtouchstone_opts.cookie_file);
    cpr::Response r = libtouchstone::authenticate(s,
        "https://classrooms.mit.edu/classrooms/quickroom",
        config::kerb(), config::kerb_password(),
        libtouchstone_opts
    );

    cout << "[?] Quickroom API response (" << r.text.size() << " chars): " << r.text.substr(0, 50) << "...\n";

    if (r.error) {
        event.edit_response("Touchstone auth failed: " + r.error.message);
        return;
    }

    auto [status, json] = jt::Json::parse(r.text);
    if (status != jt::Json::success) {
        event.edit_response("Failed to parse JSON from QuickRoom.");
        return;
    }

    string response = "**Available rooms in building " + building_query + "**:\n";

    int found = 0;
    for (auto& classroom : json["data"]["classrooms"].getArray()) {
        string building_num = classroom["buildingName"].getString();

        if (building_num == building_query) {
            found++;
            int capacity = classroom["capacity"].getLong();
            string room_name = classroom["room"].getString();
            string begin = utils::parse_time(classroom["availabilities"].getArray()[0]["begin"].getString());
            string end = utils::parse_time(classroom["availabilities"].getArray()[0]["end"].getString());
            response += "├ **" + room_name + "** (capacity: " + to_string(capacity) + ") — " + begin + " → " + end + "\n";
        }
    }

    response += "-# Sourced via [QuickRoom](https://classrooms.mit.edu/classrooms/#/quickroom). May not be comprehensive.\n";

    if (found == 0) {
        event.edit_response("No available rooms found on QuickRoom for building **" + building_query + "**.");
    } else event.edit_response(response);
}
