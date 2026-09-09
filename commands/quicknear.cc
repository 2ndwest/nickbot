#include "commands.h"
#include <algorithm>
#include "mit_buildings.h"
#include "utils.h"

void commands::quicknear(const dpp::slashcommand_t& event, dpp::cluster& bot) {
    std::string building_query = std::get<std::string>(event.get_parameter("building"));
    std::string building = utils::uppercase(building_query);
    int radius = (int)utils::get_or<int64_t>(event.get_parameter("radius"), 2);

    if (!mit_buildings::neighbors.count(building)) {
        event.reply("Unknown building **" + building_query + "**. Use a building number as shown on the [MIT map](https://whereis.mit.edu/?q=" + dpp::utility::url_encode(building_query) + ").");
        return;
    }

    event.reply("Looking up available rooms within **" + std::to_string(radius) + "** building" + (radius == 1 ? "" : "s") + " of **" + building_query + "**...");

    // Map of building -> graph distance from the queried building, for everything within the radius.
    std::map<std::string, int> nearby = mit_buildings::neighbors_within(building, radius);

    auto rooms = fetch_quickroom(event, bot);
    if (!rooms) return;

    rooms->erase(
        std::remove_if(rooms->begin(), rooms->end(), [&](const quickroom_entry& room) { return !nearby.count(room.building); }),
        rooms->end()
    );
    for (auto& room : *rooms) room.distance = nearby[room.building];

    if (rooms->empty()) {
        event.edit_response("No available rooms found on [QuickRoom](https://classrooms.mit.edu/classrooms/#/quickroom) within **" + std::to_string(radius) + "** building" + (radius == 1 ? "" : "s") + " of **" + building_query + "**.");
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

    event.edit_response(format_quickroom_entries(*rooms,
        "**Available rooms near building " + building_query + "** (searched " + searched_list + "):\n"));
}
