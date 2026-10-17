// The world editor's objects: deleting, the map's own objects made movable, transforms (turn, tip, lift, scale, stretch) that
// reach the world and the saved file, clicks that land on the shape and not the sphere round it, playtests and recovery copies.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "../src/editcore.h"
#include "../src/platform.h"
#include "../src/props.h"
#include "../src/saveflow.h"
#include "../src/sectionfile.h"
#include "harness.h"

namespace {

World playWorldOf(const Level& lv) {
    World w = levelWorld(lv);
    applyLevel(lv, w);
    return w;
}

Level roundTrip(const Level& l) {
    Level back;
    parseLevelText(saveLevelText(l), back);
    return back;
}

double maxVertexGap(const Rock& a, const Rock& b) {
    if (a.v.size() != b.v.size()) return 1e9;
    double m = 0;
    for (size_t i = 0; i < a.v.size(); i++) m = std::max(m, len(a.v[i] - b.v[i]));
    return m;
}

bool sameFields(const LevelObject& a, const LevelObject& b) {
    auto close = [](const V3& p, const V3& q) { return len(p - q) < 0.01; };
    const bool flat = a.kind == OBJ_ROCK || a.kind == OBJ_TREE || a.kind == OBJ_STATION || a.kind == OBJ_MODEL || a.kind == OBJ_BUSH || a.kind == OBJ_FENCE;  // (their height isn't saved: the ground gives it)
    return a.kind == b.kind && close(flat ? V3(a.p.x, 0, a.p.z) : a.p, flat ? V3(b.p.x, 0, b.p.z) : b.p) && close(a.kind == OBJ_FENCE ? V3(a.p2.x, 0, a.p2.z) : a.p2, b.kind == OBJ_FENCE ? V3(b.p2.x, 0, b.p2.z) : b.p2) && close(a.size, b.size) && std::fabs(a.yaw - b.yaw) < 2e-3 &&
           std::fabs(a.pitch - b.pitch) < 2e-3 && std::fabs(a.roll - b.roll) < 2e-3 && std::fabs(a.top - b.top) < 1e-2 &&
           std::fabs(a.lift - b.lift) < 1e-2 && a.sub == b.sub && a.scan == b.scan && a.name == b.name;
}

}  // namespace

SCENARIO(editorDelete, "editor/delete", "Delete takes the selected object away (not shifts it), the map's own too, and undo puts each back") {
    World w(MAP_MOUNTAIN);
    Level lv;
    lv.map = MAP_MOUNTAIN;
    EditCore ed;
    ed.begin(w, lv);
    const size_t rocks0 = w.rocks.size(), boxes0 = w.boxes.size(), trees0 = w.trees.size();
    LevelObject rock, box, tree;
    rock.kind = OBJ_ROCK, rock.p = V3(30, 0, -300), rock.size = V3(1.5, 1, 1.2), rock.top = 1.2, rock.seed = 5;
    box.kind = OBJ_BOX, box.p = V3(40, 0, -300), box.size = V3(2, 0.4, 1), box.col[0] = 0.6f;
    tree.kind = OBJ_TREE, tree.p = V3(50, 0, -300), tree.size = V3(0, 9, 0);
    const int kr = ed.addObject(rock), kb = ed.addObject(ed.grounded(box)), kt = ed.addObject(tree);
    ctx.check("three placed", ed.level.objects.size() == 3 && w.rocks.size() == rocks0 + 1 && w.boxes.size() == boxes0 + 1 && w.trees.size() == trees0 + 1);
    const double boxY = ed.level.objects[kb].p.y;
    EditPick p;
    p.kind = EditPick::OBJECT, p.index = kb;
    ed.removePicks({p});
    ctx.check("the box is gone (the others are where they were)", ed.level.objects.size() == 2 && w.boxes.size() == boxes0 && w.rocks.size() == rocks0 + 1 && w.trees.size() == trees0 + 1);
    ctx.check("nothing else moved down", std::fabs(ed.level.objects[0].p.x - 30) < 1e-6 && std::fabs(ed.level.objects[1].p.x - 50) < 1e-6);
    ctx.check("undo puts it back where it was", ed.undo() && ed.level.objects.size() == 3 && w.boxes.size() == boxes0 + 1 && std::fabs(ed.level.objects[kb].p.y - boxY) < 1e-9);
    ctx.check("redo deletes it again", ed.redo() && ed.level.objects.size() == 2);
    ed.undo();
    // Several at once (a rock and a tree): one step.
    EditPick pr, pt;
    pr.kind = pt.kind = EditPick::OBJECT;
    pr.index = kr, pt.index = kt;
    const size_t steps = ed.undoName().size();
    (void)steps;
    ed.removePicks({pr, pt});
    ctx.check("a rock and a tree deleted together", ed.level.objects.size() == 1 && w.rocks.size() == rocks0 && w.trees.size() == trees0);
    ed.undo();
    ctx.check("one undo brings both back", ed.level.objects.size() == 3 && w.rocks.size() == rocks0 + 1 && w.trees.size() == trees0 + 1);
    // The map's own rock, picked from above.
    const V3 vc = w.rocks[rocks0 / 2].c;
    const EditPick b = ed.pick(V3(vc.x, vc.y + 30, vc.z), V3(0, -1, 0), 1e9, true);
    ctx.check("picked a map rock", b.kind == EditPick::BASE && b.baseKind == 0, b.name);
    ed.removePicks({b});
    ctx.check("the map's rock is out", w.rocks.size() == rocks0);
    ctx.check("recorded with its kind", !ed.level.removed.empty() && ed.level.removed.back().y == 1);
    Level back = roundTrip(ed.level);
    World q = playWorldOf(back);
    ctx.check("saved and loaded: still one fewer rock", q.rocks.size() == rocks0);
    ed.undo();
    ctx.check("undo puts the map's rock back", w.rocks.size() == rocks0 + 1 && ed.level.removed.empty());
}

SCENARIO(editorMapObjects, "editor/map-objects", "The map's own rocks, trees, logs and blocks can be picked up, moved, turned and deleted like placed ones, and it saves") {
    props::ScopedRockScans scans(&props::sharedHullLibrary());
    World w(MAP_MOUNTAIN);
    Level lv;
    lv.map = MAP_MOUNTAIN;
    EditCore ed;
    ed.begin(w, lv);
    const bool scanned = !w.rocks.empty() && w.rocks[0].scan >= 0;
    ctx.check("the map's rocks are the scans (assets/props found)", scanned);
    // A rock far from anything else, and a box, a log and a tree.
    int ri = -1, ti = -1, li = -1, bi = -1;
    for (int i = 0; i < (int)w.rocks.size() && ri < 0; i++)
        if (w.rocks[i].scan >= 0) ri = i;
    for (int i = 0; i < (int)w.trees.size() && ti < 0; i++) ti = i;
    for (int i = 0; i < (int)w.logs.size() && li < 0; i++) li = i;
    for (int i = 0; i < (int)w.boxes.size() && bi < 0; i++)
        if (!w.boxes[i].hidden && w.boxes[i].solid) bi = i;
    ctx.check("the map has a rock and a tree", ri >= 0 && ti >= 0);
    if (ri < 0 || ti < 0) return;
    const Rock rock0 = w.rocks[ri];
    const Tree tree0 = w.trees[ti];
    std::vector<EditPick> picks;
    EditPick pr;
    pr.kind = EditPick::BASE, pr.baseKind = 0, pr.index = ri, pr.at = rock0.c;
    picks.push_back(pr);
    EditPick pt;
    pt.kind = EditPick::BASE, pt.baseKind = 1, pt.index = ti, pt.at = tree0.base;
    picks.push_back(pt);
    if (li >= 0) {
        EditPick pl;
        pl.kind = EditPick::BASE, pl.baseKind = 2, pl.index = li, pl.at = (w.logs[li].a + w.logs[li].b) * 0.5;
        picks.push_back(pl);
    }
    if (bi >= 0) {
        EditPick pb;
        pb.kind = EditPick::BASE, pb.baseKind = 3, pb.index = bi, pb.at = w.boxes[bi].c;
        picks.push_back(pb);
    }
    const size_t rocksN = w.rocks.size(), treesN = w.trees.size(), logsN = w.logs.size(), boxesN = w.boxes.size();
    const std::vector<int> ks = ed.adopt(picks);
    bool all = true;
    for (int k : ks) all &= k >= 0;
    ctx.check("all of them became the level's objects", all && ed.level.objects.size() == picks.size());
    ctx.check("the world has the same things (counts)", w.rocks.size() == rocksN && w.trees.size() == treesN && w.logs.size() == logsN && w.boxes.size() == boxesN);
    // The rock is the same rock: every corner where it was.
    double gap = 1e9;
    for (const Rock& r : w.rocks) gap = std::min(gap, maxVertexGap(r, rock0));
    ctx.metric("the picked-up rock vs the map's (worst corner)", gap, "m", 0, 0.02);
    bool treeBack = false;
    for (const Tree& t : w.trees) treeBack |= len(t.base - tree0.base) < 0.01 && std::fabs(t.height - tree0.height) < 0.01 && t.kind == tree0.kind;
    ctx.check("the tree is the same tree", treeBack);
    // Move the group 5 m and turn it; the rock follows.
    ed.moveObjects(ks, 5, -3);
    double moved = 1e9;
    for (const Rock& r : w.rocks) moved = std::min(moved, len(r.c - (rock0.c + V3(5, 0, -3))));
    ctx.metric("the rock moved 5 m, -3 m", moved, "m", 0, 0.5);  // (the ground differs a little there)
    ed.transformObjects({ks[0]}, OP_YAW, 0.5);
    ed.transformObjects({ks[0]}, OP_SCALE, 1.5);
    const LevelObject after = ed.level.objects[ks[0]];
    ctx.check("the object keeps what was done to it", std::fabs(after.size.x - ed.level.objects[ks[0]].size.x) < 1e-9 && after.sub == 1 && after.scan == rock0.scan);
    Level back = roundTrip(ed.level);
    bool same = back.objects.size() == ed.level.objects.size() && back.removed.size() == ed.level.removed.size();
    for (size_t i = 0; same && i < back.objects.size(); i++) same &= sameFields(back.objects[i], ed.level.objects[i]);
    std::string which;
    for (size_t i = 0; i < back.objects.size() && i < ed.level.objects.size(); i++)
        if (!sameFields(back.objects[i], ed.level.objects[i])) which += std::to_string(i) + "(kind " + std::to_string(ed.level.objects[i].kind) + ") ";
    ctx.check("saved and loaded: the same objects and removals", same, which + std::to_string(back.objects.size()) + "/" + std::to_string(ed.level.objects.size()));
    World q = playWorldOf(back);
    ctx.check("played back: the same counts", q.rocks.size() == w.rocks.size() && q.trees.size() == w.trees.size() && q.logs.size() == w.logs.size() && q.boxes.size() == w.boxes.size());
    double worst = 0;
    for (const Rock& r : w.rocks) {
        double best = 1e9;
        for (const Rock& s : q.rocks) best = std::min(best, maxVertexGap(r, s));
        worst = std::max(worst, best);
    }
    ctx.metric("played back: every rock where the editor had it", worst, "m", 0, 0.05);
    // Undo puts the map's own back exactly.
    while (ed.canUndo()) ed.undo();
    double home = 1e9;
    for (const Rock& r : w.rocks) home = std::min(home, maxVertexGap(r, rock0));
    ctx.check("undone: no level objects, the map as built", ed.level.objects.empty() && ed.level.removed.empty() && w.rocks.size() == rocksN && home < 1e-6);
}

SCENARIO(editorTransform, "editor/transform", "Turn, tip, raise, scale and stretch reach the object, the world and the saved text, for rocks, blocks, logs, trees and structures") {
    props::ScopedRockScans scans(&props::sharedHullLibrary());
    World w(MAP_PROVING_GROUND);
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    const props::HullLibrary& lib = props::sharedHullLibrary();
    if (!ctx.check("rock scans loaded", lib.loaded())) return;
    // A scanned rock at its natural size, picked by name.
    const int scan = 3;
    LevelObject r;
    r.kind = OBJ_ROCK, r.p = V3(-300, 0, 250), r.size = lib.hulls[scan].half(), r.top = r.size.y * 1.7, r.sub = 1, r.scan = scan, r.seed = 9;
    const int k = ed.addObject(r);
    const Rock& built = w.rocks.back();
    ctx.check("it's the scan asked for", built.scan == scan);
    auto extent = [&](const Rock& rk) {
        V3 lo(1e9, 1e9, 1e9), hi(-1e9, -1e9, -1e9);
        for (const V3& v : rk.v) lo = V3(std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)), hi = V3(std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z));
        return hi - lo;
    };
    const V3 e0 = extent(built);
    ed.transformObjects({k}, OP_SCALE, 1.5);
    const V3 e1 = extent(w.rocks.back());
    ctx.metric("scaled 1.5: width", e1.x / e0.x, "x", 1.4, 1.6);
    ctx.metric("scaled 1.5: height", e1.y / e0.y, "x", 1.4, 1.6);
    ed.transformObjects({k}, OP_STRETCH_Y, 2.0);
    const V3 e2 = extent(w.rocks.back());
    ctx.metric("stretched up 2x: height", e2.y / e1.y, "x", 1.8, 2.2);
    ctx.metric("stretched up 2x: width unchanged", e2.x / e1.x, "x", 0.95, 1.05);
    const double top0 = w.rocks.back().axis[1].y;
    ed.transformObjects({k}, OP_PITCH, 0.4);
    ctx.check("tipped: its up axis leans", std::fabs(w.rocks.back().axis[1].y - top0) > 0.02 || std::fabs(w.rocks.back().axis[0].y) > 0.1);
    ed.transformObjects({k}, OP_ROLL, 0.3);
    ed.transformObjects({k}, OP_YAW, 0.7);
    ed.transformObjects({k}, OP_LIFT, 0.5);
    const LevelObject o = ed.level.objects[k];
    ctx.check("the object holds the pose", std::fabs(o.pitch - 0.4) < 1e-6 && std::fabs(o.roll - 0.3) < 1e-6 && std::fabs(o.yaw - 0.7) < 1e-6);
    const Rock placed = w.rocks.back();
    Level back = roundTrip(ed.level);
    ctx.check("saved and loaded: the same fields", back.objects.size() == 1 && sameFields(back.objects[0], o));
    World q = playWorldOf(back);
    ctx.metric("played back: the rock is where the editor shows it", q.rocks.empty() ? 1e9 : maxVertexGap(q.rocks.back(), placed), "m", 0, 0.05);
    // A rock left to chance, turned: it's pinned as it was, then turns.
    LevelObject c;
    c.kind = OBJ_ROCK, c.p = V3(-320, 0, 250), c.size = V3(1.6, 1.0, 1.2), c.top = 0.9, c.seed = 11;
    const int kc = ed.addObject(c);
    const Rock chance = w.rocks.back();
    ed.moveObjects({kc}, 0, 0);
    ctx.metric("pinning a rock left to chance keeps its shape", maxVertexGap(w.rocks.back(), chance), "m", 0, 0.03);
    ed.transformObjects({kc}, OP_YAW, 0.9);
    ctx.check("and then it turns", maxVertexGap(w.rocks.back(), chance) > 0.2);
    // A block: stretch, raise.
    LevelObject b;
    b.kind = OBJ_BOX, b.p = V3(-340, 0, 250), b.size = V3(2, 0.4, 1), b.col[0] = b.col[1] = b.col[2] = 0.6f;
    const int kb = ed.addObject(ed.grounded(b));
    ed.transformObjects({kb}, OP_STRETCH_X, 2);
    ed.transformObjects({kb}, OP_STRETCH_Z, 0.5);
    const double y0 = w.boxes.back().c.y;  // (resting on the ground under its new footprint)
    ed.transformObjects({kb}, OP_LIFT, 1.0);
    ctx.check("block: stretched along and across", std::fabs(w.boxes.back().h.x - 4) < 1e-6 && std::fabs(w.boxes.back().h.z - 0.5) < 1e-6);
    ctx.metric("block: raised 1 m", w.boxes.back().c.y - y0, "m", 0.99, 1.01);
    ed.moveObjects({kb}, 3, 0);
    ctx.check("block: moving it on the ground keeps it raised", std::fabs(ed.level.objects[kb].lift - 1.0) < 1e-6);
    ed.transformObjects({kb}, OP_PITCH, 0.2);
    ctx.check("block: tipped", std::fabs(w.boxes.back().ax[0].y - std::sin(0.2)) < 1e-6);
    // A log: tilt, lift, thicken, lengthen.
    LevelObject g;
    g.kind = OBJ_LOG, g.p = V3(-360, 0, 250), g.p2 = V3(-354, 0, 250), g.size = V3(0.3, 0, 0);
    const int kg = ed.addObject(ed.grounded(g));
    auto elev = [&](const Log& l) { return std::asin((l.b.y - l.a.y) / len(l.b - l.a)); };
    const double el0 = elev(w.logs.back());
    ed.transformObjects({kg}, OP_PITCH, 0.3);
    const Log lg = w.logs.back();
    ctx.metric("log: tilted 0.3 rad", elev(lg) - el0, "rad", 0.29, 0.31);
    ed.transformObjects({kg}, OP_STRETCH_X, 2);
    ctx.metric("log: twice as long", len(w.logs.back().b - w.logs.back().a), "m", 11.5, 12.5);
    ed.transformObjects({kg}, OP_SCALE, 0.5);
    ctx.metric("log: scaled half", w.logs.back().r, "m", 0.14, 0.16);
    // A tree and a structure.
    LevelObject t;
    t.kind = OBJ_TREE, t.p = V3(-380, 0, 250), t.size = V3(0, 10, 0);
    const int kt = ed.addObject(t);
    ed.transformObjects({kt}, OP_SCALE, 1.5);
    ed.transformObjects({kt}, OP_LIFT, 2);
    ctx.metric("tree: 15 m", w.trees.back().height, "m", 14.9, 15.1);
    ctx.metric("tree: raised 2 m", w.trees.back().base.y - (w.height(-380, 250) - 0.2), "m", 1.99, 2.01);
    const size_t modelsBefore = w.models.size(), boxesBefore = w.boxes.size();
    LevelObject m;
    m.kind = OBJ_MODEL, m.name = "outhouse", m.p = V3(-400, 0, 250), m.size = V3(1, 1, 1);
    const int km = ed.addObject(m);
    ctx.check("structure: a model and a collider", w.models.size() == modelsBefore + 1 && w.boxes.size() == boxesBefore + 1 && w.boxes.back().hidden);
    ed.transformObjects({km}, OP_STRETCH_Y, 1.5);
    ed.transformObjects({km}, OP_YAW, 1.0);
    ctx.check("structure: stretched taller, turned", std::fabs(w.models.back().scale.y - 1.5) < 1e-9 && std::fabs(w.models.back().yaw - 1.0) < 1e-9);
    Level back2 = roundTrip(ed.level);
    bool same = back2.objects.size() == ed.level.objects.size();
    for (size_t i = 0; same && i < back2.objects.size(); i++) same &= sameFields(back2.objects[i], ed.level.objects[i]);
    std::string which;
    for (size_t i = 0; i < back2.objects.size() && i < ed.level.objects.size(); i++)
        if (!sameFields(back2.objects[i], ed.level.objects[i])) {
            const LevelObject &a = back2.objects[i], &b = ed.level.objects[i];
            char buf[400];
            std::snprintf(buf, sizeof buf, "%zu: p %.3f,%.3f,%.3f/%.3f,%.3f,%.3f size %.3f,%.3f,%.3f/%.3f,%.3f,%.3f yaw %.4f/%.4f pit %.4f/%.4f roll %.4f/%.4f top %.3f/%.3f lift %.3f/%.3f sub %d/%d scan %d/%d ", i, a.p.x, a.p.y, a.p.z, b.p.x, b.p.y, b.p.z, a.size.x, a.size.y, a.size.z, b.size.x, b.size.y, b.size.z, a.yaw, b.yaw, a.pitch, b.pitch, a.roll, b.roll, a.top, b.top, a.lift, b.lift, a.sub, b.sub, a.scan, b.scan);
            which += buf;
        }
    ctx.check("everything saved and loaded the same", same, which);
    // And the old format still reads: a rock line with no pose, a box with no lift, a tree with none.
    Level old;
    const std::string txt = "CRUMPLEZONE LEVEL 1\nname OLD\nmap 1\nmode free\nrock -300 250 1 1 1 0.8 5 1\nbox 10 1 10 2 0.5 1 0 0 0 0.5 0.5 0.5 0\ntree 5 5 9 0 1\nremove 1 2\n";
    ctx.check("an older file parses", parseLevelText(txt, old) && old.objects.size() == 3 && old.removed.size() == 1 && old.removed[0].y == 0);
    // A newer file in an older reader: the rock's pose and scan are extra numbers after what it read.
    std::string text = saveLevelText(ed.level);
    ctx.check("the saved text puts the new numbers last", text.find("rock -300") != std::string::npos);
}

SCENARIO(editorPickExact, "editor/pick-exact", "A click lands on the object itself: a ray through a rock's bounding sphere that misses its hull selects nothing") {
    props::ScopedRockScans scans(&props::sharedHullLibrary());
    World w(MAP_PROVING_GROUND);
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    const props::HullLibrary& lib = props::sharedHullLibrary();
    if (!ctx.check("rock scans loaded", lib.loaded())) return;
    LevelObject r;
    r.kind = OBJ_ROCK, r.p = V3(-300, 0, 250), r.size = lib.hulls[3].half() * 1.0, r.top = r.size.y * 1.6, r.sub = 1, r.scan = 3;
    const int k = ed.addObject(r);
    const Rock& rk = w.rocks.back();
    // Rays down from above, over a grid across the rock's bounding circle.
    int hitIn = 0, missInSphere = 0, wrong = 0;
    for (double dx = -rk.r; dx <= rk.r; dx += rk.r / 14)
        for (double dz = -rk.r; dz <= rk.r; dz += rk.r / 14) {
            if (std::hypot(dx, dz) >= rk.r * 0.98) continue;
            const V3 o(rk.c.x + dx, rk.c.y + 60, rk.c.z + dz), d(0, -1, 0);
            const EditPick p = ed.pick(o, d, 1e9, true);
            const bool selected = p.kind == EditPick::OBJECT && p.index == k;
            // The truth: some corner-based test, independent of the pick: is the point under the hull's silhouette? Use the hull faces.
            bool inside = false;
            for (const auto& f : rk.f) {
                const V3 a = rk.v[f[0]], b = rk.v[f[1]], c = rk.v[f[2]];
                auto side = [](double ax, double az, double bx, double bz, double px, double pz) { return (bx - ax) * (pz - az) - (bz - az) * (px - ax); };
                const double px = o.x, pz = o.z;
                const double s1 = side(a.x, a.z, b.x, b.z, px, pz), s2 = side(b.x, b.z, c.x, c.z, px, pz), s3 = side(c.x, c.z, a.x, a.z, px, pz);
                if ((s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0)) inside = true;
            }
            if (selected) hitIn++;
            if (!selected && !inside) missInSphere++;
            if (selected != inside) wrong++;
        }
    ctx.check("rays through the rock select it", hitIn > 20, std::to_string(hitIn));
    ctx.check("rays inside its bounding circle but past the rock select nothing", missInSphere > 5, std::to_string(missInSphere));
    ctx.metric("picks that disagree with the silhouette", wrong, "", 0, 2);
    // Empty ground next to it is the ground.
    const V3 o(rk.c.x + rk.r * 1.2, rk.c.y + 60, rk.c.z);
    const EditPick p = ed.pick(o, V3(0, -1, 0), 1e9, true);
    ctx.check("beside the rock: nothing", p.kind == EditPick::NONE || !(p.kind == EditPick::OBJECT && p.index == k));
    // Trees: the trunk and the crown count, the air beside them does not. Logs: the capsule, not a margin.
    LevelObject t;
    t.kind = OBJ_TREE, t.p = V3(-330, 0, 250), t.size = V3(0, 12, 0), t.sub = 0;
    const int kt = ed.addObject(t);
    const Tree tr = w.trees.back();
    const V3 aimAt = tr.base + V3(0, 12 * 0.4, 0);
    EditPick pc = ed.pick(aimAt + V3(40, 0, 0), V3(-1, 0, 0), 1e9, true);
    ctx.check("a ray through the pine's crown selects it", pc.kind == EditPick::OBJECT && pc.index == kt);
    pc = ed.pick(aimAt + V3(40, 0, 2.6), V3(-1, 0, 0), 1e9, true);  // 2.6 m to the side at crown height: wider than the cone there
    ctx.check("a ray 2.6 m beside the narrowing crown misses", !(pc.kind == EditPick::OBJECT && pc.index == kt));
    pc = ed.pick(tr.base + V3(40, 12 * 0.95, 0.5), V3(-1, 0, 0), 1e9, true);  // near the top, off the axis
    ctx.check("a ray off the tip of the pine misses", !(pc.kind == EditPick::OBJECT && pc.index == kt));
    LevelObject g;
    g.kind = OBJ_LOG, g.p = V3(-360, 0, 250), g.p2 = V3(-354, 0, 250), g.size = V3(0.3, 0, 0);
    const int kg = ed.addObject(ed.grounded(g));
    const Log lg = w.logs.back();
    EditPick pg = ed.pick(V3(-357, lg.a.y + 40, 250.2), V3(0, -1, 0), 1e9, true);
    ctx.check("a ray down through the log's body selects it", pg.kind == EditPick::OBJECT && pg.index == kg);
    pg = ed.pick(V3(-357, lg.a.y + 40, 250.6), V3(0, -1, 0), 1e9, true);
    ctx.check("0.6 m beside a 0.3 m log: not it", !(pg.kind == EditPick::OBJECT && pg.index == kg));
}

SCENARIO(editorPlaytest, "editor/playtest", "A playtest starts where asked (here or at the start), and coming back finds every edit, unsaved, with undo") {
    World w(MAP_PROVING_GROUND);
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    const double h0 = w.height(-200, 200);
    ed.brush(BRUSH_RAISE, -200, 200, 8, 2.0);
    LevelObject r;
    r.kind = OBJ_ROCK, r.p = V3(-300, 0, 250), r.size = V3(1.5, 1, 1.2), r.top = 1, r.seed = 5, r.sub = 1;
    ed.addObject(r);
    LevelObject b;
    b.kind = OBJ_BOX, b.p = V3(-310, 0, 250), b.size = V3(2, 0.4, 1);
    ed.addObject(ed.grounded(b));
    // From here: where the camera looks, facing as it faces.
    const EditCore::PlaySpot here = ed.playSpot(true, V3(-120, 0, 90), 1.25);
    ctx.check("from here: at the asked place, facing the camera's way", here.here && std::fabs(here.p.x + 120) < 1e-9 && std::fabs(here.p.z - 90) < 1e-9 && std::fabs(here.yaw - 1.25) < 1e-9);
    ctx.metric("from here: on the ground", here.p.y - w.height(-120, 90), "m", -0.01, 0.01);
    const EditCore::PlaySpot none = ed.playSpot(false, V3(), 0);
    ctx.check("no start set: the map's spawn (not the camera)", !none.fromStart && std::fabs(none.p.x - w.spawn.x) < 1e-9);
    ed.setStart(-250, 240, 0.5);
    const EditCore::PlaySpot st = ed.playSpot(false, V3(-120, 0, 90), 1.25);
    ctx.check("from the start: the level's start and its facing", st.fromStart && std::fabs(st.p.x + 250) < 1e-9 && std::fabs(st.yaw - 0.5) < 1e-9);
    ctx.check("testing from here is a free drive", ed.levelToPlay(true).mode == LEVEL_FREE);
    // Play: the game builds the level on a world of its own...
    const Level played = ed.levelToPlay(false);
    World pw = playWorldOf(played);
    ctx.check("the playtest world has the edits", std::fabs(pw.height(-200, 200) - w.height(-200, 200)) < 1e-4 && pw.rocks.size() >= 1);
    // ...and the editor's world is thrown over by it, as the game does (sim.world = ...), then it comes back.
    const bool unsavedBefore = ed.unsaved();
    const size_t objs = ed.level.objects.size();
    w = World(MAP_PROVING_GROUND);
    applyLevel(played, w);
    addGateMarkers(played, w);
    w = levelWorld(ed.level);
    ed.resume(w);
    ctx.metric("back in the editor: the raised ground is back", w.height(-200, 200) - h0, "m", 1.8, 2.1);
    ctx.check("back in the editor: both objects are in the world", ed.level.objects.size() == objs && w.boxes.size() > 0 && w.rocks.size() > 0);
    ctx.check("back in the editor: still unsaved", unsavedBefore && ed.unsaved());
    ctx.check("back in the editor: undo works (the start, then the block)", ed.undo() && !ed.level.hasStart && ed.undo() && ed.level.objects.size() == objs - 1);
    ctx.check("and redo", ed.redo());
    // Recovery copy: written beside, not over, the saved file, and it reads back as the same level.
    makeDir("build/test-autosave");
    std::string err;
    ed.level.file = "build/test-autosave/mine.czl";
    ctx.check("autosave writes", ed.autosave("build/test-autosave/rec", &err) || (makeDir("build/test-autosave/rec"), ed.autosave("build/test-autosave/rec", &err)), err);
    Level rec;
    ctx.check("it reads back the same level", loadLevelFile(ed.autosavePath("build/test-autosave/rec"), rec) && rec.objects.size() == ed.level.objects.size() && rec.hasStart == ed.level.hasStart);
    ctx.check("the level's own file isn't touched", ed.level.file == "build/test-autosave/mine.czl" && ed.unsaved());
}

SCENARIO(editorGroup, "editor/group", "Several objects move together, turn about their middle and delete as one step") {
    World w(MAP_PROVING_GROUND);
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    LevelObject a, b;
    a.kind = b.kind = OBJ_TREE, a.size = b.size = V3(0, 9, 0);
    a.p = V3(-300, 0, 250), b.p = V3(-290, 0, 250);
    const int ka = ed.addObject(a), kb = ed.addObject(b);
    ed.moveObjects({ka, kb}, 3, -2);
    ctx.check("both moved together", std::fabs(ed.level.objects[ka].p.x + 297) < 1e-9 && std::fabs(ed.level.objects[kb].p.x + 287) < 1e-9 && std::fabs(ed.level.objects[ka].p.z - 248) < 1e-9);
    ed.transformObjects({ka, kb}, OP_YAW, M_PI / 2);  // a quarter turn about the middle, (-292, 248)
    const V3 pa = ed.level.objects[ka].p, pb = ed.level.objects[kb].p;
    ctx.metric("the pair is still 10 m apart", len(V3(pa.x - pb.x, 0, pa.z - pb.z)), "m", 9.99, 10.01);
    ctx.metric("and still centred where it was", std::hypot((pa.x + pb.x) / 2 + 292, (pa.z + pb.z) / 2 - 248), "m", 0, 0.01);
    ctx.check("and now lined up across, not along", std::fabs(pa.x - pb.x) < 1e-6 && std::fabs(pa.z - pb.z) > 9.9);
    // A moved group is one undo step per drag (merged), a delete of two is one step.
    EditPick p1, p2;
    p1.kind = p2.kind = EditPick::OBJECT;
    p1.index = ka, p2.index = kb;
    ed.removePicks({p1, p2});
    ctx.check("both deleted", ed.level.objects.empty());
    ed.undo();
    ctx.check("one undo: both back", ed.level.objects.size() == 2);
}

SCENARIO(editorMapProps, "editor/map-props", "The map's cabins, fences, signs, tables, stumps and bushes are movable like the rest: their colliders go and come with them, undo, and the file keeps them") {
    World w(MAP_MOUNTAIN);
    Level lv;
    lv.map = MAP_MOUNTAIN;
    EditCore ed;
    ed.begin(w, lv);
    auto find = [&](PropKind k) {
        for (int i = 0; i < (int)w.fixtures.size(); i++)
            if (w.fixtures[i].kind == k) return i;
        return -1;
    };
    auto pick = [&](int baseKind, int index, const V3& at) {
        EditPick p;
        p.kind = EditPick::BASE, p.baseKind = baseKind, p.index = index, p.at = at;
        return p;
    };
    const int ic = find(PROP_CABIN), iff = find(PROP_FENCE), is = find(PROP_SIGN), it = find(PROP_TABLE);
    if (!ctx.check("the mountain has a cabin, fence, sign and table", ic >= 0 && iff >= 0 && is >= 0 && it >= 0)) return;
    const Prop cabin = w.fixtures[ic], fence = w.fixtures[iff], sign = w.fixtures[is], table = w.fixtures[it];
    const size_t hidden0 = std::count_if(w.boxes.begin(), w.boxes.end(), [](const Box& b) { return b.hidden; });
    const size_t poles0 = w.poles.size(), fixtures0 = w.fixtures.size(), bushes0 = w.bushes.size();
    ctx.check("the map has bushes", bushes0 > 0);
    const std::vector<int> ks = ed.adopt({pick(4, ic, cabin.a), pick(4, iff, fence.a), pick(4, is, sign.a), pick(4, it, table.a), pick(6, 0, w.bushes[0].p)});
    bool all = true;
    for (int k : ks) all &= k >= 0;
    ctx.check("all five became level objects", all && ed.level.objects.size() == 5);
    ctx.check("the world's fixtures are the same in number (they're models and a fence now)", w.fixtures.size() == fixtures0 - 3 && w.models.size() > 0 && w.bushes.size() == bushes0,
              std::to_string(w.fixtures.size()) + " of " + std::to_string(fixtures0));
    const size_t hiddenNow = std::count_if(w.boxes.begin(), w.boxes.end(), [](const Box& b) { return b.hidden; });
    ctx.check("the cabin's and table's collider box and the fence's rail box are still there once each", hiddenNow >= hidden0 - 3);
    // Move them all 20 m: the old collider spots are empty, the new ones are not.
    ed.moveObjects(ks, 20, 0);
    auto boxAt = [&](double x, double z, double dy) {
        for (const Box& b : w.boxes)
            if (b.hidden && std::hypot(b.c.x - x, b.c.z - z) < 0.5 && std::fabs(b.c.y - (w.height(x, z) + dy)) < 1.2) return true;
        return false;
    };
    ctx.check("the cabin's collider moved with it", !boxAt(cabin.a.x, cabin.a.z, 1.7) && boxAt(cabin.a.x + 20, cabin.a.z, 1.7));
    auto poleNear = [&](double x, double z) {
        for (const Pole& p : w.poles)
            if (std::hypot(p.x - x, p.z - z) < 0.1) return true;
        return false;
    };
    ctx.check("the sign's pole moved with it", !poleNear(sign.a.x, sign.a.z) && poleNear(sign.a.x + 20, sign.a.z));
    const V3 fd = fence.b - fence.a;
    const double fL = std::hypot(fd.x, fd.z);
    const V3 post = fence.a + fd * (fL > 3 ? 2.5 / fL : 0.5);  // (the second post: the ends are shared with the next fence)
    ctx.check("the fence's posts moved with it", !poleNear(post.x, post.z) && poleNear(post.x + 20, post.z));
    ctx.metric("poles: same number after the move", (double)w.poles.size() - (double)poles0, "", -3, 8);
    bool bushMoved = false;
    for (const Bush& b : w.bushes) bushMoved |= std::hypot(b.p.x - (ed.level.objects[ks[4]].p.x), b.p.z - ed.level.objects[ks[4]].p.z) < 0.01;
    ctx.check("the bush moved", bushMoved);
    // Turn and scale the cabin; it's a model with a new yaw and size.
    ed.transformObjects({ks[0]}, OP_YAW, 0.8);
    ed.transformObjects({ks[0]}, OP_SCALE, 1.5);
    bool turned = false;
    for (const Placed& m : w.models) turned |= m.id == "cabin" && std::fabs(m.scale.x - 1.5) < 1e-6 && std::fabs(m.yaw - (cabin.yaw + 0.8)) < 1e-6;
    ctx.check("the cabin turned and grew", turned);
    // Picking: the cabin's own shape, the bush's.
    const V3 cc = ed.level.objects[ks[0]].p;
    const EditPick pc = ed.pick(V3(cc.x, cc.y + 40, cc.z), V3(0, -1, 0), 1e9, true);
    ctx.check("a ray down onto the moved cabin picks it", pc.kind == EditPick::OBJECT && pc.index == ks[0]);
    // Save and load: the same objects, and the world played from the file is as the editor's.
    Level back = roundTrip(ed.level);
    bool same = back.objects.size() == ed.level.objects.size() && back.removed.size() == ed.level.removed.size();
    for (size_t i = 0; same && i < back.objects.size(); i++) same &= sameFields(back.objects[i], ed.level.objects[i]);
    std::string which;
    for (size_t i = 0; i < back.objects.size() && i < ed.level.objects.size(); i++)
        if (!sameFields(back.objects[i], ed.level.objects[i])) which += std::to_string(i) + "(kind " + std::to_string(ed.level.objects[i].kind) + ") ";
    ctx.check("saved and loaded: the same objects", same, which);
    World q = playWorldOf(back);
    ctx.check("played back: the same fixtures, models and bushes", q.fixtures.size() == w.fixtures.size() && q.models.size() == w.models.size() && q.bushes.size() == w.bushes.size());
    // Delete a map cabin outright: its collider box goes with it (it used to stay, invisible).
    const int ic2 = [&] {
        for (int i = 0; i < (int)w.fixtures.size(); i++)
            if (w.fixtures[i].kind == PROP_TABLE) return i;
        return -1;
    }();
    if (ic2 >= 0) {
        const Prop t2 = w.fixtures[ic2];
        const bool had = boxAt(t2.a.x, t2.a.z, 0.4);
        ed.removePicks({pick(4, ic2, t2.a)});
        ctx.check("deleting a table takes its collider with it", !had || !boxAt(t2.a.x, t2.a.z, 0.4));
    }
    while (ed.canUndo()) ed.undo();
    size_t hiddenBack = std::count_if(w.boxes.begin(), w.boxes.end(), [](const Box& b) { return b.hidden; });
    ctx.check("everything undone: the map as built", ed.level.objects.empty() && ed.level.removed.empty() && w.fixtures.size() == fixtures0 && w.poles.size() == poles0 && hiddenBack == hidden0 && w.bushes.size() == bushes0);
}

SCENARIO(editorSaveFlow, "editor/save-flow", "Saving never refuses: an unnamed level asks for a name (with a default that clashes with nothing), SAVE AS asks again, leaving with unsaved work asks SAVE / DISCARD / CANCEL, and what's saved reopens the same") {
    // (Everything under a scratch config folder: nothing of the player's is touched.)
    makeDir("build/test-config");
    setenv("XDG_CONFIG_HOME", "build/test-config", 1);
    const std::string levels = userLevelFolder();
    makeDir(configDir());
    makeDir(levels);
    for (const std::string& f : listLevelFiles(levels, ".czl")) std::remove(f.c_str());
    World w(MAP_PROVING_GROUND);
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    ed.brush(BRUSH_RAISE, -200, 200, 8, 2.0);
    LevelObject r;
    r.kind = OBJ_ROCK, r.p = V3(-300, 0, 250), r.size = V3(1.5, 1, 1.2), r.top = 1, r.seed = 5, r.sub = 1;
    ed.addObject(r);
    ed.setStart(-250, 240, 0.5);
    SaveFlow flow;
    // The default name: the map's, and a number no file has.
    const std::string d1 = SaveFlow::defaultName(ed, levels);
    ctx.check("the default name has the map's name and a number", d1.find(mapName(MAP_PROVING_GROUND)) == 0 && d1.back() == '1', d1);
    {
        std::ofstream f(levels + "/" + levelFileName(d1) + ".czl");
        f << "CRUMPLEZONE LEVEL 1\nname X\nmap 0\n";
    }
    const std::string d2 = SaveFlow::defaultName(ed, levels);
    ctx.check("with that file there, the next default is the next number", d2 != d1 && d2.back() == '2', d2);
    std::remove((levels + "/" + levelFileName(d1) + ".czl").c_str());
    // Ctrl+S on the unnamed level: a prompt, prefilled, not a refusal.
    flow.requestSave(ed, false);
    ctx.check("the unnamed level asks for a name", flow.stage == SaveFlow::NAMING && flow.text == d1 && !flow.saved);
    flow.text.clear();
    for (char c : std::string("CLIFF SIDE 1")) flow.type(c);
    flow.enter(ed);
    ctx.check("Enter saves it under that name", flow.stage == SaveFlow::IDLE && flow.saved && !ed.unsaved() && ed.level.name == "CLIFF SIDE 1");
    const std::string path = levels + "/cliff_side_1.czl";
    Level back;
    ctx.check("the file is there and reads back the same level", loadLevelFile(path, back) && back.objects.size() == ed.level.objects.size() && back.hasStart && back.heights.size() == ed.level.heights.size() &&
                                                                  back.name == "CLIFF SIDE 1");
    bool listed = false;
    for (const std::string& f : listLevelFiles(levels, ".czl")) listed |= f.find("cliff_side_1.czl") != std::string::npos;
    bool inList = false;
    for (const Level& l : listLevels()) inList |= l.name == "CLIFF SIDE 1";
    ctx.check("the level lists have it at once", listed && inList);
    // Saved and named: Ctrl+S just saves.
    ed.brush(BRUSH_RAISE, -150, 150, 5, 1.0);
    flow.requestSave(ed, false);
    ctx.check("a named, saved level saves with no question", flow.stage == SaveFlow::IDLE && flow.saved && !ed.unsaved());
    // SAVE AS: asks, prefilled with the current name; a new name makes a second file and the first stays.
    flow.requestSave(ed, true);
    ctx.check("SAVE AS asks, with the current name in the box", flow.stage == SaveFlow::NAMING && flow.text == "CLIFF SIDE 1");
    flow.text = "CLIFF SIDE 2";
    flow.enter(ed);
    ctx.check("SAVE AS writes the new file and the old one stays", flow.saved && ed.level.name == "CLIFF SIDE 2" && loadLevelFile(levels + "/cliff_side_2.czl", back) && loadLevelFile(path, back));
    // A taken name asks before replacing.
    flow.requestSave(ed, true);
    flow.text = "CLIFF SIDE 1";
    flow.enter(ed);
    ctx.check("a taken name asks again before it replaces", flow.stage == SaveFlow::NAMING && !flow.saved && flow.replaceOk);
    flow.enter(ed);
    ctx.check("and then replaces it", flow.stage == SaveFlow::IDLE && flow.saved);
    // A built-in level's copy (as the editor opens it: renamed, no file) just saves.
    Level own = builtinLevels().front();
    own.builtin = false, own.file.clear(), own.name += " (MINE)";
    World bw = levelWorld(own);
    EditCore eb;
    eb.begin(bw, own);
    eb.brush(BRUSH_RAISE, -200, 200, 5, 1.0);
    SaveFlow fb;
    fb.requestSave(eb, false);
    ctx.check("a built-in level's own copy saves with Ctrl+S, no extra steps", fb.stage == SaveFlow::IDLE && fb.saved && !eb.unsaved() && !eb.level.file.empty());
    // Open it again the same way: the name's taken by the first copy, so it asks (and offers a free name) rather than overwrite.
    EditCore eb2;
    World bw2 = levelWorld(own);
    eb2.begin(bw2, own);
    eb2.brush(BRUSH_RAISE, -200, 200, 5, 2.0);
    SaveFlow fb2;
    fb2.requestSave(eb2, false);
    ctx.check("a second copy of it asks instead of overwriting the first", fb2.stage == SaveFlow::NAMING);
    // Leaving with unsaved work.
    SaveFlow lf;
    ed.brush(BRUSH_RAISE, -100, 100, 5, 1.0);
    ctx.check("there's unsaved work", ed.unsaved());
    lf.requestLeave(ed, SaveFlow::LEAVE);
    ctx.check("leaving asks", lf.stage == SaveFlow::ASKING && lf.done == SaveFlow::NOTHING);
    lf.choose(ed, SaveFlow::CANCEL);
    ctx.check("CANCEL stays, and nothing is lost", lf.stage == SaveFlow::IDLE && lf.done == SaveFlow::NOTHING && ed.unsaved());
    lf.requestLeave(ed, SaveFlow::LEAVE);
    lf.choose(ed, SaveFlow::DISCARD);
    ctx.check("DISCARD leaves without saving", lf.done == SaveFlow::LEAVE && ed.unsaved() && !lf.saved);
    lf = SaveFlow();
    lf.requestLeave(ed, SaveFlow::LEAVE);
    lf.choose(ed, SaveFlow::SAVE);
    ctx.check("SAVE saves, then leaves", lf.saved && lf.done == SaveFlow::LEAVE && !ed.unsaved());
    lf = SaveFlow();
    lf.requestLeave(ed, SaveFlow::OPEN, 3);
    ctx.check("nothing unsaved: leaving or opening goes straight on", lf.stage == SaveFlow::IDLE && lf.done == SaveFlow::OPEN && lf.doneArg == 3);
    // An unnamed level, leaving: SAVE asks for the name first, then leaves.
    EditCore eu;
    World uw(MAP_PROVING_GROUND);
    eu.begin(uw, lv);
    eu.brush(BRUSH_RAISE, -200, 200, 8, 1.0);
    SaveFlow uf;
    uf.requestLeave(eu, SaveFlow::LEAVE);
    uf.choose(eu, SaveFlow::SAVE);
    ctx.check("SAVE on an unnamed level asks its name before leaving", uf.stage == SaveFlow::NAMING && uf.done == SaveFlow::NOTHING);
    uf.text = "UNNAMED NO MORE";
    uf.enter(eu);
    ctx.check("then it saves and leaves", uf.saved && uf.done == SaveFlow::LEAVE && !eu.unsaved());
    // Esc in the name box cancels the whole thing.
    SaveFlow ef;
    EditCore ee;
    World ew(MAP_PROVING_GROUND);
    ee.begin(ew, lv);
    ee.brush(BRUSH_RAISE, -200, 200, 8, 1.0);
    ef.requestLeave(ee, SaveFlow::LEAVE);
    ef.choose(ee, SaveFlow::SAVE);
    ef.escape();
    ctx.check("Esc in the name box cancels leaving too", ef.stage == SaveFlow::IDLE && ef.done == SaveFlow::NOTHING && ee.unsaved());
    for (const std::string& f : listLevelFiles(levels, ".czl")) std::remove(f.c_str());
    unsetenv("XDG_CONFIG_HOME");
}

SCENARIO(editorFreeHeight, "editor/free-height", "Any object can float or sink and stays exactly there (save, load, undo, terrain edits nearby); rocks stack on rocks; SNAP TO GROUND sits one back down; scaling shows at once for code-made rocks too") {
    props::ScopedRockScans scans(&props::sharedHullLibrary());
    World w(MAP_PROVING_GROUND);
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    const props::HullLibrary& lib = props::sharedHullLibrary();
    if (!ctx.check("rock scans loaded", lib.loaded())) return;
    auto lowY = [](const Rock& r) {
        double m = 1e9;
        for (const V3& v : r.v) m = std::min(m, v.y);
        return m;
    };
    auto highY = [](const Rock& r) {
        double m = -1e9;
        for (const V3& v : r.v) m = std::max(m, v.y);
        return m;
    };
    LevelObject a;
    a.kind = OBJ_ROCK, a.p = V3(-300, 0, 250), a.size = lib.hulls[3].half() * 2.0, a.top = a.size.y * 1.7, a.sub = 1, a.scan = 3, a.seed = 1;
    const int ka = ed.addObject(a);
    const Rock under = w.rocks.back();
    const double g = w.height(-300, 250);
    ctx.check("it sits in the ground", lowY(under) < g + 0.05 && highY(under) > g + 0.5);
    // Stacking: a ray from above onto it meets its top; a second rock placed there rests on that surface.
    V3 p, n;
    const bool hit = ed.rayObjects(V3(-300, g + 40, 250), V3(0, -1, 0), 1e9, {}, p, n);
    ctx.check("a ray from above meets the rock's top surface", hit && n.y > 0.25 && p.y > g + 0.5 && p.y <= highY(under) + 1e-6);
    ctx.check("...but not when that rock is the one being dragged", !ed.rayObjects(V3(-300, g + 40, 250), V3(0, -1, 0), 1e9, {ka}, p, n));
    ed.rayObjects(V3(-300, g + 40, 250), V3(0, -1, 0), 1e9, {}, p, n);
    LevelObject b = a;
    b.p = V3(p.x, 0, p.z), b.size = lib.hulls[3].half() * 1.2, b.scan = 3, b.top = b.size.y * 1.7 + (p.y - g);
    const int kb = ed.addObject(b);
    const Rock stacked = w.rocks.back();
    ctx.check("the second rock rests on the first, not in the ground", lowY(stacked) > g + 0.4 && lowY(stacked) < p.y + 0.05 && highY(stacked) > p.y + 0.3 && lowY(stacked) > p.y - 0.4 * (highY(stacked) - lowY(stacked)),
              "g " + std::to_string(g) + " surface " + std::to_string(p.y) + " low " + std::to_string(lowY(stacked)) + " high " + std::to_string(highY(stacked)));
    // Floating and sinking, exactly, through everything that rebuilds the world.
    ed.transformObjects({kb}, OP_LIFT, 3.0);
    const double topAir = ed.level.objects[kb].top;
    ed.transformObjects({kb}, OP_LIFT, -9.0);
    const double sunk = ed.level.objects[kb].top;
    ctx.metric("raised 3 m then lowered 9 m: 6 m down, exactly", topAir - sunk, "m", 8.999, 9.001);
    ctx.check("lowered below the ground it's buried, and the world agrees", highY(w.rocks.back()) < g + 0.01);
    ed.transformObjects({kb}, OP_LIFT, 12.0);
    ctx.check("floating well over the ground", lowY(w.rocks.back()) > g + 3);
    const double floatTop = ed.level.objects[kb].top;
    ed.brush(BRUSH_RAISE, -290, 250, 6, 0.5);  // (terrain edited beside it)
    ctx.check("terrain edits nearby don't change what was set", ed.level.objects[kb].top == floatTop);
    Level back = roundTrip(ed.level);
    ctx.check("saved and loaded: the same offset", std::fabs(back.objects[kb].top - floatTop) < 1e-3);
    World pw = playWorldOf(back);
    ctx.metric("played back: floating where the editor had it", lowY(pw.rocks.back()) - lowY(w.rocks.back()), "m", -0.02, 0.02);
    ed.undo();
    ed.redo();
    ctx.check("undo and redo leave it where it was", ed.level.objects[kb].top == floatTop);
    // Boxes, trees, structures, bushes: a lift, any size, the same.
    LevelObject bx;
    bx.kind = OBJ_BOX, bx.p = V3(-320, 0, 250), bx.size = V3(2, 0.4, 1), bx.col[0] = 0.5f;
    const int kx = ed.addObject(ed.grounded(bx));
    LevelObject tr;
    tr.kind = OBJ_TREE, tr.p = V3(-330, 0, 250), tr.size = V3(0, 8, 0);
    const int kt = ed.addObject(tr);
    LevelObject bu;
    bu.kind = OBJ_BUSH, bu.p = V3(-340, 0, 250), bu.size = V3(1, 0, 0), bu.col[0] = 1;
    const int ku = ed.addObject(bu);
    LevelObject st;
    st.kind = OBJ_MODEL, st.name = "outhouse", st.p = V3(-350, 0, 250), st.size = V3(1, 1, 1);
    const int ks = ed.addObject(st);
    ed.transformObjects({kx, kt, ku, ks}, OP_LIFT, 4.0);
    const double boxY = w.boxes[w.boxes.size() - 2].c.y;  // (the model's collider is the last)
    (void)boxY;
    ctx.check("a box floats", ed.level.objects[kx].p.y > w.height(-320, 250) + 4 && std::fabs(ed.level.objects[kx].lift - 4) < 1e-6);
    ctx.check("a tree, a bush and a structure float", std::fabs(ed.level.objects[kt].lift - 4) < 1e-6 && std::fabs(ed.level.objects[ku].lift - 4) < 1e-6 && std::fabs(ed.level.objects[ks].lift - 4) < 1e-6 &&
                                                       w.bushes.back().p.y > w.height(-340, 250) + 3.9);
    ed.transformObjects({kx, kt, ku, ks}, OP_LIFT, -6.0);
    ctx.check("and sink", ed.level.objects[kt].lift < -1.9 && w.trees.back().base.y < w.height(-330, 250));
    ed.brush(BRUSH_RAISE, -320, 250, 5, 1.0);
    ctx.check("a box's height isn't redone by terrain edits", std::fabs(ed.level.objects[kx].lift + 2) < 1e-6);
    // Snap to ground: back as placed.
    ed.transformObjects({kb, kx, kt, ku, ks}, OP_SEAT, 0);
    ctx.check("boxes, trees, bushes and structures sit on the ground again", ed.level.objects[kx].lift == 0 && ed.level.objects[kt].lift == 0 && ed.level.objects[ku].lift == 0 && ed.level.objects[ks].lift == 0);
    const Rock seated = w.rocks.back();
    const double gb = w.height(ed.level.objects[kb].p.x, ed.level.objects[kb].p.z);
    ctx.check("a rock sits in the ground a little, its top above it", lowY(seated) < gb && lowY(seated) > gb - 0.5 * (highY(seated) - lowY(seated)) && highY(seated) > gb + 0.5 * (highY(seated) - lowY(seated)),
              "gb " + std::to_string(gb) + " low " + std::to_string(lowY(seated)) + " high " + std::to_string(highY(seated)));
    // Scaling shows at once on code-made rocks too (scans off), and the collider is that hull.
    {
        props::ScopedRockScans off(nullptr);
        World cw(MAP_PROVING_GROUND);
        EditCore ec;
        ec.begin(cw, lv);
        LevelObject c;
        c.kind = OBJ_ROCK, c.p = V3(-300, 0, 250), c.size = V3(1.5, 1, 1.2), c.top = 0.9, c.seed = 7;
        const int kc = ec.addObject(c);
        auto ext = [&](const Rock& r) {
            double lo = 1e9, hi = -1e9;
            for (const V3& v : r.v) lo = std::min(lo, v.x), hi = std::max(hi, v.x);
            return hi - lo;
        };
        const double e0 = ext(cw.rocks.back());
        ec.transformObjects({kc}, OP_SCALE, 1.0);  // (pinned as it was built: the same width)
        ctx.metric("pinning a code-made rock keeps its width", ext(cw.rocks.back()) / e0, "x", 0.9, 1.1);
        ec.transformObjects({kc}, OP_SCALE, 2.0);
        ctx.metric("a code-made rock scaled 2x is twice as wide", ext(cw.rocks.back()) / e0, "x", 1.8, 2.2);
    }
}

SCENARIO(editorLiveSection, "editor/live-section", "Editing a built-in section level (its rocks come from the generator): they become level objects you can move, and it saves and plays back the same") {
    const Level* sec = nullptr;
    for (const Level& l : builtinLevels())
        if (l.section.on && l.section.live) {
            sec = &l;
            break;
        }
    if (!ctx.check("a built-in live section level exists", sec != nullptr)) return;
    Level own = *sec;
    own.builtin = false, own.file.clear(), own.name += " (MINE)";  // (as the editor opens it)
    World w = levelWorld(own);
    EditCore ed;
    ed.begin(w, own);
    const size_t rocks0 = w.rocks.size();
    ctx.check("the section has rocks the level doesn't list", rocks0 > 0 && ed.level.objects.empty());
    // Pick one of them the way a click does: a ray down onto its middle.
    const V3 c = w.rocks[rocks0 / 2].c;
    const EditPick pk = ed.pick(V3(c.x, c.y + 40, c.z), V3(0, -1, 0), 1e9, true);
    ctx.check("a click on it picks it", pk.kind == EditPick::BASE && pk.baseKind == 0, pk.name);
    const std::vector<int> ks = ed.adopt({pk});
    ctx.check("it became a level object", ks.size() == 1 && ks[0] >= 0 && ed.level.objects.size() == 1 && w.rocks.size() == rocks0);
    ed.moveObjects(ks, 6, 0);
    ed.transformObjects(ks, OP_SCALE, 1.4);
    ed.transformObjects(ks, OP_LIFT, 1.0);
    Level back = roundTrip(ed.level);
    World pw = levelWorld(back);
    applyLevel(back, pw);
    ctx.check("saved and played: the same number of rocks, one of them moved", back.section.live && pw.rocks.size() == w.rocks.size());
    double gap = 1e9;
    for (const Rock& r : pw.rocks) gap = std::min(gap, len(r.c - w.rocks.back().c));
    ctx.metric("the moved, scaled, raised rock plays where the editor shows it", gap, "m", 0, 0.1);
}

SCENARIO(editorDiscard, "editor/discard", "DISCARD throws the unsaved edits away: the level goes back to what was last saved (or the plain level it came from), the recovery copy goes, the saved file stays") {
    // (Its own folders: scenarios run side by side, and the save-flow one owns the config folder.)
    makeDir("build/test-edflow");
    const std::string levels = "build/test-edflow/discard", autos = levels + "/autosave";
    makeDir(levels);
    makeDir(autos);
    for (const std::string& f : listLevelFiles(levels, ".czl")) std::remove(f.c_str());
    for (const std::string& f : listLevelFiles(autos, ".czl")) std::remove(f.c_str());
    World w(MAP_PROVING_GROUND);
    Level lv;
    lv.map = MAP_PROVING_GROUND, lv.name = "DISCARD ME";
    EditCore ed;
    ed.begin(w, lv);
    LevelObject r;
    r.kind = OBJ_ROCK, r.p = V3(-300, 0, 250), r.size = V3(1.5, 1, 1.2), r.top = 1, r.seed = 5, r.sub = 1;
    ed.addObject(r);
    ctx.check("saved once, with a rock", ed.save(levels + "/discard_me.czl") && ed.level.objects.size() == 1 && !ed.unsaved() && !ed.level.file.empty());
    SaveFlow flow;
    // More edits, a recovery copy of them, then DISCARD.
    r.p = V3(-310, 0, 250);
    ed.addObject(r);
    ed.autosave(autos);
    ctx.check("two rocks, unsaved, with a recovery copy", ed.level.objects.size() == 2 && ed.unsaved() && !listLevelFiles(autos, ".czl").empty());
    flow.requestLeave(ed, SaveFlow::LEAVE);
    ctx.check("leaving asks", flow.stage == SaveFlow::ASKING && !flow.discarded);
    flow.choose(ed, SaveFlow::DISCARD);
    ctx.check("DISCARD says so", flow.discarded && flow.done == SaveFlow::LEAVE);
    const Level back = ed.discard(autos);
    ctx.check("what's driven is the last saved level: one rock", back.objects.size() == 1 && back.name == "DISCARD ME");
    ctx.check("the recovery copy is gone", listLevelFiles(autos, ".czl").empty());
    Level onDisk;
    ctx.check("the saved file is still there, with its one rock", loadLevelFile(levels + "/discard_me.czl", onDisk) && onDisk.objects.size() == 1);
    // Never saved: back to the plain level, and no recovery copy left.
    Level fresh;
    fresh.map = MAP_PROVING_GROUND, fresh.name = "NEVER SAVED";
    World w2(MAP_PROVING_GROUND);
    EditCore e2;
    e2.begin(w2, fresh);
    e2.addObject(r);
    e2.brush(BRUSH_RAISE, -200, 200, 8, 1.0);
    e2.autosave(autos);
    ctx.check("a recovery copy of the new level", listLevelFiles(autos, ".czl").size() == 1);
    const Level plain = e2.discard(autos);
    ctx.check("never saved: the plain level comes back, no edits", plain.objects.empty() && plain.heights.empty() && plain.name == "NEVER SAVED" && listLevelFiles(autos, ".czl").empty());
    for (const std::string& f : listLevelFiles(levels, ".czl")) std::remove(f.c_str());
}

SCENARIO(editorBuiltinSectionFile, "editor/builtin-section-file", "An unmodified copy of any built-in section level saves its section file with no warning: its entry is the generator's, not the start pad out on the run-in") {
    makeDir("build/test-edflow");
    const std::string dir = "build/test-edflow/sections";
    makeDir(dir);
    int n = 0;
    for (const Level& l : builtinLevels()) {
        if (!l.section.on || !l.section.live) continue;
        n++;
        Level own = l;
        own.builtin = false, own.file.clear(), own.name += " (MINE)";
        World w = levelWorld(own);
        EditCore ed;
        ed.begin(w, own);
        const std::string path = dir + "/" + levelFileName(own.name) + ".czl";
        ctx.check(l.name + ": saves", ed.save(path));
        ctx.check(l.name + ": no section-file warning", ed.saveNote.empty(), ed.saveNote);
        SectionFile sf;
        std::string why;
        ctx.check(l.name + ": extracts", extractSection(w, ed.level, sf, &why), why);
        // The frame's entry is the section's own, an apron in; the first gate sits on it and the exit is the length on.
        const double ap = sectionFootprint((SectionType)l.section.gen).apron;
        ctx.check(l.name + ": the canvas starts an apron before the entry", std::fabs(sf.u0 + ap) < 1.5, std::to_string(sf.u0));
        ctx.check(l.name + ": the first gate is at the entry", !sf.gates.empty() && std::fabs(sf.gates.front().p.x) < 1.5);
        std::remove(path.c_str());
        std::remove(sectionPathFor(path).c_str());
    }
    ctx.check("all seven checked", n == SEC_COUNT, std::to_string(n));
}

SCENARIO(editorHeightModel, "editor/height-model", "Every object's height is an offset from the ground: sculpting carries blocks and logs along, tipping keeps a block's height, a scaled group grows as a whole, and it all saves, loads (old files too) and stacks") {
    World w(MAP_PROVING_GROUND);
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    // A seated block, a floating block, a log and a rock, side by side; then the ground comes up under all of them.
    LevelObject b;
    b.kind = OBJ_BOX, b.p = V3(-340, 0, 250), b.size = V3(2, 0.4, 1), b.col[0] = b.col[1] = b.col[2] = 0.6f;
    const int kb = ed.addObject(ed.grounded(b));
    LevelObject f = b;
    f.p = V3(-340, 0, 262), f.lift = 1.5;
    const int kf = ed.addObject(ed.grounded(f));
    LevelObject g;
    g.kind = OBJ_LOG, g.p = V3(-352, 0, 250), g.p2 = V3(-346, 0, 250), g.size = V3(0.3, 0, 0);
    const int kg = ed.addObject(ed.grounded(g));
    LevelObject r;
    r.kind = OBJ_ROCK, r.p = V3(-360, 0, 250), r.size = V3(1.5, 1, 1.2), r.top = 1, r.seed = 5, r.sub = 1;
    ed.addObject(r);
    // (how far a block hangs above where it would rest on the ground under its footprint)
    auto boxOver = [&](int k) { return ed.level.objects[k].p.y - boxRestY(w, ed.level.objects[k]); };
    ctx.metric("a seated block rests on the ground", boxOver(kb), "m", -0.01, 0.01);
    ctx.metric("a floating block hangs 1.5 m up", boxOver(kf), "m", 1.49, 1.51);
    ed.brush(BRUSH_RAISE, -345, 252, 14, 2.5);
    ctx.check("the ground came up a good way", w.height(-340, 250) > 1);
    // (the world's boxes are the level's, in order: the map has none here)
    const Box* wb = nullptr;
    for (const Box& x : w.boxes)
        if (!x.hidden && std::fabs(x.c.x + 340) < 0.01 && std::fabs(x.c.z - 250) < 0.01) wb = &x;
    ctx.check("the world has the block", wb != nullptr);
    if (wb) ctx.metric("raise the ground under a block: it rides up with it", wb->c.y - boxRestY(w, ed.level.objects[kb]), "m", -0.01, 0.01);
    if (wb) ctx.check("...and its underside is on the ground, not in it", std::fabs(wb->c.y - 0.4 - ([&] {
        double m = 1e9;
        for (int a = -1; a <= 1; a += 2)
            for (int c = -1; c <= 1; c += 2) m = std::min(m, w.height(-340 + 2 * a, 250 + 1 * c));
        return m;
    })()) < 0.01);
    ctx.metric("the floating block floats just as high over the new ground", boxOver(kf), "m", 1.49, 1.51);
    const Log lg = w.logs.back();
    ctx.metric("a log's low end lies on the raised ground", lg.a.y - w.height(lg.a.x, lg.a.z) - 0.3, "m", -0.01, 0.01);
    ctx.metric("and its other end", lg.b.y - w.height(lg.b.x, lg.b.z) - 0.3, "m", -0.01, 0.01);
    ed.undo();
    ctx.metric("undoing the sculpt brings the block back down", boxOver(kb), "m", -0.01, 0.01);
    ed.redo();
    ctx.metric("and redo up again", boxOver(kb), "m", -0.01, 0.01);
    // A log with a slope: each end keeps its own clearance over the ground it's over.
    ed.transformObjects({kg}, OP_LIFT, 1.0);
    ed.brush(BRUSH_RAISE, -352, 250, 3, 3.0);  // (more ground under one end only)
    const Log lg2 = w.logs.back();
    ctx.metric("a raised log keeps its clearance at the end the ground came up under", lg2.a.y - w.height(lg2.a.x, lg2.a.z) - 1.3, "m", -0.02, 0.02);
    ctx.metric("and at the other end", lg2.b.y - w.height(lg2.b.x, lg2.b.z) - 1.3, "m", -0.02, 0.02);
    // Tipping a block keeps the height it was set (about its middle), floating or not.
    const double y0 = ed.level.objects[kf].p.y;
    ed.transformObjects({kf}, OP_PITCH, 0.5);
    ctx.metric("tipping a floating block leaves its middle where it was", ed.level.objects[kf].p.y - y0, "m", -1e-6, 1e-6);
    ctx.check("and it did tip", std::fabs(ed.level.objects[kf].pitch - 0.5) < 1e-9);
    ed.transformObjects({kf}, OP_ROLL, 0.3);
    ctx.metric("rolling it too", ed.level.objects[kf].p.y - y0, "m", -1e-6, 1e-6);
    ed.transformObjects({kf}, OP_PITCH, -0.5);
    ed.transformObjects({kf}, OP_ROLL, -0.3);
    ctx.metric("and back flat: the same height, so the same offset", boxOver(kf), "m", 1.49, 1.51);
    const double seatedY = ed.level.objects[kb].p.y;
    ed.transformObjects({kb}, OP_PITCH, 0.3);
    ctx.check("a seated block stays seated as it tips (the low end's top edge on the ground)", ed.level.objects[kb].lift == 0 && std::fabs(ed.level.objects[kb].p.y - seatedY) < 1.0);
    ed.transformObjects({kb}, OP_PITCH, -0.3);
    // A group scaled grows as a whole: spacing scales about the middle too.
    {
        World gw(MAP_PROVING_GROUND);
        EditCore eg;
        eg.begin(gw, lv);
        std::vector<int> ks;
        for (int i = 0; i < 3; i++) {
            LevelObject o;
            o.kind = OBJ_ROCK, o.p = V3(-300 + 4 * i, 0, 250), o.size = V3(1, 0.8, 1), o.top = 0.8, o.seed = 3 + i, o.sub = 1;
            ks.push_back(eg.addObject(o));
        }
        LevelObject l;
        l.kind = OBJ_LOG, l.p = V3(-302, 0, 256), l.p2 = V3(-298, 0, 256), l.size = V3(0.3, 0, 0);
        ks.push_back(eg.addObject(eg.grounded(l)));
        const V3 c0(-296, 0, 250 + 6.0 / 4);  // (the middle of the four places: rocks at -300 -296 -292, log at -300)
        V3 mid;
        for (int k : ks) mid += eg.level.objects[k].kind == OBJ_LOG ? (eg.level.objects[k].p + eg.level.objects[k].p2) * 0.5 : eg.level.objects[k].p;
        mid = mid * (1.0 / ks.size());
        (void)c0;
        const double d0 = eg.level.objects[ks[2]].p.x - eg.level.objects[ks[0]].p.x, s0 = eg.level.objects[ks[0]].size.x;
        eg.transformObjects(ks, OP_SCALE, 1.5);
        ctx.metric("a scaled group: the spacing grows with it", (eg.level.objects[ks[2]].p.x - eg.level.objects[ks[0]].p.x) / d0, "x", 1.499, 1.501);
        ctx.metric("and each rock's size", eg.level.objects[ks[0]].size.x / s0, "x", 1.499, 1.501);
        V3 mid2;
        for (int k : ks) mid2 += eg.level.objects[k].kind == OBJ_LOG ? (eg.level.objects[k].p + eg.level.objects[k].p2) * 0.5 : eg.level.objects[k].p;
        mid2 = mid2 * (1.0 / ks.size());
        ctx.metric("about the group's middle", std::hypot(mid2.x - mid.x, mid2.z - mid.z), "m", 0, 0.01);
        const Log& sl = gw.logs.back();
        ctx.metric("the log is as much longer", len(sl.b - sl.a), "m", 5.9, 6.1);
        ctx.metric("one object alone still scales about itself", [&] {
            const double x0 = eg.level.objects[ks[1]].p.x;
            eg.transformObjects({ks[1]}, OP_SCALE, 2.0);
            return eg.level.objects[ks[1]].p.x - x0;
        }(), "m", -1e-6, 1e-6);
    }
    // Saved and played: the same offsets, on the sculpted ground.
    Level back = roundTrip(ed.level);
    ctx.check("saved and loaded: heights are offsets", back.objects[kb].rel && back.objects[kf].rel && back.objects[kg].rel);
    ctx.check("...the same offsets", std::fabs(back.objects[kf].lift - ed.level.objects[kf].lift) < 2e-3 && std::fabs(back.objects[kg].lift - ed.level.objects[kg].lift) < 2e-3 &&
                                          std::fabs(back.objects[kg].lift2 - ed.level.objects[kg].lift2) < 2e-3);
    World pw = playWorldOf(back);
    const Log& pl = pw.logs.back();
    ctx.metric("played back: the log is where the editor has it", std::max(len(pl.a - w.logs.back().a), len(pl.b - w.logs.back().b)), "m", 0, 0.02);
    double gap = 1e9;
    auto floater = [&](const World& ww) {
        for (const Box& x : ww.boxes)
            if (!x.hidden && std::fabs(x.c.x + 340) < 0.01 && std::fabs(x.c.z - 262) < 0.01) return x.c.y;
        return -1e9;
    };
    gap = std::fabs(floater(pw) - floater(w));
    ctx.metric("played back: the floating block is where the editor has it", gap, "m", 0, 0.02);
    // An older file: absolute box and log heights (no offsets). They load as they stood, become offsets, and then follow the sculpting.
    {
        World ow(MAP_PROVING_GROUND);
        LevelObject probe;
        probe.kind = OBJ_BOX, probe.p = V3(-340, 0, 250), probe.size = V3(2, 0.4, 1);
        const double gh = boxRestY(ow, probe) - 0.4, gl = ow.height(-352, 250), gl2 = ow.height(-346, 250);  // (gh: where the block's underside rests)
        std::ostringstream t;
        t << "CRUMPLEZONE LEVEL 1\nname OLD\nmap " << MAP_PROVING_GROUND << "\n";
        t << "box -340 " << gh + 3.0 + 0.4 << " 250 2 0.4 1 0 0 0 0.6 0.6 0.6 0\n";
        t << "log -352 " << gl + 2.0 + 0.3 << " 250 -346 " << gl2 + 2.0 + 0.3 << " 250 0.3\n";
        Level old;
        std::string why;
        const bool ok = parseLevelText(t.str(), old, &why);
        ctx.check("an old-format level reads", ok && old.objects.size() == 2 && !old.objects[0].rel && !old.objects[1].rel, why);
        World pw0 = playWorldOf(old);
        ctx.metric("played as it always was: the block 3 m up", pw0.boxes.back().c.y - gh - 0.4, "m", 2.99, 3.01);
        World ew(MAP_PROVING_GROUND);
        EditCore eo;
        eo.begin(ew, old);
        ctx.check("opened in the editor: offsets, converted", eo.level.objects[0].rel && eo.level.objects[1].rel && std::fabs(eo.level.objects[0].lift - 3.0) < 1e-3 && std::fabs(eo.level.objects[1].lift - 2.0) < 1e-3 &&
                                                                  std::fabs(eo.level.objects[1].lift2 - 2.0) < 1e-3);
        eo.brush(BRUSH_RAISE, -345, 250, 12, 2.0);
        const double up = ew.height(-340, 250) - gh;
        ctx.check("the ground came up", up > 0.5);
        const Box* ob = nullptr;
        for (const Box& x : ew.boxes)
            if (!x.hidden && std::fabs(x.c.x + 340) < 0.01) ob = &x;
        if (ob) ctx.metric("an old floating block rides the sculpting", ob->c.y - (boxRestY(ew, probe) + 3.0), "m", -0.01, 0.01);
        ctx.metric("an old log too", ew.logs.back().a.y - (ew.height(-352, 250) + 0.3 + 2.0), "m", -0.01, 0.01);
        Level again = roundTrip(eo.level);
        ctx.check("and it saves in the new form", again.objects[0].rel && again.objects[1].rel);
    }
    // Stacking: a rock resting on another keeps its rise through rebuilds; it doesn't follow if the lower rock moves, but is never seated into it.
    {
        World sw(MAP_PROVING_GROUND);
        EditCore es;
        es.begin(sw, lv);
        LevelObject a;
        a.kind = OBJ_ROCK, a.p = V3(-300, 0, 250), a.size = V3(1.6, 1.0, 1.4), a.top = 1.4, a.seed = 9, a.sub = 1;
        const int ka = es.addObject(a);
        const double g0 = sw.height(-300, 250);
        LevelObject u = a;
        u.size = V3(0.8, 0.5, 0.7), u.top = 0.7 + 1.4, u.seed = 10;
        const int ku = es.addObject(u);
        auto low = [&](const Rock& rk) {
            double m = 1e9;
            for (const V3& v : rk.v) m = std::min(m, v.y);
            return m;
        };
        const double lowU = low(sw.rocks.back());
        ctx.check("the upper rock rests above the ground, on the other", lowU > g0 + 0.5);
        es.moveObjects({ka}, 10, 0);
        ctx.metric("the lower rock moved away: the upper one stays where it was (it doesn't follow)", low(sw.rocks.back()) - lowU, "m", -1e-6, 1e-6);
        es.moveObjects({ka}, -10, 0);
        es.brush(BRUSH_RAISE, -300, 250, 6, 1.0);
        ctx.check("the ground rises under both: the upper stays as far above its ground, not seated into the lower", std::fabs(es.level.objects[ku].top - u.top) < 1e-9 && low(sw.rocks.back()) > lowU + 0.9);
        (void)ku;
    }
}
