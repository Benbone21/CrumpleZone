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
#include "../src/onemap.h"
#include "../src/repairdeal.h"
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
                else if (!modOptions(v, k)[o].removal) nothingElse &= !c.owns(v, k, o);  // (taking the factory winch off is free)
            }
    ctx.check("career: every vehicle's factory parts fit", factory);
    ctx.check("career: nothing else does, to begin with", nothingElse);
    // The Blazer's big V8 is its own part; the wheel sizes and treads are the same part on every vehicle.
    const std::string bigBlock = modFindPart("blazer", MOD_ENGINE, "BIG V8");
    const int onBlazer = optionFor(blazer, MOD_ENGINE, bigBlock);
    ctx.check("the big V8 is offered on the Blazer, not the Civic", onBlazer > 0 && optionFor(civic, MOD_ENGINE, bigBlock) < 0, bigBlock);
    ctx.check("its name", modPartTitle(bigBlock) == "BLAZER: BIG V8", modPartTitle(bigBlock));
    c.grant(bigBlock);
    ctx.check("found, it fits", c.owns(blazer, MOD_ENGINE, onBlazer));
    const std::string slicks = "TIRES:RACE SLICK";
    ctx.check("race tires: the same part on every vehicle", optionFor(civic, MOD_TIRES, slicks) > 0 && optionFor(vehicleIndex("bronco"), MOD_TIRES, slicks) > 0);
    ctx.check("their name", modPartTitle(slicks) == "RACE TIRES", modPartTitle(slicks));
    const std::string huge = "WHEELS:buggy:0";
    const int hugeCivic = optionFor(civic, MOD_WHEELS, huge), hugeBlazer = optionFor(blazer, MOD_WHEELS, huge);
    c.grant(huge);
    ctx.check("huge wheels: one part, found once, fit both", hugeCivic > 0 && hugeBlazer > 0 && c.owns(civic, MOD_WHEELS, hugeCivic) && c.owns(blazer, MOD_WHEELS, hugeBlazer) &&
                                                                 modPartTitle(huge) == "HUGE WHEELS", modPartTitle(huge));
}

SCENARIO(partsSimple, "career/parts-simple", "Every slot of every vehicle: the factory part and two or three more (an attachment: on or off), each named plainly and differently, with no part numbers") {
    bool sizes = true, distinct = true, plain = true;
    std::string bad;
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        for (int k = 0; k < MOD_SLOTS; k++) {
            const auto& o = modOptions(v, k);
            const std::string where = std::string(vehicleCatalog()[v].id) + " " + modSlotName(k);
            // (The attachments are on or off: two options, tests/attachments.cpp.)
            if (modIsAttachment(k) ? o.size() != 2 : (o.size() < 3 || o.size() > 4)) sizes = false, bad += where + " has " + std::to_string(o.size()) + "; ";
            for (size_t i = 0; i < o.size(); i++) {
                for (size_t j = 0; j < i; j++)
                    if (o[i].name == o[j].name) distinct = false, bad += where + " twice: " + o[i].name + "; ";
                // (A plain name is short and has no codes: a word with letters and digits in it is a count, "3-SPEED",
                // a V and its cylinders, "V8", or a part number, "ZZ4", "B18C".)
                bool coded = o[i].name.size() > 20;
                std::string word;
                for (char ch : o[i].name + " ") {
                    if (ch != ' ') {
                        word += ch;
                        continue;
                    }
                    bool digit = false, letter = false;
                    for (char w : word) digit |= w >= '0' && w <= '9', letter |= w >= 'A' && w <= 'Z';
                    const bool vee = word.size() == 2 && word[0] == 'V' && word[1] >= '0' && word[1] <= '9';
                    if (digit && letter && word.find('-') == std::string::npos && !vee) coded = true;
                    word.clear();
                }
                if (coded) plain = false, bad += where + ": " + o[i].name + "; ";
            }
        }
    ctx.check("3 or 4 options in every slot (2 for the winch and the bumper)", sizes, bad);
    ctx.check("each named differently", distinct, bad);
    ctx.check("plain names (no part numbers, 20 letters at most)", plain, bad);
}

SCENARIO(partsMigrate, "career/parts-migrate", "Saves from before parts list 2 load: a garage, a career's owned parts and its vehicles, and a car's condition text each get the nearest part today") {
    const int civic = vehicleIndex("civic"), blazer = vehicleIndex("blazer"), bronco = vehicleIndex("bronco"), runner = vehicleIndex("4runner");
    auto pickNamed = [](int v, int k, const char* name) {
        for (int o = 0; o < (int)modOptions(v, k).size(); o++)
            if (modOptions(v, k)[o].name == name) return o;
        return -1;
    };
    // The old lists, one by one: every old pick maps onto a real option today, and a part still there is itself.
    const int oldSizes[MOD_SLOTS] = {31, 32, 32, 9, 6};
    bool real = true, kept = true;
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        for (int k = 0; k < MOD_SLOTS; k++)
            for (int o = 0; o < oldSizes[k]; o++) {
                const std::string old = modLegacyKey(v, k, o);
                const int now = modLegacyPick(v, k, o);
                real &= !old.empty() && now >= 0 && now < (int)modOptions(v, k).size();
                for (int n = 0; n < (int)modOptions(v, k).size(); n++)
                    if (modPartKey(v, k, n) == old) kept &= now == n;
            }
    ctx.check("every old pick has a part today", real);
    ctx.check("a part that's still there is still itself", kept);
    // The Blazer's old engine list: 0 stock, 1 the 350 four-barrel (cut), 2 the 454, 3 the ZZ4, then the Civic's four, the Jesko's three...
    ctx.check("old Blazer engines: the 454 is the BIG V8, the four-barrel (cut) the BIG V8 too, the ZZ4 the HOT ROD V8",
              modLegacyPick(blazer, MOD_ENGINE, 2) == pickNamed(blazer, MOD_ENGINE, "BIG V8") && modLegacyPick(blazer, MOD_ENGINE, 1) == pickNamed(blazer, MOD_ENGINE, "BIG V8") &&
                  modLegacyPick(blazer, MOD_ENGINE, 3) == pickNamed(blazer, MOD_ENGINE, "HOT ROD V8"));
    ctx.check("the Jesko's V8 swapped into the Blazer: its strongest engine", modLegacyKey(blazer, MOD_ENGINE, 8) == "ENGINE:jesko:0" &&
                                                                                 modLegacyPick(blazer, MOD_ENGINE, 8) == pickNamed(blazer, MOD_ENGINE, "HOT ROD V8"));
    ctx.check("the Civic's 1.6 swapped into the Blazer: its factory engine", modLegacyKey(blazer, MOD_ENGINE, 4) == "ENGINE:civic:0" && modLegacyPick(blazer, MOD_ENGINE, 4) == 0);
    ctx.check("another truck's wheels: the nearest size (the buggy's: HUGE WHEELS)", modLegacyKey(blazer, MOD_WHEELS, 5) == "WHEELS:buggy:0" &&
                                                                                      modLegacyPick(blazer, MOD_WHEELS, 5) == pickNamed(blazer, MOD_WHEELS, "HUGE WHEELS"));
    ctx.check("another truck's suspension: the factory one", modLegacyPick(runner, MOD_SUSPENSION, 13) == 0);
    ctx.check("snow tires (cut): all-terrains, here the factory tread", modLegacyKey(blazer, MOD_TIRES, 5) == "TIRES:SNOW" && modLegacyPick(blazer, MOD_TIRES, 5) == 0 &&
                                                                         modLegacyPick(civic, MOD_TIRES, 5) == pickNamed(civic, MOD_TIRES, "ALL-TERRAIN TIRES"));
    ctx.check("semi-slicks (cut): street tires", modLegacyPick(blazer, MOD_TIRES, 2) == pickNamed(blazer, MOD_TIRES, "STREET TIRES"));
    ctx.check("cut parts' keys go to the nearest one; others stay", modMigrateKey("ENGINE:blazer:1") == "ENGINE:blazer:2" && modMigrateKey("TIRES:SNOW") == "TIRES:ALL-TERRAIN" &&
                                                                       modMigrateKey("TIRES:SEMI-SLICK") == "TIRES:STREET" && modMigrateKey("ENGINE:blazer:3") == "ENGINE:blazer:3" &&
                                                                       modMigrateKey("TIRES:RACE SLICK") == "TIRES:RACE SLICK" && modMigrateKey("TOOL:HAMMER") == "TOOL:HAMMER");

    // An old garage.txt (no "parts" line): the Blazer on the 454, the 4Runner's long travel, the Bronco's 4.70 gears (cut) and buggy wheels.
    makeDir("build/test-career");
    const std::string garagePath = "build/test-career/old-garage.txt";
    {
        std::ofstream f(garagePath);
        f << "blazer 2 1 2 5 2\n4runner 0 2 1 0 0\nbronco 0 0 1 5 0\n";
    }
    Garage g;
    ctx.check("an old garage loads", g.load(garagePath));
    ctx.check("the Blazer: BIG V8, LIFT KIT, LOCKERS, HUGE WHEELS, STREET TIRES",
              g.fitted[blazer].pick[MOD_ENGINE] == pickNamed(blazer, MOD_ENGINE, "BIG V8") && g.fitted[blazer].pick[MOD_SUSPENSION] == pickNamed(blazer, MOD_SUSPENSION, "LIFT KIT") &&
                  g.fitted[blazer].pick[MOD_TRANSMISSION] == pickNamed(blazer, MOD_TRANSMISSION, "LOCKERS") &&
                  g.fitted[blazer].pick[MOD_WHEELS] == pickNamed(blazer, MOD_WHEELS, "HUGE WHEELS") && g.fitted[blazer].pick[MOD_TIRES] == pickNamed(blazer, MOD_TIRES, "STREET TIRES"));
    ctx.check("the 4Runner: BIG LIFT, and the 4.88 regear (cut) is LOCKERS", g.fitted[runner].pick[MOD_SUSPENSION] == pickNamed(runner, MOD_SUSPENSION, "BIG LIFT") &&
                                                                              g.fitted[runner].pick[MOD_TRANSMISSION] == pickNamed(runner, MOD_TRANSMISSION, "LOCKERS"));
    ctx.check("the Bronco: its 4.70 gears (cut) are LOCKERS", g.fitted[bronco].pick[MOD_TRANSMISSION] == pickNamed(bronco, MOD_TRANSMISSION, "LOCKERS"));
    ctx.check("saved again, it says which lists", g.save(garagePath));
    Garage h;
    bool same = h.load(garagePath);
    for (size_t v = 0; v < g.fitted.size(); v++)
        for (int k = 0; k < MOD_SLOTS; k++) same &= h.fitted[v].pick[k] == g.fitted[v].pick[k];
    ctx.check("and loads back the same (not migrated twice)", same);
    std::remove(garagePath.c_str());

    // An old career: owned parts that were cut, and a vehicle whose picks are in the old lists.
    std::istringstream oldText("cash 500\nown TIRES:SNOW\nown ENGINE:blazer:1\nown TIRES:RACE SLICK\nveh " + std::to_string(blazer) + " my-world 8 0 0 0 0\n");
    const std::string careerPath = "build/test-career/old-career.txt";
    {
        std::ofstream f(careerPath);
        f << oldText.str();
    }
    Career c;
    c.on = true;
    ctx.check("an old career loads", c.load(careerPath));
    ctx.check("its cut parts are their nearest ones", c.owned.count("TIRES:ALL-TERRAIN") && c.owned.count("ENGINE:blazer:2") && c.owned.count("TIRES:RACE SLICK") &&
                                                          !c.owned.count("TIRES:SNOW") && !c.owned.count("ENGINE:blazer:1"));
    ctx.check("its Blazer had the Jesko's V8: now its HOT ROD V8, and that's owned",
              c.vehicles.size() == 1 && c.vehicles[0].mods.pick[MOD_ENGINE] == pickNamed(blazer, MOD_ENGINE, "HOT ROD V8") && c.owns(blazer, MOD_ENGINE, c.vehicles[0].mods.pick[MOD_ENGINE]));
    std::remove(careerPath.c_str());
}

SCENARIO(careerShop, "career/shop", "The stations sell everyday and serious parts (the serious ones dear), not the best ones; buying takes the cash and the part fits every vehicle") {
    Career c;
    c.on = true;
    c.startFresh();
    const int civic = vehicleIndex("civic"), bronco = vehicleIndex("bronco");
    const int street = optionFor(civic, MOD_TIRES, "TIRES:ALL-TERRAIN"), slick = optionFor(civic, MOD_TIRES, "TIRES:RACE SLICK");
    const int jesko = vehicleIndex("jesko"), jeskoV8 = optionFor(jesko, MOD_ENGINE, modFindPart("jesko", MOD_ENGINE, "RACE TUNE"));
    ctx.check("all-terrain tires are everyday kit", Career::tier(civic, MOD_TIRES, street) == 1 && Career::price(civic, MOD_TIRES, street) > 0);
    ctx.check("race tires are the best: not for sale", Career::tier(civic, MOD_TIRES, slick) == 3 && Career::price(civic, MOD_TIRES, slick) == 0);
    ctx.check("the Jesko's race tune: not for sale", jeskoV8 > 0 && Career::price(jesko, MOD_ENGINE, jeskoV8) == 0);
    std::string why;
    const int cash0 = c.cash, p = Career::price(civic, MOD_TIRES, street);
    ctx.check("buys all-terrain tires", c.buy(civic, MOD_TIRES, street, why) && c.cash == cash0 - p, why);
    const int onJesko = optionFor(jesko, MOD_TIRES, "TIRES:ALL-TERRAIN");
    ctx.check("...and they fit the Jesko too", onJesko > 0 && c.owns(jesko, MOD_TIRES, onJesko));
    (void)bronco;
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
        // (The Backcountry: the ten hand-made ones and fifty from its places (places.h); on the big map, the Frontier's ten in its region
        // besides, and the twelve of the regions' grown country (regions2: four each).)
        // The trail network (bctrails.cpp) adds eleven at its hardest spots, on either map.
        const size_t want = map == MAP_BACKCOUNTRY ? (w.regionStarts.empty() ? 71 : 93) : 10;
        ctx.check(name + ": " + std::to_string(want) + " caches", spots.size() == want, std::to_string(spots.size()));
        if (map == MAP_BACKCOUNTRY && !w.regionStarts.empty()) {
            // The Frontier's own, still the Frontier's (an opened one stays opened), where they were on its map, moved over.
            World fr(MAP_FRONTIER);
            const std::vector<CacheSpot> own = cacheSpots(fr);
            int same = 0;
            for (const CacheSpot& o : own)
                for (const CacheSpot& s : spots)
                    same += s.id == o.id && s.map == MAP_FRONTIER && std::hypot(s.at.x - regionToWorld(MAP_FRONTIER, o.at).x, s.at.z - regionToWorld(MAP_FRONTIER, o.at).z) < 0.01;
            ctx.check("the Frontier's caches in its region, still the Frontier's", same == (int)own.size() && !own.empty(), std::to_string(same));
        }
        bool contents = true, dry = true, apart = true, onMap = true;
        std::string bad;
        for (size_t i = 0; i < spots.size(); i++) {
            const CacheSpot& s = spots[i];
            std::printf("  %s %s at (%.0f, %.0f): %s\n", name.c_str(), s.id.c_str(), s.at.x, s.at.z, s.part.empty() ? ("$" + std::to_string(s.cash)).c_str() : modPartTitle(s.part).c_str());
            if (s.part.empty() ? s.cash <= 0 : modPartTitle(s.part).empty()) contents = false, bad += " " + s.id;
            if (w.waterLevel(s.at.x, s.at.z) > s.at.y - 0.1) dry = false, bad += " wet:" + s.id;
            if (std::fabs(s.at.x) > w.halfSize() - 10 || std::fabs(s.at.z) > w.halfSize() - 10) onMap = false;
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

SCENARIO(careerProfileBrokenOld, "career/profile-broken-old", "A broken or empty old career.txt isn't migrated as $0 with no tools; a damaged profile.txt doesn't fall back to career.txt") {
    const std::string dir = freshDir("broken-old");
    ScopedCareerDir scope(dir);
    spit(dir + "/career.txt", "");
    Career c;
    ctx.check("an empty career.txt: nothing loaded", !c.loadProfile());
    ctx.check("Career::load says so too", !c.load(dir + "/career.txt"));
    spit(dir + "/career.txt", "cash 4321\n");
    spit(dir + "/profile.txt", "garbage\n");
    Career d;
    ctx.check("a damaged profile.txt: not the old career.txt instead", !d.loadProfile() && d.cash != 4321, std::to_string(d.cash));
    Career e;
    e.cash = 9;
    e.loadProfile();
    ctx.check("a failed load leaves the career as it was", e.cash == 9);
}

SCENARIO(careerRepairDeal, "career/repair-deal", "Career R costs: repairing on the spot is dearer than respawning at a station, both grow with the damage, and a player who can't pay (or has no station) is refused with the cash untouched") {
    using namespace repairdeal;
    ctx.check("on the spot costs more than the station at every damage",
              price(SPOT, 0) > price(STATION, 0) && price(SPOT, 0.25) > price(STATION, 0.25) && price(SPOT, 0.5) > price(STATION, 0.5) && price(SPOT, 5) > price(STATION, 5));
    ctx.check("both grow with the damage", price(SPOT, 0.1) < price(SPOT, 0.4) && price(STATION, 0.1) < price(STATION, 0.4));
    ctx.check("capped at a full damage bar", price(SPOT, 0.5) == price(SPOT, 3) && price(STATION, 0.5) == price(STATION, 3));
    ctx.check("bad damage values are priced as none", price(SPOT, -1) == price(SPOT, 0) && price(SPOT, NAN) == price(SPOT, 0));
    ctx.check("nothing is free", price(STATION, 0) > 0);

    Career c;
    c.startFresh(DIFF_MEDIUM);
    c.on = true;
    std::string why;
    const int spot = price(SPOT, 0.3), stat = price(STATION, 0.3);
    ctx.check("paid on the spot", pay(c, SPOT, 0.3, true, why) && c.cash == 1000 - spot, std::to_string(c.cash) + " " + why);
    ctx.check("paid at the station", pay(c, STATION, 0.3, true, why) && c.cash == 1000 - spot - stat, std::to_string(c.cash) + " " + why);

    c.cash = stat;  // exactly enough for the station, short of the spot
    ctx.check("can't afford the spot repair", !pay(c, SPOT, 0.3, true, why) && c.cash == stat && why.find("AFFORD") != std::string::npos, why);
    ctx.check("exact cash is enough", pay(c, STATION, 0.3, true, why) && c.cash == 0, why);
    ctx.check("broke: refused, still $0", !pay(c, STATION, 0.3, true, why) && c.cash == 0);

    c.cash = 5000;
    ctx.check("no station on the map: the respawn is refused, free", !pay(c, STATION, 0.3, false, why) && c.cash == 5000 && why.find("NO REPAIR STATION") != std::string::npos, why);
    ctx.check("the spot repair needs no station", pay(c, SPOT, 0.3, false, why) && c.cash == 5000 - spot);

    // Hardcore's $500: a wreck can still be towed, but not repaired by a mechanic on the trail.
    c.startFresh(DIFF_HARDCORE);
    ctx.check("hardcore, wrecked: the tow is affordable, the call-out is not", price(STATION, 0.5) <= c.cash && price(SPOT, 0.5) > c.cash);
    ctx.check("the lines show the prices", lineSpot(0.3).find("$" + std::to_string(spot)) != std::string::npos && lineStation(0.3).find("$" + std::to_string(stat)) != std::string::npos);
}
