#pragma once
// Hand-maintained: rooms that look free on paper but aren't actually usable (locked, departmental,
// not really a room, ...). Blocked rooms are never fetched or shown by /quickroom and /quicknear.

#include <set>
#include <string>

namespace room_blocklist {

inline const std::set<std::string> rooms = {
    // Labs, per MIT facilities room types (via mitmapit.org).
    // TEACHING LAB
    "1-004", "1-307", "3-038", "4-361", "5-026", "8-107", "34-501",
    "35-125", "46-1015", "46-1024", "48-109", "56-322", "68-074",
    // RESEARCH LAB
    "4-131B", "4-409",
    // LABORATORY SVC
    "1-050", "38-545",
    // LAB SUPPORT SHOP
    "4-006",

    // Offices and other private space, per MIT facilities room types (via mitmapit.org).
    // OFFICE
    "3-001", "76-261D",
    // OFFICE SERVICE
    "2-361", "10-063", "35-434",
    // RECEPTION
    "32-044", "54-911",
    // PVT CIRCULATION
    "12-5170B",
    // CONFERENCE ROOM (mostly departmental, even when a class meets there)
    "3-149", "6-104", "18-278", "26-414", "31-270", "33-218", "37-252",
    "46-3037", "46-3189", "46-3310", "48-311", "54-209", "54-517", "55-108",
    "66-319", "66-360", "68-156", "68-181", "76-258", "76-559", "76-659",
    // STUDIO
    "4-013", "50-201",
    // EXHIBITION FACIL (Compton Gallery)
    "10-150",
};

} // namespace room_blocklist
