#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = atoi(argv[1]);
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
    const double h = 1.0 / 2000;
    const char* tn[] = {"BODY","CABIN","ARM","SPRING","KNUCKLE","FRAME","MOUNT","ENGINE","TIEROD","SHAFT","PANEL","BUMPSTOP"};
    std::vector<std::pair<double,int>> worst;
    for (size_t i = 0; i < c.nodes.size(); i++) {
        double wl = 0;
        for (const Beam& d : c.beams) {
            if (d.a != (int)i && d.b != (int)i) continue;
            V3 u = norm(c.restLocal[d.b] - c.restLocal[d.a]);
            double K = 0, C = 0;
            for (const Beam& e : c.beams) {
                if (e.a != (int)i && e.b != (int)i) continue;
                double c2 = dot(u, norm(c.restLocal[e.b] - c.restLocal[e.a])); c2 *= c2;
                bool spring = e.type == BEAM_SPRING || e.type == BEAM_BUMPSTOP;
                K += c2 * e.k * (e.type == BEAM_BUMPSTOP ? 125 : spring ? 26 : 1);
                C += c2 * e.c * (spring ? 1.35 : 1);
            }
            wl = std::max(wl, h * h * K + 2 * h * C);
        }
        worst.push_back({wl / c.nodes[i].m, (int)i});
    }
    std::sort(worst.rbegin(), worst.rend());
    for (int k = 0; k < 8; k++) {
        int i = worst[k].second;
        printf("node %d L/m %.2f m %.2f local (%.2f %.2f %.2f):", i, worst[k].first, c.nodes[i].m, c.restLocal[i].x, c.restLocal[i].y, c.restLocal[i].z);
        for (const Beam& b : c.beams) if (b.a == i || b.b == i) printf(" %s(%.1e)", tn[b.type], b.k);
        printf("\n");
    }
}
