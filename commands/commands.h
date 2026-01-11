#pragma once

#include <dpp/dpp.h>
#include <cpr/cpr.h>
#include <sqlite3.h>
#include <iostream>
#include <string>
#include <map>
#include "config.h"

namespace commands {

void workrequest(const dpp::slashcommand_t& event, dpp::cluster& bot, sqlite3* database);
void quickroom(const dpp::slashcommand_t& event, dpp::cluster& bot);

// Submits all pending work requests stored in the database. Deletes pending requests from db on success.
// Returns a pair of (number of successfully submitted requests, initial number of pending requests).
std::pair<int, int> submit_pending_work_requests_to_atlas(sqlite3* database, cpr::Session& session);

// Handles Touchstone authentication failures DMing the admin to reauthenticate,
// and (optionally) notifying the user that Touchstone authentication failed.
inline void handle_touchstone_auth_failure(
    const dpp::slashcommand_t& event,
    dpp::cluster& bot,
    const std::string& error_message,
    bool alert_user = true // Whether to notify the user authentication failed.
) {
    if (alert_user) event.edit_response("**Touchstone authentication failed.** <@" + std::string(config::admin_user_id()) + "> has been notified to reauthenticate. Try again later.");

    dpp::message dm(
        "**Touchstone authentication failed:**\n"
        "├ Message: `" + error_message + "`\n"
        "├ Triggered by: <@" + std::to_string(event.command.usr.id) + "> "
        "([jump to message](https://discord.com/channels/" +
        std::to_string(event.command.guild_id) + "/" +
        std::to_string(event.command.channel_id) + "/" +
        std::to_string(event.command.id) + "))"
    );
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
    std::cout << "[!] Touchstone auth failed, notified admin to reauthenticate.\n";
}

// Mapping of Discord channel IDs to MIT room numbers. See for more details:
// https://gist.github.com/transmissions11/c411439ff94e3c3768f1dc16eb9e33fb
inline const std::map<dpp::snowflake, std::string> channel_to_room = {
    {1441113720733302946, "213"},
    {1450258903915958404, "214"}, // multi-stall wood bathroom
    {1450258967484694671, "215"}, // single-stall wood bathroom
    {1441107232715702294, "216"},
    {1441109516492996750, "217"},
    {1441081030789566555, "218"},
    {1425231301882937398, "219"},
    {1376032277347303434, "220"},
    {1371034710909653102, "221"},
    {1371343063724851230, "222"},
    {1442113736859975751, "223"},
    {1450344041890971719, "224"}, // single-stall hayden bathroom
    {1440613858585608303, "226"}, // halo
    {1440613781788037221, "227"}, // kitchen
    {1444170225716166686, "232"},
    {1441081804101652572, "233"},
    {1404927516786954290, "234"},
    {1390856294046634095, "235"},
    {1450258751222186015, "236"}, // multi-stall munroe bathroom
    {1448235766865068146, "237"}, // mennis
    {1441082450347561153, "238"},
    {1441084289696665711, "240"},
    {1371252730349486262, "241"},
    {1390628113947295845, "242"}, // basha
    {1371249067665002558, "243"},
    {1441085418945773701, "245"},
    {1441085501917364225, "247"},
    {1441102231641391238, "249"},
    {1370843161886326914, "250"},
    {1370933397316173854, "251"},
    {1373909810742951986, "252"},
    {1407406325285257408, "253"},
};

}
