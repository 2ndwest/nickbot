#include "commands.h"
#include <algorithm>
#include "utils.h"

// Rooms whose availability begins more than this far in the future are listed separately, after the rest.
static constexpr time_t STARTS_LATER_THRESHOLD = 5 * 60;

// Discord's message length limit.
static constexpr size_t MAX_MESSAGE_LENGTH = 2000;

// Room for the "...and N more" line.
static constexpr size_t MORE_LINE_LENGTH = 32;

// How long a room stays free, colored like its bar on wokenet: under 30m yellow, under an hour blue, otherwise green.
static std::string dot(time_t free_for) {
    return free_for < 30 * 60 ? "🟡" : free_for < 60 * 60 ? "🔵" : "🟢";
}

std::optional<std::vector<room_schedule::free_room>> commands::find_free_rooms(const dpp::slashcommand_t& event,
                                                                               const std::map<std::string, int>& buildings) {
    auto rooms = room_schedule::find_free_rooms(time(nullptr));
    if (!rooms) {
        event.reply("**Room schedules aren't loaded yet.** The bot may have just restarted, or Touchstone may need reauthentication. Try again in a few minutes.");
        return std::nullopt;
    }
    bool lecture_halls = utils::get_or<bool>(event.get_parameter("lecture_halls"), true);
    rooms->erase(
        std::remove_if(rooms->begin(), rooms->end(), [&](const room_schedule::free_room& room) {
            return !buildings.count(room.building) || (!lecture_halls && room.lecture_hall());
        }),
        rooms->end()
    );
    return rooms;
}

std::string commands::format_free_rooms(std::vector<room_schedule::free_room> rooms, const std::string& header,
                                        const std::map<std::string, int>& distances) {
    time_t now = time(nullptr);
    auto starts_later = [now](const room_schedule::free_room& room) { return room.begin > now + STARTS_LATER_THRESHOLD; };
    auto distance = [&](const std::string& building) {
        auto it = distances.find(building);
        return it == distances.end() ? 0 : it->second;
    };

    // Rooms free now come first, by building (nearest first) and then room number. Rooms opening up later follow,
    // soonest first.
    std::sort(rooms.begin(), rooms.end(), [&](const room_schedule::free_room& a, const room_schedule::free_room& b) {
        if (starts_later(a) != starts_later(b)) return !starts_later(a);
        if (starts_later(a) && a.begin != b.begin) return a.begin < b.begin;
        if (distance(a.building) != distance(b.building)) return distance(a.building) < distance(b.building);
        if (a.building != b.building) return utils::natural_less(a.building, b.building);
        return utils::natural_less(a.room, b.room);
    });

    std::string footer = "-# Sourced from [MIT room bookings](https://classrooms.mit.edu/classrooms/) and [Hydrant](https://hydrant.mit.edu), "
        "refreshed every " + std::to_string(room_schedule::REFRESH_AFTER / (60 * 60)) + " hours. Not every room is unlocked.\n";
    if (size_t stale = room_schedule::stale_rooms(now)) {
        footer += "-# ⚠️ " + (stale == 1 ? std::string("1 room hasn't") : std::to_string(stale) + " rooms haven't") +
            " refreshed in over " + std::to_string(room_schedule::STALE_AFTER / (60 * 60)) + " hours.\n";
    }

    // Headings only help when rooms could be from more than one building.
    bool by_building = distances.size() > 1;
    std::string response = header;
    size_t shown = 0;

    for (const auto& room : rooms) {
        const room_schedule::free_room* prev = shown ? &rooms[shown - 1] : nullptr;
        std::string name = "**" + room.room + "**" + (room.lecture_hall() ? " `LH`" : "");
        std::string line;
        if (starts_later(room)) {
            if (!prev || !starts_later(*prev)) line += "**Opening up later:**\n";
            line += dot(room.end - room.begin) + " " + name + " — " + utils::format_until_et(room.begin, now) + " → " + utils::format_until_et(room.end, now) + "\n";
        } else {
            if (by_building && (!prev || prev->building != room.building)) line += "**Building " + room.building + "**\n";
            line += dot(room.end - now) + " " + name + " — until " + utils::format_until_et(room.end, now) + " · " + utils::format_duration(room.end - now) + "\n";
        }

        if (response.size() + line.size() + footer.size() + MORE_LINE_LENGTH > MAX_MESSAGE_LENGTH) break;
        response += line;
        shown++;
    }

    if (shown < rooms.size()) {
        response += "*...and " + std::to_string(rooms.size() - shown) + " more*\n";
    }
    return response + footer;
}

void commands::quickroom(const dpp::slashcommand_t& event) {
    std::string building_query = std::get<std::string>(event.get_parameter("building"));
    std::string building = room_schedule::graph_building(utils::uppercase(building_query));

    auto rooms = find_free_rooms(event, {{building, 0}});
    if (!rooms) return;

    if (rooms->empty()) {
        event.reply("No available rooms found in building **" + building_query + "**.");
    } else {
        event.reply(format_free_rooms(std::move(*rooms), "**Available rooms in building " + building_query + "**:\n"));
    }
}
