// Clipping probe: harsh events, and how far parts pass into each other.
//   clip VEHICLE EVENT    (event: 0 wall 50, 1 t-boned at 50, 2 5 m drop on rock, 3 roll, 4 2 m tail drop, 5 door torn: swing)
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <map>

struct Probe {
    double panelIn = 0, housingInRail = 0, housingAboveRail = 0, bodyInRail = 0, railInTire = 0, tireTire = 0, armInRail = 0;
    std::vector<double> bodyRest;  // per body node: depth inside a rail at rest
};

// Depth of point p inside the rail box between stations st, st+1 of side s (0 if outside).
static double railDepth(const Car& c, int st, int s, const V3& p) {
    auto N = [&](int k, int corner) { return c.nodes[c.ladder[k * 8 + s * 4 + corner]].p; };
    V3 a0 = (N(st, 0) + N(st, 1) + N(st, 2) + N(st, 3)) / 4.0, a1 = (N(st + 1, 0) + N(st + 1, 1) + N(st + 1, 2) + N(st + 1, 3)) / 4.0;
    V3 ax = a1 - a0;
    double L = len(ax);
    if (L < 1e-6) return 0;
    ax = ax / L;
    double t = dot(p - a0, ax) / L;
    if (t < 0 || t > 1) return 0;
    auto lerp = [&](int corner) { return N(st, corner) * (1 - t) + N(st + 1, corner) * t; };
    V3 ib = lerp(0), ob = lerp(1), it = lerp(2), ot = lerp(3);
    V3 o = (ib + ob + it + ot) / 4.0;
    V3 u = ((ob + ot) - (ib + it)) * 0.5, v = ((it + ot) - (ib + ob)) * 0.5;
    double wu = len(u), wv = len(v);
    double pu = dot(p - o, u / wu), pv = dot(p - o, v / wv);
    if (std::fabs(pu) > wu / 2 || std::fabs(pv) > wv / 2) return 0;
    return std::min(wu / 2 - std::fabs(pu), wv / 2 - std::fabs(pv));
}
static double inRails(const Car& c, const V3& p) {
    double d = 0;
    for (int s = 0; s < 2; s++)
        for (int st = 0; st + 1 < c.ladderStations; st++) d = std::max(d, railDepth(c, st, s, p));
    return d;
}

static void measure(Car& c, Probe& pr, bool rest) {
    // Panels: a panel node behind (inside) its own panel's body quads.
    for (const Panel& pn : c.panels) {
        std::map<int, int> copy;
        for (size_t k = 0; k < pn.bodyNodes.size(); k++) copy[pn.bodyNodes[k]] = pn.panelNodes[k];
        for (int qi : pn.quads) {
            const SkinQuad& q = c.quads[qi];
            V3 b[4];
            for (int k = 0; k < 4; k++) b[k] = c.nodes[q.v[k]].p;
            V3 ctr = (b[0] + b[1] + b[2] + b[3]) / 4.0;
            V3 nrm = norm(cross(b[1] - b[0], b[3] - b[0]));
            double half = 0.5 * std::max(len(b[2] - b[0]), len(b[3] - b[1]));
            for (int pnode : pn.panelNodes) {
                V3 d = c.nodes[pnode].p - ctr;
                double h = dot(d, nrm);
                if (h > -0.005 || h < -0.25) continue;
                if (len(d - nrm * h) > half * 0.6) continue;
                pr.panelIn = std::max(pr.panelIn, -h);
            }
        }
    }
    if (c.ladderStations == 0) return;
    // Axle housings inside or above the rails.
    for (const Axle& a : c.axles)
        for (int n : a.housing) pr.housingInRail = std::max(pr.housingInRail, inRails(c, c.nodes[n].p));
    // Body tub nodes in the rails, beyond where they sit at rest.
    std::vector<char> isLadder(c.nodes.size(), 0);
    for (int n : c.ladder) isLadder[n] = 1;
    if (rest) pr.bodyRest.assign(c.bodyNodes, 0);
    for (int n = 0; n < c.bodyNodes; n++) {
        if (isLadder[n]) continue;
        double d = inRails(c, c.nodes[n].p);
        if (rest) pr.bodyRest[n] = d;
        else pr.bodyInRail = std::max(pr.bodyInRail, d - pr.bodyRest[n]);
    }
    // Rail corners inside tires.
    for (const Wheel& w : c.wheels) {
        if (w.detached) continue;
        V3 a = c.nodes[w.inner].p, b = c.nodes[w.outer].p, ctr = (a + b) * 0.5, ax = norm(b - a);
        for (int n : c.ladder) {
            V3 d = c.nodes[n].p - ctr;
            double da = dot(d, ax), dr = len(d - ax * da);
            if (std::fabs(da) < w.width / 2 && dr < w.R) pr.railInTire = std::max(pr.railInTire, std::min(w.R - dr, w.width / 2 - std::fabs(da)));
        }
        for (const Wheel& o : c.wheels) {
            if (&o == &w || o.detached) continue;
            V3 oc = (c.nodes[o.inner].p + c.nodes[o.outer].p) * 0.5;
            double gap = len(oc - ctr) - w.R - o.R;
            if (gap < 0 && std::fabs(dot(oc - ctr, ax)) < w.width) pr.tireTire = std::max(pr.tireTire, -gap);
        }
    }
    // Suspension link midpoints in the rails.
    for (const SuspLink& l : c.links) {
        if (l.kind == LINK_BUSHING || l.kind == LINK_STRUT_GUIDE) continue;
        const Beam& b = c.beams[l.beam];
        if (b.broken) continue;
        for (double t : {0.25, 0.5, 0.75}) {
            V3 p = c.nodes[b.a].p * (1 - t) + c.nodes[b.b].p * t;
            pr.armInRail = std::max(pr.armInRail, inRails(c, p));
        }
    }
}

int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 2, ev = argc > 2 ? atoi(argv[2]) : 0;
    Sim sim;
    Ctx ctx(sim);
    const VehicleSpec& spec = vehicleCatalog()[v];
    Probe pr;
    Car* cp = nullptr;
    if (ev == 0) {
        cp = &ctx.add(spec, 100 - spec.length / 2, 0, 0);
    } else if (ev == 1) {
        ctx.add(vehicleCatalog()[3], 0, 30 - 12, -90);
        cp = &ctx.add(spec, 0, 30, 0);
    } else {
        cp = &ctx.add(spec, -100, -50, 0);
    }
    Car& c = *cp;
    ctx.run(1.0);
    measure(c, pr, true);
    Probe base = pr;
    std::printf("rest: panelIn %.3f housingInRail %.3f railInTire %.3f armInRail %.3f\n", pr.panelIn, pr.housingInRail, pr.railInTire, pr.armInRail);
    pr = Probe();
    pr.bodyRest = base.bodyRest;
    double dur = 3.0;
    if (ev == 0) {
        ctx.launch(c, 50 / 3.6);
    } else if (ev == 1) {
        ctx.launch(ctx.sim.cars[0], 50 / 3.6);
    } else if (ev == 2) {
        ctx.sim.world.addBox(V3(-100, ctx.sim.world.height(-100, -50) + 0.05, -50), V3(4, 0.05, 2), 0, 0, 0, V3(0.4, 0.4, 0.4));
        ctx.sim.world.boxes.back().rock = true;
        ctx.sim.world.buildGrid();
        for (Node& n : c.nodes) n.p.y += 5;
    } else if (ev == 3) {
        ctx.launch(c, 60 / 3.6);
        for (Node& n : c.nodes) n.v += V3(0, 3, 0) + cross(c.frame().fwd * 3.5, n.p - c.frame().com);
        dur = 5;
    } else if (ev == 4) {
        c.rotateRigid(c.frame().left, -0.5);
        for (Node& n : c.nodes) n.p.y += 2;
    } else if (ev == 5) {
        for (Panel& pn : c.panels)
            if (std::string(pn.name).find("DOOR") != std::string::npos)
                for (int pi : pn.pins)
                    if (c.pins[pi].kind != PIN_HINGE) c.pins[pi].broken = true;
        ctx.launch(c, 20 / 3.6);
        dur = 4;
    }
    double t0 = ctx.t;
    ctx.run(dur, [&] {
        if (ev == 5) c.steer = std::sin((ctx.t - t0) * 3) * 0.4, c.throttle = 0.2;
        if (ev == 5 && getenv("CHECKS")) {
            for (const Panel& pn : c.panels) {
                if (std::string(pn.name).find("DOOR") == std::string::npos || pn.checks.empty()) continue;
                if (getenv("ALLCHECKS")) {
                    std::printf("t=%.3f %s", ctx.t - t0, pn.name);
                    for (int pi : pn.checks) std::printf(" %.3f%s", len(c.nodes[c.pins[pi].panel].p - c.nodes[c.pins[pi].body].p) - c.pins[pi].slack, c.pins[pi].broken ? "x" : "");
                    int hb = 0; for (int pi : pn.pins) hb += c.pins[pi].kind == PIN_HINGE && !c.pins[pi].broken;
                    std::printf("  hinges %d\n", hb);
                    continue;
                }
                const Pin& p0 = c.pins[pn.checks[0]];
                V3 rel = c.nodes[p0.panel].v - c.nodes[p0.body].v;
                std::printf("t=%.3f %s stretch %.3f broken %d relv %.2f  lat %.2f g\n", ctx.t - t0, pn.name, len(c.nodes[p0.panel].p - c.nodes[p0.body].p) - p0.slack, (int)p0.broken,
                            len(rel), dot(c.frame().vel, c.frame().left) );
            }
        }
        if (ev == 5 && getenv("TRACE")) {
            for (const Panel& pn : c.panels) {
                if (std::string(pn.name) != "LEFT REAR DOOR") continue;
                int k = atoi(getenv("TRACE"));
                const Node& P = c.nodes[pn.panelNodes[k]];
                const SkinQuad& q = c.quads[pn.nodeQuads[k][0]];
                V3 b0 = c.nodes[q.v[0]].p, b1 = c.nodes[q.v[1]].p, b2 = c.nodes[q.v[2]].p, b3 = c.nodes[q.v[3]].p;
                V3 e1 = ((b1 - b0) + (b2 - b3)) * 0.5, e2 = ((b3 - b0) + (b2 - b1)) * 0.5;
                V3 n = norm(cross(e1, e2));
                V3 d = P.p - (b0 + b1 + b2 + b3) * 0.25;
                std::printf("t=%.3f h %.3f s %.2f t %.2f vn %.2f pin %d\n", ctx.t - t0, dot(d, n), dot(d, e1) / dot(e1, e1), dot(d, e2) / dot(e2, e2),
                            dot(P.v - c.nodes[q.v[0]].v, n), (int)c.pins[pn.pins[k]].broken);
            }
        }
        Probe now = pr;
        measure(c, pr, false);
        (void)now;
    });
    if (ev == 5)
        for (const Panel& pn : c.panels) {
            for (int pi : pn.checks)
                std::printf("  %s check pin %d slack %.3f k %.0f broken %d brk %.3f now %.3f  panel nodes %zu pins %zu\n", pn.name, pi, c.pins[pi].slack, c.pins[pi].k, (int)c.pins[pi].broken,
                            c.pins[pi].breakDist, len(c.nodes[c.pins[pi].panel].p - c.nodes[c.pins[pi].body].p), pn.panelNodes.size(), pn.pins.size());
        }
    if (ev == 5)
        for (const Panel& pn : c.panels) {
            if (std::string(pn.name).find("DOOR") == std::string::npos) continue;
            for (size_t k = 0; k < pn.panelNodes.size(); k++) {
                const Node& P = c.nodes[pn.panelNodes[k]];
                double mn = 1e9, mx = -1e9;
                for (int qi : pn.nodeQuads[k]) {
                    const SkinQuad& q = c.quads[qi];
                    V3 b0 = c.nodes[q.v[0]].p, b1 = c.nodes[q.v[1]].p, b2 = c.nodes[q.v[2]].p, b3 = c.nodes[q.v[3]].p;
                    V3 n = norm(cross((b1 - b0) + (b2 - b3), (b3 - b0) + (b2 - b1)));
                    double hh = dot(P.p - (b0 + b1 + b2 + b3) * 0.25, n);
                    mn = std::min(mn, hh), mx = std::max(mx, hh);
                }
                if (mx < -0.02 || mn < -0.05)
                    std::printf("  %s node %zu pin %d kind %d broken %d quads %zu  h min %.3f max %.3f  state %d\n", pn.name, k, pn.pins[k], c.pins[pn.pins[k]].kind,
                                c.pins[pn.pins[k]].broken, pn.nodeQuads[k].size(), mn, mx, pn.state);
            }
        }
    std::printf("%s ev%d: panelIn %.3f  housingInRail %.3f  bodyInRail %.3f  railInTire %.3f  tireTire %.3f  armInRail %.3f  damage %.1f%%\n", spec.name, ev,
                pr.panelIn, pr.housingInRail, pr.bodyInRail, pr.railInTire, pr.tireTire, pr.armInRail, c.damage * 100);
}
