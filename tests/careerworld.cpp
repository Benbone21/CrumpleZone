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
#include "../src/net.h"
#include "../src/platform.h"
#include "../src/vehicles.h"
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
    ctx.check("ended", !s.active);
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
