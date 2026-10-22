// The showroom (garage preview) runs a second Sim next to the game's: it must not leak into it. Each Sim owns its
// world, cars and scratch, so the main one steps bit-identically with or without a studio Sim stepped alongside.
#include <algorithm>
#include <cstdint>
#include <cstring>

#include "../src/mods.h"
#include "../src/studio.h"
#include "../src/vehicles.h"
#include "harness.h"

namespace {
// The state fingerprint of tools/bench_sim.cpp: FNV over every node's p and v and every beam's L0 and broken.
uint64_t simFingerprint(const Sim& sim) {
    uint64_t hash = 14695981039346656037ull;
    auto mix = [&](double d) {
        uint64_t bits;
        std::memcpy(&bits, &d, sizeof bits);
        hash = (hash ^ bits) * 1099511628211ull;
    };
    for (const Car& c : sim.cars) {
        for (const Node& n : c.nodes) {
            mix(n.p.x), mix(n.p.y), mix(n.p.z);
            mix(n.v.x), mix(n.v.y), mix(n.v.z);
        }
        for (const Beam& b : c.beams) mix(b.L0), mix(b.broken);
    }
    return hash;
}

// Two driven cars on the proving ground, 10 m apart.
void mainSim(Sim& s) {
    s.setupEmpty();
    s.world = World(MAP_PROVING_GROUND);
    for (int k = 0; k < 2; k++) {
        const double x = -80 + 10 * k, z = -60;
        s.addCar(vehicleCatalog()[k * 2], V3(x, s.world.clearY(x, z) + 0.05, z), 0, V3(0.4, 0.5, 0.6));
        s.cars.back().throttle = 0.5;
    }
}
}  // namespace

SCENARIO(showroomIsolation, "showroom/isolation", "A studio Sim stepped alongside the main one leaves it bit-identical") {
    Sim a, b, s;
    mainSim(a);
    mainSim(b);
    s.setupEmpty();
    s.world = studioWorld();
    s.addCar(vehicleCatalog()[3], V3(0, s.world.clearY(0, 0) + 0.05, 0), 0, V3(0.2, 0.2, 0.2));
    Car& c = s.cars.back();
    c.gear = 0, c.handbrake = 1, c.throttle = 1;
    for (int f = 0; f < 600; f++) a.advance(1 / 60.0, 64);
    for (int f = 0; f < 600; f++) {
        b.advance(1 / 60.0, 64);
        s.advance(1 / 60.0, 64);
    }
    ctx.check("main sim unchanged by a studio sim", simFingerprint(a) == simFingerprint(b));
}

// applyMods fills an owned spec; tunedSpec (the cached path) must agree with it on every vehicle.
SCENARIO(showroomApplyMods, "showroom/apply-mods", "applyMods into an owned spec matches tunedSpec, and clamps picks") {
    const int n = (int)vehicleCatalog().size();
    for (int v = 0; v < n; v++) {
        ModSet ones, last, wild;
        for (int k = 0; k < MOD_SLOTS; k++) ones.pick[k] = 1, last.pick[k] = (int)modOptions(v, k).size() - 1;
        const int w[MOD_SLOTS] = {999, -5, 1, 0, 2};
        std::copy(w, w + MOD_SLOTS, wild.pick);
        const std::pair<const char*, ModSet> sets[] = {{"ones", ones}, {"last", last}, {"wild", wild}};
        for (const auto& [label, m] : sets) {
            VehicleSpec s;
            applyMods(v, m, s);
            const VehicleSpec& t = tunedSpec(v, m);
            ctx.check(std::string(vehicleCatalog()[v].id) + "/" + label,
                      s.modSummary == t.modSummary && s.layerY == t.layerY && s.redline == t.redline && s.finalDrive == t.finalDrive &&
                          s.wheelRadius == t.wheelRadius && std::equal(s.mods, s.mods + MOD_SLOTS, t.mods));
        }
        VehicleSpec s;
        applyMods(v, wild, s);
        ctx.check(std::string(vehicleCatalog()[v].id) + "/clamp",
                  s.mods[0] == (int)modOptions(v, 0).size() - 1 && s.mods[1] == 0);
    }
}
