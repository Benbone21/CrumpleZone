// The editor's brushes: one mask (shape, edge, pattern, density) shared by terrain, paint and the
// water drain, from a single grid cell to 100 m; and draining water, which undoes.
#include <chrono>
#include <cmath>
#include <cstdio>

#include "../src/editcore.h"
#include "harness.h"

namespace {

// How many whole-meter cells the brush reaches at half strength or more, centered on the origin.
int cellsOf(const Brush& b, double cx = 0, double cz = 0) {
    int n = 0;
    const int R = (int)std::ceil(b.radius) + 3;
    for (int i = -R; i <= R; i++)
        for (int j = -R; j <= R; j++) n += brushMask(b, cx, cz, i, j) >= 0.5;
    return n;
}

int reaches(const Brush& b) {
    int n = 0;
    const int R = (int)std::ceil(b.radius) + 3;
    for (int i = -R; i <= R; i++)
        for (int j = -R; j <= R; j++) n += brushMask(b, 0, 0, i, j) > 0;
    return n;
}

double share(const Brush& b) {  // of the brush's own area (cells within the rim)
    Brush hard = b;
    hard.style = STYLE_SOLID, hard.fall = FALL_HARD;
    return (double)cellsOf(b) / std::max(1, reaches(hard));
}

World ground() { return World(MAP_PROVING_GROUND); }

}  // namespace

SCENARIO(editBrushSizes, "editor/brush-sizes", "A brush runs from one grid cell to 100 m, and square, round and diamond cover what their radius says") {
    Brush b;
    b.fall = FALL_HARD;
    b.radius = 0.5;
    ctx.check("0.5 m is one cell", cellsOf(b) == 1, std::to_string(cellsOf(b)));
    int near = 0;
    for (int i = 5; i <= 15; i++)
        for (int j = -8; j <= 2; j++) near += brushMask(b, 10.4, -3.3, i, j) > 0;
    ctx.check("one cell wherever the cursor is", near == 1 && brushMask(b, 10.4, -3.3, 10, -3) > 0);
    b.radius = 1;
    ctx.check("1 m is a plus of five", reaches(b) == 5, std::to_string(reaches(b)));
    b.shape = SHAPE_SQUARE, b.radius = 4;
    ctx.metric("square 4 m: 9 x 9 cells", reaches(b), "", 81, 81);
    b.shape = SHAPE_DIAMOND;
    ctx.metric("diamond 4 m: 41 cells", reaches(b), "", 41, 41);
    b.shape = SHAPE_ROUND;
    for (double r : {2.0, 8.0, 30.0, 100.0}) {
        b.radius = r;
        ctx.metric("round " + std::to_string((int)r) + " m, area over pi r^2", reaches(b) / (M_PI * r * r), "", 0.93, 1.1);
    }
    // The size ladder: distinct steps, 1 cell to 100 m, stable at both ends.
    double r = kBrushMin;
    int steps = 0;
    for (; steps < 40 && brushStep(r, 1) > r; steps++) r = brushStep(r, 1);
    ctx.check("ladder tops out at 100 m", r == kBrushMax && steps >= 12 && steps <= 24, std::to_string(steps) + " steps");
    ctx.check("and bottoms out at one cell", brushStep(0.5, -1) == 0.5 && brushStep(100, 1) == 100 && brushStep(brushStep(8, 1), -1) == 8);
}

SCENARIO(editBrushFalloff, "editor/brush-falloff", "Hard edges are full to the rim; smooth and linear fall away; strength never goes past 1") {
    Brush b;
    b.radius = 10;
    b.fall = FALL_HARD;
    ctx.check("hard: full at the rim", brushMask(b, 0, 0, 10, 0) == 1 && brushMask(b, 0, 0, 0, 0) == 1 && brushMask(b, 0, 0, 10.5, 0) == 0);
    b.fall = FALL_SMOOTH;
    const double m0 = brushMask(b, 0, 0, 0, 0), m5 = brushMask(b, 0, 0, 5, 0), m9 = brushMask(b, 0, 0, 9, 0);
    ctx.check("smooth: falls away", m0 > m5 && m5 > m9 && m9 > 0 && m0 <= 1, std::to_string(m0) + " " + std::to_string(m5) + " " + std::to_string(m9));
    b.fall = FALL_LINEAR;
    ctx.metric("linear: about half at half way", brushMask(b, 0, 0, 5.25, 0), "", 0.45, 0.55);
    b.shape = SHAPE_SQUARE;
    ctx.check("square: the corner is as far as the side", brushMask(b, 0, 0, 10, 10) == brushMask(b, 0, 0, 10, 0));
}

SCENARIO(editBrushStyles, "editor/brush-styles", "Splatter, speckle, noise and stripes cover about the density asked for, stay inside the brush, and are the same every time") {
    Brush b;
    b.radius = 30;
    b.fall = FALL_HARD;
    for (int st : {STYLE_SPLATTER, STYLE_SPECKLE, STYLE_NOISE, STYLE_STRIPES}) {
        b.style = (BrushStyle)st;
        b.density = 0.2;
        const double lo = share(b);
        b.density = 0.5;
        const double mid = share(b);
        b.density = 0.9;
        const double hi = share(b);
        const std::string nm = brushStyleName(st);
        ctx.check(nm + ": more density, more coverage", lo < mid && mid < hi, std::to_string(lo) + " " + std::to_string(mid) + " " + std::to_string(hi));
        ctx.check(nm + ": a pattern, not all or nothing", lo > 0.02 && hi < 0.98 && hi > 0.4, std::to_string(lo) + " .. " + std::to_string(hi));
        ctx.check(nm + ": nothing outside the rim", brushMask(b, 0, 0, 31, 0) == 0 && brushMask(b, 0, 0, 25, 25) == 0);
        // Same brush, same answer; another seed, another pattern.
        Brush c = b;
        bool same = true, differs = false;
        c.seed = b.seed + 5;
        for (int i = -20; i <= 20; i++)
            for (int j = -20; j <= 20; j++) {
                same &= brushMask(b, 0, 0, i, j) == brushMask(b, 0, 0, i, j);
                differs |= st != STYLE_STRIPES && brushMask(b, 0, 0, i, j) != brushMask(c, 0, 0, i, j);
            }
        ctx.check(nm + ": deterministic", same);
        ctx.check(nm + ": a new seed is a new scatter", st == STYLE_STRIPES || differs);
    }
    b.style = STYLE_SPECKLE, b.density = 0.5;
    ctx.metric("speckle at 50%", share(b), "", 0.4, 0.6);
    b.style = STYLE_STRIPES;
    ctx.metric("stripes at 50%", share(b), "", 0.35, 0.65);
    b.style = STYLE_NOISE;
    ctx.metric("noise at 50%", share(b), "", 0.3, 0.7);
    b.style = STYLE_SPLATTER;
    ctx.metric("splatter at 50%", share(b), "", 0.15, 0.6);
    // Splatter is dabs, not dust: its covered cells touch each other.
    int lone = 0, covered = 0;
    for (int i = -25; i <= 25; i++)
        for (int j = -25; j <= 25; j++)
            if (brushMask(b, 0, 0, i, j) >= 0.5) {
                covered++;
                lone += brushMask(b, 0, 0, i + 1, j) < 0.5 && brushMask(b, 0, 0, i - 1, j) < 0.5 && brushMask(b, 0, 0, i, j + 1) < 0.5 && brushMask(b, 0, 0, i, j - 1) < 0.5;
            }
    ctx.metric("splatter: cells with no neighbour, of those covered", (double)lone / std::max(1, covered), "", 0, 0.12);
}

SCENARIO(editBrushTerrain, "editor/brush-terrain", "Terrain brushes take the shape and pattern: splatter raises scattered mounds, roughen bumps, terrace steps; a 100 m stroke is quick, and all undo") {
    World w = ground();
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    Brush b;
    b.radius = 20;
    b.fall = FALL_HARD;
    b.style = STYLE_SPLATTER;
    b.density = 0.5;
    const double cx = -200, cz = 200;
    int up = 0, flat = 0, outside = 0;
    std::vector<double> before;
    for (int i = -22; i <= 22; i++)
        for (int j = -22; j <= 22; j++) before.push_back(w.height(cx + i, cz + j));
    ed.brush(BRUSH_RAISE, b, cx, cz, 1.0);
    int k = 0;
    for (int i = -22; i <= 22; i++)
        for (int j = -22; j <= 22; j++, k++) {
            const double d = w.height(cx + i, cz + j) - before[k];
            if (std::hypot(i, j) <= 20) (d > 0.5 ? up : flat)++;
            else outside += std::fabs(d) > 0.001;
        }
    ctx.check("splatter raise: some of the disc raised, some left", up > 80 && flat > 80, std::to_string(up) + " up, " + std::to_string(flat) + " left");
    ctx.check("nothing past the rim", outside == 0);
    ed.undo();
    double worst = 0;
    k = 0;
    for (int i = -22; i <= 22; i++)
        for (int j = -22; j <= 22; j++, k++) worst = std::max(worst, std::fabs(w.height(cx + i, cz + j) - before[k]));
    ctx.metric("undone", worst, "m", 0, 0.001);
    // Single cell.
    Brush one;
    one.radius = 0.5;
    ed.brush(BRUSH_RAISE, one, cx + 0.3, cz - 0.4, 1.0);
    ctx.check("one cell raised, one cell", ed.level.heights.size() == 1);
    ed.undo();
    // Square keeps its corners; round does not.
    Brush sq;
    sq.radius = 10, sq.shape = SHAPE_SQUARE, sq.fall = FALL_HARD;
    const double c0 = w.height(cx + 9, cz + 9);
    ed.brush(BRUSH_RAISE, sq, cx, cz, 1.0);
    ctx.check("square raised its corner", w.height(cx + 9, cz + 9) > c0 + 0.9);
    ed.undo();
    // Roughen: bumps both ways. Terrace: cells land on steps.
    Brush rg;
    rg.radius = 16, rg.fall = FALL_HARD;
    ed.brush(BRUSH_ROUGHEN, rg, cx, cz, 1.0);
    int hi = 0, lo = 0;
    for (int i = -12; i <= 12; i++)
        for (int j = -12; j <= 12; j++) {
            const double d = w.height(cx + i, cz + j) - before[(i + 22) * 45 + (j + 22)];
            hi += d > 0.15, lo += d < -0.15;
        }
    ctx.check("roughen: bumps up and down", hi > 40 && lo > 40, std::to_string(hi) + " up, " + std::to_string(lo) + " down");
    ed.brush(BRUSH_TERRACE, rg, cx, cz, 1.0, 0.5);
    double off = 0;
    for (int i = -10; i <= 10; i++)
        for (int j = -10; j <= 10; j++) {
            const double h = w.cellHeight((int)(cx + i + w.halfSize()) * w.gridSize() + (int)(cz + j + w.halfSize()));
            off = std::max(off, std::fabs(h / 0.5 - std::round(h / 0.5)) * 0.5);
        }
    ctx.metric("terrace: inside, heights sit on 0.5 m steps", off, "m", 0, 0.01);
    // The biggest brush, a smooth and a raise, timed.
    Brush big;
    big.radius = kBrushMax;
    for (BrushOp op : {BRUSH_RAISE, BRUSH_SMOOTH, BRUSH_ROUGHEN}) {
        const auto t0 = std::chrono::steady_clock::now();
        ed.brush(op, big, 0, 0, 0.1);
        ed.brush(op, big, 0, 0, 0.1);  // (a second pass over cells already in the level)
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 2;
        ctx.metric(std::string("100 m ") + (op == BRUSH_RAISE ? "raise" : op == BRUSH_SMOOTH ? "smooth" : "roughen") + ", ms a pass", ms, "ms", 0, 60);
    }
}

SCENARIO(editBrushPaint, "editor/brush-paint", "Paint takes the pattern and the opacity: noise leaves the grass between blotches, a light stroke covers part, and it undoes") {
    World w = ground();
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    int mud = 0;
    for (int k = 0; k < (int)paintMaterials().size(); k++)
        if (paintMaterials()[k].surface == SURF_MUD) mud = k;
    auto muddy = [&](double cx, double cz, int R) {
        int n = 0;
        for (int i = -R; i <= R; i++)
            for (int j = -R; j <= R; j++)
                if (std::hypot(i, j) <= R) n += w.surfaceAt(cx + i, cz + j) == SURF_MUD;
        return n;
    };
    const double area = M_PI * 15 * 15;
    Brush b;
    b.radius = 15, b.fall = FALL_HARD;
    const int before = muddy(-210, 190, 15);
    ed.paint(b, -210, 190, mud);
    ctx.metric("solid hard: covers the disc", (muddy(-210, 190, 15) - before) / area, "", 0.97, 1.05);
    ed.undo();
    ctx.check("undone", muddy(-210, 190, 15) == before);
    b.style = STYLE_NOISE, b.density = 0.5;
    ed.paint(b, -210, 190, mud);
    const double noise = (muddy(-210, 190, 15) - before) / area;
    ctx.metric("noise at 50%: part of the disc", noise, "", 0.2, 0.8);
    ed.undo();
    b.style = STYLE_SPLATTER, b.density = 0.4;
    ed.paint(b, -210, 190, mud);
    ctx.metric("splatter at 40%: part of the disc", (muddy(-210, 190, 15) - before) / area, "", 0.1, 0.6);
    ed.undo();
    b.style = STYLE_SOLID, b.strength = 0.4;
    ed.paint(b, -210, 190, mud);
    ctx.metric("40% opacity: about 40% of the cells", (muddy(-210, 190, 15) - before) / area, "", 0.3, 0.5);
    ed.undo();
    // Another stroke over it (another seed) fills more in, never less.
    b.strength = 0.4;
    ed.paint(b, -210, 190, mud);
    const int once = muddy(-210, 190, 15);
    b.seed = 9;
    ed.paint(b, -210, 190, mud);
    ctx.check("a second pass fills in more", muddy(-210, 190, 15) > once);
    // The level it saves holds the same cells.
    Level back;
    parseLevelText(saveLevelText(ed.level), back);
    World p(MAP_PROVING_GROUND);
    applyLevel(back, p);
    int diff = 0;
    for (int i = -15; i <= 15; i++)
        for (int j = -15; j <= 15; j++) diff += p.surfaceAt(-210 + i, 190 + j) != w.surfaceAt(-210 + i, 190 + j);
    ctx.check("saved and loaded: the same paint", diff == 0, std::to_string(diff) + " cells differ");
}

SCENARIO(editBrushWater, "editor/water-drain", "Water comes out again: the brush clears the cells it reaches, a pool drains whole with no half-filled edge, and undo puts it back") {
    World w = ground();
    Level lv;
    lv.map = MAP_PROVING_GROUND;
    EditCore ed;
    ed.begin(w, lv);
    ed.brush(BRUSH_LOWER, -60, 80, 12, 2.0);
    const int cells = ed.flood(-60, 80, 1.0, 40);
    ctx.check("flooded a pool", cells > 100, std::to_string(cells));
    auto wet = [&]() {
        int n = 0;
        for (int i = -40; i <= 40; i++)
            for (int j = -40; j <= 40; j++) n += w.waterLevel(-60 + i, 80 + j) > -1e8;
        return n;
    };
    const int all = wet();
    ctx.check("the pool is wet", all >= cells - 2, std::to_string(all));
    Brush b;
    b.radius = 6, b.fall = FALL_HARD, b.style = STYLE_SPLATTER, b.density = 0.5;
    const int part = ed.dryBrush(b, -60, 80);
    ctx.check("a splatter drain takes only part", part > 5 && part < all - 20 && wet() == all - part, std::to_string(part) + " of " + std::to_string(all));
    b.style = STYLE_SOLID, b.radius = 3;
    const int dried = ed.dryBrush(b, -60, 80);
    ctx.check("the brush dried its cells and no more", dried > 10 && dried <= 29 && wet() == all - part - dried, std::to_string(dried) + " dried");
    ctx.check("the middle is dry, the rim still wet", w.waterLevel(-60, 80) < -1e8 && w.waterLevel(-60, 85) > -1e8);
    ctx.check("undo puts the drained cells back", ed.undo() && wet() == all - part);
    ctx.check("and the splatter too", ed.undo() && wet() == all && w.waterLevel(-60, 80) > w.height(-60, 80));
    // The whole pool, from one click at its edge, the shallow cells with it.
    const int n = ed.drain(-60, 88);
    ctx.check("drain took the whole pool", n == all && wet() == 0, std::to_string(n) + " of " + std::to_string(all) + ", " + std::to_string(wet()) + " left");
    ctx.check("nothing to drain on dry ground", ed.drain(-150, 150) == 0);
    Level back;
    parseLevelText(saveLevelText(ed.level), back);
    World p(MAP_PROVING_GROUND);
    applyLevel(back, p);
    int pw = 0;
    for (int i = -40; i <= 40; i++)
        for (int j = -40; j <= 40; j++) pw += p.waterLevel(-60 + i, 80 + j) > -1e8;
    ctx.check("saved and loaded: still dry", pw == 0, std::to_string(pw));
    ctx.check("undo brings the pool back", ed.undo() && wet() == all);
    ctx.check("redo drains it", ed.redo() && wet() == 0);
}
