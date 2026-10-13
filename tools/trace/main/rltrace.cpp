#define private public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>

// Usage: trace rest <vehicleIdx> | trace ledge <vehicleIdx>
static double frameSet(const Car& c, int* worstIdx) {
    double worst = 0; *worstIdx = -1;
    for (size_t i = 0; i < c.beams.size(); i++) {
        const Beam& b = c.beams[i];
        if (b.type != BEAM_FRAME) continue;
        double s = b.broken ? 1.0 : std::fabs(b.L0 - b.Lorig) / b.Lorig;
        if (s > worst) worst = s, *worstIdx = (int)i;
    }
    return worst * 100;
}
int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "rest";
    int vi = argc > 2 ? atoi(argv[2]) : 3;
    const VehicleSpec& spec = vehicleCatalog()[vi];
    Sim sim;
    Ctx ctx(sim);
    if (!strcmp(mode, "rest")) {
        Car& c = ctx.add(spec, 0, 0, 0);
        c.handbrake = getenv("NOHB") ? 0 : 1;
        ctx.run(1.5);
        for (const Wheel& w : c.wheels) std::printf("  %s%s sx %.4f fx-ish omega %.3f\n", w.front ? "F" : "R", w.left ? "L" : "R", w.sx, w.omega);
        for (const Wheel& w : c.wheels) {
            const Beam& sp = c.beams[w.spring];
            double L = len(c.nodes[sp.b].p - c.nodes[sp.a].p);
            double F = sp.k * (sp.L0 - L);
            std::printf("%s%s spring len %.4f rest %.4f dev %+.1f mm  k %.0f L0 %.4f force %.0f N  mr %.3f load %.0f N\n",
                w.front ? "F" : "R", w.left ? "L" : "R", L, w.springRest, (L - w.springRest) * 1000, sp.k, sp.L0, F, w.motionRatio, w.load);
        }
        double m = 0; for (auto& n : c.nodes) m += n.m;
        std::printf("total mass %.0f kg  axleMass spec %.0f\n", m, spec.axleMass);
        for (int k = 0; k < (int)c.wheels.size(); k++) {
            const Wheel& w = c.wheels[k];
            double am = 0; for (int n : w.assembly) am += c.nodes[n].m;
            std::printf("  wheel %d assembly mass %.1f kg (%zu nodes)\n", k, am, w.assembly.size());
        }
        // link forces (vertical component, on the axle side = node not < frameNodes)
        for (const SuspLink& l : c.links) {
            const Beam& b = c.beams[l.beam];
            V3 d = c.nodes[b.b].p - c.nodes[b.a].p; double L = len(d);
            double F = b.k * (b.L0 - L);  // >0 compression pushes nodes apart
            V3 u = d / L;
            // force on node b = +F*u (push apart)
            int axn = b.a >= c.frameNodes ? b.a : b.b; double sgn = axn == b.b ? 1 : -1;
            std::printf("  link kind %d wheel %d: L %.4f L0 %.4f strain %+.5f F %.0f N, vertical on moving end %+.0f N, fore-aft %+.0f\n",
                l.kind, l.wheel, L, b.L0, (L - b.L0) / b.L0, F, sgn * F * u.y, sgn * F * u.x);
        }
        for (const Wheel& w : c.wheels) if (w.bumpStop >= 0) { const Beam& b = c.beams[w.bumpStop]; double L = len(c.nodes[b.b].p - c.nodes[b.a].p); std::printf("  bumpstop L %.4f minL %.4f\n", L, b.minL); }
        return 0;
    }
    // ledge (offroad rock-hits case 3)
    World& w = sim.world;
    const double z = 60;
    w.addBox(V3(-120, w.height(-120, z) + 0.6, z), V3(10, 0.6, 3), 0, 0, 0, V3(0.4, 0.38, 0.35));
    w.boxes.back().rock = true;
    w.buildGrid();
    Car& c = ctx.add(spec, -125 + (getenv("DX") ? atof(getenv("DX")) : 0.0), z, 0);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    c.tractionControl = false;
    // rest positions in car frame
    std::vector<V3> rest(c.nodes.size());
    { Frame fr = c.frame(); for (size_t i = 0; i < c.nodes.size(); i++) { V3 d = c.nodes[i].p - fr.com; rest[i] = V3(dot(d, fr.fwd), dot(d, fr.up), -dot(d, fr.left)); } }
    double maxStop[4] = {0,0,0,0};
    auto crawl = [&](double zz, double speed) {
        Frame fr = c.frame();
        V3 d = V3(fr.com.x + 6, fr.com.y, zz) - fr.com;
        c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 1.5, -0.55, 0.55);
        double v = dot(fr.vel, fr.fwd);
        c.throttle = clampd(0.3 + (speed - v) * 0.9, 0, 1);
        c.brake = v > speed + 1.0 ? 0.3 : 0;
        c.handbrake = 0;
    };
    double lastSet = 0;
    int fcount = 0;
    auto watch = [&] {
        int wi; double s = frameSet(c, &wi);
        if (s > lastSet + 0.05) {
            const Beam& b = c.beams[wi];
            Frame fr = c.frame();
            std::printf("t=%.3f set %.2f%% beam %d nodes %d (%.2f,%.2f,%.2f) - %d (%.2f,%.2f,%.2f) L %.3f   pitch %.1f\n", ctx.t, s, wi, b.a,
                rest[b.a].x, rest[b.a].y, rest[b.a].z, b.b, rest[b.b].x, rest[b.b].y, rest[b.b].z, b.Lorig, std::asin(fr.fwd.y) * 57.3);
            lastSet = s;
        }
        for (int k = 0; k < 4; k++) {
            const Wheel& wh = c.wheels[k];
            if (wh.bumpStop >= 0) { const Beam& b = c.beams[wh.bumpStop]; double L = len(c.nodes[b.a].p - c.nodes[b.b].p); maxStop[k] = std::max(maxStop[k], b.k * std::max(0.0, b.minL > 0 ? b.minL - L : 0.0)); }
        }
        Frame fr = c.frame();
        if (getenv("TR") && ctx.t > atof(getenv("TR")) && ctx.t < atof(getenv("TR")) + (getenv("TRD") ? atof(getenv("TRD")) : 1.0)) {
            int bw = getenv("BW") ? atoi(getenv("BW")) : 2213;
            const Beam& b = c.beams[bw];
            double st = (len(c.nodes[b.b].p - c.nodes[b.a].p) - b.L0) / b.Lorig;
            // link forces
            double lf[8]; int nl = 0;
            for (const SuspLink& l : c.links) if (l.kind == LINK_TRAILING && nl < 8) { const Beam& q = c.beams[l.beam]; double L = len(c.nodes[q.b].p - c.nodes[q.a].p); lf[nl++] = q.k * (q.L0 - L); }
            // tail contact: lowest node behind the rear axle relative to ground, and the stop forces
            double lowest = 1e9; int ln = -1;
            for (size_t i = 0; i < c.nodes.size(); i++) if (rest[i].x < -1.75) { double h = c.nodes[i].p.y - sim.world.height(c.nodes[i].p.x, c.nodes[i].p.z);
                for (auto& bx : sim.world.boxes) { (void)bx; }
                if (h < lowest) lowest = h, ln = (int)i; }
            double stopF[2] = {0, 0};
            for (int k = 2; k < 4; k++) { const Wheel& wh = c.wheels[k]; if (wh.bumpStop >= 0) { const Beam& q = c.beams[wh.bumpStop]; double L = len(c.nodes[q.a].p - c.nodes[q.b].p); stopF[k - 2] = L < q.minL ? q.k * (q.minL - L) : 0; } }
            if (getenv("LIP")) {
                // nodes near the ledge's lip (x = -110, top 1.2 m over the terrain)
                for (size_t i = 0; i < c.nodes.size(); i++) {
                    const V3& p = c.nodes[i].p; double top = sim.world.height(-120, 60) + 1.2;
                    if (p.x < -109.85 && p.x > -110.3 && p.y < top + 0.08 && std::fabs(p.z - 60) < 3)
                        std::printf("   near lip: node %zu (%.2f,%.2f,%.2f) %s  dx %.3f dy %.3f m %.1f\n", i, rest[i].x, rest[i].y, rest[i].z,
                            (int)i < c.bodyNodes ? "body" : (std::find(c.ladder.begin(), c.ladder.end(), (int)i) != c.ladder.end() ? "rail" : "other"), p.x + 110, p.y - top, c.nodes[i].m);
                }
            }
            if (getenv("AX")) {
                // rear axle (diff node) in the body frame, against its rest position; pitch rate
                const Axle& ax = c.axles.back();
                V3 d = c.nodes[ax.diffNode].p - fr.com;
                V3 lp(dot(d, fr.fwd), dot(d, fr.up), -dot(d, fr.left));
                static double lastPitch = 0;
                double pitch = std::asin(fr.fwd.y) * 57.3;
                const Wheel& w2 = c.wheels[2];
                std::printf("   axle dx %+.3f dy %+.3f  pitchrate %+.0f deg/s  RL omega %.2f sx %.2f torque? fx\n", lp.x - rest[ax.diffNode].x, lp.y - rest[ax.diffNode].y,
                    (pitch - lastPitch) * 60, w2.omega, w2.sx);
                lastPitch = pitch;
            }
            const Wheel& rl = c.wheels[2]; const Beam& sp = c.beams[rl.spring]; double spl = len(c.nodes[sp.b].p - c.nodes[sp.a].p) - rl.springRest;
            std::printf("t=%.3f x %.2f y %.2f pitch %.1f vy %.2f loads %.0f %.0f %.0f %.0f | b%d strain %+.4f | links %.0f %.0f %.0f | lowest tail node %d (%.2f,%.2f,%.2f) h %.3f scrape %.2f | stops %.0f %.0f | RLspring %+.3f\n",
                ctx.t, fr.com.x, fr.com.y, std::asin(fr.fwd.y) * 57.3, fr.vel.y, c.wheels[0].load, c.wheels[1].load, c.wheels[2].load, c.wheels[3].load,
                bw, st, nl > 0 ? lf[0] : 0, nl > 1 ? lf[1] : 0, nl > 2 ? lf[2] : 0, ln, ln >= 0 ? rest[ln].x : 0, ln >= 0 ? rest[ln].y : 0, ln >= 0 ? rest[ln].z : 0, lowest, ln >= 0 ? c.nodes[ln].scrape : 0, stopF[0], stopF[1], spl);
        }
        fcount++;
    };
    ctx.runUntil(25, [&] { return c.frame().com.x > -104; }, [&] { crawl(z, getenv("SPD") ? atof(getenv("SPD")) : 2.5); watch(); });
    ctx.run(1.0, [&] { c.throttle = 0, c.brake = 1; watch(); });
    int wi; double s = frameSet(c, &wi);
    std::printf("final frame set %.2f%%  damage %.3f%%\n", s, c.damage * 100);
    std::printf("max bumpstop overtravel force: %.0f %.0f %.0f %.0f N\n", maxStop[0], maxStop[1], maxStop[2], maxStop[3]);
    auto kind = [&](int n) { return n < c.bodyNodes ? "body" : (std::find(c.ladder.begin(), c.ladder.end(), n) != c.ladder.end() ? "rail" : "other"); };
    std::vector<std::pair<double,int>> v;
    for (size_t i = 0; i < c.beams.size(); i++) { const Beam& b = c.beams[i]; if (b.type == BEAM_FRAME) { double st = std::fabs(b.L0 - b.Lorig) / b.Lorig; if (st > 0.002) v.push_back({st, (int)i}); } }
    std::sort(v.rbegin(), v.rend());
    for (size_t k = 0; k < v.size() && k < 25; k++) { const Beam& b = c.beams[v[k].second];
        std::printf("  set %.2f%% beam %d: %d(%.2f,%.2f,%.2f) %s - %d(%.2f,%.2f,%.2f) %s  k %.3g yield %.3f\n", v[k].first*100, v[k].second,
          b.a, rest[b.a].x, rest[b.a].y, rest[b.a].z, kind(b.a), b.b, rest[b.b].x, rest[b.b].y, rest[b.b].z, kind(b.b), b.k, b.yieldStrain); }
    return 0;
}
