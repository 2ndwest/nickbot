#include "commands.h"
#include <algorithm>
#include "mit_buildings.h"
#include "utils.h"

void commands::quicknear(const dpp::slashcommand_t& event) {
    std::string building_query = std::get<std::string>(event.get_parameter("building"));
    std::string building = room_schedule::graph_building(utils::uppercase(building_query));
    int radius = (int)utils::get_or<int64_t>(event.get_parameter("radius"), 2);

    // Map of building -> graph distance from the queried building, for everything within the radius.
    std::map<std::string, int> nearby = mit_buildings::neighbors_within(building, radius);
    bool on_graph = !nearby.empty();
    if (!on_graph) {
        if (!room_schedule::has_rooms_in(building)) {
            event.reply("Unknown building **" + building_query + "**. Use a building number as shown on the [MIT map](https://whereis.mit.edu/?q=" + dpp::utility::url_encode(building_query) + ").");
            return;
        }
        // We have rooms here but no idea what's next door, so just search the building itself.
        nearby[building] = 0;
    }

    auto rooms = find_free_rooms(event);
    if (!rooms) return;

    rooms->erase(
        std::remove_if(rooms->begin(), rooms->end(), [&](const room_schedule::free_room& room) { return !nearby.count(room.building); }),
        rooms->end()
    );

    std::string where = on_graph
        ? "within **" + std::to_string(radius) + "** building" + (radius == 1 ? "" : "s") + " of **" + building_query + "**"
        : "in **" + building_query + "** (its neighboring buildings aren't mapped yet)";
    if (rooms->empty()) {
        event.reply("No available rooms found " + where + ".");
        return;
    }

    // List which buildings were searched, nearest first.
    std::vector<std::string> searched;
    for (const auto& [name, _] : nearby) searched.push_back(name);
    std::sort(searched.begin(), searched.end(), [&](const std::string& a, const std::string& b) {
        if (nearby[a] != nearby[b]) return nearby[a] < nearby[b];
        return a < b;
    });
    std::string searched_list;
    for (size_t i = 0; i < searched.size(); i++) searched_list += (i ? ", " : "") + searched[i];

    std::string header = on_graph
        ? "**Available rooms near building " + building_query + "** (searched " + searched_list + "):\n"
        : "**Available rooms in building " + building_query + "** (its neighboring buildings aren't mapped yet):\n";
    event.reply(format_free_rooms(*rooms, header, nearby));
}
