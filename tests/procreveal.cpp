// The procedural map's fog (src/procreveal.h): the reveal radius grows with height, explored cells are kept in global coordinates
// (they survive the window sliding), the file round-trips, and what was not visited stays dark.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <unistd.h>

#include "../src/procreveal.h"
#include "../src/procworld.h"
#include "../src/world.h"
#include "harness.h"

using namespace proc;

namespace {
World revealWindowAt(uint64_t seed, double gx, double gz) {
    World::procSeed = seed;
    World::procOriginSet = true;
    World::procOriginX = (int64_t)gx, World::procOriginZ = (int64_t)gz;
    World w(MAP_PROCEDURAL);
    World::procOriginSet = false;
    return w;
}
std::string tempDir() {
#ifdef _WIN32  // (no mkdtemp: a fresh folder in the temp directory; the Windows build of the tests didn't compile)
    static std::atomic<int> made{0};
    const std::filesystem::path p = std::filesystem::temp_directory_path() / ("cz-procmap-" + std::to_string(std::random_device{}()) + "-" + std::to_string(made++));
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
#else
    char t[] = "/tmp/cz-procmap-XXXXXX";
    const char* d = mkdtemp(t);
    return d ? d : "/tmp";
#endif
}
}  // namespace

SCENARIO(revealRadius, "procworld/reveal-radius", "The reveal radius is 250 m on low ground and grows with height over the land round about, to 900 m") {
    ctx.check("low ground: 250 m", Reveal::radiusFor(0) == 250 && Reveal::radiusFor(-5) == 250);
    ctx.check("grows with height", Reveal::radiusFor(50) > Reveal::radiusFor(10) && Reveal::radiusFor(120) > Reveal::radiusFor(50));
    ctx.check("a high ridge: 900 m, no more", Reveal::radiusFor(200) == 900 && Reveal::radiusFor(2000) == 900);
    const ProcWorld& G = ProcWorld::forSeed(77);
    // heightAbove: a point at the land's own height is near zero, one 150 m up is 150 m over it, never negative.
    const double gx = 4000, gz = -2500, y = G.macroHeight(gx, gz);
    const double a = Reveal::heightAbove(G, gx, gz, y + 150), b = Reveal::heightAbove(G, gx, gz, y - 500);
    ctx.check("height above the surroundings is never negative", b == 0);
    ctx.check("higher means more over the land", a > Reveal::heightAbove(G, gx, gz, y + 50));
    ctx.check("so a higher vantage sees further", Reveal::radiusFor(a) > Reveal::radiusFor(Reveal::heightAbove(G, gx, gz, y)));
    Reveal low, high;
    low.revealDisc(0, 0, Reveal::radiusFor(0));
    high.revealDisc(0, 0, Reveal::radiusFor(150));
    ctx.check("more cells revealed from height", high.cells() > low.cells() * 2);
    ctx.check("the disc is a disc", low.at(240, 0) && !low.at(270, 0) && low.at(0, -240) && !low.at(190, 190) && !low.at(250, 250));
}

SCENARIO(revealUnexplored, "procworld/reveal-unexplored", "Ground not visited stays unrevealed (fog), and the pictures are only made where revealed") {
    Reveal r;
    ctx.check("nothing at the start", r.cells() == 0 && !r.at(0, 0) && !r.at(1e6, -1e6));
    r.revealDisc(500, 500, 100);
    ctx.check("revealed where visited", r.at(500, 500) && r.at(560, 500));
    ctx.check("fog just past the edge and far away", !r.at(500, 640) && !r.at(-3000, 4000) && !r.at(-500, -500));
    ctx.check("the edge fades: full inside, partial on the rim, none away", r.fade(Reveal::cellOf(500), Reveal::cellOf(500)) == 1.0f &&
                                                                          r.fade(Reveal::cellOf(500 + 96), Reveal::cellOf(500)) > 0 &&
                                                                          r.fade(Reveal::cellOf(500 + 96), Reveal::cellOf(500)) < 1 && r.fade(Reveal::cellOf(2000), 0) == 0);
    const ProcWorld& G = ProcWorld::forSeed(5);
    MapImage img;
    ctx.check("no picture for a tile far from anything", img.update(G, r, 40, 40, 50) == nullptr);
    MapImage::Tile* t = img.update(G, r, 0, 0, 1000);
    ctx.check("a picture for the tile that was seen, all made", t && t->complete);
    const int N = Reveal::kTileCells;
    const int ci = (int)Reveal::cellOf(500), cj = (int)Reveal::cellOf(500);
    ctx.check("opaque at the middle, clear where unseen", t && t->alpha[cj * N + ci] == 255 && t->alpha[2 * N + 2] == 0 && !t->made[2 * N + 2] && t->made[cj * N + ci]);
    // Pixel generation is budgeted: a tiny budget makes some and says it isn't done.
    Reveal big;
    big.revealDisc(500, 500, 450);
    MapImage img2;
    const auto t0 = std::chrono::steady_clock::now();
    MapImage::Tile* p = img2.update(G, big, 0, 0, 0.5);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ctx.check("a budgeted update stops early and carries on later", p && !p->complete && ms < 20, std::to_string(ms) + " ms, " + (p && p->complete ? "complete" : "not complete"));
    int calls = 1;
    while (!img2.update(G, big, 0, 0, 5)->complete && calls < 2000) calls++;
    ctx.note("full-tile (about 2500 revealed pixels) took " + std::to_string(calls) + " updates of 5 ms after the first");
    ctx.check("and gets there", img2.madePixels() > 1000);
}

SCENARIO(revealWindowSlide, "procworld/reveal-window-slide", "Cells revealed in global coordinates survive the window sliding: the same ground is revealed through any window's local coordinates") {
    const ProcWorld& G = ProcWorld::forSeed(31);
    int64_t sx, sz;
    double yaw;
    G.start(sx, sz, yaw);
    World w1 = revealWindowAt(31, (double)sx, (double)sz);
    Reveal r;
    const V3 local(120, 0, -75);
    const V3 g1 = ProcWorld::toGlobal(w1, local);
    r.revealDisc(g1.x, g1.z, 300);
    // The window slides 512 m east and 1024 m north.
    World w2 = revealWindowAt(31, (double)w1.proc.ox + 512, (double)w1.proc.oz + 1024);
    ctx.check("the second window is somewhere else", w2.proc.ox != w1.proc.ox && w2.proc.oz != w1.proc.oz);
    const V3 same = ProcWorld::toLocal(w2, g1);  // (where that spot is in the new window)
    const V3 back = ProcWorld::toGlobal(w2, same);
    ctx.check("the spot is the same ground through either window", back.x == g1.x && back.z == g1.z);
    ctx.check("still revealed", r.at(back.x, back.z) && r.at(back.x + 280, back.z) && !r.at(back.x + 330, back.z));
    const V3 far = ProcWorld::toGlobal(w2, V3(same.x + 100, 0, same.z + 100));
    ctx.check("and ground 100 m on in the new window is revealed too", r.at(far.x, far.z));
    // Negative coordinates (west and south of the origin) tile correctly.
    Reveal n;
    n.revealDisc(-1000, -3100, 200);
    ctx.check("negative global coordinates", n.at(-1000, -3100) && n.at(-1150, -3100) && !n.at(-1000, -2800) && n.tiles() == 4);
}

SCENARIO(revealSaveLoad, "procworld/reveal-save-load", "The explored cells save to a small versioned file and load back exactly (only for the same seed)") {
    const std::string dir = tempDir();
    const std::string path = dir + "/procmap/42.bin";
    Reveal r;
    r.revealDisc(100, 100, 400);
    r.revealDisc(-5200, 9100, 900);
    r.revealDisc(2e9, -3e9, 300);  // (far out)
    ctx.check("dirty before saving", r.dirty);
    ctx.check("saved", r.save(path, 42) && !r.dirty);
    FILE* f = std::fopen(path.c_str(), "rb");
    long size = 0;
    char magic[5] = {};
    if (f) {
        if (std::fread(magic, 1, 4, f) != 4) magic[0] = 0;
        std::fseek(f, 0, SEEK_END), size = std::ftell(f), std::fclose(f);
    }
    ctx.check("a CZPM file, small", std::string(magic) == "CZPM" && size > 100 && size < 100000, std::to_string(size) + " bytes");
    Reveal l;
    ctx.check("loaded", l.load(path, 42) && !l.dirty);
    ctx.check("the same cells", l.cells() == r.cells() && l.tiles() == r.tiles());
    bool same = true;
    for (double x = -6200; x < 3000; x += 37)
        for (double z = -1000; z < 10200; z += 41) same &= l.at(x, z) == r.at(x, z);
    same &= l.at(2e9, -3e9) && !l.at(2e9 + 500, -3e9);
    ctx.check("and the same answers everywhere sampled", same);
    Reveal other;
    ctx.check("another seed's file is refused", !other.load(path, 43) && other.cells() == 0);
    ctx.check("a missing file is just empty", !other.load(dir + "/nope.bin", 42) && other.cells() == 0);
    // A truncated file is refused (and leaves nothing).
    {
        FILE* g = std::fopen(path.c_str(), "rb");
        std::string data(size, 0);
        if (g && std::fread(&data[0], 1, (size_t)size, g) == (size_t)size) {}
        if (g) std::fclose(g);
        FILE* h = std::fopen((dir + "/cut.bin").c_str(), "wb");
        if (h) std::fwrite(data.data(), 1, data.size() / 2, h), std::fclose(h);
        ctx.check("a cut-off file is refused", !other.load(dir + "/cut.bin", 42) && other.cells() == 0);
    }
    std::remove(path.c_str());
    std::remove((dir + "/cut.bin").c_str());
    rmdir((dir + "/procmap").c_str());
    rmdir(dir.c_str());
}
