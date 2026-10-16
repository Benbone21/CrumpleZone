// THROWAWAY capability probes for the four overlanders (Blazer, Bronco, 4Runner, Tacoma), stock and
// "set up": square ledge, grid wall, off-camber, slab climb, wading. Same approach as probe.cpp (scripted
// controls on a blank rock/dirt/grass test region; this measures the physics, it is not a gameplay check).
// Linked against the game's physics objects + the test runner; never part of the game build.
//
//   scenarios:  cap/info/<truck>-<cfg>
//               cap/ledge/<truck>-<cfg>-<norm|air>-<H cm>[-<speed dm/s>]
//               cap/wall/<truck>-<cfg>-<norm|air>-<deg>
//               cap/slab/<truck>-<cfg>-<norm|air>-<deg>
//               cap/camber/<truck>-<cfg>-<dirt|rock>-<deg>
//               cap/wade/<truck>-<cfg>-<depth cm>
//   Every probe prints one "CAP ..." line; the sweeps are summarised by grepping those.
//   cfgs: stock, stocknl (stock, no lockers in), stockair (stock parts, driver set up), setup, first, legacy, a-* (ablations).
//   Also: camberv/slabv/wadev = the same probe at other crawl speeds, to see how noisy it is.
//
// Build (from the repo root, after `make -j build/run_tests`; links only run_tests.o + harness.o, so only these scenarios exist):
//   OBJS=$(make -n -B build/run_tests 2>/dev/null | grep -o 'build/src/[^ ]*\.o' | sort -u)
//   g++ -O2 -march=native -std=c++17 -I tests -I src -c prototypes/sections/capability.cpp -o build/cap.o
//   g++ -O2 -o build/cap build/tests/run_tests.o build/tests/harness.o $OBJS build/cap.o -lpthread -lm
//   HOME=<scratch> nice -n 19 ./build/cap --jobs 6 cap/ledge/bronco-setup-air-60-   # (substring filters; --list shows names)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "harness.h"
#include "mods.h"

namespace {

// ---- Configurations -----------------------------------------------------------------------------------
// A configuration is a truck plus the garage parts fitted (by exact option name), and whether the driver
// does the set-up (lockers in, front sway bar unhooked). Stock = factory parts, factory lockers engaged.
struct Cfg {
    std::string name, truck;
    const char *eng, *sus, *trn, *tire;
    bool driverSetup;  // sway bar off, every fitted locker in (stock also locks what it has: the e-lockers)
    bool forceFrontLocker = false;  // probe.cpp's own set-up forced the Bronco's optional front locker on
    bool noLockers = false;         // leave every locker out
};

std::vector<Cfg>& cfgs() {
    static std::vector<Cfg> c;
    return c;
}
const Cfg& cfgOf(const std::string& truck, const std::string& name) {
    for (const Cfg& c : cfgs())
        if (c.truck == truck && c.name == name) return c;
    std::fprintf(stderr, "no cfg %s %s\n", truck.c_str(), name.c_str());
    std::abort();
}

int catIdx(const std::string& id) {
    const auto& cat = vehicleCatalog();
    for (int i = 0; i < (int)cat.size(); i++)
        if (id == cat[i].id) return i;
    std::fprintf(stderr, "no vehicle %s\n", id.c_str());
    std::abort();
}
int optIdx(int v, int slot, const char* name) {
    const auto& o = modOptions(v, slot);
    for (int i = 0; i < (int)o.size(); i++)
        if (o[i].name == name) return i;
    std::fprintf(stderr, "no option %s (veh %d slot %d)\n", name, v, slot);
    std::abort();
}
const VehicleSpec& specOf(const Cfg& c) {
    const int v = catIdx(c.truck);
    ModSet m;
    if (c.eng) m.pick[MOD_ENGINE] = optIdx(v, MOD_ENGINE, c.eng);
    if (c.sus) m.pick[MOD_SUSPENSION] = optIdx(v, MOD_SUSPENSION, c.sus);
    if (c.trn) m.pick[MOD_TRANSMISSION] = optIdx(v, MOD_TRANSMISSION, c.trn);
    if (c.tire) m.pick[MOD_TIRES] = optIdx(v, MOD_TIRES, c.tire);
    return tunedSpec(v, m);
}

// ---- The test region (as probe.cpp) -------------------------------------------------------------------
World& blank(Ctx& ctx, Surface surf) {
    World& w = ctx.sim.world;
    w.rocks.clear(), w.trees.clear(), w.poles.clear(), w.boxes.clear(), w.logs.clear(), w.bushes.clear();
    w.stations.clear(), w.fixtures.clear();
    w.crashProps = false;
    for (double x = -560; x <= -200; x += 1)
        for (double z = -560; z <= -200; z += 1) {
            int id = w.cellIndex(x, z);
            if (id < 0) continue;
            w.setCellHeight(id, 0);
            w.setCellSurface(id, surf, surf == SURF_ROCK ? PAINT_ROCK : surf == SURF_MUD ? PAINT_MUD : PAINT_TRAIL);
        }
    return w;
}
template <class F>
void shape(World& w, double x0, double x1, double z0, double z1, F h) {
    for (double x = x0; x <= x1; x += 1)
        for (double z = z0; z <= z1; z += 1) {
            int id = w.cellIndex(x, z);
            if (id >= 0) w.setCellHeight(id, (float)h(x, z));
        }
}

// The truck in the garage's parts, in its lowest range, lockers in, aired to `psi` (0 = leave as sold).
Car& rigUp(Ctx& ctx, const Cfg& cfg, double psi, double x, double z, double headingDeg) {
    const VehicleSpec* sp = &specOf(cfg);
    if (cfg.forceFrontLocker) {
        VehicleSpec* copy = new VehicleSpec(*sp);
        copy->lockerFront = true;
        sp = copy;
    }
    const VehicleSpec& spec = *sp;
    Car& c = ctx.add(spec, x, z, headingDeg);
    ctx.focus(c);
    ctx.run(1.0);
    if (!driveModes(spec.transfer).empty()) c.setDriveMode((int)driveModes(spec.transfer).size() - 1);  // 4L / 4L LOCK (the Jesko has none)
    if (!cfg.noLockers) c.setLocker(true, true), c.setLocker(false, true);
    if (cfg.driverSetup) c.swayDisconnected = spec.swayDisconnect > 0;
    if (psi > 0) c.tirePsi = psi;
    c.tractionControl = false;
    return c;
}

void drive(Car& c, double z, double speed, bool hold = true) {
    Frame fr = c.frame();
    V3 d = V3(fr.com.x + 6, fr.com.y, hold ? z : fr.com.z) - fr.com;
    c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 1.5, -0.55, 0.55);
    double v = dot(fr.vel, fr.fwd);
    c.throttle = clampd(0.3 + (speed - v) * 0.9, 0, 1);
    c.brake = v > speed + 1.0 ? 0.3 : 0;
    c.handbrake = 0;
}

std::string flags(const Car& c) {
    int lost = 0, shafts = 0, popped = 0;
    for (const Wheel& w : c.wheels) lost += w.detached, shafts += w.shaftBroken, popped += w.popped;
    std::string s;
    char b[64];
    if (c.engineDead) s += c.hydrolocked ? " HYDROLOCKED" : " ENGINE-DEAD";
    if (lost) std::snprintf(b, sizeof b, " WHEELS-LOST=%d", lost), s += b;
    if (shafts) std::snprintf(b, sizeof b, " SHAFTS-BROKEN=%d", shafts), s += b;
    if (popped) std::snprintf(b, sizeof b, " TIRES-POPPED=%d", popped), s += b;
    return s;
}

const char* pr(double psi) { return psi > 0 ? "air" : "norm"; }

// ---- Info: what the truck is ------------------------------------------------------------------------
double intakeY(const Car& c) {
    double intake = -1e9;
    if (c.spec->extras & EXTRA_SNORKEL) {
        for (int i = 0; i < c.bodyNodes; i++) intake = std::max(intake, c.nodes[i].p.y - 0.15);
    } else
        for (int i : c.engineNodes) intake = std::max(intake, c.nodes[i].p.y);
    return intake;
}
void infoProbe(Ctx& ctx, const Cfg& cfg) {
    blank(ctx, SURF_ROCK);
    ctx.sim.world.buildGrid();
    Car& c = rigUp(ctx, cfg, 0, -450, -380, 0);
    ctx.run(2.0);
    const VehicleSpec& s = *c.spec;
    double low = 1e9;
    for (int i = 0; i < c.bodyNodes; i++) low = std::min(low, c.nodes[i].p.y);
    double engTop = -1e9;
    for (int i : c.engineNodes) engTop = std::max(engTop, c.nodes[i].p.y);
    std::string modes;
    for (const DriveMode& m : driveModes(s.transfer)) modes += std::string(m.name) + " ";
    std::printf("CAP info %s %s: mods=[%s] tire R=%.3f m (%s) psi=%.0f lowRange=%.2f final=%.2f gear1=%.2f modes=%s fitted lockers F=%d R=%d (frontLowOnly=%d rearLowOnly=%d) "
                "lockerOn F=%d R=%d sway=%d | mass=%.0f kg | intake(engine top)=%.2f m, snorkel/body-top intake=%.2f m, engine top=%.2f m, body-low=%.2f m, in-use intake=%.2f m | shaftLimit=%.0f stall=%.0f\n",
                cfg.truck.c_str(), cfg.name.c_str(), s.modSummary.c_str(), s.wheelRadius, tireTypeName(s.tireType), s.tirePsi, s.lowRange, s.finalDrive,
                s.gears.empty() ? 0.0 : s.gears[0], modes.c_str(), s.lockerFront, s.lockerRear, s.frontLockerLowOnly, s.rearLockerLowOnly, c.lockFront, c.lockRear,
                c.swayDisconnected, [&]{double m=0;for(const Node&n:c.nodes)m+=n.m;return m;}(), engTop, [&] {
                    double m = -1e9;
                    for (int i = 0; i < c.bodyNodes; i++) m = std::max(m, c.nodes[i].p.y - 0.15);
                    return m;
                }(), engTop, low, intakeY(c), s.shaftLimit, s.stallRpm);
    ctx.metric("x", 0, "", -1, 1);
}

// ---- Ledge: a square rock box across the lane; the truck crawls at it from 12 m back -----------------------
void ledgeProbe(Ctx& ctx, const Cfg& cfg, double psi, double H, double speed) {
    World& w = blank(ctx, SURF_ROCK);
    const double face = -380, z = -380;
    w.addBox(V3(face + 8, H / 2, z), V3(8, H / 2, 5), 0, 0, 0, V3(0.4, 0.38, 0.35));
    w.boxes.back().rock = true;
    w.buildGrid();
    Car& c = rigUp(ctx, cfg, psi, face - 12, z, 0);
    double xMax = -1e9, tDone = -1;
    const double t0 = ctx.t;
    ctx.runUntil(50, [&] { return c.frame().com.x > face + 5; }, [&] {
        drive(c, z, speed);
        xMax = std::max(xMax, c.frame().com.x);
    });
    const bool ok = c.frame().com.x > face + 5;
    if (ok) tDone = ctx.t - t0;
    std::printf("CAP ledge %s %s %s H=%.2f v=%.1f: %s xmax=%.1f (face %.0f) t=%.0fs dmg=%.1f%%%s\n", cfg.truck.c_str(), cfg.name.c_str(), pr(psi), H, speed,
                ok ? "CLEARED" : "stuck", xMax - face, 0.0, tDone, c.damage * 100, flags(c).c_str());
    ctx.metric("cleared", ok, "", 0, 1);
}

// ---- Wall / slab: a grid slope rising H m at `deg`, on rock --------------------------------------------------
void slopeProbe(Ctx& ctx, const Cfg& cfg, double psi, double deg, double H, const char* kind, double speed = 1.2) {
    World& w = blank(ctx, SURF_ROCK);
    const double face = -380, run = H / std::tan(deg * M_PI / 180);
    shape(w, face, face + run + 30, -560, -200, [&](double x, double) { return std::min(H, std::max(0.0, (x - face) / run * H)); });
    w.buildGrid();
    Car& c = rigUp(ctx, cfg, psi, face - 15, -380, 0);
    double xMax = -1e9;
    ctx.run(60, [&] {
        drive(c, -380, speed);
        xMax = std::max(xMax, c.frame().com.x);
    });
    const double climbed = std::min(H, std::max(0.0, (xMax - face) / run * H));
    const bool over = xMax > face + run + 2;
    const double nowH = std::min(H, std::max(0.0, (c.frame().com.x - face) / run * H));
    std::printf("CAP %s%s %s %s %s %2.0f deg (%.1f m): reached %.2f m of %.1f, now at %.2f m %s%s\n", kind, speed == 1.2 ? "" : (speed < 1.2 ? "-v0.7" : "-v2.0"), cfg.truck.c_str(), cfg.name.c_str(), pr(psi), deg, H, climbed, H, nowH,
                over ? "OVER-THE-TOP" : "held", flags(c).c_str());
    ctx.metric("height reached", climbed, "m", 0, H);
}

// ---- Off-camber: side slope falling toward +z; drive along it at a crawl --------------------------------
void camberProbe(Ctx& ctx, const Cfg& cfg, double psi, double deg, Surface surf, double speed = 1.5) {
    World& w = blank(ctx, surf);
    const double g = std::tan(deg * M_PI / 180), zc = -380;
    shape(w, -560, -200, -560, -200, [&](double, double z) { return 30 - g * clampd(z - zc, -40, 40); });
    w.buildGrid();
    Car& c = rigUp(ctx, cfg, psi, -460, zc, 0);
    const V3 start = c.frame().com;
    double minUp = 1;
    bool rolled = false;
    ctx.run(30, [&] {
        drive(c, zc, speed, true);
        Frame fr = c.frame();
        minUp = std::min(minUp, fr.up.y);
        rolled |= fr.up.y < 0.2;
    });
    const double slide = c.frame().com.z - start.z, along = c.frame().com.x - start.x;
    std::printf("CAP camber%s %s %s %s %s %2.0f deg: along %5.1f m, slid %5.1f m, max lean %3.0f deg %s%s\n", speed == 1.5 ? "" : (speed < 1.5 ? "-v1.0" : "-v2.0"), cfg.truck.c_str(), cfg.name.c_str(), pr(psi),
                surfaceInfo(surf).name, deg, along, slide, std::acos(clampd(minUp, -1, 1)) * 180 / M_PI, rolled ? "ROLLED" : "ok-upright", flags(c).c_str());
    ctx.metric("slide", slide, "m", -100, 100);
}

// ---- Wading: a pit of water D deep (banks 1:3, 14 m flat), driven through at 2 m/s ----------------------------
void wadeProbe(Ctx& ctx, const Cfg& cfg, double D, double speed) {
    World& w = blank(ctx, SURF_GRASS);
    const double x0 = -400, len = 14;
    shape(w, x0 - 20, x0 + len + 20, -560, -200, [&](double x, double) {
        double inPit = std::min(x - x0, x0 + len - x);
        return -D * clampd(inPit / (3 * D), 0, 1);
    });
    for (double x = x0 - 3 * D - 2; x <= x0 + len + 3 * D + 2; x += 1)
        for (double z = -420; z <= -340; z += 1) {
            int id = w.cellIndex(x, z);
            if (id >= 0 && w.cellHeight(id) < -0.02) w.setCellWater(id, 0.0f);
        }
    w.buildGrid();
    Car& c = rigUp(ctx, cfg, 0, x0 - 3 * D - 10, -380, 0);
    double xMax = -1e9, maxSub = -1e9, minGap = 1e9;
    ctx.run(40, [&] {
        drive(c, -380, speed);
        xMax = std::max(xMax, c.frame().com.x);
        const double level = w.waterLevel(c.nodes[0].p.x, c.nodes[0].p.z);
        if (level > -1e8) maxSub = std::max(maxSub, level - intakeY(c)), minGap = std::min(minGap, intakeY(c) - level);
    });
    const bool out = xMax > x0 + len + 3 * D + 2;
    std::printf("CAP wade %s %s depth %.2f m v=%.1f: %s xmax=%.1f%s intake clearance min %.2f m\n", cfg.truck.c_str(), cfg.name.c_str(), D, speed,
                c.hydrolocked ? "HYDROLOCKED" : "ran-fine", xMax - x0, out ? " (crossed)" : " (not out)", minGap);
    ctx.metric("hydrolocked", c.hydrolocked, "", 0, 1);
    (void)maxSub;
}

// ---- Registration ---------------------------------------------------------------------------------------
int reg = [] {
    auto add = [](const std::string& n, std::function<void(Ctx&)> f) { scenarioRegistry().push_back({"cap/" + n, n, std::move(f)}); };
    auto& C = cfgs();
    // Stock: every vehicle as it comes from the catalog (the 4Runner's "stock" is its AS BUILT truck: 35s, snorkel, winch).
    for (const char* t : {"blazer", "bronco", "4runner", "tacoma"}) C.push_back({"stock", t, nullptr, nullptr, nullptr, nullptr, false});
    // Legacy: what probe.cpp's "SETUP" did to a stock Bronco (front locker forced on, 12 psi, sway off), to check this harness against the handoff.
    for (const char* t : {"blazer", "bronco", "4runner", "tacoma"}) C.push_back({"stocknl", t, nullptr, nullptr, nullptr, nullptr, false, false, true});  // stock, no lockers in
    for (const char* t : {"blazer", "bronco", "4runner", "tacoma"}) C.push_back({"stockair", t, nullptr, nullptr, nullptr, nullptr, true});  // stock parts, driver set up (12 psi, sway off, lockers in)
    C.push_back({"legacy", "bronco", nullptr, nullptr, nullptr, nullptr, true, true});
    // Setups (garage parts, chosen by ledge ablation, see the doc). "first" = the first guess (35/37s + the lockers/crawl box), kept for the record.
    C.push_back({"first", "blazer", nullptr, "4 IN LIFT AND 35S", "4.56 GEARS AND LOCKERS", nullptr, true});
    C.push_back({"first", "bronco", nullptr, "3 IN LIFT AND 37S", "4.70 AND LOCKERS", nullptr, true});
    C.push_back({"first", "4runner", nullptr, "LONG TRAVEL AND 37S", "CRAWL BOX", nullptr, true});
    C.push_back({"first", "tacoma", nullptr, "LONG TRAVEL AND 35S", "CRAWL BOX", nullptr, true});
    C.push_back({"setup", "blazer", "ZZ4 CRATE 350", "4 IN LIFT AND 35S", "4.56 GEARS AND LOCKERS", nullptr, true});
    C.push_back({"setup", "bronco", "RAPTOR 3.0 V6 SWAP", "3 IN LIFT AND 37S", "4.70 AND LOCKERS", nullptr, true});
    C.push_back({"setup", "4runner", nullptr, nullptr, "4.88 AND LOCKERS", nullptr, true});
    C.push_back({"setup", "tacoma", "MAGNUSON SUPERCHARGER", "LONG TRAVEL AND 35S", "CRAWL BOX", nullptr, true});
    // Ablations (name -> parts), for choosing the setups.
    C.push_back({"a-blazer-lift-only", "blazer", nullptr, "4 IN LIFT AND 35S", nullptr, nullptr, true});
    C.push_back({"a-blazer-lockers-only", "blazer", nullptr, nullptr, "4.56 GEARS AND LOCKERS", nullptr, true});
    C.push_back({"a-blazer-454", "blazer", "454 BIG BLOCK", "4 IN LIFT AND 35S", "4.56 GEARS AND LOCKERS", nullptr, true});
    C.push_back({"a-blazer-zz4", "blazer", "ZZ4 CRATE 350", "4 IN LIFT AND 35S", "4.56 GEARS AND LOCKERS", nullptr, true});
    C.push_back({"a-blazer-trail", "blazer", nullptr, "SOFT TRAIL LEAVES", "4.56 GEARS AND LOCKERS", nullptr, true});
    C.push_back({"a-bronco-sasq", "bronco", nullptr, "SASQUATCH PACKAGE", "4.70 AND LOCKERS", nullptr, true});
    C.push_back({"a-bronco-37-gears", "bronco", nullptr, "3 IN LIFT AND 37S", "4.70 GEARS", nullptr, true});
    C.push_back({"a-bronco-37-manual", "bronco", nullptr, "3 IN LIFT AND 37S", "7-SPEED MANUAL", nullptr, true});
    C.push_back({"a-bronco-37-stage1", "bronco", "2.7 STAGE 1 TUNE", "3 IN LIFT AND 37S", "4.70 AND LOCKERS", nullptr, true});
    C.push_back({"a-4runner-asbuilt-crawl", "4runner", nullptr, nullptr, "CRAWL BOX", nullptr, true});
    C.push_back({"a-4runner-37-lockers", "4runner", nullptr, "LONG TRAVEL AND 37S", "4.88 AND LOCKERS", nullptr, true});
    C.push_back({"a-4runner-37-1gr", "4runner", "1GR SUPERCHARGED", "LONG TRAVEL AND 37S", "CRAWL BOX", nullptr, true});
    C.push_back({"a-tacoma-33-crawl", "tacoma", nullptr, "3 IN LIFT AND 33S", "CRAWL BOX", nullptr, true});
    C.push_back({"a-tacoma-35-lockers", "tacoma", nullptr, "LONG TRAVEL AND 35S", "4.88 AND FRONT LOCKER", nullptr, true});
    C.push_back({"a-tacoma-35-sc", "tacoma", "MAGNUSON SUPERCHARGER", "LONG TRAVEL AND 35S", "CRAWL BOX", nullptr, true});
    C.push_back({"a-tacoma-crawl-only", "tacoma", nullptr, nullptr, "CRAWL BOX", nullptr, true});
    C.push_back({"a-bronco-37-raptor", "bronco", "RAPTOR 3.0 V6 SWAP", "3 IN LIFT AND 37S", "4.70 AND LOCKERS", nullptr, true});
    C.push_back({"a-bronco-37-raptor-manual", "bronco", "RAPTOR 3.0 V6 SWAP", "3 IN LIFT AND 37S", "7-SPEED MANUAL", nullptr, true});
    C.push_back({"a-4runner-lockers", "4runner", nullptr, nullptr, "4.88 AND LOCKERS", nullptr, true});
    C.push_back({"a-4runner-crawl-1gr", "4runner", "4.0 V6 1GR SWAP", nullptr, "CRAWL BOX", nullptr, true});
    C.push_back({"a-4runner-lockers-fact", "4runner", "FACTORY 3.4 V6", nullptr, "4.88 AND LOCKERS", nullptr, true});
    C.push_back({"a-4runner-37-crawl-fact", "4runner", "FACTORY 3.4 V6", "LONG TRAVEL AND 37S", "CRAWL BOX", nullptr, true});
    C.push_back({"a-tacoma-35-crawl-tune", "tacoma", "INTAKE AND TUNE", "LONG TRAVEL AND 35S", "CRAWL BOX", nullptr, true});
    C.push_back({"a-tacoma-35-lockers-sc", "tacoma", "MAGNUSON SUPERCHARGER", "LONG TRAVEL AND 35S", "4.88 AND FRONT LOCKER", nullptr, true});
    C.push_back({"stock", "jesko", nullptr, nullptr, nullptr, nullptr, false});
    // Every cfg gets the full set of scenarios registered below, so a new variant is one line above.
    for (const Cfg& cf : C) {
        const std::string key = cf.truck + "-" + cf.name;
        add("info/" + key, [cf](Ctx& c) { infoProbe(c, cf); });
        for (bool air : {false, true}) {
            const std::string p = air ? "air" : "norm";
            for (int cm = cf.truck == "jesko" ? 5 : 25; cm <= 90; cm += 5)
                for (int dm : {5, 6, 7, 8, 10, 12, 14, 17, 20})  // crawl speed (dm/s): the same ledge three ways, since one try is noisy
                    add("ledge/" + key + "-" + p + "-" + std::to_string(cm) + "-s" + std::to_string(dm),
                        [cf, air, cm, dm](Ctx& c) { ledgeProbe(c, cf, air ? 12 : 0, cm / 100.0, dm / 10.0); });
            for (int deg : {35, 45, 55, 65, 75}) {
                char b[96];
                std::snprintf(b, sizeof b, "wall/%s-%s-%d", key.c_str(), p.c_str(), deg);
                add(b, [cf, air, deg](Ctx& c) { slopeProbe(c, cf, air ? 12 : 0, deg, 4.0, "wall"); });
            }
            for (int deg : {20, 25, 30, 35, 40, 42, 45, 50}) {
                char b[96];
                std::snprintf(b, sizeof b, "slab/%s-%s-%d", key.c_str(), p.c_str(), deg);
                add(b, [cf, air, deg](Ctx& c) { slopeProbe(c, cf, air ? 12 : 0, deg, 14 * std::sin(deg * M_PI / 180), "slab"); });
                for (int dm : {7, 20}) {  // the same slab at other crawl speeds, to see how noisy it is
                    std::snprintf(b, sizeof b, "slabv/%s-%s-%d-v%d", key.c_str(), p.c_str(), deg, dm);
                    add(b, [cf, air, deg, dm](Ctx& c) { slopeProbe(c, cf, air ? 12 : 0, deg, 14 * std::sin(deg * M_PI / 180), "slabv", dm / 10.0); });
                }
            }
        }
        for (int deg : {25, 30, 35, 40})
            for (Surface s : {SURF_DIRT, SURF_ROCK}) {
                char b[96];
                std::snprintf(b, sizeof b, "camber/%s-%s-%d", key.c_str(), s == SURF_DIRT ? "dirt" : "rock", deg);
                // stock = as sold (normal pressure); setup = aired to 12 psi
                const bool air = cf.driverSetup;
                add(b, [cf, s, deg, air](Ctx& c) { camberProbe(c, cf, air ? 12 : 0, deg, s); });
                for (int dm : {10, 20}) {
                    std::snprintf(b, sizeof b, "camberv/%s-%s-%d-v%d", key.c_str(), s == SURF_DIRT ? "dirt" : "rock", deg, dm);
                    add(b, [cf, s, deg, air, dm](Ctx& c) { camberProbe(c, cf, air ? 12 : 0, deg, s, dm / 10.0); });
                }
            }
        for (int cm = 30; cm <= 260; cm += 5) {
            add("wade/" + key + "-" + std::to_string(cm), [cf, cm](Ctx& c) { wadeProbe(c, cf, cm / 100.0, 2.0); });
            for (int dm : {5, 40}) add("wadev/" + key + "-" + std::to_string(cm) + "-v" + std::to_string(dm), [cf, cm, dm](Ctx& c) { wadeProbe(c, cf, cm / 100.0, dm / 10.0); });
        }
    }
    return 0;
}();

}  // namespace
