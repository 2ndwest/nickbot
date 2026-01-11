#include <dpp/dpp.h>
#include <iostream>
#include <libtouchstone.h>
#include "commands/commands.h"
#include "config.h"
#include "db.h"
#include "utils.h"

using namespace std;

int main() {
    if (!config::token() ||
        !config::kerb() ||
        !config::kerb_password() ||
        !config::admin_user_id() ||
        !config::cookiefile()) {
        cerr << "[!] Some required environment variables are not set.\n";
        return 1;
    }

    cout << "[~] Starting bot...\n";

    sqlite3* database = db::init();
    if (!database) {
        cerr << "[!] Failed to initialize sqlite db.\n";
        return 1;
    }

    dpp::cluster bot(config::token());

    bot.on_slashcommand([&bot, database](const dpp::slashcommand_t& event) {
        cout << "[~] Command invoked: /" << event.command.get_command_name() << "\n";

        if (event.command.get_command_name() == "workrequest") {
            commands::workrequest(event, bot, database);
        } else if (event.command.get_command_name() == "quickroom") {
            commands::quickroom(event, bot);
        }
    });

    bot.on_button_click([database](const dpp::button_click_t& event) {
        if (event.custom_id == "touchstone_reauth") {
            cout << "[~] Reauth button clicked, authenticating to Atlas...\n";
            event.reply("Sending a 2FA prompt to your device...");

            cpr::Session s = libtouchstone::session(config::cookiefile());
            cpr::Response r = libtouchstone::authenticate(s,
                "https://atlas.mit.edu",
                config::kerb(), config::kerb_password(),
                // block = true is critical, otherwise we can't do the 2FA prompt
                {config::cookiefile(), true, true}
            );

            if (r.error) {
                cout << "[!] Touchstone reauth failed: " << r.error.message << "\n";
                event.edit_response("Reauth failed: " + r.error.message);
                return;
            }

            cout << "[*] Touchstone reauth succeeded. Handling any unfinished business...\n";

            // submit any pending work requests that were stalled due to touchstone auth previously
            auto [submitted_reqs, initial_pending_reqs] = commands::submit_pending_work_requests_to_atlas(database, s);

            event.edit_response(
                "Successfully re-authenticated to Touchstone!" +
                (initial_pending_reqs > 0
                    ? "\n├ Submitted **" + to_string(submitted_reqs) + "/" +
                      to_string(initial_pending_reqs) +
                      "** pending work requests to [Atlas](https://adminappsts.mit.edu/facilities/CreateRequest.action)."
                    : "")
            );
        }
    });

    bot.on_ready([&bot](auto event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            cout << "[!] Connected to Discord.\n";

            // set presence to show last restarted time
            bot.set_presence(dpp::presence(
                dpp::presence_status::ps_dnd,
                dpp::activity_type::at_custom,
                "last restarted: " + utils::current_time()
            ));

            // workrequest command
            dpp::slashcommand workrequest_cmd(
                "workrequest",
                "File a work request for the room corresponding to this channel.",
                bot.me.id
            );
            workrequest_cmd.add_option(
                dpp::command_option(
                    dpp::co_string,
                    "short_description",
                    "A short description of the issue (max 40 characters).",
                    true
                )
            );
            workrequest_cmd.add_option(
                dpp::command_option(
                    dpp::co_string,
                    "additional_information",
                    "Additional details about the requested services.",
                    false
                )
            );
            bot.global_command_create(workrequest_cmd);

            // quickroom command
            dpp::slashcommand quickroom_cmd(
                "quickroom",
                "List currently available rooms in a building.",
                bot.me.id
            );
            quickroom_cmd.add_option(
                dpp::command_option(
                    dpp::co_string,
                    "building",
                    "Building number (e.g. 1, 34, 66).",
                    true
                )
            );
            bot.global_command_create(quickroom_cmd);

            // list available commands
            bot.global_commands_get([](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    cerr << "[!] Error getting commands: " << callback.get_error().message << "\n";
                } else {
                    auto commands = get<dpp::slashcommand_map>(callback.value);
                    cout << "[?] Available commands globally:\n";
                    for (const auto& [id, cmd] : commands) {
                        cout << "- " << cmd.name << " (snowflake: " << id << ")\n";
                    }
                }
            });
        }
    });

    bot.start(dpp::st_wait); // never returns, even on sigterm
    return 0;
}