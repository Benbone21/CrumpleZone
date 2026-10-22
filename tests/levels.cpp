// Levels: the file format, putting edits on a map, the game's own levels, and playing one.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../src/level.h"
#include "../src/levelrun.h"
#include "../src/sections.h"
#include "harness.h"

SCENARIO(levelFormat, "levels/format", "A level with every kind of edit saves to text and loads back the same, and its edits land on the map") {
    Level lv;
    lv.name = "TEST LEVEL";
    lv.about = "ALL THE THINGS";
    lv.map = MAP_PROVING_GROUND, lv.mode = LEVEL_SPRINT, lv.vehicle = 2, lv.resets = false, lv.maxDamage = 25;
    lv.medals[0] = 10, lv.medals[1] = 20, lv.medals[2] = 30;
    lv.hasStart = true, lv.start = V3(-20, 0, 10), lv.startYaw = 0.5;
    lv.gates.push_back({V3(0, 0, 10), 0.0, 12});
    lv.gates.push_back({V3(30, 0, 10), 0.2, 14});
    World w(MAP_PROVING_GROUND);
    const int a = w.cellIndex(50, 50), b = w.cellIndex(51, 50);
    lv.heights[a] = 3.25f, lv.heights[b] = 3.5f;
    lv.surfaces[a] = (uint16_t)(SURF_MUD << 8 | PAINT_MUD);
    lv.water[b] = 4.0f;
    LevelObject rock;
    rock.kind = OBJ_ROCK, rock.p = V3(-30, 0, -30), rock.size = V3(1.2, 0.8, 1.0), rock.top = 0.7, rock.seed = 77;
    lv.objects.push_back(rock);
    LevelObject box;
    box.kind = OBJ_BOX, box.p = V3(-40, 0.5, -30), box.size = V3(2, 0.5, 1.5), box.yaw = 0.3, box.sub = 1;
    lv.objects.push_back(box);
    LevelObject tree;
    tree.kind = OBJ_TREE, tree.p = V3(-50, 0, -30), tree.size = V3(0, 9, 0), tree.sub = 0;
    lv.objects.push_back(tree);
    LevelObject log;
    log.kind = OBJ_LOG, log.p = V3(-60, 0.3, -30), log.p2 = V3(-60, 0.3, -24), log.size = V3(0.3, 0, 0);
    lv.objects.push_back(log);
    LevelObject st;
    st.kind = OBJ_STATION, st.p = V3(-80, 0, -60), st.name = "TEST SHOP";
    lv.objects.push_back(st);
    const std::string text = saveLevelText(lv);
    Level back;
    std::string err;
    ctx.check("parses", parseLevelText(text, back, &err), err);
    ctx.check("name, mode, rules", back.name == lv.name && back.mode == LEVEL_SPRINT && back.vehicle == 2 && !back.resets && back.maxDamage == 25);
    ctx.check("start and gates", back.hasStart && back.gates.size() == 2 && std::fabs(back.gates[1].yaw - 0.2) < 1e-3 && back.gates[1].width == 14);
    ctx.check("edits", back.heights.size() == 2 && std::fabs(back.heights[b] - 3.5f) < 1e-3 && back.surfaces[a] == lv.surfaces[a] && back.water.size() == 1);
    ctx.check("objects", back.objects.size() == 5 && back.objects[4].name == "TEST SHOP");
    ctx.check("saves the same again", saveLevelText(back) == text);
    // On the map.
    const size_t rocks0 = w.rocks.size(), trees0 = w.trees.size(), logs0 = w.logs.size(), st0 = w.stations.size();
    applyLevel(back, w);
    ctx.check("height edited", std::fabs(w.height(50, 50) - 3.25) < 0.05, std::to_string(w.height(50, 50)));
    ctx.check("mud painted", w.surfaceAt(50, 50) == SURF_MUD);
    ctx.check("water there", w.waterLevel(51, 50) > 3.9);
    ctx.check("objects added", w.rocks.size() == rocks0 + 1 && w.trees.size() == trees0 + 1 && w.logs.size() == logs0 + 1 && w.stations.size() == st0 + 1);
    // Taking one away.
    Level rm;
    rm.map = MAP_PROVING_GROUND;
    rm.removed.push_back(V3(-30, 0, -30));
    applyLevel(rm, w);
    ctx.check("removed the rock", w.rocks.size() == rocks0);
    ctx.check("junk is refused", !parseLevelText("hello\n", back, &err));
}

SCENARIO(levelBuiltins, "levels/builtin", "The game's own levels: each starts on dry ground with its gates on the ground, in sensible order") {
    const std::vector<Level>& all = builtinLevels();
    ctx.metric("levels", (double)all.size(), "", 6, 50);
    for (const Level& lv : all) {
        World w = levelWorld(lv);
        applyLevel(lv, w);
        bool ok = lv.hasStart && lv.gates.size() >= 2;
        std::string why;
        auto dry = [&](const V3& p) { return w.waterLevel(p.x, p.z) < w.height(p.x, p.z) + 0.3; };
        if (!dry(lv.start)) ok = false, why += " start under water;";
        // On the ground, not inside a rock or under a trunk: the spot a car is put at is the ground itself.
        if (lv.hasStart && w.clearY(lv.start.x, lv.start.z) - w.height(lv.start.x, lv.start.z) > 0.6) ok = false, why += " start inside something;";
        double longest = 0;
        V3 prev = lv.start;
        for (size_t k = 0; k < lv.gates.size(); k++) {
            const Gate& g = lv.gates[k];
            longest = std::max(longest, std::hypot(g.p.x - prev.x, g.p.z - prev.z));
            prev = g.p;
            if (!std::isfinite(g.p.x + g.p.z) || std::fabs(g.p.x) > 590 || std::fabs(g.p.z) > 590) ok = false, why += " gate off the map;";
        }
        std::printf("LEVEL %-20s %zu gates, longest leg %.0f m\n", lv.name.c_str(), lv.gates.size(), longest);
        if (longest > 500) ok = false, why += " a leg over 500 m;";
        ctx.check(lv.name, ok, why);
    }
}

SCENARIO(levelRunSprint, "levels/run-sprint", "Playing a sprint on the proving ground: countdown holds the car, gates count in order, the finish and its medal") {
    Level lv;
    lv.name = "STRAIGHT";
    lv.map = MAP_PROVING_GROUND, lv.mode = LEVEL_SPRINT;
    // (The acceleration tests' lane: clear for 200 m.)
    lv.hasStart = true, lv.start = V3(-120, 0, -52.8), lv.startYaw = 0;
    for (double x : {-90.0, -60.0, -30.0}) lv.gates.push_back({V3(x, 0, -50), 0.0, 12});
    lv.medals[0] = 7, lv.medals[1] = 9, lv.medals[2] = 12;
    ctx.sim.world = World(MAP_PROVING_GROUND);
    applyLevel(lv, ctx.sim.world);
    Car& c = ctx.add(vehicleCatalog()[2], -120, -50, 0);
    ctx.focus(c);
    ctx.run(0.5);
    LevelRun run;
    run.level = lv;
    run.begin(ctx.sim, 0, 2.0);
    const double x0 = c.frame().com.x;
    ctx.run(1.5, [&] {
        c.throttle = 1;  // jumping the start: held anyway
        run.update(ctx.sim, Ctx::kFrame);
    });
    ctx.check("held during the countdown", std::fabs(c.frame().com.x - x0) < 0.5 && run.state == LevelRun::COUNTDOWN);
    ctx.runUntil(30, [&] { return run.state == LevelRun::FINISHED; }, [&] {
        run.update(ctx.sim, Ctx::kFrame);
        if (run.state == LevelRun::RUNNING) {
            c.throttle = 1, c.brake = 0, c.handbrake = 0;
            const Frame f = c.frame();
            c.steer = clampd(std::atan2(dot(V3(f.com.x + 10, f.com.y, -50) - f.com, f.left), 10) * 1.0, -0.3, 0.3);
        }
        static thread_local int fr = 0;
        if (std::getenv("CZ_DEBUG") && ++fr % 60 == 0)
            std::printf("SPRINT t %.1f state %d clock %.1f com (%.1f %.1f) v %.1f gate %d thr %.1f hb %.1f gear %d\n", ctx.t, (int)run.state, run.clock, c.frame().com.x, c.frame().com.z,
                        len(c.frame().vel), run.gate, c.throttle, c.handbrake, c.gear);
    });
    ctx.check("finished", run.state == LevelRun::FINISHED);
    ctx.metric("time", run.finishTime, "s", 3, 12);
    ctx.check("three splits", run.splits.size() == 3);
    ctx.check("progress went out", !run.outbox.empty() && run.outbox.back().rfind("fin ", 0) == 0);
    ctx.check("on the board", !run.board.empty() && run.board[0].time > 0);
}

SCENARIO(levelRunRace, "levels/run-race", "A two-lap race round a square: gates passed outside the posts don't count, laps do, and the board ranks others' progress") {
    Level lv;
    lv.name = "SQUARE";
    lv.map = MAP_PROVING_GROUND, lv.mode = LEVEL_RACE, lv.laps = 2;
    lv.hasStart = true, lv.start = V3(-40, 0, -40), lv.startYaw = 0;
    // A loop: east along z=-40, north at x=40, west at z=40, south at x=-40 (yaws for each side).
    lv.gates.push_back({V3(40, 0, -40), 0.0, 14});
    lv.gates.push_back({V3(40, 0, 40), -M_PI / 2, 14});  // heading north (+z)
    lv.gates.push_back({V3(-40, 0, 40), M_PI, 14});
    lv.gates.push_back({V3(-40, 0, -40), M_PI / 2, 14});  // heading south
    LevelRun run;
    run.level = lv;
    ctx.sim.world = World(MAP_PROVING_GROUND);
    Car& c = ctx.add(vehicleCatalog()[0], -40, -40, 0);
    ctx.focus(c);
    ctx.run(0.5);
    run.begin(ctx.sim, 0, 0.5);
    // Teleport-free check of the gate logic: walk the car's position by hand.
    auto moveTo = [&](double x, double z) {
        const V3 com = c.frame().com;
        for (Node& n : c.nodes) n.p += V3(x - com.x, 0, z - com.z);
        run.update(ctx.sim, Ctx::kFrame);
        if (std::getenv("CZ_DEBUG"))
            std::printf("MOVE (%.1f %.1f) com (%.1f %.1f) state %d gate %d lap %d player %d cars %zu\n", x, z, c.frame().com.x, c.frame().com.z, (int)run.state, run.gate, run.lap,
                        ctx.sim.player, ctx.sim.cars.size());
    };
    run.update(ctx.sim, 0.6);  // green light
    moveTo(39, -60), moveTo(41, -60);  // passes gate 0's line outside the posts
    ctx.check("outside the posts doesn't count", run.gate == 0);
    for (int lapN = 0; lapN < 2; lapN++) {
        moveTo(39, -40), moveTo(41, -40);
        moveTo(40, 39), moveTo(40, 41);
        moveTo(-39, 40), moveTo(-41, 40);
        moveTo(-40, -39), moveTo(-40, -41);
        if (lapN == 0) ctx.check("lap 2 started", run.lap == 1 && run.gate == 0, std::to_string(run.lap) + "/" + std::to_string(run.gate));
    }
    ctx.check("finished after two laps", run.state == LevelRun::FINISHED, std::to_string(run.lap) + " " + std::to_string(run.gate));
    run.onMessage(3, "BUDDY", "prog 5 20.0");
    run.onMessage(4, "PAL", "out TOO DAMAGED");
    ctx.check("others on the board", run.board.size() == 3);
}

// How far (x, z) is outside any of the world's spawn pads, ramp included (negative on one).
static double outsidePads(const World& w, double x, double z) {
    double best = 1e9;
    for (const SpawnPad& pd : w.spawnPads) {
        const double dx = x - pd.p.x, dz = z - pd.p.z, u = dx * std::cos(pd.yaw) - dz * std::sin(pd.yaw), v = dx * std::sin(pd.yaw) + dz * std::cos(pd.yaw);
        best = std::min(best, std::max(std::max(-pd.back - u, u - pd.front - pd.ramp), std::fabs(v) - pd.half));
    }
    return best;
}

// The obstacle sections as levels of their own: on open country, with nothing of the proving ground round them.
SCENARIO(levelSections, "levels/sections", "Each section level is on the clean open country: no course, pad, road, yard or park near it, its start on flat clear ground, no seam at its canvas edge") {
    int found = 0;
    for (const Level& lv : builtinLevels()) {
        if (!lv.section.live) continue;
        found++;
        World w = levelWorld(lv);
        applyLevel(lv, w);
        const std::string& n = lv.name;
        ctx.check(n + ": on the open country", lv.map == MAP_WILDS && w.map == MAP_WILDS);
        // Nothing the proving ground lays out: its pad (paved), course strips, yard, park, shop, loop road, ramps and walls.
        bool paved = false;
        for (double x = -590; x <= 590 && !paved; x += 10)
            for (double z = -590; z <= 590; z += 10)
                if (outsidePads(w, x, z) > 1 && (w.surfaceAt(x, z) == SURF_PAVED || w.roadAt(x, z).paint == PAINT_PAD) || w.courseSurface(x, z) >= 0 || w.roadAt(x, z).paint == PAINT_ROAD) {
                    paved = true;
                    break;
                }
        ctx.check(n + ": no pad, course or road anywhere (but its own start's slab)", !paved);
        ctx.check(n + ": one spawn pad, at the start", w.spawnPads.size() == 1 && outsidePads(w, lv.start.x, lv.start.z) < 0);
        int solidBoxes = 0;
        for (const Box& b : w.boxes)
            if (!b.rock && !b.hidden) solidBoxes++;
        ctx.check(n + ": no ramps, walls or poles of the test course", solidBoxes == 0 && w.loopRoad.empty() && w.stations.empty() && w.fixtures.empty());
        ctx.check(n + ": no model yard or park", std::all_of(w.models.begin(), w.models.end(), [](const Placed& m) { return m.id == "spawn_pad"; }) && w.tags.empty() && w.sections.size() == 1 && w.parking.empty());
        // The start: dry, on the ground, flat for the run onto the entry, and clear of trunks and rocks along it.
        const V3 s = lv.start;
        const V3 fwd(std::cos(lv.startYaw), 0, -std::sin(lv.startYaw));
        double minNy = 1;
        for (double d = -4; d <= 22; d += 2) {
            const V3 p = s + fwd * d;
            minNy = std::min(minNy, w.normal(p.x, p.z).y);
        }
        ctx.check(n + ": start run is flat", minNy > 0.97, std::to_string(minNy));
        bool clear = true;
        for (const Pole& p : w.poles) {
            const double a = (p.x - s.x) * fwd.x + (p.z - s.z) * fwd.z, l = std::fabs(-(p.x - s.x) * fwd.z + (p.z - s.z) * fwd.x);
            if (a > -6 && a < 22 && l < 3.5) clear = false;
        }
        for (const Rock& r : w.rocks) {
            const double a = (r.c.x - s.x) * fwd.x + (r.c.z - s.z) * fwd.z, l = std::fabs(-(r.c.x - s.x) * fwd.z + (r.c.z - s.z) * fwd.x);
            if (a > -6 - r.r && a < 22 + r.r && l < 3.5 + r.r) clear = false;
        }
        ctx.check(n + ": start run is clear", clear);
        // No seam where the section's ground meets the country: the run-in's blend is gentle, and nothing beyond the blend has been touched.
        const SectionInfo& c = lv.section;
        const V3 cf(std::cos(c.heading), 0, -std::sin(c.heading)), cl(std::sin(c.heading), 0, std::cos(c.heading));
        double steepest = 0;
        for (double back = 1; back <= 30; back += 3)
            for (double v = -c.half; v <= c.half; v += 6) {
                const V3 p = V3(c.x, 0, c.z) - cf * back + cl * v;
                steepest = std::max(steepest, 1 - w.normal(p.x, p.z).y);
            }
        ctx.check(n + ": ground leading onto the section is natural", steepest < 0.2, std::to_string(steepest));
        Level bare = lv;
        bare.section.live = false;
        const World plain = levelWorld(bare);
        double changed = 0;
        const double beyond = sectionFootprint((SectionType)c.gen).blend + 3;
        for (double x = -590; x <= 590; x += 7)
            for (double z = -590; z <= 590; z += 7)
                if (c.outside(x, z) > beyond && outsidePads(w, x, z) > 25) changed = std::max(changed, std::fabs(w.height(x, z) - plain.height(x, z)));
        ctx.check(n + ": nothing changed beyond the blend", changed < 1e-4, std::to_string(changed));
        // The same level through its text (what the host sends): no megabyte of cells, and it builds the same section.
        const std::string text = saveLevelText(lv);
        Level back;
        std::string err;
        ctx.check(n + ": text round trip", parseLevelText(text, back, &err) && back.section.live && back.terrain == lv.terrain && back.map == MAP_WILDS && saveLevelText(back) == text, err);
        ctx.check(n + ": saved as a recipe, not cells", text.size() < 4000, std::to_string(text.size()));
    }
    ctx.check("one for each section type", found == SEC_COUNT, std::to_string(found));
}

SCENARIO(levelLegacy, "levels/legacy", "Sections made before the open country (the editor's, the generator's first prototypes) load on it, not on the proving ground; ordinary levels stay where they were") {
    Level proving;
    proving.name = "ORDINARY";
    proving.map = MAP_PROVING_GROUND;
    Level back;
    ctx.check("an ordinary level stays on the proving ground", parseLevelText(saveLevelText(proving), back) && back.map == MAP_PROVING_GROUND);
    Level sec = proving;
    sec.name = "OLD SECTION";
    sec.section.on = true, sec.section.kind = SK_DUNES, sec.section.x = 240, sec.section.z = -450, sec.section.length = 260, sec.section.half = 45;
    std::string text = saveLevelText(sec);
    ctx.check("a section made on the proving ground", parseLevelText(text, back) && back.section.on && back.map == MAP_WILDS && back.terrain == WILDS_MEADOW);
    Level proto = proving;
    proto.name = "GEN HILL-9", proto.about = "GENERATED OBSTACLE SECTION (PROTOTYPE)", proto.author = "GENERATOR";
    ctx.check("a generator prototype", parseLevelText(saveLevelText(proto), back) && back.map == MAP_WILDS);
    World w = levelWorld(back);
    ctx.check("its world has none of the proving ground", w.map == MAP_WILDS && w.courseSurface(-80, 90) < 0 && w.stations.empty() && w.models.empty());
    // The wilds' land far from the pad is the proving ground's own (so old edits meet it where they were made).
    World pg(MAP_PROVING_GROUND);
    double worst = 0;
    for (double x : {-440.0, -350.0, 300.0, 400.0})
        for (double z : {-450.0, -380.0, -300.0}) worst = std::max(worst, std::fabs(w.height(x, z) - pg.height(x, z)));
    ctx.metric("land vs the proving ground's", worst, "m", 0, 2.5);
}

SCENARIO(levelGrid, "levels/grid", "A level's cells keep their place in the world when the map's grid is another size than the file's") {
    // A file on a small centred grid (7 x 7: cells -3..3 m), loaded onto the map's grid.
    const std::string text = "CRUMPLEZONE LEVEL 1\nname GRID\nmap 0\ngrid 7\nheights 3 3 2.5 2.75\nheights 0 0 1\n";
    Level lv;
    std::string err;
    ctx.check("parses", parseLevelText(text, lv, &err), err);
    World w(MAP_PROVING_GROUND);
    const int mid = w.cellIndex(0, 0), next = w.cellIndex(0, 1), corner = w.cellIndex(-3, -3);
    ctx.check("three cells", lv.heights.size() == 3, std::to_string(lv.heights.size()));
    ctx.check("the middle stays in the middle", lv.heights.count(mid) && std::fabs(lv.heights[mid] - 2.5f) < 1e-6 && lv.heights.count(next) &&
                                                    std::fabs(lv.heights[next] - 2.75f) < 1e-6);
    ctx.check("the corner stays at (-3, -3)", lv.heights.count(corner) && std::fabs(lv.heights[corner] - 1.0f) < 1e-6);
    // Saved again: on the map's own grid (the old one, so no grid line).
    const std::string again = saveLevelText(lv);
    ctx.check("no grid line on the old grid", again.find("\ngrid ") == std::string::npos);
    ctx.check("a bad grid is refused", !parseLevelText("CRUMPLEZONE LEVEL 1\nmap 0\ngrid 8\n", lv, &err));
}

SCENARIO(worldHalfSize, "world/halfsize", "A map built bigger than the default reaches its new edges: grid, heights, cells, water and saved levels") {
    World::halfSizeOverride = 1024;
    World w(MAP_WILDS);
    const int n = w.gridSize();
    ctx.check("grid", n == 2049 && w.halfSize() == 1024 && World::gridSizeFor(MAP_WILDS) == 2049, std::to_string(n));
    const int far = w.cellIndex(1020, -1020);
    ctx.check("a cell near the edge", far >= 0 && std::fabs(w.cellCenter(far).x - 1020) < 1e-9 && std::fabs(w.cellCenter(far).z + 1020) < 1e-9);
    ctx.check("off the map", w.cellIndex(1030, 0) < 0 && w.waterLevel(1030, 0) < -1e8);
    ctx.check("heights out there", std::isfinite(w.height(1000, 1000)) && std::isfinite(w.height(-1023, 1023)));
    // A level saved on this map says its grid, and loads back onto it.
    Level lv;
    lv.map = MAP_WILDS;
    lv.heights[far] = 7.0f;
    const std::string text = saveLevelText(lv);
    ctx.check("grid line", text.find("\ngrid 2049\n") != std::string::npos);
    Level back;
    std::string err;
    ctx.check("loads", parseLevelText(text, back, &err), err);
    ctx.check("the cell is where it was", back.heights.count(far) && back.heights[far] == 7.0f);
    applyLevel(back, w);
    ctx.check("and lands there", std::fabs(w.height(1020, -1020) - 7.0) < 1e-3, std::to_string(w.height(1020, -1020)));
    World::halfSizeOverride = 0;
}

#include "../src/parallel.h"
SCENARIO(worldBakeThreads, "world/bake-threads", "A map's build comes out the same on one thread as on many (the land is filled and painted in parallel)") {
    for (int map : {MAP_FRONTIER, MAP_BACKCOUNTRY, MAP_MOUNTAIN, MAP_PROVING_GROUND}) {
        parallelRowsCap = 1;
        const uint64_t one = World(map).landHash();
        parallelRowsCap = 0;
        const uint64_t many = World(map).landHash();
        ctx.check(mapName(map), one == many);
    }
}
