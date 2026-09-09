#!/usr/bin/env python3
"""
Generates commands/mit_buildings.h: an adjacency graph of MIT buildings used by /quicknear.

Two buildings are neighbors when their footprints touch or are within a few meters of each
other (i.e. they're physically connected or right next door), so walking a "radius" of N
in the graph roughly corresponds to walking through N buildings.

Data sources:
  - Building numbers + coordinates: MIT map (https://whereis.mit.edu)
  - Building footprints: OpenStreetMap via the Overpass API

Usage: pip install shapely && python3 scripts/gen_building_graph.py > commands/mit_buildings.h
"""
import json, math, sys, urllib.request, urllib.parse, concurrent.futures
from shapely.geometry import Polygon, Point
from shapely.ops import unary_union

# Max gap (meters) between two footprints for them to count as neighbors.
NEIGHBOR_THRESHOLD_M = 25.0

OVERPASS = "https://overpass.private.coffee/api/interpreter"
OVERPASS_QUERY = """
[out:json][timeout:90];
(
  way["building"](42.350,-71.115,42.370,-71.078);
  relation["building"](42.350,-71.115,42.370,-71.078);
);
out geom;
"""

# Buildings on the MIT map that shouldn't be treated as "buildings" for room-finding purposes,
# e.g. parking structures or things physically outside campus that the map still numbers.
EXCLUDE = set()

def log(*a): print(*a, file=sys.stderr)

def whereis_lookup(bldgnum):
    url = "https://whereis.mit.edu/search?type=building&q=" + urllib.parse.quote(bldgnum) + "&output=json"
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            return json.loads(r.read().decode())
    except Exception as e:
        log("whereis error", bldgnum, e)
        return []

def fetch_whereis_buildings():
    """Brute-force the MIT map for every plausible building number. Returns {bldgnum: (lat, lon)}."""
    cands = set()
    for prefix in ["", "E", "W", "N", "NE", "NW", "WW"]:
        for n in range(1, 140):
            cands.add(f"{prefix}{n}")
            for suffix in "ABCDEFG":
                cands.add(f"{prefix}{n}{suffix}")
    cands |= {"W85ABC", "W85DE", "W85FG", "W85HJK"}
    out = {}
    with concurrent.futures.ThreadPoolExecutor(12) as ex:
        for results in ex.map(whereis_lookup, sorted(cands)):
            for b in results or []:
                if b.get("bldgnum") and b.get("lat_wgs84"):
                    out[b["bldgnum"].upper()] = (b["lat_wgs84"], b["long_wgs84"])
    return out

def fetch_osm_footprints():
    req = urllib.request.Request(OVERPASS, data=OVERPASS_QUERY.encode())
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read().decode())["elements"]

# Equirectangular projection to meters around campus; plenty accurate at this scale.
LAT0, LON0 = 42.36, -71.09
def proj(lon, lat):
    return ((lon - LON0) * 111320 * math.cos(math.radians(LAT0)), (lat - LAT0) * 110574)

def footprint(el):
    rings = []
    if el["type"] == "way":
        rings.append(el.get("geometry", []))
    elif el["type"] == "relation":
        rings += [m["geometry"] for m in el.get("members", []) if m.get("role") in ("outer", "") and m.get("geometry")]
    polys = []
    for ring in rings:
        pts = [proj(p["lon"], p["lat"]) for p in ring]
        if len(pts) < 4: continue
        if pts[0] != pts[-1]: pts.append(pts[0])
        try: polys.append(Polygon(pts).buffer(0))
        except Exception: pass
    return unary_union(polys) if polys else None

def main():
    cache = sys.argv[1] if len(sys.argv) > 1 else None  # optional dir with cached whereis_bldgs.json / osm_all.json
    if cache:
        where = {k: (v["lat"], v["lon"]) for k, v in json.load(open(f"{cache}/whereis_bldgs.json")).items()}
        osm = json.load(open(f"{cache}/osm_all.json"))["elements"]
    else:
        log("Fetching building list from whereis.mit.edu...")
        where = fetch_whereis_buildings()
        log("Fetching footprints from OpenStreetMap...")
        osm = fetch_osm_footprints()

    polys = []
    for el in osm:
        g = footprint(el)
        if g is not None and not g.is_empty:
            polys.append((el.get("tags", {}).get("ref", ""), g))

    # Match each MIT building number to a footprint: by OSM ref tag, else by the polygon
    # containing the map pin, else the nearest polygon within 25m.
    bld = {}
    for num, (lat, lon) in where.items():
        if num in EXCLUDE: continue
        pin = Point(proj(lon, lat))
        by_ref = [g for r, g in polys if r.upper() == num]
        containing = [g for r, g in polys if g.contains(pin)]
        if by_ref: bld[num] = unary_union(by_ref)
        elif containing: bld[num] = unary_union(containing)
        else:
            dist, g = min(((g.distance(pin), g) for r, g in polys), key=lambda t: t[0])
            if dist < 25: bld[num] = g
            else: log("no footprint for", num)

    nums = sorted(bld, key=lambda s: (len(s), s))
    adj = {n: set() for n in nums}
    for i, a in enumerate(nums):
        for b in nums[i + 1:]:
            if bld[a].distance(bld[b]) <= NEIGHBOR_THRESHOLD_M:
                adj[a].add(b); adj[b].add(a)

    print("#pragma once")
    print("// GENERATED by scripts/gen_building_graph.py -- do not edit by hand.")
    print("//")
    print("// Adjacency graph of MIT buildings: two buildings are neighbors when their footprints")
    print("// touch or are within ~25m of each other (next door, or across a narrow street).
// Built from the MIT map")
    print("// (https://whereis.mit.edu) and OpenStreetMap building footprints.")
    print()
    print("#include <map>")
    print("#include <string>")
    print("#include <vector>")
    print()
    print("namespace mit_buildings {")
    print()
    print("inline const std::map<std::string, std::vector<std::string>> neighbors = {")
    for n in nums:
        ns = ", ".join(f'"{x}"' for x in sorted(adj[n], key=lambda s: (len(s), s)))
        print(f'    {{"{n}", {{{ns}}}}},')
    print("};")
    print()
    print("// Returns every building within `radius` hops of `building` (inclusive), mapped to its hop distance.")
    print("// Returns an empty map if the building is unknown.")
    print("inline std::map<std::string, int> neighbors_within(const std::string& building, int radius) {")
    print("    std::map<std::string, int> found;")
    print("    if (!neighbors.count(building)) return found;")
    print("    std::vector<std::string> frontier = {building};")
    print("    found[building] = 0;")
    print("    for (int depth = 1; depth <= radius && !frontier.empty(); depth++) {")
    print("        std::vector<std::string> next;")
    print("        for (const auto& b : frontier) {")
    print("            for (const auto& n : neighbors.at(b)) {")
    print("                if (found.count(n)) continue;")
    print("                found[n] = depth;")
    print("                next.push_back(n);")
    print("            }")
    print("        }")
    print("        frontier = std::move(next);")
    print("    }")
    print("    return found;")
    print("}")
    print()
    print("} // namespace mit_buildings")

if __name__ == "__main__":
    main()
