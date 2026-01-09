#include "commands.h"
#include "db.h"

using namespace std;

// Mapping of Discord channel IDs to MIT room numbers. See for more details:
// https://gist.github.com/transmissions11/c411439ff94e3c3768f1dc16eb9e33fb
const std::map<dpp::snowflake, std::string> channel_to_room = {
    {1441113720733302946, "213"},
    {1450258903915958404, "214"}, // multi-stall wood bathroom
    {1450258967484694671, "215"}, // single-stall wood bathroom
    {1441107232715702294, "216"},
    {1441109516492996750, "217"},
    {1441081030789566555, "218"},
    {1425231301882937398, "219"},
    {1376032277347303434, "220"},
    {1371034710909653102, "221"},
    {1371343063724851230, "222"},
    {1442113736859975751, "223"},
    {1450344041890971719, "224"}, // single-stall hayden bathroom
    {1440613858585608303, "226"}, // halo
    {1440613781788037221, "227"}, // kitchen
    {1444170225716166686, "232"},
    {1441081804101652572, "233"},
    {1404927516786954290, "234"},
    {1390856294046634095, "235"},
    {1450258751222186015, "236"}, // multi-stall munroe bathroom
    {1448235766865068146, "237"}, // mennis
    {1441082450347561153, "238"},
    {1441084289696665711, "240"},
    {1371252730349486262, "241"},
    {1390628113947295845, "242"}, // basha
    {1371249067665002558, "243"},
    {1441085418945773701, "245"},
    {1441085501917364225, "247"},
    {1441102231641391238, "249"},
    {1370843161886326914, "250"},
    {1370933397316173854, "251"},
    {1373909810742951986, "252"},
    {1407406325285257408, "253"},
};

void commands::workrequest(const dpp::slashcommand_t& event, sqlite3* database) {
    string details = get<string>(event.get_parameter("details"));
    string room_id = to_string(event.command.channel_id);

    if (db::insert_pending_work_request(database, room_id, details)) {
        event.reply("Work request submitted successfully! Details: " + details);
    } else {
        event.reply("Failed to submit work request. Please try again later.");
    }
}
