#define private public
#include "../../../tests/harness.h"
#include <cstdio>
int main() {
    Sim sim;
    sim.setupEmpty();
    Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[0], -120, -50, 0);
    Car& s = ctx.add(vehicleCatalog()[0], -120, -42, 0);
    ctx.focus(c);
    int fr = 0;
    ctx.run(2.5, [&] {
        if (fr++ % 6 == 0) {
            double mv = 0;
            int mi = 0;
            for (int i = 0; i < (int)s.nodes.size(); i++)
                if (len(s.nodes[i].v) > mv) mv = len(s.nodes[i].v), mi = i;
            double mw = 0;
            for (auto& w : s.wheels) mw = std::max(mw, std::fabs(w.omega));
            printf("t=%.2f sleep=%d still=%.3f maxv=%.4f (node %d of %d bodyNodes %d) maxomega=%.3f\n", ctx.t, s.sleeping, s.stillTime, mv, mi,
                   (int)s.nodes.size(), s.bodyNodes, mw);
        }
    });
}
