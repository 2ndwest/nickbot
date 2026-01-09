#pragma once

#include <dpp/dpp.h>
#include <sqlite3.h>
#include <iostream>
#include <string>
#include "config.h"

namespace commands {

void workrequest(const dpp::slashcommand_t& event, sqlite3* database);
void quickroom(const dpp::slashcommand_t& event, dpp::cluster& bot);

// Handles Touchstone authentication failures by notifying the user and DMing the admin.
inline void handle_touchstone_auth_failure(
    const dpp::slashcommand_t& event,
    dpp::cluster& bot,
    const std::string& error_message
) {
    event.edit_response("Touchstone authentication failed. <@" + std::string(config::admin_user_id()) + "> has been notified to reauthenticate.");

    dpp::message dm("Touchstone auth failed with error: " + error_message + ". Please reauthenticate to Touchstone.");
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
