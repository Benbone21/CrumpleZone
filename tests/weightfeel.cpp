// How heavy the vehicles feel: mass and how it's spread (inertia, CG), the unsprung mass on
// the tires, and how a hard landing settles, against real trucks.
//
// survey/weight-feel prints the measurements for every vehicle (no limits; CZ_WF_ONLY=BRONCO
// for one). dynamics/landing-settle holds the trucks to a landing that thuds and settles.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "harness.h"

namespace {

struct MassSpread {
    double M = 0, cgH = 0;                   // kg; CG height over the ground (m)
    double kRoll = 0, kPitch = 0, kYaw = 0;  // radii of gyration about the CG (m)
    double unsprung = 0;                     // kg in the wheel assemblies (all four)
    double track = 0;                        // mean track, tire centre to tire centre (m)
};

MassSpread massSpread(const Ctx& ctx, const Car& c) {
    MassSpread s;
    const Frame f = c.frame();
    V3 cg;
    for (const Node& n : c.nodes) s.M += n.m, cg += n.p * n.m;
    cg = cg / s.M;
    double Ir = 0, Ip = 0, Iy = 0;
    for (const Node& n : c.nodes) {
        const V3 d = n.p - cg;
        const double x = dot(d, f.fwd), y = dot(d, f.up), z = dot(d, f.left);
        Ir += n.m * (y * y + z * z), Ip += n.m * (x * x + y * y), Iy += n.m * (x * x + z * z);
    }
    s.kRoll = std::sqrt(Ir / s.M), s.kPitch = std::sqrt(Ip / s.M), s.kYaw = std::sqrt(Iy / s.M);
    s.cgH = cg.y - ctx.sim.world.height(cg.x, cg.z);
    std::vector<char> us(c.nodes.size(), 0);
    for (const Wheel& w : c.wheels)
        for (int n : w.assembly) us[n] = 1;
    for (size_t i = 0; i < c.nodes.size(); i++)
        if (us[i]) s.unsprung += c.nodes[i].m;
    double tr = 0;
    int n = 0;
    for (const Wheel& a : c.wheels)
        for (const Wheel& b : c.wheels)
            if (a.front == b.front && a.left && !b.left) {
                const V3 pa = (c.nodes[a.inner].p + c.nodes[a.outer].p) * 0.5, pb = (c.nodes[b.inner].p + c.nodes[b.outer].p) * 0.5;
                tr += std::fabs(dot(pa - pb, f.left)), n++;
            }
    s.track = n ? tr / n : 0;
    return s;
}

// Dropped `h` flat (wheels level, at rest) onto the apron, handbrake on. Everything is read
// from the deepest squat on: the rebound that follows.
struct Landing {
    double squat = 0;    // body CG's deepest point below where it ends up (m)
    double rebound = 0;  // its highest point after that, over where it ends up (m)
    double air = 0;      // s with all four tires off the ground after the squat (it hops)
    double lift = 0;     // highest any tire gets off the ground after the squat (m)
    int bounces = 0;     // separate times the CG swings more than 2 cm past where it ends up
    double settle = 0;   // s from the squat until it's within 1 cm for good
};
Landing landing(Ctx& ctx, const VehicleSpec& spec, double z, double h) {
    ctx.sim.cars.clear();
    Car& c = ctx.add(spec, -100, z, 0);
    ctx.focus(c);
    ctx.run(2.0, [&] { c.handbrake = 1; });
    for (Node& n : c.nodes) n.p.y += h, n.v = V3();
    c.wake();
    struct Sample {
        double t, y;
        bool allOff;
        double lift;
    };
    std::vector<Sample> s;
    const double t0 = ctx.t;
    ctx.run(5.0, [&] {
        c.handbrake = 1;
        bool none = true;
        double lift = 0;
        for (const Wheel& w : c.wheels) {
            none &= w.load <= 0;
            const V3 hub = (c.nodes[w.inner].p + c.nodes[w.outer].p) * 0.5;
            lift = std::max(lift, hub.y - w.R - ctx.sim.world.height(hub.x, hub.z));
        }
        s.push_back({ctx.t - t0, c.frame().com.y, none, lift});
    });
    Landing L;
    double yEq = 0;
    int n = 0;
    for (const Sample& e : s)
        if (e.t > s.back().t - 0.8) yEq += e.y, n++;
    yEq /= std::max(1, n);
    size_t iLow = 0;
    for (size_t i = 0; i < s.size(); i++)
        if (s[i].y < s[iLow].y) iLow = i;
    L.squat = yEq - s[iLow].y;
    int side = -1;  // (below to start with)
    for (size_t i = iLow; i < s.size(); i++) {
        const double e = s[i].y - yEq;
        L.rebound = std::max(L.rebound, e);
        if (s[i].allOff) L.air += Ctx::kFrame;
        L.lift = std::max(L.lift, s[i].lift);  // (any one tire, the highest it gets)
        if (e * side < -0.02) L.bounces++, side = -side;
        if (std::fabs(e) > 0.01) L.settle = s[i].t - s[iLow].t;
    }
    return L;
}

// A steady push of `g` g on every node (sideways, or backward as in braking), wheels braked:
// how far the body leans on its springs and tires (deg; + toward the push).
double leanUnder(Ctx& ctx, const VehicleSpec& spec, double z, double g, bool sideways) {
    ctx.sim.cars.clear();
    Car& c = ctx.add(spec, -100, z, 0);
    ctx.focus(c);
    ctx.run(1.5, [&] { c.handbrake = 1, c.brake = 1; });
    const Frame f0 = c.frame();
    const V3 dir = sideways ? f0.left : f0.fwd;
    c.extForce.assign(c.nodes.size(), V3());
    for (size_t i = 0; i < c.nodes.size(); i++) c.extForce[i] = dir * (g * 9.81 * c.nodes[i].m);
    c.wake();
    ctx.run(2.0, [&] { c.handbrake = 1, c.brake = 1; });
    const Frame f = c.frame();
    c.extForce.clear();
    return std::asin(clampd(dot(f.up - f0.up, dir), -1, 1)) * 180 / M_PI;
}

}  // namespace

SCENARIO(weightFeelSurvey, "survey/weight-feel", "Survey (no limits): mass, CG, inertia, unsprung mass and tire hop, and 0.6 and 1 m flat landings for every vehicle") {
    for (const VehicleSpec& s : vehicleCatalog()) {
        if (std::getenv("CZ_WF_ONLY") && !std::strstr(s.name, std::getenv("CZ_WF_ONLY"))) continue;
        ctx.sim.cars.clear();
        Car& c = ctx.add(s, -100, 0, 0);
        ctx.focus(c);
        ctx.run(2.0, [&] { c.handbrake = 1; });
        const MassSpread m = massSpread(ctx, c);
        // The tire's vertical bounce on its own unsprung mass (a corner's share of it).
        const double us = m.unsprung / std::max<size_t>(1, c.wheels.size());
        const double K = c.wheels[0].radialK(c.tirePsi, s.tirePsi, s.tireSoft);
        std::printf("MASS %-18s %5.0f kg, CG %.2f m, track %.2f m, SSF %.2f | k roll %.2f pitch %.2f yaw %.2f m (I %4.0f %4.0f %4.0f kg m^2) | unsprung %3.0f kg/corner, tire K %.0f kN/m, hop %.1f Hz\n",
                    s.name, m.M, m.cgH, m.track, m.track / (2 * m.cgH), m.kRoll, m.kPitch, m.kYaw, m.M * m.kRoll * m.kRoll,
                    m.M * m.kPitch * m.kPitch, m.M * m.kYaw * m.kYaw, us, K / 1000, std::sqrt(K / us) / (2 * M_PI));
        const double roll = leanUnder(ctx, s, 0, 0.4, true), pitch = leanUnder(ctx, s, 0, 0.5, false);
        std::printf("LEAN %-18s roll %.1f deg/g (0.4 g sideways), pitch %.1f deg/g (0.5 g braking)\n", s.name, roll / 0.4, pitch / 0.5);
        for (double h : {0.6, 1.0}) {
            const Landing L = landing(ctx, s, 0, h);
            std::printf("LAND %-18s dropped %.1f m: squat %3.0f mm, rebound %3.0f mm (%3.0f %%), all four up %.2f s, a tire up to %3.0f mm, %d swings, settled %.2f s\n", s.name, h,
                        L.squat * 1000, L.rebound * 1000, L.rebound / std::max(1e-6, L.squat) * 100, L.air, L.lift * 1000, L.bounces, L.settle);
        }
    }
}

// A hard flat landing thuds and settles: the suspension and the tires soak it up rather than
// spring the truck back into the air. (Before the tires' carcass lost energy as it unloaded,
// a truck dropped 1 m rebounded 110-130 % of its squat, all four tires left the ground for
// ~0.35 s and came up 21-27 cm: it bounced off its own tires.) A real truck dropped flat
// 1 m onto concrete squats onto its bump stops, comes back up past its ride height once,
// maybe skips a tire a little, and sits.
VEHICLE_SCENARIO(landingSettle, "dynamics/landing-settle", "Dropped 1 m flat onto concrete: it lands with a thud and settles (a small rebound, the tires barely leave the ground, within ~1 s)") {
    // The trucks (Bronco, 4Runner, Tacoma, Blazer) are held hardest: they're what this is about. The
    // cars and the light off-roaders on their own springs and tires a little looser (before: the
    // Jesko 79 %, the Baja 84 % with a tire 15 cm up).
    const bool truck = spec.wheelLoss;
    const Landing L = landing(ctx, spec, 0, 1.0);
    std::printf("LAND %-18s dropped 1.0 m: squat %3.0f mm, rebound %3.0f mm (%3.0f %%), all four up %.2f s, a tire up to %3.0f mm, %d swings, settled %.2f s\n", spec.name,
                L.squat * 1000, L.rebound * 1000, L.rebound / std::max(1e-6, L.squat) * 100, L.air, L.lift * 1000, L.bounces, L.settle);
    ctx.metric("rebound over its ride height (share of the squat)", L.rebound / std::max(1e-6, L.squat) * 100, "%", 0, truck ? 55 : 75);
    ctx.metric("time all four tires are off the ground after the squat", L.air, "s", 0, 0.22);
    ctx.metric("highest a tire comes off the ground after the squat", L.lift * 1000, "mm", 0, truck ? 50 : 110);
    ctx.metric("swings past its ride height (2 cm)", L.bounces, "", 0, 3);
    ctx.metric("settled within 1 cm", L.settle, "s", 0, truck ? 1.1 : 1.4);
}

// Tire load sensitivity: a tire's friction coefficient falls as it's loaded harder (the
// rubber can't key into the road as well under more pressure). Real tires lose ~5-15 % of
// their mu from their rated load to twice it. Pushed along on locked wheels until it
// slides, a Bronco carrying 60 % more weight (a load pressed down on every node) holds a
// little less than its own weight's share.
SCENARIO(loadSensitivity, "tires/load-sensitivity", "A tire's grip coefficient falls as its load rises: the Bronco shoved along on locked wheels until it slides holds a few % less per kg when it's 60 % heavier") {
    const VehicleSpec& spec = vehicleCatalog()[2];
    auto slideMu = [&](double extra) {
        ctx.sim.cars.clear();
        Car& c = ctx.add(spec, -100, 0, 0);
        ctx.focus(c);
        c.extForce.assign(c.nodes.size(), V3());
        for (size_t i = 0; i < c.nodes.size(); i++) c.extForce[i] = V3(0, -extra * 9.81 * c.nodes[i].m, 0);
        ctx.run(2.0, [&] { c.handbrake = 1, c.brake = 1; });
        const V3 side = c.frame().fwd, p0 = c.frame().com;  // (pushed along it: sideways it would tip first)
        double mu = 0;
        for (double a = 0.3; a < 2.0; a += 0.01) {  // the push, in g of the loaded weight
            for (size_t i = 0; i < c.nodes.size(); i++) c.extForce[i] = V3(0, -extra * 9.81 * c.nodes[i].m, 0) + side * (a * (1 + extra) * 9.81 * c.nodes[i].m);
            c.wake();
            ctx.run(0.1, [&] { c.handbrake = 1, c.brake = 1; });
            if (std::fabs(dot(c.frame().com - p0, side)) > 0.25) break;
            mu = a;
        }
        c.extForce.clear();
        return mu;
    };
    const double mu1 = slideMu(0), mu2 = slideMu(0.6);
    std::printf("LOADMU Bronco slides on locked wheels at %.2f g, %.2f g 60 %% heavier (x%.3f)\n", mu1, mu2, mu2 / std::max(mu1, 1e-6));
    ctx.metric("grip 60 % heavier, against its own (x)", mu2 / std::max(mu1, 1e-6), "", 0.85, 0.97);
}

// Low-speed traction: a Bronco in 4L first with both lockers, flat out against a strap to a load
// cell behind it, tires spinning: what it pulls, over its weight. That's about the steepest bank
// it can walk up (its tangent). Real all-terrains at street
// pressure pull ~0.65-0.75 of their weight on dry dirt and ~0.55-0.65 on loose gravel (what
// the soil shears at); aired down a little more on loose ground, not much (the patch is
// longer, the soil isn't stronger). (Before: 0.83 on dirt, 0.98 aired down, gravel 0.72 and 0.88: it walked up dirt
// banks no real truck climbs.)
namespace {
double drawbar(Ctx& ctx, const VehicleSpec& spec, V3 at, double psi) {
    ctx.sim.cars.clear();
    Car& c = ctx.add(spec, at.x, at.z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    c.tirePsi = psi;
    for (int m = (int)driveModes(spec.transfer).size() - 1; m >= 0; m--)
        if (driveModes(spec.transfer)[m].low) {
            c.setDriveMode(m);
            break;
        }
    c.setLocker(true, true);
    c.setLocker(false, true);
    c.tractionControl = false;
    c.manualShift = true;
    c.gear = 1;
    // A strap from the tail to a load cell behind it: slack for the first 0.3 m, then a stiff
    // spring (damped, so it doesn't bounce) pulling back on every node by its mass. The most it
    // holds for half a second, over its weight.
    const Frame f0 = c.frame();
    const V3 fwd = norm(V3(f0.fwd.x, 0, f0.fwd.z));
    c.extForce.assign(c.nodes.size(), V3());
    double sum = 0;
    int n = 0;
    const double t0 = ctx.t;
    ctx.run(6.0, [&] {
        c.throttle = 1, c.brake = 0, c.handbrake = 0, c.steer = 0;
        const Frame f = c.frame();
        const double x = dot(f.com - f0.com, fwd) - 0.3, v = dot(f.vel, fwd);
        const double F = x > 0 ? std::max(0.0, 6e4 * x + 1.5e4 * v) : 0.0;
        for (size_t i = 0; i < c.nodes.size(); i++) c.extForce[i] = -fwd * (F * c.nodes[i].m / c.mass);
        if (ctx.t - t0 > 3.0) sum += F, n++;  // (steady: the tires spinning against the strap)
    });
    c.extForce.clear();
    return sum / std::max(1, n) / (c.mass * 9.81);
}
}  // namespace

SCENARIO(lowSpeedPull, "offroad/drawbar-pull", "Bronco in 4L with lockers, crawling, pulled back until it's dragged: on dry dirt it pulls ~0.65-0.75 of its weight, on loose gravel ~0.55-0.65; airing down to 15 psi adds only a little on loose ground") {
    const VehicleSpec& spec = vehicleCatalog()[2];
    const V3 gravel = ctx.sim.world.courseStart(SURF_GRAVEL) + V3(10, 0, 0);
    const double g35 = drawbar(ctx, spec, gravel, spec.tirePsi), g15 = drawbar(ctx, spec, gravel, 15);
    // Flat dry dirt: on the mountain map, the first patch of it ~12 m across that's level.
    ctx.sim.cars.clear();
    ctx.sim.world = World(MAP_MOUNTAIN);
    const World& w = ctx.sim.world;
    V3 dirt;
    bool found = false;
    for (double r = 0; r < 600 && !found; r += 8)
        for (double a = 0; a < 6.28 && !found; a += 8 / std::max(r, 8.0)) {
            const double x = r * std::cos(a), z = r * std::sin(a);
            bool ok = true;
            for (int i = -1; i <= 1 && ok; i++)
                for (int j = -1; j <= 1 && ok; j++)
                    ok = w.surfaceAt(x + 6 * i, z + 6 * j) == SURF_DIRT && w.normal(x + 6 * i, z + 6 * j).y > 0.995 &&
                         std::fabs(w.height(x + 6 * i, z + 6 * j) - w.height(x, z)) < 0.3;
            if (ok) dirt = V3(x - 3, 0, z), found = true;
        }
    ctx.check("found a level patch of dirt on the mountain map", found);
    const double d35 = found ? drawbar(ctx, spec, dirt, spec.tirePsi) : 0, d15 = found ? drawbar(ctx, spec, dirt, 15) : 0;
    std::printf("PULL Bronco 4L lockers: dirt %.2f (15 psi %.2f), gravel %.2f (15 psi %.2f) of its weight\n", d35, d15, g35, g15);
    ctx.metric("dry dirt at 35 psi: pull over weight", d35, "", 0.62, 0.78);
    ctx.metric("loose gravel at 35 psi: pull over weight", g35, "", 0.50, 0.66);
    ctx.metric("dirt aired down to 15 psi: pull against 35 psi (x)", d15 / std::max(d35, 1e-6), "", 1.0, 1.15);
    ctx.metric("gravel aired down to 15 psi: pull against 35 psi (x)", g15 / std::max(g35, 1e-6), "", 1.0, 1.15);
}
