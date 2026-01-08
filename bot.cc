#include <dpp/dpp.h>
#include <cstdlib>
#include <iostream>
#include <libtouchstone.h>
#include <json.h>
#include "db.h"
#include "utils.h"

using namespace std;

const char* token = getenv("BOT_TOKEN");
const char* kerb = getenv("TOUCHSTONE_USERNAME");
const char* password = getenv("TOUCHSTONE_PASSWORD");

libtouchstone::AuthOptions opts = {"cookies.txt", true};

// TODO: get work requests actually working
// TODO: handle failed touchstone auth gracefully -> DM me with button to rectify

int main() {
    if (!token || !kerb || !password) {
        cerr << "[!] Some required environment variables are not set.\n";
        return 1;
    }

    cout << "[~] Starting bot...\n";

    sqlite3* database = db::init();
    if (!database) {
        cerr << "[!] Failed to initialize sqlite db.\n";
        return 1;
    }

    dpp::cluster bot(token);

    bot.on_slashcommand([&bot, database](const dpp::slashcommand_t& event) {
        cout << "[~] Command invoked: /" << event.command.get_command_name() << "\n";

        if (event.command.get_command_name() == "workrequest") {
            string details = get<string>(event.get_parameter("details"));
            string room_id = to_string(event.command.channel_id);
            int mit_request_id = 0; // TODO: get mit request id from touchstone

            if (db::insert_work_request(database, room_id, details, mit_request_id)) {
                event.reply("Work request submitted successfully! Details: " + details);
            } else event.reply("Failed to submit work request. Please try again later.");
        } else if (event.command.get_command_name() == "quickroom") {
            string room_query = get<string>(event.get_parameter("room"));
            event.reply("Looking up room " + room_query + "...");

            cout << "[?] Authenticating to Quickroom API...\n";

            cpr::Session s = libtouchstone::session(opts.cookie_file);
            cpr::Response r = libtouchstone::authenticate(s,
                "https://classrooms.mit.edu/classrooms/quickroom",
                kerb, password, opts
            );

            cout << "[?] Quickroom API response (" << r.text.size() << " chars): " << r.text.substr(0, 50) << "...\n";

            if (r.error) {
                event.edit_response("Touchstone auth failed: " + r.error.message);
                return;
            }

            auto [status, json] = jt::Json::parse(r.text);
            if (status != jt::Json::success || !json.contains("data") || !json["data"].contains("classrooms")) {
                event.edit_response("Failed to parse JSON.");
                return;
            }

            for (auto& classroom : json["data"]["classrooms"].getArray()) {
                string room_name = classroom["room"].getString();

                if (utils::uppercase(room_name) == utils::uppercase(room_query)) {
                    // Found the room!
                    int capacity = classroom["capacity"].getLong();
                    string building = classroom["buildingName"].getString();
                    string room_num = classroom["roomNumber"].getString();

                    string response = "**" + room_name + "** (capacity: " + to_string(capacity) + ")\n";
                    response += "Building " + building + ", Room " + room_num + "\n\n";
                    response += "**Availability:**\n";

                    for (auto& avail : classroom["availabilities"].getArray()) {
                        string begin = utils::parse_time(avail["begin"].getString());
                        string end = utils::parse_time(avail["end"].getString());
                        response += "• " + begin + " → " + end + "\n";
                    }

                    event.edit_response(response);
                    return;
                }
            }

            event.edit_response("Room **" + room_query + "** not listed on Quickroom.");
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

            // Work request command.
            dpp::slashcommand workrequest_cmd("workrequest", "File a work request for the room corresponding to this channel.", bot.me.id);
            workrequest_cmd.add_option(dpp::command_option(dpp::co_string, "details", "A detailed description of the requested services.", true));
            bot.global_command_create(workrequest_cmd);

            // Quick room command.
            dpp::slashcommand quickroom_cmd("quickroom", "Look up a specific classroom's availability.", bot.me.id);
            quickroom_cmd.add_option(dpp::command_option(dpp::co_string, "room", "Room to look up (e.g. 1-132, 34-302)", true));
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

    bot.start(dpp::st_wait);

    cout << "[~] Bot stopped. Exiting...\n";
    db::close(database);
    return 0;
}