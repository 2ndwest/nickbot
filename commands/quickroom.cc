#include "commands.h"
#include <iostream>
#include <libtouchstone.h>
#include <json.h>
#include "config.h"
#include "utils.h"

void commands::quickroom(const dpp::slashcommand_t& event, dpp::cluster& bot) {
    std::string building_query = std::get<std::string>(event.get_parameter("building"));
    event.reply("Looking up available rooms in building **" + building_query + "**...");

    std::cout << "[?] Authenticating to Quickroom API...\n";

    cpr::Session s = libtouchstone::session(config::cookiefile());
    cpr::Response r = libtouchstone::authenticate(s,
        "https://classrooms.mit.edu/classrooms/quickroom",
        config::kerb(), config::kerb_password(),
        // block = false is critical, we don't want to be stuck waiting for a 2FA prompt
        {config::cookiefile(), true, false}
    );

    if (r.error) return handle_touchstone_auth_failure(event, bot, r.error.message);

    std::cout << "[?] Quickroom API response (" << r.text.size() << " chars): " << r.text.substr(0, 50) << "...\n";

    auto [status, json] = jt::Json::parse(r.text);
    if (status != jt::Json::success) return event.edit_response("Failed to parse JSON from QuickRoom.");

    std::string response = "**Available rooms in building " + building_query + "**:\n";

    int found = 0;
    for (auto& classroom : json["data"]["classrooms"].getArray()) {
        std::string building_num = classroom["buildingName"].getString();

        if (utils::uppercase(building_num) == utils::uppercase(building_query)) {
            found++;
            int capacity = classroom["capacity"].getLong();
            std::string room_name = classroom["room"].getString();
            std::string begin = utils::parse_time(classroom["availabilities"].getArray()[0]["begin"].getString());
            std::string end = utils::parse_time(classroom["availabilities"].getArray()[0]["end"].getString());
            response += "├ **" + room_name + "** (capacity: " + std::to_string(capacity) + ") — " + begin + " → " + end + "\n";
        }
    }

    response += "-# Sourced via [QuickRoom](https://classrooms.mit.edu/classrooms/#/quickroom). May not be comprehensive.\n";

    if (found == 0) {
        event.edit_response("No available rooms found on [QuickRoom](https://classrooms.mit.edu/classrooms/#/quickroom) for building **" + building_query + "**.");
    } else event.edit_response(response);
}
