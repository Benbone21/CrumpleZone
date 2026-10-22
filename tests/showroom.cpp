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

#include "../src/builds.h"
#include "../src/career.h"
#include "../src/mods.h"
#include "../src/partpicker.h"
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
        // Level 1's tiles: 2 columns, 2 rows at the smallest size and 3 at 720p, every one inside the left panel.
        ctx.check(at + " tile columns", l.tileCols == 2, std::to_string(l.tileCols));
        if (H == 600) ctx.check(at + " tile rows", l.tileRows >= 2, std::to_string(l.tileRows));
        if (H == 720) ctx.check(at + " tile rows", l.tileRows >= 3, std::to_string(l.tileRows));
        bool tiles = l.tileRows >= 1;
        for (int row = 0; row < l.tileRows; row++)
            for (int col = 0; col < l.tileCols; col++) {
                const Rect t = l.tileRect(col, row);
                tiles = tiles && t.w > 0 && t.h > 0 && t.x >= l.left.x - 0.01f && t.y >= l.left.y - 0.01f && t.x + t.w <= l.left.x + l.left.w + 0.01f &&
                        t.y + t.h <= l.left.y + l.left.h + 0.01f;
            }
        ctx.check(at + " tiles inside the left panel", tiles);
        // Level 2: every row (name and detail line) inside the panel, under the breadcrumb.
        bool rows = l.listRows >= 3 && l.rowH > 0;
        for (int i = 0; i < l.listRows; i++) {
            const Rect t = l.rowRect(i);
            rows = rows && t.x >= l.left.x - 0.01f && t.y >= l.tileTop + 18 * l.u - 0.01f && t.x + t.w <= l.left.x + l.left.w + 0.01f && t.y + t.h <= l.left.y + l.left.h + 0.01f;
        }
        ctx.check(at + " rows inside the left panel", rows);
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
    // Held at black (the preview still building, the fitted car not yet swapped in), then let go.
    fade.start();
    blacks = 0;
    for (int i = 0; i < 120; i++) blacks += fade.update(1 / 60.0, true);  // 2 s
    ctx.check("holds black", blacks == 1 && fade.alpha() == 1 && fade.busy());
    for (int i = 0; i < 18; i++) fade.update(1 / 60.0);  // 0.3 s
    ctx.check("then clears", fade.alpha() == 0 && !fade.busy());
}

SCENARIO(showroomBuyPart, "showroom/buy", "B buys the part shown in a career, at a repair station only; a sandbox has nothing to buy") {
    Career c;
    c.on = true;
    c.startFresh();
    const int v = 0;
    int o = -1;  // something on sale, not owned, and affordable
    for (int k = 1; k < (int)modOptions(v, MOD_TIRES).size() && o < 0; k++)
        if (!c.owns(v, MOD_TIRES, k) && Career::price(v, MOD_TIRES, k) > 0 && Career::price(v, MOD_TIRES, k) <= c.cash) o = k;
    if (!ctx.check("a part on sale", o > 0)) return;
    const int cash0 = c.cash, p = Career::price(v, MOD_TIRES, o);
    std::string toast;
    ctx.check("away from a station: spends nothing", !showroomBuy(c, v, MOD_TIRES, o, false, toast) && c.cash == cash0 && !c.owns(v, MOD_TIRES, o) &&
                                                         toast == "PARTS ARE SOLD AT THE REPAIR STATIONS", toast);
    ctx.check("at a station: buys it", showroomBuy(c, v, MOD_TIRES, o, true, toast) && c.cash == cash0 - p && c.owns(v, MOD_TIRES, o), toast);
    ctx.check("...and says so", toast == "BOUGHT " + modOptions(v, MOD_TIRES)[o].name + ": $" + std::to_string(cash0 - p) + " LEFT", toast);
    ctx.check("not twice", !showroomBuy(c, v, MOD_TIRES, o, true, toast) && c.cash == cash0 - p && toast == "YOU HAVE IT ALREADY", toast);
    Career sandbox;
    sandbox.cash = 1000000;
    ctx.check("a sandbox: nothing", !showroomBuy(sandbox, v, MOD_TIRES, o, true, toast) && sandbox.cash == 1000000 && toast.empty(), toast);
}

SCENARIO(showroomPickerTree, "showroom/picker-tree", "Every option of every slot is in exactly one group, grouped by donor in catalog order") {
    const int n = (int)vehicleCatalog().size();
    int bad = 0;
    std::string first;
    auto fail = [&](const std::string& why) {
        if (!bad++) first = why;
    };
    for (int v = 0; v < n; v++) {
        PartTree t;
        t.build(v);
        for (int s = 0; s < MOD_SLOTS; s++) {
            const std::string at = "vehicle " + std::to_string(v) + " slot " + std::to_string(s) + ": ";
            const int total = (int)modOptions(v, s).size();
            const auto& g = t.groups[s];
            std::vector<int> seen(total, 0);
            for (const PartGroup& p : g)
                for (int o = p.first; o < p.first + p.count; o++)
                    if (o >= 0 && o < total) seen[o]++;
                    else fail(at + "option " + std::to_string(o) + " out of range");
            for (int o = 0; o < total; o++) {
                if (seen[o] != 1) fail(at + "option " + std::to_string(o) + " in " + std::to_string(seen[o]) + " groups");
                const int gi = t.groupOf(s, o);
                if (gi < 0 || gi >= (int)g.size() || o < g[gi].first || o >= g[gi].first + g[gi].count) fail(at + "groupOf(" + std::to_string(o) + ")");
            }
            if (t.groupOf(s, -1) != -1 || t.groupOf(s, total) != -1) fail(at + "groupOf out of range");
            if (g.empty() || g[0].donor != v || g[0].first != 0 || g[0].count != modOwnCount(v, s)) fail(at + "group 0 isn't the vehicle's own");
            if (s == MOD_TIRES) {
                for (const PartGroup& p : g)
                    if (p.count != 1) fail(at + "a tire group of " + std::to_string(p.count));
                continue;
            }
            int prev = -1;
            for (size_t i = 1; i < g.size(); i++) {
                if (g[i].donor == v || g[i].donor <= prev) fail(at + "donor " + std::to_string(g[i].donor) + " after " + std::to_string(prev));
                prev = g[i].donor;
                for (int o = g[i].first; o < g[i].first + g[i].count; o++)
                    if (modOptions(v, s)[o].fromVehicle != g[i].donor) fail(at + "option " + std::to_string(o) + " not from the group's donor");
            }
        }
    }
    ctx.check("grouped by donor, nothing lost or doubled", bad == 0, first);
}

SCENARIO(showroomPickerNav, "showroom/picker-nav", "Tiles and rows pick the highlighted part, a group remembers its variant, tabs keep the pick") {
    const int v = 3;
    const auto& cat = vehicleCatalog();
    int jesko = -1;
    for (int i = 0; i < (int)cat.size(); i++)
        if (std::string(cat[i].id) == "jesko") jesko = i;
    if (!ctx.check("the Jesko is a donor to catalog 3", jesko >= 0 && jesko != v)) return;
    PickerNav nav;
    nav.open(v, ModSet(), 2);
    ctx.check("opens on ENGINE, level 1, group 0", nav.tabIndex() == TAB_ENGINE && nav.level() == 1 && nav.cursor() == 0 && nav.pick().stock());
    const PartTree& tr = nav.tree();
    int jg = -1;
    for (int g = 0; g < (int)tr.groups[MOD_ENGINE].size(); g++)
        if (tr.groups[MOD_ENGINE][g].donor == jesko) jg = g;
    if (!ctx.check("the Jesko has a multi-option engine group", jg > 0 && tr.groups[MOD_ENGINE][jg].count >= 2)) return;
    const PartGroup jp = tr.groups[MOD_ENGINE][jg];

    // Onto the Jesko's tile, one step at a time (the grid's two columns, then down a row).
    bool changed = false;
    for (int guard = 0; nav.cursor() < jg && guard < 100; guard++) changed = nav.move(1, 0) || changed;
    ctx.check("moving onto its tile picks its first engine", nav.cursor() == jg && nav.pick().pick[MOD_ENGINE] == jp.first && changed);
    ctx.check("opening it keeps the pick", !nav.enter() && nav.level() == 2 && nav.pick().pick[MOD_ENGINE] == jp.first && nav.cursor() == 0);
    ctx.check("one row down picks its second", nav.move(0, 1) && nav.pick().pick[MOD_ENGINE] == jp.first + 1 && nav.cursor() == 1);
    bool close = true;
    ctx.check("back keeps it", !nav.back(close) && !close && nav.level() == 1 && nav.cursor() == jg && nav.pick().pick[MOD_ENGINE] == jp.first + 1);
    ctx.check("away picks the other tile's part", nav.move(-1, 0) && nav.pick().pick[MOD_ENGINE] != jp.first + 1 && nav.cursor() == jg - 1);
    ctx.check("and back remembers the second", nav.move(1, 0) && nav.pick().pick[MOD_ENGINE] == jp.first + 1);
    ctx.check("a move up to the top picks group 0's part", nav.move(0, -100) && nav.cursor() == 0 && nav.scroll() == 0 && nav.pick().pick[MOD_ENGINE] == nav.remembered(MOD_ENGINE, 0));
    ctx.check("a clamped move picks nothing", !nav.move(0, -100) && !nav.move(-1, 0) && nav.cursor() == 0 && nav.scroll() == 0 && nav.pick().pick[MOD_ENGINE] == nav.remembered(MOD_ENGINE, 0));
    nav.clickTile(jg);

    // Tabs: the new tab's cursor lands on the group holding the pick; the pick never changes.
    const ModSet before = nav.pick();
    ctx.check("tab changes no pick", !nav.tab(1) && nav.tabIndex() == TAB_SUSP && nav.level() == 1 && nav.pick().pick[MOD_ENGINE] == before.pick[MOD_ENGINE]);
    ctx.check("...and lands on the SUSP group holding the pick", nav.cursor() == tr.groupOf(MOD_SUSPENSION, nav.pick().pick[MOD_SUSPENSION]));
    ctx.check("tab(-1) wraps back", !nav.tab(-1) && nav.tabIndex() == TAB_ENGINE && nav.cursor() == jg);
    ctx.check("tab(-1) wraps to BUILDS", !nav.tab(-1) && nav.tabIndex() == TAB_BUILDS && nav.cursor() == 0);
    ctx.check("...and forward again", !nav.tab(1) && nav.tabIndex() == TAB_ENGINE);

    // A one-option tile doesn't open.
    nav.open(v, ModSet(), 0);
    nav.tab(TAB_TIRES);
    ctx.check("at TIRES", nav.tabIndex() == TAB_TIRES && nav.level() == 1);
    ctx.check("enter on a one-option tile does nothing", !nav.enter() && nav.level() == 1);
    close = false;
    ctx.check("back at level 1 closes, and changes nothing", !nav.back(close) && close && nav.level() == 1 && nav.pick().stock());

    // A fitted non-own part: the cursor opens on its group.
    ModSet fit;
    fit.pick[MOD_ENGINE] = 5;
    if (!ctx.check("option 5 is a donor's, not the vehicle's own", 5 >= modOwnCount(v, MOD_ENGINE) && 5 < (int)modOptions(v, MOD_ENGINE).size())) return;
    nav.open(v, fit, 0);
    const int want = nav.tree().groupOf(MOD_ENGINE, 5);
    ctx.check("opens on the fitted part's group", want > 0 && nav.cursor() == want && nav.pick().pick[MOD_ENGINE] == 5);
    nav.enter();
    const PartGroup fg = nav.tree().groups[MOD_ENGINE][want];
    ctx.check("its row is under the cursor", fg.count < 2 || nav.cursor() == 5 - fg.first);

    // A fitted pick out of range is clamped like applyMods, and everything built on it agrees.
    ModSet wild;
    const int w[MOD_SLOTS] = {99, -3, 0, 0, 0};
    std::copy(w, w + MOD_SLOTS, wild.pick);
    nav.open(v, wild, 0);
    const int top = (int)modOptions(v, MOD_ENGINE).size() - 1;
    ctx.check("an out-of-range fitted pick is clamped", nav.pick().pick[MOD_ENGINE] == top && nav.pick().pick[MOD_SUSPENSION] == 0 && nav.yourPick().pick[MOD_ENGINE] == top);
    ctx.check("...the cursor is on the clamped part's group", nav.cursor() == nav.tree().groupOf(MOD_ENGINE, top) && nav.cursor() >= 0);
    ctx.check("...and its group remembers it", nav.remembered(MOD_ENGINE, nav.cursor()) == top);
}

SCENARIO(showroomPickerView, "showroom/picker-view", "The grid clamps, scroll keeps the cursor in view, clicks are absolute, and BUILDS only moves the cursor") {
    const int v = 3;
    PickerNav nav;
    nav.open(v, ModSet(), 3);
    nav.setView(2, 2, 3);
    const int n = (int)nav.tree().groups[MOD_ENGINE].size();
    if (!ctx.check("enough tiles to scroll", n >= 6)) return;
    ctx.check("up from the top clamps", !nav.move(0, -1) && !nav.move(-1, 0) && nav.cursor() == 0 && nav.scroll() == 0);
    ctx.check("down is a whole row", nav.move(0, 1) && nav.cursor() == 2 && nav.scroll() == 0);
    nav.move(0, 1);
    ctx.check("scrolls once the cursor leaves the view", nav.cursor() == 4 && nav.scroll() == 1);
    for (int i = 0; i < 40; i++) nav.move(0, 1);
    ctx.check("down clamps to the last tile", nav.cursor() == n - 1 && nav.scroll() == (n - 1) / 2 - 1);
    // (Raising the view's rows never leaves blank rows: scroll stops at the last full view.)
    const int rows = (n + 1) / 2;
    nav.setView(2, 4, 3);
    ctx.check("a taller view clamps the scroll", nav.scroll() == std::max(0, rows - 4) && nav.cursor() == n - 1);
    nav.setView(2, 2, 3);
    ctx.check("...and the cursor stays in view", nav.scroll() >= (n - 1) / 2 - 1 && nav.scroll() <= (n - 1) / 2);
    ctx.check("up to the top scrolls back", nav.move(0, -100) && nav.cursor() == 0 && nav.scroll() == 0);
    nav.clickTile(0);
    ctx.check("clicking a tile picks its part", nav.clickTile(n - 1) && nav.cursor() == n - 1 && nav.pick().pick[MOD_ENGINE] == nav.tree().groups[MOD_ENGINE][n - 1].first);
    ctx.check("out of range does nothing", !nav.clickTile(n) && !nav.clickTile(-1) && nav.cursor() == n - 1);

    // Rows are absolute option-within-group indices, scrolled to stay visible.
    int big = -1;
    for (int g = 0; g < n && big < 0; g++)
        if (nav.tree().groups[MOD_ENGINE][g].count >= 3) big = g;
    if (!ctx.check("a group of three or more", big >= 0)) return;
    const PartGroup bg = nav.tree().groups[MOD_ENGINE][big];
    nav.setView(2, 2, 2);
    nav.clickTile(big);
    nav.enter();
    ctx.check("clicking a row picks and remembers it", nav.clickRow(2) && nav.cursor() == 2 && nav.scroll() == 1 && nav.pick().pick[MOD_ENGINE] == bg.first + 2);
    ctx.check("a tile click at level 2 does nothing", !nav.clickTile(0) && nav.level() == 2);
    bool close = true;
    nav.back(close);
    nav.clickTile(0);
    nav.clickTile(big);
    ctx.check("the tile remembers it", nav.pick().pick[MOD_ENGINE] == bg.first + 2);

    // BUILDS: YOUR PICK, then the builds. With nothing to resolve them the cursor moves; the pick doesn't.
    nav.tab(-1);
    const ModSet yours = nav.pick();
    ctx.check("BUILDS opens on YOUR PICK", nav.tabIndex() == TAB_BUILDS && nav.cursor() == 0 && nav.tileCount() == 4);
    ctx.check("with no resolve, moving over builds changes no pick", !nav.move(0, 1) && !nav.move(1, 0) && nav.cursor() == 3 && !nav.move(0, 5) && nav.cursor() == 3 && nav.pick().pick[MOD_ENGINE] == yours.pick[MOD_ENGINE]);
    ctx.check("...nor does enter or a click", !nav.enter() && nav.level() == 1 && !nav.clickTile(1) && nav.cursor() == 1 && nav.pick().pick[MOD_ENGINE] == yours.pick[MOD_ENGINE]);
    ctx.check("it kept YOUR PICK", nav.yourPick().pick[MOD_ENGINE] == yours.pick[MOD_ENGINE]);
}

// ---- Builds

namespace {
ModSet onlySlot(int slot, int option) {
    ModSet m;
    m.pick[slot] = option;
    return m;
}
bool samePicks(const ModSet& a, const ModSet& b) { return std::equal(a.pick, a.pick + MOD_SLOTS, b.pick); }
std::string picksText(const ModSet& m) {
    std::string t;
    for (int p : m.pick) t += (t.empty() ? "" : " ") + std::to_string(p);
    return t;
}
}  // namespace

SCENARIO(showroomBuilds, "showroom/builds", "Every build resolves on every vehicle, names its engine, gearbox and tread, and sits on top of a pick; setPicks clamps and restarts the debounce once") {
    const auto& cat = vehicleCatalog();
    const auto& bl = builds();
    if (!ctx.check("there are builds", !bl.empty())) return;
    bool named = true;
    for (const Build& b : bl) named = named && b.name && *b.name && b.detail && *b.detail;
    ctx.check("each has a name and a detail", named);

    // The base: parts in every slot, so a build's unnamed slots show where they came from.
    const int baseWant[MOD_SLOTS] = {2, 1, 3, 1, 3};
    int bad = 0, badNames = 0, badBase = 0, badOwn = 0;
    std::string first, firstName, firstBase, firstOwn;
    for (int v = 0; v < (int)cat.size(); v++) {
        const std::string at = std::string(cat[v].id) + " ";
        for (const Build& b : bl) {
            ModSet io;
            if (!resolveBuild(v, b, io)) {
                if (!bad++) first = at + b.name;
                continue;
            }
            VehicleSpec s;
            applyMods(v, io, s);
            for (int k = 0; k < MOD_SLOTS; k++) {
                if (!b.parts[k]) {
                    if (io.pick[k] != 0 && !badNames++) firstName = at + b.name + ": unnamed slot " + std::to_string(k) + " moved";
                    continue;
                }
                const std::string key = b.parts[k];
                if (key == std::string(modSlotName(k)) + ":own") {  // (the vehicle's own part: option 0)
                    if (io.pick[k] != 0 && !badNames++) firstName = at + b.name + ": " + key + " took option " + std::to_string(io.pick[k]);
                    continue;
                }
                if (k == MOD_TIRES && io.pick[k] == 0 && key != modPartKey(v, k, 0)) {
                    // (the vehicle's own tread: option 0 stands for it)
                    if (key != std::string("TIRES:") + tireTypeName(cat[v].tireType) && !badNames++) firstName = at + b.name + ": " + key + " took option 0";
                    continue;
                }
                if (modPartKey(v, k, io.pick[k]) != key) {
                    if (!badNames++) firstName = at + b.name + ": " + key + " resolved to " + modPartKey(v, k, io.pick[k]);
                    continue;
                }
                const ModOption& o = modOptions(v, k)[io.pick[k]];
                if (k == MOD_ENGINE || k == MOD_TRANSMISSION) {
                    // The part on its own vehicle names what it fits: the same engine or gearbox name here.
                    VehicleSpec donor;
                    applyMods(o.fromVehicle, onlySlot(k, o.fromOption), donor);
                    const std::string& want = k == MOD_ENGINE ? donor.engineName : donor.gearboxName;
                    const std::string& got = k == MOD_ENGINE ? s.engineName : s.gearboxName;
                    if (want != got && !badNames++) firstName = at + b.name + ": " + key + " is " + got + ", wanted " + want;
                }
            }
            if (b.parts[MOD_TIRES] && std::string("TIRES:") + tireTypeName(s.tireType) != b.parts[MOD_TIRES] && !badNames++)
                firstName = at + b.name + ": tires are " + tireTypeName(s.tireType) + ", wanted " + b.parts[MOD_TIRES];

            // Over a base, the unnamed slots stay the base's.
            ModSet base;
            std::copy(baseWant, baseWant + MOD_SLOTS, base.pick);
            ModSet over = base;
            if (!resolveBuild(v, b, over)) {
                if (!badBase++) firstBase = at + b.name + " over a base";
                continue;
            }
            for (int k = 0; k < MOD_SLOTS; k++)
                if (b.parts[k] ? over.pick[k] != io.pick[k] : over.pick[k] != base.pick[k])
                    if (!badBase++) firstBase = at + b.name + " slot " + std::to_string(k) + ": " + picksText(over);
        }
        // The vehicle's own tread is option 0, however it is named.
        Build own{"OWN", "", {nullptr, nullptr, nullptr, nullptr, nullptr}};
        const std::string ownKey = std::string("TIRES:") + tireTypeName(cat[v].tireType);
        own.parts[MOD_TIRES] = ownKey.c_str();
        ModSet io = onlySlot(MOD_TIRES, 3);
        if (!resolveBuild(v, own, io) || io.pick[MOD_TIRES] != 0)
            if (!badOwn++) firstOwn = at + ownKey + " -> " + std::to_string(io.pick[MOD_TIRES]);
    }
    ctx.check("every build resolves on every vehicle", bad == 0, first);
    ctx.check("each names its engine, gearbox and tread, and leaves the rest", badNames == 0, firstName);
    ctx.check("over a base, unnamed slots stay the base's", badBase == 0, firstBase);
    ctx.check("the vehicle's own tread resolves to option 0", badOwn == 0, firstOwn);

    // A build that names a tread delivers it over any base: the base's suspension (which can bring its own tread) and wheels
    // can't change it. Own treads resolve to option 0, which applies no tread, so this is the check that the parts hold it.
    int badTread = 0, treadCases = 0;
    std::string firstTread;
    for (int v = 0; v < (int)cat.size(); v++) {
        std::vector<ModSet> bases(1);  // (all default)
        for (int o = 0; o < (int)modOptions(v, MOD_SUSPENSION).size(); o++) bases.push_back(onlySlot(MOD_SUSPENSION, o));
        for (int o = 0; o < (int)modOptions(v, MOD_WHEELS).size(); o++) bases.push_back(onlySlot(MOD_WHEELS, o));
        for (const Build& b : bl) {
            if (!b.parts[MOD_TIRES]) continue;
            for (const ModSet& base : bases) {
                ModSet m = base;
                resolveBuild(v, b, m);
                VehicleSpec s;
                applyMods(v, m, s);
                treadCases++;
                if (std::string("TIRES:") + tireTypeName(s.tireType) != b.parts[MOD_TIRES] && !badTread++)
                    firstTread = std::string(cat[v].id) + " " + b.name + " over " + picksText(base) + ": " + tireTypeName(s.tireType);
            }
        }
    }
    ctx.check("a named tread holds over any suspension or wheels base", badTread == 0 && treadCases > 0, firstTread);

    // SLEEPER: the most powerful engine in the catalog, and the vehicle's own suspension and wheels over any base.
    const Build* sleeper = nullptr;
    for (const Build& b : bl)
        if (std::string(b.name) == "SLEEPER") sleeper = &b;
    if (ctx.check("there is a SLEEPER", sleeper != nullptr)) {
        int badSleeper = 0;
        std::string firstSleeper;
        for (int v = 0; v < (int)cat.size(); v++) {
            ModSet over;
            std::copy(baseWant, baseWant + MOD_SLOTS, over.pick);  // (a lifted suspension and donor wheels)
            resolveBuild(v, *sleeper, over);
            if ((over.pick[MOD_SUSPENSION] != 0 || over.pick[MOD_WHEELS] != 0) && !badSleeper++) firstSleeper = std::string(cat[v].id) + ": " + picksText(over);
            double best = 0;
            for (int o = 0; o < (int)modOptions(v, MOD_ENGINE).size(); o++) {
                VehicleSpec s;
                applyMods(v, onlySlot(MOD_ENGINE, o), s);
                best = std::max(best, peakPower(s));
            }
            VehicleSpec got;
            applyMods(v, over, got);
            if (peakPower(got) < best && !badSleeper++) firstSleeper = std::string(cat[v].id) + ": engine " + std::to_string(peakPower(got)) + " W of " + std::to_string(best);
        }
        ctx.check("SLEEPER: the strongest engine, and its own suspension and wheels over a lifted, donor-wheeled base", badSleeper == 0, firstSleeper);
    }

    // A key that isn't a part: false, and that slot stays as it was.
    Build bogus{"BOGUS", "", {"ENGINE:nowhere:9", nullptr, nullptr, nullptr, "TIRES:SNOW"}};
    ModSet io = onlySlot(MOD_ENGINE, 2);
    ctx.check("an unknown key fails and leaves its slot", !resolveBuild(3, bogus, io) && io.pick[MOD_ENGINE] == 2 && modPartKey(3, MOD_TIRES, io.pick[MOD_TIRES]) == "TIRES:SNOW");

    // Through the picker.
    const int v = 3;
    ModSet yours;
    std::copy(baseWant, baseWant + MOD_SLOTS, yours.pick);
    PickerNav nav;
    nav.open(v, yours, (int)bl.size());
    nav.resolve = [&](int k, const ModSet& base) {
        ModSet m = base;
        resolveBuild(v, bl[k], m);
        return m;
    };
    const int second = std::min(1, (int)bl.size() - 1);  // (two different builds, if there are two)
    ModSet b0 = yours, b1 = yours;
    resolveBuild(v, bl[0], b0);
    resolveBuild(v, bl[second], b1);
    ctx.check("builds differ from the base", !samePicks(b0, yours) && !samePicks(b1, yours));
    nav.tab(-1);
    ctx.check("entering BUILDS changes nothing", nav.tabIndex() == TAB_BUILDS && nav.cursor() == 0 && samePicks(nav.pick(), yours) && samePicks(nav.yourPick(), yours));
    ctx.check("moving onto a build picks it", nav.move(1, 0) && nav.cursor() == 1 && samePicks(nav.pick(), b0), picksText(nav.pick()));
    ctx.check("another build is over YOUR PICK, not the last build", nav.clickTile(second + 1) && samePicks(nav.pick(), b1), picksText(nav.pick()));
    ctx.check("a build asked for again changes nothing", !nav.clickTile(second + 1) && samePicks(nav.pick(), b1));
    ctx.check("back on YOUR PICK restores it exactly", nav.clickTile(0) && samePicks(nav.pick(), yours) && samePicks(nav.yourPick(), yours));
    ctx.check("...also by moving", nav.move(1, 0) && samePicks(nav.pick(), b0) && nav.move(-1, 0) && samePicks(nav.pick(), yours) && nav.cursor() == 0);
    ctx.check("tabbing through BUILDS changes nothing", !nav.tab(1) && !nav.tab(-1) && nav.tabIndex() == TAB_BUILDS && samePicks(nav.pick(), yours) && nav.cursor() == 0);

    // Leaving keeps the build, and the other tabs' groups remember its parts.
    nav.clickTile(second + 1);
    ctx.check("leaving keeps the build", !nav.tab(1) && nav.tabIndex() == TAB_ENGINE && samePicks(nav.pick(), b1));
    bool follows = true;
    std::string why;
    for (int k = 0; k < MOD_SLOTS; k++) {
        const int g = nav.tree().groupOf(k, b1.pick[k]);
        if (g < 0 || nav.remembered(k, g) != b1.pick[k]) follows = false, why += " slot " + std::to_string(k);
    }
    ctx.check("each slot's group remembers the build's option", follows, why);
    const int eg = nav.tree().groupOf(MOD_ENGINE, b1.pick[MOD_ENGINE]);
    ctx.check("...and the cursor is on the group holding it", eg >= 0 && nav.cursor() == eg);
    // Highlighting that group's tile again picks the same part, not the group's first.
    nav.clickTile(eg == 0 ? 1 : 0);
    ctx.check("away and back onto the group re-picks the build's part", nav.clickTile(eg) && nav.pick().pick[MOD_ENGINE] == b1.pick[MOD_ENGINE]);
    nav.tab(-1);
    ctx.check("entering again takes a fresh YOUR PICK", samePicks(nav.yourPick(), nav.pick()) && nav.cursor() == 0);

    // Browsing builds doesn't disturb what the groups remember: after YOUR PICK is restored, the Jesko's engine
    // tile picks the option it would have picked before BUILDS.
    {
        int hyper = -1;
        for (int i = 0; i < (int)bl.size(); i++)
            if (bl[i].parts[MOD_ENGINE] && std::string(bl[i].parts[MOD_ENGINE]) == "ENGINE:jesko:2") hyper = i;
        PickerNav n2;
        n2.open(v, ModSet(), (int)bl.size());
        n2.resolve = nav.resolve;
        int jg = -1;
        for (int g = 0; g < (int)n2.tree().groups[MOD_ENGINE].size(); g++)
            if (std::string(cat[n2.tree().groups[MOD_ENGINE][g].donor].id) == "jesko") jg = g;
        if (ctx.check("a build with the Jesko's race engine, and the Jesko's engine group", hyper >= 0 && jg > 0)) {
            const int before = n2.remembered(MOD_ENGINE, jg), raced = n2.tree().groups[MOD_ENGINE][jg].first + 2;
            n2.tab(-1);
            ctx.check("the build picks the race engine", n2.clickTile(hyper + 1) && n2.pick().pick[MOD_ENGINE] == raced);
            ctx.check("...and is remembered while it is picked", n2.remembered(MOD_ENGINE, jg) == raced);
            ctx.check("YOUR PICK restores", n2.clickTile(0) && n2.pick().stock());
            ctx.check("...and gives the group back what it remembered", n2.remembered(MOD_ENGINE, jg) == before);
            n2.tab(1);
            ctx.check("on the ENGINE tab the Jesko tile picks the option remembered before BUILDS", n2.clickTile(jg) && n2.pick().pick[MOD_ENGINE] == before, picksText(n2.pick()));
        }
    }

    // setPicks: clamped like setPick, one debounce restart, neighbours only for a single slot.
    {
        FakeCache f;
        ShowroomCore core(fakeHooks(f)), ref(fakeHooks(f));
        core.open(v, ModSet(), kPaint);
        ref.open(v, ModSet(), kPaint);
        ModSet wild;
        const int w[MOD_SLOTS] = {999, -5, 1, 0, 40};
        std::copy(w, w + MOD_SLOTS, wild.pick);
        core.setPicks(wild);
        for (int k = 0; k < MOD_SLOTS; k++) ref.setPick(k, wild.pick[k]);
        ctx.check("setPicks clamps as setPick does", samePicks(core.pick(), ref.pick()) && core.pick().pick[MOD_ENGINE] == (int)modOptions(v, MOD_ENGINE).size() - 1 && core.pick().pick[MOD_SUSPENSION] == 0, picksText(core.pick()));
    }
    FakeCache f;
    ShowroomCore core(fakeHooks(f));
    core.open(v, ModSet(), kPaint);
    for (int i = 0; i < 20; i++) tick(core, f, 0.2);
    f.built.clear();
    ModSet two = core.pick();
    two.pick[MOD_ENGINE] = 5, two.pick[MOD_SUSPENSION] = 2;
    core.setPicks(two);
    tick(core, f, 0.05);
    ctx.check("nothing builds inside the debounce", f.built.empty() && !showsPick(core));
    core.setPicks(two);
    for (int i = 0; i < 2; i++) tick(core, f, 0.05);
    ctx.check("...and a restart pushes the build back", f.built.empty());
    for (int i = 0; i < 20; i++) tick(core, f, 0.2);
    ctx.check("several slots changed: just the pick is built, no neighbours", f.built.size() == 1 && showsPick(core), std::to_string(f.built.size()) + " builds");
    f.built.clear();
    ModSet one = core.pick();
    one.pick[MOD_ENGINE] = 9;
    core.setPicks(one);
    for (int i = 0; i < 20; i++) tick(core, f, 0.2);
    ctx.check("exactly one slot changed: the pick and the options either side are built", f.built.size() == 3 && showsPick(core) && !f.bad, std::to_string(f.built.size()) + " builds");
}
