#include "commands.h"
#include <algorithm>
#include <iostream>
#include <libtouchstone.h>
#include <json.h>
#include "config.h"
#include "utils.h"

// Rooms whose availability begins more than this far in the future are sorted to the bottom.
static constexpr time_t STARTS_LATER_THRESHOLD = 5 * 60;

// Discord message length limit (with some headroom).
static constexpr size_t MAX_MESSAGE_LENGTH = 1900;

std::optional<std::vector<commands::quickroom_entry>> commands::fetch_quickroom(const dpp::slashcommand_t& event, dpp::cluster& bot) {
    std::cout << "[?] Authenticating to Quickroom API...\n";

    cpr::Session s = libtouchstone::session(config::cookiefile());
    cpr::Response r = libtouchstone::authenticate(s,
        "https://classrooms.mit.edu/classrooms/quickroom",
        config::kerb(), config::kerb_password(),
        // block = false is critical, we don't want to be stuck waiting for a 2FA prompt
        {config::cookiefile(), true, false}
    );

    if (r.error) {
        handle_touchstone_auth_failure(event, bot, r.error.message);
        return std::nullopt;
    }

    std::cout << "[?] Quickroom API response (" << r.text.size() << " chars): " << r.text.substr(0, 50) << "...\n";

    auto [status, json] = jt::Json::parse(r.text);
    if (status != jt::Json::success) {
        event.edit_response("Failed to parse JSON from QuickRoom.");
        return std::nullopt;
    }
    if (json.contains("error")) {
        event.edit_response("**QuickRoom request failed.** It may be outside operating hours, try again later.");
        return std::nullopt;
    }

    std::vector<quickroom_entry> rooms;
    for (auto& classroom : json["data"]["classrooms"].getArray()) {
        auto& availabilities = classroom["availabilities"].getArray();
        if (availabilities.empty()) continue;

        rooms.push_back({
            utils::uppercase(classroom["buildingName"].getString()),
            classroom["room"].getString(),
            (int)classroom["capacity"].getLong(),
            utils::parse_iso_utc(availabilities[0]["begin"].getString()),
            utils::parse_iso_utc(availabilities[0]["end"].getString()),
            0
        });
    }
    return rooms;
}

std::string commands::format_quickroom_entries(std::vector<quickroom_entry> rooms, const std::string& header) {
    time_t now = time(nullptr);
    auto starts_later = [now](const quickroom_entry& room) { return room.begin > now + STARTS_LATER_THRESHOLD; };

    // Rooms available right now come first (nearest buildings first), then rooms that only open up later.
    std::sort(rooms.begin(), rooms.end(), [&](const quickroom_entry& a, const quickroom_entry& b) {
        if (starts_later(a) != starts_later(b)) return !starts_later(a);
        if (starts_later(a) && a.begin != b.begin) return a.begin < b.begin;
        if (a.distance != b.distance) return a.distance < b.distance;
        return a.room < b.room;
    });

    std::string response = header;
    bool in_later_section = false;
    size_t shown = 0;

    for (const auto& room : rooms) {
        std::string line;
        if (starts_later(room) && !in_later_section) {
            in_later_section = true;
            line += "**Opening up later:**\n";
        }
        line += "├ **" + room.room + "** (capacity: " + std::to_string(room.capacity) + ") — " +
            utils::format_time_et(room.begin) + " → " + utils::format_time_et(room.end) + "\n";

        if (response.size() + line.size() > MAX_MESSAGE_LENGTH) break;
        response += line;
        shown++;
    }

    if (shown < rooms.size()) {
        response += "├ *...and " + std::to_string(rooms.size() - shown) + " more*\n";
    }

    response += "-# Sourced via [QuickRoom](https://classrooms.mit.edu/classrooms/#/quickroom). May not be comprehensive.\n";
    return response;
}

void commands::quickroom(const dpp::slashcommand_t& event, dpp::cluster& bot) {
    std::string building_query = std::get<std::string>(event.get_parameter("building"));
    event.reply("Looking up available rooms in building **" + building_query + "**...");

    auto rooms = fetch_quickroom(event, bot);
    if (!rooms) return;

    std::string building = utils::uppercase(building_query);
    rooms->erase(
        std::remove_if(rooms->begin(), rooms->end(), [&](const quickroom_entry& room) { return room.building != building; }),
        rooms->end()
    );

    if (rooms->empty()) {
        event.edit_response("No available rooms found on [QuickRoom](https://classrooms.mit.edu/classrooms/#/quickroom) for building **" + building_query + "**.");
    } else {
        event.edit_response(format_quickroom_entries(*rooms, "**Available rooms in building " + building_query + "**:\n"));
    }
}
