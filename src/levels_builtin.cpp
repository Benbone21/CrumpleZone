// The game's own levels. Their gates come from the maps' features (so they follow the maps
// if those change), worked out once, the first time the list is asked for.
#include <cmath>

#include "level.h"

namespace {

double yawAlong(const V3& a, const V3& b) { return std::atan2(-(b.z - a.z), b.x - a.x); }

// Gates every `step` meters along a polyline from `from` (meters along it) to its end.
void gatesAlong(Level& lv, const std::vector<V3>& pts, double from, double step, double width, bool closed = false) {
    const int n = (int)pts.size();
    double along = 0, next = from;
    for (int k = 0; k + 1 < n + (closed ? 1 : 0); k++) {
        const V3& a = pts[k % n];
        const V3& b = pts[(k + 1) % n];
        const double L = std::hypot(b.x - a.x, b.z - a.z);
        while (next <= along + L && L > 1e-6) {
            const double t = (next - along) / L;
            Gate g;
            g.p = a + (b - a) * t;
            g.yaw = yawAlong(a, b);
            g.width = width;
            lv.gates.push_back(g);
            next += step;
        }
        along += L;
    }
}

void startAt(Level& lv, const V3& p, const V3& toward) {
    lv.hasStart = true;
    lv.start = p;
    lv.startYaw = yawAlong(p, toward);
}

Gate gateAt(const V3& p, const V3& toward, double width) {
    Gate g;
    g.p = p;
    g.yaw = yawAlong(p, toward);
    g.width = width;
    return g;
}

std::vector<Level> build() {
    std::vector<Level> out;
    // The map features are laid out as each map is built.
    { World f(MAP_FRONTIER); }
    { World m(MAP_MOUNTAIN); }

    // ---- The Frontier ----
    {
        Level lv;
        lv.name = "FRONTIER GP";
        lv.about = "THREE LAPS OF THE FRONTIER CIRCUIT: 1.4 KM, A 400 M STRAIGHT, SWEEPERS AND A HAIRPIN";
        lv.map = MAP_FRONTIER, lv.mode = LEVEL_RACE, lv.laps = 3, lv.timeOfDay = 15;
        lv.medals[0] = 160, lv.medals[1] = 175, lv.medals[2] = 200;  // (Jesko)
        const std::vector<V3>& t = frontierTrack();
        // The line: 90 m along, as the lap timer has it; then a gate every ~230 m round.
        std::vector<V3> loop;
        double acc = 0;
        size_t k0 = 0;
        for (size_t k = 0; k + 1 < t.size(); k++) {
            acc += std::hypot(t[k + 1].x - t[k].x, t[k + 1].z - t[k].z);
            if (acc >= 90) {
                k0 = k;
                break;
            }
        }
        for (size_t k = 0; k <= t.size(); k++) loop.push_back(t[(k0 + k) % t.size()]);
        gatesAlong(lv, loop, 230, 230, 18);
        lv.gates.push_back(gateAt(loop[0], loop[1], 18));  // the line itself, last
        startAt(lv, loop[0] - norm(loop[3] - loop[0]) * 12, loop[3]);
        lv.builtin = true;
        out.push_back(lv);
    }
    {
        Level lv;
        lv.name = "THE WALL";
        lv.about = "TWELVE ROCK LEDGES UP A BOULDER GULLY. 4L, AIR DOWN, LOCK IT UP. NO REPAIRS";
        lv.map = MAP_FRONTIER, lv.mode = LEVEL_SPRINT, lv.vehicle = 2, lv.timeOfDay = 10;
        lv.resets = false, lv.maxDamage = 30;
        lv.medals[0] = 50, lv.medals[1] = 80, lv.medals[2] = 140;
        const V3 foot = frontierFeature("wall"), top = frontierFeature("walltop");
        const V3 dir = norm(V3(top.x - foot.x, 0, top.z - foot.z));
        startAt(lv, foot - dir * 16, foot);
        lv.gates.push_back(gateAt(foot - dir * 2, foot, 10));
        lv.gates.push_back(gateAt(foot + dir * 24, top, 10));
        lv.gates.push_back(gateAt(top - dir * 4, top, 14));
        lv.builtin = true;
        out.push_back(lv);
    }
    {
        Level lv;
        lv.name = "MESA CLIMB";
        lv.about = "FOURTEEN LEDGES UP THE RED MESA'S EAST FACE, THEN ACROSS THE TOP";
        lv.map = MAP_FRONTIER, lv.mode = LEVEL_SPRINT, lv.vehicle = 3, lv.timeOfDay = 17.5;
        lv.resets = false, lv.maxDamage = 30;
        lv.medals[0] = 60, lv.medals[1] = 90, lv.medals[2] = 150;
        const V3 foot = frontierFeature("mesaclimb"), mesa = frontierFeature("mesa");
        const V3 dir = norm(V3(mesa.x - foot.x, 0, mesa.z - foot.z));
        startAt(lv, foot - dir * 18, foot);
        lv.gates.push_back(gateAt(foot - dir * 4, foot, 12));
        lv.gates.push_back(gateAt(foot + dir * 28, mesa, 12));
        lv.gates.push_back(gateAt(mesa + V3(25, 0, 0), mesa, 16));
        lv.builtin = true;
        out.push_back(lv);
    }
    {
        Level lv;
        lv.name = "SNAKE RIVER FORD";
        lv.about = "ACROSS THE SNAKE RIVER AT THE FORD: STAY ON THE GRAVEL BAR OR THE CURRENT HAS YOU";
        lv.map = MAP_FRONTIER, lv.mode = LEVEL_SPRINT, lv.timeOfDay = 8;
        lv.maxDamage = 40;
        lv.medals[0] = 20, lv.medals[1] = 30, lv.medals[2] = 50;
        const V3 f = frontierFeature("ford1");
        const V3 dir = norm(V3(1, 0, -0.16));
        startAt(lv, f - dir * 45, f);
        lv.gates.push_back(gateAt(f - dir * 12, f, 14));
        lv.gates.push_back(gateAt(f + dir * 12, f + dir * 30, 14));
        lv.gates.push_back(gateAt(f + dir * 34, f + dir * 50, 16));
        lv.builtin = true;
        out.push_back(lv);
    }
    // ---- The mountain ----
    {
        Level lv;
        lv.name = "SUMMIT RUN";
        lv.about = "TRAILHEAD TO THE LOOKOUT: 1.3 KM OF DIRT SPIRALING 140 M UP THE MASSIF";
        lv.map = MAP_MOUNTAIN, lv.mode = LEVEL_SPRINT, lv.timeOfDay = 11;
        lv.medals[0] = 110, lv.medals[1] = 150, lv.medals[2] = 220;
        const std::vector<V3> trail = mountainTrail();
        const V3 th = mountainFeature("trailhead");
        startAt(lv, th, trail.front());
        lv.gates.push_back(gateAt(trail.front(), trail[3], 14));
        gatesAlong(lv, trail, 160, 160, 14);
        lv.gates.push_back(gateAt(trail.back(), trail.back() + (trail.back() - trail[trail.size() - 4]), 16));
        lv.builtin = true;
        out.push_back(lv);
    }
    {
        Level lv;
        lv.name = "WIDOWMAKER";
        lv.about = "OUT ALONG THE KNIFE-EDGE SPUR TO THE PINNACLE. 50 M DROPS BOTH SIDES. NO REPAIRS";
        lv.map = MAP_MOUNTAIN, lv.mode = LEVEL_SPRINT, lv.timeOfDay = 18.2;
        lv.resets = false, lv.maxDamage = 20;
        lv.medals[0] = 70, lv.medals[1] = 100, lv.medals[2] = 160;
        const V3 s0 = mountainFeature("spur"), pin = mountainFeature("pinnacle");
        const V3 dir = norm(V3(pin.x - s0.x, 0, pin.z - s0.z));
        startAt(lv, s0 - dir * 10, s0);
        for (double d = 0; d < 150; d += 37.5) lv.gates.push_back(gateAt(s0 + dir * d, pin, 8));
        lv.gates.push_back(gateAt(pin - dir * 4, pin, 14));
        lv.builtin = true;
        out.push_back(lv);
    }
    {
        Level lv;
        lv.name = "BOULDER GULLY";
        lv.about = "110 M UP A RAVINE CHOKED WITH BOULDERS, LEDGES AND SCREE AT 40%. BOTH LOCKERS";
        lv.map = MAP_MOUNTAIN, lv.mode = LEVEL_SPRINT, lv.vehicle = 2, lv.timeOfDay = 13;
        lv.resets = false, lv.maxDamage = 35;
        lv.medals[0] = 90, lv.medals[1] = 140, lv.medals[2] = 220;
        const V3 g0 = mountainFeature("gully"), g1 = mountainFeature("gullytop");
        const V3 dir = norm(V3(g1.x - g0.x, 0, g1.z - g0.z));
        startAt(lv, g0 - dir * 12, g0);
        lv.gates.push_back(gateAt(g0, g1, 14));
        lv.gates.push_back(gateAt(g0 + (g1 - g0) * 0.5, g1, 14));
        lv.gates.push_back(gateAt(g1 - dir * 6, g1, 16));
        lv.builtin = true;
        out.push_back(lv);
    }
    for (Level& lv : out) lv.author = "CRUMPLE ZONE";
    return out;
}

}  // namespace

const std::vector<Level>& builtinLevels() {
    static const std::vector<Level> levels = build();
    return levels;
}
