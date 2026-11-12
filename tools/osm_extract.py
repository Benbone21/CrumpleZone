#!/usr/bin/env python3
"""A real-terrain map's OpenStreetMap file (docs/real-terrain.md): an Overpass response cut down, by rules, to what the importer uses.

    python3 -I tools/osm_extract.py RAW.json S W N E OUT.txt [SIDECAR ...]

Any box, no hand work. Keeps:
  - ways: highway=track|unclassified|tertiary that are named or 4wd_only=yes (the 4x4 tracks and the county roads to them),
    and every way a sidecar given after OUT names (`trail ... | osm ID ID ...`, a negative id meaning reversed);
  - points inside the box: tourism=attraction|viewpoint, highway=trailhead, natural=peak|arch|rock|cliff, with a name.
Coordinates to 6 decimals (about 0.1 m); tags cut to the ones the importer reads.

The query that makes RAW (one bounded, read-only Overpass request; retry a 502 politely):
  [out:json][timeout:180][bbox:S,W,N,E];
  ( way["highway"~"^(track|path|unclassified|tertiary|secondary|primary|trunk|service)$"];
    node["tourism"~"^(attraction|viewpoint)$"]; node["highway"="trailhead"]; node["natural"]["name"]; );
  out geom;
Data (c) OpenStreetMap contributors, ODbL 1.0: the output says so, and so must the map's credits.
"""
import json
import re
import sys

KEEP_TAGS = ("highway", "4wd_only", "tracktype", "smoothness", "oneway", "surface", "motor_vehicle", "access", "ref")
NODE_KINDS = (("tourism", ("attraction", "viewpoint")), ("highway", ("trailhead",)), ("natural", ("peak", "arch", "rock", "cliff")),
              ("historic", ("archaeological_site",)))


def clean(s):
    return s.replace("|", "/").replace("#", "").strip()


def main(raw, box, out, sidecars):
    S, W, N, E = box
    d = json.load(open(raw))
    named = []
    for sc in sidecars:
        for line in open(sc):
            m = re.search(r"\|\s*osm\s+([-0-9 ]+)", line.split("#")[0])
            if m:
                named += [abs(int(w)) for w in m.group(1).split()]
    ways = {e["id"]: e for e in d["elements"] if e["type"] == "way"}
    missing = [w for w in named if w not in ways]
    if missing:
        sys.exit("ways not in the response: %s" % missing)
    keep = set(named)
    for w in ways.values():
        t = w.get("tags", {})
        if t.get("highway") in ("track", "unclassified", "tertiary") and (t.get("name") or t.get("4wd_only") == "yes"):
            if any(S <= g["lat"] <= N and W <= g["lon"] <= E for g in w["geometry"]):
                keep.add(w["id"])
    stamp = d.get("osm3s", {}).get("timestamp_osm_base", "?")
    o = ["# OpenStreetMap data: (c) OpenStreetMap contributors, ODbL 1.0 (https://www.openstreetmap.org/copyright).",
         "# Derived by tools/osm_extract.py from one Overpass query of %.4f-%.4f N, %.4f-%.4f E, data as of %s." % (S, N, W, E, stamp),
         "# way ID | NAME | TAGS | LAT LON ...   (OSM's own vertex order)",
         "# node ID | NAME | TAGS | LAT LON"]
    for wid in sorted(keep):
        w = ways[wid]
        t = w.get("tags", {})
        tags = " ".join("%s=%s" % (k, t[k].replace(" ", "_")) for k in KEEP_TAGS if k in t)
        pts = " ".join("%.6f %.6f" % (g["lat"], g["lon"]) for g in w["geometry"])
        o.append("way %d | %s | %s | %s" % (wid, clean(t.get("name", "")), tags, pts))
    nodes = []
    for e in d["elements"]:
        if e["type"] != "node" or "name" not in e.get("tags", {}) or not (S <= e["lat"] <= N and W <= e["lon"] <= E):
            continue
        t = e["tags"]
        kinds = ["%s=%s" % (k, t[k]) for k, vals in NODE_KINDS if t.get(k) in vals]
        if kinds:
            nodes.append((e["id"], clean(t["name"]), " ".join(kinds), e["lat"], e["lon"]))
    for nid, name, kinds, lat, lon in sorted(nodes):
        o.append("node %d | %s | %s | %.6f %.6f" % (nid, name, kinds, lat, lon))
    open(out, "w").write("\n".join(o) + "\n")
    print("%s: %d ways (%d named by the sidecars), %d points" % (out, len(keep), len(set(named)), len(nodes)))


if __name__ == "__main__":
    if len(sys.argv) < 7:
        sys.exit(__doc__)
    main(sys.argv[1], tuple(float(v) for v in sys.argv[2:6]), sys.argv[6], sys.argv[7:])
