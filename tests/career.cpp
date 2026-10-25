// Career mode: you own the factory parts and earn the rest; caches out on the trails, cash for
// jobs and medals, the stations' shop; and it all saves. Sandbox owns everything.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "../src/career.h"
#include "../src/interact.h"
#include "../src/careerworld.h"
#include "../src/mods.h"
#include "../src/platform.h"
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

namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
void spit(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    f << text;
}
// A fresh folder under build/ for one scenario (the tests are threads of one process, so each has its own).
std::string freshDir(const std::string& name) {
    makeDir("build");
    makeDir("build/test-career");
    const std::string dir = "build/test-career/" + name;
    makeDir(dir);
    for (const char* f : {"/profile.txt", "/profile.txt.bak", "/profile.txt.tmp", "/career.txt"}) std::remove((dir + f).c_str());
    return dir;
}

}  // namespace

SCENARIO(careerProfileMigrate, "career/profile-migrate", "With no profile.txt, loadProfile takes the old career.txt; saveProfile then writes profile.txt and leaves career.txt alone") {
    const std::string dir = freshDir("migrate");
    ScopedCareerDir scope(dir);
    const std::string old = "cash 4321\nown TIRES:RACE SLICK\ncache 3:WALL TOP\n";
    spit(dir + "/career.txt", old);
    Career c;
    ctx.check("loads", c.loadProfile());
    ctx.check("the old cash", c.cash == 4321);
    ctx.check("owns the slick", c.owned.count("TIRES:RACE SLICK") == 1);
    ctx.check("the cache is opened", c.opened.count("3:WALL TOP") == 1);
    ctx.check("saves", c.saveProfile());
    ctx.check("profile.txt is there", !slurp(dir + "/profile.txt").empty());
    ctx.check("career.txt is untouched", slurp(dir + "/career.txt") == old);
    Career d;
    ctx.check("and loads back", d.loadProfile() && d.cash == 4321 && d.opened == c.opened && d.owned == c.owned);
    Career none;
    {
        ScopedCareerDir empty(freshDir("migrate-none"));
        ctx.check("nothing at all: false", !none.loadProfile());
    }
}

SCENARIO(careerProfileRoundtrip, "career/profile-roundtrip", "Owned vehicles with mods, worlds, conditions and when they were last driven save and load back the same") {
    const std::string dir = freshDir("roundtrip");
    ScopedCareerDir scope(dir);
    Career c;
    c.startFresh(DIFF_HARDCORE);
    c.cash = 777;
    OwnedVehicle a, b;
    a.vehicle = vehicleIndex("blazer");
    a.mods.pick[MOD_ENGINE] = 1;
    a.mods.pick[MOD_WHEELS] = (int)modOptions(a.vehicle, MOD_WHEELS).size() - 1;
    a.world = "my-world";
    a.condition = "dent 3 0.5\nline two with \\ slash";
    a.drove = 1234.5;
    b.vehicle = vehicleIndex("civic");
    b.world = "second-1";
    c.vehicles = {a, b};
    ctx.check("saves", c.saveProfile());
    Career d;
    ctx.check("loads", d.loadProfile());
    bool same = d.vehicles.size() == 2 && d.cash == 777 && d.owned == c.owned && d.stock == c.stock;
    for (size_t i = 0; same && i < 2; i++) {
        const OwnedVehicle &x = c.vehicles[i], &y = d.vehicles[i];
        same &= x.vehicle == y.vehicle && x.world == y.world && x.condition == y.condition && x.drove == y.drove;
        for (int k = 0; k < MOD_SLOTS; k++) same &= x.mods.pick[k] == y.mods.pick[k];
    }
    ctx.check("the same profile", same);
}

SCENARIO(careerProfileBak, "career/profile-bak", "A corrupt profile.txt falls back to profile.txt.bak, and a second save keeps the previous good profile as the .bak") {
    const std::string dir = freshDir("bak");
    ScopedCareerDir scope(dir);
    Career c;
    c.startFresh();
    c.cash = 111;
    ctx.check("first save", c.saveProfile());
    c.cash = 222;
    ctx.check("second save", c.saveProfile());
    ctx.check("the .bak is the first", slurp(dir + "/profile.txt.bak").find("cash 111") != std::string::npos);
    spit(dir + "/profile.txt", "x#!\x01z");
    Career d;
    ctx.check("loads", d.loadProfile());
    ctx.check("from the .bak", d.cash == 111, std::to_string(d.cash));
    d.cash = 333;
    ctx.check("saving over the corrupt file", d.saveProfile());
    ctx.check("a bad file doesn't displace the good .bak", slurp(dir + "/profile.txt.bak").find("cash 111") != std::string::npos);
}

SCENARIO(careerStartCash, "career/start-cash", "A new profile starts with the difficulty's cash and the starting tools") {
    Career c;
    c.startFresh(DIFF_LIGHT);
    ctx.check("light", c.cash == 2000);
    c.startFresh(DIFF_MEDIUM);
    ctx.check("medium", c.cash == 1000);
    c.startFresh(DIFF_HARDCORE);
    ctx.check("hardcore", c.cash == 500 && c.owned.size() == 2 && c.vehicles.empty());
    c.startFresh();
    ctx.check("default is medium", c.cash == 1000);
}

SCENARIO(careerProfileBadVehicle, "career/profile-bad-vehicle", "A veh line that can't be read (bad catalog index, malformed) is skipped without moving later vehicles' conditions onto the wrong car") {
    const std::string dir = freshDir("badveh");
    ScopedCareerDir scope(dir);
    const int civic = vehicleIndex("civic"), blazer = vehicleIndex("blazer");
    std::string text = "cash 5\nveh " + std::to_string(civic) + " w 0 0 0 0 0\ncond 0 first\nveh 99999 w 0 0 0 0 0\ncond 1 lost\nveh oops\nveh " +
                       std::to_string(blazer) + " w 0 0 0 0 0\ncond 3 third\nfuture-key whatever\n";
    spit(dir + "/profile.txt", text);
    Career c;
    ctx.check("loads", c.loadProfile());
    ctx.check("two vehicles survive", c.vehicles.size() == 2);
    ctx.check("each keeps its own condition", c.vehicles.size() == 2 && c.vehicles[0].vehicle == civic && c.vehicles[0].condition == "first" &&
              c.vehicles[1].vehicle == blazer && c.vehicles[1].condition == "third");
    ctx.check("resaved, they still match", c.saveProfile());
    Career d;
    ctx.check("reloaded", d.loadProfile() && d.vehicles.size() == 2 && d.vehicles[0].condition == "first" && d.vehicles[1].condition == "third");
}
