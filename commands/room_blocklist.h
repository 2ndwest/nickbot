#pragma once
// Hand-maintained: rooms that look free on paper but aren't actually usable (locked, departmental,
// not really a room, ...). Blocked rooms are never fetched or shown by /quickroom and /quicknear.

#include <set>
#include <string>

namespace room_blocklist {

inline const std::set<std::string> rooms = {
};

} // namespace room_blocklist
