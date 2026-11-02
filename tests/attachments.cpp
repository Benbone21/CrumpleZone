// Garage attachments (attachments.h, docs/parts-simplified.md, "Attachments"): the winch and the steel bumper fit every vehicle, independently;
// the winch line comes out of the attachment's fairlead on a vehicle that had no winch; the bumper crushes less in a wall
// crash (measured against the same vehicle without it); the sim stays stable with it at speed and landing jumps; old
// saves load with the attachments as the factory had them; and other players see them.
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "../src/anchors.h"
#include "../src/attachments.h"
#include "../src/career.h"
#include "../src/carsave.h"
#include "../src/interact.h"
#include "../src/net.h"
#include "../src/platform.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

int vehicleIndex(const char* id) {
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        if (std::string(vehicleCatalog()[v].id) == id) return v;
    return -1;
}

int optionNamed(int v, int slot, const char* name) {
    for (int o = 0; o < (int)modOptions(v, slot).size(); o++)
        if (modOptions(v, slot)[o].name == name) return o;
    return -1;
}

// The vehicle with the winch and / or the steel bumper fitted (and everything else as it came).
const VehicleSpec& fitted(int v, bool winch, bool bumper) {
    ModSet m;
    m.pick[MOD_WINCH] = winch ? optionNamed(v, MOD_WINCH, "WINCH") : optionNamed(v, MOD_WINCH, "NO WINCH");
    m.pick[MOD_BUMPER] = bumper ? 1 : 0;
    return tunedSpec(v, m);
}

double bodyLength(const Car& c) {
    const Frame f = c.frame();
    double lo = 1e9, hi = -1e9;
    for (int i = 0; i < c.bodyNodes; i++) {
        const double x = dot(c.nodes[i].p - f.com, f.fwd);
        lo = std::min(lo, x), hi = std::max(hi, x);
    }
    return hi - lo;
}

double totalMass(const Car& c) {
    double m = 0;
    for (const Node& n : c.nodes) m += n.m;
    return m;
}

// The explicit step's margin: h * sqrt(k (1/ma + 1/mb)) for each beam (stable below 2), the worst of them;
// `bumperOnly`: of the beams on the bumper's nodes.
double worstOmegaH(const Car& c, bool bumperOnly) {
    const double h = 1.0 / 2000.0;
    std::vector<char> on(c.nodes.size(), 0);
    for (int n : c.bumperNodes) on[n] = 1;
    double w = 0;
    for (const Beam& b : c.beams) {
        if (b.k <= 0 || (bumperOnly && !on[b.a] && !on[b.b])) continue;
        w = std::max(w, h * std::sqrt(b.k * (1 / c.nodes[b.a].m + 1 / c.nodes[b.b].m)));
    }
    return w;
}

void stand(Sim& sim, Walker& wk, const V3& at, V3& eye) {
    wk.active = true;
    wk.v = V3();
    wk.p = V3(at.x, sim.world.height(at.x, at.z), at.z);
    eye = wk.p + V3(0, 1.6, 0);
}

std::string holdE(Sim& sim, Interact& it, Walker& wk, const V3& eye, const V3& look, double seconds) {
    std::string said;
    for (double t = 0; t < seconds; t += 1.0 / 60) {
        const std::string s = it.update(sim, wk, eye, look, true, 1.0 / 60);
        if (!s.empty()) said = s;
    }
    it.update(sim, wk, eye, look, false, 1.0 / 60);
    return said;
}

}  // namespace

SCENARIO(attachFitAll, "attachments/every-vehicle", "Every vehicle takes the winch and the steel bumper, each on its own or both: plain names, the winch's pull and its fairlead ahead of the nose, the bumper's nodes on its face, the weight they add, and nothing fitted is the factory vehicle itself") {
    const auto& cat = vehicleCatalog();
    for (int v = 0; v < (int)cat.size(); v++) {
        const VehicleSpec& stock = cat[v];
        const std::string id = stock.id;
        const bool factoryWinch = stock.extras & EXTRA_WINCH;
        const auto& w = modOptions(v, MOD_WINCH);
        const auto& b = modOptions(v, MOD_BUMPER);
        ctx.check(id + ": WINCH and NO WINCH, the factory's first", w.size() == 2 && w[0].name == (factoryWinch ? "WINCH" : "NO WINCH") &&
                                                                       w[1].name == (factoryWinch ? "NO WINCH" : "WINCH"));
        ctx.check(id + ": STOCK BUMPER and STEEL BUMPER", b.size() == 2 && b[0].name == "STOCK BUMPER" && b[1].name == "STEEL BUMPER");
        ctx.check(id + ": nothing fitted is the catalog vehicle (bit-identical physics)", &tunedSpec(v, ModSet()) == &stock);
        for (int combo = 1; combo < 4; combo++) {
            const bool wi = combo & 1, bu = combo & 2;
            const VehicleSpec& s = fitted(v, wi, bu);
            const std::string what = id + (wi ? " +winch" : "") + (bu ? " +bumper" : "");
            // (fitted(v, false, ...) on a vehicle with a factory winch is NO WINCH: it comes off.)
            ctx.check(what + ": fitted as picked, each on its own", ((s.extras & EXTRA_WINCH) != 0) == wi && s.steelBumper == bu && s.winchKit == (wi && !factoryWinch));
            if (wi && !factoryWinch) ctx.check(what + ": a 12,000 lb winch", std::fabs(s.winchPull - kWinchKitPull) < 1);
            Car& c = ctx.add(s, -60 + 14 * (combo - 1), -40 - 12 * v, 0);
            Car& ref = ctx.add(stock, -60 + 14 * (combo - 1), 60 + 12 * v, 0);
            double items = 0, stockItems = 0;
            for (const MassItem& it : s.items) items += it.kg;
            for (const MassItem& it : stock.items) stockItems += it.kg;
            const double added = (wi && !factoryWinch ? kWinchKitKg : 0) + (bu ? kSteelBumperKg : 0);
            ctx.check(what + ": its listed weight", added == 0 || items - stockItems >= added - (factoryWinch && !wi ? 40 : 0) - 1e-9);
            ctx.metric(what + ": weight added to the built car (kg)", totalMass(c) - totalMass(ref), "kg", items - stockItems - 2, items - stockItems + 2);
            const AttachFit f = attachFit(s);
            if (s.winchKit || bu)
                ctx.check(what + ": the front recovery point is the fairlead (or the bumper's face), ahead of the nose",
                          std::fabs(c.hookRest[0].x - f.fairlead.x) < 1e-9 && f.fairlead.x > stock.length / 2 + 0.1);
            if (bu) {
                bool ahead = !c.bumperNodes.empty();
                for (int n : c.bumperNodes) ahead &= std::fabs(c.restLocal[n].x - f.faceX) < 1e-6 && c.nodes[n].collide;
                ctx.check(what + ": the bumper's face nodes are on its face and collide", ahead, std::to_string(c.bumperNodes.size()) + " nodes");
                ctx.check(what + ": its drawing has the bumper's hex to ride on", c.bumperHex[0] >= 0);
            }
        }
        ctx.run(1.5);
        bool ok = true;
        for (const Car& c : ctx.sim.cars) ok &= c.finite() && c.damage < 0.003;
        ctx.check(id + ": all of them settle, unharmed", ok);
        ctx.sim.cars.clear();
        ctx.sim.player = 0;
    }
}

SCENARIO(attachWinchKit, "attachments/winch-kit", "A Blazer (no winch from the factory) can't winch; with the WINCH attachment its line comes out of the attachment's fairlead: grabbed there on foot, walked out to a stake, hooked, and reeling in pulls the truck; the same with the steel bumper too, and NO WINCH on a Bronco takes its winch away") {
    const int blazer = vehicleIndex("blazer"), bronco = vehicleIndex("bronco");
    ctx.sim = Sim();
    ctx.sim.world = World(MAP_PROVING_GROUND);
    Sim& sim = ctx.sim;
    sim.anchorStock.fill(anchors::kCareerStart);
    {
        Car& c = ctx.add(vehicleCatalog()[blazer], -60, -300, 0);  // (parked out of the way: it stays, the player's car)
        ctx.run(0.5);
        const int ci = (int)(&c - sim.cars.data());
        RopeEnd e;
        e.kind = ANCHOR_STAKE;
        e.p = c.hookPoint(0) + V3(10, 0, 0);
        ctx.check("stock Blazer: no winch", sim.hookWinchTo(ci, e) == "NO WINCH ON THIS TRUCK" && sim.ropes.empty());
        Walker wk;
        Interact it;
        V3 eye;
        const V3 fl = c.hookPoint(0);
        stand(sim, wk, fl + V3(1.2, 0, 0.3), eye);
        it.update(sim, wk, eye, norm(fl - eye), false, 1.0 / 60);
        ctx.check("...and at its front E offers no winch line", !(it.current.kind == IA_GRAB_LINE && it.current.lineKind == ROPE_WINCH), it.current.prompt);
    }
    for (bool bumper : {false, true}) {
        const std::string tag = bumper ? "winch in the steel bumper" : "winch attachment";
        ctx.sim.ropes.clear();
        const VehicleSpec& spec = fitted(blazer, true, bumper);
        Car& c = ctx.add(spec, -60, bumper ? -250 : -270, 0);
        ctx.focus(c);
        ctx.run(0.8);
        const int ci = (int)(&c - sim.cars.data());
        // The fairlead: where the attachment's drawing has it, on the moving truck.
        const AttachFit f = attachFit(spec);
        const Frame fr = c.frame();
        const V3 fl = c.hookPoint(0);
        const double ahead = dot(fl - fr.com, fr.fwd);
        ctx.metric(tag + ": the fairlead ahead of the centre (m)", ahead, "m", f.fairlead.x - 0.35, f.fairlead.x + 0.35);
        ctx.metric(tag + ": ...at its height over the ground (m)", fl.y - sim.world.height(fl.x, fl.z), "m", f.fairlead.y - 0.12, f.fairlead.y + 0.12);
        Walker wk;
        Interact it;
        V3 eye;
        stand(sim, wk, fl + V3(1.2, 0, 0.3), eye);
        V3 look = norm((fl + V3(0, 0.15, 0)) - eye);
        it.update(sim, wk, eye, look, false, 1.0 / 60);
        ctx.check(tag + ": at the fairlead E offers the winch line", it.current.kind == IA_GRAB_LINE && it.current.lineKind == ROPE_WINCH, it.current.prompt);
        const std::string got = holdE(sim, it, wk, eye, look, 1.3);
        ctx.check(tag + ": holding E grabs it", sim.handLine.on() && sim.handLine.car == ci && sim.handLine.kind == ROPE_WINCH, got);
        // Out to a stake spot 12 m ahead, and hook it there.
        const V3 spot = fl + V3(12, 0, 0);
        stand(sim, wk, spot, eye);
        sim.handLine.hand = wk.p + V3(0, 1, 0);
        look = norm(V3(1, -0.8, 0));
        it.update(sim, wk, eye, look, false, 1.0 / 60);
        const std::string hooked = holdE(sim, it, wk, eye, look, 2.0);
        const int r = sim.ropeOn(ci, ROPE_WINCH);
        ctx.check(tag + ": hooked to a stake", hooked.rfind("WINCH HOOKED", 0) == 0 && r >= 0, hooked);
        if (r < 0) return;
        ctx.check(tag + ": a 12,000 lb winch's pull", std::fabs(sim.ropes[r].stallN - kWinchKitPull) < 1, std::to_string(sim.ropes[r].stallN));
        wk.active = false;
        const double len0 = sim.ropes[r].length, x0 = c.frame().com.x;
        sim.setWinch(ci, 1);
        ctx.run(8.0);
        sim.setWinch(ci, 0);
        const int r2 = sim.ropeOn(ci, ROPE_WINCH);
        ctx.check(tag + ": reeling in takes line in", r2 >= 0 && sim.ropes[r2].length < len0 - 0.3, r2 >= 0 ? std::to_string(len0 - sim.ropes[r2].length) : "gone");
        ctx.metric(tag + ": ...and pulls the truck toward the stake (m)", c.frame().com.x - x0, "m", 0.3, 12);
        sim.dropWinch(ci, false);
        ctx.check(tag + ": unhooked", sim.ropeOn(ci, ROPE_WINCH) < 0);
    }
    // A Bronco with its factory winch taken off.
    Car& b = ctx.add(fitted(bronco, false, false), -60, -230, 0);
    ctx.run(0.3);
    RopeEnd e;
    e.kind = ANCHOR_STAKE;
    e.p = b.hookPoint(0) + V3(10, 0, 0);
    ctx.check("a Bronco with NO WINCH can't winch", sim.hookWinchTo((int)(&b - sim.cars.data()), e) == "NO WINCH ON THIS TRUCK");
    ctx.check("...and a stock one still can (its factory winch)", (vehicleCatalog()[bronco].extras & EXTRA_WINCH) && modIsFactory(bronco, MOD_WINCH, optionNamed(bronco, MOD_WINCH, "WINCH")));
}

// The same vehicle into the concrete wall at 50 km/h (damage/wall-50) with and without the steel bumper, side by side.
static int bumperCrashScenarios = [] {
    for (const char* id : {"civic", "blazer", "tacoma", "bronco", "4runner", "baja", "jesko", "utv"}) {
        const std::string name = std::string("attachments/bumper-wall/") + id;
        scenarioRegistry().push_back({name, "50 km/h into the wall, with and without the steel bumper: the bumper takes the hit, and the body behind it crumples less", [id](Ctx& ctx) {
                                          const int v = vehicleIndex(id);
                                          const VehicleSpec& stock = vehicleCatalog()[v];
                                          const VehicleSpec& armored = fitted(v, false, true);
                                          // (The bumper sticks out ahead of the nose: both start with their fronts as far from the wall.)
                                          Car& a = ctx.add(stock, 100 - stock.length / 2, -9, 0);
                                          Car& b = ctx.add(armored, 100 - stock.length / 2 - attachFit(armored).depth, 9, 0);
                                          ctx.focus(b);
                                          ctx.run(1.0);
                                          const double La = bodyLength(a), Lb = bodyLength(b);
                                          ctx.launch(a, 50 / 3.6);
                                          ctx.launch(b, 50 / 3.6);
                                          double peak = 0;
                                          ctx.run(2.5, [&] {
                                              for (int n : b.bumperNodes) peak = std::max(peak, len(b.nodes[n].v));
                                          });
                                          const double ca = La - bodyLength(a), cb = Lb - bodyLength(b);
                                          ctx.note(std::string(id) + ": crush " + std::to_string(ca * 100) + " cm stock, " + std::to_string(cb * 100) + " cm with the steel bumper; damage " +
                                                   std::to_string(a.damage * 100) + " % / " + std::to_string(b.damage * 100) + " %");
                                          ctx.metric("crumple depth, stock (cm)", ca * 100, "cm", 0, 100);
                                          ctx.metric("crumple depth, steel bumper (cm)", cb * 100, "cm", -2, 100);
                                          ctx.metric("bumper / stock crush", cb / std::max(ca, 1e-3), "", -0.2, 0.8);
                                          // (A stiffer nose stops the car harder: a rear engine behind it takes more of a jolt on its mounts.)
                                          ctx.metric("engine health with the bumper", b.engineHealth, "", stock.rearEngine ? 0.6 : a.engineHealth - 0.02, 1.0);
                                          ctx.check("the bumper stops at the wall", b.frame().com.x < 114 - 0.5 && b.finite());
                                          ctx.metric("fastest bumper node (no node flung off, m/s)", peak, "m/s", 0, 20);
                                      }});
    }
    return 0;
}();

// Stable at speed and over a jump with the bumper (and the winch in it): the stiff steel members on heavy nodes step stably.
static int bumperStableScenarios = [] {
    for (const char* id : {"civic", "blazer", "jesko", "baja", "utv", "buggy"}) {
        const std::string name = std::string("attachments/bumper-stable/") + id;
        scenarioRegistry().push_back({name, "With the steel bumper and the winch: the explicit step's margin on the bumper's members, flat out down the straight, and the kicker jump's landing", [id](Ctx& ctx) {
                                          const int v = vehicleIndex(id);
                                          const VehicleSpec& s = fitted(v, true, true);
                                          Car& ref = ctx.add(vehicleCatalog()[v], 20, 60, 0);
                                          Car& c = ctx.add(s, 20, -30, 0);
                                          ctx.focus(c);
                                          const double wb = worstOmegaH(c, true), wAll = worstOmegaH(c, false), wStock = worstOmegaH(ref, false);
                                          ctx.metric("step margin on the bumper's members (h*omega; explicit limit 2)", wb, "", 0, 1.0);
                                          ctx.metric("worst member in the car with it / stock", wAll / std::max(wStock, 1e-9), "", 0, 1.05);
                                          ctx.run(1.0);
                                          ctx.launch(c, 70 / 3.6);
                                          double fastest = 0;
                                          ctx.run(5.0, [&] {
                                              c.throttle = 0.3;
                                              for (const Node& n : c.nodes) fastest = std::max(fastest, len(n.v));
                                          });
                                          ctx.check("lands the kicker jump in one piece", c.finite() && !c.engineDead, "damage " + std::to_string(c.damage * 100) + " %");
                                          ctx.metric("fastest node over the jump (m/s)", fastest, "m/s", 0, 40);
                                          // Flat out on the long lane.
                                          Car& d = ctx.add(s, -120, -50, 0);
                                          ctx.focus(d);
                                          ctx.run(1.0);
                                          ctx.launch(d, 120 / 3.6);
                                          double up = 1;
                                          ctx.run(4.0, [&] {
                                              d.throttle = 1;
                                              up = std::min(up, d.frame().up.y);
                                          });
                                          ctx.check("120 km/h down the lane: upright and whole", d.finite() && up > 0.9, std::to_string(up));
                                          ctx.metric("damage at speed", d.damage * 100, "%", 0, 0.5);
                                      }});
    }
    return 0;
}();

SCENARIO(attachOldSaves, "attachments/old-saves", "Saves from before the attachments (five picks: garage.txt, a career's vehicles, a car's condition text) load with the winch and bumper as the factory had them; the new ones keep them; an older build reads the new garage line's first five") {
    const int blazer = vehicleIndex("blazer"), runner = vehicleIndex("4runner");
    makeDir("build/test-attach");
    // garage.txt from 0.4.14 (parts 2, five picks).
    const std::string gp = "build/test-attach/garage-" + std::to_string(getpid()) + ".txt";
    {
        std::ofstream f(gp);
        f << "parts 2\nblazer 1 1 0 0 0\n4runner 0 0 1 0 0\n";
    }
    Garage g;
    ctx.check("an old garage loads", g.load(gp));
    ctx.check("its picks are kept", g.fitted[blazer].pick[MOD_ENGINE] == 1 && g.fitted[blazer].pick[MOD_SUSPENSION] == 1 && g.fitted[runner].pick[MOD_TRANSMISSION] == 1);
    ctx.check("its attachments are the factory's (the 4Runner keeps its winch)", g.fitted[blazer].pick[MOD_WINCH] == 0 && g.fitted[blazer].pick[MOD_BUMPER] == 0 &&
                                                                                       g.fitted[runner].pick[MOD_WINCH] == 0 && (g.spec(runner).extras & EXTRA_WINCH) && !(g.spec(blazer).extras & EXTRA_WINCH));
    g.fitted[blazer].pick[MOD_WINCH] = 1, g.fitted[blazer].pick[MOD_BUMPER] = 1;
    ctx.check("saved with the attachments", g.save(gp));
    Garage h;
    ctx.check("...and loaded back", h.load(gp) && h.fitted[blazer].pick[MOD_WINCH] == 1 && h.fitted[blazer].pick[MOD_BUMPER] == 1 && h.fitted[blazer].pick[MOD_ENGINE] == 1 &&
                                         h.spec(blazer).winchKit && h.spec(blazer).steelBumper);
    {  // An older build's reader: "%63s %d %d %d %d %d" per line.
        std::ifstream f(gp);
        std::string line;
        bool five = false;
        while (std::getline(f, line)) {
            char id[64];
            int p[5];
            if (std::sscanf(line.c_str(), "%63s %d %d %d %d %d", id, &p[0], &p[1], &p[2], &p[3], &p[4]) == 6 && std::string(id) == "blazer") five = p[0] == 1 && p[1] == 1;
        }
        ctx.check("an older build reads the new line's first five picks", five);
    }
    std::remove(gp.c_str());
    // A career profile's vehicles: one line from before (five picks, then drove=), one from now.
    const std::string cp = "build/test-attach/career-" + std::to_string(getpid()) + ".txt";
    {
        std::ofstream f(cp);
        f << "parts 2\ncash 900\nveh " << blazer << " w1 1 0 0 0 0 drove=12.500\nveh " << blazer << " w2 0 0 0 0 0 1 1 drove=3.000\n";
    }
    Career c;
    c.on = true;
    ctx.check("a career loads", c.load(cp) && c.vehicles.size() == 2);
    if (c.vehicles.size() == 2) {
        ctx.check("its old vehicle: the engine kept, the factory's attachments, the drive time read", c.vehicles[0].mods.pick[MOD_ENGINE] == 1 &&
                                                                                                         c.vehicles[0].mods.pick[MOD_WINCH] == 0 && c.vehicles[0].mods.pick[MOD_BUMPER] == 0 &&
                                                                                                         std::fabs(c.vehicles[0].drove - 12.5) < 1e-6);
        ctx.check("its new one: the winch and the bumper", c.vehicles[1].mods.pick[MOD_WINCH] == 1 && c.vehicles[1].mods.pick[MOD_BUMPER] == 1 && std::fabs(c.vehicles[1].drove - 3) < 1e-6);
    }
    std::remove(cp.c_str());
    ctx.check("the career sells both, like any part", Career::price(blazer, MOD_WINCH, 1) > 0 && Career::price(blazer, MOD_BUMPER, 1) > 0 &&
                                                       Career::tier(blazer, MOD_WINCH, 1) == 1);
    {
        Career fresh;
        fresh.on = true;
        fresh.startFresh();
        std::string why;
        fresh.cash = 100000;
        ctx.check("a career doesn't start with them; it buys them", !fresh.owns(blazer, MOD_WINCH, 1) && !fresh.owns(blazer, MOD_BUMPER, 1) &&
                                                                         fresh.buy(blazer, MOD_WINCH, 1, why) && fresh.buy(blazer, MOD_BUMPER, 1, why) &&
                                                                         fresh.owns(blazer, MOD_WINCH, 1) && fresh.owns(blazer, MOD_BUMPER, 1));
        ctx.check("taking a factory winch off costs nothing", fresh.owns(runner, MOD_WINCH, optionNamed(runner, MOD_WINCH, "NO WINCH")));
        Career sandbox;
        ctx.check("sandbox has them all", sandbox.owns(blazer, MOD_WINCH, 1) && sandbox.owns(blazer, MOD_BUMPER, 1));
    }
    // A car's condition text from before: "mods=" with five picks.
    {
        Car& car = ctx.add(vehicleCatalog()[blazer], 0, 0, 0);
        ctx.run(0.5);
        std::string text = carsave::capture(ctx.sim, car);
        const size_t at = text.find("mods="), end = text.find(' ', at);
        ctx.check("a condition text names seven picks now", at != std::string::npos && text.substr(at, end - at) == "mods=0,0,0,0,0,0,0", text.substr(at, end - at));
        text.replace(at, end - at, "mods=0,0,0,0,0");
        ctx.check("one with five reads as the factory's attachments", carsave::modsOf(text).stock());
        ctx.check("...and restores onto the car", carsave::restore(ctx.sim, car, text));
        Car& kit = ctx.add(fitted(blazer, true, true), 0, 12, 0);
        ctx.run(0.5);
        const std::string kt = carsave::capture(ctx.sim, kit);
        const ModSet km = carsave::modsOf(kt);
        ctx.check("a fitted car's text keeps its attachments", km.pick[MOD_WINCH] == 1 && km.pick[MOD_BUMPER] == 1 && carsave::restore(ctx.sim, kit, kt));
    }
}

SCENARIO(attachNet, "attachments/net", "Host and guest see the attachments on each other's vehicles: the host's Blazer with the winch and the steel bumper, the guest's Civic with the bumper; a refit after joining arrives") {
    const int blazer = vehicleIndex("blazer"), civic = vehicleIndex("civic");
    Car& hostCar = ctx.add(fitted(blazer, true, true), 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    const int port = 27600 + (int)(getpid() % 300) + 11;
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    Sim sim2;
    sim2.world = World(MAP_PROVING_GROUND);
    sim2.setupEmpty();
    sim2.addCar(fitted(civic, false, true), V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
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
    auto frameBoth = [&] {
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        client.receive(sim2, ctx.t);
        ctx.sim.advance(Ctx::kFrame, 1000);
        sim2.advance(Ctx::kFrame, 1000);
        client.send(sim2, ctx.t);
        usleep(300);
    };
    auto remoteOf = [](Sim& s) -> Car* {
        for (Car& c : s.cars)
            if (c.remote) return &c;
        return nullptr;
    };
    ctx.run(1.5, frameBoth);
    Car* onClient = remoteOf(sim2);
    Car* onHost = remoteOf(ctx.sim);
    ctx.check("both see the other", onClient && onHost);
    if (!onClient || !onHost) return;
    ctx.check("the guest sees the host's winch and steel bumper", onClient->spec == &fitted(blazer, true, true) && onClient->spec->winchKit && onClient->spec->steelBumper &&
                                                                   !onClient->bumperNodes.empty());
    ctx.check("the host sees the guest's steel bumper (and no winch)", onHost->spec == &fitted(civic, false, true) && onHost->spec->steelBumper && !onHost->spec->winchKit);
    // A refit: the winch comes off the host's Blazer (as the garage does it: a new spec, a rebuild).
    hostCar.spec = &fitted(blazer, false, true);
    hostCar.respawn(hostCar.spawnPos, hostCar.spawnYaw);
    ctx.run(1.0, frameBoth);
    onClient = remoteOf(sim2);
    ctx.check("a refit after joining reaches the guest", onClient && onClient->spec == &fitted(blazer, false, true) && !onClient->spec->winchKit);
}
