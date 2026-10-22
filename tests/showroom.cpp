// The showroom (garage preview) runs a second Sim next to the game's: it must not leak into it. Each Sim owns its
// world, cars and scratch, so the main one steps bit-identically with or without a studio Sim stepped alongside.
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>

#include "../src/mods.h"
#include "../src/showroomcore.h"
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

// ---- ShowroomCore, with a fake template cache: a spec is "built" delayFrames updates after its prebuild.
namespace {
struct FakeCache {
    int delayFrames = 2;
    std::map<const VehicleSpec*, int> t;            // held: updates left until it's built
    std::set<const VehicleSpec*> evicted;           // let go (an address the allocator may hand out again)
    std::vector<std::array<int, MOD_SLOTS>> built;  // every prebuild's mods, in order
    bool bad = false;                               // asked about a spec it doesn't hold, told to build one it does, or to evict the preview's
    int refused = 0;                                // evicts turned down: still building
    const ShowroomCore* core = nullptr;             // (set: evicting the spec its preview is showing is bad)
    void tick() {
        for (auto& [s, n] : t)
            if (n > 0) n--;
    }
};
VisualHooks fakeHooks(FakeCache& f) {
    VisualHooks h;
    h.prebuild = [&f](const VehicleSpec& s, const V3&) {
        if (f.t.count(&s)) f.bad = true;
        f.evicted.erase(&s);
        f.t[&s] = f.delayFrames;
        std::array<int, MOD_SLOTS> m;
        std::copy(s.mods, s.mods + MOD_SLOTS, m.begin());
        f.built.push_back(m);
    };
    h.ready = [&f](const VehicleSpec& s) {
        auto it = f.t.find(&s);
        if (it == f.t.end()) return f.bad = true, false;
        return it->second == 0;
    };
    h.evict = [&f](const VehicleSpec& s) {
        auto it = f.t.find(&s);
        if (it == f.t.end()) return f.bad = true, true;
        if (f.core && f.core->preview() && f.core->preview()->spec == &s) f.bad = true;
        if (it->second > 0) return f.refused++, false;
        f.t.erase(it);
        f.evicted.insert(&s);
        return true;
    };
    return h;
}
void tick(ShowroomCore& core, FakeCache& f, double dt) {
    core.update(dt);
    f.tick();
}
bool sameMods(const VehicleSpec& s, const ModSet& m) { return std::equal(s.mods, s.mods + MOD_SLOTS, m.pick); }
bool showsPick(const ShowroomCore& core) { return core.preview() && sameMods(*core.preview()->spec, core.pick()); }
// Height of the floor above the ground, averaged over its nodes (as the mods test in tests/scenarios.cpp measures it).
double floorHeight(const Sim& sim, const Car& c) {
    double sum = 0;
    int n = 0;
    for (int i = 0; i < c.bodyNodes; i++)
        if (std::fabs(c.restLocal[i].y - c.spec->layerY[0]) < 1e-6) {
            sum += c.nodes[i].p.y - sim.world.height(c.nodes[i].p.x, c.nodes[i].p.z);
            n++;
        }
    return sum / std::max(1, n);
}
const V3 kPaint(0.6, 0.1, 0.1);
}  // namespace

SCENARIO(showroomMemory, "showroom/memory", "Picking through 40 engines keeps the showroom's specs bounded, and closing frees them all") {
    FakeCache f;
    ShowroomCore core(fakeHooks(f));
    f.core = &core;
    const int opts = (int)modOptions(3, MOD_ENGINE).size();
    std::vector<const VehicleSpec*> tuned;
    for (int k = 0; k < opts; k++) {
        ModSet m;
        m.pick[MOD_ENGINE] = k;
        tuned.push_back(&tunedSpec(3, m));
    }
    core.open(3, ModSet(), kPaint);
    int most = 0;
    for (int k = 0; k < 40; k++) {
        core.setPick(MOD_ENGINE, k % opts);
        for (int i = 0; i < 6; i++) {
            tick(core, f, 0.2);
            most = std::max(most, core.liveSpecs());
        }
    }
    ctx.metric("most specs held at once", most, "", 1, ShowroomCore::kMaxSpecs + ShowroomCore::kMaxInFlight);
    ctx.check("shows the last pick", showsPick(core));
    ctx.check("the cache never saw a spec it had let go of", !f.bad && !f.evicted.empty());
    ctx.check("never asked to evict one still building", f.refused == 0, std::to_string(f.refused) + " refused");
    bool same = true;
    for (int k = 0; k < opts; k++) {
        ModSet m;
        m.pick[MOD_ENGINE] = k;
        same = same && &tunedSpec(3, m) == tuned[k];
    }
    ctx.check("tunedSpec's specs untouched", same);
    core.close();
    for (int i = 0; i < 10; i++) tick(core, f, 0.2);
    ctx.check("closing frees every spec", core.liveSpecs() == 0 && f.t.empty() && !core.preview(),
              std::to_string(core.liveSpecs()) + " left");
}

SCENARIO(showroomMash, "showroom/mash", "Mashing through picks inside the debounce builds only the last one") {
    FakeCache f;
    ShowroomCore core(fakeHooks(f));
    core.open(3, ModSet(), kPaint);
    for (int i = 0; i < 20 && !core.preview(); i++) tick(core, f, 0.2);
    f.built.clear();
    const int opts = (int)modOptions(3, MOD_ENGINE).size();
    for (int k = 0; k < 30; k++) {
        core.setPick(MOD_ENGINE, 1 + k % (opts - 1));
        tick(core, f, 0.05);
    }
    for (int i = 0; i < 10; i++) tick(core, f, 0.2);
    const int last = 1 + 29 % (opts - 1);
    int others = 0, finals = 0;
    for (const auto& m : f.built) {
        if (std::abs(m[MOD_ENGINE] - last) == 1) continue;  // a neighbour
        others++;
        if (m[MOD_ENGINE] == last) finals++;
    }
    ctx.check("one build, for the final pick", others == 1 && finals == 1, std::to_string(others) + " non-neighbour builds");
    ctx.check("shows it", showsPick(core));
}

SCENARIO(showroomStance, "showroom/stance", "A lift kit on the turntable raises the body by what the part says") {
    FakeCache f;
    ShowroomCore core(fakeHooks(f));
    const VehicleSpec& stock = vehicleCatalog()[3];
    int lift = -1;
    VehicleSpec s;
    for (int k = 1; k < modOwnCount(3, MOD_SUSPENSION) && lift < 0; k++) {
        ModSet m;
        m.pick[MOD_SUSPENSION] = k;
        applyMods(3, m, s);
        if (s.layerY[0] > stock.layerY[0]) lift = k;
    }
    if (!ctx.check("catalog 3 has a lift", lift > 0)) return;
    // Until the pick is on the turntable, then 1 s more to settle.
    auto settle = [&] {
        for (int i = 0; i < 600 && !showsPick(core); i++) tick(core, f, Ctx::kFrame);
        for (int i = 0; i < 60; i++) tick(core, f, Ctx::kFrame);
        return core.preview() ? floorHeight(core.studio(), *core.preview()) : 0.0;
    };
    core.open(3, ModSet(), kPaint);
    const double h0 = settle();
    core.setPick(MOD_SUSPENSION, lift);
    const double h1 = settle();
    ctx.check("shows the lift", showsPick(core));
    const double want = (s.layerY[0] - stock.layerY[0]) * 1000, got = (h1 - h0) * 1000;
    ctx.metric("ride height change vs the part (mm off)", got - want, "mm", -15, 15);
    ctx.note("ride height change " + std::to_string((int)std::lround(got)) + " mm");
}

SCENARIO(showroomEvictWaits, "showroom/evict-waits", "Closing mid-build frees a spec only once the cache lets it go") {
    FakeCache f;
    f.delayFrames = 5;
    ShowroomCore core(fakeHooks(f));
    f.core = &core;
    core.open(3, ModSet(), kPaint);
    for (int i = 0; i < 20 && !core.preview(); i++) tick(core, f, 0.2);
    core.setPick(MOD_ENGINE, 1);
    tick(core, f, 0.2);  // past the debounce: the pick is building
    ctx.check("the pick is building", f.t.size() == 2 && core.fitting());
    core.close();
    bool held = true;
    for (int i = 0; i < 10; i++) {
        tick(core, f, 0.2);
        held = held && core.liveSpecs() >= (int)f.t.size();
    }
    ctx.check("a spec the cache still holds is never freed", held && f.refused > 0, std::to_string(f.refused) + " refused");
    ctx.check("then every spec goes", core.liveSpecs() == 0 && f.t.empty() && !f.bad);
}

SCENARIO(showroomFailures, "showroom/failures", "A build that never comes, and a preview that blows up, both show as failed") {
    FakeCache f;
    f.delayFrames = INT_MAX;
    ShowroomCore core(fakeHooks(f));
    f.core = &core;
    core.open(3, ModSet(), kPaint);
    for (int i = 0; i < 306; i++) tick(core, f, Ctx::kFrame);  // 5.1 s
    ctx.check("times out", core.failed() && !core.preview());
    // Three more that never come: a timed-out build holds no place among the specs or the builds in flight.
    bool timedOut = true;
    for (int k = 1; k <= 3; k++) {
        core.setPick(MOD_ENGINE, k);
        for (int i = 0; i < 320; i++) tick(core, f, Ctx::kFrame);  // the debounce, then 5.1 s
        timedOut = timedOut && core.failed();
    }
    ctx.check("each times out", timedOut);
    f.delayFrames = 2;
    core.setPick(MOD_ENGINE, 5);
    ctx.check("a new pick clears it", !core.failed());
    for (int i = 0; i < 20 && !core.preview(); i++) tick(core, f, Ctx::kFrame);
    if (!ctx.check("the new pick shows", core.preview() != nullptr)) return;
    for (int i = 0; i < 10; i++) tick(core, f, Ctx::kFrame);
    int neighbours = 0;
    for (const auto& m : f.built) neighbours += m[MOD_ENGINE] == 4 || m[MOD_ENGINE] == 6;
    ctx.check("its neighbours still get built", neighbours == 2, std::to_string(neighbours));
    core.studio().cars[0].nodes[0].p = V3(100, 0, 0);
    tick(core, f, Ctx::kFrame);
    ctx.check("a node 100 m out fails it", core.failed() && !core.preview());
    // The cache finally finishes the stuck builds: closing then frees everything.
    for (auto& [s, n] : f.t) n = 0;
    core.close();
    for (int i = 0; i < 10; i++) tick(core, f, Ctx::kFrame);
    ctx.check("closing frees every spec", core.liveSpecs() == 0 && f.t.empty() && !f.bad, std::to_string(core.liveSpecs()) + " left");
}

SCENARIO(showroomClamp, "showroom/clamp", "open() clamps the fitted mods like applyMods, and builds them") {
    FakeCache f;
    ShowroomCore core(fakeHooks(f));
    ModSet wild;
    const int w[MOD_SLOTS] = {999, -5, 1, 0, 2};
    std::copy(w, w + MOD_SLOTS, wild.pick);
    core.open(3, wild, kPaint);
    const int want[MOD_SLOTS] = {(int)modOptions(3, 0).size() - 1, 0, 1, 0, 2};
    ctx.check("fitted clamped", std::equal(want, want + MOD_SLOTS, core.fitted().pick));
    ctx.check("pick starts at fitted", std::equal(want, want + MOD_SLOTS, core.pick().pick));
    for (int i = 0; i < 20 && !core.preview(); i++) tick(core, f, 0.2);
    ctx.check("builds and shows", showsPick(core) && !f.bad);
}

SCENARIO(showroomPanels, "showroom/layout", "The showroom's panels fit the screen at every size") {
    for (auto [W, H] : {std::pair{1024, 600}, {1280, 720}, {1920, 1080}, {3840, 2160}}) {
        const ShowroomLayout l = showroomLayout(W, H);
        const std::string at = std::to_string(W) + "x" + std::to_string(H);
        auto inside = [&](const Rect& r) {
            return r.x >= 0 && r.y >= 0 && r.w > 0 && r.h > 0 && r.x + r.w <= W + 0.01f && r.y + r.h <= H + 0.01f;
        };
        auto apart = [](const Rect& a, const Rect& b) {
            return a.x + a.w <= b.x + 0.01f || b.x + b.w <= a.x + 0.01f || a.y + a.h <= b.y + 0.01f || b.y + b.h <= a.y + 0.01f;
        };
        ctx.check(at + " on screen", inside(l.left) && inside(l.right) && inside(l.bottom) && inside(l.car));
        ctx.check(at + " no overlap", apart(l.left, l.right) && apart(l.left, l.car) && apart(l.right, l.car));
        ctx.check(at + " car gets a third", l.car.w > 0.3f * W, std::to_string(l.car.w));
        ctx.check(at + " list rows", l.listRows >= 3, std::to_string(l.listRows));
    }
}

SCENARIO(showroomFades, "showroom/fade", "The fade between worlds goes black once, then clears") {
    ShowroomFade fade;
    ctx.check("starts", fade.start() && fade.alpha() == 0);
    ctx.check("one at a time", !fade.start());
    int blacks = 0;
    float most = 0;
    bool blackThen = false;
    for (int i = 0; i < 36; i++) {  // 0.6 s
        if (fade.update(1 / 60.0)) blacks++, blackThen = fade.alpha() == 1;
        most = std::max(most, fade.alpha());
    }
    ctx.check("goes black exactly once", blacks == 1 && blackThen && most == 1);
    ctx.check("clears", fade.alpha() == 0 && !fade.busy());
}
