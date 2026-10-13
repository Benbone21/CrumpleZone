// How far each frame-mounted node (brackets) moves against the body under full throttle.
#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
// Body frame by least squares is overkill: use the chassis' rotation and the body centroid.
static std::vector<V3> localNow(Car& c) {
    V3 cn, cr;
    for (int i = 0; i < c.bodyNodes; i++) cn += c.nodes[i].p, cr += c.restLocal[i];
    cn = cn / c.bodyNodes, cr = cr / c.bodyNodes;
    Frame f = c.frame();
    std::vector<V3> out(c.nodes.size());
    for (size_t i = 0; i < c.nodes.size(); i++) {
        V3 d = c.nodes[i].p - cn;
        out[i] = V3(dot(d, f.fwd), dot(d, f.up), -dot(d, f.left)) + cr;
    }
    return out;
}
int main(int argc, char** argv) {
    int vi = argc > 1 ? atoi(argv[1]) : 4;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -120, -50, 0);
    ctx.run(2.0);
    std::vector<V3> rest = localNow(c);
    ctx.run(1.0, [&] { c.throttle = 1; c.steer = 0; });
    std::vector<V3> now = localNow(c);
    for (int i = c.bodyNodes; i < c.frameNodes; i++) {
        const V3 d = now[i] - rest[i];
        std::printf("frame node %d rest (%.2f %.2f %.2f) m %.1f moved (%+.4f %+.4f %+.4f)\n", i, c.restLocal[i].x, c.restLocal[i].y, c.restLocal[i].z, c.nodes[i].m, d.x, d.y, d.z);
    }
}
