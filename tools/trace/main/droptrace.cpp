#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int veh = argc > 1 ? atoi(argv[1]) : 2;
    double h = argc > 2 ? atof(argv[2]) : 2.0, pitch = argc > 3 ? atof(argv[3]) : 12;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[veh], -100, -50, 0);
    ctx.run(1.0);
    if (pitch != 0) c.rotateRigid(c.frame().left, pitch * M_PI / 180);
    for (Node& n : c.nodes) { n.p.y += h; n.v = V3(); }
    int rear[2], k = 0;
    for (size_t i = 0; i < c.wheels.size(); i++) if (!c.wheels[i].front) rear[k++] = i;
    int fr = 0;
    ctx.run(1.6, [&] {
        if (fr++ % (getenv("EVERY") ? atoi(getenv("EVERY")) : 3)) return;
        printf("t=%.3f", ctx.t);
        for (int s = 0; s < 2; s++) {
            const Wheel& w = c.wheels[rear[s]];
            const Beam& st = c.beams[w.bumpStop];
            const Beam& sp = c.beams[w.spring];
            double Ls = len(c.nodes[st.b].p - c.nodes[st.a].p), Lc = len(c.nodes[sp.b].p - c.nodes[sp.a].p);
            printf(" | stop %+.3f coil %+.3f load %5.0f", Ls - st.minL, Lc - w.springRest, w.load);
        }
        // housing: diff, pinion, dtop
        const Axle* ax = nullptr; for (auto& a : c.axles) if (!a.front) ax = &a;
        V3 d = c.nodes[ax->housing[0]].p, t = c.nodes[ax->housing[2]].p, f = c.frame().fwd, u = c.frame().up;
        V3 v = t - d;
        printf(" | housing pitch vs body %.0f deg", atan2(-dot(v, f), dot(v, u)) * 180 / M_PI);
        printf(" body pitch %.1f |", asin(c.frame().fwd.y) * 180 / M_PI);
        for (const SuspLink& l : c.links) {
            const Beam& b = c.beams[l.beam];
            bool mine = false; for (int n : ax->housing) mine |= (b.a == n || b.b == n);
            if (!mine) continue;
            double L = len(c.nodes[b.b].p - c.nodes[b.a].p);
            printf(" k%d %+.0f/%+.0fmm%s", l.kind, (L - b.Lorig) * 1000, (b.L0 - b.Lorig) * 1000, b.broken ? "X" : "");
        }
        int yielded = 0; double worst = 0;
        for (const Beam& b : c.beams) {
            bool a = false, bb = false; for (int n : ax->housing) { a |= b.a == n; bb |= b.b == n; }
            if (a && bb && std::fabs(b.L0 - b.Lorig) > 1e-4) { yielded++; worst = std::max(worst, std::fabs(b.L0 - b.Lorig) / b.Lorig); }
        }
        printf(" housing beams yielded %d worst %.0f%%", yielded, worst * 100);
        if (getenv("PICK")) {
            // frame ends of this axle's links
            std::vector<int> fe;
            for (const SuspLink& l : c.links) {
                const Beam& b = c.beams[l.beam];
                bool ia = false, ib = false; for (int n : ax->housing) { ia |= b.a == n; ib |= b.b == n; }
                if (ia != ib) fe.push_back(ia ? b.b : b.a);
            }
            for (size_t i = 0; i < fe.size(); i++) for (size_t j = i + 1; j < fe.size(); j++) {
                double d = len(c.nodes[fe[i]].p - c.nodes[fe[j]].p), d0 = len(c.restLocal[fe[i]] - c.restLocal[fe[j]]);
                printf(" d%zu%zu %+.0fmm", i, j, (d - d0) * 1000);
            }
        }
        if (getenv("RISE")) {
            // vertical (car up) rise of points vs rest, relative to the coil tops' midpoint
            Frame F = c.frame();
            const Wheel& wl = c.wheels[rear[0]]; const Wheel& wr = c.wheels[rear[1]];
            int ctl = c.beams[wl.spring].a < c.frameNodes ? c.beams[wl.spring].a : c.beams[wl.spring].b;
            int ctr = c.beams[wr.spring].a < c.frameNodes ? c.beams[wr.spring].a : c.beams[wr.spring].b;
            V3 mid = (c.nodes[ctl].p + c.nodes[ctr].p) * 0.5, mid0 = (c.restLocal[ctl] + c.restLocal[ctr]) * 0.5;
            auto rise = [&](int n) { return dot(c.nodes[n].p - mid, F.up) - (c.restLocal[n].y - mid0.y); };
            const Beam& st = c.beams[wl.bumpStop];
            printf(" rise: diff %+.3f padL %+.3f stopNodeL %+.3f innerL %+.3f", rise(ax->housing[0]), rise(st.a), rise(st.b), rise(wl.inner));
        }
        if (getenv("NODES")) {
            V3 o = c.nodes[ax->housing[0]].p; Frame F = c.frame();
            auto loc = [&](int n) { V3 d = c.nodes[n].p - o; return V3(dot(d, F.fwd), dot(d, F.up), -dot(d, F.left)); };
            for (int i : {1, 2, 3, 4, 5, 6}) { V3 q = loc(ax->housing[i]); printf(" [%d %+.2f %+.2f %+.2f]", i, q.x, q.y, q.z); }
        }
        printf("\n");
    });
}
