#include "commands.h"
#include <algorithm>
#include "utils.h"

// Rooms whose availability begins more than this far in the future are sorted to the bottom.
static constexpr time_t STARTS_LATER_THRESHOLD = 5 * 60;

// Discord message length limit (with some headroom).
static constexpr size_t MAX_MESSAGE_LENGTH = 1900;

std::optional<std::vector<room_schedule::free_room>> commands::find_free_rooms(const dpp::slashcommand_t& event) {
    auto rooms = room_schedule::find_free_rooms(time(nullptr));
    if (!rooms) {
        event.reply("**Room schedules aren't loaded yet.** The bot may have just restarted, or Touchstone may need reauthentication. Try again in a few minutes.");
    }
    return rooms;
}

std::string commands::format_free_rooms(std::vector<room_schedule::free_room> rooms, const std::string& header,
                                        const std::map<std::string, int>& distances) {
    time_t now = time(nullptr);
    auto starts_later = [now](const room_schedule::free_room& room) { return room.begin > now + STARTS_LATER_THRESHOLD; };
    auto distance = [&](const room_schedule::free_room& room) {
        auto it = distances.find(room.building);
        return it == distances.end() ? 0 : it->second;
    };

    // Rooms available right now come first (nearest buildings first), then rooms that only open up later.
    std::sort(rooms.begin(), rooms.end(), [&](const room_schedule::free_room& a, const room_schedule::free_room& b) {
        if (starts_later(a) != starts_later(b)) return !starts_later(a);
        if (starts_later(a) && a.begin != b.begin) return a.begin < b.begin;
        if (distance(a) != distance(b)) return distance(a) < distance(b);
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
        line += "├ **" + room.room + "** — " + utils::format_time_et(room.begin) + " → " +
            (room.until_end_of_day ? "end of day" : utils::format_time_et(room.end)) + "\n";

        if (response.size() + line.size() > MAX_MESSAGE_LENGTH) break;
        response += line;
        shown++;
    }

    if (shown < rooms.size()) {
        response += "├ *...and " + std::to_string(rooms.size() - shown) + " more*\n";
    }

    response += "-# Sourced from [MIT room bookings](https://classrooms.mit.edu/classrooms/) and [Hydrant](https://hydrant.mit.edu), refreshed hourly. Not every room is unlocked.\n";
    return response;
}

void commands::quickroom(const dpp::slashcommand_t& event) {
    std::string building_query = std::get<std::string>(event.get_parameter("building"));
    std::string building = room_schedule::graph_building(utils::uppercase(building_query));

    auto rooms = find_free_rooms(event);
    if (!rooms) return;

    rooms->erase(
        std::remove_if(rooms->begin(), rooms->end(), [&](const room_schedule::free_room& room) { return room.building != building; }),
        rooms->end()
    );

    if (rooms->empty()) {
        event.reply("No available rooms found in building **" + building_query + "**.");
    } else {
        event.reply(format_free_rooms(*rooms, "**Available rooms in building " + building_query + "**:\n"));
    }
}
