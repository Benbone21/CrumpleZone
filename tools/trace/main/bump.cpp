#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
const VehicleSpec& barrierSpec();
int main() {
    Sim sim2; sim2.setupEmpty(); Ctx c2(sim2);
    c2.addProp(barrierSpec(), 0, 0, 90);
    const VehicleSpec& spec = vehicleCatalog()[2];
    Car& c = c2.add(spec, -1.5 - spec.length / 2, 0, 0);
    c2.run(1.0);
    c2.launch(c, 18 / 3.6);
    c2.run(2.5);
    printf("engine health %.2f\n", c.engineHealth);
    for (int bi : c.engineMounts) { const Beam& b = c.beams[bi]; printf("mount %d broken %d L0 %.3f orig %.3f strain %.2f yield %.2f break %.2f\n", bi, b.broken, b.L0, b.Lorig, (len(c.nodes[b.b].p - c.nodes[b.a].p) - b.Lorig) / b.Lorig, b.yieldStrain, b.breakStrain); }
    double crush = 0; for (int bi : c.bayBeams) { const Beam& b = c.beams[bi]; crush += b.broken ? 0.3 : std::fabs(b.L0 - b.Lorig) / b.Lorig; }
    printf("bay crush avg %.4f over %zu beams\n", crush / std::max<size_t>(1, c.bayBeams.size()), c.bayBeams.size());
}
