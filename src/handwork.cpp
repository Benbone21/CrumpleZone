#include "handwork.h"

#include <algorithm>
#include <cmath>

#include "mathx.h"
#include "modelkit.h"
#include "walker.h"

namespace {

const V3 kUp(0, 1, 0);

double frac(double x) { return x - std::floor(x); }
V3 lerp(const V3& a, const V3& b, double t) { return a + (b - a) * t; }
V3 flat(const V3& v) {
    V3 f(v.x, 0, v.z);
    return len(f) > 1e-6 ? norm(f) : V3(1, 0, 0);
}

constexpr double kSwing = 0.52;  // s a wrench takes to swing once (a stroke, then back to re-seat)
constexpr double kStroke = 75 * M_PI / 180;  // how far one stroke turns it

double smooth(double x) { return x * x * (3 - 2 * x); }

struct Scene {
    Handwork& out;
    // A hand on the last model put down: `along` m from its origin down its X (its grip, for a tool), the thumb toward its head.
    void hold(int side, double along = 0, double grip = 0.95) {
        if (out.models.empty()) return;
        const Placed& pl = out.models.back();
        out.hands[side].on = true;
        out.hands[side].p = pl.p + pl.ax[0] * (along * pl.scale.x);
        out.hands[side].axis = pl.ax[0];
        out.hands[side].grip = grip;
    }
    // Both hands either side of the last model's middle (`half` m out along its Z), gripping across it.
    void both(double half, double up = 0) {
        if (out.models.empty()) return;
        const Placed& pl = out.models.back();
        for (int s = 0; s < 2; s++) {
            out.hands[s].on = true;
            out.hands[s].p = pl.p + pl.ax[2] * (s == 0 ? -half : half) + pl.ax[1] * up;
            out.hands[s].axis = pl.ax[0] * (s == 0 ? 1.0 : -1.0);
            out.hands[s].grip = 0.7;
        }
    }
    // A model turned so its front is X and its up is (near) Y.
    void put(const char* id, const V3& at, V3 X, V3 Y, double scale = 1) {
        if (!id || !*id || !mk::findModel(id)) return;
        X = norm(X);
        Y = Y - X * dot(Y, X);
        Y = len(Y) > 1e-6 ? norm(Y) : norm(cross(cross(X, kUp), X));
        Placed p;
        p.id = id, p.p = at, p.oriented = true;
        p.ax[0] = X, p.ax[1] = Y, p.ax[2] = cross(X, Y);
        p.scale = V3(scale, scale, scale);
        out.models.push_back(p);
    }
    // A wrench or socket turning a nut or bolt: `top` is the middle of its outer face, `axis` the
    // way the stud points (out of the work, toward you). The tool's working end is its +X end
    // with its socket opening down its -Y (the models' own way), so it goes on with +Y along
    // the axis and the handle (-X) in the plane the stud turns it in. Each swing the handle
    // sweeps ~75 degrees about the stud (off: the way a nut comes undone, counter-clockwise
    // seen from you), then lifts off and goes back to take a fresh bite. `time` is seconds in this job.
    void turn(const char* id, const V3& top, const V3& axis, double time, bool off, const V3& toward) {
        if (!id || !mk::findModel(id)) return;
        const std::string name = id;
        const mk::Mesh& m = mk::modelMesh(name);
        const bool socket = id[0] == 'l' || id[0] == 'r';  // (lug wrench, ratchet: a socket over the nut; a spanner lies on it)
        const double reach = m.hi.x - (id[0] == 'w' ? 0.02 : id[0] == 'r' ? 0.022 : 0.017);
        const double engage = socket ? std::min(0.015, -m.lo.y * 0.5) : 0.0;
        // The plane the handle turns in: "down" in it is gravity's, or toward you for a stud that points up.
        V3 u = kUp - axis * dot(kUp, axis);
        if (len(u) < 0.3) u = toward - axis * dot(toward, axis);
        u = len(u) > 1e-6 ? norm(u) : norm(cross(axis, V3(1, 0, 0)));
        const V3 left = cross(axis, u);  // (left, seen from the stud's tip looking in)
        const double c = frac(time / kSwing), stroke = 0.7;
        out.swing = c;
        const double p = c < stroke ? smooth(c / stroke) : 1 - smooth((c - stroke) / (1 - stroke));
        const double phi = 0.1 + kStroke * (off ? p : 1 - p);
        const double lift = c < stroke ? 0.0 : 0.02 * std::sin((c - stroke) / (1 - stroke) * M_PI);
        const V3 d = left * std::cos(phi) - u * std::sin(phi);  // (from the stud to the grip)
        put(id, top + axis * (-m.lo.y - engage + lift) + d * reach, -d, axis);
        hold(1, 0.02);
    }
    // A wrench turned like a crank: the handle sticks out along the bolt's axis, not in the turning plane.
    // This is the pose for wheel lug nuts (a real lug wrench sits on the nut with its handle along the axle).
    void turnCrank(const char* id, const V3& top, const V3& axis, double time, bool off) {
        if (!id || !mk::findModel(id)) return;
        const std::string name = id;
        const mk::Mesh& m = mk::modelMesh(name);
        const double engage = std::min(0.015, -m.lo.y * 0.5);
        // The wrench's socket (at its +X end) sits on the nut; the handle extends along the bolt axis.
        // As it turns, the whole wrench rotates about the bolt axis.
        const double c = frac(time / kSwing), stroke = 0.7;
        const double p = c < stroke ? smooth(c / stroke) : 1 - smooth((c - stroke) / (1 - stroke));
        const double phi = 0.1 + kStroke * (off ? p : 1 - p);
        const double lift = c < stroke ? 0.0 : 0.02 * std::sin((c - stroke) / (1 - stroke) * M_PI);
        // The wrench is placed with its X axis along the bolt axis, socket at the nut.
        V3 along = axis;  // (the handle sticks out this way: the axle line)
        V3 side = flat(V3(1, 0, 0));
        if (std::fabs(dot(along, side)) > 0.9) side = flat(V3(0, 1, 0));
        side = side - along * dot(side, along);
        side = len(side) > 1e-6 ? norm(side) : V3(1, 0, 0);
        // Rotate the wrench around the bolt axis by the turn angle.
        const V3 rSide = rotateAxis(side, along, phi);
        const V3 rUp = rotateAxis(cross(along, side), along, phi);
        put(id, top + along * (-m.lo.y - engage + lift), rSide, along);
        hold(1, 0.02);
    }
    // A hammer tapping at `at`, swung from your side (toward is from `at` to you).
    void tap(const char* id, const V3& at, const V3& toward, double phase) {
        if (!id || !mk::findModel(id)) return;
        const double lift = 0.12 + 0.9 * std::pow(std::max(0.0, std::sin(phase)), 2), reach = mk::modelMesh(id).hi.x;
        const V3 grip = at + toward * (reach * std::cos(lift)) + kUp * (reach * std::sin(lift));
        put(id, grip, at - grip, kUp);
        hold(1, 0.05);
    }
    // A bar levering at `at`.
    void lever(const char* id, const V3& at, const V3& toward, double phase) {
        if (!id || !mk::findModel(id)) return;
        const V3 grip = at + norm(toward * 0.42 + kUp * (0.22 + 0.1 * std::sin(phase))) * mk::modelMesh(id).hi.x;
        put(id, grip, at - grip, kUp);
        hold(1, 0.04);
        hold(0, std::min(0.3, mk::modelMesh(id).hi.x * 0.35));
    }
};

V3 hubOf(const Car& c, const Wheel& wh) { return (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5; }

// The nuts on a wheel as the renderer draws them (its rim, built for a 0.32 m tire and
// 0.24 m width, and turned as drawWheels turns it): how many, and where the face of nut i is.
// `item`: the wheel on (its style and size). `carried`: the wheel in your arms, going on (spin 0, turned
// about its up).
struct Lugs {
    const Car& car;
    const Wheel& wh;
    const WheelItem& item;
    bool carried;
    V3 out;
    int n = 5;
    double top = 0.072;
    Lugs(const Car& c, const Wheel& w, const WheelItem& it, bool carriedWheel) : car(c), wh(w), item(it), carried(carriedWheel) {
        out = norm(c.nodes[w.outer].p - c.nodes[w.inner].p);
        switch (it.style) {
        case WHEEL_OFFROAD_BRONZE:
        case WHEEL_OFFROAD_BLACK: n = 6, top = 0.092; break;
        case WHEEL_HONDA_COVER: n = 4, top = 0.072; break;
        case WHEEL_FORGED: n = 1, top = 0.072; break;
        case WHEEL_STEEL: n = 5, top = 0.086; break;  // (under the dog dish: the tool goes on the cap)
        default: break;
        }
    }
    V3 at(int i) const {
        V3 al = out, up = car.frame().up;
        double ang = wh.angle;
        if (carried) up = V3(0, 1, 0), ang = 0;
        else if (!wh.left) al = -al;
        V3 f0 = norm(cross(al, up));
        if (len(f0) < 0.5) f0 = norm(cross(al, V3(1, 0, 0)));
        const V3 u0 = cross(f0, al);
        const double a = 2 * M_PI * (i + 0.5) / n, rad = (carried ? item.R : wh.R) / 0.32, axial = (carried ? item.width : wh.width) / 0.24;
        const V3 e1 = rotateAxis(f0, al, ang), e2 = rotateAxis(u0, al, ang);
        return hubOf(car, wh) + (e1 * std::cos(a) + e2 * std::sin(a)) * (0.04 * rad) + out * (top * axial);
    }
    V3 hubFace() const { return hubOf(car, wh) + out * (top * (carried ? item.width : wh.width) / 0.24); }
};

const char* toolId(Tool t) { return toolModel(t); }

// Which nut next, in a star: 0 2 4 1 3 for five, in turn for the rest.
int starNut(int visit, int n) { return n % 2 ? visit * 2 % n : visit % n; }

}  // namespace

Handwork handwork(const Sim& sim, const Walker& wk, const Interact& ia, const V3& eye, const V3& look, double time) {
    Handwork h;
    Scene sc{h};
    const V3 F = norm(look), R = norm(cross(F, kUp)), U = cross(R, F);
    const InteractOption& o = ia.current;
    const bool working = ia.progress > 0 && o.kind != IA_NONE && !o.blocked;
    const double t = o.continuous ? 0 : ia.share();
    const double secs = ia.progress;  // s into the job
    const Car* car = o.car >= 0 && o.car < (int)sim.cars.size() ? &sim.cars[o.car] : nullptr;
    const Wheel* wh = car && o.wheel >= 0 && o.wheel < (int)car->wheels.size() ? &car->wheels[o.wheel] : nullptr;
    const V3 toward = flat(eye - o.at);

    // What you carry while you walk.
    // (Your hand on the jack's bar near its top; the chain held in both.)
    auto jackGrip = [&](const Placed& pl) {
        if (!mk::findModel("jack")) return;
        const mk::Mesh& m = mk::modelMesh("jack");
        h.hands[1].on = true;
        h.hands[1].p = pl.p + pl.ax[0] * (0.5 * (m.lo.x + m.hi.x)) + pl.ax[1] * (m.hi.y - 0.12) + pl.ax[2] * (0.5 * (m.lo.z + m.hi.z));
        h.hands[1].axis = pl.ax[1];
        h.hands[1].grip = 1;
    };
    if (wk.holdingJack && !(working && o.kind == IA_PLACE_JACK)) {
        sc.put("jack", eye + flat(F) * 0.55 + R * 0.1 - kUp * 1.05, flat(F), kUp);
        if (!h.models.empty() && h.models.back().id == std::string("jack")) jackGrip(h.models.back());
    }
    if (wk.holdingChain && !(working && o.kind == IA_FIT_CHAIN)) {
        sc.put("chain_bundle", eye + flat(F) * 0.5 + R * 0.05 - kUp * 0.75, flat(F), kUp);
        sc.both(0.12, 0.05);
    }
    // A tool, part or anchor out of the back (trunk.h): held out in front of the right hand.
    if (wk.hand.any() && !working) {
        const char* id = trunk::model(wk.hand);
        if (id && *id && mk::findModel(id)) {
            const mk::Mesh& m = mk::modelMesh(id);
            const double L = std::max({m.hi.x - m.lo.x, m.hi.y - m.lo.y, m.hi.z - m.lo.z, 0.05});
            const double k = clampd(0.4 / L, 0.35, 2.0), bob = 0.01 * std::sin(time * 1.7);
            const V3 grip = eye + F * 0.46 + R * 0.22 - U * 0.33 + kUp * bob;  // (low in the right of the view)
            sc.put(id, grip, flat(F) * 0.95 + U * 0.3 - R * 0.15, kUp, k);
            h.models.back().bare = true;  // (an anchor without the soil, snow or rock it's shown set in)
            // In your right hand: a tool by its grip (its origin), anything else by its middle.
            if (wk.hand.kind == TK_TOOL) sc.hold(1, 0.03);
            else {
                const Placed& pl = h.models.back();
                const V3 mid = (m.lo + m.hi) * 0.5;
                h.hands[1].on = true, h.hands[1].grip = 0.75, h.hands[1].axis = pl.ax[0];
                h.hands[1].p = pl.p + (pl.ax[0] * mid.x + pl.ax[1] * (m.lo.y * 0.4 + mid.y * 0.6) + pl.ax[2] * mid.z) * k;
            }
        }
    }
    // The winch line or a strap end in your right hand (the game holds it there: Sim::handLine), the line running back
    // through the fist to its drum.
    if (sim.handLine.on() && sim.handLine.car < (int)sim.cars.size()) {
        h.hands[1].on = true, h.hands[1].grip = 1;
        h.hands[1].p = sim.handLine.hand;
        h.hands[1].axis = norm(sim.cars[sim.handLine.car].hookPoint(sim.handLine.hook) - sim.handLine.hand);
    }
    // Reaching for a line (the fairlead's hook, a strap's end, a hooked line's end, the drum to stow it): the right hand
    // goes out to it, opening, and closes on it.
    if (working && (o.kind == IA_GRAB_LINE || o.kind == IA_UNHOOK_LINE || o.kind == IA_STOW_LINE) && std::isfinite(o.at.x)) {
        const V3 rest = eye + flat(F) * 0.35 + R * 0.2 - kUp * 0.55;
        const double s = smooth(std::min(1.0, t * 1.7));
        h.hands[1].on = true;
        h.hands[1].p = lerp(rest, o.at + toward * 0.03, s);
        h.hands[1].axis = norm(lerp(flat(F), kUp, s));
        h.hands[1].grip = s < 0.85 ? 0.15 : 0.15 + (s - 0.85) / 0.15 * 0.85;
    }
    if (!working) return h;

    switch (o.kind) {
    case IA_REMOVE_WHEEL:
    case IA_FIT_WHEEL: {
        if (!car || !wh) break;
        const bool off = o.kind == IA_REMOVE_WHEEL;
        const V3 a = norm(car->nodes[wh->outer].p - car->nodes[wh->inner].p);
        // Off: the nuts (0..0.9), then the wheel slides off its studs. On: the wheel goes on
        // (0..0.12), then the nuts, each with about two swings.
        const double n0 = off ? 0 : 0.12, n1 = off ? 0.9 : 1.0;
        if (!off) {
            h.carriedPose = true;
            const double s = std::min(1.0, t / n0);
            const V3 from = eye + F * 0.6 - kUp * 0.75;
            h.carriedC = lerp(from, hubOf(*car, *wh) + a * (0.25 * (1 - s)), smooth(s));
            h.carriedAxis = norm(lerp(-flat(F), a, s));
        }
        if (t >= n0 && t < n1) {
            const bool carriedWheel = !off && wk.carrying;
            const Lugs lugs(*car, *wh, carriedWheel ? *wk.carrying : wh->item, carriedWheel);
            const double span = (n1 - n0) * o.hold, per = span / lugs.n, local = (t - n0) * o.hold;
            const int visit = std::min(lugs.n - 1, (int)(local / per));
            const double within = local - visit * per;
            const double swings = std::max(1.0, std::round(per / kSwing));  // (whole swings, so each nut ends on a reset)
            sc.turnCrank(toolId(TOOL_LUG_WRENCH), lugs.at(starNut(visit, lugs.n)), a, within / per * swings * kSwing, off);
        } else if (off && t >= n1) {
            h.wheel.car = o.car, h.wheel.wheel = o.wheel, h.wheel.out = (t - n1) / (1 - n1) * 0.16;
        }
        break;
    }
    case IA_FIT_CHAIN:
    case IA_REMOVE_CHAIN: {
        if (!car || !wh) break;
        h.wheel.car = o.car, h.wheel.wheel = o.wheel;
        h.wheel.chain = o.kind == IA_FIT_CHAIN ? t : 1 - t;
        // The chain's bundle comes up to the top of the tread as it's wrapped round.
        const V3 top = hubOf(*car, *wh) + kUp * (wh->R * 1.02) + norm(car->nodes[wh->outer].p - car->nodes[wh->inner].p) * (wh->width * 0.3);
        if (o.kind == IA_FIT_CHAIN && mk::findModel("chain_bundle")) {
            sc.put("chain_bundle", lerp(eye + F * 0.4 + R * 0.2 - kUp * 0.55, top, 0.3), flat(F), kUp);
            sc.both(0.12, 0.05);
        }
        break;
    }
    case IA_RAISE_JACK:
    case IA_LOWER_JACK:
        break;  // (the jack itself goes up and down, drawn from the car's state)
    case IA_PLACE_JACK: {
        if (!car || !wh) break;
        // From your arms to its place under the corner, turning handle-out as it goes.
        Placed pl = Interact::jackModel(sim, *car, o.wheel);
        const V3 from = eye + flat(F) * 0.55 + R * 0.1 - kUp * 1.05;
        const double s = smooth(t);
        pl.p = lerp(from, pl.p, s);
        const V3 x = norm(lerp(flat(F), pl.ax[0], s));
        pl.ax[0] = x, pl.ax[1] = kUp, pl.ax[2] = cross(x, kUp);
        for (auto& kv : pl.set) kv.second *= s;
        h.models.push_back(pl);
        if (s < 0.85) jackGrip(pl);  // (let go as it settles)
        break;
    }
    case IA_FIX: {
        const Fix& f = o.fix;
        // At a corner the work's done at the hub (the axle nut, the knuckle behind it), where you
        // can see it, the tool coaxial with it; anywhere else where the step is (a plug points up,
        // a shaft's ends). One step, one tool, worked slowly for the whole hold; or the new part going in.
        V3 work = o.at, workAxis = toward;
        if (f.kind == Fix::SPARK_PLUGS) workAxis = kUp;
        if (car && wh && !o.atEnd) {
            WheelItem item = wh->item;
            if (!wh->mounted) item.style = car->spec->wheelStyle;
            const Lugs lugs(*car, *wh, item, false);
            work = lugs.hubFace();
            workAxis = lugs.out;
        }
        if (o.part >= 0) {
            // The new part, from outboard of the work into place along the stud.
            const double s = smooth(std::min(1.0, t / 0.8));
            V3 along = R;
            if (car && wh) along = norm(car->nodes[wh->outer].p - car->nodes[wh->inner].p);
            const V3 at = lerp(work + norm(workAxis + kUp * 0.3) * 0.45, work, s);
            sc.put(repairPartModel((RepairPart)o.part), at, along, kUp);
            if (s < 0.95) sc.both(0.1);
        } else if (o.tool >= 0) {
            const Tool tool = (Tool)o.tool;
            switch (tool) {
            case TOOL_HAMMER: sc.tap(toolId(tool), work, flat(workAxis), secs / 0.8 * M_PI * 2); break;
            case TOOL_PRY_BAR:
            case TOOL_TIRE_IRONS: sc.lever(toolId(tool), work, flat(workAxis), secs / 1.4 * M_PI * 2); break;
            default: {  // (undoing comes before the part, doing up after it)
                bool on = false;
                if (car) {
                    const std::vector<RepairStep> st = repairSteps(f.kind, jobJacks(f.kind, f.wheel), jobWheelOff(*car, f.kind, f.wheel));
                    for (int i = 0; i < o.step && i < (int)st.size(); i++) on |= st[i].part >= 0;
                }
                sc.turn(toolId(tool), work, workAxis, secs * 0.75, !on, toward);
                break;
            }
            }
        }  // (no tool: the engine cranked over on the starter)
        break;
    }
    case IA_TIRE_WORK: {
        if (o.loose < 0 || o.loose >= (int)sim.looseWheels.size()) break;
        const LooseWheel& lw = sim.looseWheels[o.loose];
        const V3 rim = lw.p + kUp * (lw.item.rimR * 0.9);  // (the bead at the top of the rim)
        if (o.part >= 0) {
            const double s = smooth(std::min(1.0, t / 0.8));
            sc.put(repairPartModel((RepairPart)o.part), lerp(rim + toward * 0.4 + kUp * 0.3, lw.p, s), lw.axis(), kUp);
            if (s < 0.95) sc.both(0.1);
        } else if (o.tool >= 0) {
            sc.lever(toolId((Tool)o.tool), rim, toward, secs / 1.4 * M_PI * 2);
        }
        break;
    }
    case IA_OPEN_CACHE:
        sc.lever(toolId(TOOL_PRY_BAR), o.at + kUp * 0.1, toward, secs / 1.1 * M_PI * 2);
        break;
    default: break;
    }
    return h;
}
