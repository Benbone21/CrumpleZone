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
#include "../src/loadprof.h"
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

}  // namespace

namespace {
// Everything the roads and water say at every cell (roadAt, surfaceAt, waterLevel, waterFlow), exactly: for checking a change
// to how they're stored.
uint64_t layerHash(const World& w) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    auto bits = [](double v) {
        uint64_t u;
        std::memcpy(&u, &v, sizeof u);
        return u;
    };
    const int n = w.gridSize();
    for (int id = 0; id < n * n; id++) {
        const V3 c = w.cellCenter(id);
        const RoadInfo r = w.roadAt(c.x, c.z);
        mix(r.near), mix(r.paint), mix(bits(r.lateral)), mix(bits(r.along)), mix(w.surfaceAt(c.x, c.z));
        mix(bits(w.waterLevel(c.x, c.z)));
        const V3 f = w.waterFlow(c.x, c.z);
        mix(bits(f.x)), mix(bits(f.z));
    }
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
                    "models %zu stations %zu  land %016llx layers %016llx\n",
                    map, mapName(map), ms, (r1 - r0) / 1024, w->gridSize(), w->halfSize(), w->trees.size(), w->rocks.size(),
                    w->logs.size(), w->bushes.size(), w->boxes.size(), w->models.size(), w->stations.size(),
                    (unsigned long long)w->landHash(), (unsigned long long)layerHash(*w));
        std::fflush(stdout);
        delete w;
        loadprof::report(mapName(map));  // (CZ_LOADPROF=1: the build's stages)
    }
    return 0;
}
