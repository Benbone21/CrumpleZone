// The grass (render.cpp's grass shader gets its density from the world): where it grows and where it does not, and the
// adaptive quality's grass levels. No SDL or GL.
#include <cmath>

#include "../src/perf.h"
#include "../src/world.h"
#include "harness.h"

SCENARIO(grassDensityMap, "grass/density", "Grass grows on the meadow, thinning toward a road's verge; none on the pad, the road itself or under water") {
    World w(MAP_PROVING_GROUND);
    ctx.check("none on the concrete pad", w.grassDensity(0, 0) == 0 && w.grassDensity(60, -40) == 0);
    int meadow = 0, bare = 0, near = 0, far = 0;
    bool range = true;
    double sumNear = 0, sumFar = 0;
    float most = 0;
    for (double x = -520; x <= 520; x += 13)
        for (double z = -520; z <= 520; z += 13) {
            const float d = w.grassDensity(x, z);
            range &= d >= 0 && d <= 1.0001;
            if (w.surfaceAt(x, z) != SURF_GRASS && d != 0) bare++;
            if (d > 0.2f) meadow++;
            most = std::max(most, d);
            const RoadInfo r = w.roadAt(x, z);
            if (r.near && r.paint == PAINT_NONE && w.surfaceAt(x, z) == SURF_GRASS) {
                const double lat = std::fabs(r.lateral);
                if (lat < 8.0) sumNear += d, near++;
                else if (lat > 11.0) sumFar += d, far++;
            }
        }
    ctx.check("density stays 0..1", range);
    ctx.check("grass only on grass ground", bare == 0);
    ctx.check("there is meadow", meadow > 500, std::to_string(meadow) + " samples above 0.2");
    ctx.check("it can grow thick", most > 0.8f, std::to_string(most));
    if (near > 3 && far > 3)
        ctx.check("thinner at the verge than a little further out", sumNear / near < 0.6 * sumFar / far,
                  std::to_string(sumNear / near) + " against " + std::to_string(sumFar / far));
}

SCENARIO(grassQuality, "grass/adaptive-levels", "Adaptive quality steps the grass down a level at a time under load (to LOW, not off), and back up to no more than the chosen level") {
    RenderSettings chosen, runtime;
    chosen.grass = 3;
    runtime = chosen;
    AdaptiveQuality controller;
    controller.init(chosen);
    int lowest = 3;
    bool steps = true;
    for (int i = 0; i < 3000; ++i) {
        const int before = runtime.grass;
        controller.evaluate(40, runtime);
        lowest = std::min(lowest, runtime.grass);
        steps &= std::abs(runtime.grass - before) <= 1;
    }
    ctx.check("sustained slow frames take it down to LOW, never off", lowest == 1 && runtime.grass == 1);
    ctx.check("one level at a time", steps);
    ctx.check("the chosen level is untouched", chosen.grass == 3);
    bool within = true;
    for (int i = 0; i < 9000; ++i) {
        controller.evaluate(1000.0f / 60, runtime);
        within &= runtime.grass <= chosen.grass;
    }
    ctx.check("recovery brings it back and no higher", within && runtime.grass == 3);
}
