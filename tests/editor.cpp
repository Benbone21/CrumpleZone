// The world editor's core (no graphics): brushes, paint, water, objects, the course, undo,
// and that what it makes saves and loads back as the same world.
#include <cmath>
#include <cstdio>

#include "../src/editcore.h"
#include "harness.h"

namespace {

// A level freshly loaded onto its map, as the game would play it.
World playWorld(const Level& lv) {
    World w(lv.map);
    applyLevel(lv, w);
    return w;
}

}  // namespace

SCENARIO(editorBrush, "editor/brush", "Terrain brushes change the world and the level the same way; saved and loaded, the heights come back") {
    World w(MAP_PROVING_GROUND);
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    const double h0 = w.height(-200, 200), edge0 = w.height(-210, 200);
    ed.brush(BRUSH_RAISE, -200, 200, 8, 2.0);
    ctx.metric("raised at the centre", w.height(-200, 200) - h0, "m", 1.8, 2.1);
    ctx.metric("untouched past the rim (10 m out)", std::fabs(w.height(-210, 200) - edge0), "m", 0, 0.01);
    ctx.check("recorded in the level", !ed.level.heights.empty());
    ed.brush(BRUSH_SMOOTH, -200, 200, 8, 1.0);
    ed.brush(BRUSH_FLATTEN, -150, 150, 6, 1.0, h0 + 1.0);
    ctx.metric("flattened to the target", w.height(-150, 150) - (h0 + 1.0), "m", -0.05, 0.05);
    // Round trip: the level's text, played on a fresh map, is the same ground.
    Level back;
    std::string err;
    ctx.check("parses", parseLevelText(saveLevelText(ed.level), back, &err), err);
    World p = playWorld(back);
    double worst = 0;
    for (double x = -215; x <= -185; x += 1.5)
        for (double z = 185; z <= 215; z += 1.5) worst = std::max(worst, std::fabs(p.height(x, z) - w.height(x, z)));
    ctx.metric("saved and loaded vs edited", worst, "m", 0, 0.02);
}

SCENARIO(editorUndo, "editor/undo", "Undo puts the ground, paint and objects back as they were; redo does them again") {
    World w(MAP_PROVING_GROUND);
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    const double h0 = w.height(-220, 220);
    const Surface s0 = w.surfaceAt(-220, 220);
    const size_t rocks0 = w.rocks.size();
    ed.brush(BRUSH_RAISE, -220, 220, 6, 1.5);
    int mud = -1;
    for (int k = 0; k < (int)paintMaterials().size(); k++)
        if (paintMaterials()[k].surface == SURF_MUD) mud = k;
    ed.paint(-220, 220, 4, mud, false);
    LevelObject rock;
    rock.kind = OBJ_ROCK, rock.p = V3(-230, 0, 220), rock.size = V3(1, 0.7, 1), rock.top = 0.6, rock.seed = 3;
    ed.addObject(rock);
    ctx.check("all three happened", w.height(-220, 220) > h0 + 1 && w.surfaceAt(-220, 220) == SURF_MUD && w.rocks.size() == rocks0 + 1);
    ctx.check("undo the rock", ed.undo() && w.rocks.size() == rocks0);
    ctx.check("undo the paint", ed.undo() && w.surfaceAt(-220, 220) == s0);
    ctx.check("undo the brush", ed.undo() && std::fabs(w.height(-220, 220) - h0) < 0.01);
    ctx.check("level is empty again", ed.level.heights.empty() && ed.level.surfaces.empty() && ed.level.objects.empty());
    ctx.check("redo all", ed.redo() && ed.redo() && ed.redo() && w.rocks.size() == rocks0 + 1 && w.surfaceAt(-220, 220) == SURF_MUD);
}

SCENARIO(editorObjects, "editor/objects", "Placing, moving and deleting objects (and the map's own) shows in the world and survives save and load") {
    World w(MAP_MOUNTAIN);
    Level lv;
    lv.map = MAP_MOUNTAIN;
    EditCore ed;
    ed.begin(w, lv);
    const size_t trees0 = w.trees.size(), rocks0 = w.rocks.size();
    LevelObject t;
    t.kind = OBJ_TREE, t.p = V3(30, 0, -300), t.size = V3(0, 10, 0);
    const int k = ed.addObject(t);
    ctx.check("tree added", w.trees.size() == trees0 + 1);
    t.p = V3(34, 0, -300);
    ed.setObject(k, t);
    bool moved = false;
    for (const Tree& tr : w.trees) moved |= std::fabs(tr.base.x - 34) < 0.01 && std::fabs(tr.base.z + 300) < 0.01;
    ctx.check("tree moved", moved && w.trees.size() == trees0 + 1);
    // Take one of the map's own rocks out (picking it as the UI would: a ray down onto it).
    const Rock& victim = w.rocks[rocks0 / 2];
    const V3 vc = victim.c;
    const EditPick p = ed.pick(V3(vc.x, vc.y + 30, vc.z), V3(0, -1, 0), 1e9, true);
    ctx.check("picked a map rock", p.kind == EditPick::BASE && p.baseKind == 0, p.name);
    ctx.check("removed it", ed.removeBase(p) && w.rocks.size() == rocks0 - 1);
    Level back;
    parseLevelText(saveLevelText(ed.level), back);
    World q = playWorld(back);
    ctx.check("saved and loaded: one more tree, one less rock", q.trees.size() == trees0 + 1 && q.rocks.size() == rocks0 - 1,
              std::to_string(q.trees.size()) + " trees, " + std::to_string(q.rocks.size()) + " rocks");
    ed.removeObject(k);
    ctx.check("tree deleted", w.trees.size() == trees0);
}

SCENARIO(editorCourse, "editor/course", "Start, gates and settings: a course built in the editor plays as a level") {
    World w(MAP_PROVING_GROUND);
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    ed.setStart(-120, -52, 0);
    ed.addGate({V3(-90, 0, -50), 0.0, 12});
    ed.addGate({V3(-30, 0, -50), 0.0, 12});
    ed.addGate({V3(-60, 0, -50), 0.0, 12}, 1);  // inserted between
    ed.settings("mode", [](Level& l) {
        l.mode = LEVEL_SPRINT;
        l.name = "EDITOR SPRINT";
    });
    ctx.check("three gates in order", ed.level.gates.size() == 3 && ed.level.gates[1].p.x == -60 && ed.level.gates[2].p.x == -30);
    ed.moveGate(2, -1);
    ctx.check("reordered", ed.level.gates[1].p.x == -30);
    ed.undo();
    ctx.check("reorder undone", ed.level.gates[1].p.x == -60);
    Level back;
    parseLevelText(saveLevelText(ed.level), back);
    ctx.check("plays as a sprint", back.hasJob() && back.mode == LEVEL_SPRINT && back.name == "EDITOR SPRINT" && back.hasStart);
    ctx.check("file name", levelFileName("Dune Run!") == "dune_run");
}

SCENARIO(editorWater, "editor/water", "Flooding a dug hollow fills it to the brim and no further; drying takes it away") {
    World w(MAP_PROVING_GROUND);
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    ed.brush(BRUSH_LOWER, -60, 80, 10, 2.0);  // (on the flat test pad)
    bool spilled = false;
    const int cells = ed.flood(-60, 80, 1.0, 30, &spilled);
    ctx.metric("cells wet", cells, "", 20, 400);
    ctx.check("stayed in the hollow", !spilled);
    ctx.check("water at the middle", w.waterLevel(-60, 80) > w.height(-60, 80) + 0.5);
    ctx.check("dry outside", w.waterLevel(-60, 95) < w.height(-60, 95));
    ed.dry(-60, 80, 15);
    ctx.check("dried", w.waterLevel(-60, 80) < w.height(-60, 80));
}
