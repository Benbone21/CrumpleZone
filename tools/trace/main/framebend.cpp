// Three-point bend of a ladder frame: the truck on its wheels, a press pushing down on the
// frame station at mid-wheelbase, ramped. Prints the load at first set and the sag.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <set>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 2;
    double rate = argc > 2 ? atof(argv[2]) : 15000;  // N/s
    double where = argc > 3 ? atof(argv[3]) : 0.5;   // 0 = rear axle, 1 = front axle
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
    ctx.run(1.5, [&] { c.handbrake = 1; });
    const int S = c.ladderStations;
    double xf = -1e9, xr = 1e9;
    for (const Wheel& w : c.wheels) xf = std::max(xf, c.restLocal[w.inner].x), xr = std::min(xr, c.restLocal[w.inner].x);
    double xm = xr + (xf - xr) * where;
    int mid = 0, sf = 0, sr = 0;
    auto sx = [&](int st) { return c.restLocal[c.ladder[st * 8]].x; };
    for (int st = 0; st < S; st++) {
        if (std::fabs(sx(st) - xm) < std::fabs(sx(mid) - xm)) mid = st;
        if (std::fabs(sx(st) - xf) < std::fabs(sx(sf) - xf)) sf = st;
        if (std::fabs(sx(st) - xr) < std::fabs(sx(sr) - xr)) sr = st;
    }
    printf("stations %d: rear axle st %d (x %.2f), load st %d (x %.2f), front st %d (x %.2f)\n", S, sr, sx(sr), mid, sx(mid), sf, sx(sf));
    std::set<int> lad(c.ladder.begin(), c.ladder.end());
    auto stY = [&](int st) { double y = 0; for (int k = 0; k < 8; k++) y += c.nodes[c.ladder[st * 8 + k]].p.y; return y / 8; };
    auto sag = [&] { double a = stY(sr), b = stY(sf); double t = (sx(mid) - sx(sr)) / (sx(sf) - sx(sr)); return a + (b - a) * t - stY(mid); };
    const double sag0 = sag();
    c.extForce.assign(c.nodes.size(), V3());
    double P = 0, firstSet = -1, sagAt20 = -1;
    for (int f = 0; f < 60 * 12; f++) {
        P += rate / 60;
        for (int k = 0; k < 8; k++) c.extForce[c.ladder[mid * 8 + k]] = V3(0, -P / 8, 0);
        ctx.step();
        c.handbrake = 1;
        double set = 0; int worst = -1;
        for (size_t i = 0; i < c.beams.size(); i++) {
            const Beam& b = c.beams[i];
            if (b.type != BEAM_FRAME || !lad.count(b.a) || !lad.count(b.b)) continue;
            double d = std::fabs(b.L0 - b.Lorig);
            if (d > set) set = d, worst = (int)i;
        }
        double s = sag() - sag0;
        if (sagAt20 < 0 && P >= 20000) sagAt20 = s;
        if (firstSet < 0 && set > 0.002) {
            firstSet = P;
            const Beam& b = c.beams[worst];
            printf("first set (%.1f mm) at %.1f kN, sag %.1f mm, beam (%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n", set * 1000, P / 1000, s * 1000,
                   c.restLocal[b.a].x, c.restLocal[b.a].y, c.restLocal[b.a].z, c.restLocal[b.b].x, c.restLocal[b.b].y, c.restLocal[b.b].z);
        }
        if (f % 60 == 59) printf("  t %4.1f  P %6.1f kN  sag %6.1f mm  set %5.1f mm  frame dmg %.3f%%\n", ctx.t, P / 1000, s * 1000, set * 1000, c.damage * 100);
    }
    printf("sag at 20 kN: %.1f mm\n", sagAt20 * 1000);
}
