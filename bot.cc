#include <dpp/dpp.h>
#include <iostream>
#include <libtouchstone.h>
#include "commands/commands.h"
#include "config.h"
#include "db.h"
#include "utils.h"

int main() {
    if (!config::token() ||
        !config::kerb() ||
        !config::kerb_password() ||
        !config::admin_user_id() ||
        !config::cookiefile()) {
        std::cerr << "[!] Some required environment variables are not set.\n";
        return 1;
    }

    std::cout << "[~] Starting bot...\n";

    sqlite3* database = db::init();
    if (!database) {
        std::cerr << "[!] Failed to initialize sqlite db.\n";
        return 1;
    }

    dpp::cluster bot(config::token());

    room_schedule::start([&bot](const std::string& error_message) {
        commands::alert_admin_touchstone_failure(bot, error_message, "the hourly room bookings refresh");
    });

    bot.on_slashcommand([&bot, database](const dpp::slashcommand_t& event) {
        std::cout << "[~] Command invoked: /" << event.command.get_command_name() << "\n";

        if (event.command.get_command_name() == "workrequest") {
            commands::workrequest(event, bot, database);
        } else if (event.command.get_command_name() == "quickroom") {
            commands::quickroom(event);
        } else if (event.command.get_command_name() == "quicknear") {
            commands::quicknear(event);
        }
    });

    bot.on_button_click([database](const dpp::button_click_t& event) {
        if (event.custom_id == "touchstone_reauth") {
            std::cout << "[~] Reauth button clicked, authenticating to Atlas...\n";
            event.reply("Sending a 2FA prompt to your device...");

            cpr::Session s = libtouchstone::session(config::cookiefile());
            cpr::Response r = libtouchstone::authenticate(s,
                "https://atlas.mit.edu",
                config::kerb(), config::kerb_password(),
                // block = true is critical, otherwise we can't do the 2FA prompt
                {config::cookiefile(), true, true}
            );

            if (r.error) {
                std::cout << "[!] Touchstone reauth failed: " << r.error.message << "\n";
                event.edit_response("Reauth failed: " + r.error.message);
                return;
            }

            std::cout << "[*] Touchstone reauth succeeded. Handling any unfinished business...\n";

            // submit any pending work requests that were stalled due to touchstone auth previously
            auto [submitted_reqs, initial_pending_reqs] = commands::submit_pending_work_requests_to_atlas(database, s);

            // resume refreshing room bookings, which stops at the first auth failure. curl only writes the
            // cookie jar when a session is destroyed, so flush it first or the refresh would read stale cookies
            curl_easy_setopt(s.GetCurlHolder()->handle, CURLOPT_COOKIELIST, "FLUSH");
            room_schedule::refresh_now();

            event.edit_response(
                "Successfully re-authenticated to Touchstone!" +
                (initial_pending_reqs > 0
                    ? "\n├ Submitted **" + std::to_string(submitted_reqs) + "/" +
                      std::to_string(initial_pending_reqs) +
                      "** pending work requests to [Atlas](https://adminappsts.mit.edu/facilities/CreateRequest.action)."
                    : "")
            );
        }
    });

    bot.on_ready([&bot](auto event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            std::cout << "[!] Connected to Discord.\n";

            // set presence to show last restarted time and room bookings sweep progress. polled on a timer
            // rather than pushed per room, since discord rate limits presence updates
            auto update_presence = [&bot, restarted = "last restarted: " + utils::current_time(), last = std::make_shared<std::string>()] {
                auto sweep = room_schedule::get_sweep_status();
                std::string text = restarted + " · rooms " + std::to_string(sweep.done) + "/" + std::to_string(sweep.total);
                if (sweep.auth_failed) text += " (needs reauth)";
                else if (!sweep.running && sweep.finished_at) text += " (synced " + utils::format_time_et(sweep.finished_at) + ")";

                if (text == *last) return;
                *last = text;
                bot.set_presence(dpp::presence(dpp::presence_status::ps_online, dpp::activity_type::at_custom, text));
            };
            update_presence();
            bot.start_timer([update_presence](dpp::timer) { update_presence(); }, 30);

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

            // quicknear command
            dpp::slashcommand quicknear_cmd(
                "quicknear",
                "List currently available rooms in a building and its neighboring buildings.",
                bot.me.id
            );
            quicknear_cmd.add_option(
                dpp::command_option(
                    dpp::co_string,
                    "building",
                    "Building number to search around (e.g. 1, 34, 66).",
                    true
                )
            );
            quicknear_cmd.add_option(
                dpp::command_option(
                    dpp::co_integer,
                    "radius",
                    "How many buildings away to search (default 2).",
                    false
                ).set_min_value(1).set_max_value(5)
            );
            bot.global_command_create(quicknear_cmd);

            // list available commands
            bot.global_commands_get([](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    std::cerr << "[!] Error getting commands: " << callback.get_error().message << "\n";
                } else {
                    auto commands = std::get<dpp::slashcommand_map>(callback.value);
                    std::cout << "[?] Available commands globally:\n";
                    for (const auto& [id, cmd] : commands) {
                        std::cout << "- " << cmd.name << " (snowflake: " << id << ")\n";
                    }
                }
            });
        }
    });

    bot.start(dpp::st_wait); // never returns, even on sigterm
    return 0;
}