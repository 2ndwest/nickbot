#include "commands.h"
#include "db.h"
#include <libtouchstone.h>
#include <iostream>
#include "config.h"

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

const std::string PUTZ_EMAIL = "2ndwest@mit.edu";
const std::string PUTZ_PHONE = "(617)+253-2871"; // via https://officesdirectory.mit.edu/east-campus

cpr::Response send_mit_work_request(
    cpr::Session& session,
    const std::string& room_number,
    const std::string& subject_line = "",
    const std::string& description = "",
    const std::string& additional_location_info = "",
    const std::string& special_instructions = ""
) {
    std::cout << "[~] Starting repair request submission for room " << room_number << "\n";

    std::cout << "[~] Step 1: Initializing request session\n";
    session.SetUrl(cpr::Url{"https://adminappsts.mit.edu/facilities/CreateRequest.action"});
    cpr::Response r = session.Get();

    if (r.status_code != 200) {
        std::cerr << "[!] CreateRequest failed.";
        return r;
    }

    std::cout << "[~] Step 2: Submitting location details\n";
    session.SetHeader(cpr::Header{{"Content-Type", "application/x-www-form-urlencoded"}});
    session.SetUrl(cpr::Url{"https://adminappsts.mit.edu/facilities/SelectRepairDetails.action"});
    session.SetPayload(cpr::Payload{
        {"facilities.request.locationType", "PM_RESIDENCE"},
        {"facilities.request.buildingLeased", "false"},
        {"facilities.request.residenceName", "62"},
        {"facilities.request.residenceRoomNumber", room_number},
        {"facilities.request.residenceLeased", "false"},
        {"facilities.request.parkingType", "PM_PARKING_LOT"},
        {"facilities.request.parkingDescr", ""},
        {"facilities.request.greenspaceDescr", ""},
        {"facilities.request.locationInfo", additional_location_info}
    });
    r = session.Post();

    if (r.status_code != 200) {
        std::cerr << "[!] SelectRepairDetails failed.";
        return r;
    }

    std::cout << "[~] Step 3: Submitting repair request\n";
    session.SetUrl(cpr::Url{"https://adminappsts.mit.edu/facilities/CreateRepairRequest.action"});
    session.SetHeader(cpr::Header{{"Content-Type", "application/x-www-form-urlencoded"}});
    session.SetPayload(cpr::Payload{
        // This is the option for the very generic "Repair" category. There are others,
        // see: https://gist.github.com/transmissions11/ae1a8f24e675b34c728ab0258391bcdb
        {"facilities.request.requestCategory", "H640;HOUS001"},
        {"facilities.request.fumeHood", ""},
        {"facilities.request.subjectLine", subject_line},
        {"facilities.request.description", description},
        {"facilities.request.specialInstructions", special_instructions},
        {"facilities.request.creatorLocation", "62-2"}, // Building 62, 2nd floor.
        {"facilities.request.creatorPhone", PUTZ_PHONE},
        {"facilities.request.creatorEmail", PUTZ_EMAIL},
        {"facilities.request.ccEmail", ""},
        {"facilities.request.requestorName", ""},
        {"facilities.request.requestorKerbId", ""},
        {"facilities.request.requestorLocation", ""},
        {"facilities.request.requestorPhone", ""},
        {"facilities.request.requestorEmail", ""}
    });
    r = session.Post();

    if (r.status_code != 200) std::cerr << "[!] CreateRepairRequest failed.";

    return r;
}

void commands::workrequest(const dpp::slashcommand_t& event, dpp::cluster& bot, sqlite3* database) {
    string details = get<string>(event.get_parameter("details"));

    auto it = channel_to_room.find(event.command.channel_id);
    if (it == channel_to_room.end()) return event.reply("**This channel is not associated with a room.** Please use this command in a room channel.");
    string room_number = it->second;

    event.reply("Submitting work request for room **" + room_number + "**...");

    cpr::Session session = libtouchstone::session(config::cookiefile());
    cpr::Response r = libtouchstone::authenticate(
        session,
        "https://adminappsts.mit.edu/facilities/CreateRequest.action",
        config::kerb(),
        config::kerb_password()
    );

    if (r.error) return handle_touchstone_auth_failure(event, bot, r.error.message);

    r = send_mit_work_request(
        session,
        room_number,
        details.substr(0, 40),
        details
    );
    if (r.error || r.status_code != 200) {
        std::cerr << "[!] Failed to submit work request: " << r.error.message << " (status code: " << r.status_code << ")\n";
        return event.edit_response("**Failed to submit work request.** Please try again later.");
    }

    cout << "[*] Work request submitted successfully for room " << room_number << " with details: " << details << "\n";
    event.edit_response("Work request submitted successfully! **Submitted details:** " + details);

    // if (db::insert_pending_work_request(database, room_number, details)) {
    //     event.reply("Work request submitted successfully! Details: " + details);
    // } else {
    //     event.reply("Failed to submit work request. Please try again later.");
    // }
}