// Fitment sweep: every engine, transmission, suspension, wheel and tire the garage offers, on
// every vehicle, alone and in the worst pairings (the biggest lift on the biggest tires,
// lowered on the biggest tires). Each build is settled, steered to full lock, driven straight,
// over a bumpy patch, round a turn and dropped onto its bump stops, and must:
//   - keep its tires off its own body (the tire-into-fender contact's push, at rest, at full
//     lock, at full bump and on the move: the stock tire's own clearance is the yardstick);
//   - stay together: no NaNs, no node flung off or buzzing at rest, the body level;
//   - sit at a sane ride height with a sane centre of gravity for its track;
//   - run straight with no pull or weave;
//   - carry its engine and gearbox inside the bay (behind the radiator, ahead of the
//     firewall, under the hood, clear of the front tires) and its prop shafts within their
//     joints' angles.
// CZ_FITMENT_CSV=path appends one line per build (the numbers behind the checks).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/mods.h"
#include "harness.h"

namespace {

// A wide pad of flat pavement on the freeway map, around (0, -400).
const World& padWorld() {
    static const World w = [] {
        World x(MAP_FREEWAY);
        for (double px = -320; px < 320; px += 1)
            for (double pz = -500; pz < -300; pz += 1) {
                const int id = x.cellIndex(px, pz);
                if (id >= 0) x.setCellHeight(id, 0), x.setCellSurface(id, SURF_PAVED, PAINT_PAD);
            }
        x.buildGrid();
        return x;
    }();
    return w;
}

struct Fit {
    double restRub = 0, lockRub = 0, driveRub = 0, bumpRub = 0, dropRub = 0;  // N, summed over the wheels (peak)
    double roll = 0, pitch = 0;      // at rest (deg)
    double ride = 0, cg = 0, ssf = 0;  // lowest body point over the ground, CG height (m), track / 2 CG
    double buzz = 0;                 // fastest body node relative to the body at rest (m/s)
    double pull = 0, weave = 0;      // heading change over the straight (deg), yaw rate swing (deg/s rms)
    double kmh = 0;                  // speed reached on the straight
    bool upright = true, finite = true;
};

// CZ_FITMENT_DEBUG=1: where each wheel was (up from where it sat at rest, m) when it rubbed hardest.
std::vector<double> gRestUp, gPeak, gPeakUp;
std::vector<V3> gPeakAt;  // (the deepest point past its clearance, in the car's rest frame relative to the hub's rest spot)
static const double kSamp[7][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.5, 0.5, 0}, {0, 0.5, 0.5}, {0.5, 0, 0.5}, {1.0 / 3, 1.0 / 3, 1.0 / 3}};
V3 deepestRub(const Car& c, const Wheel& w) {
    const V3 a = c.nodes[w.inner].p, b = c.nodes[w.outer].p, ctr = (a + b) * 0.5, ax = norm(b - a);
    double best = 0;
    V3 at;
    for (size_t ri = 0; ri < w.rubFaces.size(); ri++)
        for (int si = 0; si < 7; si++) {
            V3 p, rest;
            for (int k = 0; k < 3; k++) p += c.nodes[w.rubFaces[ri][k]].p * kSamp[si][k], rest += c.restLocal[w.rubFaces[ri][k]] * kSamp[si][k];
            const V3 d = p - ctr;
            const double da = dot(d, ax), dr = len(d - ax * da);
            if (std::fabs(da) >= w.width * 0.5 || dr >= w.rollR()) continue;
            const double pen = std::min(w.rollR() - dr, w.width * 0.5 - std::fabs(da)) - w.rubAllow[ri * 7 + si];
            if (pen > best) best = pen, at = rest - (c.restLocal[w.inner] + c.restLocal[w.outer]) * 0.5;
        }
    return at;
}
double hubUp(const Car& c, const Wheel& w) {
    const Frame f = c.frame();
    return dot((c.nodes[w.inner].p + c.nodes[w.outer].p) * 0.5 - f.com, f.up);
}
double sumRub(const Car& c) {
    double s = 0;
    for (size_t i = 0; i < c.wheels.size(); i++) {
        const Wheel& w = c.wheels[i];
        s += w.rub;
        if (i < gPeak.size() && w.rub > gPeak[i]) {
            gPeak[i] = w.rub, gPeakUp[i] = hubUp(c, w) - gRestUp[i];
            if (std::getenv("CZ_FITMENT_DEBUG")) gPeakAt[i] = deepestRub(c, w);
        }
    }
    return s;
}

bool finiteCar(const Car& c) {
    const V3 com = c.frame().com;
    for (const Node& n : c.nodes)
        if (!std::isfinite(n.p.x) || !std::isfinite(n.p.y) || !std::isfinite(n.p.z) || len(n.p - com) > 12) return false;
    return true;
}

double yawOf(const Car& c) {
    const Frame f = c.frame();
    return std::atan2(-f.fwd.z, f.fwd.x) * 180 / M_PI;
}

Fit runFit(Ctx& ctx, const VehicleSpec& spec, bool quick) {
    Fit r;
    gRestUp.clear(), gPeak.clear(), gPeakUp.clear();
    ctx.sim = Sim();
    ctx.sim.world = padWorld();
    Car& c = ctx.add(spec, -250, -400, 0);
    c.blowoutsOn = false;
    ctx.sim.player = 0;
    ctx.focus(c);
    // ---- At rest.
    ctx.run(2.5, [&] { c.handbrake = 1; });
    ctx.run(0.5, [&] {
        c.handbrake = 1;
        r.restRub = std::max(r.restRub, sumRub(c));
    });
    gRestUp.clear(), gPeak.assign(c.wheels.size(), 0), gPeakUp.assign(c.wheels.size(), 0), gPeakAt.assign(c.wheels.size(), V3());
    for (const Wheel& w : c.wheels) gRestUp.push_back(hubUp(c, w));
    {
        const Frame f = c.frame();
        r.roll = std::asin(clampd(dot(f.left, V3(0, 1, 0)), -1, 1)) * 180 / M_PI;
        r.pitch = std::asin(clampd(dot(f.fwd, V3(0, 1, 0)), -1, 1)) * 180 / M_PI;
        double low = 1e9, M = 0, cy = 0;
        for (int i = 0; i < c.bodyNodes; i++) low = std::min(low, c.nodes[i].p.y);
        for (const Node& n : c.nodes) M += n.m, cy += n.m * n.p.y;
        const double g = ctx.sim.world.height(f.com.x, f.com.z);
        r.ride = low - g;
        r.cg = cy / M - g;
        double track = 0;
        for (const Wheel& a : c.wheels)
            for (const Wheel& b : c.wheels)
                if (a.front && b.front && a.left && !b.left) track = len((c.nodes[a.inner].p + c.nodes[a.outer].p) - (c.nodes[b.inner].p + c.nodes[b.outer].p)) / 2;
        r.ssf = track / (2 * std::max(0.05, r.cg));
        for (int i = 0; i < c.bodyNodes; i++) r.buzz = std::max(r.buzz, len(c.nodes[i].v - f.vel));
    }
    if (!quick) {
        // ---- Full lock each way, standing (the tire swings round in its well).
        for (double s : {0.6, -0.6})
            ctx.run(1.2, [&] {
                c.handbrake = 1;
                c.steer = s;
                r.lockRub = std::max(r.lockRub, sumRub(c));
            });
        ctx.run(0.6, [&] { c.steer = 0, c.handbrake = 1; });
    }
    // ---- Straight: up to ~60 km/h hands off (gently: a 1,600 hp swap would spin its tires),
    // then cruise 4 s, measured.
    c.handbrake = 0;
    double yaw0 = 0, prevYaw = 0, ss = 0;
    int n = 0;
    bool started = false;
    // (The driver feathers it when the tires spin, as anyone would with a 1,600 hp swap in a
    // 1972 truck with no traction control; a 150 hp one in a 2 t truck needs the foot down.)
    auto spinning = [&] {
        double worst = 0;
        for (const Wheel& w : c.wheels)
            if (w.driven) worst = std::max(worst, std::fabs(w.sx));
        return worst > 0.12;
    };
    const double t0 = ctx.t;
    ctx.runUntil(16, [&] { return speedKmh(c) > 58; }, [&] {
        c.steer = 0, c.brake = 0;
        c.throttle = (ctx.t - t0 < 4 ? 0.45 : 0.9) * (spinning() ? 0.25 : 1.0);
        r.driveRub = std::max(r.driveRub, sumRub(c));
    });
    ctx.run(4.0, [&] {
        const double v = speedKmh(c);
        c.steer = 0;
        c.throttle = clampd((61 - v) * 0.05, 0.0, 0.35) * (spinning() ? 0.25 : 1.0);
        c.brake = 0;
        const double y = yawOf(c);
        if (!started) started = true, yaw0 = prevYaw = y;
        if (started && std::getenv("CZ_FITMENT_DEBUG") && ((int)std::lround(ctx.t * 60)) % 6 == 0)
            std::printf("  t %.2f yaw %+.2f rate %+.2f steer %+.4f roll %+.2f lat %+.3f\n", ctx.t, y - yaw0, (y - prevYaw) / Ctx::kFrame, c.steer,
                        std::asin(clampd(dot(c.frame().left, V3(0, 1, 0)), -1, 1)) * 180 / M_PI, dot(c.frame().vel, c.frame().left));
        if (started) {
            const double rate = (y - prevYaw) / Ctx::kFrame;
            ss += rate * rate, n++;
            prevYaw = y;
        }
        r.driveRub = std::max(r.driveRub, sumRub(c));
    });
    r.kmh = speedKmh(c);
    r.pull = started ? yawOf(c) - yaw0 : 0;
    r.weave = n ? std::sqrt(ss / n) : 0;
    if (!quick) {
        // ---- A bumpy patch: alternating 7 cm ridges under each side.
        {
            const Frame f = c.frame();
            for (int k = 0; k < 8; k++) {
                const double x = f.com.x + 12 + k * 2.2, z = f.com.z + (k % 2 ? 0.75 : -0.75);
                ctx.sim.world.addBox(V3(x, 0.035, z), V3(0.25, 0.035, 0.7), 0, 0, 0, V3(0.4, 0.4, 0.4));
            }
            ctx.sim.world.buildGrid();
        }
        ctx.run(3.0, [&] {
            c.steer = 0;
            c.throttle = speedKmh(c) < 45 ? 0.5 : 0.1;
            r.bumpRub = std::max(r.bumpRub, sumRub(c));
        });
        // ---- A steady turn at ~0.45 g (inside any of them's rollover limit, on any tire),
        // slowed for it first.
        const double vTurn = std::sqrt(0.45 * 9.81 * c.wheelbase / 0.2) * 3.6;
        ctx.runUntil(6, [&] { return speedKmh(c) < vTurn + 2; }, [&] {
            c.steer = 0, c.throttle = 0, c.brake = 0.4;
            r.driveRub = std::max(r.driveRub, sumRub(c));
        });
        ctx.run(5.0, [&] {
            const double v = speedKmh(c);
            c.throttle = v < vTurn ? 0.3 : 0;
            c.brake = v > vTurn + 8 ? 0.4 : 0;
            c.steer = 0.2;
            r.driveRub = std::max(r.driveRub, sumRub(c));
        });
        r.upright = r.upright && c.frame().up.y > 0.7;
        // ---- Stopped, then dropped 0.45 m flat onto its bump stops.
        ctx.run(3.0, [&] {
            c.steer = 0, c.throttle = 0, c.brake = 1;
        });
        for (Node& nd : c.nodes) nd.p.y += 0.45, nd.v = V3();
        ctx.run(2.0, [&] {
            c.brake = 1;
            r.dropRub = std::max(r.dropRub, sumRub(c));
        });
    }
    r.upright = r.upright && c.frame().up.y > 0.7;
    r.finite = finiteCar(c);
    if (std::getenv("CZ_FITMENT_DEBUG"))
        for (size_t i = 0; i < gPeak.size(); i++)
            std::printf("  %s wheel %zu (%s %s): peak rub %.0f N at %+.3f m from rest, on the body at %+.2f %+.2f %+.2f from the hub\n", spec.id, i,
                        c.wheels[i].front ? "front" : "rear", c.wheels[i].left ? "left" : "right", gPeak[i], gPeakUp[i], gPeakAt[i].x, gPeakAt[i].y, gPeakAt[i].z);
    return r;
}

// ---- The engine bay, from the spec (where the block and gearbox are drawn and weigh).

struct Bay {
    double radiatorGap = 1, firewallGap = 1, hoodGap = 1, tireGap = 1;  // m of clearance (< 0: into it)
    bool applies = false;
};

Bay bayOf(const VehicleSpec& vs) {
    Bay b;
    if (vs.midEngine || vs.rearEngine || vs.openFrame || vs.engineMass <= 0) return b;
    b.applies = true;
    const double hl = vs.transverse ? vs.engineWid / 2 : vs.engineLen / 2, hw = vs.transverse ? vs.engineLen / 2 : vs.engineWid / 2;
    const double front = vs.engineX + hl, rear = vs.engineX - hl, top = vs.engineY + vs.engineHgt / 2;
    for (const MassItem& it : vs.items)
        if (std::strstr(it.name, "radiator")) b.radiatorGap = std::min(b.radiatorGap, it.x - 0.05 - front);
    // The firewall: at the cabin's first column (the cowl), the block's back face may tuck under
    // it a little (the bellhousing and the rear of the heads always do).
    int g0 = 99;
    for (const std::string& row : vs.profile)
        for (int i = 0; i < (int)row.size(); i++)
            if (row[i] == 'G') g0 = std::min(g0, i);
    if (g0 < 99) b.firewallGap = rear - (latticeX(vs, g0) - 0.25);
    // The hood over the block: the top line at its front and back (or the layer under the top).
    auto hoodAt = [&](double x) {
        const int n = (int)vs.profile[0].size();
        for (int i = 0; i < n; i++) {
            const double a = latticeX(vs, i), c = latticeX(vs, i + 1);
            if (x <= a && x >= c) {
                auto h = [&](int k) { return k < (int)vs.topLine.size() && vs.topLine[k] > 0 ? vs.topLine[k] : vs.layerY[vs.layerY.size() - 2]; };
                const double t = (a - x) / std::max(1e-6, a - c);
                return h(i) + (h(i + 1) - h(i)) * t;
            }
        }
        return vs.layerY.back();
    };
    b.hoodGap = std::min(hoodAt(front), hoodAt(rear)) - 0.03 - top;
    // The front tires at full lock: a block reaching past the axle's x must fit between them.
    const std::vector<int> ax = axleColumns(vs);
    if (!ax.empty()) {
        const double xa = latticeX(vs, ax[0]);
        const double track = vs.trackFront > 0 ? vs.trackFront : vs.width - 0.16 - vs.wheelWidth;
        const double reach = vs.wheelRadius + 0.05;  // (how far along the car the tire swings at lock)
        if (front > xa - reach && rear < xa + reach && vs.engineY - vs.engineHgt / 2 < 2 * vs.wheelRadius)
            b.tireGap = track / 2 - vs.wheelWidth / 2 - 0.12 - hw;  // (12 cm: the tire's swing inboard at lock)
    }
    return b;
}

// Prop shaft angles (deg) at rest: the gearbox output to each driven solid axle.
double shaftAngle(const Car& c) {
    if (c.gearboxOut < 0) return 0;
    const V3 out = c.restLocal[c.gearboxOut];
    double worst = 0;
    for (const Wheel& w : c.wheels) {
        if (!w.driven) continue;
        const VehicleSpec& vs = *c.spec;
        if ((w.front ? vs.frontSusp : vs.rearSusp) != SOLID_AXLE) continue;
        const V3 hub = (c.restLocal[w.inner] + c.restLocal[w.outer]) * 0.5;
        const double dx = std::fabs(out.x - hub.x), dy = out.y - hub.y;
        if (dx > 0.2) worst = std::max(worst, std::atan2(std::fabs(dy), dx) * 180 / M_PI);
    }
    return worst;
}

struct Base {
    Bay bay;
    double pitch = 0, shaft = 0;
    double rub[5] = {0, 0, 0, 0, 0};
};

std::string lower(std::string s) {
    for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}

FILE* csvOut() {
    static FILE* f = [] {
        const char* p = std::getenv("CZ_FITMENT_CSV");
        return p ? std::fopen(p, "a") : (FILE*)nullptr;
    }();
    return f;
}

// One build: its checks recorded against `what` (named in the check), worst numbers kept.
void judge(Ctx& ctx, int v, const ModSet& ms, const std::string& what, const Base& base, bool quick, int& fails) {
    const VehicleSpec& spec = tunedSpec(v, ms);
    const Fit f = runFit(ctx, spec, quick);
    Sim& sim = ctx.sim;
    const double shaft = sim.cars.empty() ? 0 : shaftAngle(sim.cars[0]);
    const Bay bay = bayOf(spec);
    // (The bay's gaps as the factory build has them, or better: the measure is a box round the
    // block, and some factory engines already tuck a corner under the cowl or the hood's crown.)
    auto bayOk = [&](double got, double stock) { return got > std::min(0.0, stock) - 0.005; };
    char d[512];
    std::snprintf(d, sizeof d,
                  "rub rest %.0f lock %.0f drive %.0f bumps %.0f drop %.0f N; roll %.1f pitch %.1f (stock %.1f) deg; ride %.2f cg %.2f ssf %.2f; "
                  "buzz %.2f m/s; pull %.1f deg weave %.2f deg/s at %.0f km/h; shaft %.0f deg (stock %.0f); bay rad %.2f fw %.2f hood %.2f tire %.2f%s%s",
                  f.restRub, f.lockRub, f.driveRub, f.bumpRub, f.dropRub, f.roll, f.pitch, base.pitch, f.ride, f.cg, f.ssf, f.buzz, f.pull, f.weave,
                  f.kmh, shaft, base.shaft, bay.radiatorGap, bay.firewallGap, bay.hoodGap, bay.tireGap,
                  f.upright ? "" : "; ROLLED OVER", f.finite ? "" : "; BLEW UP");
    if (FILE* o = csvOut()) {
        std::fprintf(o, "%s,%d,%d,%d,%d,%d,\"%s\",%s\n", spec.id, ms.pick[0], ms.pick[1], ms.pick[2], ms.pick[3], ms.pick[4], what.c_str(), d);
        std::fflush(o);
    }
    // The rub allowance over stock: the factory build's own (the coarse lattice brushing a
    // tire at the bump stops) and a little more.
    auto rubOk = [&](double got, double stock, double extra) { return got <= std::max(stock * 1.5, stock + extra); };
    const bool ok = f.finite && f.upright && rubOk(f.restRub, base.rub[0], 150) && rubOk(f.lockRub, base.rub[1], 500) &&
                    rubOk(f.driveRub, base.rub[2], 2500) && rubOk(f.bumpRub, base.rub[3], 2500) && rubOk(f.dropRub, base.rub[4] * 1.4, 8000) &&
                    std::fabs(f.roll) < 1.0 && std::fabs(f.pitch - base.pitch) < 2.0 && f.ride > 0.04 && f.ssf > 0.80 && f.buzz < 0.10 &&
                    std::fabs(f.pull) < 2.5 && f.weave < 1.5 && f.kmh > 15 && shaft < std::max(24.0, std::min(32.0, base.shaft + 12)) && bayOk(bay.radiatorGap, base.bay.radiatorGap) &&
                    bayOk(bay.firewallGap, base.bay.firewallGap) && bayOk(bay.hoodGap, base.bay.hoodGap) && bayOk(bay.tireGap, base.bay.tireGap);
    if (!ok) fails++;
    ctx.check(lower(spec.id) + ": " + what, ok, d);
}

// The suspension options that lift it most and least, and the biggest wheels (by radius).
void extremes(int v, int& liftMost, int& lowMost, int& bigWheels, int& bigSusp) {
    const VehicleSpec& stock = vehicleCatalog()[v];
    double hi = -1, lo = 1, big = 0, bigS = 0;
    liftMost = lowMost = bigWheels = bigSusp = 0;
    for (int o = 1; o < (int)modOptions(v, MOD_SUSPENSION).size(); o++) {
        ModSet m;
        m.pick[MOD_SUSPENSION] = o;
        const VehicleSpec& s = tunedSpec(v, m);
        const double lift = s.layerY[0] - stock.layerY[0] - (s.wheelRadius - stock.wheelRadius);
        if (lift > hi) hi = lift, liftMost = o;
        if (lift < lo) lo = lift, lowMost = o;
        if (s.wheelRadius > bigS) bigS = s.wheelRadius, bigSusp = o;
    }
    for (int o = 1; o < (int)modOptions(v, MOD_WHEELS).size(); o++) {
        ModSet m;
        m.pick[MOD_WHEELS] = o;
        if (tunedSpec(v, m).wheelRadius > big) big = tunedSpec(v, m).wheelRadius, bigWheels = o;
    }
}

void sweep(Ctx& ctx, int v, int slot) {
    Base base;
    base.bay = bayOf(vehicleCatalog()[v]);
    {
        const Fit f = runFit(ctx, vehicleCatalog()[v], false);
        base.pitch = f.pitch;
        base.shaft = shaftAngle(ctx.sim.cars[0]);
        const double r[5] = {f.restRub, f.lockRub, f.driveRub, f.bumpRub, f.dropRub};
        for (int k = 0; k < 5; k++) base.rub[k] = r[k];
        ctx.metric("stock: tire push on the body, landing on the bump stops", f.dropRub / 1000, "kN", 0, 30);
    }
    int fails = 0;
    const bool quick = slot == MOD_ENGINE || slot == MOD_TRANSMISSION;
    if (slot >= 0) {
        const auto& opts = modOptions(v, slot);
        for (int o = 1; o < (int)opts.size(); o++) {
            ModSet m;
            m.pick[slot] = o;
            judge(ctx, v, m, std::string(modSlotName(slot)) + " " + std::to_string(o) + " " + opts[o].name, base, quick, fails);
        }
    } else {
        // The worst pairings.
        int liftMost, lowMost, bigWheels, bigSusp;
        extremes(v, liftMost, lowMost, bigWheels, bigSusp);
        const auto& S = modOptions(v, MOD_SUSPENSION);
        const auto& W = modOptions(v, MOD_WHEELS);
        auto pair = [&](int s, int w, int t, const char* why) {
            ModSet m;
            m.pick[MOD_SUSPENSION] = s, m.pick[MOD_WHEELS] = w, m.pick[MOD_TIRES] = t;
            judge(ctx, v, m, std::string(why) + ": " + S[s].name + " + " + W[w].name, base, false, fails);
        };
        pair(liftMost, bigWheels, 0, "biggest lift, biggest wheels");
        pair(lowMost, bigWheels, 0, "lowest, biggest wheels");
        pair(bigSusp, bigWheels, 0, "biggest-tire kit, biggest wheels");
        // The heaviest engine on the biggest lift.
        int heavy = 0;
        double kg = 0;
        for (int o = 1; o < (int)modOptions(v, MOD_ENGINE).size(); o++) {
            ModSet m;
            m.pick[MOD_ENGINE] = o;
            if (tunedSpec(v, m).engineMass > kg) kg = tunedSpec(v, m).engineMass, heavy = o;
        }
        ModSet m;
        m.pick[MOD_ENGINE] = heavy, m.pick[MOD_SUSPENSION] = liftMost, m.pick[MOD_WHEELS] = bigWheels;
        judge(ctx, v, m, "heaviest engine, biggest lift and wheels: " + modOptions(v, MOD_ENGINE)[heavy].name, base, false, fails);
    }
    ctx.metric("builds that don't fit or drive right", fails, "", 0, 0);
}

// CZ_FITMENT_ONE="id e s t w ti": that one build alone (fitment/one), against its stock car.
void one(Ctx& ctx) {
    const char* e = std::getenv("CZ_FITMENT_ONE");
    char id[32] = "";
    ModSet m;
    if (!e || std::sscanf(e, "%31s %d %d %d %d %d", id, &m.pick[0], &m.pick[1], &m.pick[2], &m.pick[3], &m.pick[4]) < 6) return;
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        if (std::string(vehicleCatalog()[v].id) == id) {
            Base base;
            base.bay = bayOf(vehicleCatalog()[v]);
            const Fit f = runFit(ctx, vehicleCatalog()[v], false);
            base.pitch = f.pitch, base.shaft = shaftAngle(ctx.sim.cars[0]);
            const double r[5] = {f.restRub, f.lockRub, f.driveRub, f.bumpRub, f.dropRub};
            for (int k = 0; k < 5; k++) base.rub[k] = r[k];
            int fails = 0;
            judge(ctx, v, m, "one", base, false, fails);
        }
}

int registerAll() {
    scenarioRegistry().push_back({"fitment-one", "One garage build (CZ_FITMENT_ONE=\"id e s t w ti\")", one});
    const auto& cat = vehicleCatalog();
    static const char* slotTag[MOD_SLOTS] = {"engine", "suspension", "transmission", "wheels", "tires"};
    for (int v : listedVehicles()) {  // (the garage's: the Civic, UTV and buggy aren't offered, only their parts)
        const std::string who = lower(cat[v].id);
        for (int k = -1; k < MOD_SLOTS; k++) {
            const std::string name = "fitment/" + who + "/" + (k < 0 ? std::string("combos") : std::string(slotTag[k]));
            scenarioRegistry().push_back({name, "Every garage part on this vehicle (and the worst pairings) fits and drives: no tire on the body, stable, level, straight, the engine in its bay",
                                          [v, k](Ctx& ctx) { sweep(ctx, v, k); }});
        }
    }
    return 0;
}
const int registered = registerAll();

}  // namespace
