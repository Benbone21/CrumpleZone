#include "softbody.h"
#include "vehicles.h"

#include <set>
#include <utility>

namespace {

// ---- Tires and ground -------------------------------------------------------
const double kTireK = 2.2e5, kTireC = 1500;
const double kRimK = 3.0e6;          // stiffness once the tire is squashed onto the rim
const double kRimPopSpeed = 7.0;     // rim strike faster than this (m/s) pinches the tire -> blowout
const double kStickK = 1.2e5, kStickC = 4000;  // contact-patch static friction (parking, slopes)
const double kNodeRadius = 0.04;
const double kGroundK = 4e5, kGroundC = 2000, kGroundCt = 3000, kGroundMu = 0.7;
const double kHandbrakeTorque = 3000;

// Soft ground under a hard landing (estimates). Loose sand bears ~100-150 kPa under a
// tire before it shears and flows out from under it, more the deeper the tire goes (the
// patch lengthens and the sand round it is confined); wet mud a little more, fresh snow
// less but it packs up fast. Past that the ground gives instead of the tire.
double softPressure(Surface s, double depth) {
    const double q0 = s == SURF_MUD ? 150e3 : s == SURF_SNOW ? 90e3 : 120e3;   // Pa at the surface
    const double q1 = s == SURF_MUD ? 1.2e6 : s == SURF_SNOW ? 1.6e6 : 1.0e6;  // Pa per m deeper
    return q0 + q1 * depth;
}
double softBearing(Surface s, double width, double R, double sink) {
    const double patch = 2 * std::sqrt(2 * R * (sink + 0.03));  // the chord of the tire down in it
    return softPressure(s, sink) * width * patch;
}
// How deep a landing can push a tire into it before what's underneath is packed hard.
double softCrushMax(Surface s) { return s == SURF_SAND ? 0.22 : s == SURF_MUD ? 0.2 : 0.16; }

// Normalized tire friction vs combined slip. `bite` sets how fast it builds (a stiff
// tire on pavement peaks at ~0.15 slip; on loose ground the surface itself shears, so it
// builds slowly), `keep` how much is left sliding well past the peak.
double tireCurve(double s, double bite, double keep) {
    const double C = 2 - 2 * std::asin(clampd(keep, 0.0, 1.0)) / M_PI;  // sin(C * pi/2) = keep
    return std::sin(C * std::atan(11.0 * bite * s));
}

// Each tread on each surface, against its grip on dry pavement (PAVED, GRASS, DIRT, ROCK,
// SCREE, GRAVEL, SAND, MUD, SNOW, ICE, WET). Semi-slicks have no voids for water, mud or
// snow to go; knobbly tires bite into loose stuff; big mud lugs clear mud but have less
// rubber on ice and wet pavement.
const double kTreadGrip[4][SURF_COUNT] = {
    {1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 0.95, 0.85, 0.90, 1.00, 1.00},  // street
    {1.00, 0.75, 0.80, 0.95, 0.80, 0.80, 0.70, 0.50, 0.55, 0.80, 0.72},  // semi-slick
    {1.00, 1.08, 1.08, 1.00, 1.08, 1.08, 1.10, 1.20, 1.15, 1.00, 0.95},  // all-terrain
    {1.00, 1.12, 1.12, 1.00, 1.12, 1.10, 1.08, 1.45, 1.05, 0.90, 0.88},  // mud-terrain
};
// How stiff the tread is (how quickly grip builds with slip): a semi-slick's shallow
// tread barely squirms, tall lugs do.
const double kTreadBite[4] = {1.0, 1.4, 1.0, 0.85};

const V3 kPalette[] = {
    {0.80, 0.16, 0.10}, {0.14, 0.34, 0.78}, {0.90, 0.72, 0.16}, {0.88, 0.88, 0.86}, {0.16, 0.55, 0.30},
    {0.10, 0.10, 0.12}, {0.95, 0.45, 0.10}, {0.55, 0.20, 0.60}, {0.40, 0.70, 0.85}, {0.60, 0.60, 0.62},
};

}  // namespace

// =============================================================================
// TriGrid

int TriGrid::key(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
    return (int)(h & (kBuckets - 1));
}

void TriGrid::clear() {
    for (int b : used) bucket[b].clear();
    used.clear();
}

void TriGrid::insert(int tri, const V3& mn, const V3& mx) {
    int x0 = (int)std::floor(mn.x / kCell), x1 = (int)std::floor(mx.x / kCell);
    int y0 = (int)std::floor(mn.y / kCell), y1 = (int)std::floor(mx.y / kCell);
    int z0 = (int)std::floor(mn.z / kCell), z1 = (int)std::floor(mx.z / kCell);
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
            for (int z = z0; z <= z1; z++) {
                int b = key(x, y, z);
                if (bucket[b].empty()) used.push_back(b);
                bucket[b].push_back(tri);
            }
}

const std::vector<int>& TriGrid::query(const V3& p) const {
    return bucket[key((int)std::floor(p.x / kCell), (int)std::floor(p.y / kCell), (int)std::floor(p.z / kCell))];
}

V3 Car::groupAvg(const std::vector<int>& g) const {
    V3 s;
    for (int i : g) s += nodes[i].p;
    return s / (double)g.size();
}

Frame Car::frame() const {
    Frame f;
    double M = 0;
    for (int i = 0; i < bodyNodes; i++) {
        f.com += nodes[i].p * nodes[i].m;
        f.vel += nodes[i].v * nodes[i].m;
        M += nodes[i].m;
    }
    f.com = f.com / M;
    f.vel = f.vel / M;
    if (!chassis.nodes.empty()) {
        // The chassis frame's best-fit rotation: the body's true attitude, whatever its shape.
        f.fwd = chassis.rot.rotate(V3(1, 0, 0));
        f.up = chassis.rot.rotate(V3(0, 1, 0));
        f.left = cross(f.up, f.fwd);
        return f;
    }
    f.fwd = norm(groupAvg(grpFront) - groupAvg(grpRear));
    V3 l = norm(groupAvg(grpLeft) - groupAvg(grpRight));
    f.up = norm(cross(f.fwd, l));
    f.left = cross(f.up, f.fwd);
    return f;
}

V3 Car::engineBay() const { return groupAvg(engineNodes); }

double Car::engineTorque(double r) const {
    const auto& pts = spec->torque;
    if (r <= pts.front().rpm) return pts.front().nm;
    for (size_t i = 1; i < pts.size(); i++)
        if (r < pts[i].rpm) {
            double t = (r - pts[i - 1].rpm) / (pts[i].rpm - pts[i - 1].rpm);
            return pts[i - 1].nm + t * (pts[i].nm - pts[i - 1].nm);
        }
    return pts.back().nm;
}

double Car::gearRatio(int g) const { return g < 0 ? -spec->reverseRatio : spec->gears[g - 1]; }

bool Car::shift(int dir) {
    if (engineDead || shiftTimer > 0) return false;
    const int ng = (int)spec->gears.size();
    if (dir > 0) {
        if (gear < 1) {  // out of reverse
            gear = 1;
            shiftTimer = spec->shiftTime;
            return true;
        }
        if (gear >= ng) return false;
        gear++;
        shiftTimer = spec->shiftTime;
        return true;
    }
    if (gear <= 1) return false;
    // Refuse a downshift that would throw the engine past its redline.
    if (rpm * spec->gears[gear - 2] / spec->gears[gear - 1] > spec->redline + 200) return false;
    gear--;
    shiftTimer = spec->shiftTime * 0.7;
    return true;
}

// =============================================================================
// Physics

void Car::computeForces(double h, const World& w) {
    const V3 g(0, -9.81, 0);
    const double dragPerKg = 0.5 * 1.2 * spec->dragArea / mass;  // 0.5 * rho * Cd * A, spread over the nodes
    for (Node& n : nodes) n.f = g * n.m - n.v * (len(n.v) * dragPerKg * n.m);
    // Water: what's under the surface is held up a little and dragged back hard.
    if (w.hasWater()) {
        const double level = w.waterLevel(nodes[0].p.x, nodes[0].p.z);
        if (level > -1e8) {
            // (A river's current pushes too: the drag is against the moving water.)
            const V3 flow = w.waterFlow(nodes[0].p.x, nodes[0].p.z);
            for (Node& n : nodes) {
                double depth = level - n.p.y;
                if (depth <= 0) continue;
                double sub = std::min(1.0, depth / 0.3);
                const V3 rel = n.v - flow;
                // The body's sealed cabin floats it once the water's halfway up (that's what
                // lets a current carry a car off); the wheels and tires much less so.
                const double lift = &n - nodes.data() < bodyNodes ? 0.85 : 0.4;
                n.f += V3(0, 9.81 * n.m * lift * sub, 0) - rel * (n.m * sub * (1.0 + 0.6 * len(rel)));
            }
            // The air intake: atop the engine, or up at the roof with a snorkel. Water
            // in it hydrolocks the engine.
            if (!engineDead && !engineNodes.empty() && rpm > 100) {
                double intake = -1e9;
                if (spec->extras & EXTRA_SNORKEL)
                    for (int i = 0; i < bodyNodes; i++) intake = std::max(intake, nodes[i].p.y - 0.15);
                else
                    for (int i : engineNodes) intake = std::max(intake, nodes[i].p.y);
                if (intake < level) {
                    engineDead = hydrolocked = true;
                    engineHealth = std::min(engineHealth, 0.1);
                }
            }
        }
    }
    if (!aeroShare.empty()) {
        // Downforce: grows with the square of the airspeed along the car, pushing down
        // through the body (split between the axles by the aero balance).
        const V3 up = chassis.rot.rotate(V3(0, 1, 0)), fwd = chassis.rot.rotate(V3(1, 0, 0));
        V3 mv;
        double M = 0;
        for (int i = 0; i < bodyNodes; i++) {
            mv += nodes[i].v * nodes[i].m;
            M += nodes[i].m;
        }
        double vf = dot(mv / M, fwd), F = spec->downforce * vf * vf;
        aeroSpeed = vf;
        if (spec->downforceMax > 0) F = std::min(F, spec->downforceMax);
        if (F > 1)
            for (int i = 0; i < bodyNodes; i++) nodes[i].f -= up * (F * aeroShare[i]);
    }
    if (extForce.size() == nodes.size())
        for (size_t i = 0; i < nodes.size(); i++) nodes[i].f += extForce[i];
    chassisForces(h);

    // Steering rack: each tie rod's length is set so its knuckle turns to the
    // steer angle (inner wheel a little more: Ackermann). The rack travels at a real
    // rack's speed (~3 rad/s at the road wheels, lock to lock in ~0.4 s): told to jump,
    // it would yank the tie rods past their yield and bend them.
    rackSteer += clampd(steer - rackSteer, -3.0 * h, 3.0 * h);
    for (Wheel& wh : wheels) {
        if (!wh.front || wh.tieRod < 0 || beams[wh.tieRod].broken) continue;
        double d = rackSteer;
        if (std::fabs(d) > 1e-4 && wheelbase > 0.5) {
            double track = std::fabs(restLocal[wh.outer].z + restLocal[wh.inner].z);  // 2 * |zc|
            bool inner = (d > 0) == wh.left;  // turning left: the left wheel is on the inside
            double cotC = 1.0 / std::tan(std::fabs(d)), dt = 0.5 * track / wheelbase * 0.7;
            d = (d > 0 ? 1 : -1) * std::atan(1.0 / (cotC + (inner ? -dt : dt)));
        }
        V3 k = norm(wh.kpUp - wh.kpLow);
        V3 armNow = wh.kpLow + rotateAxis(wh.armRest - wh.kpLow, k, d);
        Beam& tr = beams[wh.tieRod];
        tr.L0 = tr.Lorig = len(wh.rackRest - armNow) + wh.tieOffset;
    }

    // Heave springs: push both coil-overs apart once they've squeezed together past the gap.
    for (Axle& ax : axles) {
        if (ax.heaveK <= 0 || ax.wheel[0] < 0 || ax.wheel[1] < 0) continue;
        const Wheel& l = wheels[ax.wheel[0]];
        const Wheel& r = wheels[ax.wheel[1]];
        const Beam& sl = beams[l.spring];
        const Beam& sr = beams[r.spring];
        if (l.detached || r.detached || sl.broken || sr.broken) continue;
        const double squeeze = -0.5 * (len(nodes[sl.b].p - nodes[sl.a].p) - l.springRest + len(nodes[sr.b].p - nodes[sr.a].p) - r.springRest);
        if (squeeze <= ax.heaveGap) continue;
        // (Its damper too: an undamped stiff spring would set the car bouncing on the downforce.)
        double rate = 0;
        for (const Beam* sp : {&sl, &sr}) rate -= 0.5 * dot(nodes[sp->b].v - nodes[sp->a].v, norm(nodes[sp->b].p - nodes[sp->a].p));
        // It's set up to carry downforce: comes in from ~90 km/h and is all there by ~160, so
        // at low speed (kerbs, drops, parking) the car rides on its corner springs alone.
        const double aero = smoothstep(25, 45, std::fabs(aeroSpeed));
        if (aero <= 0) continue;
        const double F = aero * std::max(0.0, ax.heaveK * (squeeze - ax.heaveGap) + ax.heaveC * rate);
        for (const Beam* sp : {&sl, &sr}) {
            const V3 dir = norm(nodes[sp->b].p - nodes[sp->a].p) * F;
            nodes[sp->a].f -= dir;
            nodes[sp->b].f += dir;
        }
    }

    for (Axle& ax : axles) {
        if (ax.arbK <= 0 || ax.wheel[0] < 0 || (ax.front && swayDisconnected)) continue;
        const Wheel& l = wheels[ax.wheel[0]];
        const Wheel& r = wheels[ax.wheel[1]];
        const Beam& sl = beams[l.spring];
        const Beam& sr = beams[r.spring];
        if (l.detached || r.detached || sl.broken || sr.broken) continue;
        double dl = len(nodes[sl.b].p - nodes[sl.a].p) - l.springRest, dr = len(nodes[sr.b].p - nodes[sr.a].p) - r.springRest;
        // Twisted past its elastic limit, the bar takes a permanent set.
        double twist = dl - dr - ax.arbBend;
        if (std::fabs(twist) > ax.arbYield) {
            const double over = twist - (twist > 0 ? ax.arbYield : -ax.arbYield);
            ax.arbBend += over;
            twist -= over;
            impactEnergy += ax.arbK * ax.arbYield * std::fabs(over);
        }
        double arbForce[2] = {ax.arbK * twist, -ax.arbK * twist};
        for (int s2 = 0; s2 < 2; s2++) {
            const Beam& sp = s2 ? sr : sl;
            V3 dir = norm(nodes[sp.b].p - nodes[sp.a].p) * arbForce[s2];
            nodes[sp.a].f += dir;
            nodes[sp.b].f -= dir;
        }
    }

    for (Beam& b : beams) {
        if (b.broken) continue;
        Node& A = nodes[b.a];
        Node& B = nodes[b.b];
        V3 d = B.p - A.p;
        double L = len(d);
        if (L < 1e-9) continue;
        if (b.type == BEAM_SHAFT) {  // slides and flexes freely, but tears off when pulled apart
            if (L > b.Lorig * (1 + b.breakStrain) || L < b.Lorig * 0.55) b.broken = true;
            continue;
        }
        V3 dir = d / L;
        double F;
        double vrel = dot(B.v - A.v, dir);
        if (b.type == BEAM_BUMPSTOP) {
            // Jounce bumper: nothing until the axle rises onto it, then the same progressive
            // rubber as a coil-over's stop, damped only while it's being squashed. Past ~4 cm
            // the rubber is solid and the tube is on the frame: steel on steel.
            if (L >= b.minL + 0.01) continue;
            double c = std::min(b.minL + 0.01 - L, 0.03);
            F = -(25 * b.k * c * c / 0.03 + 25 * b.k * std::max(0.0, b.minL - 0.02 - L) + 100 * b.k * std::max(0.0, b.minL - 0.04 - L));
            if (vrel < 0) F += b.c * vrel;
        } else if (b.type == BEAM_SPRING) {
            F = b.k * (L - b.L0);
            // Progressive rubber bump stop: firms up over its first 3 cm.
            if (L < b.minL + 0.01) {
                double c = std::min(b.minL + 0.01 - L, 0.03);
                F -= 25 * b.k * c * c / 0.03 + 25 * b.k * std::max(0.0, b.minL - 0.02 - L);
            } else if (L > b.maxL) {
                F += 25 * b.k * (L - b.maxL);
            }
            // Dampers resist rebound (extension) harder than bump. In bump the valves blow
            // off above ~0.25 m/s, so a sharp hit doesn't turn into a huge kick; rebound
            // stays firm so the car doesn't spring itself into the air.
            if (vrel > 0) {
                F += b.c * 1.35 * vrel;
            } else {
                double sv = -vrel, knee = 0.25;
                F -= b.c * 0.65 * (sv < knee ? sv : knee + 0.3 * (sv - knee));
            }
            // A leaf pack's leaves rub on each other as it flexes: friction damping, strongest
            // on small, quick movements (washboard), as real leaf springs ride.
            if (b.friction > 0) F += b.friction * std::tanh(vrel / 0.06);  // (smoothed: an explicit step can't hold a true stick)
        } else {
            // Work hardening: bent metal gets harder to bend further (folds stack up).
            double plastic = std::fabs(b.L0 - b.Lorig) / b.Lorig;
            double yieldNow = b.yieldStrain * (1 + b.hardening * plastic);
            double strain = (L - b.L0) / b.L0;
            // A rubber mount has a snubber both ways (the rebound washer on its through-bolt):
            // squashed flat or pulled up to the washer it's a hard stop. Well past that, pulled
            // the rubber tears, pushed the bracket bends: either way it sets (and stretched to
            // breakStrain, lets go).
            const bool mount = b.snub;
            const double snub = mount ? yieldNow : 0;
            if (mount) yieldNow *= 2.5;
            if (mount && std::fabs(strain) > snub && std::fabs(strain) <= yieldNow) {
                // on the rebound washer or the snubber: no set
            } else if (strain > yieldNow) {  // stretched past yield: permanent elongation
                double nl = L / (1 + yieldNow);
                impactEnergy += b.k * yieldNow * b.L0 * (nl - b.L0);
                b.L0 = nl;
                b.relaxTime = 0.25f;
                releaseFromFrame(b);
            } else if (strain < -yieldNow) {  // crushed past yield: permanent shortening
                double nl = std::max(L / (1 - yieldNow), 0.25 * b.Lorig);
                impactEnergy += b.k * yieldNow * b.L0 * std::fabs(b.L0 - nl);
                b.L0 = nl;
                b.relaxTime = 0.25f;
                releaseFromFrame(b);
            } else if (b.relaxTime > 0) {
                // Viscoplastic relaxation: real crushed metal hardly springs back, but this
                // lattice is far softer than steel and would store (and return) a lot of
                // elastic energy. Let freshly yielded beams creep toward their current length.
                b.relaxTime -= (float)h;
                b.L0 += (L - b.L0) * std::min(1.0, 90.0 * h);
            }
            if (L > b.Lorig * (1 + b.breakStrain)) {
                b.broken = true;
                impactEnergy += 200;
                continue;
            }
            F = b.k * (L - b.L0);
            if (mount && strain < -snub) F -= 6 * b.k * (std::min(-strain, yieldNow) - snub) * b.L0;  // on the snubber
            if (mount && strain > snub) F += 6 * b.k * (std::min(strain, yieldNow) - snub) * b.L0;  // on the washer
            // Fully crushed material is nearly solid. One-way (only resists further
            // crushing) so it soaks up energy instead of springing the car back.
            if (L < 0.45 * b.Lorig && vrel < 0) F -= 3 * b.k * (0.45 * b.Lorig - L);
            F += b.c * vrel;
        }
        V3 fv = dir * F;
        A.f += fv;
        B.f -= fv;
    }
    // Panels on their pins.
    for (Pin& pn : pins) {
        if (pn.broken) continue;
        Node& A = nodes[pn.body];
        Node& B = nodes[pn.panel];
        V3 d = B.p - A.p;
        if (dot(d, d) > pn.breakDist * pn.breakDist) {
            pn.broken = true;
            continue;
        }
        if (pn.slack > 0) {  // a stop: nothing until it's pulled tight
            const double L = len(d);
            if (L <= pn.slack) continue;
            const V3 u = d / L;
            const double F = pn.k * (L - pn.slack) + std::max(0.0, dot(B.v - A.v, u)) * pn.c;
            A.f += u * F;
            B.f -= u * F;
            continue;
        }
        V3 F = d * pn.k + (B.v - A.v) * pn.c;
        A.f += F;
        B.f -= F;
    }

    // A tie rod that got bent stays bent: the wheel now points a little off.
    for (Wheel& wh : wheels) {
        if (!wh.front || wh.tieRod < 0) continue;
        Beam& tr = beams[wh.tieRod];
        if (tr.L0 != tr.Lorig) {
            wh.tieOffset += tr.L0 - tr.Lorig;
            tr.Lorig = tr.L0;
        }
    }

    engineBlockContact(h);
    panelBodyContact(h);
    wheelBodyContact(h);
    wheelStep(h, w);
    poleEdgeContacts(h, w);

    for (Node& n : nodes) {
        n.scrape = 0;
        if (!n.collide) continue;
        Contact ct;
        if (!w.contact(n.p, kNodeRadius, nullptr, 0, ct)) continue;
        // Damping terms are capped by node mass so light props stay stable.
        double vn = dot(n.v, ct.n);
        double Fn;
        const double layer = surfaceInfo(ct.surface).sink * 3.0;
        if (layer > 0) {
            // Sand, mud and snow: a crush layer. The body ploughs into it on a soft spring
            // (6% of hard ground's: it still holds a resting body within a few cm) until
            // it's packed hard ~25-40 cm down, and it doesn't push back as the body lifts out:
            // a landing or a roll is cushioned, not bounced.
            Fn = kGroundK * (0.06 * ct.pen + std::max(0.0, ct.pen - layer)) - std::min(600.0, 0.5 * n.m / h) * vn;
            Fn *= 1 - 0.7 * smoothstep(0.05, 0.5, vn);
        } else {
            Fn = kGroundK * ct.pen - std::min(kGroundC, 0.5 * n.m / h) * vn;
        }
        if (Fn <= 0) continue;
        V3 vt = n.v - ct.n * vn;
        double vtl = len(vt);
        V3 F = ct.n * Fn;
        if (vtl > 1e-6) F -= vt * (std::min(kGroundMu * Fn, std::min(kGroundCt, 0.8 * n.m / h) * vtl) / vtl);
        n.f += F;
        n.scrape = (float)vtl;
    }
}

void Car::releaseFromFrame(const Beam& b) {
    for (ChassisFrame* cf : {&chassis, &railFrame[0], &railFrame[1]}) {
        if (cf->slot.empty() || cf->yieldDist > 0) continue;  // (steel flows instead)
        for (int nid : {b.a, b.b}) {
            int k = cf->slot[nid];
            if (k >= 0) cf->release[k] = 0.15f;
        }
    }
}

void Car::chassisForces(double h) {
    chassisGroupForces(chassis, h);
    for (ChassisFrame& rf : railFrame) chassisGroupForces(rf, h);
}

void Car::chassisGroupForces(ChassisFrame& cf, double h) {
    if (cf.nodes.empty() || cf.kappa <= 0) return;
    const int n = (int)cf.nodes.size();
    // Nodes in a fresh dent are left to the beams; everything else holds its shape.
    auto active = [&](int k) { return cf.attached[k] && cf.release[k] <= 0; };

    // Center of mass and momentum of the structure. (The shape is fitted by mass alone: a
    // member held harder, a bracket on a rail, follows the rail's shape rather than leading it.)
    const bool weighted = !cf.weight.empty();
    auto held = [&](int k) { return nodes[cf.nodes[k]].m; };
    // (c: the fit's center; cm: the true center of mass, which the rigid motion is about.)
    V3 c, vc, rc, cm;
    double M = 0, Mv = 0;
    for (int k = 0; k < n; k++) {
        if (!active(k)) continue;
        const Node& nd = nodes[cf.nodes[k]];
        const double w = held(k);
        c += nd.p * w;
        cm += nd.p * nd.m;
        vc += nd.v * nd.m;
        rc += cf.rest[k] * w;
        M += w;
        Mv += nd.m;
    }
    if (M <= 0) return;
    c = c / M;
    cm = cm / Mv;
    vc = vc / Mv;
    rc = rc / M;  // rest shape's center for the active set (0 unless nodes are released)
    // Best-fit rotation: A = sum m (x - c)(r - rc)^T; iterate R toward the rotational part of A.
    M3 A;
    V3 L;
    M3 I;
    for (int k = 0; k < n; k++) {
        if (!active(k)) continue;
        const Node& nd = nodes[cf.nodes[k]];
        V3 d = nd.p - c;
        V3 r = cf.rest[k] - rc;
        const double w = held(k);
        A.c[0] += d * (w * r.x);
        A.c[1] += d * (w * r.y);
        A.c[2] += d * (w * r.z);
        const V3 e = nd.p - cm;
        L += cross(e, nd.v - vc) * nd.m;
        double dd = dot(e, e);
        I.c[0] += V3(dd - e.x * e.x, -e.y * e.x, -e.z * e.x) * nd.m;
        I.c[1] += V3(-e.x * e.y, dd - e.y * e.y, -e.z * e.y) * nd.m;
        I.c[2] += V3(-e.x * e.z, -e.y * e.z, dd - e.z * e.z) * nd.m;
    }
    for (int it = 0; it < 4; it++) {
        V3 R0 = cf.rot.rotate(V3(1, 0, 0)), R1 = cf.rot.rotate(V3(0, 1, 0)), R2 = cf.rot.rotate(V3(0, 0, 1));
        V3 om = (cross(R0, A.c[0]) + cross(R1, A.c[1]) + cross(R2, A.c[2])) /
                (std::fabs(dot(R0, A.c[0]) + dot(R1, A.c[1]) + dot(R2, A.c[2])) + 1e-9);
        double w = len(om);
        if (w < 1e-12) break;
        cf.rot = Quat::axisAngle(om / w, w) * cf.rot;
        cf.rot.normalize();
    }
    V3 Om = inverseSym(I) * L;
    const double cap = cf.maxPull;
    // Forces, capped per node. Capping (only ever in a crash) breaks the balance, so the
    // net force and torque left over are taken back out evenly: the frame can never push
    // the car as a whole.
    static thread_local std::vector<V3> F;
    static thread_local std::vector<int> flowed;
    F.assign(n, V3());
    flowed.clear();
    // Steel: past its yield it bends, and the bend is the new shape. Each cross-section
    // bends as a whole: where its mean is pushed past the yield, all its members' rest
    // positions follow by the excess. A member shoved much further than the rest of its
    // section crushes it locally, and flows on its own.
    if (cf.yieldDist > 0) {
        static thread_local std::vector<V3> D, secD;
        static thread_local std::vector<double> secW;
        D.assign(n, V3());
        const bool sections = !cf.section.empty();
        const int nsec = sections ? 1 + *std::max_element(cf.section.begin(), cf.section.end()) : 0;
        secD.assign(nsec, V3());
        secW.assign(nsec, 0);
        for (int k = 0; k < n; k++) {
            if (!active(k)) continue;
            D[k] = c + cf.rot.rotate(cf.rest[k] - rc) - nodes[cf.nodes[k]].p;
            if (sections) secD[cf.section[k]] += D[k] * nodes[cf.nodes[k]].m, secW[cf.section[k]] += nodes[cf.nodes[k]].m;
        }
        for (int sct = 0; sct < nsec; sct++) {
            if (secW[sct] <= 0) continue;
            const V3 dm = secD[sct] / secW[sct];
            const double dl = len(dm);
            secD[sct] = dl > cf.yieldDist ? dm * (1 - cf.yieldDist / dl) : V3();
        }
        const double crush = sections ? 3 * cf.yieldDist : cf.yieldDist;
        for (int k = 0; k < n; k++) {
            if (!active(k)) continue;
            V3 back = sections ? secD[cf.section[k]] : V3();
            const V3 rem = D[k] - back;
            const double rl = len(rem);
            if (rl > crush) back += rem * (1 - crush / rl);
            if (dot(back, back) <= 0) continue;
            cf.rest[k] += cf.rot.unrotate(-back);
            const double wk = weighted ? cf.weight[k] : 1.0;
            impactEnergy += cf.kappa * wk * nodes[cf.nodes[k]].m * cf.yieldDist * len(back);
            flowed.push_back(k);
        }
    }
    V3 sumF, sumT;
    bool capped = false;
    for (int k = 0; k < n; k++) {
        if (!cf.attached[k]) continue;
        Node& nd = nodes[cf.nodes[k]];
        if (cf.release[k] > 0) {
            // Follow the dent while it's still moving; once it has been still (relative
            // to the body) for a moment, take that shape as the new rest shape.
            cf.rest[k] = rc + cf.rot.unrotate(nd.p - c);
            V3 rel = nd.v - (vc + cross(Om, nd.p - cm));
            if (dot(rel, rel) < 0.1 * 0.1) cf.release[k] -= h;
            continue;
        }
        V3 d = c + cf.rot.rotate(cf.rest[k] - rc) - nd.p;
        const double wk = weighted ? cf.weight[k] : 1.0;
        double nodeCap = cap;
        if (cf.yieldDist > 0) {
            // (Flowed above; the pull stays at about the yield force however fast it's bent.)
            nodeCap = cf.kappa * wk * nd.m * cf.yieldDist * (cf.section.empty() ? 1.25 : 3.0);
        } else if (dot(d, d) > cf.releaseDist * cf.releaseDist) {  // shoved far out of shape: a dent in progress
            cf.release[k] = 0.15f;
            continue;
        }
        V3 vr = vc + cross(Om, nd.p - cm);
        V3 f = (d * (cf.kappa * wk) + (vr - nd.v) * cf.damping) * nd.m;
        double fl = len(f);
        if (fl > nodeCap) {
            f *= nodeCap / fl;
            capped = true;
        }
        F[k] = f;
        sumF += f;
        sumT += cross(nd.p - cm, f);
    }
    // (Members held harder than their mass leave a net pull too: taken out the same way.)
    capped |= weighted;
    V3 alpha;
    if (capped) alpha = inverseSym(I) * sumT;
    for (int k = 0; k < n; k++) {
        if (!active(k)) continue;
        Node& nd = nodes[cf.nodes[k]];
        V3 f = F[k];
        if (capped) f -= (sumF / Mv + cross(alpha, nd.p - cm)) * nd.m;
        nd.f += f;
    }
    // The steel between nodes that bent takes the same set (so it holds the new shape, and
    // counts as damage).
    if (!flowed.empty()) {
        if (cf.beamsAt.empty()) {
            cf.beamsAt.assign(n, {});
            for (size_t i = 0; i < beams.size(); i++) {
                const Beam& b = beams[i];
                if (b.type != BEAM_FRAME) continue;
                const int ka = cf.slot[b.a], kb = cf.slot[b.b];
                if (ka < 0 || kb < 0) continue;
                cf.beamsAt[ka].push_back((int)i);
                cf.beamsAt[kb].push_back((int)i);
            }
        }
        for (int k : flowed)
            for (int bi : cf.beamsAt[k]) {
                Beam& b = beams[bi];
                if (!b.broken) b.L0 = len(cf.rest[cf.slot[b.a]] - cf.rest[cf.slot[b.b]]);
            }
    }
}

// The engine block is a solid lump: body nodes around it are kept outside it, so a
// crumpling nose piles up against the engine and pushes it back into the firewall.
void Car::engineBlockContact(double h) {
    if (engineNodes.size() < 12 || blockContacts.empty()) return;
    V3 c, vb;
    for (int b = 0; b < 8; b++) {
        c += nodes[engineNodes[b]].p;
        vb += nodes[engineNodes[b]].v;
    }
    c = c / 8.0;
    vb = vb / 8.0;
    V3 ax[3];
    double half[3];
    for (int a = 0; a < 3; a++) {
        V3 hi, lo;
        for (int b = 0; b < 8; b++) ((b >> a) & 1 ? hi : lo) += nodes[engineNodes[b]].p;
        V3 d = (hi - lo) / 4.0;
        half[a] = len(d) / 2;
        ax[a] = d / std::max(1e-9, 2 * half[a]);
    }
    const double gb = spec->gearboxLen;
    for (const auto& bc : blockContacts) {
        Node& n = nodes[bc.first];
        V3 d = n.p - c;
        double e[3] = {dot(d, ax[0]), dot(d, ax[1]), dot(d, ax[2])};
        double lo[3] = {-(half[0] + gb), -half[1], -half[2]}, hi[3] = {half[0], half[1], half[2]};
        bool inside = true;
        for (int a = 0; a < 3 && inside; a++) inside = e[a] > lo[a] && e[a] < hi[a];
        if (!inside) continue;
        int a = bc.second / 2;
        bool pos = bc.second & 1;
        double depth = pos ? hi[a] - e[a] : e[a] - lo[a];
        V3 nrm = ax[a] * (pos ? 1.0 : -1.0);
        double vn = dot(n.v - vb, nrm);
        double F = 2e5 * depth - std::min(1500.0, 0.4 * n.m / h) * vn;
        if (F <= 0) continue;
        F = std::min(F, n.m * 3000.0);
        n.f += nrm * F;
        for (int b = 0; b < 8; b++) nodes[engineNodes[b]].f -= nrm * (F / 8);
    }
}

// A panel off its clips (a door unlatched, a hood popped, a bumper hanging by one corner)
// swings out from the body, not in through it: a loose node that has gone behind every
// body quad it sat on (inside the shell) is pushed back out the nearest way, and the body
// takes the push (a door slammed past its pillar dents the pillar). Skin quads are wound
// outward. (At a convex fold a node is only inside once it's behind both faces, so a
// bumper sliding down past its own top face isn't caught on it.)
void Car::panelBodyContact(double h) {
    constexpr double kTol = 0.004, kK = 1.5e5;
    for (Panel& pn : panels) {
        if (pn.nodeQuads.size() != pn.panelNodes.size()) continue;
        pn.outside.resize(pn.panelNodes.size(), 1);
        for (size_t k = 0; k < pn.panelNodes.size(); k++) {
            if (!pins[pn.pins[k]].broken) continue;  // on its pin: the pin holds it
            Node& P = nodes[pn.panelNodes[k]];
            double best = -1e9;  // least deep of its quads (the way out)
            int bq = -1;
            double bs = 0, bt = 0;
            V3 bn;
            bool inside = !pn.nodeQuads[k].empty(), near = false;
            for (int qi : pn.nodeQuads[k]) {
                const SkinQuad& q = quads[qi];
                const V3 &b0 = nodes[q.v[0]].p, &b1 = nodes[q.v[1]].p, &b2 = nodes[q.v[2]].p, &b3 = nodes[q.v[3]].p;
                const V3 e1 = ((b1 - b0) + (b2 - b3)) * 0.5, e2 = ((b3 - b0) + (b2 - b1)) * 0.5;
                V3 n = cross(e1, e2);
                const double nl = len(n);
                if (nl < 1e-9) continue;
                n = n / nl;
                const V3 d = P.p - (b0 + b1 + b2 + b3) * 0.25;
                const double hgt = dot(d, n);
                if (hgt > -kTol) {
                    inside = false;
                    break;
                }
                const double s = dot(d, e1) / std::max(dot(e1, e1), 1e-9), t = dot(d, e2) / std::max(dot(e2, e2), 1e-9);
                near |= std::fabs(s) < 0.75 && std::fabs(t) < 0.75;
                if (hgt > best) best = hgt, bq = qi, bs = s, bt = t, bn = n;
            }
            if (!inside || bq < 0) {
                pn.outside[k] = 1;
                continue;
            }
            // Got in some other way (round the panel's edge, or flung through before the
            // first step caught it): let it find its own way out rather than pump it out.
            if (!near || best < -0.3) pn.outside[k] = 0;
            if (!pn.outside[k]) continue;
            const SkinQuad& q = quads[bq];
            const double u = clampd(bs + 0.5, 0, 1), v = clampd(bt + 0.5, 0, 1);
            const double wgt[4] = {(1 - u) * (1 - v), u * (1 - v), u * v, (1 - u) * v};
            V3 vb;
            for (int c = 0; c < 4; c++) vb += nodes[q.v[c]].v * wgt[c];
            const double vn = dot(P.v - vb, bn);
            double F = kK * (-best - kTol) - std::min(300.0, 0.4 * P.m / h) * vn;
            if (F <= 0) continue;
            F = std::min(F, P.m * 3000.0);
            P.f += bn * F;
            for (int c = 0; c < 4; c++) nodes[q.v[c]].f -= bn * (F * wgt[c]);
        }
    }
}

// Panel edges against poles and trunks: catches a pole that would otherwise pass
// between two nodes (the nodes themselves collide through World::contact).
void Car::poleEdgeContacts(double h, const World& w) {
    nearPoles.clear();
    w.polesNear(bbMin, bbMax, nearPoles);
    for (int pi : nearPoles) {
        const Pole& p = w.poles[pi];
        for (const auto& e : skinEdges) {
            Node& A = nodes[e.first];
            Node& B = nodes[e.second];
            if (std::min(A.p.x, B.p.x) > p.x + p.r || std::max(A.p.x, B.p.x) < p.x - p.r ||
                std::min(A.p.z, B.p.z) > p.z + p.r || std::max(A.p.z, B.p.z) < p.z - p.r)
                continue;
            double dx = B.p.x - A.p.x, dz = B.p.z - A.p.z, den = dx * dx + dz * dz;
            if (den < 1e-9) continue;
            double u = clampd(-((A.p.x - p.x) * dx + (A.p.z - p.z) * dz) / den, 0, 1);
            if (u < 0.03 || u > 0.97) continue;  // endpoints are handled as nodes
            V3 q = A.p + (B.p - A.p) * u;
            if (q.y < p.y0 || q.y > p.y1) continue;
            double hx = q.x - p.x, hz = q.z - p.z, d = std::sqrt(hx * hx + hz * hz);
            if (d >= p.r || d < 1e-9) continue;
            V3 n(hx / d, 0, hz / d);
            V3 vq = A.v * (1 - u) + B.v * u;
            double m = std::min(A.m, B.m);
            double vn = dot(vq, n);
            double Fn = kGroundK * (p.r - d) - std::min(kGroundC, 0.5 * m / h) * vn;
            if (Fn <= 0) continue;
            V3 vt = vq - n * vn;
            double vtl = len(vt);
            V3 F = n * Fn;
            if (vtl > 1e-6) F -= vt * (std::min(0.5 * Fn, std::min(kGroundCt, 0.8 * m / h) * vtl) / vtl);
            A.f += F * (1 - u);
            B.f += F * u;
        }
    }
}

const DriveMode* Car::mode() const {
    const auto& modes = driveModes(spec->transfer);
    return driveMode >= 0 && driveMode < (int)modes.size() ? &modes[driveMode] : nullptr;
}

bool Car::frontEngaged() const {
    const DriveMode* m = mode();
    return !m || m->front;
}

bool Car::centerLocked() const {
    const DriveMode* m = mode();
    return m && m->front && m->centerLock;
}

DiffType Car::diffType(bool front) const {
    if (front ? lockFront : lockRear) return DIFF_LOCKED;
    return front ? spec->diffFront : spec->diffRear;
}

void Car::setDriveMode(int m) {
    const auto& modes = driveModes(spec->transfer);
    if (m < 0 || m >= (int)modes.size()) return;
    driveMode = m;
    lowRange = modes[m].low;
    if (!modes[m].front) lockFront = false;  // nothing to lock on an undriven axle
    // Lockers wired to low range drop out when it's left.
    if (!lowRange && spec->frontLockerLowOnly) lockFront = false;
    if (!lowRange && spec->rearLockerLowOnly) lockRear = false;
}

bool Car::lockerAllowed(bool front) const {
    if (!(front ? spec->lockerFront : spec->lockerRear)) return false;
    if (front && !frontEngaged()) return false;
    if (!lowRange && (front ? spec->frontLockerLowOnly : spec->rearLockerLowOnly)) return false;
    return true;
}

void Car::setLocker(bool front, bool on) {
    if (on && !lockerAllowed(front)) return;
    (front ? lockFront : lockRear) = on;
}

bool Car::wheelPowered(const Wheel& wh) const {
    if (!wh.driven || wh.detached || wh.shaftBroken) return false;
    if (wh.front && !frontEngaged()) return false;  // 2H: the front hubs freewheel
    for (int bi : wh.shafts)
        if (bi >= 0 && beams[bi].broken) return false;
    const DiffType diff = diffType(wh.front);
    if (diff != DIFF_LOCKED) {
        // An open diff sends everything to the side that turns freely: a broken shaft
        // or a missing wheel on the other side leaves this one with nothing.
        for (const Wheel& other : wheels) {
            if (&other == &wh || other.front != wh.front) continue;
            if (other.detached || other.shaftBroken) return false;
            for (int bi : other.shafts)
                if (bi >= 0 && beams[bi].broken) return false;
        }
    }
    return true;
}

// Applies a pure torque to a set of nodes: forces F_i = alpha x d_i that sum to zero and
// whose moment is exactly `torque` (alpha = I^-1 torque, I = the set's geometric inertia).
void Car::applyTorque(const std::vector<int>& set, const V3& torque) {
    if (dot(torque, torque) < 1e-12 || set.size() < 3) return;
    V3 c;
    for (int i : set) c += nodes[i].p;
    c = c / (double)set.size();
    M3 I;
    for (int i : set) {
        V3 d = nodes[i].p - c;
        double dd = dot(d, d);
        I.c[0] += V3(dd - d.x * d.x, -d.y * d.x, -d.z * d.x);
        I.c[1] += V3(-d.x * d.y, dd - d.y * d.y, -d.z * d.y);
        I.c[2] += V3(-d.x * d.z, -d.y * d.z, dd - d.z * d.z);
    }
    double tr = I.c[0].x + I.c[1].y + I.c[2].z;
    if (tr < 1e-6) return;
    I.c[0].x += tr * 1e-3;  // keeps nearly-flat sets well conditioned
    I.c[1].y += tr * 1e-3;
    I.c[2].z += tr * 1e-3;
    V3 alpha = inverseSym(I) * torque;
    for (int i : set) nodes[i].f += cross(alpha, nodes[i].p - c);
}

// Points sampled on a skin triangle for the tire to meet (barycentric weights): the
// corners, the edge midpoints and the middle. The lattice is coarse (~0.35 m), so a
// tire's tread could slip between its corners.
static const double kRubSamples[7][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.5, 0.5, 0}, {0, 0.5, 0.5}, {0.5, 0, 0.5}, {1.0 / 3, 1.0 / 3, 1.0 / 3}};

void Car::buildWheelRub() {
    // The clearance the factory designed in: everywhere the stock tire goes through the
    // steering lock and the suspension's travel (plus a little). Skin the stock tire reaches
    // into over that is the coarse lattice cutting a corner, not bodywork: it's only met past
    // that depth. Beyond it the tire arrives only past its design travel (a torn coil-over, a
    // bent arm, a crash shoving it back) or when it's bigger than the factory's (lifted
    // trucks know this: it rubs).
    const VehicleSpec* stock = spec;
    for (const VehicleSpec& vs : vehicleCatalog())
        if (spec && std::string(vs.id) == spec->id) stock = &vs;
    // The faces it could meet: the skin's triangles, and each frame rail's four sides from
    // station to station (a tire knocked inboard, or turned with a bent arm, hits the rail).
    std::vector<std::array<int, 3>> faces;
    for (const Tri& tr : tris) faces.push_back({tr.v[0], tr.v[1], tr.v[2]});
    for (int side = 0; side < 2; side++)
        for (int st = 0; st + 1 < ladderStations; st++)
            for (auto e : {std::make_pair(0, 1), std::make_pair(2, 3), std::make_pair(0, 2), std::make_pair(1, 3)}) {
                const int a0 = ladder[st * 8 + side * 4 + e.first], b0 = ladder[st * 8 + side * 4 + e.second];
                const int a1 = ladder[(st + 1) * 8 + side * 4 + e.first], b1 = ladder[(st + 1) * 8 + side * 4 + e.second];
                faces.push_back({a0, b0, b1});
                faces.push_back({a0, b1, a1});
            }
    for (Wheel& wh : wheels) {
        wh.rubFaces.clear();
        wh.rubAllow.clear();
        if (restLocal.empty() || !stock) continue;
        const V3 c0 = (restLocal[wh.inner] + restLocal[wh.outer]) * 0.5, axis0 = norm(restLocal[wh.outer] - restLocal[wh.inner]);
        const double Rs = std::max(stock->wheelRadius, stock->wellTireRadius) + 0.03,
                     half = 0.5 * std::max(wh.front || stock->wheelWidthRear <= 0 ? stock->wheelWidth : stock->wheelWidthRear, stock->wellTireWidth) + 0.02;
        const double bump = wh.front ? (stock->bumpFront > 0 ? stock->bumpFront : 0.085) : (stock->bumpRear > 0 ? stock->bumpRear : 0.085);
        const double droop = wh.front ? (stock->droopFront > 0 ? stock->droopFront : 0.11) : (stock->droopRear > 0 ? stock->droopRear : 0.11);
        const double reach = wh.R + 0.35;
        for (const auto& f : faces) {
            bool own = false;
            for (int k = 0; k < 3; k++) own |= std::find(wh.assembly.begin(), wh.assembly.end(), f[k]) != wh.assembly.end();
            if (own) continue;
            V3 pts[7];
            bool near = false;
            for (int i = 0; i < 7; i++) {
                for (int k = 0; k < 3; k++) pts[i] += restLocal[f[k]] * kRubSamples[i][k];
                near |= len(pts[i] - c0) < reach;
            }
            if (!near) continue;
            double allow[7] = {0, 0, 0, 0, 0, 0, 0};
            for (double up : {-droop, -0.5 * droop, 0.0, 0.5 * bump, bump})
                for (double kp : {0.0, 0.08})
                    for (double st : {-0.62, -0.31, 0.0, 0.31, 0.62}) {
                        if (!wh.front && st != 0) continue;
                        // (Steered about the kingpin, up to ~8 cm inboard of the wheel's middle, so
                        // the tire swings round in an arc rather than turning on the spot.)
                        const double cs = std::cos(st), sn = std::sin(st);
                        auto turn = [&](const V3& v) { return V3(v.x * cs + v.z * sn, v.y, -v.x * sn + v.z * cs); };
                        const V3 pivot = c0 - axis0 * kp;
                        const V3 c = pivot + turn(c0 - pivot) + V3(0, up, 0);
                        const V3 ax = turn(axis0);
                        for (int i = 0; i < 7; i++) {
                            const V3 d = pts[i] - c;
                            const double da = dot(d, ax), dr = len(d - ax * da);
                            // (By the nearer way out, as the contact measures it.)
                            if (std::fabs(da) < half && dr < Rs) allow[i] = std::max(allow[i], std::min(Rs - dr, half - std::fabs(da)));
                        }
                    }
            wh.rubFaces.push_back(f);
            for (double a : allow) wh.rubAllow.push_back((float)a);
        }
    }
}

// The tire meeting its own car. Rammed up past the bump stops (a landing, a rock under
// it, a bent arm) or turned onto the wheel-well liner, it shoves the sheet metal it hits
// (the fender dents, a bumper cover folds) and the body pushes back on the hub and through
// the suspension. A tire rubbing a fender drags on its spin.
void Car::wheelBodyContact(double h) {
    (void)h;

    for (Wheel& wh : wheels) {
        wh.rubTorque = 0;
        wh.rub = 0;
        if (wh.detached || wh.rubFaces.empty()) continue;
        Node& a = nodes[wh.inner];
        Node& b = nodes[wh.outer];
        const V3 c = (a.p + b.p) * 0.5, vc = (a.v + b.v) * 0.5;
        V3 axis = norm(b.p - a.p);
        const double half = wh.width * 0.5, R = wh.popped ? wh.rimR + 0.015 : wh.R, side = R - wh.rimR;
        const V3 spinAxis = wh.left ? axis : -axis;  // points to the car's left: + omega rolls it forward
        for (size_t ri = 0; ri < wh.rubFaces.size(); ri++) {
            const auto& tv = wh.rubFaces[ri];
            // Quick reject: the triangle's middle too far from the hub to reach.
            const V3 m = (nodes[tv[0]].p + nodes[tv[1]].p + nodes[tv[2]].p) / 3.0;
            if (dot(m - c, m - c) > (R + 0.4) * (R + 0.4)) continue;
            for (int si = 0; si < 7; si++) {
                const auto& wgt = kRubSamples[si];
                const double allow = wh.rubAllow[ri * 7 + si];
                V3 p, vp;
                for (int k = 0; k < 3; k++) p += nodes[tv[k]].p * wgt[k], vp += nodes[tv[k]].v * wgt[k];
                const V3 d = p - c;
                const double da = dot(d, axis);
                if (std::fabs(da) >= half) continue;
                const V3 rv = d - axis * da;
                const double dr = len(rv);
                if (dr >= R || dr < 1e-6) continue;
                // Out through the tread, or out past the sidewall, whichever is nearer.
                const double penR = R - dr - allow, penA = half - std::fabs(da) - allow;
                if (penR <= 0 || penA <= 0) continue;
                V3 n;
                double F;
                if (penR <= penA) {
                    n = rv / dr;
                    F = kTireK * penR + kRimK * std::max(0.0, penR - side * 0.85);  // squashed onto the rim, it's steel
                } else {
                    n = axis * (da > 0 ? 1.0 : -1.0);
                    F = kTireK * penA;
                }
                const double vn = dot(vp - vc, n);
                F = std::max(0.0, F - kTireC * vn);
                if (F <= 0) continue;
                // Rubbing: the tread's own motion against the sheet metal.
                const V3 vSurf = vc + cross(spinAxis * wh.omega, rv);
                V3 vt = vp - vSurf;
                vt -= n * dot(vt, n);
                const double vtl = len(vt);
                V3 Fp = n * F;  // on the body
                if (vtl > 1e-6) Fp -= vt * (std::min(0.6 * F, 40.0 * vtl) / vtl);
                for (int k = 0; k < 3; k++) nodes[tv[k]].f += Fp * wgt[k];
                // ...and back on the hub, shared by how far out along the axle it touches.
                const double share = clampd(0.5 + da / (2 * half), 0, 1);
                a.f -= Fp * (1 - share);
                b.f -= Fp * share;
                wh.rubTorque += dot(cross(rv, -Fp), spinAxis);
                wh.rub += (float)F;
            }
        }
    }
}

void Car::wheelStep(double h, const World& w) {
    // Half shafts. A wheel spun or stopped from outside (a test, a reset, the network)
    // takes its side gear along; one with no drive has nothing winding its shaft.
    for (Wheel& wh : wheels) {
        if (wh.omega != wh.omegaWas) wh.sideOmega += wh.omega - wh.omegaWas;
        if (!wheelPowered(wh)) wh.sideOmega = wh.omega, wh.twist = 0;
    }
    // Side-gear speeds at the start of the step: the drivetrain's real spin-up over the
    // step, whatever caused it (drive, road, brakes, lockers), is what the engine follows.
    double omega0[16] = {0};
    for (size_t i = 0; i < wheels.size() && i < 16; i++) omega0[i] = wheels[i].sideOmega;
    // Engine: rpm follows the driven wheels once the clutch is locked.
    int nd = 0;
    double ws = 0, gs = 0;
    for (Wheel& wh : wheels)
        if (wheelPowered(wh)) {
            ws += wh.sideOmega;
            gs += std::fabs(wh.groundSpeed) / wh.R;
            nd++;
        }
    const double lowMul = lowRange && spec->lowRange > 0 ? spec->lowRange : 1.0;
    double ratio = gearRatio(gear) * spec->finalDrive * lowMul;
    double wheelRpm = (nd ? ws / nd : 0) * ratio * 60 / (2 * M_PI);
    // What the engine would turn at if the tires weren't slipping; shift on this so
    // wheelspin doesn't run the gearbox up to 4th at walking pace.
    double roadRpm = (nd ? gs / nd : 0) * std::fabs(ratio) * 60 / (2 * M_PI);
    shiftTimer -= h;

    // Brake traction control (it needs the ABS unit to work the brakes on its own): an
    // open diff gives both sides the same torque, so one wheel spinning (in the air, on
    // ice, unloaded by the body's roll) leaves the other with only what the spinner can
    // hold. Braking the spinner adds that braking torque to what the diff hands the
    // other side. It acts on the spin difference across the axle; the engine cut below
    // still watches every wheel (backing off a spinning tire keeps it from digging in).
    const bool tcOn = tractionControl && spec->tractionFitted;
    const bool btcs = tcOn && spec->absEnabled && brake < 0.05 && throttle > 0.05;
    for (const Axle& ax : axles) {
        if (ax.wheel[0] < 0 || ax.wheel[1] < 0) continue;
        Wheel& l = wheels[ax.wheel[0]];
        Wheel& r = wheels[ax.wheel[1]];
        const bool open = diffType(ax.front) == DIFF_OPEN && wheelPowered(l) && wheelPowered(r);
        for (int s2 = 0; s2 < 2; s2++) {
            Wheel& me = s2 ? r : l;
            const Wheel& other = s2 ? l : r;
            if (btcs && open) {
                // How much faster this tread spins over the road than the other's, in the
                // direction it's being driven (m/s; reversing too). A tread turning slower than
                // the road (the inside wheel of a turn, one dragging on ice) isn't spinning:
                // braking it would only drag it more.
                const double dir = gear < 0 ? -1.0 : 1.0;
                const double lead = dir * (me.omega * me.rollR() - me.groundSpeed) - dir * (other.omega * other.rollR() - other.groundSpeed);
                // Pressure keeps building while it leads by more than ~0.3 m/s and bleeds off
                // once it doesn't (the valves hold it there, as ABS does the other way).
                me.tcBrake = clampd(me.tcBrake + h * 4.0 * clampd(lead - 0.3, -2, 1), 0, 1);
                // (Off above ~60 km/h: there it's stability control's job, and a stab of brake
                // at speed pulls the car sideways.)
                me.tcBrake = std::min(me.tcBrake, 1 - smoothstep(14, 18, std::fabs(me.groundSpeed)));
            } else {
                me.tcBrake = std::max(0.0, me.tcBrake - h * 20);
            }
        }
    }
    // Stability control (VSC / AdvanceTrac / ESC: it needs the ABS unit too): it compares
    // how fast the car is turning with how fast the steering asks for (a simple bicycle model,
    // no faster than the tires can hold), and when the tail comes round, or the body slides
    // past ~4 degrees, it brakes the outside front wheel to pull the nose back and eases the
    // throttle. Off in low range (as Toyota's and Ford's are), off with traction control.
    // (It works the brake gently, a third of its force at most: a hard stab at speed would
    // pull the car off line.)
    {
        const V3 fwd = chassis.nodes.empty() ? V3(1, 0, 0) : chassis.rot.rotate(V3(1, 0, 0));
        const V3 up = chassis.nodes.empty() ? V3(0, 1, 0) : chassis.rot.rotate(V3(0, 1, 0));
        if (dot(escFwd, escFwd) > 0.5) escYaw += (dot(cross(escFwd, fwd), up) / h - escYaw) * std::min(1.0, h * 40);
        escFwd = fwd;
        V3 vc;
        double M = 0;
        for (int i = 0; i < bodyNodes; i++) vc += nodes[i].v * nodes[i].m, M += nodes[i].m;
        vc = vc / std::max(M, 1e-9);
        const double vx = dot(vc, fwd), vy = dot(vc, cross(up, fwd));  // (vy: + to the left)
        double want = 0;
        if (tcOn && spec->absEnabled && !lowRange && vx > 8) {
            const double vch = 25, refYaw = vx * steer / (wheelbase * (1 + vx * vx / (vch * vch)));
            const double cap = 0.9 * 9.81 / vx;
            const double ref = clampd(refYaw, -cap, cap);
            const double sign = escYaw >= 0 ? 1 : -1;
            const double over = (escYaw - ref) * sign;            // turning faster than asked (rad/s)
            const double slip = std::fabs(std::atan2(vy, vx));     // body sideslip (rad)
            // (It lets a car run ~7 degrees of slip before it steps in, and fades in from 30 km/h.
            // Its yaw deadband grows with the turn it expects: going straight, where the model is
            // exact, ~2 deg/s of yaw nobody asked for is enough; in a bend it allows ~4.5.)
            const double deadband = 0.035 + 0.5 * std::fabs(ref);
            want = clampd(std::max((over - std::min(deadband, 0.08)) * 3, (slip - 0.12) * 3), 0, 0.35) * smoothstep(8, 12, vx);
        }
        escLevel += (want - escLevel) * std::min(1.0, h * 25);
        for (Wheel& wh : wheels) {
            // The front wheel on the outside of the turn the car is actually making. Under hard
            // braking that brake is already full, so it also lets off the inside wheels, front
            // and rear (it works through the ABS valves).
            const bool outsideSide = escYaw >= 0 ? !wh.left : wh.left;
            wh.escBrake = wh.front && outsideSide ? escLevel : 0;
            wh.escCut = outsideSide ? 0 : escLevel;
        }
    }
    // Traction control: back off the throttle while a driven wheel is spinning.
    // At a crawl a wheel turning a little faster than the car reads as a big slip ratio;
    // only count spin once the tread is really sliding (> ~1 m/s), so climbing a kerb or
    // a ridge slowly still gets full power.
    double maxSpin = 0, cornerSpin = 0;
    for (const Wheel& wh : wheels)
        if (wheelPowered(wh)) {
            double slideSpeed = std::fabs(wh.omega * wh.rollR() - wh.groundSpeed);
            // (As a share of the slip that grips best on this tread and surface: more on
            // loose ground, where a wheel has to dig to pull.)
            maxSpin = std::max(maxSpin, std::fabs(wh.sx) * wh.bite * clampd((slideSpeed - 0.5) / 1.5, 0, 1));
            // Cornering on the limit, driven: the tire's grip is already spent sideways, and
            // the drive asks it for more. Watch the combined slip, so the power can't break
            // the rear loose mid-corner (the stability-control half of it).
            if (wh.sx > 0.02 && wh.load > 0)
                cornerSpin = std::max(cornerSpin, 0.9 * wh.slip * wh.bite * clampd((std::fabs(wh.groundSpeed) - 4) / 4, 0, 1));
        }
    maxSpin = std::max(maxSpin, cornerSpin);
    // Proportional, so it settles on the slip that gives the most grip instead of
    // switching the power on and off (which shakes a live axle into hopping).
    // (Down to a trickle when it's the car sliding sideways under power; otherwise a floor,
    // so a hopping live axle still gets some drive.)
    // In low range it only works the brakes (Toyota's A-TRAC, Ford's rock crawl mode): a
    // throttle cut at the foot of a ledge stalls the climb, and at a crawl a spinning
    // tire isn't going fast enough to dig in.
    if (tcOn && !(lowRange && spec->absEnabled)) tcCut = clampd(tcCut - h * 30.0 * (maxSpin - 0.14), cornerSpin > 0.16 ? 0.02 : 0.15, 1.0);
    else tcCut = 1;
    tcCut = std::min(tcCut, 1 - 0.9 * escLevel);  // (stability control eases the throttle too)
    // Engine drag control (the other half of a modern traction system): off the throttle on
    // ice or snow, engine braking can drag the driven wheels toward locking, and a dragging
    // front axle won't steer; it opens the throttle just enough to keep them rolling.
    double maxDrag = 0;
    for (const Wheel& wh : wheels)
        if (wheelPowered(wh) && wh.load > 0) {
            double slideSpeed = std::fabs(wh.omega * wh.rollR() - wh.groundSpeed);
            maxDrag = std::max(maxDrag, -wh.sx * wh.bite * clampd((slideSpeed - 0.5) / 1.5, 0, 1));
        }
    if (tcOn && brake < 0.05) dragAssist = clampd(dragAssist + h * 20.0 * (maxDrag - 0.1), 0.0, 0.35);
    else dragAssist = 0;
    double thr = shiftTimer > 0 || engineDead ? 0.0 : std::max(throttle * tcCut, dragAssist);
    // Speed limiter (the ECU): eases the throttle off over the last 3 km/h. (On the road
    // speed it filters, ~0.3 s: a live axle hopping on its springs mustn't pulse the throttle.)
    if (spec->speedLimit > 0 && nd) {
        govKmh += (gs / nd * wheels[0].R * 3.6 - govKmh) * std::min(1.0, h / 0.3);
        thr *= clampd((spec->speedLimit - govKmh) / 3.0, 0, 1);
    }
    double power = 0.25 + 0.75 * engineHealth;  // a crushed engine bay loses power
    // Turbo: boost follows the pedal, slowly at low revs (lag), and leaks away off throttle.
    // Off boost the engine makes a bit over half its torque.
    double turbo = 1;
    if (spec->turboLag > 0) {
        double want = engineDead ? 0 : throttle;  // traction control trims the spark, not the boost
        double tau = want > boost ? spec->turboLag * clampd(3000 / std::max(rpm, 1.0), 0.5, 2.0) : 0.25;
        boost += (want - boost) * std::min(1.0, h / tau);
        turbo = 0.7 + 0.3 * boost;
    }

    const int ng = (int)spec->gears.size();
    // Automatic shifting, judged on the gearbox's input speed (the engine's, through a
    // clutch or a locked converter; the turbine's through a slipping one).
    auto autoShift = [&](double in) {
        if (manualShift || gear < 1) return;
        // The autopilot crawling in low range keeps it in first (no upshift between
        // ledges, to arrive at the next one in second, lugging).
        const int top = pilot.active && pilot.holdFirst ? 1 : ng;
        // Full throttle: upshift near the redline. Light throttle: short-shift to keep the
        // revs down, as a real gearbox does when cruising. Downshift once revs really sag
        // (lower point under throttle, never right after an upshift, so wheelspin doesn't
        // cause hunting); kick down a gear for a burst of power when the pedal is floored
        // at low revs.
        const double pedal = clampd(throttle, 0, 1);
        const double easyUp = std::max(2300.0, 0.45 * spec->redline), fullUp = spec->redline - 400;
        const double upAt = easyUp + (fullUp - easyUp) * smoothstep(0.35, 0.9, pedal);
        const double downAt = pedal > 0.5 ? 1600 : 1900;
        const double nextRpm = gear < ng ? in * spec->gears[gear] / spec->gears[gear - 1] : 0;
        const double lowerRpm = gear > 1 ? in * spec->gears[gear - 2] / spec->gears[gear - 1] : 1e9;
        if (gear > top && shiftTimer < -0.3) {
            gear--;
            shiftTimer = spec->shiftTime * 0.7;
        } else if (gear < top && in > upAt && roadRpm > upAt - 1100 && nextRpm > downAt + 350 && shiftTimer < -0.3) {
            gear++;
            shiftTimer = spec->shiftTime;
        } else if (gear > 1 && std::min(in, roadRpm) < downAt && shiftTimer < -1.2) {
            gear--;
            shiftTimer = spec->shiftTime * 0.7;
        } else if (gear > 1 && pedal > 0.9 && in < 0.55 * spec->redline && lowerRpm < spec->redline - 900 && shiftTimer < -0.6) {
            gear--;
            shiftTimer = spec->shiftTime * 0.7;
        }
    };
    // The engine and flywheel (and gearbox input), kg*m^2.
    const double Ie = 0.06 + 0.00012 * peakTorque(*spec);
    const bool automatic = spec->stallRpm > 0;
    // A torque converter locks up in the higher gears once it's moving; below that its
    // fluid coupling slips, and the engine runs on its own speed.
    const bool converterSlips = automatic && !(gear >= spec->lockupGear && wheelRpm > 1300);
    bool clutchSlipping = !automatic && wheelRpm < 1250;
    double Te, Iin = Ie;
    if (fuel < 0) fuel = spec->fuelCapacity;
    if (jerry < 0) jerry = spec->jerryCan;
    if (engineDead || nd == 0 || gear == 0 || outOfFuel()) {
        if (engineDead || outOfFuel()) rpm += (0 - rpm) * std::min(1.0, h * 2);
        else rpm += (spec->idleRpm + thr * 4500 - rpm) * std::min(1.0, h * 4);  // nothing to drive: revs freely
        Te = 0;
    } else if (converterSlips) {
        // Torque converter. The pump (on the crank) churns oil at the turbine (on the
        // gearbox input): it takes (rpm / K)^2 from the engine, K set by the stall speed,
        // and the stator multiplies that up to stallRatio at the turbine while the turbine
        // lags far behind, falling to 1:1 at the coupling point (~85% of pump speed). The
        // wheels driving the engine (coasting) go back through it, weakly: an automatic's
        // engine braking is soft.
        const double K = spec->stallRpm / std::sqrt(std::max(1.0, engineTorque(spec->stallRpm)));
        const double pump = std::max(rpm, 0.0), turb = wheelRpm;
        double Tp, Tt;
        const double sr = pump > 1 ? turb / pump : (turb > 1 ? 9.0 : 0.0);
        // Stopped with the pedal up, it's parked (in neutral, so a strap or a winch can
        // still move it): no creep until the driver asks for it.
        const bool parked = throttle < 0.02 && (nd ? gs / nd * wheels[0].R : 0.0) < 0.5;
        if (parked) {
            Tp = Tt = 0;
        } else if (sr < 1) {
            const double sc = std::max(sr, 0.0);
            const double cap = sc < 0.85 ? 1.0 : (1 - sc) / 0.15;  // capacity falls away near coupling
            Tp = (pump / K) * (pump / K) * cap;
            Tt = Tp * (sc < 0.85 ? spec->stallRatio - (spec->stallRatio - 1) * sc / 0.85 : 1.0);
        } else {
            Tp = Tt = -0.6 * (turb / K) * (turb / K) * std::min(1.0, (sr - 1) / 0.15);
        }
        // The engine spins up or down on its own inertia; an idle governor holds it at idle.
        const double idleThr = clampd((spec->idleRpm + 100 - rpm) / 400, 0, 0.4);
        const double tEff = std::max(thr, idleThr);
        double Teng = tEff * engineTorque(rpm) * power * turbo - (1 - tEff) * (18 + rpm * 0.005);
        if (rpm > spec->redline) Teng = std::min(Teng, 0.0) - (rpm - spec->redline) * 0.5;  // rev limiter
        rpm = std::max(0.0, rpm + (Teng - Tp) / Ie * h * 60 / (2 * M_PI));
        Te = Tt;          // what the gears get
        Iin = 0.08;       // turbine and gearbox input
        autoShift(turb);  // (a modern auto shifts on output speed, not on a flaring engine)
    } else if (clutchSlipping) {
        double target = spec->idleRpm + thr * 2600;
        rpm += (target - rpm) * std::min(1.0, h * 5);
        rpm = std::max(rpm, wheelRpm);
        Te = thr * engineTorque(rpm) * power * turbo;
        // Rolling to a stop: drop back toward first, ready to pull away.
        if (!manualShift && gear > 1 && shiftTimer < -0.25 && roadRpm < 1250) {
            gear--;
            shiftTimer = 0;
        }
    } else {
        rpm = wheelRpm;
        Te = thr * engineTorque(rpm) * power * turbo - (1 - thr) * (18 + rpm * 0.005);  // engine braking
        if (rpm > spec->redline) Te = std::min(Te, 0.0) - (rpm - spec->redline) * 0.5;  // rev limiter
        autoShift(rpm);
    }
    // Fuel: what the engine burns for the power it makes (~250 g/kWh, petrol 0.745 kg/L), and
    // a trickle at idle.
    if (fuelUse > 0 && !engineDead && fuel > 0) {
        const double kW = std::max(0.0, Te) * rpm * 2 * M_PI / 60 / 1000;
        const double lps = kW * 250.0 / 745.0 / 3600.0 + (rpm > 300 ? 0.8 / 3600.0 : 0.0);
        fuel = std::max(0.0, fuel - lps * fuelUse * h);
    }
    int driveWheels = 0;
    for (const Wheel& wh : wheels) driveWheels += wh.driven && (!wh.front || frontEngaged());
    double driveTorque = nd && driveWheels ? Te * ratio * 0.92 / driveWheels : 0;
    // Clutch in (engaged): the engine is geared to the wheels, and its torque goes into the
    // crank, not straight to the hubs: the gearbox, the transfer case and the differentials
    // pass it on (solved after the wheels, below). Slipping (pulling away), the clutch
    // presses a set torque into the gearbox, the same to every driven wheel.
    const bool engaged = !clutchSlipping && nd > 0 && !engineDead && gear != 0;  // (a converter is always "in")
    brakePressure += clampd(brake - brakePressure, -h * 10, h * 7);

    // Limited-slip differentials: torque flows from the faster side to the slower one, up
    // to the diff's bias (a share of the torque going through it), so it frees up off the
    // throttle. (Lockers and a locked center are solid gears: they're in the exact solve
    // at the end of the step.)
    for (const Axle& ax : axles) {
        if (ax.wheel[0] < 0 || ax.wheel[1] < 0) continue;
        Wheel& l = wheels[ax.wheel[0]];
        Wheel& r = wheels[ax.wheel[1]];
        const DiffType dt = diffType(ax.front);
        if (dt != DIFF_LSD || !wheelPowered(l) || !wheelPowered(r)) continue;
        const double Ieff = kSideGearI / 2;
        // A limited-slip diff grabs the wheels together hard at low speed (launches, slow
        // corners) and eases off as the speed climbs: held stiffly together at 300 km/h, the
        // rear wheels' fight over every tiny yaw sets the car weaving (e-diffs open up at
        // speed for the same reason).
        const double v = 0.5 * (std::fabs(l.groundSpeed) + std::fabs(r.groundSpeed));
        const double lsdGain = 0.25 * clampd((70 - v) / 35, 0.02, 1.0);
        double T = (l.sideOmega - r.sideOmega) * Ieff / h * lsdGain;
        const double cap = 30 + (spec->lsdBias - 1) / (spec->lsdBias + 1) * std::fabs(2 * driveTorque);
        T = clampd(T, -cap, cap);
        l.sideOmega -= T * h / kSideGearI;
        r.sideOmega += T * h / kSideGearI;
        l.xferTorque -= T;
        r.xferTorque += T;
    }

    V3 fwd = norm(groupAvg(grpFront) - groupAvg(grpRear));
    V3 leftDir = norm(groupAvg(grpLeft) - groupAvg(grpRight));

    // Torque reactions. Each driven axle's differential is pushed back by the torque
    // it sends to the wheels (squat under power, axle wind-up on a solid axle), and
    // the prop shaft twists the gearbox one way and the diff the other.
    if (!engineNodes.empty()) {
        // What the engine (or the converter's turbine) is actually putting in, at the
        // wheels. The housings take the shafts' torque only up to that: the rest is the
        // shafts ringing against the free-spinning gears (parked, or between shifts),
        // which goes round inside the driveline and doesn't wrap the axle.
        const double input = std::fabs(engaged ? Te * ratio * 0.92 : driveTorque * driveWheels);
        double shafts = 0;
        for (const Wheel& wh : wheels)
            if (wheelPowered(wh)) shafts += wh.driveActual;
        const double share = std::fabs(shafts) > 1 ? std::min(1.0, input / std::fabs(shafts)) : 0.0;
        double propTotal = 0;
        for (const Axle& ax : axles) {
            int powered = 0;
            for (int s2 = 0; s2 < 2; s2++)
                if (ax.wheel[s2] >= 0 && wheelPowered(wheels[ax.wheel[s2]])) powered++;
            if (!powered || ax.diffNodes.empty()) continue;
            double Taxle = 0;  // what reached its hubs last step
            for (int s2 = 0; s2 < 2; s2++)
                if (ax.wheel[s2] >= 0 && wheelPowered(wheels[ax.wheel[s2]])) Taxle += wheels[ax.wheel[s2]].driveActual;
            Taxle *= share;
            if (Taxle == 0) continue;
            V3 axleDir = leftDir;
            if (ax.propShaft >= 0) {
                double prop = Taxle / spec->finalDrive;
                propTotal += prop;
                applyTorque(ax.diffNodes, -(fwd * prop) - axleDir * Taxle);
            } else {
                applyTorque(ax.diffNodes, -axleDir * Taxle);  // transaxle: the whole block takes it
            }
        }
        if (propTotal != 0) applyTorque(engineNodes, fwd * propTotal);
    }

    for (Wheel& wh : wheels) {
        Node& a = nodes[wh.inner];
        Node& b = nodes[wh.outer];
        V3 c = (a.p + b.p) * 0.5, vc = (a.v + b.v) * 0.5;
        V3 axis = norm(b.p - a.p);
        if (!wh.left) axis = -axis;  // axis always points to the car's left

        const double R = wh.rollR();
        const double mu = spec->tireGrip * (wh.popped ? 0.55 : 1.0);
        double Fx = 0;
        wh.load = 0;
        wh.slip = 0;
        wh.sx = 0;
        Contact ct;
        if (w.contact(c, R, &axis, wh.width * 0.5, ct)) {
            const V3& n = ct.n;
            wh.surface = ct.surface;
            // Aired down, the tread spreads into a longer, softer patch: it wraps rocks and
            // grips loose ground (~1.2% per psi off-pavement), but the sidewall gives way
            // sooner in a fast corner and the rim is closer to the rocks.
            const double psiDrop = spec->tirePsi - tirePsi;
            const SurfaceInfo& si = surfaceInfo(ct.surface);
            const int tread = std::min(std::max(spec->tireType, 0), 3);
            double grip = si.grip * kTreadGrip[tread][ct.surface];
            if (ct.surface != SURF_PAVED && ct.surface != SURF_WET && ct.surface != SURF_ICE) grip *= (1 + 0.012 * psiDrop) * spec->looseGrip;
            // Mud packed into the tread turns it into a slick.
            grip *= 1 - (tread == TIRE_MUD_TERRAIN ? 0.25 : 0.4) * wh.clog;
            // Standing water: past ~80 km/h the tread can't push it out of the way fast
            // enough and the tire rides up on it (aquaplaning).
            if (ct.surface == SURF_WET) grip *= 1 - (tread == TIRE_SEMI_SLICK ? 0.6 : 0.45) * smoothstep(22, 40, len(vc));
            const double bite = si.bite * kTreadBite[tread];
            wh.bite = (float)bite;
            const double tireK = kTireK * spec->tireSoft * (tirePsi + 10) / (spec->tirePsi + 10);
            const double popSpeed = kRimPopSpeed * clampd((tirePsi + 5) / (spec->tirePsi + 5), 0.45, 1.1);
            // Slip is measured where the tread touches the road: the hub's velocity plus the
            // knuckle tilting or steering (its rotation other than the wheel's own spin).
            {
                V3 ab = b.p - a.p;
                double L = std::max(1e-6, len(ab));
                V3 tiltRate = cross(ab / L, (b.v - a.v) / L);
                vc += cross(tiltRate, -n * (R - ct.pen + wh.sink));
            }
            double vn = dot(vc, n);
            // Soft ground gives: the tire sinks until what's under it holds it up.
            const double pen = ct.pen - wh.sink;
            // Rim contact: once the sidewall is squashed flat, the rim takes the load. (Flat, the
            // folded rubber under the rim is all there is: past it, the rim strikes steel-hard.)
            double rimPen = pen - (R - wh.rimR) * 0.85;
            double N = pen > 0 ? std::max(0.0, tireK * pen + kRimK * std::max(0.0, rimPen) - kTireC * vn) : 0.0;
            // Soft ground gives way under a hard landing: past what it can bear, the sand
            // (mud, snow) is shoved aside and packed down instead of the tire squashing
            // further, so the tire digs a crater rather than slamming onto its rim, and
            // the ground keeps the energy (it doesn't spring back).
            bool gives = false;
            if (si.sink > 0 && N > 0) {
                const double bear = softBearing(ct.surface, wh.width, R, wh.sink);
                if (N > bear && wh.sink < softCrushMax(ct.surface)) {
                    wh.sink = (float)std::min(softCrushMax(ct.surface), wh.sink + std::max(0.0, -vn) * h);
                    N = bear;
                    gives = true;
                }
            }
            // A hard rim strike pinches the tire (the sand under a rim gives; a rock's edge doesn't).
            if (!gives && !wh.popped && rimPen > 0 && -vn > popSpeed * (si.sink > 0 ? 1.6 : 1.0)) {
                wh.popped = true;
                tirePops++;
            }
            V3 F = n * N;
            V3 Ft;
            V3 fdir = cross(axis, n);
            double fl = len(fdir);
            if (fl > 0.25 && N > 0) {
                fdir = fdir / fl;
                V3 sdir = cross(n, fdir);
                double vx = dot(vc, fdir), vy = dot(vc, sdir);
                double wr = wh.omega * R;
                double den = std::max(std::max(std::fabs(vx), std::fabs(wr)), 3.0);
                double sx = (wr - vx) / den, sy = -vy / den;
                double sm = std::sqrt(sx * sx + sy * sy);
                double fx = 0, fy = 0;
                if (sm > 1e-9) {
                    double Fm = mu * grip * N * tireCurve(sm, bite, si.keep);
                    fx = Fm * sx / sm;
                    // Low pressure: the sidewall rolls over in a fast turn.
                    const double roll = clampd((psiDrop - 12) * 0.025, 0, 0.4) * clampd((std::fabs(vx) - 8) / 12, 0, 1);
                    fy = Fm * sy / sm * spec->lateralGrip * (1 - roll);
                }
                // Static friction at walking pace: the contact patch acts as a stiff spring
                // until it breaks loose, so parked cars hold on slopes instead of creeping.
                double slipX = vx - wr, slipY = vy;
                double stick = clampd(1.0 - std::fabs(vx) / 2.0, 0, 1) *
                               clampd(1.0 - std::max(std::fabs(slipX), std::fabs(slipY)) / 0.5, 0, 1);
                if (stick > 0) {
                    // Rolling lets the tread relax: deflection fades over ~0.3 m of travel.
                    double relax = std::exp(-std::fabs(wr) * h / 0.3);
                    wh.stickX = wh.stickX * relax + slipX * h;
                    wh.stickY = wh.stickY * relax + slipY * h;
                    double sxF = -kStickK * wh.stickX - kStickC * slipX, syF = -kStickK * wh.stickY - kStickC * slipY;
                    double cap = mu * grip * N, mag = std::sqrt(sxF * sxF + syF * syF);
                    if (mag > cap) {  // breaking loose: the patch slides, deflection stays at the limit
                        sxF *= cap / mag;
                        syF *= cap / mag;
                        wh.stickX = -(sxF + kStickC * slipX) / kStickK;
                        wh.stickY = -(syF + kStickC * slipY) / kStickK;
                    }
                    fx = fx * (1 - stick) + sxF * stick;
                    fy = fy * (1 - stick) + syF * stick;
                } else {
                    wh.stickX = wh.stickY = 0;
                }
                Fx = fx;
                Ft = fdir * fx + sdir * fy;
                F += Ft;
                wh.sx = sx;
                wh.slip = (float)sm;
                wh.groundSpeed = vx;
                // Sinking: under its own load a tire settles into sand, mud or snow (less
                // aired down: a longer, wider patch; less at speed: it planes over the top),
                // and a spinning tire digs itself in.
                const double spinSpeed = std::fabs(wr - vx);
                if (si.sink > 0) {
                    const double pf = clampd((tirePsi + 8) / (spec->tirePsi + 8), 0.35, 1.2);
                    const double lf = clampd(std::sqrt(N / 4000.0), 0, 1.6);
                    const double rest = si.sink * 0.45 * pf * lf * (1 - 0.6 * smoothstep(6, 20, std::fabs(vx)));
                    // (Down into it under load; back up only by rolling out of the hole: a
                    // tire left in a crater or dug in stays there.)
                    const double settle = rest > wh.sink ? 0.5 + std::fabs(vx) : 0.05 + std::fabs(vx);
                    wh.sink += (float)((rest - wh.sink) * std::min(1.0, h * settle / 0.4));
                    if (spinSpeed > 1.2) wh.sink = std::max(wh.sink, (float)std::min(si.sink, wh.sink + 0.05 * (spinSpeed - 1.2) * h));
                } else {
                    wh.sink = (float)std::max(0.0, wh.sink - wh.sink * h * (1 + std::fabs(vx)) / 0.3);
                }
                // Mud packs into the tread as it rolls; wheel speed flings it out (big
                // mud lugs shed it far better).
                const double roll = std::fabs(wr);
                const bool mudLugs = tread == TIRE_MUD_TERRAIN;
                if (ct.surface == SURF_MUD) wh.clog = (float)std::min(1.0, wh.clog + h * roll * (mudLugs ? 0.03 : 0.06));
                wh.clog = (float)std::max(0.0, wh.clog - h * wh.clog * (std::max(0.0, roll - 6) * (mudLugs ? 0.5 : 0.2) + (ct.surface == SURF_MUD ? 0 : roll * 0.03)));
            }
            a.f += F * 0.5;
            b.f += F * 0.5;
            // The road pushes on the tire at the ground, not at the hub: the extra moment
            // (cornering force times wheel radius) leans on the knuckle and the suspension,
            // which is what rolls the body. Its part along the axle spins the wheel instead.
            V3 arm = -n * (R - pen);
            V3 tau = cross(arm, Ft);
            tau -= axis * dot(tau, axis);
            if (!wh.detached) applyTorque(wh.assembly, tau);
            wh.load = (float)N;
            wh.contactNormal = n;
            wh.contact = c - n * (R - pen);
        }

        if (wh.load <= 0) {
            // No contact patch survives a jump. Old deflection otherwise kicks the
            // tire sideways on landing, and road speed is the hub's own (for shifts and ABS).
            wh.stickX = wh.stickY = 0;
            wh.groundSpeed = dot(vc, fwd);
            // Off the brake there is nothing for ABS to hold: its valves stand open, so the
            // first stab after the landing gets full pressure. (Braking in the air, it keeps
            // working the valve below.)
            if (brakePressure <= 0.05) wh.abs = 1;
        }
        double Td = wheelPowered(wh) && !engaged ? driveTorque : 0.0;
        // The shaft's wind-up drives the hub (and holds back the side gear).
        double Ts = 0;
        if (wheelPowered(wh)) {
            // (The damping works on the side gear's small inertia, stepped explicitly: past
            // c h / I = 2 it would pump the gear instead of calming it. A big, heavy tire on a
            // stiff shaft (the buggy's 40s) asked for more than that and the shafts snapped at
            // rest; it gets what the step can carry. Every other car stays under the cap.)
            const double k = shaftStiffness(), c = std::min(std::sqrt(k * wh.I), 1.8 * kSideGearI / h);  // shuffle damped at ~1/2 critical
            Ts = k * wh.twist + c * (wh.sideOmega - wh.omega);
            wh.sideOmega += (Td - Ts) / kSideGearI * h;
        }
        wh.shaftTorque = Ts;
        wh.omega += (Ts - Fx * R + wh.rubTorque) / wh.I * h;
        // Axle shaft and CV: what the engine winds into it, plus whatever a locker or the
        // locked center shoves through it to keep the wheels together. Full throttle in low
        // range with a wheel in the air and the axle locked doubles the load on the other
        // side, a locker at full lock binds, and a spinning wheel landing grabs; past its
        // limit the shaft snaps and that hub freewheels until it's repaired. IFS CVs are
        // weaker than solid-axle shafts, and weaker again at a steep angle.
        if (wheelPowered(wh) && spec->shaftLimit > 0) {
            const double shaftT = std::fabs(Ts);
            wh.shaftLoad += (float)((shaftT - wh.shaftLoad) * std::min(1.0, h / 0.01));
            double limit = spec->shaftLimit;
            if (wh.front && spec->frontSusp != SOLID_AXLE) limit *= 0.65 * (1 - 0.35 * clampd(std::fabs(steer) / 0.6, 0, 1));
            if (wh.shaftLoad > limit) {
                wh.shaftBroken = true;
                shaftsBroken++;
                if (getenv("CZ_DEBUG")) std::printf("%s: %s %s shaft snapped at %.0f N*m (limit %.0f), load %.0f N, Fx %.0f, slip %.2f, steer %.2f\n", spec->name, wh.front ? "front" : "rear", wh.left ? "left" : "right", wh.shaftLoad, limit, wh.load, Fx, wh.sx, steer);
                impactEnergy += 400;
            }
        } else {
            wh.shaftLoad = 0;
        }
        wh.xferTorque = 0;

        // Bearing drag plus rolling resistance (the tire flexing as it rolls: ~1.2% of
        // its load). It's also what keeps a parked car from drifting on almost-flat ground.
        // On soft ground the tire is always climbing out of the rut it presses (bulldozing):
        // more the deeper it's sunk, less aired down.
        // (Aired down, a tire flexes more on hard ground but floats over soft ground.)
        double crr = 0.012 * (1 + 0.03 * std::max(0.0, spec->tirePsi - tirePsi));
        if (wh.load > 0) {
            const SurfaceInfo& si = surfaceInfo(wh.surface);
            if (si.sink > 0) crr = si.rolling * std::pow(clampd((tirePsi + 8) / (spec->tirePsi + 8), 0.3, 1.2), 0.7) + 1.3 * wh.sink / wh.R;
            else crr += si.rolling - 0.012;
        }
        const double Tdrag = 0.5 + std::max(0.004, crr) * wh.load * wh.R;
        double Tb = Tdrag;
        if (!wh.detached) {
            // ABS: each wheel's pressure is backed off quickly when it starts to lock and
            // let back in when it grips again (it switches off at a crawl).
            // It holds each tire just under the slip that gives the most grip (~13%).
            if (spec->absEnabled && brakePressure > 0.05 && std::fabs(wh.groundSpeed) > 2.0)
            {
                // Cornering brake control: braking while the tire is also sliding sideways,
                // ease it off (the rears especially: a rear sliding under braking spins the car).
                // (The rears are held under the slip the fronts run at, as real ABS does: a
                // rear tire braking at its peak has little left to hold the tail in line.)
                const double target = wh.front ? 0.11 : 0.07;
                double err = std::fabs(wh.sx) * wh.bite - target;
                if (wh.sx < -0.01) err = std::max(err, wh.slip * wh.bite * (wh.front ? 0.8 : 0.95) - 0.14);
                // A wheel off the ground (a light rear hopping under a hard stop, a jump): the
                // wheel-speed sensor still sees it slowing against the car, and the pressure is
                // held off rather than let back in, so it isn't locked when it lands.
                if (wh.load <= 0) err = std::fabs(std::min(0.0, wh.omega * wh.rollR() - wh.groundSpeed)) / std::fabs(wh.groundSpeed) * wh.bite - target;
                wh.abs = clampd(wh.abs - h * 60.0 * err, 0.05, 1.0);
            }
            else if (brakePressure <= 0.05)
                wh.abs = 1;  // (pedal up: nothing to hold off)
            else
                wh.abs = std::min(1.0, wh.abs + h * 6);
            double service = (brakePressure * wh.abs * (1 - 1.5 * wh.escCut) + std::max(wh.tcBrake, wh.escBrake)) * wh.brakeMax;
            Tb += service + (wh.front ? 0.0 : handbrake * kHandbrakeTorque);
        }
        // The brake grips the wheel to its knuckle, so it stops the wheel turning relative to
        // the knuckle, not in space: when the knuckle (or a solid axle) pitches back under
        // braking, a locked wheel pitches with it.
        double spin = 0;
        if (!wh.detached && wh.assembly.size() >= 3) {
            // Best-fit angular velocity of the whole knuckle (or axle housing): I^-1 * L.
            V3 cm, vm;
            double M = 0;
            for (int i : wh.assembly) {
                cm += nodes[i].p * nodes[i].m;
                vm += nodes[i].v * nodes[i].m;
                M += nodes[i].m;
            }
            cm = cm / M;
            vm = vm / M;
            M3 I;
            V3 L;
            for (int i : wh.assembly) {
                V3 d = nodes[i].p - cm;
                double dd = dot(d, d), m = nodes[i].m;
                L += cross(d, nodes[i].v - vm) * m;
                I.c[0] += V3(dd - d.x * d.x, -d.y * d.x, -d.z * d.x) * m;
                I.c[1] += V3(-d.x * d.y, dd - d.y * d.y, -d.z * d.y) * m;
                I.c[2] += V3(-d.x * d.z, -d.y * d.z, dd - d.z * d.z) * m;
            }
            double tr = I.c[0].x + I.c[1].y + I.c[2].z;
            if (tr > 1e-6) {
                I.c[0].x += tr * 1e-3;
                I.c[1].y += tr * 1e-3;
                I.c[2].z += tr * 1e-3;
                spin = dot(inverseSym(I) * L, axis);
            }
        }
        double dw = Tb / wh.I * h, before = wh.omega, rel = wh.omega - spin;
        if (std::fabs(rel) <= dw) rel = 0;
        else rel -= (rel > 0 ? dw : -dw);
        wh.omega = spin + rel;
        // The caliper is bolted to the knuckle: braking twists it (nose dive through the arms).
        // (Smoothed over ~10 ms, like the give in pads and caliper; the total is unchanged.)
        // The drag too: it holds the wheel to the knuckle's turning, so the knuckle takes the
        // reaction (else a housing rocking on its springs would drag its wheels round with
        // nothing pushing back, and never settle).
        double Tbrake = (before - wh.omega) * wh.I / h;
        wh.brakeReaction += (Tbrake - wh.brakeReaction) * std::min(1.0, h / 0.01);
        if (!wh.detached && std::fabs(wh.brakeReaction) > 1) applyTorque(wh.assembly, axis * wh.brakeReaction);
        wh.angle += wh.omega * h;
    }
    // The drivetrain's gears, solved exactly. The crank (engine, flywheel, gearbox input:
    // inertia Ie; or the converter's turbine) turns at the gear ratio times the transfer
    // case's output, which turns at the mean of the axles' carriers, each at the mean of its
    // two side gears (that's what a differential is). With the center locked the two
    // carriers turn together; a locker holds its two side gears together. The side gears
    // have just moved under the half shafts and the limited-slip diffs, the crank under the
    // engine's torque. These are rigid gears, so take out whatever breaks them, sharing it
    // by inertia (an exact impulse solve). It's what makes an open diff an open diff: the
    // same torque to both sides, so a wheel in the air spins up (revving the engine with it)
    // and the other gets only what that one takes; and the engine's own spinning mass is
    // felt in every gear. With the clutch slipping (or the engine off) only the locks apply.
    {
        const int nw = (int)wheels.size();
        double cf[16] = {0}, cr[16] = {0};  // each side gear's weight in the front and rear carriers
        int nf = 0, nr = 0;
        for (int i = 0; i < nw; i++)
            if (wheelPowered(wheels[i])) (wheels[i].front ? nf : nr)++;
        for (int i = 0; i < nw; i++)
            if (wheelPowered(wheels[i])) {
                if (wheels[i].front) cf[i] = 1.0 / nf;
                else cr[i] = 1.0 / nr;
            }
        const int axlesOn = (nf > 0) + (nr > 0);
        double J[4][17] = {{0}};
        int rows = 0;
        if (engaged && axlesOn) {  // crank - ratio * root = 0, root = mean of the carriers
            J[rows][0] = 1;
            for (int i = 0; i < nw; i++) J[rows][1 + i] = -ratio * (cf[i] + cr[i]) / axlesOn;
            rows++;
        }
        if (centerLocked() && nf > 0 && nr > 0) {  // front carrier - rear carrier = 0
            for (int i = 0; i < nw; i++) J[rows][1 + i] = cf[i] - cr[i];
            rows++;
        }
        for (const Axle& ax : axles) {  // locker: left side gear - right side gear = 0
            if (ax.wheel[0] < 0 || ax.wheel[1] < 0 || diffType(ax.front) != DIFF_LOCKED) continue;
            if (!wheelPowered(wheels[ax.wheel[0]]) || !wheelPowered(wheels[ax.wheel[1]]) || rows >= 4) continue;
            J[rows][1 + ax.wheel[0]] = 1;
            J[rows][1 + ax.wheel[1]] = -1;
            rows++;
        }
        if (rows > 0) {
            double invM[17], v[17];
            invM[0] = 1 / Iin;
            // The crank: where the gears had it at the start of the step, plus the engine's torque.
            double root0 = 0;
            for (int i = 0; i < nw; i++) root0 += (cf[i] + cr[i]) * omega0[i];
            root0 /= std::max(axlesOn, 1);
            v[0] = ratio * root0 + Te * 0.92 / Iin * h;
            for (int i = 0; i < nw; i++) invM[1 + i] = 1 / kSideGearI, v[1 + i] = wheels[i].sideOmega;
            // (J M^-1 J^T) lambda = J v, by Gaussian elimination (up to 4 rows).
            double A[4][5] = {{0}};
            for (int r = 0; r < rows; r++) {
                for (int k = 0; k <= nw; k++) A[r][4] += J[r][k] * v[k];
                for (int c2 = 0; c2 < rows; c2++)
                    for (int k = 0; k <= nw; k++) A[r][c2] += J[r][k] * invM[k] * J[c2][k];
            }
            double lam[4] = {0, 0, 0, 0};
            bool ok = true;
            for (int c2 = 0; c2 < rows && ok; c2++) {
                int piv = c2;
                for (int r = c2 + 1; r < rows; r++)
                    if (std::fabs(A[r][c2]) > std::fabs(A[piv][c2])) piv = r;
                if (std::fabs(A[piv][c2]) < 1e-12) {
                    ok = false;
                    break;
                }
                if (piv != c2)
                    for (int k = 0; k < 5; k++) std::swap(A[piv][k], A[c2][k]);
                for (int r = 0; r < rows; r++) {
                    if (r == c2) continue;
                    const double f = A[r][c2] / A[c2][c2];
                    for (int k = c2; k < 5; k++) A[r][k] -= f * A[c2][k];
                }
            }
            if (ok)
                for (int r = 0; r < rows; r++) lam[r] = A[r][4] / A[r][r];
            for (int i = 0; i < nw; i++) {
                if (!wheelPowered(wheels[i])) continue;
                double d = 0;
                for (int r = 0; r < rows; r++) d += J[r][1 + i] * lam[r];
                wheels[i].sideOmega -= invM[1 + i] * d;
            }
        }
    }
    // The shafts wind up by how far each side gear ran ahead of its hub; what they put
    // through the hubs is what the axle housings feel (torque reactions, next step).
    for (Wheel& wh : wheels) {
        if (wheelPowered(wh)) wh.twist += (wh.sideOmega - wh.omega) * h;
        wh.driveActual = (float)wh.shaftTorque;
        wh.omegaWas = wh.omega;
    }
}

// Half-shaft torsional stiffness (N*m/rad). A ~33 mm, 0.8 m steel shaft is ~12 kN*m/rad
// and yields near 30 deg of twist; the tire's sidewall adds give in series. Scaled to
// the shaft's strength: ~19 deg of wind-up at the torque that snaps it.
double Car::shaftStiffness() const { return spec->shaftLimit > 0 ? 3.0 * spec->shaftLimit : 15000.0; }

void Car::integrate(double h) {
    double maxV2 = 0;
    for (Node& n : nodes) {
        n.v += n.f * (n.invm * h);
        n.p += n.v * h;
        maxV2 = std::max(maxV2, dot(n.v, n.v));
    }
    bool spinning = false;
    for (const Wheel& wh : wheels) spinning |= std::fabs(wh.omega) > 0.2;
    if (maxV2 < 0.03 * 0.03 && !spinning) stillTime += h;
    else stillTime = 0;
}

void Car::updateBounds() {
    bbMin = bbMax = nodes[0].p;
    for (const Node& n : nodes) {
        bbMin = V3(std::min(bbMin.x, n.p.x), std::min(bbMin.y, n.p.y), std::min(bbMin.z, n.p.z));
        bbMax = V3(std::max(bbMax.x, n.p.x), std::max(bbMax.y, n.p.y), std::max(bbMax.z, n.p.z));
    }
    const double m = 0.15 + (wheels.empty() ? 0.25 : wheels[0].R);
    bbMin -= V3(m, m, m);
    bbMax += V3(m, m, m);
}

void Car::updateTris() {
    for (Tri& t : tris) {
        const V3& a = nodes[t.v[0]].p;
        const V3& b = nodes[t.v[1]].p;
        const V3& c = nodes[t.v[2]].p;
        t.nrm = norm(cross(b - a, c - a));
        t.torn = len(b - a) > 1.8 * t.rest[0] || len(c - b) > 1.8 * t.rest[1] || len(a - c) > 1.8 * t.rest[2];
    }
}

void Car::updateStats() {
    double sum = 0;
    int n = 0;
    brokenBeams = 0;
    for (const Beam& b : beams) {
        // The structure: body and cabin, frame members (and a ladder frame's brackets), and
        // the rubber mounts that hold the engine and the body on (a torn mount is damage).
        // Scaled by the body's size alone: a ladder frame's hundreds of members add their
        // damage on top, rather than thinning out the body's.
        if (b.type != BEAM_BODY && b.type != BEAM_CABIN && b.type != BEAM_FRAME && b.type != BEAM_MOUNT) continue;
        n += b.type == BEAM_BODY || b.type == BEAM_CABIN;
        if (b.broken) {
            sum += 1;
            brokenBeams++;
        } else if (b.type == BEAM_MOUNT) {
            sum += std::min(1.0, std::fabs(b.L0 - b.Lorig) / 0.04);  // rubber: ~4 cm of set and it's done
        } else {
            sum += std::min(1.0, std::fabs(b.L0 - b.Lorig) / b.Lorig * 8);
        }
    }
    damage = n ? std::min(1.0, sum / n) : 0;

    for (Wheel& wh : wheels) {
        bool attached = false;
        for (int bi : wh.mounts) attached |= !beams[bi].broken;
        wh.detached = !attached;
    }

    // Engine: a crushed bay (radiator, ancillaries) and torn mounts both hurt it.
    double crush = 0;
    for (int bi : bayBeams) {
        const Beam& b = beams[bi];
        crush += b.broken ? 0.3 : std::fabs(b.L0 - b.Lorig) / b.Lorig;
    }
    if (!bayBeams.empty()) crush /= bayBeams.size();
    int mountsBroken = 0;
    for (int bi : engineMounts) mountsBroken += beams[bi].broken;
    double mountLoss = engineMounts.empty() ? 0 : (double)mountsBroken / engineMounts.size();
    engineHealth = std::min(engineHealth, clampd(1.0 - crush * 3.2 - mountLoss * 0.8, 0, 1));
    if (engineHealth < 0.12) engineDead = true;

    // Chassis frame: anything torn off the main structure stops being held by it.
    if (!chassis.nodes.empty()) {
        static thread_local std::vector<int> parent;
        parent.resize(frameNodes);
        for (int i = 0; i < frameNodes; i++) parent[i] = i;
        auto find = [&](int x) {
            while (parent[x] != x) x = parent[x] = parent[parent[x]];
            return x;
        };
        for (const Beam& b : beams)
            if (!b.broken && b.a < frameNodes && b.b < frameNodes && b.type != BEAM_SHAFT) {
                int ra = find(b.a), rb = find(b.b);
                if (ra != rb) parent[ra] = rb;
            }
        // Each shape-holding group (the body; a ladder frame) keeps only its largest piece:
        // whatever has been torn off it goes its own way.
        static thread_local std::vector<double> compMass;
        for (ChassisFrame* cf : {&chassis, &railFrame[0], &railFrame[1]}) {
            if (cf->nodes.empty()) continue;
            compMass.assign(frameNodes, 0);
            for (int i : cf->nodes) compMass[find(i)] += nodes[i].m;
            int main = (int)(std::max_element(compMass.begin(), compMass.end()) - compMass.begin());
            for (size_t k = 0; k < cf->nodes.size(); k++) cf->attached[k] = find(cf->nodes[k]) == main;
        }
    }

    // Panels: a pin lets go when the sheet metal around it is crushed (clips first, then
    // latches, hinges only when it's mangled).
    if (!pins.empty()) {
        static thread_local std::vector<float> bent;
        bent.assign(nodes.size(), 0.0f);
        for (const Beam& b : beams) {
            if (b.type != BEAM_PANEL) continue;
            float pl = b.broken ? 1.0f : (float)(std::fabs(b.L0 - b.Lorig) / b.Lorig);
            bent[b.a] = std::max(bent[b.a], pl);
            bent[b.b] = std::max(bent[b.b], pl);
        }
        for (Pin& pn : pins) {
            if (pn.broken) continue;
            double limit = pn.kind == PIN_CLIP ? 0.08 : pn.kind == PIN_LATCH || pn.kind == PIN_SEAL ? 0.15 : 0.30;
            if (bent[pn.panel] > limit) pn.broken = true;
        }
    }
    // Open once the latches have gone but a hinge holds; off when nothing holds.
    for (Panel& p : panels) {
        int hinges = 0, hingesLeft = 0, othersLeft = 0, holding = 0;
        for (int pi : p.pins) {
            const Pin& pn = pins[pi];
            if (pn.kind == PIN_SEAL) continue;  // seals don't hold a door shut
            holding++;
            if (pn.kind == PIN_HINGE) {
                hinges++;
                hingesLeft += !pn.broken;
            } else {
                othersLeft += !pn.broken;
            }
        }
        if (hingesLeft + othersLeft == 0) p.state = 2;
        else if (hinges > 0 && othersLeft == 0) p.state = 1;
        else if (hinges == 0 && othersLeft * 3 < holding) p.state = 1;  // hanging by a clip or two
        else p.state = 0;
        if (p.state != 0)  // an open door's frame swings away from its seal
            for (int pi : p.pins)
                if (pins[pi].kind == PIN_SEAL) pins[pi].broken = true;
        if (p.state == 2)  // torn off: nothing left to stop it
            for (int pi : p.checks) pins[pi].broken = true;
    }

    // Windows: glass panels shatter when their frame is bent or stretched.
    for (Tri& t : tris) {
        if (t.mat != MAT_GLASS || t.shattered) continue;
        const V3& a = nodes[t.v[0]].p;
        const V3& b = nodes[t.v[1]].p;
        const V3& c = nodes[t.v[2]].p;
        double e[3] = {len(b - a), len(c - b), len(a - c)};
        // A bonded, laminated windshield cracks once its frame bends ~5%; side and back
        // glass float in rubber channels and take more racking before they burst.
        const V3 rn = cross(restLocal[t.v[1]] - restLocal[t.v[0]], restLocal[t.v[2]] - restLocal[t.v[0]]);
        const bool windshield = std::fabs(rn.z) < 0.5 * len(rn) && restLocal[t.v[0]].x > 0;  // faces forward (however raked)
        const double limit = windshield ? 0.05 : 0.075;
        for (int k = 0; k < 3; k++)
            if (std::fabs(e[k] / t.rest[k] - 1) > limit) {
                t.shattered = true;
                glassBursts.push_back((a + b + c) / 3.0);
                break;
            }
    }
}

bool Car::finite() const {
    for (const Node& n : nodes)
        if (!std::isfinite(n.p.x) || !std::isfinite(n.p.y) || !std::isfinite(n.p.z)) return false;
    return true;
}

// =============================================================================
// Sim

void Sim::setup() {
    const auto& cat = vehicleCatalog();
    cars.clear();
    ropes.clear();
    cars.reserve(kMaxBodies);
    colorIndex = 0;
    // You in the Civic, the others parked beside you (on the proving ground's pad, or at
    // the mountain's trailhead).
    auto at = [&](const V3& p) { return V3(p.x, world.map == MAP_PROVING_GROUND ? 0.02 : world.clearY(p.x, p.z) + 0.05, p.z); };
    addCar(cat[0], at(world.spawn), world.spawnYaw, nextColor());
    for (size_t k = 0; k < world.parking.size() && k + 1 < cat.size(); k++)
        addCar(cat[k + 1], at(world.parking[k]), world.parkingYaw, nextColor());
    for (size_t i = 1; i < cars.size(); i++) cars[i].handbrake = 1;
    player = 0;
    if (world.crashProps) placeProps();
}

void Sim::setupEmpty() {
    cars.clear();
    ropes.clear();
    cars.reserve(kMaxBodies);
    colorIndex = 0;
    player = 0;
}

void Sim::placeProps() {
    // A pyramid of foam crash boxes in the lane left of spawn.
    const V3 orange(0.95, 0.52, 0.12), white(0.92, 0.92, 0.88);
    int n = 0;
    for (int row = 0; row < 4; row++)
        for (int k = 0; k < 4 - row; k++, n++)
            addCar(crashBoxSpec(), V3(60, 0.05 + row * 1.04, -15.6 + 1.04 * k + 0.52 * row), 0, n % 2 ? white : orange);
    // A line of concrete barriers.
    for (int k = 0; k < 6; k++) addCar(barrierSpec(), V3(56 + 2.1 * k, 0.05, 25), 0, V3(0.70, 0.70, 0.67));
}

int Sim::vehicleCount() const {
    int n = 0;
    for (const Car& c : cars) n += !c.isProp();
    return n;
}

int Sim::nextVehicle(int from) const {
    for (int k = 1; k <= (int)cars.size(); k++) {
        int i = (from + k) % (int)cars.size();
        if (!cars[i].isProp() && !cars[i].remote) return i;
    }
    return from;
}

V3 Sim::nextColor() {
    const int n = sizeof(kPalette) / sizeof(kPalette[0]);
    return kPalette[colorIndex++ % n];
}

void Sim::addCar(const VehicleSpec& vs, const V3& pos, double yaw, const V3& color) {
    cars.emplace_back();
    // Vehicles modelled on one particular car wear its paint.
    V3 paint = vs.paintColor[0] >= 0 ? V3(vs.paintColor[0], vs.paintColor[1], vs.paintColor[2]) : color;
    cars.back().build(vs, pos, yaw, paint);
}

int Sim::advance(double realDt, int maxSubsteps) {
    if (paused) return 0;
    updateTrail(realDt * timeScale);
    accum += realDt * timeScale;
    int n = 0;
    while (accum >= h && n < maxSubsteps) {
        substep();
        accum -= h;
        n++;
    }
    if (n == maxSubsteps) accum = 0;  // can't keep up: slow down rather than spiral
    wrapAround();
    return n;
}

void Car::translate(const V3& d) {
    for (Node& n : nodes) n.p += d;
    for (Wheel& wh : wheels) wh.contact += d;
    bbMin += d;
    bbMax += d;
    Autopilot& ap = pilot;
    for (V3& p : ap.path) p += d;
    for (V3& p : ap.avoid) p += d;
    for (V3& p : ap.line) p += d;
    for (auto& o : ap.options)
        for (V3& p : o) p += d;
    for (PilotSeen& sn : ap.seen) sn.at += d;
    ap.goal += d;
    ap.lastPos += d;
}

void Sim::wrapAround() {
    // The freeway repeats every wrapPeriod: once you're half a period out, everything moves
    // back a period, onto the same-looking stretch, and nothing can tell. (Not with other
    // players: their machines wouldn't move with us.)
    const double P = world.wrapPeriod;
    if (P <= 0 || player < 0 || player >= (int)cars.size()) return;
    for (const Car& c : cars)
        if (c.remote) return;
    const double x = cars[player].frame().com.x;
    const double dx = x > P / 2 ? -P : x < -P / 2 ? P : 0;
    if (dx == 0) return;
    const V3 d(dx, 0, 0);
    for (Car& c : cars) c.translate(d);
    for (Rope& r : ropes) {
        if (r.a.kind != ANCHOR_CAR) r.a.p += d;
        if (r.b.kind != ANCHOR_CAR) r.b.p += d;
    }
    wrapShift += dx;
}

static bool boundsOverlap(const Car& a, const Car& b) {
    return a.bbMin.x <= b.bbMax.x && b.bbMin.x <= a.bbMax.x && a.bbMin.y <= b.bbMax.y && b.bbMin.y <= a.bbMax.y &&
           a.bbMin.z <= b.bbMax.z && b.bbMin.z <= a.bbMax.z;
}

void Sim::substep() {
    stepCount++;
    const int N = (int)cars.size();

    // Anything moving that touches a sleeping body wakes it up. (A neighbor that is
    // awake but already still doesn't, or stacked boxes would keep waking each other.)
    for (int i = 0; i < N; i++)
        for (int j = i + 1; j < N; j++) {
            Car& a = cars[i];
            Car& b = cars[j];
            if (a.remote || b.remote || a.sleeping == b.sleeping || !boundsOverlap(a, b)) continue;
            const Car& awake = a.sleeping ? b : a;
            if (awake.stillTime < 0.5) {
                a.wake();
                b.wake();
            }
        }

    for (Car& c : cars) {
        if (c.remote) {  // another player's: moved by the network, it only pushes back
            for (Node& n : c.nodes) n.f = V3();
            continue;
        }
        // Someone working a parked car's pedals or wheel wakes it too. (The player's car and
        // the AI's wake themselves every frame; a car driven any other way, by a script or a
        // test, stayed frozen where it had settled however hard its throttle was pressed.)
        if (c.sleeping) {
            const double now[4] = {c.throttle, c.brake, c.handbrake, c.steer};
            for (int k = 0; k < 4; k++)
                if (std::fabs(now[k] - c.sleptControls[k]) > 0.01) c.wake();
        }
        if (!c.sleeping) c.computeForces(h, world);
    }

    for (int i = 0; i < N; i++)
        for (int j = i + 1; j < N; j++)
            // Sleeping bodies still push back (they act as static supports); the forces
            // they receive are discarded until they wake.
            if (!(cars[i].sleeping && cars[j].sleeping) && boundsOverlap(cars[i], cars[j])) collide(cars[i], cars[j]);

    if (!ropes.empty()) ropeForces();

    for (int i = 0; i < N; i++) {
        Car& c = cars[i];
        if (c.remote) {  // coasts along on the velocities its owner last sent
            for (Node& n : c.nodes) n.p += n.v * h;
            c.updateBounds();
            continue;
        }
        if (c.sleeping) continue;
        c.integrate(h);
        c.updateBounds();
        if (i != player && c.stillTime > 1.5) {
            c.sleeping = true;
            c.sleptControls[0] = c.throttle, c.sleptControls[1] = c.brake;
            c.sleptControls[2] = c.handbrake, c.sleptControls[3] = c.steer;
            for (Node& n : c.nodes) {
                n.v = V3();
                n.scrape = 0;
            }
            for (Wheel& wh : c.wheels) wh.omega = 0;
        }
    }
}

void Sim::collide(Car& A, Car& B) {
    pointsVsSkin(A, B);
    pointsVsSkin(B, A);
}

// Pushes A's nodes (and wheels) out of B's skin with penalty forces.
void Sim::pointsVsSkin(Car& A, Car& B) {
    if (B.grid->stamp != stepCount) {
        B.updateTris();
        B.grid->clear();
        const V3 m(0.45, 0.45, 0.45);
        for (int ti = 0; ti < (int)B.tris.size(); ti++) {
            const Tri& t = B.tris[ti];
            if (t.torn) continue;
            V3 mn = B.nodes[t.v[0]].p, mx = mn;
            for (int c = 1; c < 3; c++) {
                const V3& p = B.nodes[t.v[c]].p;
                mn = V3(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
                mx = V3(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
            }
            B.grid->insert(ti, mn - m, mx + m);
        }
        B.grid->stamp = stepCount;
    }

    const double kK = 4e5 * std::min(A.spec->contactScale, B.spec->contactScale), kC = 700, kCt = 2000, kMu = 0.45;
    struct Hit {
        int tri = -1;
        double pen = 0, w[3];
    };
    // Which of B's faces to push a point out through. Among faces it's barely into (under
    // 3 cm: resting contact) take the deepest, which is steady for a box on a box's corner.
    // If it's deeper than that on every side, take the nearest: a node that has just
    // touched a barrier's front face mustn't be shoved out through the top, 30 cm above.
    auto findHit = [&](const V3& p, double radius, Hit& hit) {
        Hit nearest;
        nearest.pen = 1e9;
        for (int ti : B.grid->query(p)) {
            const Tri& t = B.tris[ti];
            if (t.torn) continue;
            const V3& a = B.nodes[t.v[0]].p;
            double d = dot(p - a, t.nrm);
            double pen = radius - d;
            if (pen <= 0 || d < -0.4) continue;
            const bool shallow = pen < radius + 0.03;
            if (shallow ? pen <= hit.pen : pen >= nearest.pen) continue;
            V3 e0 = B.nodes[t.v[1]].p - a, e1 = B.nodes[t.v[2]].p - a, q = p - a;
            double d00 = dot(e0, e0), d01 = dot(e0, e1), d11 = dot(e1, e1), d20 = dot(q, e0), d21 = dot(q, e1);
            double den = d00 * d11 - d01 * d01;
            if (den < 1e-12) continue;
            double v = (d11 * d20 - d01 * d21) / den, ww = (d00 * d21 - d01 * d20) / den, u = 1 - v - ww;
            if (u < -0.02 || v < -0.02 || ww < -0.02) continue;
            u = std::max(u, 0.0);
            v = std::max(v, 0.0);
            ww = std::max(ww, 0.0);
            double s = u + v + ww;
            Hit& h = shallow ? hit : nearest;
            h.tri = ti;
            h.pen = pen;
            h.w[0] = u / s;
            h.w[1] = v / s;
            h.w[2] = ww / s;
        }
        if (hit.tri < 0 && nearest.tri >= 0) hit = nearest;
        return hit.tri >= 0;
    };
    // Applies the contact force; the point's force is shared between pts[0..np).
    auto apply = [&](const Hit& hit, const V3& pv, Node* pts[], int np) {
        const Tri& t = B.tris[hit.tri];
        V3 vb;
        for (int k = 0; k < 3; k++) vb += B.nodes[t.v[k]].v * hit.w[k];
        V3 vrel = pv - vb;
        double mMin = pts[0]->m * np;
        for (int k = 0; k < 3; k++) mMin = std::min(mMin, B.nodes[t.v[k]].m);
        double vn = dot(vrel, t.nrm);
        double Fn = kK * hit.pen - std::min(kC, 0.4 * mMin / h) * vn;
        if (Fn <= 0) return;
        Fn = std::min(Fn, mMin * 5000.0);  // cap: no single contact may fling a node (deep overlaps)
        V3 vt = vrel - t.nrm * vn;
        double vtl = len(vt);
        V3 F = t.nrm * Fn;
        if (vtl > 1e-6) F -= vt * (std::min(kMu * Fn, std::min(kCt, 0.4 * mMin / h) * vtl) / vtl);
        for (int i = 0; i < np; i++) pts[i]->f += F / (double)np;
        for (int k = 0; k < 3; k++) B.nodes[t.v[k]].f -= F * hit.w[k];
        double e = std::fabs(vn) * Fn * h;
        A.impactEnergy += e * 0.5;
        B.impactEnergy += e * 0.5;
    };
    auto inBounds = [&](const V3& p) {
        return p.x >= B.bbMin.x && p.x <= B.bbMax.x && p.y >= B.bbMin.y && p.y <= B.bbMax.y && p.z >= B.bbMin.z &&
               p.z <= B.bbMax.z;
    };

    for (Node& n : A.nodes) {
        if (!n.collide || !inBounds(n.p)) continue;
        Hit hit;
        if (findHit(n.p, 0.0, hit)) {
            Node* pts[1] = {&n};
            apply(hit, n.v, pts, 1);
        }
    }
    for (Wheel& wh : A.wheels) {
        Node& a = A.nodes[wh.inner];
        Node& b = A.nodes[wh.outer];
        V3 c = (a.p + b.p) * 0.5;
        if (!inBounds(c)) continue;
        Hit hit;
        if (findHit(c, wh.R * 0.9, hit)) {
            Node* pts[2] = {&a, &b};
            apply(hit, (a.v + b.v) * 0.5, pts, 2);
        }
    }

    // Interior repulsion. Skin faces stop most contact, but once two lattices have
    // crumpled into each other the faces fold away. Any node that gets close to one of
    // B's *interior* nodes (not on the skin) is inside B, so push it back out. Skin
    // nodes are left alone so touching bodies don't hover apart.
    const double rr = 0.36;  // contact distance between node centers
    if (B.nodeGridStamp != stepCount) {
        B.nodeGrid->clear();
        for (int i = 0; i < B.bodyNodes; i++) {
            if (!B.nodeTris[i].empty()) continue;  // skin node
            const V3& p = B.nodes[i].p;
            B.nodeGrid->insert(i, p - V3(rr, rr, rr), p + V3(rr, rr, rr));
        }
        B.nodeGridStamp = stepCount;
    }
    for (Node& na : A.nodes) {
        if (!na.collide || !inBounds(na.p)) continue;
        for (int j : B.nodeGrid->query(na.p)) {
            Node& nb = B.nodes[j];
            V3 d = na.p - nb.p;
            double dist2 = dot(d, d);
            if (dist2 >= rr * rr || dist2 < 1e-12) continue;
            double dist = std::sqrt(dist2);
            V3 nrm = d / dist;
            double m = std::min(na.m, nb.m);
            double vn = dot(na.v - nb.v, nrm);
            double Fn = 2e5 * (rr - dist) - std::min(400.0, 0.4 * m / h) * vn;
            if (Fn <= 0) continue;
            Fn = std::min(Fn, m * 3000.0);
            V3 F = nrm * Fn;
            na.f += F;
            nb.f -= F;
        }
    }
}

int Sim::nearestRoadIndex(const V3& p, int hint) const {
    const auto& road = world.loopRoad;
    const int n = (int)road.size();
    auto d2 = [&](int i) {
        double dx = road[i].x - p.x, dz = road[i].z - p.z;
        return dx * dx + dz * dz;
    };
    int best = 0;
    double bd = 1e18;
    if (hint < 0) {
        for (int i = 0; i < n; i++)
            if (d2(i) < bd) {
                bd = d2(i);
                best = i;
            }
        return best;
    }
    for (int k = -40; k <= 40; k++) {
        int i = ((hint + k) % n + n) % n;
        if (d2(i) < bd) {
            bd = d2(i);
            best = i;
        }
    }
    return best;
}

// ---- AI driver ---------------------------------------------------------------
//
// Steering: pure pursuit toward a point in the right-hand lane, further ahead at speed.
// Speed: the lower of (a) the fastest speed from which every bend ahead can be taken
// and (b) an IDM car-following law behind whatever is ahead in our lane.
// Recovery: back up and try again when stuck; give up once on the roof.

void Sim::updateAI(double dt) {
    if (world.loopRoad.empty()) return;
    bool any = false;
    for (const Car& c : cars) any |= c.ai && !c.isProp();
    if (!any) return;
    // Where every body is along the road, computed once per frame.
    aiFrames.resize(cars.size());
    aiAlong.assign(cars.size(), 0);
    aiLat.assign(cars.size(), 1e9);
    const auto& road = world.loopRoad;
    const int n = (int)road.size();
    for (size_t i = 0; i < cars.size(); i++) {
        Car& o = cars[i];
        aiFrames[i] = o.frame();
        const V3& p = aiFrames[i].com;
        V3 near = road[o.aiIndex >= 0 ? o.aiIndex : 0];
        bool farAway = o.aiIndex >= 0 && (p.x - near.x) * (p.x - near.x) + (p.z - near.z) * (p.z - near.z) > 400 * 400;
        o.aiIndex = nearestRoadIndex(p, farAway ? -1 : o.aiIndex);
        int k = o.aiIndex, kn = (k + 1) % n;
        V3 t = norm(V3(road[kn].x - road[k].x, 0, road[kn].z - road[k].z));
        aiAlong[i] = world.loopAlong[k];
        aiLat[i] = (p.x - road[k].x) * -t.z + (p.z - road[k].z) * t.x;  // right of the +1 direction
    }
    for (size_t i = 0; i < cars.size(); i++)
        if (cars[i].ai && !cars[i].isProp()) aiDrive((int)i, dt);
}

void Sim::aiDrive(int self, double dt) {
    Car& c = cars[self];
    const VehicleSpec* spec = c.spec;
    const auto& road = world.loopRoad;
    const int n = (int)road.size();
    auto wrap = [&](int i) { return ((i % n) + n) % n; };
    auto tangent = [&](int i) {  // road direction at sample i, in our direction of travel
        V3 d = road[wrap(i + c.aiDir)] - road[i];
        return norm(V3(d.x, 0, d.z));
    };
    const double kLane = 2.6;
    const Frame& fr = aiFrames[self];
    const double speed = len(fr.vel);
    c.wake();
    c.handbrake = 0;

    // On its roof or side: nothing sensible left to do.
    if (fr.up.y < 0.3) {
        c.throttle = 0;
        c.brake = 1;
        c.steer = 0;
        return;
    }

    // Backing up to get unstuck.
    if (c.aiReverseTime > 0) {
        c.aiReverseTime -= dt;
        c.gear = -1;
        c.throttle = 0.6;
        c.brake = 0;
        c.steer = -c.aiSteerAtStuck;
        if (c.aiReverseTime <= 0) c.gear = 1;
        return;
    }

    // ---- What's around us ---------------------------------------------------
    // Offsets are measured to the right of our direction of travel; aiLane is where
    // we want to be (+2.6 = our lane, -2.6 = the oncoming lane while passing).
    const double L = world.loopLength;
    auto aheadOf = [&](size_t o) {  // road distance from us to body o, in our direction
        return std::fmod((aiAlong[o] - aiAlong[self]) * c.aiDir + 2 * L, L);
    };
    auto inLane = [&](size_t o, double lane) {
        const Car& other = cars[o];
        double halfW = 0.3 * std::max(other.bbMax.x - other.bbMin.x, other.bbMax.z - other.bbMin.z);
        return std::fabs(aiLat[o] * c.aiDir - lane) < 1.3 + halfW;
    };
    double gap = 1e9, leadSpeed = 0;
    for (size_t o = 0; o < cars.size(); o++) {
        if ((int)o == self || !inLane(o, c.aiLane)) continue;
        double ahead = aheadOf(o);
        if (ahead > 120) continue;
        double g = ahead - (spec->length + cars[o].spec->length) * 0.5;
        if (g < gap) {
            gap = g;
            leadSpeed = dot(aiFrames[o].vel, tangent(c.aiIndex));
        }
    }

    // ---- Passing a stopped obstacle -------------------------------------------
    double progress = 0;
    if (c.aiLastAlong >= 0) {
        progress = (aiAlong[self] - c.aiLastAlong) * c.aiDir;
        if (progress < -L / 2) progress += L;
        if (progress > L / 2) progress -= L;
    }
    c.aiLastAlong = aiAlong[self];
    auto oncomingClear = [&](double range) {
        for (size_t o = 0; o < cars.size(); o++) {
            if ((int)o == self || !inLane(o, -kLane)) continue;
            double ahead = aheadOf(o);
            if (ahead < range || ahead > L - 15) return false;
        }
        return true;
    };
    if (!c.aiPassing) {
        bool blocked = gap < 15 && leadSpeed < 1.0 && speed < 1.5;
        c.aiBlockedTime = blocked ? c.aiBlockedTime + dt : 0;
        if (c.aiBlockedTime > 2.5 && oncomingClear(90)) {
            c.aiPassing = true;
            c.aiPassLeft = std::max(gap, 0.0) + spec->length + 14;
            c.aiBlockedTime = 0;
        }
    } else {
        c.aiPassLeft -= progress;
        bool alongside = c.aiPassLeft < spec->length + 10;
        if (c.aiPassLeft <= 0 || (!alongside && !oncomingClear(40))) c.aiPassing = false;
    }
    double laneTarget = c.aiPassing ? -kLane : kLane;
    c.aiLane += clampd(laneTarget - c.aiLane, -1.8 * dt, 1.8 * dt);

    // ---- Steering: Stanley controller at the front axle -----------------------
    V3 front = fr.com + fr.fwd * (c.wheelbase * 0.5);
    int fi = nearestRoadIndex(front, c.aiIndex);
    V3 t = tangent(fi), right(-t.z, 0, t.x);
    double crossTrack = dot(front - road[fi], right) - c.aiLane;  // + = too far right
    double headingErr = std::atan2(dot(t, fr.left), dot(t, fr.fwd));
    V3 t2 = tangent(wrap(fi + 6 * c.aiDir));
    double segLen = 0;
    for (int m = 0; m < 6; m++) segLen += len(road[wrap(fi + (m + 1) * c.aiDir)] - road[wrap(fi + m * c.aiDir)]);
    double curvature = std::asin(clampd(t.z * t2.x - t.x * t2.z, -1, 1)) / std::max(segLen, 1.0);  // + = bends left
    // Far off the line it steers back harder (ramping in, so it doesn't hunt at a threshold).
    const double offLine = std::fabs(crossTrack);
    const double gain = 1.6 + 0.9 * clampd((offLine - 1.0) / 2.0, 0, 1);
    // A car that understeers settles a little wide of the line on a long bend; a slow,
    // leaky integral of the offset trims that out.
    c.aiSteerTrim += (clampd(crossTrack, -2.0, 2.0) * 0.05 - c.aiSteerTrim * 0.1) * dt;
    c.aiSteerTrim = clampd(c.aiSteerTrim, -0.12, 0.12);
    double steer = headingErr + std::atan2(gain * crossTrack, speed + 1.5) + std::atan(c.wheelbase * curvature) + c.aiSteerTrim;
    c.steer = clampd(steer, -0.55, 0.55);
    c.steerInput = c.steer / 0.6;

    // ---- Speed: bends ahead, then car following ------------------------------
    // Tall, heavy vehicles corner more gently.
    // (A tube-frame buggy's 40 in tires and long travel top out at ~0.6 g: it drives the loop gently.)
    const double height = spec->layerY.back(), aLat = std::min(clampd(6.5 - 3.0 * (height - 1.4) - (c.mass - 1300) / 800, 3.2, 5.5), spec->openFrame && spec->wheelRadius > 0.45 ? 3.3 : 9.0);
    const double decel = aLat * 0.75;
    auto bendSpeed = [&](int from) {  // max speed for the ~10 m of road starting at `from`
        double seg = 0;
        int j = from;
        for (int m = 0; m < 5; m++) {
            int jn = wrap(j + c.aiDir);
            seg += len(road[jn] - road[j]);
            j = jn;
        }
        double turn = std::acos(clampd(dot(tangent(from), tangent(j)), -1, 1));
        return turn > 1e-3 ? std::sqrt(aLat * seg / turn) : 1e9;
    };
    double vTarget = std::min(c.aiCruise, bendSpeed(c.aiIndex)), dist = 0;
    // Recover at a speed the tires can steer at. Accelerating hard with full lock
    // on grass used to push the front-drive car along the shoulder indefinitely.
    // The limit eases off smoothly as it gets back to its lane: a hard on/off threshold
    // made it brake, drift out, and brake again just short of the lane.
    {
        const double back = clampd((6.0 - offLine) / 5.0, 0, 1);
        vTarget = std::min(vTarget, 5.0 + 14.0 * back * back);
    }
    if (c.aiPassing) vTarget = std::min(vTarget, 9.0);  // pass slowly
    int i = c.aiIndex;
    for (int k = 1; k <= 60; k++) {
        int nx = wrap(i + c.aiDir);
        dist += len(road[nx] - road[i]);
        i = nx;
        if (k % 2 == 0) {
            double vb = bendSpeed(i);
            vTarget = std::min(vTarget, std::sqrt(vb * vb + 2 * decel * dist));
        }
    }

    // IDM acceleration toward vTarget while keeping a time gap to the leader.
    const double aMax = 2.5, bComf = 3.0, headway = 1.4, minGap = 4.0;
    double v0 = std::max(vTarget, 0.1);
    double accel = aMax * (1 - std::pow(speed / v0, 4));
    if (gap < 1e8) {
        double sStar = minGap + std::max(0.0, speed * headway + speed * (speed - leadSpeed) / (2 * std::sqrt(aMax * bComf)));
        accel -= aMax * (sStar / std::max(gap, 0.5)) * (sStar / std::max(gap, 0.5));
    }
    if (c.gear < 1) c.gear = 1;
    c.throttle = clampd(accel / 3.0, 0, 1);
    c.brake = clampd(-accel / 7.0, 0, 1);
    c.aiGap = gap;
    // A driver feels a tire spinning away (an open diff, an unloaded inside wheel, no
    // traction control) and feathers the pedal.
    {
        double spin = 0;
        for (const Wheel& w : c.wheels)
            if (c.wheelPowered(w)) spin = std::max(spin, std::fabs(w.omega * w.rollR()) - std::fabs(w.groundSpeed));
        c.throttle *= clampd(1 - (spin - 2.0) / 4.0, 0.3, 1.0);
    }

    // Stuck (nose in a wall or ditch while trying to drive, or rolling back down a slope
    // with a wheel spinning): back up with the wheel turned the other way, then try again.
    if ((speed < 0.6 || dot(fr.vel, fr.fwd) < -0.3) && c.throttle > 0.25) c.aiStuckTime += dt;
    else c.aiStuckTime = 0;
    if (c.aiStuckTime > 3.0) {
        c.aiStuckTime = 0;
        c.aiReverseTime = 2.0;
        c.aiSteerAtStuck = c.steer;
    }
}

void Sim::drive(bool up, bool down, bool left, bool right, bool hb, double dt) {
    for (int i = 0; i < (int)cars.size(); i++) {
        if (i == player || cars[i].ai || cars[i].remote) continue;
        Car& o = cars[i];
        if (o.pilot.active) {  // left on autopilot: keeps going
            pilotStep(i, dt);
            continue;
        }
        o.throttle = o.brake = o.steerInput = o.steer = 0;
        o.handbrake = 1;
        // On a tow strap or another truck's winch line: off the brakes and rolling, steered
        // to follow the line (the buddy in the seat).
        for (const Rope& r : ropes) {
            const bool isA = r.a.kind == ANCHOR_CAR && r.a.car == i, isB = r.b.kind == ANCHOR_CAR && r.b.car == i;
            if (!isA && !isB) continue;
            const RopeEnd& other = isA ? r.b : r.a;
            if (other.kind != ANCHOR_CAR || (r.kind == ROPE_WINCH && isA)) continue;
            o.handbrake = 0;
            const Frame fr = o.frame();
            const V3 to = cars[other.car].hookPoint(other.hook) - o.hookPoint(isA ? r.a.hook : r.b.hook);
            const int hook = isA ? r.a.hook : r.b.hook;
            const double sgn = hook == 0 ? 1 : -1;  // towed backwards: steer the other way
            o.steer = clampd(std::atan2(dot(to, fr.left), std::max(sgn * dot(to, fr.fwd), 0.5)) * sgn * 0.8, -0.5, 0.5);
            if (len(fr.vel) < 0.3 && r.tension < 500) o.brake = 0.2;  // parked on slack: hold it
        }
    }
    updateAI(dt);
    Car& c = cars[player];
    c.wake();
    if (c.ai) return;  // following the loop road
    if (c.pilot.active) {
        if (up || down || left || right) {  // taking the wheel switches it off
            c.pilot.active = false;
            c.pilot.followCar = -1;
            if (c.gear < 0) c.gear = 1;
            events.push_back("AUTOPILOT OFF: YOU HAVE THE WHEEL");
        } else {
            pilotStep(player, dt);
            return;
        }
    }
    Frame fr = c.frame();
    double speed = len(fr.vel), vf = dot(fr.vel, fr.fwd);

    double target = (left ? 1.0 : 0.0) - (right ? 1.0 : 0.0);
    // (Hands get gentler as the speed climbs: past ~150 km/h a tap is a smaller input. Letting
    // go is as quick as ever.)
    double rate = (target == 0 || target * c.steerInput < 0) ? 5.0 : 2.5 * std::min(1.0, 40.0 / std::max(1.0, speed));
    c.steerInput += clampd(target - c.steerInput, -rate * dt, rate * dt);
    // Keys are all or nothing, so full key steers to what the tires can hold at this
    // speed (a driver's hands would): the road-wheel angle for ~85% of the car's grip on
    // this surface (downforce included), plus the tires' slip, plus however far the car is
    // already sliding so there's lock left to catch it. At parking speeds that's full lock;
    // at 130 km/h a few degrees.
    {
        double grip = 0;
        int n = 0;
        for (const Wheel& wh : c.wheels)
            if (wh.front && wh.load > 0) grip += surfaceGrip(wh.surface), n++;
        grip = n ? grip / n : 1.0;
        double df = c.spec->downforce * vf * vf;
        if (c.spec->downforceMax > 0) df = std::min(df, c.spec->downforceMax);
        const double aMax = 9.81 * c.spec->tireGrip * c.spec->lateralGrip * grip * (1 + df / std::max(1.0, c.mass * 9.81)) * 0.85;
        // (The extra lock for a slide only when steering into it, to catch it: not to wind
        // on more of the turn that's already sliding.)
        const double beta = std::atan2(dot(fr.vel, fr.left), std::max(1.0, std::fabs(vf)));
        const double catchLock = c.steerInput * beta > 0 ? std::fabs(beta) : 0.0;
        const double limit = std::atan(c.wheelbase * aMax / std::max(1.0, speed * speed)) + 0.045 + catchLock;
        c.steer = c.steerInput * std::min(0.6, limit);
    }

    c.throttle = c.brake = 0;
    c.handbrake = hb ? 1 : 0;
    if (c.gear >= 1) {
        if (up) c.throttle = 1;
        if (down) {
            if (vf > 0.8) c.brake = 1;
            else {
                c.gear = -1;
                c.throttle = 1;
            }
        }
    } else {
        if (down) c.throttle = 1;
        if (up) {
            if (vf < -0.8) c.brake = 1;
            else {
                c.gear = 1;
                c.throttle = 1;
            }
        }
    }
}

void Sim::pilotStep(int i, double dt) {
    Car& c = cars[i];
    Autopilot& ap = c.pilot;
    // Following someone: the goal moves with them (plan again now and then).
    if (ap.followCar >= 0) {
        if (ap.followCar >= (int)cars.size() || ap.followCar == i) {
            ap.active = false;
            events.push_back("AUTOPILOT: LOST THE VEHICLE IT WAS FOLLOWING");
            return;
        }
        const V3 g = cars[ap.followCar].frame().com;
        ap.replanTime -= dt;
        if (ap.replanTime <= 0 && len(g - ap.goal) > 8) {
            ap.replanTime = 3;
            ap.goal = g;
            std::vector<V3> np;
            if (planRoute(world, *c.spec, c.frame().com, g, ap.route, ap.avoid, np)) ap.path = np, ap.idx = 0;
        }
    }
    const bool was = ap.active;
    const int mode0 = c.driveMode;
    const bool lf0 = c.lockFront, lr0 = c.lockRear, sway0 = c.swayDisconnected, air0 = ap.airTarget > 0;
    pilotDrive(c, world, dt, &cars);
    if (i != player) return;
    if (was && !ap.active && ap.arrived) events.push_back("AUTOPILOT: " + ap.status);
    // Say what it changed on the truck.
    if (c.driveMode != mode0 && c.mode()) events.push_back(std::string("AUTOPILOT: SHIFTED TO ") + c.mode()->name);
    if (c.lockRear != lr0) events.push_back(c.lockRear ? "AUTOPILOT: REAR LOCKER ON" : "AUTOPILOT: REAR LOCKER OFF");
    if (c.lockFront != lf0) events.push_back(c.lockFront ? "AUTOPILOT: FRONT LOCKER ON" : "AUTOPILOT: FRONT LOCKER OFF");
    if (c.swayDisconnected != sway0 && c.spec->swayDisconnect == 2)
        events.push_back(c.swayDisconnected ? "AUTOPILOT: SWAY BAR PINS PULLED" : "AUTOPILOT: SWAY BAR PINS BACK IN");
    if (!air0 && ap.airTarget > 0) events.push_back("AUTOPILOT: AIRING DOWN FOR THE ROCKS");
}

void Sim::respawnPlayer() {
    Car& c = cars[player];
    dropRopes(player);
    c.respawn(c.spawnPos, c.spawnYaw);
    if (!c.isProp()) c.setDown(world);
    accum = 0;
}

void Sim::rightPlayer() {
    Car& c = cars[player];
    dropRopes(player);
    const Frame fr = c.frame();
    double yaw = std::atan2(-fr.fwd.z, fr.fwd.x);
    if (!std::isfinite(yaw)) yaw = 0;
    c.placeUpright(world, fr.com.x, fr.com.z, yaw);
}

void Sim::towPlayerHome() {
    Car& c = cars[player];
    dropRopes(player);
    c.placeUpright(world, c.spawnPos.x, c.spawnPos.z, c.spawnYaw);
    accum = 0;
}

void Sim::recoverPlayer() {
    Car& c = cars[player];
    dropRopes(player);
    Frame fr = c.frame();
    V3 sp = c.spawnPos;
    double sy = c.spawnYaw;
    double yaw = std::atan2(-fr.fwd.z, fr.fwd.x);
    if (!std::isfinite(yaw)) yaw = 0;
    c.respawn(V3(fr.com.x, world.clearY(fr.com.x, fr.com.z) + 0.05, fr.com.z), yaw);
    c.setDown(world);
    c.spawnPos = sp;  // R still returns to the original spawn
    c.spawnYaw = sy;
}

void Sim::swapPlayerVehicle(const VehicleSpec& vs) {
    Car& c = cars[player];
    c.spec = &vs;
    recoverPlayer();
}

bool Sim::spawnAhead(const VehicleSpec& vs) {
    if (vehicleCount() >= kMaxVehicles || (int)cars.size() >= kMaxBodies) return false;
    Frame fr = cars[player].frame();
    V3 flat = norm(V3(fr.fwd.x, 0, fr.fwd.z));
    if (len(flat) < 0.5) flat = V3(1, 0, 0);
    V3 p = fr.com + flat * (cars[player].spec->length * 0.5 + vs.length * 0.5 + 4.0);
    double yaw = std::atan2(-flat.z, flat.x) + M_PI / 2;  // parked across your path
    addCar(vs, V3(p.x, world.clearY(p.x, p.z) + 0.05, p.z), yaw, nextColor());
    cars.back().handbrake = 1;
    return true;
}

bool Sim::spawnTraffic(const VehicleSpec& vs) {
    if (vehicleCount() >= kMaxVehicles || (int)cars.size() >= kMaxBodies || world.loopRoad.empty()) return false;
    const auto& road = world.loopRoad;
    const int n = (int)road.size();
    int idx = nearestRoadIndex(cars[player].frame().com, -1);
    int dir = trafficDir;
    trafficDir = -trafficDir;  // alternate directions so traffic comes both ways
    // Start 40 m from the nearest point, on the far side from whatever is already there.
    double acc = 0;
    while (acc < 40) {
        int nx = ((idx + dir) % n + n) % n;
        acc += len(road[nx] - road[idx]);
        idx = nx;
    }
    V3 d = road[((idx + dir) % n + n) % n] - road[idx];
    V3 t = norm(V3(d.x, 0, d.z));
    V3 p = road[idx] + V3(-t.z, 0, t.x) * 2.6;
    double yaw = std::atan2(-t.z, t.x);
    addCar(vs, V3(p.x, world.clearY(p.x, p.z) + 0.05, p.z), yaw, nextColor());
    Car& c = cars.back();
    c.ai = true;
    c.aiDir = dir;
    c.aiIndex = idx;
    return true;
}

void Sim::removePlayerCar() {
    if (vehicleCount() <= 1) return;
    carRemoved(player);
    cars.erase(cars.begin() + player);
    player = nextVehicle(player - 1 < 0 ? (int)cars.size() - 1 : player - 1);
    cars[player].wake();
}
