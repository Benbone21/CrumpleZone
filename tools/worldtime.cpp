// Headless world-build timer (docs/plans/big-maps.md, phase 0): builds World(map) for each map asked and prints how long it
// took, the memory it added, its grid and what's on it, and a hash of the land (heights to 1 cm, surfaces, water) so two
// builds can be compared.
//   build/worldtime [--scans] [MAP...]     MAP: 0 proving ground, 1 mountain, 2 freeway, 3 frontier, 4 backcountry, 5 wilds
//   --scans: rocks from the scanned hull library (as the game has them; the dedicated server doesn't)
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "../src/props.h"
#include "../src/world.h"

namespace {
long rssKb() {  // (Linux; 0 elsewhere)
    std::ifstream f("/proc/self/status");
    std::string l;
    while (std::getline(f, l))
        if (l.rfind("VmRSS:", 0) == 0) return std::atol(l.c_str() + 6);
    return 0;
}

uint64_t landHash(const World& w) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    const int n = w.gridSize();
    mix((uint64_t)n);
    for (int id = 0; id < n * n; id++) {
        mix((uint64_t)(int64_t)std::llround(w.cellHeight(id) * 100.0));
        mix(w.cellSurface(id));
        const V3 c = w.cellCenter(id);
        const double wl = w.waterLevel(c.x, c.z);
        mix(wl < -1e8 ? 0xffffu : (uint64_t)(int64_t)std::llround(wl * 100.0));
    }
    mix(w.trees.size()), mix(w.rocks.size()), mix(w.logs.size()), mix(w.bushes.size()), mix(w.boxes.size());
    auto at = [&](const V3& p) { mix((uint64_t)(int64_t)std::llround(p.x * 100)), mix((uint64_t)(int64_t)std::llround(p.z * 100)); };
    for (const Tree& t : w.trees) at(t.base);
    for (const Rock& r : w.rocks) at(r.c);
    for (const Log& l : w.logs) at(l.a);
    for (const Box& b : w.boxes) at(b.c);
    for (const Placed& m : w.models) at(m.p);
    for (const Station& s : w.stations) at(s.p);
    return h;
}
}  // namespace

int main(int argc, char** argv) {
    std::vector<int> maps;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--scans")) props::setActiveRockScans(&props::sharedHullLibrary());
        else maps.push_back(std::atoi(argv[i]));
    }
    if (maps.empty()) maps = {MAP_PROVING_GROUND, MAP_MOUNTAIN, MAP_FREEWAY, MAP_FRONTIER, MAP_BACKCOUNTRY, MAP_WILDS};
    for (int map : maps) {
        if (map < 0 || map >= MAP_COUNT) continue;
        const long r0 = rssKb();
        const auto t0 = std::chrono::steady_clock::now();
        World* w = new World(map);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const long r1 = rssKb();
        std::printf("map %d %-14s build %8.1f ms  +%4ld MB  grid %d (half %.0f m)  trees %zu rocks %zu logs %zu bushes %zu boxes %zu "
                    "models %zu stations %zu  land %016llx\n",
                    map, mapName(map), ms, (r1 - r0) / 1024, w->gridSize(), World::kHalfSize, w->trees.size(), w->rocks.size(),
                    w->logs.size(), w->bushes.size(), w->boxes.size(), w->models.size(), w->stations.size(),
                    (unsigned long long)landHash(*w));
        std::fflush(stdout);
        delete w;
    }
    return 0;
}
