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
