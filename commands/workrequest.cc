#include "commands.h"
#include "db.h"
#include <libtouchstone.h>
#include <iostream>
#include "config.h"
#include "utils.h"

// Implementation at the end of the file (it's a bit long).
// Forward declared so we can use it in the command handler.
cpr::Response submit_work_request_to_atlas(
    cpr::Session& session,
    const db::AtlasWorkRequest& request
);

const int MAX_SHORT_DESCRIPTION_LENGTH = 40; // via the Atlas UI, not sure if this is a hard limit or not

void commands::workrequest(const dpp::slashcommand_t& event, dpp::cluster& bot, sqlite3* database) {
    auto it = channel_to_room.find(event.command.channel_id);
    if (it == channel_to_room.end()) return event.reply("**This channel is not associated with a room.** Please use this command in a room channel.");
    std::string room_number = it->second;

    std::string short_description = std::get<std::string>(event.get_parameter("short_description"));
    std::string additional_information = utils::get_or<std::string>(event.get_parameter("additional_information"), "");

    if (short_description.length() > MAX_SHORT_DESCRIPTION_LENGTH) {
        event.reply(
            dpp::message(
                "**Short description is too long** (" +
                std::to_string(short_description.length()) + " characters). Limit: " +
                std::to_string(MAX_SHORT_DESCRIPTION_LENGTH) + " characters. Please try again."
            ).set_flags(dpp::m_ephemeral)
        );
        return;
    }

    event.reply("Submitting work request for room **" + room_number + "**...");

    cpr::Session session = libtouchstone::session(config::cookiefile());
    cpr::Response r = libtouchstone::authenticate(
        session,
        "https://adminappsts.mit.edu/facilities/CreateRequest.action",
        config::kerb(),
        config::kerb_password(),
        // block = false is critical, we don't want to be stuck waiting for a 2FA prompt
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
        // the user. we'll store their request in sqlite and replay it later
        handle_touchstone_auth_failure(event, bot, r.error.message, false);

        if (!db::insert_pending_work_request(database, work_request)) {
            std::cerr << "[!] Failed to record work request: " << sqlite3_errmsg(database) << "\n";
            return event.edit_response("**Failed to record work request.** Please try again later.");
        } else std::cout << "[*] Work request stored in database for room " << room_number << " with short description: " << short_description << "\n";
    }

    event.edit_response(
        std::string(!r.error ? "**Work request submitted:**\n" : "**Work request recorded:**\n") +
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
    std::cout << "[~] Starting repair request submission for room " << request.room_number << "...\n";

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

std::pair<int, int> commands::submit_pending_work_requests_to_atlas(sqlite3* database, cpr::Session& session) {
    auto pending = db::get_pending_work_requests(database);
    if (pending.empty()) {
        std::cout << "[*] No pending work requests to submit.\n";
        return {0, 0};
    }

    std::cout << "[~] Submitting " << pending.size() << " pending work request(s)...\n";

    int submitted = 0;
    for (const auto& req : pending) {
        std::cout << "[~] Submitting work request id=" << req.sqlite_id << " for room " << req.request.room_number << "\n";

        cpr::Response r = submit_work_request_to_atlas(session, req.request);

        if (r.error || r.status_code != 200) {
            std::cerr << "[!] Failed to submit work request id=" << req.sqlite_id << ": " << r.error.message << " (status code: " << r.status_code << ")\n";
            continue;
        }

        std::cout << "[*] Successfully submitted work request id=" << req.sqlite_id << " for room " << req.request.room_number << "\n";
        db::delete_pending_work_request(database, req.sqlite_id);
        submitted++;
    }

    std::cout << "[*] Submitted " << submitted << "/" << pending.size() << " pending work requests.\n";
    return {submitted, static_cast<int>(pending.size())};
}