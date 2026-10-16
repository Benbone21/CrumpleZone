// Obstacle sections (sections.h): each type builds on a fresh proving ground at any heading, leaving its
// ends drivable and everything finite, touching nothing outside its own ground; the park laid out in
// the game keeps the spawn pad, the loop road and the test courses as they were.
#include <cmath>
#include <cstdio>

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
        int mud = 0, sand = 0, wet = 0;
        double lo = 1e9, hi = -1e9;
        for (double x = f.entry.x - 320; x <= f.entry.x + 320; x += 1)
            for (double z = f.entry.z - 320; z <= f.entry.z + 320; z += 1) {
                const int id = w.cellIndex(x, z);
                if (id < 0) continue;
                const double h = w.cellHeight(id), o = outsideOf(fp, f, x, z);
                finite = finite && std::isfinite(h) && std::isfinite(w.waterLevel(x, z) + 0.0);
                if (o < 0) {
                    lo = std::min(lo, h), hi = std::max(hi, h);
                    mud += w.cellSurface(id) == SURF_MUD, sand += w.cellSurface(id) == SURF_SAND, wet += w.waterLevel(x, z) > h + 0.05;
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
        int slabs = 0, trees = 0, track = 0;
        for (const Box& bx : w.boxes) slabs += bx.rock && std::hypot(bx.c.x - f.entry.x, bx.c.z - f.entry.z) < 400;
        for (const Tree& tr2 : w.trees) {
            if (outsideOf(fp, f, tr2.base.x, tr2.base.z) >= 0) continue;
            trees++;
            const double dx = tr2.base.x - f.entry.x, dz = tr2.base.z - f.entry.z;
            track += std::fabs(dx * std::sin(f.heading) + dz * std::cos(f.heading)) < 2.0;
        }
        switch (t) {
            case SEC_STEEP_HILL: ctx.check(tag + ": climbs", b.exit.y - b.entry.y > 8, std::to_string(b.exit.y - b.entry.y)); break;
            case SEC_MUD_PIT: ctx.check(tag + ": mud and water", mud > 300 && wet > 100, std::to_string(mud) + " mud, " + std::to_string(wet) + " wet"); break;
            case SEC_ROCKY_RIVER: ctx.check(tag + ": a river of water", wet > 400, std::to_string(wet)); break;
            case SEC_ROCK_WALL: ctx.check(tag + ": slab boxes", slabs >= 60, std::to_string(slabs)); break;
            case SEC_DUNES: ctx.check(tag + ": sand dunes", sand > 1500 && hi - lo > 3, std::to_string(sand) + " sand, " + std::to_string(hi - lo) + " m of relief"); break;
            case SEC_CLIFF_SHELF: ctx.check(tag + ": a real drop", lo < b.entry.y - 15, std::to_string(b.entry.y - lo)); break;
            case SEC_TIMBER: ctx.check(tag + ": trees, with some in the track", trees > 250 && track >= 4 && !w.logs.empty(), std::to_string(trees) + " trees, " + std::to_string(track) + " in the track"); break;
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
