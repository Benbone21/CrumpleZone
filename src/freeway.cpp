// The freeway map: a straight six-lane divided freeway across a valley that never ends.
//
// Everything repeats every kPeriod meters along x (terrain, trees, lamp posts, gantries,
// the rest areas), and the sim moves everything back a period whenever the player gets
// half a period from the middle (Sim::wrapAround), so it goes on as far as you can drive.
//
// Across (z): the concrete median at 0; each side a 1.2 m inner shoulder, three 3.6 m
// lanes and a 3 m outer shoulder, then a guardrail. Eastbound (+x) is the z > 0 side
// (traffic keeps right).
#include <cmath>

#include "world.h"

namespace {
constexpr double kPeriod = 400.0;
constexpr double kInner = 1.5, kLane = 3.6, kOuter = 12.3, kPaved = 15.5, kRail = 16.3;
const V3 kConcrete(0.62, 0.62, 0.60);
const V3 kSteel(0.62, 0.64, 0.66);
const V3 kSignGreen(0.05, 0.36, 0.18);

// x folded into one period, centered on 0.
double fold(double x) { return x - kPeriod * std::floor(x / kPeriod + 0.5); }
}  // namespace

void World::buildFreeway() {
    map = MAP_FREEWAY;
    crashProps = false;
    wrapPeriod = kPeriod;
    spawn = V3(-150, 0, kInner + kLane * 1.5);  // the middle eastbound lane
    spawnYaw = 0;
    parking = {V3(-175, 0, 13.8), V3(-190, 0, 13.8), V3(-205, 0, 13.8)};  // on the shoulder
    parkingYaw = 0;
    hN = (int)(2 * half / kRes) + 1;
    const int N = hN * hN;
    hgrid.assign(N, 0);
    paint.assign(N, PAINT_NONE);
    surf.assign(N, SURF_GRASS);
    roadLat.assign(N, 0);
    roadAlong.assign(N, 0);
    roadNear.assign(N, 0);
    auto X = [&](int i) { return -half + i * kRes; };
    const double tau = 2 * M_PI / kPeriod;
    // A rest area with a repair shop off the eastbound side every period: its pad, and a
    // gravel slip road from the shoulder.
    const double restZ = 48;
    for (int i = 0; i < hN; i++)
        for (int j = 0; j < hN; j++) {
            const double x = X(i), z = X(j), az = std::fabs(z), fx = fold(x);
            const int id = i * hN + j;
            // Rolling farmland either side, periodic along the road, climbing to hills.
            double h = 6 + 5 * std::sin(tau * x + 0.013 * z) + 2.5 * std::sin(3 * tau * x + 0.05 * z + 1.0) + 1.2 * std::sin(7 * tau * x - 0.04 * z);
            h *= smoothstep(28, 95, az);
            h += std::pow(std::max(0.0, az - 430), 2) * 0.02;  // hills close the valley in
            const double rest = std::hypot(fx, z - restZ);
            h *= smoothstep(22, 40, rest) * smoothstep(8, 16, std::fabs(fx) + std::max(0.0, kRail - z) * 10);  // flat round the shop and its road
            hgrid[id] = (float)h;
            if (az <= kPaved) {
                paint[id] = PAINT_FREEWAY;
                surf[id] = SURF_PAVED;
            } else if (z > 0 && z < restZ - 6 && std::fabs(fx) < 4.5) {
                paint[id] = PAINT_GRAVEL;
                surf[id] = SURF_GRAVEL;
            } else if (rest < 11) {
                paint[id] = PAINT_PAD;
                surf[id] = SURF_PAVED;
            }
            roadNear[id] = az < kRail + 2;
            roadLat[id] = (float)z;
            roadAlong[id] = (float)(x + half);
        }

    // The median: a concrete barrier the length of the map, in 20 m sections.
    for (double x = -half; x < half; x += 20) {
        addBox(V3(x + 10, 0.42, 0), V3(10, 0.42, 0.28), 0, 0, 0, kConcrete);
        standIn(boxes.size() - 1, "jersey_barrier", V3(x + 10, 0, 0), 0, {{"length", 20}});
    }
    // Guardrails, with a gap at each slip road (eastbound).
    for (int s = -1; s <= 1; s += 2)
        for (double x = -half; x < half; x += 20) {
            if (s > 0 && std::fabs(fold(x + 10)) < 12) continue;
            addBox(V3(x + 10, 0.38, s * kRail), V3(10, 0.38, 0.07), 0, 0, 0, kSteel);
            standIn(boxes.size() - 1, "guardrail", V3(x + 10, 0, s * kRail), s > 0 ? M_PI : 0,  // (its rail to the traffic)
                    {{"length", 20}});
        }
    // Lamp posts every 50 m on both sides, the heads reaching over the outer lane.
    for (double x = -half + 25; x < half; x += 50)
        for (int s = -1; s <= 1; s += 2) {
            const double z = s * (kRail + 1.0);
            const size_t first = boxes.size();
            addBox(V3(x, 5, z), V3(0.1, 5, 0.1), 0, 0, 0, kSteel);
            boxes.back().solid = false;
            addBox(V3(x, 9.9, z - s * 1.4), V3(0.12, 0.07, 1.4), 0, 0, 0, kSteel);
            boxes.back().solid = false;
            addBox(V3(x, 9.8, z - s * 2.7), V3(0.35, 0.08, 0.2), 0, 0, 0, V3(0.95, 0.9, 0.75));
            boxes.back().solid = false;
            poles.push_back({x, z, -0.5, 10, 0.12});
            standIn(first, "lamp_post", V3(x, 0, z), s * M_PI / 2);  // (its arm out over the road)
        }
    // A sign gantry over both carriageways every period (half a period from the shops).
    for (int k = -2; k <= 1; k++) {
        const double x = kPeriod / 2 + k * kPeriod;
        if (std::fabs(x) > half - 5) continue;
        const size_t first = boxes.size();
        for (int s = -1; s <= 1; s += 2) {
            const double z = s * (kRail + 2.4);
            addBox(V3(x, 3.6, z), V3(0.25, 3.6, 0.25), 0, 0, 0, kSteel);
            boxes.back().solid = false;
            poles.push_back({x, z, -0.5, 7.2, 0.3});
            addBox(V3(x - s * 0.3, 5.6, s * 7), V3(0.08, 1.1, 5.0), 0, 0, 0, kSignGreen);
            boxes.back().solid = false;
        }
        addBox(V3(x, 7.0, 0), V3(0.35, 0.3, kRail + 2.6), 0, 0, 0, kSteel);
        boxes.back().solid = false;
        standIn(first, "sign_gantry", V3(x, 0, 0), 0, {{"span", 2 * (kRail + 2.4)}, {"sign", (double)((k + 4) % 4)}});
    }
    // The shops.
    for (int k = -1; k <= 1; k++) addStation(V3(k * kPeriod, 0, restZ), M_PI / 2, "REST AREA SHOP");  // drive in from the slip road
    // Trees and bushes: one period's worth, repeated along.
    uint32_t rng = 4242;
    auto rnd = [&]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (rng & 0xFFFFFF) / double(0x1000000);
    };
    std::vector<Tree> oneTrees;
    for (int attempt = 0; attempt < 4000 && oneTrees.size() < 170; attempt++) {
        const double x = (rnd() - 0.5) * kPeriod, z = (rnd() * 2 - 1) * 420;
        if (std::fabs(z) < 32 || (z > 20 && z < 80 && std::fabs(x) < 40)) continue;
        // Shelter belts along the field edges, and a scatter between.
        const bool belt = std::fabs(std::fmod(std::fabs(z), 90.0) - 45) < 4;
        if (!belt && rnd() > 0.25) continue;
        Tree t;
        t.base = V3(x, 0, z);
        t.height = 7 + rnd() * 8;
        t.radius = t.height * (0.24 + rnd() * 0.08);
        t.tint = (float)(0.8 + rnd() * 0.4);
        t.kind = rnd() < 0.6 ? 1 : 0;
        oneTrees.push_back(t);
    }
    std::vector<Bush> oneBushes;
    for (int k = 0; k < 140; k++) {
        const double x = (rnd() - 0.5) * kPeriod, z = (rnd() * 2 - 1) * 400;
        if (std::fabs(z) < 22 || (z > 20 && z < 80 && std::fabs(x) < 40)) continue;
        const double u = rnd();
        oneBushes.push_back(growBush(V3(x, 0, z), HAB_MEADOW, false, u, (float)(0.8 + rnd() * 0.4)));
    }
    for (int k = -2; k <= 2; k++) {
        for (Tree t : oneTrees) {
            t.base.x += k * kPeriod;
            if (std::fabs(t.base.x) > half - 2) continue;
            t.base.y = height(t.base.x, t.base.z) - 0.2;
            plant(t);
        }
        for (Bush b : oneBushes) {
            b.p.x += k * kPeriod;
            if (std::fabs(b.p.x) > half - 2) continue;
            b.p.y = height(b.p.x, b.p.z);
            bushes.push_back(b);
        }
    }
}
