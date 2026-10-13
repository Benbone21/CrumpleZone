#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 2;
    double F = argc > 2 ? atof(argv[2]) : 60000, T = argc > 3 ? atof(argv[3]) : 0.08;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], 0, -50, 0);
    ctx.run(1.0);
    Wheel& w = c.wheels[0];
    std::vector<double> L0(c.beams.size());
    for (size_t i = 0; i < c.beams.size(); i++) L0[i] = c.beams[i].L0;
    double peak = 0, worstPen = 0, rise = 0;
    const double y0 = (c.nodes[w.inner].p.y + c.nodes[w.outer].p.y) * 0.5 - c.frame().com.y;
    for (int s = 0; s < (int)(0.6 * 60); s++) {
        double t = s / 60.0;
        // the pulse is applied per frame as an extra velocity change (impulse F*dt over the hub's mass)
        if (t < T) for (int n : {w.inner, w.outer}) c.nodes[n].v.y += F / 2 * (1.0 / 60) / c.nodes[n].m / 1.0 * 0 + 0;
        ctx.run(1.0 / 60, [&] {});
        if (t < T) for (int n : {w.inner, w.outer}) c.nodes[n].v.y += (F / 2) * (1.0 / 60) / (c.nodes[n].m);
        peak = std::max(peak, (double)w.rub);
        rise = std::max(rise, (c.nodes[w.inner].p.y + c.nodes[w.outer].p.y) * 0.5 - c.frame().com.y - y0);
    }
    int panelBent = 0, bodyBent = 0, armBent = 0;
    for (size_t i = 0; i < c.beams.size(); i++) {
        if (std::fabs(c.beams[i].L0 - L0[i]) < 0.002) continue;
        if (c.beams[i].type == BEAM_PANEL) panelBent++;
        else if (c.beams[i].type == BEAM_BODY || c.beams[i].type == BEAM_FRAME) bodyBent++;
        else if (c.beams[i].type == BEAM_ARM || c.beams[i].type == BEAM_TIEROD) armBent++;
    }
    printf("%s: peak rub %.0f N, hub rose %.3f m, bent: panel %d body %d arms %d, damage %.2f%% detached %d\n", vehicleCatalog()[v].name, peak, rise, panelBent, bodyBent, armBent, c.damage * 100, w.detached);
}
