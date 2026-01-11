#pragma once

#include <dpp/dpp.h>
#include <cpr/cpr.h>
#include <sqlite3.h>
#include <iostream>
#include <string>
#include "config.h"

namespace commands {

void workrequest(const dpp::slashcommand_t& event, dpp::cluster& bot, sqlite3* database);
void quickroom(const dpp::slashcommand_t& event, dpp::cluster& bot);

// Submits all pending work requests stored in the database.
// Returns the number of successfully submitted requests.
int submit_pending_work_requests(sqlite3* database, cpr::Session& session);

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

}
