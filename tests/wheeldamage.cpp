// Front wheel damage: what ordinary off-road hits do to a front corner's geometry, and
// whether a field repair puts it back.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "harness.h"

namespace {

const VehicleSpec& byName(const char* n) {
    for (const VehicleSpec& s : vehicleCatalog())
        if (std::strstr(s.name, n)) return s;
    return vehicleCatalog()[0];
}

// A wheel's alignment against the body, from its axle line: camber (deg, + top out),
// toe (deg, + toward the nose... as steer, + left) and how far its hub sits from where
// it was built (cm, in the car's frame, ride-height bounce taken out: x and z only).
struct Align {
    double camber = 0, toe = 0, hubMove = 0;
};
// What the suspension hangs on: a ladder frame's rails (the body tub sits on rubber mounts
// above them and can be knocked askew on its own), else the body.
const Quat& baseRot(const Car& c) { return c.railFrame[0].nodes.empty() ? c.chassis.rot : c.railFrame[0].rot; }
Align alignOf(const Car& c, const Wheel& wh, const Align& ref) {
    const V3 a = baseRot(c).unrotate(c.nodes[wh.outer].p - c.nodes[wh.inner].p), a0 = c.restLocal[wh.outer] - c.restLocal[wh.inner];
    const double side = a0.z > 0 ? 1 : -1;
    Align r;
    // (Toe against the rear wheels' line, the car's thrust line: a crumpled corner skews the
    // body's best fit, so both fronts read toed the same way.)
    double rear = 0;
    int nr = 0;
    for (const Wheel& o : c.wheels) {
        if (o.front) continue;
        const V3 b = baseRot(c).unrotate(c.nodes[o.outer].p - c.nodes[o.inner].p), b0 = c.restLocal[o.outer] - c.restLocal[o.inner];
        rear += std::remainder(std::atan2(b.x, b.z) - std::atan2(b0.x, b0.z), 2 * M_PI), nr++;
    }
    rear /= std::max(nr, 1);
    r.toe = std::remainder(std::atan2(a.x, a.z) - std::atan2(a0.x, a0.z) - rear, 2 * M_PI) * 180 / M_PI - ref.toe;
    r.camber = (std::asin(a.y / len(a)) - std::asin(a0.y / len(a0))) * side * 180 / M_PI - ref.camber;
    const V3 hub = baseRot(c).unrotate((c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5 - c.frame().com);
    const V3 hub0 = (c.restLocal[wh.inner] + c.restLocal[wh.outer]) * 0.5;
    r.hubMove = std::hypot(hub.x - hub0.x, hub.z - hub0.z) * 100 - ref.hubMove;
    return r;
}

// The body ends of a corner's links (its pick-ups), in the body's frame.
std::vector<V3> pickupsOf(const Car& c, const Wheel& wh) {
    auto in = [&](int n) { for (int a : wh.assembly) if (a == n) return true; return false; };
    std::vector<V3> out;
    const V3 com = c.frame().com;
    for (int bi : wh.mounts) {
        if (bi < 0 || c.beams[bi].type == BEAM_SPRING) continue;
        const int far = in(c.beams[bi].a) ? c.beams[bi].b : c.beams[bi].a;
        out.push_back(baseRot(c).unrotate(c.nodes[far].p - com));
    }
    return out;
}
// How far they moved against each other since `was` (cm, worst; their mean drift taken out).
double pickupMove(const std::vector<V3>& now, const std::vector<V3>& was) {
    V3 mean;
    for (size_t i = 0; i < now.size(); i++) mean += (now[i] - was[i]) / (double)now.size();
    double worst = 0;
    for (size_t i = 0; i < now.size(); i++) worst = std::max(worst, len(now[i] - was[i] - mean));
    return worst * 100;
}

// The knuckle's own shape: the worst change in distance between its hub, ball joint and
// steering arm nodes (mm).
double knuckleShape(const Car& c, const Wheel& wh) {
    const int ns[] = {wh.inner, wh.outer, wh.lower, wh.top, wh.arm};
    double worst = 0;
    for (int i = 0; i < 5; i++)
        for (int j = i + 1; j < 5; j++) {
            if (ns[i] < 0 || ns[j] < 0) continue;
            worst = std::max(worst, std::fabs(len(c.nodes[ns[i]].p - c.nodes[ns[j]].p) - len(c.restLocal[ns[i]] - c.restLocal[ns[j]])));
        }
    return worst * 1000;
}
// A corner's links' worst set (% of length; the tie rod's is its tieOffset).
double linkSet(const Car& c, const Wheel& wh) {
    double worst = 0;
    for (int bi : wh.mounts)
        if (bi >= 0 && c.beams[bi].type == BEAM_ARM) worst = std::max(worst, c.beams[bi].broken ? 100.0 : std::fabs(c.beams[bi].L0 - c.beams[bi].Lorig) / c.beams[bi].Lorig * 100);
    return worst;
}

V3 hubOf(const Car& c, const Wheel& wh) { return (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5; }

// What took a set near a corner: bent beams by type within 0.9 m of its hub.
std::string setsNear(const Car& c, const Wheel& wh, const std::vector<double>& L0before) {
    static const char* names[] = {"body", "cabin", "arm", "coil", "knuckle", "frame", "mount", "engine", "tierod", "shaft", "panel", "bump"};
    int n[BEAM_TYPE_COUNT] = {}, br[BEAM_TYPE_COUNT] = {};
    double worst[BEAM_TYPE_COUNT] = {};
    const V3 hub = hubOf(c, wh);
    for (size_t i = 0; i < c.beams.size(); i++) {
        const Beam& b = c.beams[i];
        if (len((c.nodes[b.a].p + c.nodes[b.b].p) * 0.5 - hub) > 0.9) continue;
        if (b.broken) br[b.type]++;
        else if (std::fabs(b.L0 - L0before[i]) > 0.002 * b.Lorig) n[b.type]++, worst[b.type] = std::max(worst[b.type], std::fabs(b.L0 - L0before[i]) / b.Lorig * 100);
    }
    std::string s;
    char buf[96];
    for (int t = 0; t < BEAM_TYPE_COUNT; t++)
        if (n[t] || br[t]) std::snprintf(buf, sizeof buf, " %s:%d(%.1f%%)%s", t < 12 ? names[t] : "?", n[t], worst[t], br[t] ? (" broken " + std::to_string(br[t])).c_str() : ""), s += buf;
    return s;
}

enum HitKind { CURB, ROCK, CORNER, DROP, FIELD };
const char* const kHitName[] = {"curb 15cm", "rock 30cm", "wall corner", "drop m", "rock field"};

// One hit on a fresh car on flat ground, what it left, and what a field repair put back.
struct Outcome {
    Align hit, other, hitFixed, otherFixed;  // the struck front wheel's and the other's, after; after the repair
    double pickups = 0;                      // the struck corner's pick-ups moved against each other (cm)
    double knuckle = 0, knuckleFixed = 0;    // its knuckle out of shape (mm)
    double railFlow = 0;                     // the most a ladder frame's rail flowed (cm)
    double damage = 0;
    bool offered = false;                    // a field repair was offered
    std::string sets, otherSets;
};

Outcome runHit(const VehicleSpec& spec, HitKind kind, double v) {
    Outcome o;
    Sim sim2;
    sim2.setupEmpty();
    Ctx c2(sim2);
    Car& c = c2.add(spec, 0, 0, 0);
    c2.run(1.0);
    int fw[2] = {-1, -1}, nf = 0;
    for (int i = 0; i < (int)c.wheels.size(); i++)
        if (c.wheels[i].front && nf < 2) fw[nf++] = i;
    Align ref[2];
    for (int k = 0; k < 2; k++) ref[k] = alignOf(c, c.wheels[fw[k]], Align());
    const std::vector<V3> rail0[2] = {c.railFrame[0].rest, c.railFrame[1].rest};
    std::vector<double> L0(c.beams.size());
    for (size_t i = 0; i < c.beams.size(); i++) L0[i] = c.beams[i].L0;
    // (The hits go on the front wheel on the +z side.)
    const int L = c.restLocal[c.wheels[fw[0]].inner].z > c.restLocal[c.wheels[fw[1]].inner].z ? 0 : 1;
    const Wheel& wh = c.wheels[fw[L]];
    const V3 hub = hubOf(c, wh);
    const std::vector<V3> pk0 = pickupsOf(c, wh);
    const double ks0 = knuckleShape(c, wh);
    const Frame f = c.frame();
    const double gx = hub.x + 3.0;
    World& w = sim2.world;
    const size_t boxes0 = w.boxes.size();
    const V3 grey(0.5, 0.5, 0.5);
    if (kind == CURB) w.addBox(V3(gx, 0.075, hub.z), V3(0.25, 0.075, 0.35), 0, 0, 0, grey);  // a square-edged 15 cm curb under one wheel
    if (kind == ROCK) w.addBox(V3(gx, 0.15, hub.z), V3(0.25, 0.15, 0.2), 0, 0, 0, grey);     // a 30 cm step of rock
    if (kind == CORNER) w.addBox(V3(gx + 1.5, 1.0, hub.z + 0.65), V3(0.5, 1.0, 1.0), 0, 0, 0, grey);  // a wall overlapping the nose 35 cm in from that side
    if (kind == FIELD) {  // a 25 m strip of loose rocks, 16-44 cm tall
        unsigned sd = 12345;
        auto rnd = [&] {
            sd = sd * 1103515245u + 12345u;
            return (sd >> 8 & 0xffff) / 65535.0;
        };
        for (int i = 0; i < 120; i++)
            w.addBox(V3(gx + rnd() * 25, 0.05, f.com.z + (rnd() - 0.5) * 3), V3(0.15 + rnd() * 0.15, 0.08 + rnd() * 0.14, 0.15 + rnd() * 0.15), rnd() * 3, rnd() * 0.4, rnd() * 0.4, grey);
    }
    w.buildGrid();
    if (kind == DROP) {  // dropped flat from v m
        for (Node& n : c.nodes) n.p.y += v, n.v = V3();
        c.wake();
        c2.run(3.0);
    } else {
        c2.launch(c, v / 3.6);
        c2.run(kind == FIELD ? 6.0 : 2.0, [&] {
            c.steerInput = 0;
            c.throttle = kind == FIELD && dot(c.frame().vel, c.frame().fwd) < v / 3.6 ? 0.5 : 0;
        });
        c2.run(3.0, [&] { c.throttle = 0, c.brake = 1, c.steerInput = 0; });
    }
    // (Measured on flat ground: what it hit taken away, it settles where it stopped. Hung up on a
    // rock or against the wall, a live axle's droop reads as camber.)
    w.boxes.resize(boxes0);
    w.buildGrid();
    c.wake();
    c2.run(2.5, [&] { c.steerInput = 0, c.brake = 1; });
    o.hit = alignOf(c, wh, ref[L]);
    o.other = alignOf(c, c.wheels[fw[1 - L]], ref[1 - L]);
    o.pickups = pickupMove(pickupsOf(c, wh), pk0);
    o.knuckle = knuckleShape(c, wh) - ks0;
    // (At the corner: the rail and brackets within 0.6 m of the axle line, where the suspension hangs.)
    for (int r = 0; r < 2; r++)
        for (size_t k = 0; k < rail0[r].size(); k++)
            if (std::fabs(c.restLocal[c.railFrame[r].nodes[k]].x - c.restLocal[wh.inner].x) < 0.6)
                o.railFlow = std::max(o.railFlow, len(c.railFrame[r].rest[k] - rail0[r][k]) * 100);
    o.damage = c.damage;
    if (std::getenv("WD_GEO"))
        for (int k = 0; k < 2; k++) {
            const Wheel& w2 = c.wheels[fw[k]];
            const V3 kax = norm(w2.kpUp - w2.kpLow);
            double best = 0, bl = 0;
            for (double t = -1.6; t <= 1.6; t += 0.01) {
                const double l = len(w2.rackRest - (w2.kpLow + rotateAxis(w2.armRest - w2.kpLow, kax, t)));
                std::printf("GEO %d %.2f %.4f\n", k, t, l);
                if (l > bl) bl = l, best = t;
            }
            std::printf("GEO %d dead centre at %.2f rad, rest length %.4f\n", k, best, len(w2.rackRest - w2.armRest));
        }
    if (std::getenv("WD_DEBUG"))
        for (int k = 0; k < 2; k++) {
            const Wheel& w2 = c.wheels[fw[k]];
            std::printf("  front %d knuckle shape %.1f mm, kp->arm rest %.3f now %.3f, hub->arm rest %.3f now %.3f\n", k, knuckleShape(c, w2), len(c.restLocal[w2.arm] - c.restLocal[w2.lower]), len(c.nodes[w2.arm].p - c.nodes[w2.lower].p), len(c.restLocal[w2.arm] - c.restLocal[w2.outer]), len(c.nodes[w2.arm].p - c.nodes[w2.outer].p));
            std::printf("  front %d: tie rod %s L0 %.3f Lorig %.3f len %.3f tieOffset %.1f mm, rackSteer %.3f\n", k, c.beams[w2.tieRod].broken ? "BROKEN" : "ok", c.beams[w2.tieRod].L0, c.beams[w2.tieRod].Lorig,
                        len(c.nodes[c.beams[w2.tieRod].a].p - c.nodes[c.beams[w2.tieRod].b].p), w2.tieOffset * 1000, c.rackSteer);
        }
    o.sets = setsNear(c, wh, L0);
    o.otherSets = setsNear(c, c.wheels[fw[1 - L]], L0);
    // The trail repair, with all the parts it wants.
    c.repairKit.tieRods = c.repairKit.suspensionLinks = c.repairKit.cvJoints = 99;
    o.offered = c.needsFieldRepair();
    c.fieldRepair();
    c2.run(2.5, [&] { c.steerInput = 0, c.brake = 1; });
    o.hitFixed = alignOf(c, wh, ref[L]);
    o.otherFixed = alignOf(c, c.wheels[fw[1 - L]], ref[1 - L]);
    o.knuckleFixed = knuckleShape(c, wh) - ks0;
    return o;
}

double worstAlign(const Align& a, const Align& b) { return std::max({std::fabs(a.camber), std::fabs(a.toe), std::fabs(b.camber), std::fabs(b.toe)}); }

}  // namespace

// Survey: every hit on a fresh car, both front wheels' alignment after, and after a field repair.
SCENARIO(frontWheelSurvey, "survey/front-wheel-damage", "Survey (no limits): curb, rock, wall-corner hits, drops and a rock field, front alignment after and after a field repair") {
    const char* cars[] = {"BLAZER", "CIVIC", "BRONCO", "JESKO"};
    const std::pair<HitKind, double> hits[] = {{CURB, 20}, {CURB, 35}, {CURB, 50}, {ROCK, 20}, {ROCK, 35}, {ROCK, 50}, {CORNER, 15}, {CORNER, 30},
                                               {CORNER, 45}, {CORNER, 65}, {DROP, 1.0}, {DROP, 2.0}, {DROP, 3.5}, {FIELD, 30}, {FIELD, 45}};
    for (const char* cn : cars) {
        if (std::getenv("WD_CAR") && !std::strstr(cn, std::getenv("WD_CAR"))) continue;
        for (const auto& h : hits) {
            if (std::getenv("WD_HIT") && !std::strstr(kHitName[h.first], std::getenv("WD_HIT"))) continue;
            const Outcome o = runHit(byName(cn), h.first, h.second);
            std::printf("WHEEL %-7s %-11s %4.1f | hit: camber %+6.2f toe %+6.2f hub %5.1f cm | other: camber %+6.2f toe %+6.2f | pickups %.1f cm knuckle %.1f mm rails %.1f cm"
                        " | repair%s -> camber %+6.2f toe %+6.2f / %+6.2f %+6.2f knuckle %.1f mm | dmg %.1f%% | sets%s || other%s\n",
                        cn, kHitName[h.first], h.second, o.hit.camber, o.hit.toe, o.hit.hubMove, o.other.camber, o.other.toe, o.pickups, o.knuckle, o.railFlow, o.offered ? "" : "(none offered)",
                        o.hitFixed.camber, o.hitFixed.toe, o.otherFixed.camber, o.otherFixed.toe, o.knuckleFixed, o.damage * 100, o.sets.c_str(), o.otherSets.c_str());
            std::fflush(stdout);
        }
    }
}

// Ordinary abuse (a 30 cm rock at 35 km/h, a curb at 50, a 2 m drop, a corner tap into a wall at
// 15, and 30 for the unibodies and the IFS truck) bends at most what a field repair swaps: after
// it the front wheels point straight again, and nothing on the frame or the knuckle stays bent.
// (Before, a 30 cm rock at 35-50 km/h folded a front knuckle 6-17 % out of shape, toed the
// wheel 5-7 degrees, or flowed the Bronco's lower-arm bracket 3.6 cm, none of it repairable;
// and a crumpled corner that shoved the rack end left the toe out with nothing to adjust.)
#define FRONT_MILD(fn, NAME_, CAR_, WALL30_)                                                                                         \
    SCENARIO(fn, NAME_, "Front wheel after ordinary hits (" CAR_ "): field-repairable, and straight again after the repair") {        \
        std::vector<std::pair<HitKind, double>> hits = {{ROCK, 35}, {CURB, 50}, {DROP, 2.0}, {CORNER, 15}};                         \
        if (WALL30_) hits.push_back({CORNER, 30});                                                                                 \
        for (const auto& h : hits) {                                                                                               \
            const Outcome o = runHit(byName(CAR_), h.first, h.second);                                                             \
            const std::string tag = std::string(kHitName[h.first]) + " " + std::to_string((int)h.second) + ": ";                  \
            ctx.metric(tag + "worst camber or toe after the repair", worstAlign(o.hitFixed, o.otherFixed), "deg", 0, 0.6);         \
            ctx.metric(tag + "knuckle out of shape after the repair", o.knuckleFixed, "mm", -2, 2);                                \
            ctx.metric(tag + "pick-ups moved", o.pickups, "cm", 0, 1.5);                                                           \
            ctx.metric(tag + "frame rail flowed at the corner (a bracket scraped, not the pick-ups)", o.railFlow, "cm", 0, 2.5);   \
            if (std::getenv("WD_VERBOSE")) std::printf("%s%s before repair %+.2f %+.2f / %+.2f %+.2f, after %+.2f %+.2f / %+.2f %+.2f, rails %.2f cm, sets%s\n", CAR_, tag.c_str(), o.hit.camber, o.hit.toe, \
                        o.other.camber, o.other.toe, o.hitFixed.camber, o.hitFixed.toe, o.otherFixed.camber, o.otherFixed.toe, o.railFlow, o.sets.c_str());             \
        }                                                                                                                          \
    }
FRONT_MILD(frontMildBlazer, "damage/front-wheel-mild/blazer", "BLAZER", false)
FRONT_MILD(frontMildCivic, "damage/front-wheel-mild/civic", "CIVIC", true)
FRONT_MILD(frontMildBronco, "damage/front-wheel-mild/bronco", "BRONCO", true)
FRONT_MILD(frontMildJesko, "damage/front-wheel-mild/jesko", "JESKO", true)

// A severe crash, 65 km/h with the corner into a wall: the frame or body at the pick-ups takes
// a set no trail repair can put back (it's the station's job).
SCENARIO(frontSevere, "damage/front-wheel-severe", "65 km/h corner into a wall (Bronco, Civic): the front corner's geometry stays out after a field repair (a station job)") {
    for (const char* cn : {"BRONCO", "CIVIC"}) {
        const Outcome o = runHit(byName(cn), CORNER, 65);
        ctx.metric(std::string(cn) + ": worst camber or toe after the field repair", worstAlign(o.hitFixed, o.otherFixed), "deg", 3, 90);
        ctx.metric(std::string(cn) + ": pick-ups moved", o.pickups, "cm", 1.5, 100);
    }
}
