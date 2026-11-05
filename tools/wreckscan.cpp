// Where the built maps' wrecks go: a scan of the land, printed as src/places_wrecks.inc (wrecks.h). Run it with that table cut down to its
// first rows or empty, on a build where the land is as it will ship; edit the table by hand from then on (ids go by row: new ones at the end).
//
//   build/wreckscan > /tmp/rows && cp /tmp/rows src/places_wrecks.inc
//
// A spot is dry, not on a road or trail, no steeper than 1 in 2.5, clear of rocks, trees, boxes, logs and every place's yard, and far from the
// other wrecks. Each region (and each kind of spot in it) gets its share: the scrub, a gully, a cliff's foot, among the trees.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "../src/onemap.h"
#include "../src/places.h"
#include "../src/procworld.h"
#include "../src/props.h"
#include "../src/world.h"
#include "../src/wrecks.h"

namespace {

uint32_t rng = 0xC0FFEE;
double rnd() {
    rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
    return (rng & 0xFFFFFF) / (double)0x1000000;
}

struct Row {
    double x, z, yaw;
};

bool clearAt(const World& w, double x, double z, double r) {
    World::NearbySolids n;
    w.solidsNear(x - r - 25, z - r - 25, x + r + 25, z + r + 25, n);
    for (int i : n.rocks)
        if (std::hypot(w.rocks[i].c.x - x, w.rocks[i].c.z - z) < w.rocks[i].r + r) return false;
    for (int i : n.poles)
        if (std::hypot(w.poles[i].x - x, w.poles[i].z - z) < w.poles[i].r + r) return false;
    for (int i : n.boxes)
        if (std::hypot(w.boxes[i].c.x - x, w.boxes[i].c.z - z) < w.boxes[i].bound + r) return false;
    for (int i : n.logs) {
        const V3 m = (w.logs[i].a + w.logs[i].b) * 0.5;
        if (std::hypot(m.x - x, m.z - z) < len(w.logs[i].a - w.logs[i].b) / 2 + r) return false;
    }
    return true;
}

bool dryGentle(const World& w, double x, double z) {
    for (double dx = -5; dx <= 5; dx += 5)
        for (double dz = -5; dz <= 5; dz += 5) {
            if (w.waterLevel(x + dx, z + dz) > w.height(x + dx, z + dz) - 0.4) return false;
            if (w.roadAt(x + dx, z + dz).near) return false;
        }
    const double sx = (w.height(x + 3, z) - w.height(x - 3, z)) / 6, sz = (w.height(x, z + 3) - w.height(x, z - 3)) / 6;
    return std::hypot(sx, sz) < 0.4;
}

// `n` spots in the rectangle, `apart` m from each other and from what's in `taken`.
// (`also`: the same map built with the other rocks: clear in both.)
void scan(const World& w, const World* also, double x0, double x1, double z0, double z1, int n, double apart, std::vector<Row>& out) {
    const std::vector<MapPlace>& pl = mapPlaces(w.map);
    int made = 0;
    for (int t = 0; t < 400000 && made < n; t++) {
        const int niche = made % 4;
        double bx = 0, bz = 0, best = -1e18;
        bool found = false;
        for (int c = 0; c < (niche ? 10 : 1); c++) {
            const double x = x0 + (x1 - x0) * rnd(), z = z0 + (z1 - z0) * rnd();
            if (std::max(std::fabs(x), std::fabs(z)) > w.halfSize() - 40 || !dryGentle(w, x, z) || !clearAt(w, x, z, 3.5) || (also && !clearAt(*also, x, z, 3.5))) continue;
            bool ok = true;
            for (const MapPlace& p : pl)
                ok = ok && (p.kind == PK_WRECK || std::hypot(p.p.x - x, p.p.z - z) > p.pad + 30);
            for (const Row& r : out) ok = ok && std::hypot(r.x - x, r.z - z) > apart;
            if (!ok) continue;
            const double h = w.height(x, z);
            double s = 0;
            if (niche == 1) {  // a gully: lower than what's round it
                for (int a = 0; a < 8; a++) s += w.height(x + 25 * std::cos(a * 0.785), z + 25 * std::sin(a * 0.785)) - h;
            } else if (niche == 2) {  // a cliff's foot: the most land standing over it
                for (int a = 0; a < 8; a++) s = std::max(s, w.height(x + 24 * std::cos(a * 0.785), z + 24 * std::sin(a * 0.785)) - h);
            } else if (niche == 3) {  // among trees
                for (const Tree& tr : w.trees) s += std::hypot(tr.base.x - x, tr.base.z - z) < 25;
            }
            if (s > best) best = s, bx = x, bz = z, found = true;
        }
        if (!found) continue;
        out.push_back({std::round(bx), std::round(bz), std::round(rnd() * 628) / 100});
        made++;
    }
    std::fprintf(stderr, "%d of %d in [%.0f %.0f] x [%.0f %.0f]\n", made, n, x0, x1, z0, z1);
}

void print(const char* name, const std::vector<Row>& rows) {
    std::printf("const WreckRow %s[] = {\n", name);
    for (const Row& r : rows) std::printf("    {%.0f, %.0f, %.2f},\n", r.x, r.z, r.yaw);
    std::printf("};\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {  // build/wreckscan SEED: where a procedural window's wrecks are, for screenshots
        World::procSeed = std::strtoull(argv[1], nullptr, 10);
        World w(MAP_PROCEDURAL);
        for (const MapPlace& p : proc::windowPlaces(w))
            if (p.kind == PK_WRECK) {
                const wrecks::Pose q = wrecks::poseOf(w, p);
                std::printf("%s %.0f %.0f %.1f %s v%d pitch %.2f roll %.2f sink %.2f\n", p.id.c_str(), p.p.x, p.p.z, w.height(p.p.x, p.p.z), q.look.model, q.look.variant, q.look.pitch, q.look.roll, q.look.sink);
            }
        return 0;
    }
    std::printf("// Generated from a scan of the land (tools/wreckscan.cpp: dry, gentle, off the roads, clear of rocks and trees and every yard, far apart); edit by hand now.\n");
    std::vector<Row> bc;
    {
        // (The game's world has the scanned rocks, a checkout without assets the code-made ones: a wreck stands clear in both.)
        World w(MAP_BACKCOUNTRY);
        const props::HullLibrary& lib = props::sharedHullLibrary();
        std::unique_ptr<World> scanned;
        if (lib.loaded()) {
            props::ScopedRockScans scans(&lib);
            scanned = std::make_unique<World>(MAP_BACKCOUNTRY);
        } else std::fprintf(stderr, "no assets/props: the scanned rocks not looked at\n");
        scan(w, scanned.get(), -2900, 2900, -2900, 2900, 24, 500, bc);
        for (const MapRegion& r : mapRegions()) scan(w, scanned.get(), r.ex0, r.ex1, r.ez0, r.ez1, r.map == MAP_WILDS ? 6 : 7, 450, bc);
    }
    print("kBackcountryWrecks", bc);
    std::vector<Row> fw;
    {
        World w(MAP_FREEWAY);
        scan(w, nullptr, -200, 199, -200, 200, 3, 90, fw);
    }
    print("kFreewayWrecks", fw);
    return 0;
}
