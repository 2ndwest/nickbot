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
    std::string where, header;
    if (!nearby.empty()) {
        // List which buildings were searched, nearest first.
        std::vector<std::string> searched;
        for (const auto& [name, _] : nearby) searched.push_back(name);
        std::sort(searched.begin(), searched.end(), [&](const std::string& a, const std::string& b) {
            if (nearby[a] != nearby[b]) return nearby[a] < nearby[b];
            return a < b;
        });
        std::string searched_list;
        for (size_t i = 0; i < searched.size(); i++) searched_list += (i ? ", " : "") + searched[i];

        where = "within **" + std::to_string(radius) + "** building" + (radius == 1 ? "" : "s") + " of **" + building_query + "**";
        header = "**Available rooms near building " + building_query + "** (searched " + searched_list + "):\n";
    } else if (room_schedule::has_rooms_in(building)) {
        // We have rooms here but no idea what's next door, so just search the building itself.
        nearby[building] = 0;
        where = "in **" + building_query + "** (its neighboring buildings aren't mapped yet)";
        header = "**Available rooms in building " + building_query + "** (its neighboring buildings aren't mapped yet):\n";
    } else {
        event.reply("Unknown building **" + building_query + "**. Use a building number as shown on the [MIT map](https://whereis.mit.edu/?q=" + dpp::utility::url_encode(building_query) + ").");
        return;
    }

    auto rooms = find_free_rooms(event, nearby);
    if (!rooms) return;

    if (rooms->empty()) {
        event.reply("No available rooms found " + where + ".");
    } else {
        event.reply(format_free_rooms(std::move(*rooms), header, nearby));
    }
}
