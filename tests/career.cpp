// Career mode: you own the factory parts and earn the rest; caches out on the trails, cash for
// jobs and medals, the stations' shop; and it all saves. Sandbox owns everything.
#include <cmath>
#include <cstdio>

#include "../src/career.h"
#include "../src/interact.h"
#include "../src/mods.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

int vehicleIndex(const char* id) {
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        if (std::string(vehicleCatalog()[v].id) == id) return v;
    return -1;
}

// The option in `vehicle`'s `slot` that is part `key` (-1 none).
int optionFor(int vehicle, int slot, const std::string& key) {
    for (int o = 0; o < (int)modOptions(vehicle, slot).size(); o++)
        if (modPartKey(vehicle, slot, o) == key) return o;
    return -1;
}

}  // namespace

SCENARIO(careerOwnership, "career/ownership", "Sandbox owns everything; a career owns each vehicle's factory parts and nothing else; a part is the same part on every vehicle") {
    Career sandbox;
    const int civic = vehicleIndex("civic"), blazer = vehicleIndex("blazer");
    bool all = true;
    for (int k = 0; k < MOD_SLOTS; k++)
        for (int o = 0; o < (int)modOptions(civic, k).size(); o++) all &= sandbox.owns(civic, k, o);
    ctx.check("sandbox: every part fits", all);
    Career c;
    c.on = true;
    c.startFresh();
    bool factory = true, nothingElse = true;
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        for (int k = 0; k < MOD_SLOTS; k++)
            for (int o = 0; o < (int)modOptions(v, k).size(); o++) {
                if (modIsFactory(v, k, o)) factory &= c.owns(v, k, o);
                else nothingElse &= !c.owns(v, k, o);
            }
    ctx.check("career: every vehicle's factory parts fit", factory);
    ctx.check("career: nothing else does, to begin with", nothingElse);
    // The Blazer's 454 is one part, wherever it's offered.
    const std::string bigBlock = modFindPart("blazer", MOD_ENGINE, "454 BIG BLOCK");
    const int onBlazer = optionFor(blazer, MOD_ENGINE, bigBlock), onCivic = optionFor(civic, MOD_ENGINE, bigBlock);
    ctx.check("the 454 is offered on the Blazer and on the Civic", onBlazer > 0 && onCivic > 0, bigBlock);
    ctx.check("its name", modPartTitle(bigBlock) == "BLAZER: 454 BIG BLOCK", modPartTitle(bigBlock));
    c.grant(bigBlock);
    ctx.check("found once, it fits both", c.owns(blazer, MOD_ENGINE, onBlazer) && c.owns(civic, MOD_ENGINE, onCivic));
    const std::string slicks = "TIRES:RACE SLICK";
    ctx.check("race slicks: the same part on every vehicle", optionFor(civic, MOD_TIRES, slicks) > 0 && optionFor(vehicleIndex("bronco"), MOD_TIRES, slicks) > 0);
}

SCENARIO(careerShop, "career/shop", "The stations sell everyday and serious parts (the serious ones dear), not the best ones; buying takes the cash and the part fits every vehicle") {
    Career c;
    c.on = true;
    c.startFresh();
    const int civic = vehicleIndex("civic"), bronco = vehicleIndex("bronco");
    const int street = optionFor(civic, MOD_TIRES, "TIRES:SNOW"), slick = optionFor(civic, MOD_TIRES, "TIRES:RACE SLICK");
    const int jeskoV8 = optionFor(civic, MOD_ENGINE, modFindPart("jesko", MOD_ENGINE, "RACE FUEL"));
    ctx.check("snow tires are everyday kit", Career::tier(civic, MOD_TIRES, street) == 1 && Career::price(civic, MOD_TIRES, street) > 0);
    ctx.check("race slicks are the best: not for sale", Career::tier(civic, MOD_TIRES, slick) == 3 && Career::price(civic, MOD_TIRES, slick) == 0);
    ctx.check("the Jesko's race V8: not for sale", jeskoV8 > 0 && Career::price(civic, MOD_ENGINE, jeskoV8) == 0);
    std::string why;
    const int cash0 = c.cash, p = Career::price(civic, MOD_TIRES, street);
    ctx.check("buys snow tires", c.buy(civic, MOD_TIRES, street, why) && c.cash == cash0 - p, why);
    const int onBronco = optionFor(bronco, MOD_TIRES, "TIRES:SNOW");
    ctx.check("...and they fit the Bronco too", onBronco > 0 && c.owns(bronco, MOD_TIRES, onBronco));
    ctx.check("can't buy them twice", !c.buy(civic, MOD_TIRES, street, why), why);
    // Something dear: more than is left.
    int dear = -1;
    for (int o = 1; o < (int)modOptions(civic, MOD_ENGINE).size() && dear < 0; o++)
        if (!c.owns(civic, MOD_ENGINE, o) && Career::price(civic, MOD_ENGINE, o) > c.cash) dear = o;
    ctx.check("short of cash: no sale", dear > 0 && !c.buy(civic, MOD_ENGINE, dear, why) && why == "NOT ENOUGH CASH", why);
}

SCENARIO(careerRewards, "career/rewards", "A job pays; each medal pays once per level and a first gold brings one of the best parts") {
    Career c;
    c.on = true;
    c.startFresh();
    const int cash0 = c.cash;
    ctx.check("a job pays", !c.jobDone().empty() && c.cash == cash0 + Career::kJobPay);
    const int cash1 = c.cash;
    const std::string bronze = c.medal("HILL CLIMB", 1);
    ctx.check("a bronze pays", !bronze.empty() && c.cash > cash1, bronze);
    const size_t owned0 = c.owned.size();
    const int cash2 = c.cash;
    const std::string gold = c.medal("HILL CLIMB", 3);
    ctx.check("then gold pays silver and gold, and a part", c.cash > cash2 && c.owned.size() == owned0 + 1, gold);
    ctx.check("medals already won don't pay again", c.medal("HILL CLIMB", 3).empty());
}

SCENARIO(careerSave, "career/save", "A career saves and loads: cash, parts, opened caches, rewards") {
    Career c;
    c.on = true;
    c.startFresh();
    c.cash = 4321;
    c.grant(modFindPart("blazer", MOD_ENGINE, "454 BIG BLOCK"));
    c.grant("TIRES:MUD-TERRAIN");
    c.opened.insert("3:SUMMIT");
    c.claimed.insert("LEVEL:HILL CLIMB:GOLD");
    const std::string path = "build/test-career.txt";
    ctx.check("saves", c.save(path));
    Career d;
    ctx.check("loads", d.load(path));
    ctx.check("the same career", d.cash == c.cash && d.owned == c.owned && d.opened == c.opened && d.claimed == c.claimed);
    std::remove(path.c_str());
}

SCENARIO(careerCaches, "career/caches", "Supply caches on the Frontier and in the Backcountry: each holds a real part or cash, on dry ground, apart; one is opened on foot (hold E) and pays out once") {
    for (int map : {MAP_FRONTIER, MAP_BACKCOUNTRY}) {
        ctx.sim = Sim();
        ctx.sim.world = World(map);
        const World& w = ctx.sim.world;
        const std::vector<CacheSpot> spots = cacheSpots(w);
        const std::string name = mapName(map);
        ctx.check(name + ": ten caches", spots.size() == 10, std::to_string(spots.size()));
        bool contents = true, dry = true, apart = true, onMap = true;
        std::string bad;
        for (size_t i = 0; i < spots.size(); i++) {
            const CacheSpot& s = spots[i];
            std::printf("  %s %s at (%.0f, %.0f): %s\n", name.c_str(), s.id.c_str(), s.at.x, s.at.z, s.part.empty() ? ("$" + std::to_string(s.cash)).c_str() : modPartTitle(s.part).c_str());
            if (s.part.empty() ? s.cash <= 0 : modPartTitle(s.part).empty()) contents = false, bad += " " + s.id;
            if (w.waterLevel(s.at.x, s.at.z) > s.at.y - 0.1) dry = false, bad += " wet:" + s.id;
            if (std::fabs(s.at.x) > 590 || std::fabs(s.at.z) > 590) onMap = false;
            for (size_t j = 0; j < i; j++)
                if (len(spots[j].at - s.at) < 30) apart = false, bad += " near:" + s.id;
            for (const Station& st : w.stations)
                if (std::hypot(st.p.x - s.at.x, st.p.z - s.at.z) < 25) apart = false, bad += " station:" + s.id;
        }
        ctx.check(name + ": every cache holds a real part or cash", contents, bad);
        ctx.check(name + ": on dry ground", dry, bad);
        ctx.check(name + ": on the map, apart, and off the stations' pads", onMap && apart, bad);
    }
    // On foot at the Frontier's summit: hold E at the crate.
    ctx.sim = Sim();
    ctx.sim.world = World(MAP_FRONTIER);
    const std::vector<CacheSpot> spots = cacheSpots(ctx.sim.world);
    const CacheSpot& summit = spots[1];
    Career c;
    c.on = true;
    c.startFresh();
    Car& car = ctx.add(vehicleCatalog()[2], summit.at.x + 12, summit.at.z, 0);
    ctx.run(0.5);
    Walker wk;
    wk.exitCar(ctx.sim.world, car);
    wk.p = summit.at + V3(-1.2, 0, 0);
    wk.p.y = ctx.sim.world.height(wk.p.x, wk.p.z);
    Interact ia;
    ia.spots.push_back({summit.id, summit.at, "OPEN THE SUPPLY CACHE"});
    const V3 eye = wk.p + V3(0, 1.6, 0);
    double held = 0;
    for (; held < 5 && ia.openedSpot.empty(); held += Ctx::kFrame) ia.update(ctx.sim, wk, eye, summit.at + V3(0, 0.5, 0) - eye, true, Ctx::kFrame);
    ctx.check("hold E at the crate: it opens", ia.openedSpot == summit.id, "held " + std::to_string(held) + " s");
    const std::string said = c.openCache(summit);
    ctx.check("its part is yours", said.rfind("FOUND: ", 0) == 0 && c.owned.count(summit.part), said);
    ctx.check("opened once only", c.openCache(summit).empty() && c.isOpened(summit));
}
