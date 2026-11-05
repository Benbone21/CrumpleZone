// Wrecks out in the country (src/wrecks.*): how many, where (dry, off the rocks, the same on every machine), what's in them, searching one in a
// career and in sandbox, and the search shared in multiplayer.
#include <map>
#include <set>

#include "../src/career.h"
#include "../src/careersession.h"
#include "../src/interact.h"
#include "../src/modelkit.h"
#include "../src/onemap.h"
#include "../src/places.h"
#include "../src/procworld.h"
#include "../src/props.h"
#include "../src/worldcache.h"
#include "../src/walker.h"
#include "../src/wrecks.h"
#include "harness.h"

using namespace proc;

namespace {

World windowOf(uint64_t seed) {
    World::procSeed = seed, World::procOriginX = 0, World::procOriginZ = 0, World::procOriginSet = true;
    World w(MAP_PROCEDURAL);
    World::procOriginSet = false;
    return w;
}

// How `w` stands round each wreck: wet ones, ones with a rock through them, ones with a trunk through them.
void checkClear(Ctx& ctx, const World& w, const std::vector<const MapPlace*>& list, const std::string& tag) {
    int wet = 0, rocked = 0, treed = 0, checked = 0;
    std::string why;
    for (const MapPlace* pl : list) {
        if (std::max(std::fabs(pl->p.x), std::fabs(pl->p.z)) > w.halfSize() - 120) continue;
        checked++;
        const wrecks::Pose p = wrecks::poseOf(w, *pl);
        const V3 mid = p.middle();
        const double r = std::max(p.look.half.x, p.look.half.z);
        if (w.waterLevel(pl->p.x, pl->p.z) > w.height(pl->p.x, pl->p.z) - 0.1) wet++, why += " wet:" + pl->id;
        World::NearbySolids n;
        w.solidsNear(mid.x - 30, mid.z - 30, mid.x + 30, mid.z + 30, n);
        for (int i : n.rocks) {
            const Rock& k = w.rocks[i];
            bool in = false;  // (a corner or the middle of its box inside the rock's hull)
            for (int c = 0; c < 9 && !in; c++) {
                const V3 q = c == 8 ? mid : mid + p.ax[0] * ((c & 1 ? 1 : -1) * p.look.half.x) + p.ax[1] * ((c & 2 ? 0.9 : -0.2) * p.look.half.y) + p.ax[2] * ((c & 4 ? 1 : -1) * p.look.half.z);
                bool inside = !k.fn.empty();
                for (size_t f = 0; f < k.fn.size() && inside; f++) inside = dot(k.fn[f], q) <= k.fd[f];
                in = inside;
            }
            if (in) {
                rocked++, why += " rock:" + pl->id;
                break;
            }
        }
        for (const Tree& t : w.trees)
            if (std::hypot(t.base.x - mid.x, t.base.z - mid.z) < std::min(r, 2.0)) {
                treed++, why += " tree:" + pl->id;
                break;
            }
    }
    ctx.check(tag + ": wrecks to check", checked > 5, std::to_string(checked));
    ctx.metric(tag + ": wrecks in water", wet, "", 0, 0);
    ctx.metric(tag + ": wrecks with a rock through them", rocked, "", 0, 0);
    ctx.metric(tag + ": wrecks with a tree through them", treed, "", 0, 0);
    if (!why.empty()) ctx.note(why.substr(0, 400));
}

}  // namespace

SCENARIO(wrecksCount, "wrecks/count", "About three times as many on the procedural world (0.9 a node before), the same from another generator for the seed, no names, off the trails; every model used") {
    const ProcWorld& G = ProcWorld::forSeed(5150);
    const ProcWorld other(5150);
    int n = 0, nodes = 0, same = 0, compared = 0;
    std::set<std::string> ids;
    bool named = false;
    for (int64_t i = -12; i <= 12; i++)
        for (int64_t j = -12; j <= 12; j++) {
            std::vector<MapPlace> v;
            G.nodePlaces(i, j, v);
            nodes++;
            for (const MapPlace& p : v)
                if (p.kind == PK_WRECK) n++, ids.insert(p.id), named |= !p.name.empty();
            if (compared < 40) {
                const auto a = G.nodeWrecks(i, j), b = other.nodeWrecks(i, j);
                compared++;
                bool eq = a.size() == b.size();
                for (size_t k = 0; k < a.size() && eq; k++) eq = a[k].x == b[k].x && a[k].z == b[k].z && a[k].yaw == b[k].yaw;
                same += eq;
            }
        }
    ctx.metric("wrecks a node (0.9 before)", (double)n / nodes, "", 2.3, 3.4);
    ctx.check("ids unique", (int)ids.size() == n);
    ctx.check("no names", !named);
    ctx.check("deterministic: another generator for the seed gives the same", same == compared, std::to_string(same));
    std::map<std::string, int> models;
    int rolled = 0, sunk = 0;
    for (const std::string& id : ids) {
        const wrecks::Look l = wrecks::lookOf(wrecks::hashOf(id, 5150));
        models[l.model]++;
        rolled += std::fabs(l.roll) > 1.2, sunk += l.sink > 0.3;
    }
    ctx.check("all six models turn up", models.size() == 6, std::to_string(models.size()));
    for (int i = 0; i < wrecks::modelCount(); i++) ctx.check(std::string("a model for ") + wrecks::modelAt(i), mk::findModel(wrecks::modelAt(i)) != nullptr);
    ctx.check("some rolled, some half buried", rolled > n / 40 && sunk > n / 10, std::to_string(rolled) + " rolled, " + std::to_string(sunk) + " sunk of " + std::to_string(n));
}

SCENARIO(wrecksProcClear, "wrecks/proc-clear", "A window's wrecks: placed with a tilted collider, in no water, no rock and no tree; no crate, no cache and no discovery for them; the next window has them alike") {
    World w = windowOf(5150);
    const std::vector<MapPlace> places = windowPlaces(w);
    std::vector<const MapPlace*> list;
    for (const MapPlace& p : places)
        if (p.kind == PK_WRECK) list.push_back(&p);
    checkClear(ctx, w, list, "procedural");
    int models = 0;
    for (const Placed& m : w.models) models += m.id.rfind("wreck_", 0) == 0 && m.oriented;
    ctx.check("every wreck's model is there", models >= (int)list.size(), std::to_string(models) + " of " + std::to_string(list.size()));
    setActiveWindow(w);
    for (const CacheSpot& c : cacheSpots(w)) ctx.check("no wreck among the caches", c.id.find("WRECK") == std::string::npos);
    for (const DiscoverSpot& d : discoverSpots(MAP_PROCEDURAL)) ctx.check("no wreck among the things to discover", d.kind != PK_WRECK);
    ctx.check("searchable", wrecks::spots(w).size() >= list.size() / 2);
    World::procSeed = 0;
}

SCENARIO(wrecksBuiltMaps, "wrecks/built-maps", "The Backcountry has about 40 with its regions (the Frontier's and the Mountain's too), the freeway a few, the proving ground none; none in water, rocks or trees") {
    const std::vector<const MapPlace*> bc = placesOfKind(MAP_BACKCOUNTRY, PK_WRECK);
    ctx.metric("the Backcountry's wrecks", (double)bc.size(), "", 36, 60);
    int frontier = 0, mountain = 0;
    for (const MapPlace* p : bc) {
        const MapRegion* r = regionContaining(p->p.x, p->p.z);
        frontier += p->p.x > -150 && p->p.z < -2650, mountain += p->p.x < -150 && p->p.z < -2650;
        (void)r;
    }
    ctx.check("some in the Frontier's country and the Mountain's", frontier >= 4 && mountain >= 4, std::to_string(frontier) + ", " + std::to_string(mountain));
    ctx.check("none on the proving ground", placesOfKind(MAP_PROVING_GROUND, PK_WRECK).empty());
    ctx.check("a few by the freeway", placesOfKind(MAP_FREEWAY, PK_WRECK).size() >= 3);
    World w(MAP_BACKCOUNTRY);
    checkClear(ctx, w, bc, "Backcountry");
    ctx.check("all searchable on the big map", wrecks::spots(w).size() == bc.size());
    for (const CacheSpot& c : wrecks::spots(w)) ctx.check("kept by its id, no label", c.wreck && c.label.empty() && !c.id.empty());
    // (The game's own world has the scanned rocks: another whole big map, only asked for, as onemap/xplat-fingerprint.)
    const props::HullLibrary& lib = props::sharedHullLibrary();
    if (std::getenv("CZ_XPLAT_SCANS") && lib.loaded()) {
        props::ScopedRockScans scans(&lib);
        const bool was = worldcache::bypass;
        worldcache::bypass = true;
        World s(MAP_BACKCOUNTRY);
        worldcache::bypass = was;
        checkClear(ctx, s, bc, "Backcountry, scanned rocks");
    }
}

SCENARIO(wrecksLoot, "wrecks/loot", "About half hold something (fuel cans among it), settled by the id; a career gets it once; an empty one says NOTHING USEFUL") {
    World& w = ctx.sim.world;
    int full = 0, cans = 0, total = 0, money = 0;
    for (int i = 0; i < 2000; i++) {
        MapPlace pl;
        pl.id = "T.WRECK." + std::to_string(i), pl.kind = PK_WRECK, pl.p = V3(0, 0, 0);
        const CacheSpot c = wrecks::spotOf(w, pl), again = wrecks::spotOf(w, pl);
        total++;
        full += !c.gear.empty() || c.cash > 0;
        money += c.cash > 0;
        for (const std::string& g : c.gear) cans += g == repairPartKey(PART_FUEL_CAN);
        if (c.gear != again.gear || c.cash != again.cash) ctx.check("the same wreck holds the same", false);
    }
    ctx.metric("share with something in them", (double)full / total, "", 0.42, 0.58);
    ctx.check("fuel cans and money among it", cans > 150 && money > 150, std::to_string(cans) + " cans, " + std::to_string(money) + " with cash");
    CareerSession run;
    run.write = false;
    run.profile.startFresh();
    run.profile.on = true;
    wrecks::Searched done;
    CacheSpot empty, can;
    empty.id = "E", empty.wreck = true;
    can.id = "C", can.wreck = true, can.cash = 120, can.gear = {repairPartKey(PART_FUEL_CAN)};
    const int cash0 = run.profile.cash;
    ctx.check("an empty wreck says so", wrecks::search(run, ctx.sim, done, empty) == "NOTHING USEFUL");
    ctx.check("and pays nothing", run.profile.cash == cash0);
    const std::string said = wrecks::search(run, ctx.sim, done, can);
    ctx.check("the banner says what's in it", said == "FOUND: $120, FUEL CAN", said);
    ctx.check("a career gets it", run.profile.cash == cash0 + 120 && run.profile.kit().parts[PART_FUEL_CAN] == 1);
    ctx.check("once", wrecks::search(run, ctx.sim, done, can).empty() && wrecks::isSearched(run, done, can));
}

SCENARIO(wrecksSandbox, "wrecks/sandbox", "In sandbox the search takes the hold and says what you found, gives nothing, and stays searched; no supply crates come with it") {
    Car& car = ctx.add(vehicleCatalog()[2], 0, 0, 0);
    ctx.focus(car);
    CareerSession run;
    run.write = false;
    wrecks::Searched done;
    CacheSpot c;
    c.id = "S", c.wreck = true, c.cash = 200, c.at = V3(8, 0, 8), c.gear = {repairPartKey(PART_FUEL_CAN)};
    Walker wk;
    wk.p = V3(8, ctx.sim.world.height(8, 8), 6), wk.active = true;
    Interact ia;
    ia.spots.push_back({c.id, c.at, "SEARCH THE WRECK"});
    ia.spots.back().wreck = true;
    const V3 eye = wk.p + V3(0, 1.62, 0), look = c.at + V3(0, 0.5, 0) - eye;
    ia.update(ctx.sim, wk, eye, look, false, Ctx::kFrame);
    ctx.check("the prompt", ia.current.kind == IA_SEARCH_WRECK && ia.current.prompt == "SEARCH THE WRECK", ia.current.prompt);
    double held = 0;
    while (ia.openedSpot.empty() && held < 6) ia.update(ctx.sim, wk, eye, look, true, Ctx::kFrame), held += Ctx::kFrame;
    ctx.check("it takes a hold", ia.openedSpot == "S" && held > 2, std::to_string(held));
    const std::string said = wrecks::search(run, ctx.sim, done, c);
    ctx.check("FOUND banner", said == "FOUND: $200, FUEL CAN", said);
    ctx.check("nothing granted", run.profile.cash == 0 && run.profile.stock.empty());
    ctx.check("searched for the session", wrecks::isSearched(run, done, c) && wrecks::search(run, ctx.sim, done, c).empty());
}

SCENARIO(wrecksShared, "wrecks/shared", "A wreck searched by one player is searched for the others, in sandbox and in a career; the message reads back and a bad one is refused") {
    int map = -1;
    std::string id;
    ctx.check("reads back", wrecks::takeMessage(wrecks::message(MAP_PROCEDURAL, "5150#PW.1.2.WRECK0"), map, id) && map == MAP_PROCEDURAL && id == "5150#PW.1.2.WRECK0");
    ctx.check("bad ones refused", !wrecks::takeMessage("wreck 3", map, id) && !wrecks::takeMessage("cache 3 X", map, id));
    CacheSpot c;
    c.id = "W", c.map = MAP_BACKCOUNTRY, c.wreck = true, c.cash = 100;
    CareerSession a, b;
    a.write = b.write = false;
    wrecks::Searched da, db;
    ctx.check("sandbox: the first search counts", !wrecks::search(a, ctx.sim, da, c).empty());
    ctx.check("sandbox: told, it's searched here too", !wrecks::search(b, ctx.sim, db, c).empty() && wrecks::isSearched(b, db, c));
    CareerSession ca, cb;
    ca.write = cb.write = false;
    ca.profile.on = cb.profile.on = true;
    wrecks::search(ca, ctx.sim, da, c);
    wrecks::search(cb, ctx.sim, db, c);
    ctx.check("career: everyone there gets what was in it, as with a cache", ca.profile.cash == 100 && cb.profile.cash == 100 && wrecks::isSearched(cb, db, c));
}

SCENARIO(wrecksOldSave, "wrecks/old-save", "A career saved before fuel cans and wrecks loads as it was: no cans, its caches still opened; cans save and load") {
    const std::string path = "build/test-oldcareer.txt";
    Career old;
    old.on = true, old.cash = 900;
    old.opened.insert("4:WRECK SITE");
    old.stock[repairPartKey(PART_CV_AXLE)] = 1;
    ctx.check("saved", old.save(path));
    Career back;
    ctx.check("loads", back.load(path) && back.cash == 900 && back.opened.count("4:WRECK SITE") == 1);
    ctx.check("no cans", back.kit().parts[PART_FUEL_CAN] == 0 && back.kit().parts[PART_EMPTY_CAN] == 0);
    back.grant(repairPartKey(PART_FUEL_CAN));
    back.grant(repairPartKey(PART_EMPTY_CAN));
    ctx.check("saved with cans", back.save(path));
    Career again;
    ctx.check("loads them", again.load(path) && again.kit().parts[PART_FUEL_CAN] == 1 && again.kit().parts[PART_EMPTY_CAN] == 1);
}
