// Frame hitches on the big map (branch wip/hitches): the streaming does a bounded piece of work at a time and
// keeps what the shaders read right; the physics pool stops waiting on a worker the machine isn't running.
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "../src/autopilot.h"
#include "../src/bgwork.h"
#include "../src/physics_workers.h"
#include "../src/vehicles.h"
#include "../src/streamwin.h"
#include "harness.h"

SCENARIO(streamWindowWalk, "hitches/texture-window", "The terrain texture's window follows a drive across the big map a 32-cell strip at a time, every texel in it right throughout") {
    const int n = 8193, w = 2048, slack = 160, stepCells = 32;  // (the big map's grid, render.cpp's window)
    std::vector<int> held((size_t)w * w);  // each texel: which cell it holds (i * n + j)
    // A route that wanders: out along a diagonal, a zigzag, back the other way, past the map's corner.
    std::vector<std::pair<double, double>> legs = {{600, 600}, {7600, 7600}, {7000, 1200}, {1500, 3000}, {4000, 8100}, {8150, 40}, {300, 7900}};
    double x = legs[0].first, z = legs[0].second;
    int winI = streamwin::start((int)x, n, w), winJ = streamwin::start((int)z, n, w);
    for (int a = 0; a < w; a++)
        for (int b = 0; b < w; b++) held[(size_t)((winI + a) % w) * w + (winJ + b) % w] = (winI + a) * n + (winJ + b);
    bool moving = false, right = true, jumped = false;
    int biggest = 0, steps = 0, pendingFrames = 0;
    double closest = 1e9;  // nearest the camera came to the window's edge (cells)
    streamwin::Step pending;
    bool havePending = false;
    auto texelsRight = [&](int i0, int i1, int j0, int j1, int stride) {
        for (int i = i0; i <= i1; i += stride)
            for (int j = j0; j <= j1; j += stride)
                if (held[(size_t)(i % w) * w + j % w] != i * n + j) return false;
        return true;
    };
    long frame = 0;
    for (size_t l = 1; l < legs.size(); l++) {
        const double tx = legs[l].first, tz = legs[l].second;
        while (std::hypot(tx - x, tz - z) > 1) {
            const double d = std::hypot(tx - x, tz - z), v = std::min(d, 0.9);  // ~54 m/s at 60 fps
            x += (tx - x) / d * v, z += (tz - z) / d * v;
            frame++;
            // The background's strip lands a few frames after it was asked for.
            if (havePending && --pendingFrames <= 0) {
                for (const streamwin::Piece& p : pending.pieces)
                    for (int a = 0; a < p.cw; a++)
                        for (int b = 0; b < p.ch; b++) held[(size_t)((p.a0 + a) % w) * w + (p.b0 + b) % w] = (p.a0 + a) * n + (p.b0 + b);
                winI = pending.toI, winJ = pending.toJ, havePending = false;
            }
            const int wi = streamwin::start((int)std::lround(x), n, w), wj = streamwin::start((int)std::lround(z), n, w);
            if (std::abs(wi - winI) >= w / 2 || std::abs(wj - winJ) >= w / 2) jumped = true;
            if (!havePending) {
                pending = streamwin::next(winI, winJ, wi, wj, w, n, slack, stepCells, moving);
                if (!pending.pieces.empty()) {
                    int cells = 0;
                    for (const streamwin::Piece& p : pending.pieces) cells += p.cw * p.ch;
                    biggest = std::max(biggest, cells), steps++;
                    havePending = true, pendingFrames = 4;
                }
            }
            const double cx = x, cz = z;
            // (How near the camera is to an edge of the window that isn't the map's own edge.)
            double edge = 1e9;
            if (winI > 0) edge = std::min(edge, cx - winI);
            if (winI < n - w) edge = std::min(edge, winI + w - 1 - cx);
            if (winJ > 0) edge = std::min(edge, cz - winJ);
            if (winJ < n - w) edge = std::min(edge, winJ + w - 1 - cz);
            closest = std::min(closest, edge);
            // The window's edges every frame, and all of it now and then.
            if (right) right = texelsRight(winI, winI + w - 1, winJ, winJ, 1) && texelsRight(winI, winI + w - 1, winJ + w - 1, winJ + w - 1, 1) &&
                               texelsRight(winI, winI, winJ, winJ + w - 1, 1) && texelsRight(winI + w - 1, winI + w - 1, winJ, winJ + w - 1, 1);
            if (right && frame % 2000 == 0) right = texelsRight(winI, winI + w - 1, winJ, winJ + w - 1, 1);
        }
    }
    ctx.check("every texel inside the window holds its own cell, every frame of the drive", right);
    ctx.check("it never had to load the whole window at once (no jump)", !jumped);
    ctx.check("each step brings in at most a 32 x 2048 strip (the background's piece of work, the frame's upload)", biggest > 0 && biggest <= stepCells * w);
    ctx.metric("the camera's nearest approach to the window's edge, away from the map's", closest, "cells", 700, 1100);
    ctx.metric("steps over the drive", (double)steps, "", 100, 20000);
}

SCENARIO(poolContentionGuard, "hitches/pool-guard", "A physics pool whose workers stall (a busy machine) falls back to running its batches on the caller, and every job still runs once") {
    PhysicsWorkers pool(3);
    const std::thread::id caller = std::this_thread::get_id();
    std::vector<std::atomic<int>> hits(8);
    bool once = true;
    int batches = 0;
    for (; batches < 200 && !pool.serialNow(); batches++) {
        for (auto& h : hits) h = 0;
        pool.run(8, [&](int i) {
            if (std::this_thread::get_id() != caller) std::this_thread::sleep_for(std::chrono::milliseconds(2));  // (a worker that wasn't scheduled)
            hits[i]++;
        });
        for (auto& h : hits) once &= h == 1;
    }
    ctx.check("stalled workers: it falls back to the caller within a few batches", pool.serialNow() && pool.fallbacks() == 1 && batches < 20);
    for (auto& h : hits) h = 0;
    pool.run(8, [&](int i) { hits[i] += std::this_thread::get_id() == caller ? 1 : 100; });
    for (auto& h : hits) once &= h == 1;
    ctx.check("while it's fallen back the batch runs on the caller alone", once);
}

namespace {
bool samePath(const std::vector<V3>& a, const std::vector<V3>& b) {
    if (a.size() != b.size()) return false;
    for (size_t k = 0; k < a.size(); k++)
        if (a[k].x != b[k].x || a[k].y != b[k].y || a[k].z != b[k].z) return false;
    return true;
}
}  // namespace

SCENARIO(pilotReplanBackground, "hitches/pilot-replan", "A replan made in the background: the old route drives on meanwhile, the new one goes in once made, and the same as one made at once") {
    Car& c = ctx.add(vehicleCatalog()[2], -110, -110, 0);
    ctx.focus(c);
    ctx.run(0.5);
    const World& w = ctx.sim.world;
    pilotStart(c, w, V3(120, 0, -110), "THE FAR SIDE", 30 / 3.6, PILOT_ANYWHERE);
    ctx.check("the first route is planned at once", c.pilot.active && c.pilot.path.size() >= 2);
    const std::vector<V3> first = c.pilot.path;
    const V3 from = c.frame().com, goal(100, 0, 60);
    pilotReplan(c, w, from, goal, 1.0, 1);
    const bool keptOld = samePath(c.pilot.path, first) && c.pilot.replanning != nullptr;
    waitWorldReaders();
    pilotTakeReplan(c, w);
    const std::vector<V3> background = c.pilot.path;
    std::vector<V3> now;
    planRoute(w, *c.spec, from, goal, c.pilot.route, c.pilot.avoid, now, 1.0);
    ctx.check("while it's being made the old route stays", keptOld);
    ctx.check("once made it goes in, the same route as one made at once", !c.pilot.replanning && samePath(background, now) && !samePath(background, first));
    pilotReplan(c, w, from, V3(120, 0, -110), 1.0, 1);
    waitWorldReaders();
    c.pilot.active = false;  // (stopped meanwhile: what was made is dropped)
    pilotTakeReplan(c, w);
    ctx.check("a replan that lands after the autopilot stopped is dropped", samePath(c.pilot.path, background) && !c.pilot.replanning);
}
