#include "commands.h"
#include <iostream>
#include <libtouchstone.h>
#include <json.h>
#include "config.h"
#include "utils.h"

using namespace std;

void commands::quickroom(const dpp::slashcommand_t& event, dpp::cluster& bot) {
    string building_query = get<string>(event.get_parameter("building"));
    event.reply("Looking up available rooms in building **" + building_query + "**...");

    cout << "[?] Authenticating to Quickroom API...\n";

    cpr::Session s = libtouchstone::session(config::cookiefile());
    cpr::Response r = libtouchstone::authenticate(s,
        "https://classrooms.mit.edu/classrooms/quickroom",
        config::kerb(), config::kerb_password(),
        {config::cookiefile(), true, false}
    );

    if (r.error) {
        event.edit_response("Touchstone authentication failed. <@" + string(config::admin_user_id()) + "> has been notified to reauthenticate.");

        // DM admin with reauth button
        dpp::message dm("Touchstone auth failed with error: " + r.error.message +   ". Please reauthenticate to Touchstone.");
        dm.add_component(
            dpp::component().add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Re-authenticate to Touchstone")
                    .set_style(dpp::cos_success)
                    .set_id("touchstone_reauth")
            )
        );
        bot.direct_message_create(dpp::snowflake(config::admin_user_id()), dm);
        cout << "[!] Touchstone auth failed, notified admin to reauthenticate.\n";
        return;
    }

    cout << "[?] Quickroom API response (" << r.text.size() << " chars): " << r.text.substr(0, 50) << "...\n";

    auto [status, json] = jt::Json::parse(r.text);
    if (status != jt::Json::success) {
        event.edit_response("Failed to parse JSON from QuickRoom.");
        return;
    }

    string response = "**Available rooms in building " + building_query + "**:\n";

    int found = 0;
    for (auto& classroom : json["data"]["classrooms"].getArray()) {
        string building_num = classroom["buildingName"].getString();

        if (utils::uppercase(building_num) == utils::uppercase(building_query)) {
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
        event.edit_response("No available rooms found on [QuickRoom](https://classrooms.mit.edu/classrooms/#/quickroom) for building **" + building_query + "**.");
    } else event.edit_response(response);
}
