#include "handwork.h"

#include <algorithm>
#include <cmath>

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

struct Scene {
    Handwork& out;
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
    // A tool held by its grip (its working end along X), the hand round the grip.
    void held(const char* id, const V3& grip, const V3& X, const V3& Y) {
        put(id, grip, X, Y);
        put("fp_hand", grip, X, Y);
    }
    // A wrench turning something at `at` about `axis` (toward you): the handle swinging to and
    // fro below it, `reach` m long.
    void turn(const char* id, const V3& at, const V3& axis, double reach, double swing) {
        if (id && mk::findModel(id)) reach = std::max(0.08, mk::modelMesh(id).hi.x - 0.03);  // (its working end on the nut)
        V3 u = kUp - axis * dot(kUp, axis);
        u = len(u) > 1e-6 ? norm(u) : norm(cross(axis, V3(1, 0, 0)));
        const V3 v = cross(axis, u);
        const V3 d = -(u * std::cos(swing) + v * std::sin(swing));  // (from the nut to the grip: down, give or take)
        held(id, at + d * reach + axis * 0.03, -d, axis);
    }
    // A hammer tapping at `at`, swung from your side (toward is from `at` to you).
    void tap(const char* id, const V3& at, const V3& toward, double phase) {
        const double lift = 0.12 + 0.9 * std::pow(std::max(0.0, std::sin(phase)), 2);
        const V3 grip = at + toward * (0.3 * std::cos(lift)) + kUp * (0.3 * std::sin(lift));
        held(id, grip, at - grip, kUp);
    }
    // A bar levering at `at`.
    void lever(const char* id, const V3& at, const V3& toward, double phase) {
        const V3 grip = at + toward * 0.42 + kUp * (0.22 + 0.1 * std::sin(phase));
        held(id, grip, at - grip, kUp);
    }
};

V3 hubOf(const Car& c, const Wheel& wh) { return (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5; }

// Where lug nut i of 5 sits on a hub's face.
V3 lugAt(const Car& c, const Wheel& wh, int i) {
    const V3 hub = hubOf(c, wh), a = norm(c.nodes[wh.outer].p - c.nodes[wh.inner].p);
    V3 u = kUp - a * dot(kUp, a);
    u = norm(u);
    const V3 v = cross(a, u);
    const double ang = 2 * M_PI * i / 5 + 0.3, r = 0.075 * wh.R / 0.36;
    return hub + a * (wh.width * 0.5 + 0.02) + (u * std::cos(ang) + v * std::sin(ang)) * r;
}

const char* toolId(Tool t) { return toolModel(t); }

}  // namespace

Handwork handwork(const Sim& sim, const Walker& wk, const Interact& ia, const V3& eye, const V3& look, double time) {
    Handwork h;
    Scene sc{h};
    const V3 F = norm(look), R = norm(cross(F, kUp)), U = cross(R, F);
    const V3 rest = eye + F * 0.42 + R * 0.2 - U * 0.26;  // where your right hand is
    const InteractOption& o = ia.current;
    const bool working = ia.progress > 0 && o.kind != IA_NONE && !o.blocked;
    const double t = o.continuous ? 0 : ia.share();
    const Car* car = o.car >= 0 && o.car < (int)sim.cars.size() ? &sim.cars[o.car] : nullptr;
    const Wheel* wh = car && o.wheel >= 0 && o.wheel < (int)car->wheels.size() ? &car->wheels[o.wheel] : nullptr;
    const V3 toward = flat(eye - o.at);

    // What you carry while you walk.
    if (wk.holdingJack && !(working && o.kind == IA_PLACE_JACK)) sc.put("jack", eye + flat(F) * 0.55 + R * 0.1 - kUp * 1.05, flat(F), kUp);
    if (wk.holdingChain && !(working && o.kind == IA_FIT_CHAIN)) sc.put("chain_bundle", eye + flat(F) * 0.5 + R * 0.05 - kUp * 0.75, flat(F), kUp);
    // A tool, part or anchor out of the back (trunk.h): held out in front of the right hand.
    if (wk.hand.any() && !working) {
        const char* id = trunk::model(wk.hand);
        if (id && *id && mk::findModel(id)) {
            const mk::Mesh& m = mk::modelMesh(id);
            const double L = std::max({m.hi.x - m.lo.x, m.hi.y - m.lo.y, m.hi.z - m.lo.z, 0.05});
            const double k = clampd(0.4 / L, 0.35, 2.0), bob = 0.01 * std::sin(time * 1.7);
            const V3 grip = eye + F * 0.5 + R * 0.2 - U * 0.24 + kUp * bob;
            sc.put(id, grip + (R * 0.0), flat(F) * 0.95 + U * 0.3 - R * 0.15, kUp, k);
        }  // (no hand drawn: just the thing)
    }
    if (!working) return h;

    switch (o.kind) {
    case IA_REMOVE_WHEEL:
    case IA_FIT_WHEEL: {
        if (!car || !wh) break;
        const bool off = o.kind == IA_REMOVE_WHEEL;
        const V3 a = norm(car->nodes[wh->outer].p - car->nodes[wh->inner].p);
        // Off: the five nuts (0..0.78), then the wheel slides off its studs. On: the wheel goes
        // on (0..0.4), then the nuts.
        const double n0 = off ? 0 : 0.4, n1 = off ? 0.78 : 1.0;
        if (!off) {
            h.carriedPose = true;
            const double s = std::min(1.0, t / 0.4);
            const V3 from = eye + F * 0.6 - kUp * 0.75;
            h.carriedC = lerp(from, hubOf(*car, *wh) + a * (0.25 * (1 - s)), s * s * (3 - 2 * s));
            h.carriedAxis = norm(lerp(-flat(F), a, s));
        }
        if (t >= n0 && t < n1) {
            const double k = (t - n0) / (n1 - n0) * 5;
            const int i = std::min(4, (int)k);
            const double s = frac(k);
            sc.turn(toolId(TOOL_LUG_WRENCH), lugAt(*car, *wh, i), a, 0.3, std::sin(s * M_PI * 4) * 0.45);
            if (off && s > 0.75) sc.put("lug_nut", lugAt(*car, *wh, i) + a * (0.05 * (s - 0.75) / 0.25), a, kUp);
        } else if (off && t >= n1) {
            h.wheel.car = o.car, h.wheel.wheel = o.wheel, h.wheel.out = (t - n1) / (1 - n1) * 0.16;
            const V3 grip = hubOf(*car, *wh) + a * (h.wheel.out + wh->width * 0.5) + kUp * (wh->R * 0.75);
            sc.put("fp_hand", grip, -a, kUp);
        }
        break;
    }
    case IA_FIT_CHAIN:
    case IA_REMOVE_CHAIN: {
        if (!car || !wh) break;
        h.wheel.car = o.car, h.wheel.wheel = o.wheel;
        h.wheel.chain = o.kind == IA_FIT_CHAIN ? t : 1 - t;
        const V3 a = norm(car->nodes[wh->outer].p - car->nodes[wh->inner].p);
        // Hands over the top of the tread, feeding it round.
        const V3 top = hubOf(*car, *wh) + kUp * (wh->R * 1.02) + a * (wh->width * 0.3) + toward * (0.08 * std::sin(t * M_PI * 6));
        sc.put("fp_hand", top, -toward, kUp);
        if (o.kind == IA_FIT_CHAIN && mk::findModel("chain_bundle")) sc.put("chain_bundle", lerp(rest - kUp * 0.3, top, 0.3), flat(F), kUp);
        break;
    }
    case IA_RAISE_JACK:
    case IA_LOWER_JACK: {
        if (!car) break;
        // Working the jack's handle up and down.
        const V3 spot = Interact::jackSpot(sim, *car, o.wheel);
        const double pump = std::sin(time * (o.kind == IA_RAISE_JACK ? 9.0 : 5.0));
        const V3 grip = spot + toward * 0.45 + kUp * (0.55 + 0.12 * pump);
        sc.put("fp_hand", grip, (spot + kUp * 0.35) - grip, kUp);
        break;
    }
    case IA_PLACE_JACK: {
        if (!car) break;
        const V3 spot = Interact::jackSpot(sim, *car, o.wheel);
        const V3 from = eye + flat(F) * 0.55 + R * 0.1 - kUp * 1.05;
        const double s = t * t * (3 - 2 * t);
        sc.put("jack", lerp(from, spot, s), flat(lerp(F, -toward, s)), kUp);
        break;
    }
    case IA_FIX: {
        const Fix& f = o.fix;
        const int n = std::max(1, (int)f.tools.size());
        // At a corner the work's done at the hub (the axle nut, the knuckle behind it), where you
        // can see it; anywhere else where the fix is.
        V3 work = o.at, workAxis = toward;
        if (car && wh) {
            const V3 a = norm(car->nodes[wh->outer].p - car->nodes[wh->inner].p);
            work = hubOf(*car, *wh) + a * (wh->width * 0.5 + 0.03) - kUp * (wh->R * 0.12);
            workAxis = a;
        }
        if (t < 0.7) {
            const double k = t / 0.7 * n;
            const Tool tool = f.tools[std::min(n - 1, (int)k)];
            const double s = frac(k);
            switch (tool) {
            case TOOL_HAMMER: sc.tap(toolId(tool), work, flat(workAxis), s * M_PI * 2 * 4); break;
            case TOOL_PRY_BAR:
            case TOOL_TIRE_IRONS: sc.lever(toolId(tool), work, flat(workAxis), s * M_PI * 2 * 2); break;
            default: sc.turn(toolId(tool), work, workAxis, 0.2, std::sin(s * M_PI * 5) * 0.5); break;
            }
        } else {
            // The new part, from your hand into place.
            const double s = (t - 0.7) / 0.3;
            const V3 at = lerp(rest, work, s * s * (3 - 2 * s));
            V3 along = R;
            if (car && wh) along = norm(car->nodes[wh->outer].p - car->nodes[wh->inner].p);
            sc.put(repairPartModel(f.part), at, along, kUp);
            sc.put("fp_hand", at - along * 0.05, along, kUp);
        }
        break;
    }
    case IA_OPEN_CACHE:
        sc.lever(toolId(TOOL_PRY_BAR), o.at + kUp * 0.1, toward, t * M_PI * 2 * 2);
        break;
    default: break;
    }
    return h;
}
