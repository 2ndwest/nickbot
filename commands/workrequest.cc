#include "commands.h"
#include "db.h"
#include <libtouchstone.h>
#include <iostream>
#include "config.h"

using namespace std;

// Implementation at the end of the file (it's a bit long).
// Forward declared so we can use it in the command handler.
cpr::Response submit_work_request_to_atlas(
    cpr::Session& session,
    const db::AtlasWorkRequest& request
);

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

const int MAX_SHORT_DESCRIPTION_LENGTH = 40; // via the Atlas UI, not sure if this is a hard limit or not.

void commands::workrequest(const dpp::slashcommand_t& event, dpp::cluster& bot, sqlite3* database) {
    string short_description = get<string>(event.get_parameter("short_description"));

    auto additional_information_raw = event.get_parameter("additional_information");
    string additional_information = holds_alternative<string>(additional_information_raw)
        ? get<string>(additional_information_raw)
        : "";

    auto it = channel_to_room.find(event.command.channel_id);
    if (it == channel_to_room.end()) return event.reply("**This channel is not associated with a room.** Please use this command in a room channel.");
    string room_number = it->second;

    event.reply("Submitting work request for room **" + room_number + "**...");

    cpr::Session session = libtouchstone::session(config::cookiefile());
    cpr::Response r = libtouchstone::authenticate(
        session,
        "https://adminappsts.mit.edu/facilities/CreateRequest.action",
        config::kerb(),
        config::kerb_password(),
        // block = false is critical, we don't want to be stuck waiting for a 2FA prompt.
        {config::cookiefile(), true, false}
    );

    db::AtlasWorkRequest work_request{room_number, short_description, additional_information};

    if (!r.error) {
        // actually submit work request to Atlas
        r = submit_work_request_to_atlas(session, work_request);

        if (r.error || r.status_code != 200) {
            std::cerr << "[!] Failed to submit work request: " << r.error.message << " (status code: " << r.status_code << ")\n";
            return event.edit_response("**Failed to submit work request.** Please try again later.");
        } else std::cout << "[*] Work request submitted successfully to Atlas for room " << room_number << " with short description: " << short_description << "\n";
    } else {
        // dm admin to reauthenticate. alert_user=false so we don't scare
        // the user. we'll store their request in sqlite and replay it later.
        handle_touchstone_auth_failure(event, bot, r.error.message, false);

        if (!db::insert_pending_work_request(database, work_request)) {
            std::cerr << "[!] Failed to record work request: " << sqlite3_errmsg(database) << "\n";
            return event.edit_response("**Failed to record work request.** Please try again later.");
        } else std::cout << "[*] Work request stored in database for room " << room_number << " with short description: " << short_description << "\n";
    }

    event.edit_response(
        string(!r.error ? "**Work request submitted:**\n" : "**Work request recorded:**\n") +
        "├ **Location:** 62-" + room_number + "\n"
        "├ **Short Description:** " + short_description + "\n" +
        "├ **Additional Information:** " + (additional_information.empty() ? "N/A" : additional_information) + "\n" +
        (!r.error
            ? "-# Submitted directly via [Atlas](https://adminappsts.mit.edu/facilities/CreateRequest.action).\n"
            : "-# Saved and will be uploaded to [Atlas](https://adminappsts.mit.edu/facilities/CreateRequest.action) shortly.\n")
    );
}

cpr::Response submit_work_request_to_atlas(
    cpr::Session& session,
    const db::AtlasWorkRequest& request
) {
    std::cout << "[~] Starting repair request submission for room " << request.room_number << "\n";

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
        {"facilities.request.residenceRoomNumber", request.room_number},
        {"facilities.request.residenceLeased", "false"},
        {"facilities.request.parkingType", "PM_PARKING_LOT"},
        {"facilities.request.parkingDescr", ""},
        {"facilities.request.greenspaceDescr", ""},
        {"facilities.request.locationInfo", ""} // additional location info
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
        // Truncate the short description to the max length, just in case the caller didn't enforce this.
        {"facilities.request.subjectLine", request.short_description.substr(0, MAX_SHORT_DESCRIPTION_LENGTH)},
        {"facilities.request.description", request.additional_information},
        {"facilities.request.specialInstructions", ""}, // don't know what you'd use this for, ngl
        {"facilities.request.creatorLocation", "62-2"}, // building 62, 2nd floor
        {"facilities.request.creatorPhone", "2ndwest@mit.edu"},
        {"facilities.request.creatorEmail", "(617)+253-2871"}, // via https://officesdirectory.mit.edu/east-campus
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

int commands::submit_pending_work_requests_to_atlas(sqlite3* database, cpr::Session& session) {
    auto pending = db::get_pending_work_requests(database);
    if (pending.empty()) {
        cout << "[*] No pending work requests to submit.\n";
        return 0;
    }

    cout << "[~] Submitting " << pending.size() << " pending work request(s)...\n";

    int submitted = 0;
    for (const auto& req : pending) {
        cout << "[~] Submitting work request id=" << req.sqlite_id << " for room " << req.request.room_number << "\n";

        cpr::Response r = submit_work_request_to_atlas(session, req.request);

        if (r.error || r.status_code != 200) {
            cerr << "[!] Failed to submit work request id=" << req.sqlite_id << ": " << r.error.message << " (status code: " << r.status_code << ")\n";
            continue;
        }

        cout << "[*] Successfully submitted work request id=" << req.sqlite_id << " for room " << req.request.room_number << "\n";
        db::delete_pending_work_request(database, req.sqlite_id);
        submitted++;
    }

    cout << "[*] Submitted " << submitted << "/" << pending.size() << " pending work request(s).\n";
    return submitted;
}