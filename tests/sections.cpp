// Obstacle sections (sections.h): each type builds on a fresh proving ground at any heading, leaving its
// ends drivable and everything finite, touching nothing outside its own ground; the park laid out in
// the game keeps the spawn pad, the loop road and the test courses as they were.
#include <cmath>
#include <cstdio>
#include <map>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "../src/editcore.h"
#include "../src/level.h"
#include "../src/sectionfile.h"
#include "../src/sections.h"
#include "harness.h"

namespace {

bool insideBox(const Box& b, const V3& p) {
    const V3 d = p - b.c;
    return std::fabs(dot(d, b.ax[0])) <= b.h.x && std::fabs(dot(d, b.ax[1])) <= b.h.y && std::fabs(dot(d, b.ax[2])) <= b.h.z;
}

// How far (x, z) is outside a section's cleared ground (negative inside).
double outsideOf(const SectionFootprint& fp, const SectionFrame& f, double x, double z) {
    const double dx = std::cos(f.heading), dz = -std::sin(f.heading);
    const double u = (x - f.entry.x) * dx + (z - f.entry.z) * dz, v = (x - f.entry.x) * std::sin(f.heading) + (z - f.entry.z) * std::cos(f.heading);
    return std::max({-fp.apron - u, u - (fp.length + fp.apron), std::fabs(v) - fp.half});
}

// A point is drivable ground when it's dry and a 1 m sphere sits on it (nothing solid in the way).
bool drivable(const World& w, const V3& p, std::string& why) {
    const double h = w.height(p.x, p.z);
    if (!std::isfinite(h)) return why = "height not finite", false;
    if (w.waterLevel(p.x, p.z) > h - 0.05) return why = "under water", false;
    for (const Box& b : w.boxes)
        if (insideBox(b, V3(p.x, h + 0.5, p.z))) return why = "inside a box", false;
    if (w.clearY(p.x, p.z) > h + 0.3) return why = "something solid on it", false;
    return true;
}

// The frame for a test: the section centred on (-300, -300), running along `heading`.
SectionFrame testFrame(const World& w, SectionType t, double heading) {
    SectionFrame f;
    f.heading = heading;
    const double L = sectionLength(t), cx = -300, cz = -300;
    f.entry = V3(cx - std::cos(heading) * L / 2, 0, cz + std::sin(heading) * L / 2);
    f.entry.y = w.height(f.entry.x, f.entry.z);
    return f;
}

void checkSection(Ctx& ctx, SectionType t) {
    const SectionFootprint fp = sectionFootprint(t);
    const World base(MAP_PROVING_GROUND);
    struct Try {
        uint32_t seed;
        double heading;
    };
    for (const Try& tr : {Try{1, 0}, Try{2, M_PI / 2}, Try{3, M_PI}, Try{1, 3 * M_PI / 2}}) {
        const std::string tag = std::string(sectionName(t)) + " seed " + std::to_string(tr.seed) + " heading " + std::to_string((int)std::lround(tr.heading * 180 / M_PI));
        World w(MAP_PROVING_GROUND);
        const SectionFrame f = testFrame(w, t, tr.heading);
        const SectionBuilt b = buildSection(w, t, tr.seed, f);
        w.buildGrid();
        // Everything finite, in the footprint and round it.
        bool finite = true;
        int mud = 0, sand = 0, wet = 0, dirt = 0, steep = 0, cellsIn = 0;
        double lo = 1e9, hi = -1e9, blockRelief = 0;
        std::map<std::pair<int, int>, std::pair<double, double>> blocks;  // 60 m blocks of the section: lowest and highest ground
        for (double x = f.entry.x - 320; x <= f.entry.x + 320; x += 1)
            for (double z = f.entry.z - 320; z <= f.entry.z + 320; z += 1) {
                const int id = w.cellIndex(x, z);
                if (id < 0) continue;
                const double h = w.cellHeight(id), o = outsideOf(fp, f, x, z);
                finite = finite && std::isfinite(h) && std::isfinite(w.waterLevel(x, z) + 0.0);
                if (o < 0) {
                    lo = std::min(lo, h), hi = std::max(hi, h);
                    mud += w.cellSurface(id) == SURF_MUD, sand += w.cellSurface(id) == SURF_SAND, wet += w.waterLevel(x, z) > h + 0.05;
                    dirt += w.cellSurface(id) == SURF_DIRT;
                    if (t == SEC_DUNES) {
                        auto& bk = blocks.try_emplace({(int)std::floor(x / 60), (int)std::floor(z / 60)}, std::make_pair(1e9, -1e9)).first->second;
                        bk.first = std::min(bk.first, h), bk.second = std::max(bk.second, h);
                        const double gx = w.cellHeight(w.cellIndex(x + 1, z)) - w.cellHeight(w.cellIndex(x - 1, z)), gz = w.cellHeight(w.cellIndex(x, z + 1)) - w.cellHeight(w.cellIndex(x, z - 1));
                        cellsIn++, steep += std::hypot(gx, gz) / 2 > std::tan(36 * M_PI / 180);
                    }
                }
                // Outside its blend the ground is exactly as it was.
                if (o > fp.blend + 1 && std::fabs(h - base.cellHeight(id)) > 1e-5) ctx.check(tag + ": ground outside the blend untouched", false, "cell moved at " + std::to_string(x) + "," + std::to_string(z));
            }
        ctx.check(tag + ": heights finite", finite);
        std::string why;
        const bool inOk = drivable(w, b.entry, why);
        ctx.check(tag + ": entry is drivable ground", inOk, why);
        const bool outOk = drivable(w, b.exit, why);
        ctx.check(tag + ": exit is drivable ground", outOk, why);
        ctx.check(tag + ": entry is where it was asked", std::hypot(b.entry.x - f.entry.x, b.entry.z - f.entry.z) < 1e-6 && std::fabs(b.entry.y - f.entry.y) < 0.6);
        ctx.check(tag + ": exit is its length along", std::fabs(std::hypot(b.exit.x - b.entry.x, b.exit.z - b.entry.z) - fp.length) < 6.0);
        // Its own character.
        int slabs = 0, trees = 0, track = 0, tooClose = 0, tooClose2 = 0;
        for (const Box& bx : w.boxes) slabs += bx.rock && std::hypot(bx.c.x - f.entry.x, bx.c.z - f.entry.z) < 400;
        for (const Tree& tr2 : w.trees) {
            if (outsideOf(fp, f, tr2.base.x, tr2.base.z) >= 0) continue;
            trees++;
            const double dx = tr2.base.x - f.entry.x, dz = tr2.base.z - f.entry.z;
            const double lat = std::fabs(dx * std::sin(f.heading) + dz * std::cos(f.heading));
            track += lat < 2.0;
            tooClose2 += lat < 10.5;  // (the mud pit's edge is 7.5 m out)
            tooClose += lat > 1.2 && lat < 3.9;  // (the ones deliberately standing in the track are within 1 m of the line)
        }
        switch (t) {
            case SEC_STEEP_HILL: ctx.check(tag + ": climbs", b.exit.y - b.entry.y > 8, std::to_string(b.exit.y - b.entry.y)); break;
            case SEC_MUD_PIT:
                ctx.check(tag + ": mud and water", mud > 300 && wet > 100, std::to_string(mud) + " mud, " + std::to_string(wet) + " wet");
                ctx.check(tag + ": trunks stand 3 m back from the mud's edge", tooClose2 == 0, std::to_string(tooClose2) + " trees too close");
                break;
            case SEC_ROCKY_RIVER: ctx.check(tag + ": a river of water", wet > 400, std::to_string(wet)); break;
            case SEC_ROCK_WALL: ctx.check(tag + ": slab boxes", slabs >= 60, std::to_string(slabs)); break;
            case SEC_DUNES: {
                for (const auto& bk : blocks) blockRelief = std::max(blockRelief, bk.second.second - bk.second.first);
                ctx.check(tag + ": a sea of sand", sand > 20000 && dirt > 3000, std::to_string(sand) + " sand, " + std::to_string(dirt) + " firm");
                ctx.check(tag + ": dunes taller than 8 m (big ones 15 m and up)", blockRelief > 15, std::to_string(blockRelief) + " m in a 60 m block");
                ctx.check(tag + ": drivable: no slope over 36 degrees but the odd cell (1%)", steep * 100 < cellsIn, std::to_string(steep) + " of " + std::to_string(cellsIn) + " cells");
                // Never start (or end) on sand: cars bog.
                int bog = 0;
                for (double uu : {0.0, fp.length})
                    for (double uv = -6; uv <= 6; uv += 2)
                        for (double du2 = -6; du2 <= 6; du2 += 2) {
                            const double ex = f.entry.x + std::cos(f.heading) * (uu + du2) + std::sin(f.heading) * uv, ez = f.entry.z - std::sin(f.heading) * (uu + du2) + std::cos(f.heading) * uv;
                            bog += w.surfaceAt(ex, ez) == SURF_SAND;
                        }
                ctx.check(tag + ": starts and ends on firm ground", bog == 0, std::to_string(bog) + " sandy spots");
                break;
            }
            case SEC_CLIFF_SHELF: ctx.check(tag + ": a real drop", lo < b.entry.y - 15, std::to_string(b.entry.y - lo)); break;
            case SEC_TIMBER:
                ctx.check(tag + ": trees, with some in the track", trees > 250 && track >= 4 && !w.logs.empty(), std::to_string(trees) + " trees, " + std::to_string(track) + " in the track");
                ctx.check(tag + ": trunks stand back 2 m from the track's edge", tooClose == 0, std::to_string(tooClose) + " trees too close");
                break;
            default: break;
        }
    }
}

}  // namespace

SCENARIO(sectionSteepHill, "sections/steep-hill", "The steep hill builds at four headings: finite, ends drivable, only its own ground changed, it climbs") { checkSection(ctx, SEC_STEEP_HILL); }
SCENARIO(sectionMudPit, "sections/mud-pit", "The mud pit builds at four headings: finite, ends drivable, only its own ground changed, mud and water") { checkSection(ctx, SEC_MUD_PIT); }
SCENARIO(sectionRockyRiver, "sections/rocky-river", "The rocky river builds at four headings: finite, ends drivable, only its own ground changed, water") { checkSection(ctx, SEC_ROCKY_RIVER); }
SCENARIO(sectionRockWall, "sections/rock-wall", "The rock wall builds at four headings: finite, ends drivable, only its own ground changed, its slab boxes") { checkSection(ctx, SEC_ROCK_WALL); }
SCENARIO(sectionDunes, "sections/dunes", "The dunes build at four headings: finite, ends drivable, only their own ground changed, sand and relief") { checkSection(ctx, SEC_DUNES); }
SCENARIO(sectionCliffShelf, "sections/cliff-shelf", "The cliff shelf builds at four headings: finite, ends drivable, only its own ground changed, a drop") { checkSection(ctx, SEC_CLIFF_SHELF); }
SCENARIO(sectionTimber, "sections/timber", "The timber track builds at four headings: finite, ends drivable, only its own ground changed, trees in the track and logs") { checkSection(ctx, SEC_TIMBER); }

SCENARIO(sectionSameSeed, "sections/same-seed", "The same seed builds the same section; another seed a different one") {
    for (int t = 0; t < SEC_COUNT; t++) {
        auto build = [&](uint32_t seed, size_t& rocks, size_t& boxes, double& sum) {
            World w(MAP_PROVING_GROUND);
            const SectionFrame f = testFrame(w, (SectionType)t, 0);
            buildSection(w, (SectionType)t, seed, f);
            rocks = w.rocks.size(), boxes = w.boxes.size(), sum = 0;
            for (double x = f.entry.x - 10; x < f.entry.x + 150; x += 3)
                for (double z = f.entry.z - 40; z < f.entry.z + 40; z += 3) sum += w.height(x, z) * (1 + std::fmod(x * 0.37 + z * 0.11 + 1000, 1.0));
        };
        size_t r1, b1, r2, b2, r3, b3;
        double s1, s2, s3;
        build(1, r1, b1, s1), build(1, r2, b2, s2), build(2, r3, b3, s3);
        ctx.check(std::string(sectionName((SectionType)t)) + ": same seed, same section", r1 == r2 && b1 == b2 && s1 == s2);
        ctx.check(std::string(sectionName((SectionType)t)) + ": another seed, another section", s1 != s3 || r1 != r3 || b1 != b3);
    }
}

// ---- Rock and uneven ground on the driven line ------------------------------------------------------------------------

namespace {

struct LineCase {
    SectionType t;
    const char* name;
    int rocksNear;  // at least this many rocks within 3 m of the line
};
const LineCase kLineCases[] = {{SEC_STEEP_HILL, "steep hill", 30}, {SEC_MUD_PIT, "mud pit", 6}, {SEC_ROCK_WALL, "rock wall", 60}, {SEC_TIMBER, "timber", 10}, {SEC_ROCKY_RIVER, "rocky river", 10}};

// The line as (u, v)-free samples of height, and the ground at (u, v) metres from the entry along the frame.
double groundAt(const World& w, const SectionFrame& f, double u, double v) {
    const double x = f.entry.x + std::cos(f.heading) * u + std::sin(f.heading) * v, z = f.entry.z - std::sin(f.heading) * u + std::cos(f.heading) * v;
    return w.height(x, z);
}

}  // namespace

SCENARIO(sectionRocksOnLine, "sections/rocks-on-line", "The hill, mud pit, rock wall, timber and river have scanned-style rocks standing along the driven line (and none on the entry and exit pads)") {
    for (const LineCase& lc : kLineCases)
        for (uint32_t seed : {1u, 2u}) {
            const std::string tag = std::string(lc.name) + " seed " + std::to_string(seed);
            World w(MAP_PROVING_GROUND);
            const size_t before = w.rocks.size();
            const SectionFrame f = testFrame(w, lc.t, seed == 1 ? 0 : M_PI / 2);
            const SectionBuilt b = buildSection(w, lc.t, seed, f);
            ctx.check(tag + ": a line is reported", b.line.size() > 20, std::to_string(b.line.size()));
            int near3 = 0, onPad = 0;
            for (size_t i = before; i < w.rocks.size(); i++) {
                double best = 1e9;
                for (size_t k = 0; k < b.line.size(); k++) {
                    const double d = std::hypot(b.line[k].x - w.rocks[i].c.x, b.line[k].z - w.rocks[i].c.z);
                    best = std::min(best, d);
                    if (d < 6 && (k < 8 || k + 8 >= b.line.size())) onPad++;
                }
                near3 += best < 3;
            }
            ctx.check(tag + ": rocks on the line", near3 >= lc.rocksNear, std::to_string(near3) + " within 3 m of it (want " + std::to_string(lc.rocksNear) + ")");
            ctx.check(tag + ": the entry and exit pads are clear of rock", onPad == 0, std::to_string(onPad));
        }
}

SCENARIO(sectionUneven, "sections/uneven-ground", "The ground along each section's line fluctuates (bumps, dips, ruts), while the entry and exit pads stay flat for 15 m") {
    for (const LineCase& lc : kLineCases)
        for (uint32_t seed : {1u, 2u}) {
            const std::string tag = std::string(lc.name) + " seed " + std::to_string(seed);
            World w(MAP_PROVING_GROUND);
            const SectionFrame f = testFrame(w, lc.t, seed == 1 ? M_PI : 0.5);
            const double L = sectionLength(lc.t);
            buildSection(w, lc.t, seed, f);
            // The ground's height every metre down the line, less its own trend (a 17 m moving mean): what's left is the bumps.
            std::vector<double> y;
            for (double u = 0; u <= L; u += 1) y.push_back(groundAt(w, f, u, 0));
            double sum2 = 0;
            int n = 0;
            for (int i = 18; i + 18 < (int)y.size(); i++) {
                double mean = 0;
                for (int k = -8; k <= 8; k++) mean += y[i + k] / 17;
                sum2 += (y[i] - mean) * (y[i] - mean), n++;
            }
            const double sd = std::sqrt(sum2 / n);
            ctx.check(tag + ": the line's ground varies", sd > 0.05, std::to_string(sd) + " m (want over 0.05)");
            // The pads: flat to a few cm about their own plane, across the width a truck uses.
            for (int end = 0; end < 2; end++) {
                const double u0 = end ? L - 15 : -15, u1 = end ? L + 15 : 15;
                double worst = 0;
                for (double v : {-2.0, 0.0, 2.0}) {
                    std::vector<double> h;
                    for (double u = u0; u <= u1; u += 1) h.push_back(groundAt(w, f, u, v));
                    // (a straight line fitted through it)
                    const int m = (int)h.size();
                    const double mx = (m - 1) / 2.0;
                    double mh = 0, sxy = 0, sxx = 0;
                    for (double hv : h) mh += hv / m;
                    for (int i = 0; i < m; i++) sxy += (i - mx) * (h[i] - mh), sxx += (i - mx) * (i - mx);
                    for (int i = 0; i < m; i++) worst = std::max(worst, std::fabs(h[i] - (mh + sxy / sxx * (i - mx))));
                }
                ctx.check(tag + (end ? ": exit pad flat" : ": entry pad flat"), worst < 0.12, std::to_string(worst) + " m off a straight line (want under 0.12)");
            }
        }
}

SCENARIO(sectionRocksSame, "sections/rocks-same-seed", "A section's rocks, slabs and line are exactly the same for the same seed, and differ for another") {
    for (const LineCase& lc : kLineCases) {
        auto build = [&](uint32_t seed, double& sum, size_t& n, double& lineSum) {
            World w(MAP_PROVING_GROUND);
            const size_t r0 = w.rocks.size();
            const SectionFrame f = testFrame(w, lc.t, 0);
            const SectionBuilt b = buildSection(w, lc.t, seed, f);
            sum = 0, lineSum = 0;
            for (size_t i = r0; i < w.rocks.size(); i++) sum += w.rocks[i].c.x * 1.3 + w.rocks[i].c.y * 2.7 + w.rocks[i].c.z * 0.9 + w.rocks[i].r;
            for (const V3& p : b.line) lineSum += p.x + 2 * p.y + 3 * p.z;
            n = w.rocks.size() - r0;
        };
        double s1, s2, s3, l1, l2, l3;
        size_t n1, n2, n3;
        build(1, s1, n1, l1), build(1, s2, n2, l2), build(2, s3, n3, l3);
        ctx.check(std::string(lc.name) + ": same seed, same rocks and line", s1 == s2 && n1 == n2 && l1 == l2);
        ctx.check(std::string(lc.name) + ": another seed, other rocks", s1 != s3 || n1 != n3);
    }
}

SCENARIO(sectionPark, "sections/park", "The game's proving ground has its section park clear of the spawn pad, the loop road and the courses; the tests' one has none") {
    const World plain(MAP_PROVING_GROUND);
    ctx.check("no park without the showcase", plain.sections.empty() && plain.tags.empty());
    const bool was = World::withShowcase;
    World::withShowcase = true;
    const World game(MAP_PROVING_GROUND);
    World::withShowcase = was;
    ctx.check("seven sections laid out", game.sections.size() == SEC_COUNT, std::to_string(game.sections.size()));
    int tagged = 0;
    for (const World::SectionSpot& s : game.sections) {
        for (const World::Tag& tg : game.tags) tagged += tg.text == s.name && std::hypot(tg.p.x - s.entry.x, tg.p.z - s.entry.z) < 1;
        std::string why;
        ctx.check(s.name + ": entry drivable", drivable(game, s.entry, why), why);
        ctx.check(s.name + ": exit drivable", drivable(game, s.exit, why), why);
        ctx.check(s.name + ": well away from the spawn pad", std::hypot(s.entry.x, s.entry.z) > 400);
    }
    ctx.check("a label at every entry", tagged == SEC_COUNT, std::to_string(tagged));
    // The spawn, the parking and the shop: nothing solid near, ground and surface as they were.
    std::vector<V3> spots = game.parking;
    spots.push_back(game.spawn);
    for (const V3& p : spots) {
        int near = 0;  // (anything solid within 10 m of a car's footprint here)
        for (const Box& bx : game.boxes) near += std::hypot(bx.c.x - p.x, bx.c.z - p.z) < bx.bound + 10;
        for (const Rock& rk : game.rocks) near += std::hypot(rk.c.x - p.x, rk.c.z - p.z) < rk.r + 10;
        for (const Pole& pl : game.poles) near += std::hypot(pl.x - p.x, pl.z - p.z) < pl.r + 10;
        for (const Log& lg : game.logs) near += std::hypot(lg.a.x - p.x, lg.a.z - p.z) < 10;
        ctx.check("spawn and parking clear", near == 0, std::to_string(p.x) + "," + std::to_string(p.z) + ": " + std::to_string(near));
        ctx.check("pad ground is the same", std::fabs(game.height(p.x, p.z) - plain.height(p.x, p.z)) < 1e-6 && game.surfaceAt(p.x, p.z) == plain.surfaceAt(p.x, p.z));
    }
    // The pad and the surface course keep their ground; so does the whole loop road.
    int moved = 0, roadMoved = 0;
    for (double x = -250; x <= 250; x += 1)
        for (double z = -250; z <= 250; z += 1) {
            const int id = game.cellIndex(x, z);
            moved += std::fabs(game.cellHeight(id) - plain.cellHeight(id)) > 1e-6 || game.cellSurface(id) != plain.cellSurface(id);
        }
    for (const V3& p : game.loopRoad)
        for (double a = -9; a <= 9; a += 3)
            for (double b = -9; b <= 9; b += 3) {
                const int id = game.cellIndex(p.x + a, p.z + b);
                roadMoved += std::fabs(game.cellHeight(id) - plain.cellHeight(id)) > 1e-6 || game.cellSurface(id) != plain.cellSurface(id);
            }
    ctx.check("the pad and its surroundings are untouched", moved == 0, std::to_string(moved) + " cells");
    ctx.check("the loop road is untouched", roadMoved == 0, std::to_string(roadMoved) + " cells");
    for (const Station& st : plain.stations) {
        bool kept = false;
        for (const Station& g : game.stations) kept |= std::hypot(g.p.x - st.p.x, g.p.z - st.p.z) < 1e-6;
        ctx.check("the repair station is kept", kept);
    }
}

// ---- Hand-made sections (sectionfile.h): the editor's section mode ---------------------------------------------

namespace {

// A section authored the way the editor does: a new canvas, a generated rock wall stamped in, then hand edits
// (a mound, mud, a pool, a slab, a log), with its metadata.
void authorSection(World& w, EditCore& ed) {
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    lv.name = "TEST SECTION";
    ed.begin(w, lv);
    ed.newSection(defaultSectionCanvas());
    ed.stampSection(SEC_ROCK_WALL, 3);
    const V3 e = ed.level.start;
    ed.brush(BRUSH_RAISE, e.x + 30, e.z + 30, 5, 1.5);
    int mud = 0;
    for (int k = 0; k < (int)paintMaterials().size(); k++)
        if (paintMaterials()[k].surface == SURF_MUD) mud = k;
    ed.paint(e.x + 60, e.z - 22, 4, mud, false);
    ed.brush(BRUSH_LOWER, e.x + 180, e.z + 26, 4, 1.6);  // (past the wall, on its run-out)
    ed.flood(e.x + 180, e.z + 26, 0.6, 12);
    LevelObject slab;
    slab.kind = OBJ_BOX, slab.sub = 1, slab.p = V3(e.x + 36, 0, e.z + 12), slab.size = V3(1.5, 0.3, 1.0), slab.yaw = 0.4, slab.col[0] = 0.7f, slab.col[1] = 0.45f, slab.col[2] = 0.3f;
    ed.addObject(ed.grounded(slab));
    LevelObject log;
    log.kind = OBJ_LOG, log.p = V3(e.x + 150, 0, e.z - 20), log.p2 = V3(e.x + 150, 0, e.z - 14), log.size = V3(0.35, 0, 0);
    ed.addObject(ed.grounded(log));
    ed.settings("meta", [](Level& l) {
        l.section.kind = SK_LEDGES, l.section.difficulty = 4;
        l.section.tests = ST_SKILL | ST_COMMIT, l.section.kit = SKIT_4L | SKIT_LOCKERS | SKIT_AIRDOWN;
    });
}

}  // namespace

SCENARIO(sectionMeta, "sections/meta", "A section's type, difficulty, tests and kit save in comment lines and load back; old levels load as they did") {
    Level lv;
    lv.name = "WALL TEST";
    lv.map = MAP_PROVING_GROUND, lv.mode = LEVEL_SPRINT;
    lv.hasStart = true, lv.start = V3(-250, 0, 450);
    lv.gates.push_back({V3(-100, 0, 450), 0.0, 14});
    lv.section = defaultSectionCanvas();
    lv.section.on = true, lv.section.kind = SK_CROSSING, lv.section.difficulty = 5, lv.section.tests = ST_PROBLEM | ST_COOP;
    lv.section.kit = SKIT_WINCH | SKIT_BUDDY, lv.section.gen = SEC_ROCKY_RIVER, lv.section.genSeed = 7;
    const std::string text = saveLevelText(lv);
    ctx.check("written as comment lines", text.find("\n#section CROSSING 5 PROBLEM,COOP WINCH,BUDDY\n") != std::string::npos && text.find("\n#canvas ") != std::string::npos && text.find("\n#stamp 2 7\n") != std::string::npos);
    Level back;
    std::string err;
    ctx.check("parses", parseLevelText(text, back, &err), err);
    const SectionInfo& b = back.section;
    ctx.check("type, difficulty, tests, kit", b.on && b.kind == SK_CROSSING && b.difficulty == 5 && b.tests == (ST_PROBLEM | ST_COOP) && b.kit == (SKIT_WINCH | SKIT_BUDDY));
    ctx.check("canvas and generator", std::fabs(b.x - lv.section.x) < 0.01 && std::fabs(b.z - lv.section.z) < 0.01 && std::fabs(b.length - 260) < 0.1 && std::fabs(b.half - 45) < 0.1 && b.gen == SEC_ROCKY_RIVER && b.genSeed == 7);
    ctx.check("saves the same again", saveLevelText(back) == text);
    // An older build skips comment lines: without them it's the same level, just not a section.
    std::string plain, line;
    std::istringstream in(text);
    while (std::getline(in, line))
        if (line.empty() || line[0] != '#') plain += line + "\n";
    Level old;
    ctx.check("without the comments it still loads", parseLevelText(plain, old, &err), err);
    ctx.check("as an ordinary level", !old.section.on && old.name == lv.name && old.gates.size() == 1 && old.hasStart);
    // Old files: every level the game ships round-trips with no section; a file written before sections existed loads.
    int ships = 0, bad = 0;
    for (const Level& g : builtinLevels()) {
        Level r;
        ships++;
        if (!parseLevelText(saveLevelText(g), r) || r.section.on || saveLevelText(r) != saveLevelText(g)) bad++;
    }
    ctx.check("the game's levels load unchanged", ships > 0 && bad == 0, std::to_string(bad) + " of " + std::to_string(ships));
    Level older;
    ctx.check("a file from before sections", parseLevelText("CRUMPLEZONE LEVEL 1\nname OLD ONE\nabout FROM THE PAST\nmap 0\nmode sprint\nstart 1 2 90\ngate 5 6 0 12\nheights 600 600 1.5 2.5\n# a comment\n", older, &err), err);
    ctx.check("with its things", older.name == "OLD ONE" && older.hasStart && older.gates.size() == 1 && older.heights.size() == 2 && !older.section.on);
    // Garbled section lines don't break it.
    Level garbled;
    ctx.check("garbled section lines are tolerated", parseLevelText("CRUMPLEZONE LEVEL 1\n#section\n#canvas x\n#stamp\nname G\n", garbled, &err), err);
}

SCENARIO(sectionNewCanvas, "sections/new-canvas", "NEW SECTION clears and levels the canvas well away from everything, with the entry and exit set; plays the same loaded") {
    const bool was = World::withShowcase;
    World::withShowcase = true;  // (the game's proving ground: the yard, the park, the road)
    World w(MAP_PROVING_GROUND);
    World::withShowcase = was;
    const SectionInfo c = defaultSectionCanvas();
    // Where it is: clear of the spawn pad, the loop road, the courses, the model yard and the park's sections (a margin of 40 m).
    bool clear = true;
    std::string why;
    auto bad = [&](const std::string& what) { clear = false, why = what; };
    for (double u = -40; u <= c.length + 40; u += 5)
        for (double v = -c.half - 40; v <= c.half + 40; v += 5) {
            const double x = c.x + u, z = c.z + v;
            if (std::hypot(x, z) < 175) bad("the spawn pad");
            if (w.roadAt(x, z).near) bad("a road");
            if (w.waterLevel(x, z) > -1e8) bad("water");
            if (x > -150 && x < -10 && z > -340 && z < -20) bad("the model yard");
            if (x > -130 && x < -40 && z > 60 && z < 135) bad("the surface courses");
            if (std::fabs(x) > 560 || std::fabs(z) > 560) bad("the map's edge");
            for (const World::SectionSpot& s : w.sections) {
                SectionFrame f;
                f.entry = s.entry, f.heading = s.heading;
                if (outsideOf(sectionFootprint((SectionType)s.type), f, x, z) < 0) bad("a park section");  // (its cleared ground and blend: the dunes are 440 m long)
            }
        }
    for (const V3& p : w.loopRoad)
        if (c.outside(p.x, p.z) < 40) bad("the loop road");
    ctx.check("the default canvas is clear of everything", clear, why);
    EditCore ed;
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    ed.begin(w, lv);
    const size_t trees0 = w.trees.size();
    ed.newSection(c);
    ctx.check("trees came out", w.trees.size() < trees0, std::to_string(trees0) + " -> " + std::to_string(w.trees.size()));
    int standing = 0, bushes = 0;
    for (const Rock& r : w.rocks) standing += c.outside(r.c.x, r.c.z) < 24;
    for (const Tree& t : w.trees) standing += c.outside(t.base.x, t.base.z) < 24;
    for (const Box& b : w.boxes) standing += !b.hidden && c.outside(b.c.x, b.c.z) < 24;
    for (const Log& g : w.logs) standing += c.outside(g.a.x, g.a.z) < 24;
    for (const Bush& b : w.bushes) bushes += c.outside(b.p.x, b.p.z) < 24;
    ctx.check("nothing stands on the canvas or its blend", standing == 0 && bushes == 0, std::to_string(standing) + " objects, " + std::to_string(bushes) + " bushes");
    double lo = 1e9, hi = -1e9;
    for (double u = 0; u <= c.length; u += 1)
        for (double v = -c.half; v <= c.half; v += 1) {
            const double h = w.height(c.x + u, c.z + v);
            lo = std::min(lo, h), hi = std::max(hi, h);
        }
    ctx.metric("canvas relief", hi - lo, "m", 0, 0.02);
    const Level& l = ed.level;
    ctx.check("a sprint with the entry and one gate", l.mode == LEVEL_SPRINT && l.hasStart && l.gates.size() == 1 && l.section.on);
    ctx.metric("entry from the back edge", l.start.x - c.x, "m", 39.9, 40.1);
    ctx.metric("exit from the entry", l.gates[0].p.x - l.start.x, "m", 179.9, 180.1);
    ctx.check("entry on a whole metre, facing along", l.start.x == std::round(l.start.x) && l.start.z == std::round(l.start.z) && std::fabs(l.startYaw - c.heading) < 1e-6);
    ctx.check("nothing to undo (it's the start)", !ed.canUndo());
    // Played: the level on a fresh world is the same cleared ground.
    World p(MAP_PROVING_GROUND);
    applyLevel(ed.level, p);
    double worst = 0;
    for (double u = -10; u <= c.length + 10; u += 3)
        for (double v = -c.half - 10; v <= c.half + 10; v += 3) worst = std::max(worst, std::fabs(p.height(c.x + u, c.z + v) - w.height(c.x + u, c.z + v)));
    int pStanding = 0;
    for (const Tree& t : p.trees) pStanding += c.outside(t.base.x, t.base.z) < 24;
    for (const Rock& r : p.rocks) pStanding += c.outside(r.c.x, r.c.z) < 24;
    for (const Bush& b : p.bushes) pStanding += c.outside(b.p.x, b.p.z) < 26;
    ctx.metric("played vs edited ground", worst, "m", 0, 0.02);
    ctx.check("played: still nothing there", pStanding == 0, std::to_string(pStanding));
}

SCENARIO(sectionStamp, "sections/stamp", "Stamping each generator into the canvas makes its objects the level's own, moves the exit, and undo puts it all back") {
    for (int t = 0; t < SEC_COUNT; t++) {
        World w(MAP_PROVING_GROUND);
        EditCore ed;
        Level lv;
        lv.map = MAP_PROVING_GROUND;
        ed.begin(w, lv);
        ed.newSection(defaultSectionCanvas());
        const std::string tag = sectionName((SectionType)t);
        const SectionInfo c = ed.level.section;
        const V3 entry = ed.level.start;
        const size_t cells0 = ed.level.heights.size();
        std::vector<double> before;
        for (double u = 0; u <= c.length; u += 7) before.push_back(w.height(c.x + u, c.z + 11));
        const int made = ed.stampSection(t, 3);
        ctx.check(tag + ": stamped", made >= 0 && ed.level.section.gen == t && ed.level.section.genSeed == 3 && ed.level.section.kind != SK_CUSTOM);
        double moved = 0;
        int k = 0;
        for (double u = 0; u <= c.length; u += 7) moved = std::max(moved, std::fabs(w.height(c.x + u, c.z + 11) - before[k++]));
        ctx.check(tag + ": the ground changed", moved > 0.2 || t == SEC_TIMBER, std::to_string(moved));
        ctx.check(tag + ": its objects are the level's", (int)ed.level.objects.size() == made && (made > 0 || t == SEC_DUNES || t == SEC_CLIFF_SHELF || t == SEC_MUD_PIT), std::to_string(made));
        const double L = ed.level.gates.back().p.x - entry.x;
        ctx.check(tag + ": the exit is where it ends", std::fabs(L - sectionLength((SectionType)t)) < 1.0 && ed.level.gates.size() == 1, std::to_string(L));
        ctx.check(tag + ": the world has them too", (int)w.rocks.size() + (int)w.boxes.size() + (int)w.trees.size() + (int)w.logs.size() > 0);
        if (t == SEC_ROCK_WALL) {
            int slabs = 0;
            for (const LevelObject& o : ed.level.objects) slabs += o.kind == OBJ_BOX && o.sub == 1;
            ctx.check(tag + ": the ledges are rock slab boxes", slabs >= 60, std::to_string(slabs));
        }
        ctx.check(tag + ": undo puts it back", ed.undo() && ed.level.objects.empty() && ed.level.section.gen == -1 && ed.level.heights.size() == cells0);
        double after = 0;
        k = 0;
        for (double u = 0; u <= c.length; u += 7) after = std::max(after, std::fabs(w.height(c.x + u, c.z + 11) - before[k++]));
        ctx.check(tag + ": the ground is flat again", after < 0.01, std::to_string(after));
    }
}

SCENARIO(sectionLocalFrame, "sections/local-frame", "Author, save, place at another frame: the same section, heights from its entry, whatever the entry height and heading") {
    World w(MAP_PROVING_GROUND);
    EditCore ed;
    authorSection(w, ed);
    SectionFile sf;
    std::string err;
    ctx.check("extracts", extractSection(w, ed.level, sf, &err), err);
    ctx.check("measured from the entry", std::fabs(sf.u0 + 40) < 1e-6 && std::fabs(sf.u1 - 220) < 1e-6 && sf.half == 45 && sf.nu == 261 && sf.nv == 91, std::to_string(sf.u0) + " " + std::to_string(sf.u1) + " " + std::to_string(sf.nu));
    ctx.check("its exit is where the rock wall ends", std::fabs(sf.length() - sectionLength(SEC_ROCK_WALL)) < 0.01 && std::fabs(sf.exitLocal().z) < 0.01, std::to_string(sf.length()));
    ctx.check("objects kept in the frame", !sf.objects.empty() && sf.dropped == 0, std::to_string(sf.objects.size()) + " objects, " + std::to_string(sf.dropped) + " dropped");
    // The file: text and back.
    const std::string text = saveSectionText(sf);
    SectionFile back;
    ctx.check("the file parses", parseSectionText(text, back, &err), err);
    double dh = 0, dwater = 0;
    int dsurf = 0, wet = 0, mudCells = 0;
    for (size_t k = 0; k < sf.h.size(); k++) {
        dh = std::max(dh, (double)std::fabs(sf.h[k] - back.h[k]));
        dsurf += sf.surf[k] != back.surf[k];
        mudCells += (sf.surf[k] >> 8) == SURF_MUD;
        if (sf.water[k] > -1e8f) wet++, dwater = std::max(dwater, (double)std::fabs(sf.water[k] - back.water[k]));
        else if (back.water[k] > -1e8f) dwater = 99;
    }
    ctx.metric("heights through the file", dh, "m", 0, 0.006);
    ctx.check("surfaces and water through the file", dsurf == 0 && dwater < 0.006 && wet > 20 && mudCells > 20, std::to_string(dsurf) + " surfaces off, " + std::to_string(wet) + " wet, " + std::to_string(mudCells) + " mud");
    ctx.check("metadata, objects and gates through the file",
              back.info.kind == SK_LEDGES && back.info.difficulty == 4 && back.info.tests == (ST_SKILL | ST_COMMIT) && back.info.kit == (SKIT_4L | SKIT_LOCKERS | SKIT_AIRDOWN) &&
                  back.info.gen == SEC_ROCK_WALL && back.name == "TEST SECTION" && back.objects.size() == sf.objects.size() && back.gates.size() == 1 && std::fabs(back.length() - sf.length()) < 0.01);
    ctx.check("and saves the same again", saveSectionText(back) == text);
    // Placed at other frames, on land at another height: the same section from its entry.
    struct Frame {
        V3 at;
        double heading;
        const char* name;
        double tol;
    };
    for (const Frame& fr : {Frame{V3(-300, 0, -300), 0, "heading 0", 0.015}, Frame{V3(-250, 0, 120), M_PI / 2, "turned left 90", 0.015},
                            Frame{V3(300, 0, 200), M_PI, "turned round", 0.015}, Frame{V3(-220, 0, -260), 0.5, "turned 29 degrees", 0.12}}) {
        World p(MAP_PROVING_GROUND);
        SectionFrame f;
        f.entry = fr.at;
        f.entry.y = p.height(fr.at.x, fr.at.z);
        f.heading = fr.heading;
        const SectionBuilt b = placeSection(p, back, f);
        p.buildGrid();
        const std::string tag = fr.name;
        double sum = 0, worst = 0;
        int n = 0, surfOff = 0;
        for (int i = 4; i < sf.nu - 4; i++)
            for (int j = 4; j < sf.nv - 4; j++) {
                const V3 q = sectionToWorld(f, V3(sf.u0 + i, 0, -sf.half + j));
                const double e = std::fabs((p.height(q.x, q.z) - f.entry.y) - sf.h[sf.at(i, j)]);
                sum += e, worst = std::max(worst, e), n++;
                if (fr.heading == 0 || std::fabs(std::fmod(fr.heading, M_PI / 2)) < 1e-9) {
                    const int id = p.cellIndex(q.x, q.z);
                    surfOff += (uint16_t)(p.cellSurface(id) << 8 | p.cellPaint(id)) != sf.surf[sf.at(i, j)];
                }
            }
        ctx.metric(tag + ": mean height error from the entry", sum / n, "m", 0, fr.tol);
        if (fr.tol < 0.05) ctx.metric(tag + ": worst height error", worst, "m", 0, fr.tol);
        ctx.check(tag + ": surfaces", surfOff == 0, std::to_string(surfOff));
        // Where the entry and exit came out.
        ctx.check(tag + ": entry where asked", std::hypot(b.entry.x - fr.at.x, b.entry.z - fr.at.z) < 1e-6);
        const V3 d(b.exit.x - b.entry.x, 0, b.exit.z - b.entry.z);
        ctx.check(tag + ": the exit is as far along the heading", std::fabs(len(d) - sf.length()) < 0.1 && std::fabs(dot(norm(d), V3(std::cos(fr.heading), 0, -std::sin(fr.heading))) - 1) < 1e-6, std::to_string(len(d)));
        // Objects: the same count, each slab where it was in the frame, turned with the heading.
        int slabsFile = 0, slabsPlaced = 0;
        for (const LevelObject& o : sf.objects) slabsFile += o.kind == OBJ_BOX;
        for (const Box& bx : p.boxes) {
            const V3 l = sectionToLocal(f, bx.c);
            slabsPlaced += l.x > sf.u0 && l.x < sf.u1 && std::fabs(l.z) < sf.half;
        }
        ctx.check(tag + ": the slabs, all of them", slabsFile > 60 && slabsPlaced == slabsFile, std::to_string(slabsPlaced) + " of " + std::to_string(slabsFile));
        // The hand-placed slab (the last box in the file) is where it was, and turned by the heading.
        const LevelObject* mine = nullptr;
        for (const LevelObject& o : sf.objects)
            if (o.kind == OBJ_BOX && std::fabs(o.size.x - 1.5) < 1e-6 && std::fabs(o.size.y - 0.3) < 1e-6) mine = &o;
        bool found = false;
        if (mine) {
            const V3 want = sectionToWorld(f, mine->p);
            for (const Box& bx : p.boxes)
                if (len(bx.c - want) < 0.02 && std::fabs(bx.h.x - 1.5) < 1e-6)
                    found = std::fabs(dot(bx.ax[0], V3(std::cos(mine->yaw + f.heading), 0, -std::sin(mine->yaw + f.heading))) - 1) < 1e-3 && bx.rock;
        }
        ctx.check(tag + ": the hand-made slab, in place and turned", found);
        ctx.check(tag + ": its log", p.logs.size() >= 1);
        // The water pool: level above the entry as it was.
        const V3 pool = sectionToWorld(f, V3(180, 0, 26));
        ctx.check(tag + ": the pool is wet", p.waterLevel(pool.x, pool.z) > p.height(pool.x, pool.z) + 0.3, std::to_string(p.waterLevel(pool.x, pool.z) - p.height(pool.x, pool.z)));
        // Round it: beyond the blend nothing moved.
        const World plain(MAP_PROVING_GROUND);
        int moved = 0;
        for (double u = sf.u0 - 80; u <= sf.u1 + 80; u += 2)
            for (double v = -sf.half - 80; v <= sf.half + 80; v += 2) {
                if (std::max({sf.u0 - u, u - sf.u1, std::fabs(v) - sf.half}) < sf.blend + 2) continue;
                const V3 q = sectionToWorld(f, V3(u, 0, v));
                const int id = p.cellIndex(q.x, q.z);
                moved += id >= 0 && std::fabs(p.cellHeight(id) - plain.cellHeight(id)) > 1e-5;
            }
        ctx.check(tag + ": the land beyond the blend is as it was", moved == 0, std::to_string(moved));
    }
}

SCENARIO(sectionFiles, "sections/files", "Saving a section writes the level and its .czs; the level reopens with its metadata and the .czs places the same section") {
    World w(MAP_PROVING_GROUND);
    EditCore ed;
    authorSection(w, ed);
    const std::string path = "build/test_section_files.czl", spath = sectionPathFor(path);
    ctx.check("the .czs goes beside the .czl", spath == "build/test_section_files.czs");
    std::string err;
    ctx.check("saves", ed.save(path, &err) && ed.saveNote.empty(), err + ed.saveNote);
    Level lv;
    ctx.check("the level reloads", loadLevelFile(path, lv, &err), err);
    ctx.check("with its metadata", lv.section.on && lv.section.kind == SK_LEDGES && lv.section.difficulty == 4 && lv.section.gen == SEC_ROCK_WALL);
    // Reopened in the editor, it extracts the same section.
    World w2(MAP_PROVING_GROUND);
    EditCore ed2;
    ed2.begin(w2, lv);
    SectionFile a, b, fromFile;
    ctx.check("extracts from the reopened level", extractSection(w, ed.level, a, &err) && extractSection(w2, ed2.level, b, &err), err);
    ctx.check("loads the section file", loadSectionFile(spath, fromFile, &err), err);
    double worst = 0;
    for (size_t k = 0; k < a.h.size(); k++) worst = std::max(worst, (double)std::fabs(a.h[k] - b.h[k]));
    ctx.metric("the reopened level's section vs the one saved", worst, "m", 0, 0.012);
    ctx.check("the .czs is that section", fromFile.objects.size() == a.objects.size() && fromFile.nu == a.nu && fromFile.info.tests == a.info.tests);
    // A level without an exit saves, but says why there's no section file.
    World w3(MAP_PROVING_GROUND);
    EditCore ed3;
    Level l3;
    l3.map = MAP_PROVING_GROUND;
    ed3.begin(w3, l3);
    ed3.newSection(defaultSectionCanvas());
    ed3.removeGate(0);
    std::remove(spath.c_str());
    ctx.check("saves without an exit", ed3.save(path, &err), err);
    ctx.check("and says why no .czs", ed3.saveNote.find("NO EXIT") != std::string::npos, ed3.saveNote);
    std::remove(path.c_str());
    std::remove(spath.c_str());
}

SCENARIO(sectionFlow, "sections/flow", "A stamped river keeps its current: in the level (saved and loaded), in the section file, and turned with the heading when placed") {
    World w(MAP_PROVING_GROUND);
    EditCore ed;
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    ed.begin(w, lv);
    ed.newSection(defaultSectionCanvas());
    ed.stampSection(SEC_ROCKY_RIVER, 2);
    const V3 e = ed.level.start;
    // The river's middle: 65 m on from the entry (the generator's channel), where the current runs.
    double speed = 0;
    int moving = 0;
    for (double u = 50; u <= 80; u += 1)
        for (double v = -3; v <= 3; v += 1) {
            const V3 f = w.waterFlow(e.x + u, e.z + v);
            if (len(f) > 0.3) moving++, speed = std::max(speed, len(f));
        }
    ctx.check("the editor's world has the current", moving > 20 && speed > 1.0, std::to_string(moving) + " cells, " + std::to_string(speed) + " m/s");
    ctx.check("the level keeps it", !ed.level.flow.empty(), std::to_string(ed.level.flow.size()));
    // Saved and loaded: the same current on a fresh world.
    Level back;
    std::string err;
    ctx.check("level parses", parseLevelText(saveLevelText(ed.level), back, &err), err);
    World p(MAP_PROVING_GROUND);
    applyLevel(back, p);
    double worst = 0;
    for (double u = 50; u <= 80; u += 1)
        for (double v = -3; v <= 3; v += 1) worst = std::max(worst, len(p.waterFlow(e.x + u, e.z + v) - w.waterFlow(e.x + u, e.z + v)));
    ctx.metric("current, saved and loaded vs edited", worst, "m/s", 0, 0.02);
    // The section file, placed turned left 90 degrees: the same speed, the direction turned with it.
    SectionFile sf, fromText;
    ctx.check("extracts", extractSection(w, ed.level, sf, &err), err);
    ctx.check("section file parses", parseSectionText(saveSectionText(sf), fromText, &err), err);
    World q(MAP_PROVING_GROUND);
    SectionFrame f;
    f.entry = V3(-250, 0, 120);
    f.entry.y = q.height(f.entry.x, f.entry.z);
    f.heading = M_PI / 2;
    placeSection(q, fromText, f);
    q.buildGrid();
    double speedErr = 0, dirErr = 0;
    int n = 0;
    for (double u = 50; u <= 80; u += 1)
        for (double v = -3; v <= 3; v += 1) {
            const V3 a = w.waterFlow(e.x + u, e.z + v);
            if (len(a) < 0.3) continue;
            const V3 wp = sectionToWorld(f, V3(u, 0, v));
            const V3 b = q.waterFlow(wp.x, wp.z);
            // (along u at the source is along u at the target: compare in the frame)
            const double c0 = std::cos(0.0), s0 = std::sin(0.0), c1 = std::cos(f.heading), s1 = std::sin(f.heading);
            const V3 la(a.x * c0 - a.z * s0, 0, a.x * s0 + a.z * c0), lb(b.x * c1 - b.z * s1, 0, b.x * s1 + b.z * c1);
            speedErr = std::max(speedErr, std::fabs(len(a) - len(b))), dirErr = std::max(dirErr, len(la - lb));
            n++;
        }
    ctx.check("placed, the water moves", n > 20, std::to_string(n));
    ctx.metric("placed current's speed vs the source", speedErr, "m/s", 0, 0.02);
    ctx.metric("placed current's direction in the frame vs the source", dirErr, "m/s", 0, 0.03);
}

SCENARIO(sectionDuneProfile, "sections/dune-profile", "duneHeight: a gentle windward back, a steep slip face near the angle of repose, tall crests, the same for the same seed") {
    DuneParams P;
    P.halfU = 220, P.halfV = 68;
    P.sub = 0;  // (the dominant wave alone: the smaller dunes over it add their own slopes, and the section settles the sum to the repose)
    const double th = P.windDeg * M_PI / 180;
    // Along the wind through the middle of the field: the steepest climb and the steepest drop.
    double up = 0, down = 0, tall = 0, tallestStep = 0;
    int slipSteps = 0, backSteps = 0;
    for (double q = -330; q < 330; q += 0.5) {
        const double h0 = duneHeight(q * std::cos(th), q * std::sin(th), P, 1), h1 = duneHeight((q + 0.5) * std::cos(th), (q + 0.5) * std::sin(th), P, 1);
        const double s = (h1 - h0) / 0.5;
        tall = std::max(tall, h1);
        if (s > 0) up = std::max(up, s), backSteps += s > 0.1;
        else down = std::max(down, -s), slipSteps += -s > 0.1;
        tallestStep = std::max(tallestStep, std::fabs(s));
    }
    const double upDeg = std::atan(up) * 180 / M_PI, downDeg = std::atan(down) * 180 / M_PI;
    ctx.check("dune crests taller than 8 m exist (the big ones near 20)", tall > 15, std::to_string(tall) + " m");
    ctx.check("windward backs are gentle (10 to 18 degrees)", upDeg > 8 && upDeg < 18, std::to_string(upDeg));
    ctx.check("slip faces are steeper than the backs", downDeg > upDeg + 8, std::to_string(downDeg) + " against " + std::to_string(upDeg));
    ctx.check("slip faces stand near the angle of repose (28 to 38 degrees)", downDeg > 28 && downDeg < 38, std::to_string(downDeg));
    ctx.check("the backs are longer than the slip faces", backSteps > slipSteps * 2, std::to_string(backSteps) + " against " + std::to_string(slipSteps));
    ctx.check("the same seed, the same dunes; another seed, other dunes", duneHeight(31, 17, P, 1) == duneHeight(31, 17, P, 1) && std::fabs(duneHeight(31, 17, P, 1) - duneHeight(31, 17, P, 2)) + std::fabs(duneHeight(-70, 5, P, 1) - duneHeight(-70, 5, P, 2)) > 0.01);
    ctx.check("nothing outside the field", duneHeight(260, 0, P, 1) == 0 && duneHeight(0, 90, P, 1) == 0);
}

SCENARIO(sectionParkSpace, "sections/park-space", "The park's sections keep apart from each other and from the loop road (cleared ground and blends)") {
    const bool was = World::withShowcase;
    World::withShowcase = true;
    const World game(MAP_PROVING_GROUND);
    World::withShowcase = was;
    auto outsideSpot = [&](const World::SectionSpot& s, double x, double z) {
        SectionFrame f;
        f.entry = s.entry, f.heading = s.heading;
        return outsideOf(sectionFootprint((SectionType)s.type), f, x, z);
    };
    int overlaps = 0, nearRoad = 0, offMap = 0;
    for (double x = -540; x <= 540; x += 4)
        for (double z = -540; z <= 540; z += 4) {
            int in = 0;
            for (const World::SectionSpot& s : game.sections) in += outsideSpot(s, x, z) < 0;
            overlaps += in > 1;
        }
    for (const World::SectionSpot& s : game.sections) {
        for (const V3& p : game.loopRoad) nearRoad += outsideSpot(s, p.x, p.z) < 12;
        const SectionFootprint fp = sectionFootprint((SectionType)s.type);
        for (double u : {-fp.apron, fp.length + fp.apron})
            for (double v : {-fp.half, fp.half}) {
                const double x = s.entry.x + std::cos(s.heading) * u + std::sin(s.heading) * v, z = s.entry.z - std::sin(s.heading) * u + std::cos(s.heading) * v;
                offMap += std::fabs(x) > 560 || std::fabs(z) > 560;
            }
        ctx.check(s.name + ": the model yard and the pad are not under it", outsideSpot(s, -80, -60) > 0 && outsideSpot(s, 0, 0) > 0);
    }
    ctx.check("no two sections' cleared ground overlap", overlaps == 0, std::to_string(overlaps) + " cells");
    ctx.check("the loop road is well clear of every section", nearRoad == 0, std::to_string(nearRoad) + " road points");
    ctx.check("every section's corners are on the map", offMap == 0);
    int dune = -1;
    for (size_t i = 0; i < game.sections.size(); i++) dune = game.sections[i].type == SEC_DUNES ? (int)i : dune;
    ctx.check("the dunes are the park's biggest by far (300+ m across)", dune >= 0 && sectionFootprint(SEC_DUNES).length >= 300 && sectionFootprint(SEC_DUNES).half * 2 >= 140);
    ctx.check("sections are in the order of their types", dune == SEC_DUNES);
}
