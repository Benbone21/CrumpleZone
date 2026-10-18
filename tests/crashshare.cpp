// Car into car: who takes the damage. The contact is one force, equal and opposite on the
// two bodies, so which of them was moving can't matter (only their speed apart), and two
// of the same car nose to nose share the crash. A heavy truck into a light car is lopsided
// the way momentum says. A parked car that has fallen asleep (or an AI's) takes it like any.
#include <cmath>
#include <cstdio>

#include "harness.h"

namespace {
const double kZ = -50;

// Work done bending the structure: each beam's set times about the force it yields at.
double bendWork(const Car& c) {
    double w = 0;
    for (const Beam& b : c.beams)
        if (b.type == BEAM_BODY || b.type == BEAM_CABIN || b.type == BEAM_FRAME || b.type == BEAM_PANEL)
            w += b.k * b.yieldStrain * b.Lorig * std::fabs(b.L0 - b.Lorig);
    return w;
}

// Ladder-frame members bent or torn.
int frameBent(const Car& c) {
    int n = 0;
    for (const Beam& b : c.beams) n += b.type == BEAM_FRAME && (b.broken || std::fabs(b.L0 - b.Lorig) > 0.002);
    return n;
}

// The ladder rails' rest shape (the steel's set), to measure how far a hit bent them.
struct RailShape {
    std::vector<V3> rest[2];
    explicit RailShape(const Car& c) {
        for (int r = 0; r < 2; r++) rest[r] = c.railFrame[r].rest;
    }
    // Furthest a rail member's rest position has moved (m), the frame's centre held still.
    double set(const Car& c) const {
        double worst = 0;
        for (int r = 0; r < 2; r++) {
            const std::vector<V3>& now = c.railFrame[r].rest;
            if (now.size() != rest[r].size()) continue;
            V3 shift;
            for (size_t k = 0; k < now.size(); k++) shift += (now[k] - rest[r][k]) / (double)now.size();
            for (size_t k = 0; k < now.size(); k++) worst = std::max(worst, len(now[k] - rest[r][k] - shift));
        }
        return worst;
    }
};

V3 momentum(const Car& c) {
    V3 p;
    for (const Node& n : c.nodes) p += n.v * n.m;
    return p;
}

struct Crash {
    double dmgA = 0, dmgB = 0;    // damage after (0..1)
    double workA = 0, workB = 0;  // bend work (J)
    V3 dpA, dpB;                  // momentum change across the contact (the first 0.3 s)
    double frameA = 0, frameB = 0;  // frame members bent
    double setA = 0, setB = 0;      // worst rail set (m)
    double upB = 1;                 // the struck one's least up (1 upright, 0 on its side)
    bool bWoke = true;
};

struct Setup {
    const VehicleSpec* a;
    const VehicleSpec* b;
    double kmh = 50;
    double headingB = 180;  // 180 nose to nose, 90 A's nose into B's side
    double share = 1;       // of the closing speed A has (the rest B, toward A)
    bool asleep = true;     // B has stood long enough to be put to sleep
    bool aiB = false;       // B is driven by the AI
    double settle = 0;      // s to stand first (0: 3 asleep, 1 awake)
    double offset = 0;      // A's line, to the side of B's middle (m)
};

// A heads +X into B. Both settle first; B falls asleep unless told otherwise.
Crash crash(Ctx& ctx, const Setup& s) {
    ctx.sim.cars.clear();
    ctx.sim.looseWheels.clear();  // (what the last crash left lying about)
    const bool side = s.headingB == 90;
    const double xa = -(s.a->length / 2 + (side ? s.b->width : s.b->length) / 2 + 3);
    ctx.add(*s.a, xa, kZ + s.offset, 0);
    ctx.add(*s.b, 0, kZ, s.headingB);
    ctx.sim.player = 0;
    ctx.run(s.settle > 0 ? s.settle : s.asleep ? 3.0 : 1.0);
    Car& a = ctx.car(0);
    Car& b = ctx.car(1);
    Crash r;
    if (s.asleep && !ctx.check("the parked one fell asleep", b.sleeping)) return r;
    if (!s.asleep) b.wake();
    if (s.share > 0) ctx.launch(a, s.share * s.kmh / 3.6);
    if (s.share < 1) ctx.launch(b, (1 - s.share) * s.kmh / 3.6);
    if (s.aiB) b.ai = true;
    const double w0a = bendWork(a), w0b = bendWork(b);
    const RailShape railA(a), railB(b);
    // Momentum across the contact: from the frame before they touch to 0.15 s on (ground
    // friction is small next to the crash over that long; along the road only, as the
    // ground takes the noses' dive and lift).
    V3 pa0 = momentum(a), pb0 = momentum(b);
    double touched = -1;
    ctx.run(2.0, [&] {
        if (touched < 0) {
            const bool near = a.bbMax.x >= b.bbMin.x && a.bbMin.x <= b.bbMax.x;
            if (near) touched = ctx.t;
            else pa0 = momentum(a), pb0 = momentum(b);
        } else if (ctx.t - touched < 0.15) {
            r.dpA = momentum(a) - pa0;
            r.dpB = momentum(b) - pb0;
            r.dpA.y = r.dpB.y = 0;
        }
        if (touched >= 0 && ctx.t - touched > 0.1 && b.sleeping) r.bWoke = false;
        r.upB = std::min(r.upB, b.frame().up.y);
    });
    r.dmgA = a.damage, r.dmgB = b.damage;
    r.frameA = frameBent(a), r.frameB = frameBent(b);
    r.setA = railA.set(a), r.setB = railB.set(b);
    r.workA = bendWork(a) - w0a, r.workB = bendWork(b) - w0b;
    return r;
}

// A crumple picks its own buckling order: the same crash a few centimetres either side,
// summed, is what one car of the pair takes on average.
Crash crashes(Ctx& ctx, Setup s) {
    Crash sum;
    for (double off : {-0.1, 0.0, 0.1}) {
        s.offset = off;
        const Crash r = crash(ctx, s);
        sum.dmgA += r.dmgA / 3, sum.dmgB += r.dmgB / 3;
        sum.workA += r.workA / 3, sum.workB += r.workB / 3;
        sum.dpA += r.dpA / 3, sum.dpB += r.dpB / 3;
        sum.frameA += r.frameA / 3, sum.frameB += r.frameB / 3;
        sum.setA += r.setA / 3, sum.setB += r.setB / 3;
        sum.upB = std::min(sum.upB, r.upB);
        sum.bWoke &= r.bWoke;
    }
    return sum;
}

void report(Ctx& ctx, const std::string& what, const Crash& r) {
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s: damage %.2f %% / %.2f %%, bend work %.1f / %.1f kJ", what.c_str(), r.dmgA * 100, r.dmgB * 100,
                  r.workA / 1000, r.workB / 1000);
    ctx.note(buf);
}

const VehicleSpec& civic() { return vehicleCatalog()[0]; }
const VehicleSpec& tacoma() { return vehicleCatalog()[6]; }
const VehicleSpec& blazer() { return vehicleCatalog()[7]; }
}

// The same structure on both sides of the contact soaks up the same: within what the
// folds' chaos allows (a crumple picks its own buckling order), so 0.6 to 1.6.
SCENARIO(shareHeadOn, "damage/share/head-on", "Blazer at 50 km/h into the nose of a parked Blazer: they share the crash, and both frames take it") {
    const Crash r = crashes(ctx, {&blazer(), &blazer()});
    report(ctx, "moving / parked", r);
    ctx.metric("damage, parked / moving", r.dmgB / std::max(r.dmgA, 1e-6), "", 0.6, 1.6);
    ctx.metric("bend work, parked / moving", r.workB / std::max(r.workA, 1.0), "", 0.6, 1.6);
    ctx.metric("damage, moving", r.dmgA * 100, "%", 1.5, 15);
    ctx.check("the parked one woke up when hit", r.bWoke && !ctx.car(1).sleeping);
    // (The ladder frames meet behind the bumpers: they used to pass through each other.)
    ctx.metric("frame members bent, moving", r.frameA, "", 1, 1e9);
    ctx.metric("frame members bent, parked", r.frameB, "", 1, 1e9);
}

// Only the speed apart matters: the same crash with the other car moving, or both at half.
SCENARIO(shareWhoMoves, "damage/share/who-moves", "The same Blazer head-on with A moving, B moving, or both at half speed: each car's damage is the same") {
    const Crash ref = crashes(ctx, {&blazer(), &blazer()});
    Setup bMoves{&blazer(), &blazer()};
    bMoves.share = 0, bMoves.asleep = false;
    Setup half{&blazer(), &blazer()};
    half.share = 0.5, half.asleep = false;
    const Crash rb = crashes(ctx, bMoves), rh = crashes(ctx, half);
    report(ctx, "A moving", ref);
    report(ctx, "B moving", rb);
    report(ctx, "both at 25", rh);
    const double total = ref.dmgA + ref.dmgB;
    ctx.metric("total damage, B moving / A moving", (rb.dmgA + rb.dmgB) / std::max(total, 1e-6), "", 0.7, 1.4);
    ctx.metric("total damage, both at half / A moving", (rh.dmgA + rh.dmgB) / std::max(total, 1e-6), "", 0.7, 1.4);
    ctx.metric("B moving: damage A / B", rb.dmgA / std::max(rb.dmgB, 1e-6), "", 0.6, 1.6);
    ctx.metric("both at half: damage A / B", rh.dmgA / std::max(rh.dmgB, 1e-6), "", 0.6, 1.6);
}

// Newton's third law across the contact, and momentum shared by mass: a 2-tonne truck
// into a 1-tonne car changes the car's speed about twice as much.
SCENARIO(shareTruckCivic, "damage/share/truck-civic", "Tacoma at 50 km/h into a parked Civic's nose: equal and opposite push, the Civic's speed changes by the mass ratio more") {
    const Crash r = crash(ctx, {&tacoma(), &civic()});
    report(ctx, "Tacoma / Civic", r);
    const Car& t = ctx.car(0);
    const Car& c = ctx.car(1);
    ctx.metric("push on the two, |sum| / |each|", len(r.dpA + r.dpB) / std::max(len(r.dpA), 1.0) * 100, "%", 0, 5);
    const double dvT = len(r.dpA) / t.mass, dvC = len(r.dpB) / c.mass;
    ctx.metric("Civic speed change / Tacoma's (/ mass ratio)", dvC / std::max(dvT, 1e-6) / (t.mass / c.mass), "", 0.9, 1.1);
    ctx.metric("Civic speed change", dvC * 3.6, "km/h", 20, 40);
    ctx.metric("damage, Tacoma", r.dmgA * 100, "%", 0.3, 20);
    ctx.metric("damage, Civic", r.dmgB * 100, "%", 0.3, 20);
    // And the light one into the heavy one: the same push both ways.
    const Crash back = crash(ctx, {&civic(), &tacoma()});
    report(ctx, "Civic / Tacoma", back);
    ctx.metric("Civic into Tacoma: push |sum| / |each|", len(back.dpA + back.dpB) / std::max(len(back.dpA), 1.0) * 100, "%", 0, 5);
}

// The T-bone: one nose into the other's side (side structure is softer than a nose, so
// the struck one takes at least a fair share of the bending).
SCENARIO(shareTBone, "damage/share/t-bone", "Civic at 50 km/h into the side of a parked Civic: the struck side takes its share") {
    Setup s{&civic(), &civic()};
    s.headingB = 90;
    const Crash r = crashes(ctx, s);
    report(ctx, "striker / struck", r);
    ctx.metric("bend work, struck / striker", r.workB / std::max(r.workA, 1.0), "", 0.6, 3);
    ctx.metric("damage, struck / striker", r.dmgB / std::max(r.dmgA, 1e-6), "", 0.6, 4);
    ctx.check("the parked one woke up when hit", r.bWoke);
}

// Damage doesn't care who's driving: an AI car hit is hit like a parked one, and a car
// asleep when it's hit wakes and takes the full crash (not a static support).
SCENARIO(shareAiAndAsleep, "damage/share/ai-asleep", "The parked Blazer hit asleep, awake, or under the AI: the same damage each way") {
    const Crash asleep = crashes(ctx, {&blazer(), &blazer()});
    Setup awake{&blazer(), &blazer()};
    awake.asleep = false;
    Setup ai{&blazer(), &blazer()};
    ai.asleep = false, ai.aiB = true;
    const Crash rw = crashes(ctx, awake), ra = crashes(ctx, ai);
    report(ctx, "asleep", asleep);
    report(ctx, "awake", rw);
    report(ctx, "AI", ra);
    ctx.metric("struck damage, asleep / awake", asleep.dmgB / std::max(rw.dmgB, 1e-6), "", 0.6, 1.6);
    // (The AI steps on it as it's hit, so a little more, not less.)
    ctx.metric("struck damage, AI / awake", ra.dmgB / std::max(rw.dmgB, 1e-6), "", 0.6, 2.0);
}

// The Blazer's nose is boxed onto its frame horns: half as hard again to crush as the stock
// lattice. Nose to nose with one built without that, the plain one takes about 1.5 times
// the bending (the same force through both, set over 1.5 times the distance).
SCENARIO(shareNose, "damage/share/blazer-nose", "Blazer nose to nose with a Blazer built with a stock-strength nose: the stock one crushes about 1.5 times as much") {
    static const VehicleSpec stock = [] {
        VehicleSpec v = vehicleCatalog()[7];
        v.noseStrength = 1;
        return v;
    }();
    const Crash r = crashes(ctx, {&blazer(), &stock});
    report(ctx, "Blazer / stock nose", r);
    ctx.metric("bend work, stock nose / Blazer's", r.workB / std::max(r.workA, 1.0), "", 1.2, 2.2);
}

// The doors, sills and tail give: a nose into another truck's side or tail bends both
// about alike (by mass), rather than the struck one shoved off whole with a scratch.
SCENARIO(shareSideRear, "damage/share/side-rear", "Blazer at 50 km/h into a Bronco's side and a Blazer's tail: the struck one takes a like share of the bending, and a caved door shows in its damage") {
    const VehicleSpec& bronco = vehicleCatalog()[2];
    const Crash side = crashes(ctx, {&blazer(), &bronco, 50, 90});
    report(ctx, "Blazer into the Bronco's side", side);
    ctx.metric("side: bend work, Bronco / Blazer", side.workB / std::max(side.workA, 1.0), "", 0.5, 2.5);
    ctx.metric("side: Bronco damage", side.dmgB * 100, "%", 1, 20);
    const Crash rear = crashes(ctx, {&blazer(), &blazer(), 50, 0});
    report(ctx, "Blazer into a Blazer's tail", rear);
    ctx.metric("rear: bend work, struck / striker", rear.workB / std::max(rear.workA, 1.0), "", 0.7, 2.5);
    ctx.metric("rear: struck damage / striker's", rear.dmgB / std::max(rear.dmgA, 1e-6), "", 0.6, 2.5);
    // A T-boned Civic: the door caved in, and counted (a little).
    Setup t{&civic(), &civic(), 50, 90};
    const Crash r = crash(ctx, t);
    const Car& c = ctx.car(1);
    double worst = 0;
    for (const Panel& p : c.panels) {
        double d = 0;
        int m = 0;
        for (int i : p.bodyNodes)
            if (c.chassis.slot[i] >= 0) d += len(c.chassis.rest[c.chassis.slot[i]] - c.chassisRest0[c.chassis.slot[i]]), m++;
        if (m) worst = std::max(worst, d / m);
    }
    ctx.metric("T-boned Civic: its worst panel pushed in (mean)", worst * 100, "cm", 4, 40);
    ctx.metric("T-boned Civic: damage", r.dmgB * 100, "%", 3, 20);
}
