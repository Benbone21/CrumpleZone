// Anchor gear (anchors.h): what a winch line can be tied to and what it takes. A tree needs a
// strap and a boulder a sling (they come back); open ground takes a stake, bare rock a bolt,
// snow and ice a picket (used up, left in the ground). Counts are carried, saved in a career,
// refilled free at a sandbox station.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../src/career.h"
#include "../src/modelkit.h"
#include "harness.h"

namespace {

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }

// A dry spot with at least 3 m of the same surface around it (a 4 m scan of the whole map), if there is one.
bool findSurface(const World& w, Surface s, V3& out) {
    for (double x = -560; x <= 560; x += 4)
        for (double z = -560; z <= 560; z += 4) {
            if (w.surfaceAt(x, z) != s || w.waterLevel(x, z) > w.height(x, z) - 0.05) continue;
            bool whole = true;
            for (int k = 0; k < 8 && whole; k++) whole = w.surfaceAt(x + 3 * std::cos(k * M_PI / 4), z + 3 * std::sin(k * M_PI / 4)) == s;
            if (whole) return out = V3(x, w.height(x, z) + 0.1, z), true;
        }
    return false;
}

RopeEnd stakeAt(const V3& p) {
    RopeEnd e;
    e.kind = ANCHOR_STAKE;
    e.p = p;
    return e;
}

// A hookable spot: `ahead` m in front of the winch on the ground, nudged so the surface is the same.
RopeEnd stakeAhead(const Car& c, double ahead, double aside = 0) {
    const Frame fr = c.frame();
    const V3 f = norm(V3(fr.fwd.x, 0, fr.fwd.z)), l(-f.z, 0, f.x);
    return stakeAt(c.hookPoint(0) + f * ahead + l * aside);
}

}  // namespace

SCENARIO(anchorRules, "anchors/rules", "What open ground takes: stakes in dirt, grass, gravel, sand and shallow mud; a bolt in bare rock; a picket in snow and ice; nothing on pavement or scree; nothing under more than 15 cm of water") {
    struct Row { Surface s; double water; AnchorGear gear; const char* name; };
    const Row rows[] = {
        {SURF_DIRT, 0, GEAR_STAKE, "dirt"},   {SURF_GRASS, 0, GEAR_STAKE, "grass"}, {SURF_GRAVEL, 0, GEAR_STAKE, "gravel"}, {SURF_SAND, 0, GEAR_STAKE, "sand"},
        {SURF_MUD, 0.1, GEAR_STAKE, "shallow mud"}, {SURF_ROCK, 0, GEAR_BOLT, "bare rock"}, {SURF_SNOW, 0, GEAR_ICE, "snow"}, {SURF_ICE, 0, GEAR_ICE, "ice"},
        {SURF_PAVED, 0, GEAR_COUNT, "pavement"}, {SURF_WET, 0, GEAR_COUNT, "wet tarmac"}, {SURF_SCREE, 0, GEAR_COUNT, "scree"},
        {SURF_MUD, 0.3, GEAR_COUNT, "deep mud"}, {SURF_DIRT, 0.3, GEAR_COUNT, "dirt under water"}, {SURF_ROCK, 0.3, GEAR_COUNT, "rock under water"},
    };
    for (const Row& r : rows) {
        const anchors::GroundRule gr = anchors::groundRule(r.s, r.water);
        ctx.check(std::string(r.name) + ": " + (r.gear == GEAR_COUNT ? "nothing sets" : anchors::gearName(r.gear)), gr.gear == r.gear, std::to_string(gr.gear));
        ctx.check(std::string(r.name) + ": " + (r.gear == GEAR_COUNT ? "says why" : "says what it needs when you're out"), r.gear == GEAR_COUNT ? *gr.why != 0 : *gr.needs != 0);
    }
    ctx.check("deep water says so", std::strcmp(anchors::groundRule(SURF_MUD, 0.3).why, "TOO DEEP TO STAKE") == 0);
    ctx.check("rock without a bolt says what it needs", std::strcmp(anchors::groundRule(SURF_ROCK, 0).needs, "CAN'T STAKE ON ROCK: NEEDS A ROCK BOLT") == 0);
    // What you carry: the starting sets, the HUD line.
    AnchorStock sandbox;
    bool set = true;
    for (int g = 0; g < GEAR_COUNT; g++) set &= sandbox.n[g] == anchors::kSandboxStart[g];
    ctx.check("a sandbox starts with the fuller set", set && sandbox.refills && anchors::kSandboxStart[GEAR_STAKE] == 4);
    ctx.check("the HUD line", anchors::hudLine(sandbox) == "STRAPS 3 SLING 2 STAKES 4 BOLTS 2 ICE 2", anchors::hudLine(sandbox));
    ctx.check("the HUD line has no '&'", anchors::hudLine(sandbox).find('&') == std::string::npos);
    ctx.check("straps and slings come back; stakes, bolts and pickets don't", !anchors::usedUp(GEAR_STRAP) && !anchors::usedUp(GEAR_SLING) && anchors::usedUp(GEAR_STAKE) &&
                                                                                  anchors::usedUp(GEAR_BOLT) && anchors::usedUp(GEAR_ICE));
}

SCENARIO(anchorSurfaces, "anchors/surfaces", "On the real ground: the proving ground's course and the Frontier decide what a spot takes, and a target that can't be used says why") {
    struct Want { Surface s; AnchorGear gear; const char* name; };
    const Want wants[] = {{SURF_GRAVEL, GEAR_STAKE, "gravel"}, {SURF_SAND, GEAR_STAKE, "sand"}, {SURF_MUD, GEAR_STAKE, "mud"}, {SURF_SNOW, GEAR_ICE, "snow"},
                          {SURF_ICE, GEAR_ICE, "ice"},         {SURF_WET, GEAR_COUNT, "wet tarmac"}, {SURF_PAVED, GEAR_COUNT, "pavement"},
                          {SURF_ROCK, GEAR_BOLT, "rock"},      {SURF_SCREE, GEAR_COUNT, "scree"},    {SURF_GRASS, GEAR_STAKE, "grass"}, {SURF_DIRT, GEAR_STAKE, "dirt"}};
    bool have[SURF_COUNT] = {};
    for (int map : {MAP_PROVING_GROUND, MAP_FRONTIER}) {
        ctx.sim = Sim();
        ctx.sim.world = World(map);
        Car& c = ctx.add(bronco(), 0, 0, 0);
        ctx.run(0.2);
        const int ci = (int)(&c - ctx.sim.cars.data());
        for (const Want& w : wants) {
            V3 p;
            if (have[w.s] || !findSurface(ctx.sim.world, w.s, p)) continue;
            have[w.s] = true;
            // Wherever the truck is: a rope end is checked against the ground under the end, whatever the distance.
            const Sim::AnchorUse use = ctx.sim.anchorUse(stakeAt(p), ci);
            ctx.check(std::string(w.name) + " (" + mapName(map) + "): " + (w.gear == GEAR_COUNT ? "refused" : anchors::gearName(w.gear)),
                      w.gear == GEAR_COUNT ? !use.ok && *use.why : use.ok && use.gear == w.gear, use.why);
            // Out of that gear: refused, and the reason is on the target.
            if (w.gear != GEAR_COUNT) {
                ctx.sim.anchorStock.n[w.gear] = 0;
                const Sim::AnchorUse out = ctx.sim.anchorUse(stakeAt(p), ci);
                ctx.check(std::string(w.name) + " with none left: refused, says why", !out.ok && *out.why, out.why);
                ctx.sim.anchorStock = AnchorStock();
            }
        }
    }
    for (const Want& w : wants) ctx.check(std::string("a ") + w.name + " spot exists to test", have[w.s]);
}

SCENARIO(anchorCounts, "anchors/counts", "Hooking to a stake uses one up and says how many are left; at none the hook is refused and no line is made; a strap goes back when the line comes off") {
    ctx.sim = Sim();
    ctx.sim.world = World(MAP_PROVING_GROUND);
    Car& c = ctx.add(bronco(), -60, -270, 0);  // open ground: dirt south of the proving ground's paved pad
    ctx.focus(c);
    ctx.run(0.5);
    const int ci = (int)(&c - ctx.sim.cars.data());
    Sim& sim = ctx.sim;
    const Surface under = sim.world.surfaceAt(c.hookPoint(0).x + 10, c.hookPoint(0).z);
    ctx.check("a stakeable spot ahead", anchors::groundRule(under, 0).gear == GEAR_STAKE, std::to_string(under));
    sim.anchorStock.fill(anchors::kCareerStart);  // 2 stakes
    sim.events.clear();
    for (int k = 0; k < 2; k++) {
        const std::string msg = sim.hookWinchTo(ci, stakeAhead(c, 8 + 3 * k, 0));
        ctx.check("stake " + std::to_string(k + 1) + " hooks", msg.rfind("WINCH HOOKED", 0) == 0, msg);
        ctx.check("one less in the pack", sim.anchorStock.n[GEAR_STAKE] == 1 - k, std::to_string(sim.anchorStock.n[GEAR_STAKE]));
        sim.dropWinch(ci);
    }
    bool said = false;
    for (const std::string& e : sim.events) said |= e == "GROUND STAKE SET: 1 LEFT";
    ctx.check("it says how many are left", said);
    ctx.check("two stakes left in the ground", sim.setAnchors.size() == 2);
    // None left.
    const std::string refused = sim.hookWinchTo(ci, stakeAhead(c, 20, 0));
    ctx.check("with none left it's refused", refused == "NO GROUND STAKES LEFT" && sim.ropes.empty() && sim.anchorStock.n[GEAR_STAKE] == 0, refused);
    // ...but the stake you left is still there to use again, for nothing.
    const std::string again = sim.hookWinchTo(ci, stakeAhead(c, 8.4, 0.3));
    ctx.check("a line back to a stake you left is free", again.rfind("WINCH HOOKED", 0) == 0 && sim.anchorStock.n[GEAR_STAKE] == 0 && sim.setAnchors.size() == 2, again);
    sim.dropWinch(ci);
    // A strap: carried, not used up.
    RopeEnd tree;
    tree.kind = ANCHOR_TREE;
    tree.p = c.hookPoint(0) + V3(12, 0.5, 0);
    sim.anchorStock.fill(anchors::kCareerStart);
    ctx.check("two straps in the pack", sim.anchorsFree(GEAR_STRAP) == 2);
    const std::string hooked = sim.hookWinchTo(ci, tree);
    ctx.check("hooks a tree with a strap", hooked.rfind("WINCH HOOKED", 0) == 0, hooked);
    ctx.check("the strap is out on the line, not used up", sim.anchorStock.n[GEAR_STRAP] == 2 && sim.anchorsFree(GEAR_STRAP) == 1);
    sim.events.clear();
    ctx.check("unhooks", sim.dropWinch(ci));
    ctx.check("the strap is back", sim.anchorsFree(GEAR_STRAP) == 2);
    bool back = false;
    for (const std::string& e : sim.events) back |= e == "TREE STRAP BACK: 2 CARRIED";
    ctx.check("and it says so", back);
    // No strap, no tree.
    sim.anchorStock.n[GEAR_STRAP] = 0;
    const std::string none = sim.hookWinchTo(ci, tree);
    ctx.check("without a strap a tree is refused", none == "NEEDS A TREE STRAP" && sim.ropes.empty(), none);
    // A boulder: the sling.
    RopeEnd rock;
    rock.kind = ANCHOR_ROCK;
    rock.p = c.hookPoint(0) + V3(12, 0.5, 0);
    sim.anchorStock.n[GEAR_SLING] = 0;
    ctx.check("without a sling a boulder is refused", sim.hookWinchTo(ci, rock) == "NEEDS A CHAIN SLING");
    sim.anchorStock.n[GEAR_SLING] = 1;
    ctx.check("with one it hooks, and the sling stays in the pack", sim.hookWinchTo(ci, rock).rfind("WINCH HOOKED", 0) == 0 && sim.anchorStock.n[GEAR_SLING] == 1 && sim.anchorsFree(GEAR_SLING) == 0);
    // Another vehicle needs no gear.
    sim.dropWinch(ci);
    sim.anchorStock.fill(anchors::kCareerStart);
    for (int g = 0; g < GEAR_COUNT; g++) sim.anchorStock.n[g] = 0;
    Car& other = ctx.add(bronco(), c.frame().com.x + 12, c.frame().com.z, 0);
    (void)other;
    ctx.run(0.5);
    RopeEnd veh;
    veh.kind = ANCHOR_CAR;
    veh.car = (int)sim.cars.size() - 1;
    veh.hook = 1;
    ctx.check("another vehicle needs no gear", sim.hookWinchTo(ci, veh).rfind("WINCH HOOKED", 0) == 0);
}

SCENARIO(anchorAutoPick, "anchors/auto-pick", "The winch's auto-pick and the target list: a target you can't use stays listed with its reason; auto-pick skips it for one you can use, or says why it found nothing") {
    ctx.sim = Sim();
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 dome = mountainFeature("dome");
    World& w = ctx.sim.world;
    w.addRock(dome.x, dome.z + 2, V3(1.3, 1.1, 1.3), 1.0, 4242, 1);
    w.buildGrid();
    Car& c = ctx.add(bronco(), dome.x, dome.z - 27, -90);
    ctx.focus(c);
    ctx.run(1.0);
    Sim& sim = ctx.sim;
    const int ci = (int)(&c - sim.cars.data());
    // The boulder ahead, with a sling: it's the pick.
    auto targets = sim.winchTargets(ci);
    const WinchTarget* boulder = nullptr;
    for (const WinchTarget& t : targets)
        if (t.end.kind == ANCHOR_ROCK) boulder = &t;
    ctx.check("the boulder is a target", boulder != nullptr);
    ctx.check("usable with a sling", boulder && boulder->usable && boulder->gear == GEAR_SLING);
    // No sling: still listed, greyed, with the reason; the auto-pick won't take it.
    for (int g = 0; g < GEAR_COUNT; g++) sim.anchorStock.n[g] = 0;
    targets = sim.winchTargets(ci);
    boulder = nullptr;
    int unusable = 0;
    for (const WinchTarget& t : targets) {
        if (t.end.kind == ANCHOR_ROCK) boulder = &t;
        if (!t.usable) unusable++;
        ctx.check(std::string("a target that can't be used says why: ") + t.what, t.usable || *t.why);
    }
    ctx.check("without a sling the boulder is still listed, greyed", boulder && !boulder->usable && std::strcmp(boulder->why, "NEEDS A CHAIN SLING") == 0);
    const std::string msg = sim.hookWinch(ci);
    ctx.check("auto-pick skips it and says why", sim.ropeOn(ci, ROPE_WINCH) < 0 && !msg.empty() && msg != "NOTHING TO WINCH TO AHEAD", msg);
    // With the sling back it's hooked.
    sim.anchorStock.n[GEAR_SLING] = 1;
    const std::string ok = sim.hookWinch(ci);
    ctx.check("with the sling it hooks the boulder", sim.ropeOn(ci, ROPE_WINCH) >= 0 && sim.ropes[0].b.kind == ANCHOR_ROCK, ok);
    sim.dropWinch(ci);
    ctx.check("some targets are listed whatever you carry", unusable > 0 || !targets.empty());
    // The proving ground's poles: with no strap the auto-pick won't take one.
    ctx.sim = Sim();
    Car& d = ctx.add(bronco(), 50, 16, -90);
    ctx.focus(d);
    ctx.run(1.0);
    const int di = (int)(&d - ctx.sim.cars.data());
    ctx.sim.anchorStock.n[GEAR_STRAP] = 0;
    const std::string pick = ctx.sim.hookWinch(di);
    const int r = ctx.sim.ropeOn(di, ROPE_WINCH);
    ctx.check("no strap: the auto-pick doesn't take the pole", r < 0 || ctx.sim.ropes[r].b.kind != ANCHOR_TREE, pick);
    for (const WinchTarget& t : ctx.sim.winchTargets(di))
        if (t.end.kind == ANCHOR_TREE) {
            ctx.check("the pole is listed greyed: NEEDS A TREE STRAP", !t.usable && std::strcmp(t.why, "NEEDS A TREE STRAP") == 0);
            break;
        }
}

SCENARIO(anchorStations, "anchors/station-refill", "A sandbox station refills the anchor gear free (and so does a full reset); a career's doesn't, it's bought") {
    ctx.sim = Sim();
    Sim& sim = ctx.sim;
    ctx.check("the proving ground has a repair station", !sim.world.stations.empty());
    if (sim.world.stations.empty()) return;
    const Station& st = sim.world.stations[0];
    Car& c = ctx.add(bronco(), st.p.x, st.p.z, 0);
    ctx.focus(c);
    sim.player = (int)(&c - sim.cars.data());
    // Out in the field: used up.
    for (int g = 0; g < GEAR_COUNT; g++) sim.anchorStock.n[g] = 0;
    ctx.run(1.0);
    ctx.check("parked on the pad", c.station >= 0);
    bool full = true;
    for (int g = 0; g < GEAR_COUNT; g++) full &= sim.anchorStock.n[g] == anchors::kSandboxStart[g];
    ctx.check("a sandbox station gives the full set back, free", full, anchors::hudLine(sim.anchorStock));
    // A career buys its own.
    sim.anchorStock.refills = false;
    for (int g = 0; g < GEAR_COUNT; g++) sim.anchorStock.n[g] = 0;
    ctx.run(1.0);
    bool none = true;
    for (int g = 0; g < GEAR_COUNT; g++) none &= sim.anchorStock.n[g] == 0;
    ctx.check("a career's station doesn't hand them out", none, anchors::hudLine(sim.anchorStock));
    // A full reset in the sandbox.
    sim.anchorStock.refills = true;
    for (int g = 0; g < GEAR_COUNT; g++) sim.anchorStock.n[g] = 0;
    sim.cars[sim.player].spawnPos = V3(st.p.x + 60, st.p.y, st.p.z);
    sim.respawnPlayer();
    full = true;
    for (int g = 0; g < GEAR_COUNT; g++) full &= sim.anchorStock.n[g] == anchors::kSandboxStart[g];
    ctx.check("a full reset refills a sandbox", full, anchors::hudLine(sim.anchorStock));
}

SCENARIO(anchorCareer, "anchors/career", "A career carries its anchor gear: the starting set, bought off the station rack, found in supply caches, saved and loaded; an old save gets the starting set") {
    Career c;
    c.on = true;
    c.startFresh();
    AnchorStock a = c.anchorStock();
    bool start = !a.refills;
    for (int g = 0; g < GEAR_COUNT; g++) start &= a.n[g] == anchors::kCareerStart[g];
    ctx.check("a fresh career: 2 straps, 1 sling, 2 stakes, no bolts, no pickets", start && anchors::kCareerStart[GEAR_STRAP] == 2 && anchors::kCareerStart[GEAR_SLING] == 1 &&
                                                                                      anchors::kCareerStart[GEAR_STAKE] == 2 && anchors::kCareerStart[GEAR_BOLT] == 0 && anchors::kCareerStart[GEAR_ICE] == 0,
              anchors::hudLine(a));
    ctx.check("kept in the stock like the other gear", c.stock.count("ANCHOR:GROUND STAKE") && c.stock["ANCHOR:GROUND STAKE"] == 2 && !c.stock.count("ANCHOR:ROCK BOLT"));
    // The rack sells all five.
    int onRack = 0;
    const ShopItem* bolt = nullptr;
    for (const ShopItem& it : stationRack())
        for (int g = 0; g < GEAR_COUNT; g++)
            if (it.key == anchors::gearKey((AnchorGear)g)) {
                onRack++;
                ctx.check(std::string(anchors::gearName((AnchorGear)g)) + " on the rack, priced, with a model", it.price == anchors::kPrice[g] && mk::findModel(it.model) != nullptr, it.model);
                if (g == GEAR_BOLT) bolt = &it;
            }
    ctx.check("the rack sells every kind", onRack == GEAR_COUNT, std::to_string(onRack));
    if (bolt) {
        std::string why;
        const int cash = c.cash;
        ctx.check("buys a rock bolt", c.buyGear(*bolt, why) && c.cash == cash - bolt->price && c.anchorStock().n[GEAR_BOLT] == 1, why);
        c.cash = 0;
        ctx.check("short of cash: no bolt", !c.buyGear(*bolt, why) && why == "NOT ENOUGH CASH" && c.anchorStock().n[GEAR_BOLT] == 1, why);
    }
    // Used in the field: the sim's counts go back into the stock.
    a = c.anchorStock();
    a.n[GEAR_STAKE] = 0;
    a.n[GEAR_BOLT] = 0;
    c.setAnchorStock(a);
    ctx.check("used up: gone from the stock", !c.stock.count("ANCHOR:GROUND STAKE") && !c.stock.count("ANCHOR:ROCK BOLT") && c.anchorStock().n[GEAR_STRAP] == 2);
    // Caches hold some.
    int stakes = 0, bolts = 0, pickets = 0;
    for (int map : {MAP_FRONTIER, MAP_BACKCOUNTRY}) {
        const World w(map);
        for (const CacheSpot& s : cacheSpots(w))
            for (const std::string& g : s.gear) {
                stakes += g == anchors::gearKey(GEAR_STAKE), bolts += g == anchors::gearKey(GEAR_BOLT), pickets += g == anchors::gearKey(GEAR_ICE);
            }
    }
    ctx.check("supply caches hold stakes, and the scarce bolts and pickets too", stakes > 0 && bolts > 0 && pickets > 0 && bolts < stakes, std::to_string(stakes) + " " + std::to_string(bolts) + " " + std::to_string(pickets));
    Career d;
    d.on = true;
    d.startFresh();
    CacheSpot cache;
    cache.id = "TEST";
    cache.gear = {anchors::gearKey(GEAR_BOLT), anchors::gearKey(GEAR_BOLT)};
    cache.cash = 5;
    d.openCache(cache);
    ctx.check("opening one gives the anchors", d.anchorStock().n[GEAR_BOLT] == 2);
    // Save and load.
    d.grant(anchors::gearKey(GEAR_ICE));
    d.stock[anchors::gearKey(GEAR_STAKE)] = 7;
    const std::string path = "build/test-anchors-career.txt";
    ctx.check("saves", d.save(path));
    Career e;
    e.on = true;
    ctx.check("loads", e.load(path));
    ctx.check("the anchor stock round-trips", e.anchorStock() == d.anchorStock() && e.anchorStock().n[GEAR_STAKE] == 7 && e.anchorStock().n[GEAR_ICE] == 1 && e.anchorStock().n[GEAR_BOLT] == 2,
              anchors::hudLine(e.anchorStock()));
    // A save from before anchor gear: it gets the starting set.
    {
        FILE* f = std::fopen(path.c_str(), "w");
        std::fputs("cash 900\nown TOOL:JACK\nstock 2 REPAIR:CV AXLE\n", f);
        std::fclose(f);
    }
    Career old;
    old.on = true;
    ctx.check("loads an old save", old.load(path));
    start = true;
    for (int g = 0; g < GEAR_COUNT; g++) start &= old.anchorStock().n[g] == anchors::kCareerStart[g];
    ctx.check("an old save gets the starting set", start && old.cash == 900 && old.stock["REPAIR:CV AXLE"] == 2, anchors::hudLine(old.anchorStock()));
    std::remove(path.c_str());
    // A sandbox "career" is the sandbox set.
    Career sb;
    ctx.check("a sandbox: the sandbox set", sb.anchorStock().refills && sb.anchorStock().n[GEAR_BOLT] == 2);
}
