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

// The grass cull: every tuft whose blades could show is in a tile that is drawn, and the shader's own cull keeps them too.
#include "../src/grasstiles.h"
#include "../src/visibility.h"

SCENARIO(grassCull, "grass/cull-frustum", "No grass cell that could show on screen lies in a culled tile or past the shader's cull, at 16:9, 16:10 and 21:9, FOV 66, facing any way") {
    struct Ring { double cell; int side; };
    const Ring rings[] = {{0.50, 56}, {1.00, 72}, {0.36, 80}, {0.62, 108}, {1.20, 106}, {0.30, 96}, {0.52, 132}, {0.95, 152}};
    const double aspects[] = {16.0 / 9, 16.0 / 10, 21.0 / 9};
    const float fovy = 66 * (float)M_PI / 180;
    int shown = 0, lost = 0, shaderLost = 0, wrongScale = 0;
    double worstScale = 0;
    for (double aspect : aspects)
        for (int yawDeg = 0; yawDeg < 360; yawDeg += 30)
            for (double pitchDeg : {-50.0, -15.0, 0.0, 10.0}) {
                const V3 eye(137.3 + yawDeg, 2.1, -61.7 + pitchDeg * 3);
                const double yaw = yawDeg * M_PI / 180, pitch = pitchDeg * M_PI / 180;
                const V3 target = eye + V3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), -std::sin(yaw) * std::cos(pitch)) * 10;
                const M4 vp = perspective(fovy, (float)aspect, 0.05f, 6000.0f) * lookAt(eye, target, V3(0, 1, 0));
                // (the shader's cull scale: the lengths of the first two rows of the view-projection are the projection's)
                const double sx = std::hypot(std::hypot(vp.m[0], vp.m[4]), vp.m[8]), sy = std::hypot(std::hypot(vp.m[1], vp.m[5]), vp.m[9]);
                worstScale = std::max({worstScale, std::fabs(sx - 1 / std::tan(fovy * 0.5) / aspect) , std::fabs(sy - 1 / std::tan(fovy * 0.5))});
                if (std::fabs(sx - 1 / std::tan(fovy * 0.5) / aspect) > 1e-4 || std::fabs(sy - 1 / std::tan(fovy * 0.5)) > 1e-4) wrongScale++;
                for (const Ring& r : rings) {
                    const double ox = grassGridOrigin(eye.x, r.cell, r.side), oz = grassGridOrigin(eye.z, r.cell, r.side);
                    const int ts = grassTileSpan(r.side);
                    for (int cz = 0; cz < r.side; cz += 1)
                        for (int cx = 0; cx < r.side; cx += 1) {
                            // a tuft sits anywhere in its cell (its corners here); its blade 0.7 cell off it and leaning a metre more; the tip 2 m up
                            for (int k = 0; k < 16; k++) {
                                const V3 base(ox + (cx + (k & 1)) * r.cell, 0.4, oz + (cz + ((k >> 1) & 1)) * r.cell);
                                const double reach = 0.7 * r.cell + 1.0, dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                                const V3 tip(base.x + dirs[k >> 2][0] * reach, 2.0, base.z + dirs[k >> 2][1] * reach);
                                const double cxw = vp.m[0] * tip.x + vp.m[4] * tip.y + vp.m[8] * tip.z + vp.m[12];
                                const double cyw = vp.m[1] * tip.x + vp.m[5] * tip.y + vp.m[9] * tip.z + vp.m[13];
                                const double cww = vp.m[3] * tip.x + vp.m[7] * tip.y + vp.m[11] * tip.z + vp.m[15];
                                if (!(cww > 0.05 && std::fabs(cxw) < cww && std::fabs(cyw) < cww)) continue;
                                shown++;
                                const int tx = cx / ts, tz = cz / ts;
                                double x0, x1, z0, z1;
                                grassTileRange(ox, r.cell, r.side, tx, x0, x1), grassTileRange(oz, r.cell, r.side, tz, z0, z1);
                                RenderBounds b;
                                const double pad = 3.0 + 0.2 * (x1 - x0);
                                b.add(V3(x0, -pad, z0)), b.add(V3(x1, pad, z1));
                                if (!b.visible(vp)) lost++;
                                // the shader's test on the tuft's base
                                const double bx = vp.m[0] * base.x + vp.m[4] * base.y + vp.m[8] * base.z + vp.m[12];
                                const double by = vp.m[1] * base.x + vp.m[5] * base.y + vp.m[9] * base.z + vp.m[13];
                                const double bw = vp.m[3] * base.x + vp.m[7] * base.y + vp.m[11] * base.z + vp.m[15];
                                if (bw < -kGrassReach || std::fabs(bx) > bw + kGrassReach * (sx + 1) || std::fabs(by) > bw + kGrassReach * (sy + 1)) shaderLost++;
                            }
                        }
                }
            }
    ctx.check("there were blades on screen to test", shown > 10000, std::to_string(shown));
    ctx.check("the cull's scale is the projection's whatever way the camera faces", wrongScale == 0, "worst " + std::to_string(worstScale));
    ctx.check("no visible cell sits in a culled tile", lost == 0, std::to_string(lost) + " of " + std::to_string(shown));
    ctx.check("the shader's cull keeps every visible tuft", shaderLost == 0, std::to_string(shaderLost) + " of " + std::to_string(shown));
}
