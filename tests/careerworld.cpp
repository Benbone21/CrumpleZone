// Career worlds: difficulty start cash, which vehicles can start a career, folder names; and a world being played
// (careersession.h): started, continued, autosaved, and nothing of it in sandbox.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "../src/career.h"
#include "../src/careersession.h"
#include "../src/careerworld.h"
#include "../src/carsave.h"
#include "../src/garageshop.h"
#include "../src/net.h"
#include "../src/platform.h"
#include "../src/vehicles.h"
#include "../src/world.h"
#include "harness.h"

SCENARIO(careerWorldSettings, "career/world-settings", "Start cash by difficulty; the Jesko can't be a starter; world slugs are folder-safe, unique and short") {
    ctx.check("light starts with 2000", startCash(DIFF_LIGHT) == 2000);
    ctx.check("medium starts with 1000", startCash(DIFF_MEDIUM) == 1000);
    ctx.check("hardcore starts with 500", startCash(DIFF_HARDCORE) == 500);
    ctx.check("difficulty name", std::string(difficultyName(DIFF_HARDCORE)) == "HARDCORE");
    ctx.check("crew mode name", std::string(modeName(MODE_CREW)) == "SHARED CREW");
    ctx.check("personal mode name", std::string(modeName(MODE_PERSONAL)) == "PERSONAL");

    int jesko = -1;
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        if (std::string(vehicleCatalog()[v].name) == "KOENIGSEGG JESKO") jesko = v;
    ctx.check("the catalog has the Jesko", jesko >= 0);
    ctx.check("the Jesko is not a starter", !startableVehicle(jesko));
    bool allOthers = true;
    for (int v : listedVehicles())
        if (v != jesko && !startableVehicle(v)) allOthers = false;
    ctx.check("every other listed vehicle is a starter", allOthers);

    ctx.check("plain name", worldSlug("My World", {}) == "my-world");
    ctx.check("taken gets -2", worldSlug("my world!", {"my-world"}) == "my-world-2");
    ctx.check("taken twice gets -3", worldSlug("my world!", {"my-world", "my-world-2"}) == "my-world-3");
    ctx.check("empty name", worldSlug("", {}) == "world");
    ctx.check("long name is cut", worldSlug(std::string(50, 'a'), {}).size() <= 32);
    ctx.check("long taken name stays short", worldSlug(std::string(50, 'a'), {std::string(32, 'a')}).size() <= 32);
}

namespace {

std::string freshDir(const std::string& name) {
    makeDir("build");
    makeDir("build/test-career");
    const std::string dir = "build/test-career/" + name;
    removeTree(dir);
    makeDir(dir);
    return dir;
}
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
bool same(const WorldSave& a, const WorldSave& b) {
    if (a.name != b.name || a.slug != b.slug || a.map != b.map || a.diff != b.diff || a.mode != b.mode || a.build != b.build ||
        a.opened != b.opened || a.playedSeconds != b.playedSeconds || a.damaged != b.damaged || a.left.size() != b.left.size())
        return false;
    for (size_t i = 0; i < a.left.size(); i++)
        if (a.left[i].owner != b.left[i].owner || a.left[i].vehicle != b.left[i].vehicle || a.left[i].condition != b.left[i].condition) return false;
    return true;
}

}  // namespace

SCENARIO(careerWorldSave, "career/world-save", "A world with left vehicles and opened caches saves, loads back equal and is listed") {
    ScopedCareerDir scope(freshDir("world-save"));
    WorldSave w = newWorld("My World", 2, DIFF_HARDCORE, MODE_CREW, {});
    w.left = {{"Ann", 3, "dent 3 0.5\nline two with \\ slash"}, {"Bo B", 1, ""}};
    w.opened = {"3:WALL TOP", "1:CLIFF"};
    w.playedSeconds = 1234.5;
    ctx.check("build is this release", w.build == net::kRelease);
    ctx.check("saves", saveWorld(w));
    WorldSave r;
    ctx.check("loads", loadWorld(w.slug, r));
    ctx.check("equal", same(w, r));
    const auto all = listWorlds();
    ctx.check("listed", all.size() == 1 && all[0].slug == w.slug && same(all[0], w));
}

SCENARIO(careerWorldDamaged, "career/world-damaged", "A garbage world.txt with no .bak is a damaged entry; with a good .bak it loads the .bak") {
    const std::string dir = freshDir("world-damaged");
    ScopedCareerDir scope(dir);
    WorldSave w = newWorld("Rough", 1, DIFF_LIGHT, MODE_PERSONAL, {});
    w.opened = {"a"};
    ctx.check("first save", saveWorld(w));
    w.opened = {"a", "b"};
    ctx.check("second save", saveWorld(w));
    const std::string path = dir + "/worlds/rough/world.txt";
    ctx.check("the .bak is the first", slurp(path + ".bak").find("cache b") == std::string::npos && !slurp(path + ".bak").empty());
    {  // cut short after the name line: not trusted, the .bak loads, and a save doesn't let it replace the .bak
        const std::string full = slurp(path);
        spit(path, full.substr(0, full.find('\n') + 1));
        WorldSave t;
        ctx.check("truncated: loads the .bak", loadWorld("rough", t) && t.opened.size() == 1);
        ctx.check("a save keeps the good .bak", saveWorld(w) && slurp(path + ".bak").find("end 1") != std::string::npos);
    }
    spit(path, "x#!\x01z");
    WorldSave r;
    ctx.check("loads the .bak", loadWorld("rough", r) && !r.damaged && r.opened.size() == 1 && r.name == "Rough");
    ctx.check("a save over garbage keeps the good .bak", saveWorld(w) && slurp(path + ".bak").find("name Rough") != std::string::npos);
    spit(path, "x#!\x01z");
    std::remove((path + ".bak").c_str());
    WorldSave d;
    ctx.check("both bad: false", !loadWorld("rough", d));
    ctx.check("and damaged", d.damaged && d.slug == "rough");
    const auto all = listWorlds();
    ctx.check("still listed, damaged", all.size() == 1 && all[0].damaged && all[0].slug == "rough");
    ctx.check("a missing slug is damaged too", !loadWorld("nope", d) && d.damaged);
    ctx.check("a path-like slug is refused", !loadWorld("../x", d) && !saveWorld([] { WorldSave s; s.slug = "../x"; return s; }()));
}

SCENARIO(careerWorldSlugs, "career/world-slugs", "Two worlds whose names slug the same get different folders; both are saved and listed") {
    ScopedCareerDir scope(freshDir("world-slugs"));
    WorldSave a = newWorld("My World", 0, DIFF_MEDIUM, MODE_PERSONAL, listWorlds());
    ctx.check("a saves", saveWorld(a));
    WorldSave b = newWorld("my world!", 0, DIFF_MEDIUM, MODE_PERSONAL, listWorlds());
    ctx.check("b saves", saveWorld(b));
    ctx.check("different slugs", a.slug != b.slug, a.slug + " " + b.slug);
    ctx.check("both listed", listWorlds().size() == 2);
}

SCENARIO(careerWorldDelete, "career/world-delete", "deleteWorld removes the folder and its listing") {
    const std::string dir = freshDir("world-delete");
    ScopedCareerDir scope(dir);
    WorldSave a = newWorld("One", 0, DIFF_MEDIUM, MODE_PERSONAL, {});
    WorldSave b = newWorld("Two", 0, DIFF_MEDIUM, MODE_PERSONAL, {});
    saveWorld(a), saveWorld(b), saveWorld(a);  // (a twice: it has a .bak to remove as well)
    ctx.check("two listed", listWorlds().size() == 2);
    ctx.check("deletes", deleteWorld("one"));
    ctx.check("the folder is gone", !isDir(dir + "/worlds/one"));
    const auto all = listWorlds();
    ctx.check("only two is left", all.size() == 1 && all[0].slug == "two");
    ctx.check("deleting it again is false", !deleteWorld("one"));
}

// ---- A world being played (careersession.h) ----

namespace {

int catalogIndex(const char* id) {
    const auto& cat = vehicleCatalog();
    for (int v = 0; v < (int)cat.size(); v++)
        if (std::string(cat[v].id) == id) return v;
    return -1;
}

double worstDentMm(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
    if (a.size() != b.size()) return 1e9;
    double worst = 0;
    for (size_t i = 0; i + 2 < a.size(); i += 3) {
        const double dx = a[i] - b[i], dy = a[i + 1] - b[i + 1], dz = a[i + 2] - b[i + 2];
        worst = std::max(worst, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return worst;
}

int vehiclesIn(const Sim& sim) {
    int n = 0;
    for (const Car& c : sim.cars) n += !c.isProp();
    return n;
}

const Car* slotCar(const Sim& sim, int slot) {
    for (const Car& c : sim.cars)
        if (c.careerSlot == slot) return &c;
    return nullptr;
}

bool fileThere(const std::string& path) { return (bool)std::ifstream(path); }

}  // namespace

SCENARIO(careerSessionNew, "career/session-new", "A new Hardcore world in the Bronco: $500, one Bronco owned in this world, and the map's lineup replaced by it at the spawn") {
    ScopedCareerDir scope(freshDir("session-new"));
    const int bronco = catalogIndex("bronco");
    ctx.sim.lineup = listedVehicles();
    ctx.sim.setup();
    ctx.check("the map put out its lineup", vehiclesIn(ctx.sim) > 1, std::to_string(vehiclesIn(ctx.sim)));
    WorldSave w = newWorld("Hard Yards", MAP_PROVING_GROUND, DIFF_HARDCORE, MODE_PERSONAL, {});
    CareerSession s;
    careersession::begin(s, ctx.sim, w, "ANN", bronco);
    ctx.check("active, career on", s.active && s.profile.on);
    ctx.check("cash 500", s.profile.cash == 500, std::to_string(s.profile.cash));
    ctx.check("one owned vehicle: the Bronco, in this world",
              s.profile.vehicles.size() == 1 && s.profile.vehicles[0].vehicle == bronco && s.profile.vehicles[0].world == w.slug);
    ctx.check("one vehicle in the sim", vehiclesIn(ctx.sim) == 1, std::to_string(vehiclesIn(ctx.sim)));
    const Car& c = ctx.sim.cars[ctx.sim.player];
    ctx.check("you're in the Bronco", c.spec->model == bronco && c.careerSlot == 0);
    const V3 d = c.frame().com - ctx.sim.world.spawn;
    ctx.check("at the spawn", std::hypot(d.x, d.z) < 1.0, std::to_string(std::hypot(d.x, d.z)) + " m");
    ctx.check("nothing written until a save", !fileThere(Career::profilePath()));
}

SCENARIO(careerSessionContinue, "career/session-continue", "A Bronco driven 30 m into a barrier and the world left: continued in a new sim, it's back where it was left, the same dents") {
    const std::string dir = freshDir("session-continue");
    ScopedCareerDir scope(dir);
    const int bronco = catalogIndex("bronco");
    ctx.sim.setupEmpty();
    WorldSave w = newWorld("Trail", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL, {});
    CareerSession s;
    careersession::begin(s, ctx.sim, w, "ANN", bronco);
    const int me = ctx.sim.player;
    const Frame f0 = ctx.sim.cars[me].frame();
    const V3 fwd = norm(V3(f0.fwd.x, 0, f0.fwd.z));
    const V3 wall = f0.com + fwd * (30 + ctx.sim.cars[me].spec->length / 2 + 1);
    ctx.addProp(barrierSpec(), wall.x, wall.z, std::atan2(-fwd.z, fwd.x) * 180 / M_PI + 90);
    Car& car = ctx.sim.cars[me];
    ctx.run(0.5);
    ctx.launch(car, 10);
    ctx.run(4.0);
    car.throttle = 0, car.handbrake = 1, car.parkBrake = true;
    ctx.run(2.0);
    const V3 left = car.frame().com;
    ctx.metric("driven", len(V3(left.x - f0.com.x, 0, left.z - f0.com.z)), "m", 25, 45);  // (into the barrier, which gives a little)
    ctx.check("dented", car.damage > 0.005, std::to_string(car.damage));
    const std::vector<int16_t> dents = net::carDents(car);
    careersession::end(s, ctx.sim);
    ctx.check("ended, career mode off with it", !s.active && !s.profile.on);
    ctx.check("profile saved", fileThere(dir + "/profile.txt"));
    ctx.check("world saved", fileThere(dir + "/worlds/" + w.slug + "/world.txt"));

    Sim sim2;
    sim2.setupEmpty();
    WorldSave w2;
    ctx.check("the world loads", loadWorld(w.slug, w2));
    CareerSession s2;
    careersession::begin(s2, sim2, w2, "ANN");
    ctx.check("one vehicle, the Bronco", vehiclesIn(sim2) == 1 && sim2.cars[sim2.player].spec->model == bronco);
    ctx.check("still one owned", s2.profile.vehicles.size() == 1);
    const Car& back = sim2.cars[sim2.player];
    ctx.check("back where it was left (0.1 m)", len(back.frame().com - left) < 0.1, std::to_string(len(back.frame().com - left)) + " m");
    const double off = worstDentMm(dents, net::carDents(back));
    ctx.check("the same dents (2 mm)", off <= 2, std::to_string(off) + " mm");
}

SCENARIO(careerSessionOdd, "career/session-odd", "Continuing: a wheel left off is back on, a vehicle the catalog lost comes back stock at its place, a crew world's left vehicles are out too") {
    ScopedCareerDir scope(freshDir("session-odd"));
    const int blazer = catalogIndex("blazer"), tacoma = catalogIndex("tacoma");
    ctx.sim.setupEmpty();
    WorldSave w = newWorld("Crew", MAP_PROVING_GROUND, DIFF_LIGHT, MODE_CREW, {});
    w.left = {{"BO", tacoma, ""}};
    CareerSession s;
    careersession::begin(s, ctx.sim, w, "ANN", blazer);
    ctx.check("the crew's Tacoma is out", vehiclesIn(ctx.sim) == 2 && slotCar(ctx.sim, -2) && slotCar(ctx.sim, -2)->spec->model == tacoma);
    ctx.run(1.5);
    WheelItem off;
    ctx.check("a wheel taken off", ctx.sim.removeWheel(ctx.sim.player, 0, off) && !ctx.sim.cars[ctx.sim.player].wheels[0].mounted);
    careersession::end(s, ctx.sim);
    ctx.check("the Tacoma's condition kept in the world", !s.world.left[0].condition.empty());

    // A second Blazer in the profile whose save names a vehicle the catalog doesn't have, 10 m to the side.
    Career p;
    p.loadProfile();
    OwnedVehicle lost = p.vehicles[0];
    std::string& t = lost.condition;
    const size_t v = t.find(" veh=");
    t.replace(v, t.find(' ', v + 1) - v, " veh=9999");
    const size_t c2 = t.find(" com="), cEnd = t.find(' ', c2 + 1);
    double x, y, z;
    std::sscanf(t.c_str() + c2 + 5, "%lf,%lf,%lf", &x, &y, &z);
    char com[96];
    std::snprintf(com, sizeof com, " com=%.17g,%.17g,%.17g", x + 10, y, z);
    t.replace(c2, cEnd - c2, com);
    lost.drove = -1;
    p.vehicles.push_back(lost);
    ctx.check("vehicleOf says unknown", carsave::vehicleOf(lost.condition) == -1);
    p.saveProfile();

    Sim sim2;
    sim2.setupEmpty();
    WorldSave w2;
    loadWorld(w.slug, w2);
    CareerSession s2;
    careersession::begin(s2, sim2, w2, "ANN");
    ctx.check("three vehicles out", vehiclesIn(sim2) == 3, std::to_string(vehiclesIn(sim2)));
    const Car* mine = slotCar(sim2, 0);
    ctx.check("you're in the one you drove", mine && &sim2.cars[sim2.player] == mine);
    bool allOn = mine != nullptr;
    for (size_t k = 0; mine && k < mine->wheels.size(); k++) allOn &= mine->wheels[k].mounted;
    ctx.check("its wheel is back on", allOn);
    const Car* stock = slotCar(sim2, 1);
    ctx.check("the lost one is out, a Blazer, undamaged", stock && stock->spec->model == blazer && stock->damage < 1e-6);
    ctx.check("at its saved place", stock && len(stock->frame().com - V3(x + 10, y, z)) < 0.1);
    const Car* crew = slotCar(sim2, -2);
    ctx.check("the crew's Tacoma is back", crew && crew->spec->model == tacoma);
}

SCENARIO(careerSessionAutosave, "career/session-autosave", "The autosave: not at 179 s, at 180 s (profile and world written), then 180 s again") {
    const std::string dir = freshDir("session-autosave");
    ScopedCareerDir scope(dir);
    ctx.sim.setupEmpty();
    WorldSave w = newWorld("Clock", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL, {});
    CareerSession s;
    careersession::begin(s, ctx.sim, w, "ANN", catalogIndex("blazer"));
    bool early = false;
    for (int k = 0; k < 179; k++) early |= careersession::tick(s, ctx.sim, 1.0);
    ctx.check("nothing by 179 s", !early && !fileThere(dir + "/profile.txt"));
    ctx.check("saves at 180 s", careersession::tick(s, ctx.sim, 1.0));
    ctx.check("the files are there", fileThere(dir + "/profile.txt") && fileThere(dir + "/worlds/clock/world.txt"));
    WorldSave r;
    ctx.check("the time played is saved", loadWorld("clock", r) && std::fabs(r.playedSeconds - 180) < 0.01, std::to_string(r.playedSeconds));
    early = false;
    for (int k = 0; k < 179; k++) early |= careersession::tick(s, ctx.sim, 1.0);
    ctx.check("then not for another 179 s", !early);
    ctx.check("and again at 360 s", careersession::tick(s, ctx.sim, 1.0));
}

SCENARIO(careerSessionSandbox, "career/session-sandbox", "Sandbox (no world begun): ticking and the ways out write no world and leave profile.txt as it was") {
    const std::string dir = freshDir("session-sandbox");
    ScopedCareerDir scope(dir);
    ctx.sim.lineup = {catalogIndex("blazer")};
    ctx.sim.setup();
    spit(dir + "/profile.txt", "cash 77\n");
    CareerSession s;
    bool saved = false;
    for (int k = 0; k < 400; k++) saved |= careersession::tick(s, ctx.sim, 1.0);
    careersession::end(s, ctx.sim);
    ctx.check("no autosave", !saved);
    ctx.check("no worlds folder", !isDir(dir + "/worlds"));
    ctx.check("profile.txt untouched", slurp(dir + "/profile.txt") == "cash 77\n");
    ctx.check("career off", !s.profile.on && !s.active);
    ctx.check("the sim's car is no career car", ctx.sim.cars[ctx.sim.player].careerSlot == -1);
}

SCENARIO(careerSessionSwap, "career/session-swap", "A car swapped for another vehicle type in a world isn't saved over the owned vehicle") {
    ScopedCareerDir scope(freshDir("session-swap"));
    const int blazer = catalogIndex("blazer"), tacoma = catalogIndex("tacoma");
    ctx.sim.setupEmpty();
    WorldSave w = newWorld("Swap", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL, {});
    CareerSession s;
    careersession::begin(s, ctx.sim, w, "ANN", blazer);
    ctx.run(1.0);
    careersession::capture(s, ctx.sim);
    const std::string before = s.profile.vehicles[0].condition;
    ctx.check("captured as the Blazer", carsave::vehicleOf(before) == blazer);
    ctx.sim.swapPlayerVehicle(vehicleCatalog()[tacoma]);
    ctx.run(0.5);
    careersession::capture(s, ctx.sim);
    ctx.check("still the Blazer, its condition as it was", s.profile.vehicles[0].vehicle == blazer && s.profile.vehicles[0].condition == before);
}

SCENARIO(careerSessionDelete, "career/session-delete", "Deleting a world keeps its vehicles in no world; a new world by the same name gets a fresh folder and none of them") {
    const std::string dir = freshDir("session-delete");
    ScopedCareerDir scope(dir);
    const int blazer = catalogIndex("blazer"), bronco = catalogIndex("bronco");
    ctx.sim.setupEmpty();
    WorldSave w = careersession::newWorldFor(Career(), "Gone", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL);
    CareerSession s;
    careersession::begin(s, ctx.sim, w, "ANN", blazer);
    careersession::end(s, ctx.sim);
    ctx.check("deleted", careersession::forgetWorld(s.profile, w.slug, true) && !isDir(dir + "/worlds/" + w.slug));
    Career p;
    ctx.check("the profile keeps the Blazer, in no world", p.loadProfile() && p.vehicles.size() == 1 && p.vehicles[0].vehicle == blazer && p.vehicles[0].world.empty());

    Career ghost;  // (a profile still naming a world whose folder is gone)
    OwnedVehicle ov;
    ov.world = "gone";
    ghost.vehicles = {ov};
    ctx.check("a slug a profile vehicle names isn't reused", careersession::newWorldFor(ghost, "Gone", 0, DIFF_MEDIUM, MODE_PERSONAL).slug != "gone");

    WorldSave again = careersession::newWorldFor(p, "Gone", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL);
    Sim sim2;
    sim2.setupEmpty();
    CareerSession s2;
    careersession::begin(s2, sim2, again, "ANN", bronco);
    ctx.check("only the new Bronco is out", vehiclesIn(sim2) == 1 && sim2.cars[sim2.player].spec->model == bronco);
    ctx.check("the profile has both", s2.profile.vehicles.size() == 2);
    careersession::end(s2, sim2);
    const std::string saved = slurp(Career::profilePath());
    ctx.check("deleting with write off touches nothing",
              careersession::forgetWorld(s2.profile, again.slug, false) && isDir(dir + "/worlds/" + again.slug) && slurp(Career::profilePath()) == saved);
}

SCENARIO(careerSessionNoWrite, "career/session-nowrite", "A scripted run's session (write off): the autosave and the end come round but nothing is written") {
    const std::string dir = freshDir("session-nowrite");
    ScopedCareerDir scope(dir);
    ctx.sim.setupEmpty();
    WorldSave w = newWorld("Quiet", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL, {});
    CareerSession s;
    s.write = false;
    careersession::begin(s, ctx.sim, w, "ANN", catalogIndex("blazer"));
    bool saved = false;
    for (int k = 0; k < 180; k++) saved |= careersession::tick(s, ctx.sim, 1.0);
    careersession::end(s, ctx.sim);
    ctx.check("the autosave came round", saved);
    ctx.check("no profile.txt", !fileThere(dir + "/profile.txt"));
    ctx.check("no worlds folder", !isDir(dir + "/worlds"));
}

SCENARIO(careerSessionStationSwap, "career/session-station-swap", "Changing vehicles: refused away from a station and for one you don't own; at a station between two of yours, both kept as they were") {
    ScopedCareerDir scope(freshDir("session-station-swap"));
    const int blazer = catalogIndex("blazer"), tacoma = catalogIndex("tacoma"), bronco = catalogIndex("bronco");
    ctx.sim.setupEmpty();
    WorldSave w = newWorld("Swaps", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL, {});
    CareerSession s;
    careersession::begin(s, ctx.sim, w, "ANN", blazer);
    OwnedVehicle other;  // (a Tacoma of yours in this world, parked off somewhere: not out)
    other.vehicle = tacoma;
    other.world = w.slug;
    s.profile.vehicles.push_back(other);
    {  // the Blazer bent a little, and settled
        Car& c = ctx.sim.cars[ctx.sim.player];
        for (size_t k = 0; k < c.beams.size(); k += 7)
            if (c.beams[k].type == BEAM_BODY) c.beams[k].L0 *= 1.02;
        c.handbrake = 1;
    }
    ctx.run(1.5);
    std::string why;
    ctx.check("away from a station: refused", !careersession::canSwapHere(ctx.sim) && !careersession::swapTo(s, ctx.sim, 1, why) && why == "SWAP VEHICLES AT A STATION", why);
    const V3 here = ctx.sim.cars[ctx.sim.player].frame().com;
    Station st;
    st.p = V3(here.x, 0, here.z);
    st.name = "TEST";
    ctx.sim.world.stations.push_back(st);
    ctx.check("on the pad: allowed", careersession::canSwapHere(ctx.sim));
    ctx.check("a Bronco: not yours", careersession::ownedOf(s, ctx.sim, bronco) < 0);
    ctx.check("a vehicle you don't own: refused", !careersession::swapTo(s, ctx.sim, 7, why), why);
    ctx.check("the one you're in: refused", !careersession::swapTo(s, ctx.sim, 0, why), why);

    const std::vector<int16_t> blazerDents = net::carDents(ctx.sim.cars[ctx.sim.player]);
    const int t = careersession::ownedOf(s, ctx.sim, tacoma);
    ctx.check("your Tacoma", t == 1);
    ctx.check("swapped into the Tacoma", careersession::swapTo(s, ctx.sim, t, why), why);
    ctx.check("you're in the Tacoma", ctx.sim.cars[ctx.sim.player].spec->model == tacoma && ctx.sim.cars[ctx.sim.player].careerSlot == 1);
    const Car* parked = slotCar(ctx.sim, 0);
    ctx.check("the Blazer stays parked where it was", parked && len(parked->frame().com - here) < 0.05);
    ctx.check("the Tacoma is in this world now", s.profile.vehicles[1].world == w.slug);
    ctx.check("two vehicles out", vehiclesIn(ctx.sim) == 2);
    ctx.run(1.0);

    ctx.check("and back into the Blazer", careersession::swapTo(s, ctx.sim, careersession::ownedOf(s, ctx.sim, blazer), why), why);
    const Car& back = ctx.sim.cars[ctx.sim.player];
    ctx.check("you're in the Blazer", back.spec->model == blazer && back.careerSlot == 0);
    const double off = worstDentMm(blazerDents, net::carDents(back));
    ctx.check("the same dents (2 mm)", off <= 2, std::to_string(off) + " mm");
    ctx.check("still two vehicles out", vehiclesIn(ctx.sim) == 2);
    ctx.check("the Tacoma's condition kept", carsave::vehicleOf(s.profile.vehicles[1].condition) == tacoma);

    careersession::end(s, ctx.sim);
    Sim sim2;
    sim2.setupEmpty();
    WorldSave w2;
    loadWorld(w.slug, w2);
    CareerSession s2;
    careersession::begin(s2, sim2, w2, "ANN");
    ctx.check("continued: both out, you in the Blazer",
              vehiclesIn(sim2) == 2 && sim2.cars[sim2.player].spec->model == blazer && slotCar(sim2, 1) && slotCar(sim2, 1)->spec->model == tacoma);
}

SCENARIO(careerSessionPaint, "career/session-paint", "A career truck keeps its own paint: in profile.txt with it, back on it when the world's continued; a crew world's left vehicle keeps its paint in its condition") {
    const std::string dir = freshDir("session-paint");
    ScopedCareerDir scope(dir);
    const int bronco = catalogIndex("bronco");
    ctx.sim.setupEmpty();
    WorldSave w = newWorld("Paint", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL, {});
    CareerSession s;
    careersession::begin(s, ctx.sim, w, "ANN", bronco);
    CarPaint p;
    p.setBody(0.8, 0.1, 0.1), p.setAccent(0.1, 0.1, 0.8), p.finish = PF_METALLIC;
    Car& car = ctx.sim.cars[ctx.sim.player];
    car.setPaint(p);  // (as the garage's PAINT page does in a world: main.cpp shopResult)
    s.profile.vehicles[car.careerSlot].paint = p;
    const std::string cond = carsave::capture(ctx.sim, car);
    CarPaint fromCond;
    ctx.check("the condition carries the paint", carsave::paintOf(cond, fromCond) && fromCond == p);
    {
        car.paint = CarPaint();
        CarPaint x;
        ctx.check("a factory paint isn't written", !carsave::paintOf(carsave::capture(ctx.sim, car), x));
        car.setPaint(p);
    }
    careersession::end(s, ctx.sim);
    Career back;
    ctx.check("the profile loads", back.loadProfile() && back.vehicles.size() == 1);
    ctx.check("its paint came back with it", !back.vehicles.empty() && back.vehicles[0].paint == p);

    Sim sim2;
    sim2.setupEmpty();
    WorldSave w2;
    ctx.check("the world loads", loadWorld(w.slug, w2));
    CareerSession s2;
    careersession::begin(s2, sim2, w2, "ANN");
    ctx.check("continued, it wears its paint", sim2.cars[sim2.player].paint == p);

    // A crew world's vehicle left by someone else: its paint is in its condition.
    Sim sim3;
    sim3.setupEmpty();
    WorldSave crew = newWorld("Crew", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_CREW, {});
    LeftVehicle lv;
    lv.vehicle = bronco;
    lv.condition = cond;
    crew.left.push_back(lv);
    CareerSession s3;
    s3.write = false;
    careersession::begin(s3, sim3, crew, "ANN", bronco);
    const Car* left = slotCar(sim3, -2);
    ctx.check("the left vehicle wears its paint", left && left->paint == p);
}

// ---- Final-review fixes: progress kept, the garage, starters, caches per world ----

SCENARIO(careerSessionKeepsProgress, "career/session-keeps-progress", "A profile from career play without a world (cash, parts, no vehicles) keeps its progress in a NEW WORLD; a damaged profile refuses the world and is left alone") {
    const std::string dir = freshDir("session-keeps-progress");
    ScopedCareerDir scope(dir);
    const int bronco = catalogIndex("bronco");
    {
        Career p;
        p.startFresh();
        p.cash = 4321;
        p.grant("TIRES:MUD-TERRAIN");
        p.opened.insert("3:SUMMIT");
        ctx.check("profile saved", p.saveProfile());
    }
    ctx.sim.setupEmpty();
    WorldSave w = newWorld("Hard", MAP_PROVING_GROUND, DIFF_HARDCORE, MODE_PERSONAL, {});
    CareerSession s;
    ctx.check("a readable profile can start a world", careersession::profileReadable());
    ctx.check("begun", careersession::begin(s, ctx.sim, w, "ANN", bronco));
    ctx.check("the cash kept (not the difficulty's)", s.profile.cash == 4321, std::to_string(s.profile.cash));
    ctx.check("the part kept", s.profile.owned.count("TIRES:MUD-TERRAIN") == 1);
    ctx.check("the opened cache kept", s.profile.opened.count("3:SUMMIT") == 1);
    ctx.check("and the Bronco", s.profile.vehicles.size() == 1 && s.profile.vehicles[0].vehicle == bronco);
    careersession::end(s, ctx.sim);

    // A profile.txt that won't parse (and no good .bak): the world isn't begun and nothing is written over it.
    const std::string junk = "this is not a profile\n";
    spit(dir + "/profile.txt", junk);
    spit(dir + "/profile.txt.bak", junk);
    Sim sim2;
    sim2.setupEmpty();
    WorldSave w2 = newWorld("Other", MAP_PROVING_GROUND, DIFF_LIGHT, MODE_PERSONAL, {});
    CareerSession s2;
    std::string why;
    ctx.check("a damaged profile: unreadable (a NEW WORLD is refused before its world.txt is made)", !careersession::profileReadable(&why) && !why.empty(), why);
    ctx.check("refused", !careersession::begin(s2, sim2, w2, "ANN", bronco, &why) && !why.empty(), why);
    ctx.check("not active, career off", !s2.active && !s2.profile.on);
    careersession::end(s2, sim2);
    ctx.check("the files left as they were", slurp(dir + "/profile.txt") == junk && slurp(dir + "/profile.txt.bak") == junk);

    // Only the .bak there, and it damaged: refused too.
    std::remove((dir + "/profile.txt").c_str());
    CareerSession s3;
    ctx.check("a damaged .bak alone: refused", !careersession::begin(s3, sim2, w2, "ANN", bronco, &why) && !careersession::profileReadable());
    std::remove((dir + "/profile.txt.bak").c_str());
    ctx.check("no profile at all: readable (a fresh one starts)", careersession::profileReadable());
}

SCENARIO(careerSessionScripted, "career/session-scripted", "A scripted run's session (write off) never reads profile.txt: a fresh profile starts at the world's difficulty, a kept one stays as it was") {
    const std::string dir = freshDir("session-scripted");
    ScopedCareerDir scope(dir);
    spit(dir + "/profile.txt", "cash 4321\n");
    ctx.sim.setupEmpty();
    WorldSave w = newWorld("Quiet", MAP_PROVING_GROUND, DIFF_HARDCORE, MODE_PERSONAL, {});
    CareerSession s;
    s.write = false;
    s.profile.startFresh();  // (as main.cpp does for a scripted run)
    ctx.check("begun", careersession::begin(s, ctx.sim, w, "ANN", catalogIndex("blazer")));
    ctx.check("profile.txt not read: the difficulty's cash", s.profile.cash == 500, std::to_string(s.profile.cash));
    Sim sim2;
    sim2.setupEmpty();
    CareerSession s2;
    s2.write = false;
    s2.profile.cash = 77;
    careersession::begin(s2, sim2, w, "ANN", catalogIndex("blazer"));
    ctx.check("not a fresh one: kept as it was", s2.profile.cash == 77, std::to_string(s2.profile.cash));
}

SCENARIO(careerSessionStarters, "career/session-starters", "A world with none of yours: a starter only when you own no vehicles; else your last driven one is moved here, as it was, to the spawn") {
    const std::string dir = freshDir("session-starters");
    ScopedCareerDir scope(dir);
    const int bronco = catalogIndex("bronco"), tacoma = catalogIndex("tacoma");
    ctx.sim.setupEmpty();
    WorldSave a = newWorld("World A", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL, {});
    saveWorld(a);
    WorldSave b = newWorld("World B", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL, {a});
    saveWorld(b);
    {  // no vehicles at all: continuing B mints one
        Sim sim0;
        sim0.setupEmpty();
        CareerSession s;
        s.write = false;
        careersession::begin(s, sim0, b, "ANN");
        ctx.check("nothing owned: a starter", s.profile.vehicles.size() == 1 && s.profile.vehicles[0].world == b.slug);
    }
    // A Tacoma (driven long ago) and a dented Bronco (driven last), both in world A.
    Sim& simA = ctx.sim;
    CareerSession sa;
    careersession::begin(sa, simA, a, "ANN", bronco);
    {
        Car& c = simA.cars[simA.player];
        for (size_t k = 0; k < c.beams.size(); k += 7)
            if (c.beams[k].type == BEAM_BODY) c.beams[k].L0 *= 1.02;
        c.handbrake = 1;
    }
    ctx.run(1.5);
    sa.world.playedSeconds = 50;
    careersession::capture(sa, simA);
    const std::vector<int16_t> dents = net::carDents(simA.cars[simA.player]);
    OwnedVehicle old;
    old.vehicle = tacoma, old.world = a.slug, old.drove = 10;
    sa.profile.vehicles.push_back(old);
    careersession::end(sa, simA);
    ctx.check("the Bronco was driven last", sa.profile.vehicles[0].drove == 50);

    Sim simB;
    simB.setupEmpty();
    CareerSession sb;
    WorldSave b2;
    loadWorld(b.slug, b2);
    careersession::begin(sb, simB, b2, "ANN");
    ctx.check("no new vehicle minted", sb.profile.vehicles.size() == 2, std::to_string(sb.profile.vehicles.size()));
    ctx.check("the Bronco moved to world B", sb.profile.vehicles[0].world == b.slug && sb.profile.vehicles[1].world == a.slug);
    const Car& you = simB.cars[simB.player];
    ctx.check("you're in it", you.spec->model == bronco && you.careerSlot == 0 && vehiclesIn(simB) == 1);
    const V3 d = you.frame().com - simB.world.spawn;
    ctx.check("at the spawn", std::hypot(d.x, d.z) < 1.0, std::to_string(std::hypot(d.x, d.z)) + " m");
    const double off = worstDentMm(dents, net::carDents(you));
    ctx.check("its dents came with it (2 mm)", off <= 2, std::to_string(off) + " mm");

    // DRIVE/swap: only what's in this world (no delivery yet).
    Station st;
    st.p = V3(you.frame().com.x, 0, you.frame().com.z);
    simB.world.stations.push_back(st);
    ctx.check("the Tacoma in world A isn't offered", careersession::ownedOf(sb, simB, tacoma) < 0);
    std::string why;
    ctx.check("nor can you swap into it", !careersession::swapTo(sb, simB, 1, why), why);
}

SCENARIO(careerSessionCaches, "career/session-caches", "A cache opened in a world is opened in that world only (saved with it); without a world it's the profile's") {
    const std::string dir = freshDir("session-caches");
    ScopedCareerDir scope(dir);
    CacheSpot spot;
    spot.id = "TEST CRATE", spot.map = MAP_PROVING_GROUND, spot.cash = 250;
    ctx.sim.setupEmpty();
    WorldSave a = newWorld("Cache A", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL, {});
    CareerSession s;
    careersession::begin(s, ctx.sim, a, "ANN", catalogIndex("blazer"));
    const int cash0 = s.profile.cash;
    ctx.check("closed at first", !careersession::cacheOpened(s, spot));
    ctx.check("opens and pays", !careersession::openCache(s, ctx.sim, spot).empty() && s.profile.cash == cash0 + 250);
    ctx.check("opened in this world", careersession::cacheOpened(s, spot) && s.world.opened.size() == 1);
    ctx.check("not in the profile", s.profile.opened.empty());
    ctx.check("once only", careersession::openCache(s, ctx.sim, spot).empty());
    WorldSave saved;
    ctx.check("the world was saved with it", loadWorld(a.slug, saved) && saved.opened.size() == 1);
    careersession::end(s, ctx.sim);
    ctx.check("left the world: the profile's state again (the crates' lids go back to it)", !careersession::cacheOpened(s, spot));

    Sim sim2;
    sim2.setupEmpty();
    WorldSave b = newWorld("Cache B", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL, {a});
    CareerSession s2;
    careersession::begin(s2, sim2, b, "ANN", catalogIndex("bronco"));
    ctx.check("still closed in world B", !careersession::cacheOpened(s2, spot));
    ctx.check("and pays there", !careersession::openCache(s2, sim2, spot).empty());
    careersession::end(s2, sim2);

    CareerSession none;  // (career without a world)
    none.write = false;
    none.profile.on = true;
    ctx.check("no world: the profile's", !careersession::openCache(none, sim2, spot).empty() && none.profile.isOpened(spot));
}

SCENARIO(careerSessionGarage, "career/session-garage", "The garage on an owned truck: opened with its own parts (not the garage file's), it isn't rebuilt or repaired; fitting goes in the profile") {
    const std::string dir = freshDir("session-garage");
    ScopedCareerDir scope(dir);
    const int bronco = catalogIndex("bronco");
    int slot = -1;
    for (int k = 0; k < MOD_SLOTS && slot < 0; k++)
        if (modOptions(bronco, k).size() > 1) slot = k;
    ctx.check("the Bronco has a part to change", slot >= 0);
    if (slot < 0) return;
    ctx.sim.setupEmpty();
    WorldSave w = newWorld("Shop", MAP_PROVING_GROUND, DIFF_MEDIUM, MODE_PERSONAL, {});
    {
        Career p;
        p.startFresh();
        OwnedVehicle ov;
        ov.vehicle = bronco, ov.world = w.slug;
        ov.mods.pick[slot] = 1;
        p.vehicles.push_back(ov);
        p.saveProfile();
    }
    CareerSession s;
    careersession::begin(s, ctx.sim, w, "ANN");
    Car& c = ctx.sim.cars[ctx.sim.player];
    ctx.check("built with its own parts", c.spec == &tunedSpec(bronco, s.profile.vehicles[0].mods));
    for (size_t k = 0; k < c.beams.size(); k += 7)
        if (c.beams[k].type == BEAM_BODY) c.beams[k].L0 *= 1.02;
    c.handbrake = 1;
    ctx.run(1.5);
    const VehicleSpec* built = c.spec;
    const std::vector<int16_t> dents = net::carDents(c);

    Garage garage;  // (the garage file's Bronco: stock, unlike the owned one)
    ModSet fit = garage.fitted[bronco];
    ctx.check("a career truck: its own parts", careersession::shopMods(s, ctx.sim, fit) && fit.pick[slot] == 1);
    shop::Shop shop;
    shop.begin(bronco, fit, s.profile, true, true, c.paint);
    ctx.check("open: not rebuilt", !shop::liftSync(ctx.sim, shop) && ctx.sim.cars[ctx.sim.player].spec == built);
    shop.close();
    ctx.check("closed: not rebuilt", !shop::liftSync(ctx.sim, shop) && ctx.sim.cars[ctx.sim.player].spec == built);
    const double off = worstDentMm(dents, net::carDents(ctx.sim.cars[ctx.sim.player]));
    ctx.check("its dents unchanged (2 mm)", off <= 2, std::to_string(off) + " mm");

    ModSet other = fit;
    other.pick[slot] = 0;
    ctx.check("fitting: into the profile", careersession::fit(s, ctx.sim, bronco, other) && s.profile.vehicles[0].mods.pick[slot] == 0);
    Career back;
    ctx.check("and saved", back.loadProfile() && back.vehicles.size() == 1 && back.vehicles[0].mods.pick[slot] == 0);
    CareerSession sandbox;
    ModSet keep = garage.fitted[bronco];
    ctx.check("not a career world: the garage's", !careersession::shopMods(sandbox, ctx.sim, keep) && !careersession::fit(sandbox, ctx.sim, bronco, other));
}

SCENARIO(careerWorldMenuPath, "career/world-menu-path", "A world made the way CAREER > NEW WORLD makes it (profileReadable, newWorldFor, saveWorld) is on disk at once and listed again with its map, seed and truck") {
    const std::string dir = freshDir("menu-path");
    ScopedCareerDir scope(dir);
    Career profile;
    std::string why;
    ctx.check("no profile yet: a world can be made", careersession::profileReadable(&why), why);
    const WorldSave built = careersession::newWorldFor(profile, "TRAIL DAYS", MAP_BACKCOUNTRY, DIFF_LIGHT, MODE_PERSONAL);
    ctx.check("saved as soon as it's made", saveWorld(built) && fileThere(dir + "/worlds/" + built.slug + "/world.txt"));
    ctx.check("a built map has no seed", built.seed == 0);
    // Two custom worlds, no seed typed: two lands.
    const WorldSave c1 = careersession::newWorldFor(profile, "WILD ONE", MAP_PROCEDURAL, DIFF_MEDIUM, MODE_PERSONAL);
    ctx.check("the first custom world saved", saveWorld(c1));
    const WorldSave c2 = careersession::newWorldFor(profile, "WILD TWO", MAP_PROCEDURAL, DIFF_MEDIUM, MODE_PERSONAL);
    ctx.check("the second custom world saved", saveWorld(c2));
    ctx.check("two new custom worlds get different seeds", c1.seed != 0 && c2.seed != 0 && c1.seed != c2.seed, std::to_string(c1.seed) + " " + std::to_string(c2.seed));
    // A typed seed is kept as typed, even one another world has.
    const WorldSave c3 = careersession::newWorldFor(profile, "FRIEND'S", MAP_PROCEDURAL, DIFF_MEDIUM, MODE_CREW, c1.seed);
    ctx.check("a typed seed is the world's", c3.seed == c1.seed && saveWorld(c3));
    // Listed again (the CAREER page reads listWorlds()).
    const std::vector<WorldSave> all = listWorlds();
    auto find = [&](const std::string& slug) -> const WorldSave* {
        for (const WorldSave& w : all)
            if (w.slug == slug) return &w;
        return nullptr;
    };
    const WorldSave *b = find(built.slug), *l1 = find(c1.slug), *l2 = find(c2.slug);
    ctx.check("all four listed", all.size() == 4 && b && l1 && l2);
    ctx.check("the built one: its name and map, a save time", b && b->name == "TRAIL DAYS" && b->map == MAP_BACKCOUNTRY && b->savedAt > 0 && !b->damaged);
    ctx.check("the custom ones keep their seeds", l1 && l2 && l1->map == MAP_PROCEDURAL && l1->seed == c1.seed && l2->seed == c2.seed);
    // A world played and left: its window and truck come back from the file.
    WorldSave played = c1;
    played.originSet = true, played.ox = 4096, played.oz = -2048, played.lastVehicle = catalogIndex("bronco");
    ctx.check("saved again after play", saveWorld(played));
    WorldSave back;
    ctx.check("loads", loadWorld(c1.slug, back));
    ctx.check("seed, window and truck kept", back.seed == c1.seed && back.originSet && back.ox == 4096 && back.oz == -2048 && back.lastVehicle == played.lastVehicle);
    WorldSave plain;
    loadWorld(built.slug, plain);
    ctx.check("a built map's save has no seed or window", plain.seed == 0 && !plain.originSet);

    // The seed reaches the land: the procedural map built from each world's seed (as the game sets it before the load) is that seed's.
    const uint64_t seedWas = World::procSeed;
    const bool setWas = World::procOriginSet;
    World::procSeed = c1.seed, World::procOriginSet = false;
    World a(MAP_PROCEDURAL);
    World::procSeed = c2.seed, World::procOriginSet = false;
    World z(MAP_PROCEDURAL);
    World::procSeed = c1.seed, World::procOriginSet = false;
    World again(MAP_PROCEDURAL);
    World::procSeed = seedWas, World::procOriginSet = setWas;
    ctx.check("each world's land is made from its seed", a.proc.seed == c1.seed && z.proc.seed == c2.seed);
    ctx.check("two seeds, two lands", a.landHash() != z.landHash());
    ctx.check("the same seed again (after another one's tiles were made), the same land", again.landHash() == a.landHash());
}
