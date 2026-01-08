#include <dpp/dpp.h>
#include <iostream>
#include "commands.h"
#include "config.h"
#include "db.h"
#include "utils.h"

using namespace std;

// TODO: get work requests actually working
// TODO: handle failed touchstone auth gracefully -> DM me with button to rectify

int main() {
    if (!config::token() || !config::kerb() || !config::kerb_password()) {
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
            commands::workrequest(event, database);
        } else if (event.command.get_command_name() == "quickroom") {
            commands::quickroom(event);
        }
    });

    bot.on_message_create([&bot](const dpp::message_create_t& event) {
        if (event.msg.mentions.size() > 0) {
            for (auto& mention : event.msg.mentions) {
                if (mention.first.id == bot.me.id) {
                    event.reply("I'm busy, go away.");
                    break;
                }
            }
        }
    });

    bot.on_ready([&bot](auto event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            cout << "[!] Connected to Discord.\n";

            // Set presence to show last restarted time.
            bot.set_presence(dpp::presence(
                dpp::presence_status::ps_dnd,
                dpp::activity_type::at_custom,
                "last restarted: " + utils::current_time()
            ));

            // Work request command.
            dpp::slashcommand workrequest_cmd(
                "workrequest",
                "File a work request for the room corresponding to this channel.",
                bot.me.id
            );
            workrequest_cmd.add_option(
                dpp::command_option(
                    dpp::co_string,
                    "details",
                    "A detailed description of the requested services.",
                    true
                )
            );
            bot.global_command_create(workrequest_cmd);

            // Quick room command.
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

            // List available commands.
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