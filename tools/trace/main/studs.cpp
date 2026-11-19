// tires/shear replica: a truck (argv[1], default the Bronco) sliding sideways at argv[2] km/h into a
// square kerb argv[3] m tall: the peak load on each wheel's studs (as sideways force at the tread)
// against what they hold, and whether a wheel came off.
#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    const int vi = argc > 1 ? atoi(argv[1]) : 2;
    const double kmh = argc > 2 ? atof(argv[2]) : 45, tall = argc > 3 ? atof(argv[3]) : 0.25;
    Sim sim;
    Ctx ctx(sim);
    World& w = sim.world;
    const double z = 40;
    w.addBox(V3(-100, w.height(-100, z + 6) + tall / 2, z + 6), V3(8, tall / 2, 0.3), 0, 0, 0, V3(0.5, 0.5, 0.5));
    w.boxes.back().rock = true;
    w.buildGrid();
    Car& c = ctx.add(vehicleCatalog()[vi], -100, z, 0);
    ctx.run(1.0);
    for (Node& n : c.nodes) n.v = V3(0, 0, kmh / 3.6);
    double peak[4] = {0, 0, 0, 0};
    ctx.run(3.0, [&] {
        for (size_t i = 0; i < c.wheels.size() && i < 4; i++) peak[i] = std::max(peak[i], (double)c.wheels[i].studLoad);
    });
    int off = 0;
    for (const Wheel& wh : c.wheels) off += !wh.mounted;
    std::printf("%s %.0f km/h into a %.2f m kerb: stud load peaks %.1f %.1f %.1f %.1f kN (hold %.0f), %d off, damage %.1f %%\n", c.spec->name, kmh, tall, peak[0] / 1000, peak[1] / 1000, peak[2] / 1000, peak[3] / 1000,
                c.spec->lugShear / 1000, off, c.damage * 100);
}
