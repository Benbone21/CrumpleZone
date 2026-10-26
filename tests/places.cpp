// Places to find on the big Backcountry (docs/plans/big-maps.md, phase 7; src/places.h): the table is stable and complete, every
// place stands on dry, flat, clear ground that a truck can get to, discovery fires once and reaches the other players.
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <thread>

#include "../src/autopilot.h"
#include "../src/net.h"
#include "../src/onemap.h"
#include "../src/places.h"
#include "harness.h"

namespace {
uint64_t fnv(uint64_t h, const std::string& s) {
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return (h ^ 0xff) * 1099511628211ull;
}
int count(const std::vector<MapPlace>& v, int kind) {
    int n = 0;
    for (const MapPlace& p : v) n += p.kind == kind;
    return n;
}
}  // namespace

SCENARIO(placesTable, "pois/table", "The Backcountry's places: how many of each, unique stable ids and names, each in the district it says, services where they belong, photo spots with a subject") {
    // The table holds the Backcountry's own places ("BC.", in its nine districts: the counts and the shipped ids below are theirs),
    // then the regions' grown country's (docs/plans/regions2.md: "OC.", "MT.", "FR.", each in its region: checked on their own below).
    const std::vector<MapPlace>& all = mapPlaces(MAP_BACKCOUNTRY);
    std::vector<MapPlace> v, grown;
    for (const MapPlace& p : all) (p.id.rfind("BC.", 0) == 0 ? v : grown).push_back(p);
    {
        std::set<std::string> ids, names;
        for (const MapPlace& p : all) {
            ids.insert(p.id);
            if (p.kind != PK_PHOTO_SPOT) names.insert(p.name);
        }
        ctx.check("ids and names unique across the whole table", ids.size() == all.size() && names.size() == all.size() - (size_t)count(all, PK_PHOTO_SPOT));
        // The Backcountry's own come first: a region's place never sits among them.
        size_t firstGrown = all.size();
        for (size_t i = 0; i < all.size(); i++)
            if (all[i].id.rfind("BC.", 0) != 0) firstGrown = std::min(firstGrown, i);
        ctx.check("the regions' places after the Backcountry's", firstGrown == v.size());
        const char* codes[3] = {"OC.", "MT.", "FR."};
        for (int k = 0; k < 3; k++) {
            const MapRegion& r = mapRegions()[k];
            std::vector<MapPlace> mine;
            std::string bad;
            for (const MapPlace& p : grown)
                if (p.id.rfind(codes[k], 0) == 0) {
                    mine.push_back(p);
                    if (std::string(p.region) != r.name || std::string(regionAt(MAP_BACKCOUNTRY, p.p.x, p.p.z)) != r.name || regionContaining(p.p.x, p.p.z))
                        bad += " " + p.id;
                }
            const std::string n = r.name;
            ctx.check(n + ": its places in its grown country (out of its old map's square), prefixed " + codes[k], bad.empty() && !mine.empty(), bad);
            int outposts = 0;
            for (const MapPlace& p : mine) outposts += p.kind == PK_FUEL_STOP && (p.services & SVC_FUEL) && (p.services & SVC_REPAIR);
            ctx.check(n + ": an outpost (fuel and a repair bay), a cabin or camp, two lookouts, four caches",
                      outposts == 1 && count(mine, PK_CABIN) + count(mine, PK_OLD_CAMP) >= 1 && count(mine, PK_LOOKOUT) >= 2 && count(mine, PK_CACHE) == 4);
        }
        size_t prefixed = 0;
        for (const MapPlace& p : grown) prefixed += p.id.rfind("OC.", 0) == 0 || p.id.rfind("MT.", 0) == 0 || p.id.rfind("FR.", 0) == 0;
        ctx.check("every place after the Backcountry's is a region's", prefixed == grown.size());
    }
    ctx.check("one ranger station", count(v, PK_RANGER_STATION) == 1);
    ctx.check("four new fuel stops", count(v, PK_FUEL_STOP) == 4);
    ctx.check("six or more cabins and old camps", count(v, PK_CABIN) + count(v, PK_OLD_CAMP) >= 6, std::to_string(count(v, PK_CABIN) + count(v, PK_OLD_CAMP)));
    ctx.check("five lookouts (and the trail network's four)", count(v, PK_LOOKOUT) == 9, std::to_string(count(v, PK_LOOKOUT)));
    ctx.check("both waterfalls", count(v, PK_WATERFALL) == 2);
    ctx.check("two mines", count(v, PK_MINE) == 2);
    ctx.check("six or more river crossings", count(v, PK_RIVER_CROSSING) >= 6, std::to_string(count(v, PK_RIVER_CROSSING)));
    ctx.check("summits", count(v, PK_SUMMIT) >= 3);
    ctx.check("eight photo spots", count(v, PK_PHOTO_SPOT) == 8);
    ctx.check("three to five abandoned vehicles", count(v, PK_ABANDONED_VEHICLE) >= 3 && count(v, PK_ABANDONED_VEHICLE) <= 5);
    ctx.check("fifty new caches (ten old: sixty)", count(v, PK_CACHE) >= 50, std::to_string(count(v, PK_CACHE)));
    std::set<std::string> ids, names;
    bool regionsOk = true, servicesOk = true, photoOk = true, vehicleOk = true;
    const std::vector<std::string> regions = regionNames(MAP_BACKCOUNTRY);
    std::string bad;
    for (const MapPlace& p : v) {
        if (!ids.insert(p.id).second) bad += " dup-id:" + p.id;
        if (p.kind != PK_PHOTO_SPOT && !names.insert(p.name).second) bad += " dup-name:" + p.name;
        if (std::string(p.region) != regionAt(MAP_BACKCOUNTRY, p.p.x, p.p.z)) regionsOk = false, bad += " region:" + p.id;
        bool known = false;
        for (const std::string& r : regions) known |= r == p.region;
        regionsOk &= known;
        if (p.id.rfind("BC.", 0) != 0) bad += " prefix:" + p.id;
        if (p.kind == PK_RANGER_STATION && !((p.services & SVC_REPAIR) && (p.services & SVC_SHOP) && (p.services & SVC_FUEL))) servicesOk = false;
        if (p.kind == PK_FUEL_STOP && !(p.services & SVC_FUEL)) servicesOk = false;
        if ((p.kind == PK_CABIN || p.kind == PK_OLD_CAMP) && !(p.services & SVC_TOW)) servicesOk = false;
        if (p.kind == PK_PHOTO_SPOT && (p.subject.x == 0 && p.subject.z == 0)) photoOk = false;
        if (p.kind == PK_ABANDONED_VEHICLE && (p.vehicle.empty() || p.condition.empty())) vehicleOk = false;
    }
    ctx.check("unique ids and names, prefixed BC.", bad.empty(), bad);
    ctx.check("every place is in the district regionAt names, and that is one of the nine", regionsOk, bad);
    ctx.check("outposts offer what they should", servicesOk);
    ctx.check("photo spots name a subject", photoOk);
    ctx.check("abandoned vehicles name a vehicle and its condition", vehicleOk);
    // Every district has something to find.
    for (const std::string& r : regions) {
        int n = 0;
        for (const MapPlace& p : v) n += r == p.region && p.kind != PK_CACHE;
        ctx.check("something to find in " + r, n >= 1, std::to_string(n));
    }
    // Stable: a saved game refers to a place by its id (and the discovery list by its name), so these never change. New places go at
    // the end of the table.
    // (The ones that shipped before the trail network's, which follow the caches from Copper Peak on.)
    uint64_t hi = 1469598103934665603ull, hn = hi;
    for (const MapPlace& p : v) {
        if (p.id == "BC.COPPER_PEAK") break;
        if (p.kind != PK_CACHE) hi = fnv(hi, p.id), hn = fnv(hn, p.name);
    }
    std::printf("PLACES ids %016llx names %016llx (%zu places)\n", (unsigned long long)hi, (unsigned long long)hn, v.size());
    ctx.check("the place ids and names are the ones that shipped (new places go at the end of the table, after the others but before the caches)", hi == 0x28e497dc6fa22ed3ull && hn == 0xcccfc52f15ad0ea7ull);
    const MapPlace* ranger = findPlace(MAP_BACKCOUNTRY, "BC.RANGER");
    ctx.check("findPlace", ranger && ranger->name == "ELK RANGER STATION" && ranger->kind == PK_RANGER_STATION);
    ctx.check("findPlace of nothing", findPlace(MAP_BACKCOUNTRY, "BC.NOPE") == nullptr && mapPlaces(MAP_PROVING_GROUND).empty());
    ctx.check("regionAt: the old map is Elk Valley", std::string(regionAt(MAP_BACKCOUNTRY, 0, 0)) == "ELK VALLEY");
    ctx.check("regionAt: the corners", std::string(regionAt(MAP_BACKCOUNTRY, -2500, 2500)) == "TIMBER BASIN" && std::string(regionAt(MAP_BACKCOUNTRY, 2500, -2500)) == "BADLANDS WASH" &&
                                           std::string(regionAt(MAP_BACKCOUNTRY, 2500, 2500)) == "HIGH LAKES" && std::string(regionAt(MAP_BACKCOUNTRY, -2500, -2500)) == "LAKESHORE");
}

SCENARIO(placesGround, "pois/ground", "Every place on the Backcountry stands on dry ground on the map, the buildings on levelled, clear, non-overlapping pads, and the models are there") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const World& w = ctx.sim.world;
    const std::vector<MapPlace>& v = mapPlaces(MAP_BACKCOUNTRY);
    std::string bad;
    double worstRelief = 0;
    for (const MapPlace& p : v) {
        if (std::max(std::fabs(p.p.x), std::fabs(p.p.z)) > w.halfSize() - 150) bad += " edge:" + p.id;
        const double h = w.height(p.p.x, p.p.z);
        if (w.waterLevel(p.p.x, p.p.z) > h - 0.3) bad += " wet:" + p.id;
        if (p.pad > 0) {  // a building's pad: level enough to stand a building on
            double lo = 1e9, hi = -1e9;
            for (double dx = -p.pad * 0.6; dx <= p.pad * 0.6; dx += 1.5)
                for (double dz = -p.pad * 0.6; dz <= p.pad * 0.6; dz += 1.5) {
                    if (std::hypot(dx, dz) > p.pad * 0.6) continue;
                    const double q = w.height(p.p.x + dx, p.p.z + dz);
                    lo = std::min(lo, q), hi = std::max(hi, q);
                    if (w.waterLevel(p.p.x + dx, p.p.z + dz) > q - 0.2) bad += " wetpad:" + p.id;
                }
            worstRelief = std::max(worstRelief, hi - lo);
            if (hi - lo > 0.8) bad += " relief:" + p.id + "=" + std::to_string(hi - lo);
        }
    }
    ctx.check("all dry, on the map, pads level", bad.empty(), bad);
    ctx.metric("worst relief across a building's pad", worstRelief, "m", 0, 0.8);
    // Pads (and the old stations') don't overlap each other; caches are apart from each other and clear of the pads.
    bad.clear();
    struct Disc {
        std::string id;
        V3 p;
        double r;
    };
    std::vector<Disc> discs;
    for (const MapPlace& p : v)
        if (p.pad > 0) discs.push_back({p.id, p.p, p.pad});
    for (const Station& st : w.stations) {
        bool mine = false;
        for (const Disc& d : discs) mine |= std::hypot(d.p.x - st.p.x, d.p.z - st.p.z) < 1;  // (the ranger station's own)
        if (!mine) discs.push_back({st.name, st.p, 14});
    }
    for (size_t i = 0; i < discs.size(); i++)
        for (size_t j = 0; j < i; j++)
            if (std::hypot(discs[i].p.x - discs[j].p.x, discs[i].p.z - discs[j].p.z) < discs[i].r + discs[j].r + 1 && discs[i].id != discs[j].id) bad += " overlap:" + discs[i].id + "/" + discs[j].id;
    ctx.check("no two pads overlap", bad.empty(), bad);
    bad.clear();
    std::vector<const MapPlace*> caches = placesOfKind(MAP_BACKCOUNTRY, PK_CACHE);
    for (size_t i = 0; i < caches.size(); i++) {
        for (size_t j = 0; j < i; j++)
            if (std::hypot(caches[i]->p.x - caches[j]->p.x, caches[i]->p.z - caches[j]->p.z) < 60) bad += " near:" + caches[i]->id;
        for (const Disc& d : discs)
            if (std::hypot(caches[i]->p.x - d.p.x, caches[i]->p.z - d.p.z) < d.r + 5) bad += " pad:" + caches[i]->id;
        const double slope = std::hypot(w.height(caches[i]->p.x + 2, caches[i]->p.z) - w.height(caches[i]->p.x - 2, caches[i]->p.z),
                                        w.height(caches[i]->p.x, caches[i]->p.z + 2) - w.height(caches[i]->p.x, caches[i]->p.z - 2)) / 4;
        if (slope > 0.35) bad += " steep:" + caches[i]->id;
        if (caches[i]->reward < 200 || caches[i]->reward > 1500) bad += " pay:" + caches[i]->id;
    }
    ctx.check("caches are 60 m apart, clear of the pads, on gentle ground, paying $200-$1500", bad.empty(), bad);
    // The buildings are drawn.
    std::map<std::string, int> placed;
    for (const Placed& m : w.models) placed[m.id]++;
    ctx.check("the ranger station, garage and headframe are placed", placed["ranger_station"] == 1 && placed["garage"] == 1 && placed["mine_headframe"] == 1,
              std::to_string(placed["ranger_station"]) + " " + std::to_string(placed["garage"]) + " " + std::to_string(placed["mine_headframe"]));
    ctx.check("fuel pumps at each stop and the ranger station", placed["fuel_pump"] >= 4 * 3 + 2, std::to_string(placed["fuel_pump"]));
    ctx.check("gates", placed["gate"] >= 4, std::to_string(placed["gate"]));
    bool rangerStation = false;
    for (const Station& st : w.stations) rangerStation |= std::string(st.name) == "ELK RANGER STATION";
    ctx.check("the ranger station is a repair station too", rangerStation);
    std::printf("MODELS: ");
    for (const auto& kv : placed) std::printf("%s x%d  ", kv.first.c_str(), kv.second);
    std::printf("\n");
}

SCENARIO(placesReachable, "pois/all-reachable", "The autopilot (a Bronco, anywhere) finds a way overland from the trailhead to every place, each from the nearest one reached before it") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const World& w = ctx.sim.world;
    const VehicleSpec& bronco = vehicleCatalog()[2];
    std::vector<const MapPlace*> todo;
    // (The Shelf, a ledge cut across the Rimrock on COPPER PASS, is driven by bctrails/pass-shelf: the planner's cost grid can't see a
    // way onto a ledge built up against a cliff, any more than the Frontier's tarn.)
    for (const MapPlace& p : mapPlaces(MAP_BACKCOUNTRY))
        if (p.id != "BC.THE_SHELF") todo.push_back(&p);
    std::vector<V3> reached = {w.spawn};
    std::string unreached;
    int ok = 0;
    double longest = 0, total = 0;
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<const MapPlace*> later;  // (ones that needed a place on the other side of the river first)
    for (int pass = 0; pass < 2; pass++) {
    if (pass == 1) todo = later, later.clear();
    while (!todo.empty()) {
        // The place nearest to something already reached; from there, or the next nearest, and so on (a river between may need another way round).
        size_t bi = 0;
        double best = 1e18;
        for (size_t i = 0; i < todo.size(); i++)
            for (const V3& r : reached) {
                const double d = std::hypot(todo[i]->p.x - r.x, todo[i]->p.z - r.z);
                if (d < best) best = d, bi = i;
            }
        const MapPlace* p = todo[bi];
        todo.erase(todo.begin() + bi);
        std::vector<size_t> order(reached.size());
        for (size_t j = 0; j < order.size(); j++) order[j] = j;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return std::hypot(p->p.x - reached[a].x, p->p.z - reached[a].z) < std::hypot(p->p.x - reached[b].x, p->p.z - reached[b].z);
        });
        std::vector<V3> route;
        const V3 goal(p->p.x, w.height(p->p.x, p->p.z), p->p.z);
        bool found = false;
        size_t bj = 0;
        for (size_t t = 0; t < order.size() && t < (pass == 0 ? 6u : 400u) && !found; t++) {
            bj = order[t];
            found = planRoute(w, bronco, reached[bj], goal, PILOT_ANYWHERE, {}, route);
        }
        double L = 0;
        for (size_t k = 1; k < route.size(); k++) L += std::hypot(route[k].x - route[k - 1].x, route[k].z - route[k - 1].z);
        std::printf("ROUTE %-24s %s %5.0f m (from %.0f, %.0f)\n", p->id.c_str(), found ? "ok  " : "NONE", L, reached[bj].x, reached[bj].z);
        if (found) {
            ok++;
            longest = std::max(longest, L);
            total += L;
            reached.push_back(goal);
        } else if (pass == 0) later.push_back(p);
        else unreached += " " + p->id;
    }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("PLANNED %d routes in %.1f s, %.1f km in all\n", ok, secs, total / 1000);
    ctx.check("a way to every place", unreached.empty(), unreached);
}

SCENARIO(placesDiscovery, "pois/discovery", "Getting near a place finds it once (at its own radius); a find reaches the other players and is checked on arrival") {
    const std::vector<DiscoverSpot> spots = discoverSpots(MAP_BACKCOUNTRY);
    ctx.check("spots exist, none for caches or photo spots", spots.size() > 40);
    bool noCache = true;
    for (const DiscoverSpot& s : spots) noCache &= s.kind != PK_CACHE && s.kind != PK_PHOTO_SPOT;
    ctx.check("(caches and photo spots are not found by getting near)", noCache);
    const MapPlace* falls = findPlace(MAP_BACKCOUNTRY, "BC.BEAR_FALLS");
    ctx.check("bear falls", falls != nullptr);
    if (!falls) return;
    std::vector<std::string> found;
    ctx.check("far away: nothing", visitSpots(found, MAP_BACKCOUNTRY, falls->p + V3(falls->radius + 20, 0, 0), spots).empty());
    const std::vector<std::string> first = visitSpots(found, MAP_BACKCOUNTRY, falls->p + V3(falls->radius - 5, 0, 0), spots);
    ctx.check("within its radius: found, by name", first.size() == 1 && first[0] == "BEAR FALLS", first.empty() ? "" : first[0]);
    ctx.check("...and once only", visitSpots(found, MAP_BACKCOUNTRY, falls->p, spots).empty() && found.size() == 1);
    ctx.check("kept as MAP:NAME", found[0] == std::to_string(MAP_BACKCOUNTRY) + ":BEAR FALLS" && isFound(found, MAP_BACKCOUNTRY, "BEAR FALLS") && !isFound(found, MAP_FRONTIER, "BEAR FALLS"));
    // Standing at the ranger station finds it and nothing else beside it.
    const MapPlace* ranger = findPlace(MAP_BACKCOUNTRY, "BC.RANGER");
    const std::vector<std::string> atRanger = visitSpots(found, MAP_BACKCOUNTRY, ranger->p, spots);
    ctx.check("at the ranger station: found", atRanger.size() == 1 && atRanger[0] == "ELK RANGER STATION");

    // The message, and what a receiver does with it.
    std::vector<std::string> theirs;
    std::string name;
    ctx.check("a found message makes the place found for the receiver", takeFoundMessage(theirs, MAP_BACKCOUNTRY, spots, foundMessage(MAP_BACKCOUNTRY, "LOST MINE"), name) && name == "LOST MINE" &&
                                                                              isFound(theirs, MAP_BACKCOUNTRY, "LOST MINE"));
    ctx.check("...once", !takeFoundMessage(theirs, MAP_BACKCOUNTRY, spots, foundMessage(MAP_BACKCOUNTRY, "LOST MINE"), name) && theirs.size() == 1);
    ctx.check("a place that isn't one is refused", !takeFoundMessage(theirs, MAP_BACKCOUNTRY, spots, foundMessage(MAP_BACKCOUNTRY, "NOT A PLACE"), name) && theirs.size() == 1);
    ctx.check("another map's find is ignored", !takeFoundMessage(theirs, MAP_BACKCOUNTRY, spots, foundMessage(MAP_FRONTIER, "BEAR FALLS"), name) && theirs.size() == 1);
    ctx.check("other chatter is not one", !takeFoundMessage(theirs, MAP_BACKCOUNTRY, spots, "chat found 4 BEAR FALLS", name) && !takeFoundMessage(theirs, MAP_BACKCOUNTRY, spots, "found", name));
}

SCENARIO(placesNetDiscovery, "net/discovery", "A find sent by one player reaches the other over the network and marks the place found for them, once") {
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    const int port = 27600 + (int)(getpid() % 300) + 90;
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    Sim sim2;
    sim2.world = World(MAP_PROVING_GROUND);
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[3], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
    sim2.player = 0;
    NetSession client;
    bool joined = false, joinDone = false;
    std::thread joiner([&] {
        joined = client.join("127.0.0.1:" + std::to_string(port), "BUDDY", 3.0);
        joinDone = true;
    });
    for (int k = 0; k < 400 && !joinDone; k++) {
        host.receive(ctx.sim, ctx.t);
        usleep(5000);
    }
    joiner.join();
    ctx.check("client is let in", joined, client.error);
    if (!joined) return;
    client.adopt(sim2);
    double t = ctx.t;
    auto pump = [&](int frames) {
        for (int k = 0; k < frames; k++) {
            host.send(ctx.sim, t);
            host.receive(ctx.sim, t);
            client.receive(sim2, t);
            client.send(sim2, t);
            usleep(1000);
            t += 0.002;
        }
    };
    pump(100);
    const std::vector<DiscoverSpot> spots = discoverSpots(MAP_BACKCOUNTRY);
    std::vector<std::string> hostFound, guestFound;
    const MapPlace* mine = findPlace(MAP_BACKCOUNTRY, "BC.LOST_MINE");
    // The host finds the lost mine and says so; the guest hears it.
    const std::vector<std::string> fresh = visitSpots(hostFound, MAP_BACKCOUNTRY, mine->p, spots);
    ctx.check("the host found it (and the pit truck beside it)", fresh.size() == 2 && fresh[0] == "LOST MINE", std::to_string(fresh.size()));
    for (const std::string& f : fresh) host.sendGame(foundMessage(MAP_BACKCOUNTRY, f));
    pump(150);
    int arrived = 0;
    std::string who;
    for (const auto& m : client.gameIn) {
        std::string name;
        if (m.player == 0 && takeFoundMessage(guestFound, MAP_BACKCOUNTRY, spots, m.text, name)) arrived++, who = name;
    }
    ctx.check("the guest receives the finds and marks them found", arrived == 2 && isFound(guestFound, MAP_BACKCOUNTRY, "LOST MINE"), std::to_string(arrived));
    // The guest, driving up to the same place, finds nothing new.
    ctx.check("...and driving there finds nothing more", visitSpots(guestFound, MAP_BACKCOUNTRY, mine->p, spots).empty());
    // And the other way.
    client.gameIn.clear(), host.gameIn.clear();
    const MapPlace* tower = findPlace(MAP_BACKCOUNTRY, "BC.BEAR_FALLS");
    client.sendGame(foundMessage(MAP_BACKCOUNTRY, tower->name));
    pump(150);
    int back = 0;
    for (const auto& m : host.gameIn) {
        std::string name;
        if (m.player == client.self && takeFoundMessage(hostFound, MAP_BACKCOUNTRY, spots, m.text, name)) back++;
    }
    ctx.check("the host receives the guest's find", back == 1 && isFound(hostFound, MAP_BACKCOUNTRY, "BEAR FALLS"), std::to_string(back));
    client.close();
    host.close();
}
