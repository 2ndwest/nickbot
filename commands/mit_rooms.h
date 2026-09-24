#pragma once
// Every room /quickroom and /quicknear track availability for. Seeded from every room a class met in
// during Fall 2026 (Hydrant) plus the registrar classroom list QuickRoom draws from; add new rooms by hand
// (MIT space accounting's room list at floorplans.mit.edu/reports/room_bldg_rmlist.html has every room's use).
// Rooms that turn out not to be usable are deleted from the list and noted at the bottom of this file.

#include <optional>
#include <string>
#include <vector>

namespace mit_rooms {

struct room {
    std::string number;          // e.g. "W41-1119"
    std::optional<int> capacity; // Seats, or std::nullopt if unknown.
    bool capacity_estimated = false;
};

// Tags a capacity as estimated from floor area rather than counted by the registrar.
inline constexpr bool estimate = true;

// Capacities come from the registrar classroom list (classrooms.mit.edu, Sep 23 2026). Rooms without a registrar
// count are estimated from MIT space accounting's floor area (floorplans.mit.edu, Sep 22 2026) at 20 sq ft per seat,
// which matches the registrar rooms' median (19.8) and sorts ~90% of them onto the right side of 50 seats.
// clang-format off
inline const std::vector<room> rooms = {
    {"1-131", 39, estimate}, {"1-132", 24}, {"1-134", 24}, {"1-135", 35}, {"1-136", 16}, {"1-150", 36},
    {"1-190", 134}, {"1-242", 34}, {"1-246", 30}, {"1-273", 25}, {"1-277", 25}, {"1-371", 25}, {"1-375", 25},
    {"1-379", 25}, {"1-390", 69}, {"2-103", 16}, {"2-105", 59}, {"2-131", 34}, {"2-132", 32}, {"2-135", 32},
    {"2-136", 26}, {"2-139", 32}, {"2-142", 26}, {"2-143", 32}, {"2-146", 24}, {"2-147", 32}, {"2-151", 18},
    {"2-190", 134}, {"3-037A", std::nullopt}, {"3-037C", std::nullopt}, {"3-062B", std::nullopt}, {"3-133", 60},
    {"3-270", 119}, {"3-333", 57}, {"3-370", 58}, {"3-442", 50}, {"4-144", 16}, {"4-145", 40}, {"4-146", 16},
    {"4-148", 6}, {"4-149", 50}, {"4-152", 20}, {"4-153", 48}, {"4-158", 20}, {"4-159", 40}, {"4-162", 20},
    {"4-163", 81}, {"4-231", 57}, {"4-237", 80}, {"4-249", 45}, {"4-251", 12}, {"4-253", 22}, {"4-257", 35},
    {"4-261", 40}, {"4-265", 40}, {"4-270", 115}, {"4-364", 30}, {"4-370", 115}, {"4-402", 38, estimate},
    {"5-134", 40}, {"5-216", 18}, {"5-217", 42}, {"5-231", 18}, {"5-232", 18}, {"5-233", 30}, {"5-234", 45},
    {"6-120", 143}, {"8-119", 22}, {"8-205", 30}, {"9-152", 51, estimate}, {"9-354", 64}, {"9-451", 23, estimate},
    {"10-250", 425}, {"13-1143", 25}, {"13-3101", 26}, {"13-4101", 28}, {"13-5101", 32}, {"14E-310", 36},
    {"14N-112", 14}, {"14N-221", 18, estimate}, {"14N-225", 19, estimate}, {"14N-313", 19, estimate}, {"14N-325", 18},
    {"14W-111", 90, estimate}, {"16-128", 46, estimate}, {"16-136", 28, estimate}, {"16-160", 56},
    {"16-220", 49, estimate}, {"16-644", 24, estimate}, {"16-654", 24, estimate}, {"16-668", 24, estimate},
    {"16-672", 24, estimate}, {"16-676", 23, estimate}, {"24-112", 24}, {"24-115", 42}, {"24-121", 42},
    {"24-307", 36}, {"24-308", 13, estimate}, {"24-310", 24, estimate}, {"24-317", 15, estimate},
    {"24-319", 15, estimate}, {"24-321", 14, estimate}, {"24-323", 15, estimate}, {"24-611A", 13, estimate},
    {"24-618", 22, estimate}, {"24-619", 26, estimate}, {"24-621", 13, estimate}, {"26-100", 551}, {"26-139", 22},
    {"26-142", 24}, {"26-152", 117}, {"26-168", 41}, {"26-204", 32}, {"26-210", 32}, {"26-314", 32}, {"26-322", 32},
    {"26-328", 32}, {"32-082", 117}, {"32-123", 318}, {"32-124", 60}, {"32-141", 90}, {"32-144", 60}, {"32-155", 90},
    {"32-D461", 43, estimate}, {"32-D769", 12, estimate}, {"32-D831", 18, estimate}, {"32-D918", 10, estimate},
    {"33-319", 36}, {"33-418", 32}, {"33-419", 48}, {"33-422", 20}, {"34-101", 311}, {"34-301", 35}, {"34-302", 35},
    {"34-303", 35}, {"34-304", 35}, {"35-225", 90}, {"35-308", 30}, {"35-310", 30}, {"36-112", 40}, {"36-144", 30},
    {"36-153", 40}, {"36-155", 40}, {"36-156", 50}, {"36-372", 24}, {"37-212", 74}, {"38-166", 24},
    {"38-530", 202, estimate}, {"45-102", 60}, {"45-230", 250}, {"46-3002", 100, estimate}, {"46-4062", 22, estimate},
    {"46-5056", 22, estimate}, {"46-5165", 41, estimate}, {"46-5305", 25, estimate}, {"46-5313", 24, estimate},
    {"48-308", 24}, {"48-316", 48}, {"50-340", 266}, {"54-100", 298}, {"54-527", 22, estimate},
    {"54-819", 34, estimate}, {"54-820", 23, estimate}, {"54-823", 33, estimate}, {"54-827", 16, estimate},
    {"54-911", 44, estimate}, {"54-1623", 22, estimate}, {"55-109", 24, estimate}, {"55-110", 25, estimate},
    {"56-114", 63}, {"56-154", 56}, {"56-162", 27}, {"56-167", 22}, {"56-169", 20}, {"56-180", 27}, {"56-191", 20},
    {"56-614", 51, estimate}, {"66-110", 96}, {"66-144", 55}, {"66-148", 16}, {"66-154", 32}, {"66-156", 22},
    {"66-160", 36}, {"66-168", 55}, {"68-121", 35, estimate}, {"E14-493", 18, estimate}, {"E14-633", 79, estimate},
    {"E15-001", 119, estimate}, {"E15-054", 41, estimate}, {"E15-070", 117, estimate}, {"E15-207", 36, estimate},
    {"E15-235", 21, estimate}, {"E15-335", 22, estimate}, {"E15-341", 49, estimate}, {"E15-359", 26, estimate},
    {"E17-136", 39, estimate}, {"E17-517", 32, estimate}, {"E18-304", 34, estimate}, {"E18-411", 5, estimate},
    {"E18-676", 63, estimate}, {"E18-676C", 27, estimate}, {"E25-111", 150}, {"E25-117", 50},
    {"E25-141", 16, estimate}, {"E25-406", 10, estimate}, {"E25-605", 30, estimate}, {"E25-650", 2, estimate},
    {"E38-579", 35, estimate}, {"E40-160", 84, estimate}, {"E51-057", 46}, {"E51-061", 24}, {"E51-063", 35},
    {"E51-085", 50}, {"E51-115", 169, estimate}, {"E51-145", 67}, {"E51-149", 67}, {"E51-151", 54},
    {"E51-165", 19, estimate}, {"E51-285", 21, estimate}, {"E51-315", 86}, {"E51-325", 86}, {"E51-335", 76},
    {"E51-345", 142}, {"E51-361", 40}, {"E51-372", 50}, {"E51-376", 54}, {"E51-385", 20}, {"E51-390", 20},
    {"E51-393", 14}, {"E51-395", 70}, {"E52-164", 78}, {"E52-314", 14, estimate}, {"E52-324", 44, estimate},
    {"E52-432", 38, estimate}, {"E52-532", 23, estimate}, {"E53-354", 29, estimate}, {"E53-438", 17, estimate},
    {"E53-485", 15, estimate}, {"E62-164", 91, estimate}, {"E62-176", 122, estimate}, {"E62-221", 36},
    {"E62-223", 80}, {"E62-233", 108}, {"E62-250", 57}, {"E62-262", 85}, {"E62-276", 105}, {"E62-346", 28, estimate},
    {"E62-350", 50, estimate}, {"E62-446", 28, estimate}, {"E62-450", 50, estimate}, {"E62-550", 48, estimate},
    {"E62-587", 28, estimate}, {"E62-650", 48, estimate}, {"E62-687", 28, estimate}, {"E66-218", 95, estimate},
    {"E66-231", 84, estimate}, {"E66-235", 100, estimate}, {"N51-310", 47, estimate}, {"N51-350", 37, estimate},
    {"N52-342B", 36, estimate}, {"NE45-202A", 1, estimate}, {"NE46-1025", std::nullopt}, {"NW14-1112", 63, estimate},
    {"W18-1102", 170, estimate}, {"W18-1202", 74, estimate}, {"W18-1311", 25, estimate}, {"W18-2310", 17, estimate},
    {"W18-4305", 21, estimate}, {"W18-4311", 28, estimate}, {"W35-199", 29, estimate}, {"W41-1101", 21, estimate},
    {"W41-1119", 14, estimate}, {"W41-1216", 157, estimate}, {"W41-1219", 42, estimate}, {"W41-1302", 4, estimate},
    {"W41-1303", 15, estimate}, {"W41-1305", 29, estimate}, {"W41-1306", 91, estimate}, {"W41-1307", 29, estimate},
    {"W41-1406", 14, estimate}, {"W41-1408", 12, estimate}, {"W41-2101", 22, estimate}, {"W41-2302", 25, estimate},
    {"W41-2319", 25, estimate}, {"W41-3101", 24, estimate}, {"W41-3102", 54, estimate}, {"W41-4101", 24, estimate},
    {"W41-4214", 16, estimate}, {"W41-4507", 13, estimate}, {"W41-5101", 24, estimate}, {"W41-5510", 12, estimate},
    {"W41-5520", 12, estimate}, {"W59-051", 52, estimate}, {"W59-073", 44, estimate}, {"W59-147", 28, estimate},
    {"W59-149", 27, estimate}, {"W97-160", 86, estimate}, {"W97-162", 76, estimate}, {"W97-261", 46, estimate},
    {"W97-269", 60, estimate},
};
// clang-format on

// Removed rooms: a class meets in each of these, but they aren't publicly usable.
// Labs, per MIT facilities room types (via mitmapit.org).
// TEACHING LAB
//   1-004, 1-307, 3-038, 4-361, 5-026, 8-107, 34-501
//   35-125, 46-1015, 46-1024, 48-109, 56-322, 68-074
// RESEARCH LAB
//   4-131B, 4-409
// LABORATORY SVC
//   1-050, 38-545
// LAB SUPPORT SHOP
//   4-006
//
// Offices and other private space, per MIT facilities room types (via mitmapit.org).
// OFFICE
//   3-001, 76-261D
// OFFICE SERVICE
//   2-361, 10-063, 35-434
// RECEPTION
//   32-044
// PVT CIRCULATION
//   12-5170B
// CONFERENCE ROOM (mostly departmental, even when a class meets there)
//   3-149, 6-104, 18-278, 26-414, 31-270, 33-218, 37-252
//   46-3037, 46-3189, 46-3310, 48-311, 54-209, 54-517, 55-108
//   66-319, 66-360, 68-156, 68-181, 76-258, 76-559, 76-659
// STUDIO
//   4-013, 50-201
// EXHIBITION FACIL (Compton Gallery)
//   10-150
//
// W41, per MIT space accounting's building room list (data as of Sep 22, 2026).
// STUDIO
//   W41-3219, W41-3509, W41-4319, W41-4419, W41-5219
//   W41-1401, W41-1402 (listed as CLASSROOMs, but in person they're part of one giant open studio)
// KITCHENETTE
//   W41-4519
// ALTER/CONVERSION (under renovation)
//   W41-3511
//
// W41 rooms that were never on the list, per the same room list. Everything else in W41 not
// mentioned here is an office, lab, support space, or circulation.
// STUDIO
//   W41-1502, W41-1506, W41-1517, W41-3118, W41-4509, W41-5118, W41-5305, W41-5403
// CONFERENCE ROOM (no classes or bookings)
//   W41-3403, W41-5214 (locked, checked in person)
//   W41-3505, W41-3527 (not checked, presumably locked too)
// LOUNGE, MULTI-PURPOSE RM (no classes or bookings)
//   W41-1106, W41-3406, W41-5106, W41-5303
// EXHIBITION FACIL
//   W41-1113, W41-3304
// CLASSROOM SVC (support rooms attached to classrooms)
//   W41-1207, W41-2318, W41-3111, W41-3203, W41-3508, W41-4404, W41-4419A, W41-4420

} // namespace mit_rooms
