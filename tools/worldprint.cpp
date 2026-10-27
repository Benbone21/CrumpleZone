// The world's fingerprint (src/worldprint.h): builds World(map) and prints a hash per category of what's on it (rocks, trees, logs,
// bushes, boxes, poles, props, places, trails) and of the land (terrain heights, ground, water), so builds on different machines can be
// compared (the multiplayer world: every player builds it themselves).
//   build/worldprint [--scans] [--threads N] [--dump FILE] [MAP...]   (default: 4, the Backcountry)
//   --scans: rocks from the scanned hull library, as the game has them.  --threads: cap the build's threads (else CZ_GEN_THREADS).
//   --dump: every object and 256 m tile of land, a line each, to diff.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../src/parallel.h"
#include "../src/props.h"
#include "../src/worldcache.h"
#include "../src/worldprint.h"

int main(int argc, char** argv) {
    std::vector<int> maps;
    const char* dumpPath = nullptr;
    if (const char* t = std::getenv("CZ_GEN_THREADS")) parallelRowsCap = std::atoi(t);
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--scans")) props::setActiveRockScans(&props::sharedHullLibrary());
        else if (!std::strcmp(argv[i], "--threads") && i + 1 < argc) parallelRowsCap = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--dump") && i + 1 < argc) dumpPath = argv[++i];
        else maps.push_back(std::atoi(argv[i]));
    }
    if (maps.empty()) maps = {MAP_BACKCOUNTRY};
    worldcache::bypass = true;  // (built here, never read from or written to a cache)
    FILE* dump = dumpPath ? std::fopen(dumpPath, "w") : nullptr;
    for (int map : maps) {
        if (map < 0 || map >= MAP_COUNT) continue;
        World* w = new World(map);
        if (dump) std::fprintf(dump, "map %d\n", map);
        const worldprint::Print p = worldprint::print(*w, dump);
        std::printf("map %d %s scans %d threads %d\n", map, mapName(map), props::activeRockScans() ? 1 : 0, parallelRowsCap);
        for (int c = 0; c < worldprint::CAT_COUNT; c++)
            std::printf("  %-8s %016llx  (%zu)\n", worldprint::catName(c), (unsigned long long)p.cat[c], p.count[c]);
        std::printf("  all      %016llx  land %016llx\n", (unsigned long long)p.all(), (unsigned long long)w->landHash());
        std::fflush(stdout);
        delete w;
    }
    if (dump) std::fclose(dump);
    return 0;
}
