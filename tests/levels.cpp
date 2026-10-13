// Levels: the file format, putting edits on a map, the game's own levels, and playing one.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../src/level.h"
#include "../src/levelrun.h"
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
        World w(lv.map);
        applyLevel(lv, w);
        bool ok = lv.hasStart && lv.gates.size() >= 2;
        std::string why;
        auto dry = [&](const V3& p) { return w.waterLevel(p.x, p.z) < w.height(p.x, p.z) + 0.3; };
        if (!dry(lv.start)) ok = false, why += " start under water;";
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
