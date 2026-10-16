// Obstacle sections (sections.h): each type builds on a fresh proving ground at any heading, leaving its
// ends drivable and everything finite, touching nothing outside its own ground; the park laid out in
// the game keeps the spawn pad, the loop road and the test courses as they were.
#include <cmath>
#include <cstdio>
#include <map>

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
