#include <dpp/dpp.h>
#include <cstdlib>
#include <iostream>
#include <libtouchstone.h>

using namespace std;

const char* token = getenv("BOT_TOKEN");
const char* kerb = getenv("TOUCHSTONE_USERNAME");
const char* password = getenv("TOUCHSTONE_PASSWORD");

libtouchstone::AuthOptions opts = {"cookies.txt", true};

int main() {
    if (!token || !kerb || !password) {
        cerr << "[!] Some required environment variables are not set.\n";
        return 1;
    }

    cout << "[~] Starting bot...\n";

    dpp::cluster bot(token);

    bot.on_slashcommand([&bot](const dpp::slashcommand_t& event) {
        cout << "[~] Command invoked: /" << event.command.get_command_name() << "\n";

        if (event.command.get_command_name() == "workrequest") {
            string details = get<string>(event.get_parameter("details"));
            event.reply("Work request submitted with details: " + details);
        } else if (event.command.get_command_name() == "quickroom") {
            event.reply("Fetching available classrooms...");

            cpr::Session s = libtouchstone::session(opts.cookie_file);
            cpr::Response r = libtouchstone::authenticate(s, "https://classrooms.mit.edu/classrooms/quickroom", kerb, password, opts);

            if (r.error) event.edit_response("Authentication failed: " + r.error.message);
            else event.edit_response("Available classrooms: " + r.text);
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
            dpp::slashcommand quickroom_cmd("quickroom", "View classrooms available on campus right now.", bot.me.id);
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
    return 0;
}