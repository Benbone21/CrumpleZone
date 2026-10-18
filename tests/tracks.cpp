// Tire tracks and skid marks (fx.h): laid at the tire's contact patch as wide as the tire, on loose
// ground when rolling and on hard ground only when sliding, none in the air; they fade out by age and
// distance, and the ring caps how many exist however long you drive.
#include <chrono>
#include <cmath>
#include <cstdio>

#include "../src/fx.h"
#include "../src/render.h"
#include "harness.h"

namespace {

void paint(World& w, double x0, double x1, double z0, double half, Surface s) {
    for (double x = x0; x <= x1; x += 0.5)
        for (double z = z0 - half; z <= z0 + half; z += 0.5) {
            const int id = w.cellIndex(x, z);
            if (id >= 0) w.setCellSurface(id, s, s == SURF_MUD ? PAINT_MUD : s == SURF_DIRT ? PAINT_TRAIL : PAINT_PAD);
        }
}

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }

// One 60 Hz frame with the effects following it. `air`: pretend nothing touches the ground.
void frame(Ctx& ctx, Effects& fx, bool air = false) {
    ctx.step();
    if (air)
        for (Car& c : ctx.sim.cars)
            for (Wheel& wh : c.wheels) wh.load = 0;
    fx.update(ctx.sim, Ctx::kFrame);
}

// Alpha of the strongest mark drawn from `eye`.
float maxAlpha(const Effects& fx, const V3& eye, size_t* verts = nullptr) {
    std::vector<Vtx> v;
    fx.buildSkids(eye, v);
    if (verts) *verts = v.size();
    float a = 0;
    for (const Vtx& x : v) a = std::max(a, x.c[3]);
    return a;
}

void drive(Ctx& ctx, Effects& fx, double seconds, double speedKmhGoal, double steer) {
    for (int i = 0; i < (int)(seconds * 60); i++) {
        Car& c = ctx.sim.cars[0];
        const double v = speedKmh(c);
        c.throttle = v < speedKmhGoal ? 0.6 : 0.0;
        c.brake = 0, c.handbrake = 0, c.steer = steer;
        frame(ctx, fx);
    }
}

}  // namespace

SCENARIO(tracksContact, "tracks/contact", "Tracks are laid by tires on the ground, none in the air or parked; as wide as the tire") {
    paint(ctx.sim.world, -200, 200, 0, 8, SURF_DIRT);
    Effects fx;
    fx.setLimits(markLimits(2));
    Car& c = ctx.add(bronco(), -150, 0, 0);
    ctx.run(4.0);  // (settled on its springs)
    ctx.sim.cars[0].brake = 1, ctx.sim.cars[0].handbrake = 1;
    ctx.run(2.0, [&] { ctx.sim.cars[0].brake = 1, ctx.sim.cars[0].handbrake = 1; });
    for (int i = 0; i < 120; i++) frame(ctx, fx);
    ctx.check("parked: no marks", fx.markStats().marks == 0, std::to_string(fx.markStats().marks));
    ctx.launch(c, 8);
    for (int i = 0; i < 120; i++) frame(ctx, fx, true);
    ctx.check("in the air: no marks", fx.markStats().marks == 0, std::to_string(fx.markStats().marks));
    drive(ctx, fx, 3, 30, 0);
    const size_t n = fx.markStats().marks;
    ctx.check("rolling on dirt: marks laid", n > 40, std::to_string(n));
    // Four wheels, each a mark about every 20 cm of travel: the count is in proportion to the road covered.
    double wmin = 1e9, wmax = 0;
    for (size_t i = 0; i < n; i++) {
        const auto m = fx.markInfo(i);
        wmin = std::min(wmin, m.width), wmax = std::max(wmax, m.width);
        if (m.rubber) ctx.check("dirt marks are grooves", false);
    }
    const double tw = c.wheels[0].width;
    ctx.metric("mark width vs tire width", wmin / tw, "x", 0.97, 1.03);
    ctx.metric("widest mark vs tire width", wmax / tw, "x", 0.97, 1.03);
}

SCENARIO(tracksPavement, "tracks/pavement", "Pavement gets marks only when the tires slide") {
    paint(ctx.sim.world, -300, 300, 0, 10, SURF_PAVED);
    Effects fx;
    fx.setLimits(markLimits(2));
    Car& c = ctx.add(bronco(), -250, 0, 0);
    ctx.run(1.0);
    ctx.launch(c, 60 / 3.6);
    drive(ctx, fx, 3, 60, 0);
    ctx.check("cruising straight on pavement: no marks", fx.markStats().marks == 0, std::to_string(fx.markStats().marks));
    for (int i = 0; i < 150; i++) {  // locked up
        Car& cc = ctx.sim.cars[0];
        cc.throttle = 0, cc.brake = 1, cc.handbrake = 1, cc.steer = 0.4;
        frame(ctx, fx);
    }
    const size_t n = fx.markStats().marks;
    size_t rubber = 0;
    for (size_t i = 0; i < n; i++) rubber += fx.markInfo(i).rubber;
    ctx.check("sliding on pavement: skid marks laid", rubber > 5, std::to_string(rubber) + " of " + std::to_string(n));
    ctx.check("pavement marks are all rubber", rubber == n);
    if (n) ctx.metric("skid width vs tire width", fx.markInfo(0).width / c.wheels[0].width, "x", 0.97, 1.03);
}

SCENARIO(tracksFade, "tracks/fade", "Marks fade out by age and by distance from the camera, smoothly, and are gone after their life") {
    paint(ctx.sim.world, -200, 200, 0, 8, SURF_DIRT);
    Effects fx;
    MarkLimits lim = markLimits(2);
    lim.life = 20, lim.fadeDist = 120;
    fx.setLimits(lim);
    Car& c = ctx.add(bronco(), -150, 0, 0);
    ctx.run(1.0);
    ctx.launch(c, 8);
    drive(ctx, fx, 3, 30, 0);
    const V3 here = fx.markInfo(fx.markStats().marks / 2).p;
    const V3 near = here + V3(0, 3, 0);
    const float fresh = maxAlpha(fx, near);
    ctx.check("fresh marks are drawn", fresh > 0.3f, std::to_string(fresh));
    // Distance: gone past fadeDist, fading in over the last 30%, never popping (small steps).
    float prev = fresh, worst = 0;
    for (double d = 0; d <= 130; d += 1) {
        const float a = maxAlpha(fx, here + V3(0, 3, 0) + V3(0, 0, d));
        worst = std::max(worst, std::fabs(a - prev));
        prev = a;
    }
    ctx.check("far away: gone", maxAlpha(fx, here + V3(0, 0, 125)) < 0.01f);
    ctx.check("at 90% of the fade distance: partly faded", maxAlpha(fx, here + V3(0, 3, 108)) < fresh * 0.6f);
    ctx.metric("largest alpha step per metre of camera distance", worst, "", 0, 0.12);
    // Age: fading over the last 30% of the life, gone after it.
    double wA = 0;
    float pa = fresh;
    for (int i = 0; i < 25 * 60; i++) {
        Effects* f = &fx;
        f->update(ctx.sim, Ctx::kFrame);
        if (i % 30 == 0 && f->markStats().marks) {
            const float a = maxAlpha(fx, near);
            wA = std::max(wA, (double)std::fabs(a - pa));
            pa = a;
        }
        if (i == 14 * 60) ctx.check("at 70% of its life: still full", maxAlpha(fx, near) > fresh * 0.7f);
        if (i == 18 * 60) ctx.check("at 90% of its life: mostly faded", maxAlpha(fx, near) < fresh * 0.5f);
    }
    ctx.check("after its life: expired", fx.markStats().marks == 0, std::to_string(fx.markStats().marks));
    ctx.metric("largest alpha step per half second of age", wA, "", 0, 0.12);
}

SCENARIO(tracksCap, "tracks/cap", "The ring holds at its cap however long you drive, and the cap follows the quality preset") {
    paint(ctx.sim.world, -400, 400, 0, 12, SURF_DIRT);
    Effects fx;
    MarkLimits lim = markLimits(4);  // POTATO's cap, with no age or distance fade to hide it
    lim.cap = 300, lim.life = 1e6f, lim.fadeDist = 1e6f;
    fx.setLimits(lim);
    Car& c = ctx.add(bronco(), -300, 0, 0);
    ctx.run(1.0);
    ctx.launch(c, 8);
    size_t most = 0;
    for (int k = 0; k < 6; k++) {
        drive(ctx, fx, 10, 30, k & 1 ? 0.15 : -0.15);
        most = std::max(most, fx.markStats().marks);
    }
    ctx.check("marks stay within the cap", most <= lim.cap, std::to_string(most) + " vs " + std::to_string(lim.cap));
    ctx.check("the preset ladder: POTATO < LOW < MEDIUM < HIGH < ULTRA",
              markLimits(4).cap < markLimits(0).cap && markLimits(0).cap < markLimits(1).cap && markLimits(1).cap < markLimits(2).cap &&
                  markLimits(2).cap < markLimits(3).cap && markLimits(4).life < markLimits(2).life && markLimits(4).fadeDist < markLimits(2).fadeDist);
    fx.setLimits({100, 60, 200});
    ctx.check("lowering the cap trims the oldest", fx.markStats().marks <= 100, std::to_string(fx.markStats().marks));
}

// A five minute drive in circles on dirt and mud: how many marks, how many vertices drawn and what building them costs,
// at the start and at the end.
SCENARIO(tracksLongDrive, "tracks/five-minute-drive", "Five minutes of driving: the mark count and the cost of drawing them stay flat") {
    paint(ctx.sim.world, -300, 300, 0, 100, SURF_DIRT);
    Effects fx;
    fx.setLimits(markLimits(1));
    Car& c = ctx.add(bronco(), -40, 0, 0);
    ctx.run(1.0);
    ctx.launch(c, 8);
    struct Row { double t; size_t marks, verts; double ms; };
    std::vector<Row> rows;
    auto sampleRow = [&](double t) {
        const V3 eye = ctx.sim.cars[0].frame().com + V3(0, 5, 0);
        std::vector<Vtx> v, w;
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 20; k++) {
            v.clear(), w.clear();
            fx.buildSkids(eye, v);
            fx.buildRutWalls(ctx.sim, eye, w);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20;
        rows.push_back({t, fx.markStats().marks, v.size() + w.size(), ms});
    };
    sampleRow(0);
    for (int s = 0; s < 300; s += 10) {
        Car& cc = ctx.sim.cars[0];
        const V3 p = cc.frame().com;
        const double r = std::hypot(p.x, p.z);
        drive(ctx, fx, 10, 28, r > 60 ? 0.25 : 0.1);
        if (s == 140 || s == 290) sampleRow(s + 10);
    }
    sampleRow(300);
    for (const Row& r : rows) {
        char b[160];
        std::snprintf(b, sizeof b, "tracks-csv: t=%.0f,marks=%zu,verts=%zu,build_ms=%.3f,bytes=%zu", r.t, r.marks, r.verts, r.ms, fx.markStats().bytes);
        ctx.note(b);
        std::puts(b);
    }
    const Row& a = rows[rows.size() / 2];
    const Row& z = rows.back();
    ctx.check("marks were laid", z.marks > 100, std::to_string(z.marks));
    ctx.check("mark count doesn't grow after the first minute-or-so (mid vs end)", z.marks <= a.marks + a.marks / 10 + 50,
              std::to_string(a.marks) + " vs " + std::to_string(z.marks));
    ctx.check("within the cap", z.marks <= markLimits(1).cap);
    ctx.check("drawn vertices stay bounded", z.verts <= (markLimits(1).cap * 6) * 3);
}
