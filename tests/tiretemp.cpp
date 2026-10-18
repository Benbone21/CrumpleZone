// Tire grip by tread and surface, and the tread's temperature: cold tires grip less, warm
// ones more; wheelspin and speed warm them, parking cools them, winter air starts them cold.
#include <cmath>

#include "../src/biome.h"
#include "../src/mods.h"
#include "../src/tiretemp.h"
#include "harness.h"

namespace {

void paintStrip(World& w, double x0, double x1, double z0, Surface s) {
    for (double x = x0; x <= x1; x += 0.5)
        for (double z = z0 - 6; z <= z0 + 6; z += 0.5) {
            const int id = w.cellIndex(x, z);
            if (id >= 0) w.setCellSurface(id, s, s == SURF_ICE ? PAINT_ICE : s == SURF_MUD ? PAINT_MUD : s == SURF_SNOW ? PAINT_SNOW : PAINT_PAD);
        }
}

// The Bronco on a given tread (the garage's tires slot).
const VehicleSpec& broncoOn(int tread) {
    const int v = 2;
    for (int o = 0; o < (int)modOptions(v, MOD_TIRES).size(); o++) {
        ModSet ms;
        ms.pick[MOD_TIRES] = o;
        const VehicleSpec& s = tunedSpec(v, ms);
        if (s.tireType == tread) return s;
    }
    return vehicleCatalog()[v];
}

// Full brakes from 40 km/h on a strip of `surf`, the tread held at `tempC` (NAN: left to
// itself): the distance to a stop.
double stopDistance(Ctx& ctx, const VehicleSpec& spec, Surface surf, double z, double tempC) {
    paintStrip(ctx.sim.world, -150, 40, z, surf);
    ctx.add(spec, -130, z, 0);
    const int idx = (int)ctx.sim.cars.size() - 1;
    auto hold = [&] {
        if (std::isfinite(tempC))
            for (Wheel& wh : ctx.sim.cars[idx].wheels) wh.tempC = (float)tempC;
    };
    ctx.run(1.0, hold);
    ctx.launch(ctx.sim.cars[idx], 40 / 3.6);
    const V3 p0 = ctx.sim.cars[idx].frame().com;
    ctx.runUntil(30, [&] { return len(ctx.sim.cars[idx].frame().vel) < 0.3; }, [&] {
        Car& cc = ctx.sim.cars[idx];
        cc.throttle = 0, cc.brake = 1, cc.handbrake = 0, cc.steer = 0;
        hold();
    });
    return len(ctx.sim.cars[idx].frame().com - p0);
}

// On a dyno: the body held still (no sideways or forward motion), the wheels turned at
// `spin` rad/s (or left to the engine when spin < 0), for `secs`. Calls each(t) each 0.5 s.
void dyno(Ctx& ctx, int idx, double secs, double rearSpin, double frontSpin, const std::function<void(double)>& each = nullptr) {
    Sim& sim = ctx.sim;
    const int steps = (int)std::lround(secs / sim.h), every = (int)std::lround(0.5 / sim.h);
    for (int k = 0; k < steps; k++) {
        Car& c = sim.cars[idx];
        c.sleeping = false;
        c.throttle = 0, c.brake = 0;
        for (Wheel& wh : c.wheels) wh.omega = wh.sideOmega = wh.front ? frontSpin : rearSpin, wh.twist = 0;
        sim.substep();
        for (Node& n : sim.cars[idx].nodes) n.v.x = n.v.z = 0;
        if (each && (k + 1) % every == 0) each((k + 1) * sim.h);
    }
}

// On a treadmill: the truck rolling at `speed` along its heading (no slip), put back where
// it started every frame, for `secs`. Calls each(t) each 0.5 s.
void treadmill(Ctx& ctx, int idx, double secs, double speed, const std::function<void(double)>& each) {
    Sim& sim = ctx.sim;
    const V3 home = sim.cars[idx].frame().com, fwd = sim.cars[idx].frame().fwd;
    const int steps = (int)std::lround(secs / sim.h), every = (int)std::lround(0.5 / sim.h), frame = (int)std::lround(Ctx::kFrame / sim.h);
    for (int k = 0; k < steps; k++) {
        Car& c = sim.cars[idx];
        c.sleeping = false;
        c.throttle = 0, c.brake = 0;
        for (Wheel& wh : c.wheels) wh.omega = wh.sideOmega = speed / wh.R, wh.twist = 0;
        for (Node& n : c.nodes) n.v.x = fwd.x * speed, n.v.z = fwd.z * speed;
        sim.substep();
        if ((k + 1) % frame == 0) {
            V3 d = home - sim.cars[idx].frame().com;
            d.y = 0;
            sim.cars[idx].translate(d);
        }
        if ((k + 1) % every == 0) each((k + 1) * sim.h);
    }
}

double meanTemp(const Car& c, int front) {  // front: 1 fronts, 0 rears, -1 all
    double s = 0;
    int n = 0;
    for (const Wheel& wh : c.wheels)
        if (front < 0 || wh.front == (front == 1)) s += wh.tempC, n++;
    return n ? s / n : 0;
}

}  // namespace

SCENARIO(tireRanking, "tires/grip-ranking", "Warm tires, braking from 40 km/h: race slicks stop shortest on pavement and worst off it; mud tires best in mud; street tires in between") {
    struct Row { int tread; const char* name; double warm; };
    const Row rows[] = {{TIRE_RACE_SLICK, "race slick", 90}, {TIRE_STREET, "street", 50}, {TIRE_MUD_TERRAIN, "mud-terrain", 50}};
    const Surface surfs[] = {SURF_PAVED, SURF_DIRT, SURF_MUD};
    const char* sname[] = {"pavement", "dirt", "mud"};
    double d[3][3];
    double z = -60;
    for (int s = 0; s < 3; s++)
        for (int r = 0; r < 3; r++, z += 18) {
            d[s][r] = stopDistance(ctx, broncoOn(rows[r].tread), surfs[s], z, rows[r].warm);
            ctx.metric(std::string(sname[s]) + ", " + rows[r].name + ": stopping from 40 km/h", d[s][r], "m", 5, 120);
        }
    ctx.check("pavement: slick < street < mud-terrain", d[0][0] < d[0][1] && d[0][1] < d[0][2]);
    ctx.check("dirt: mud-terrain < street < slick", d[1][2] < d[1][1] && d[1][1] < d[1][0]);
    ctx.check("mud: mud-terrain < street < slick", d[2][2] < d[2][1] && d[2][1] < d[2][0]);
    ctx.metric("dirt, slick vs street (much longer)", d[1][0] / d[1][1], "x", 1.4, 4);
}

SCENARIO(tireColdWarm, "tires/cold-warm", "Race slicks on rock: stone cold (5C) they stop far longer than in their window (90C); street tires barely care") {
    // (On rock: a hard face, where the Bronco's brakes still out-pull the tires.)
    const double cold = stopDistance(ctx, broncoOn(TIRE_RACE_SLICK), SURF_ROCK, -40, 5);
    const double warm = stopDistance(ctx, broncoOn(TIRE_RACE_SLICK), SURF_ROCK, -20, 90);
    const double sCold = stopDistance(ctx, broncoOn(TIRE_STREET), SURF_PAVED, 0, 5);
    const double sWarm = stopDistance(ctx, broncoOn(TIRE_STREET), SURF_PAVED, 20, 50);
    ctx.metric("race slicks: cold vs warm stopping distance", cold / warm, "x", 1.12, 1.6);
    ctx.metric("street tires: cold vs warm stopping distance", sCold / sWarm, "x", 0.97, 1.08);
    ctx.check("the curves: slick cold .72-.85, in window 1, overheated < 1", tireTempGrip(TIRE_RACE_SLICK, 5) < 0.85 && tireTempGrip(TIRE_RACE_SLICK, 5) > 0.7 &&
              tireTempGrip(TIRE_RACE_SLICK, 95) == 1 && tireTempGrip(TIRE_RACE_SLICK, 150) < 0.9);
    ctx.check("winter tires work cold and go greasy warm", tireTempGrip(TIRE_SNOW, -15) == 1 && tireTempGrip(TIRE_SNOW, 60) < 0.92);
}

SCENARIO(tireWarmup, "tires/warmup", "Spinning on rock warms the driven tires over tens of seconds (and a slick's grip comes up); parked they cool") {
    paintStrip(ctx.sim.world, -140, -110, 30, SURF_ROCK);
    ctx.add(broncoOn(TIRE_RACE_SLICK), -125, 30, 0);
    const int idx = (int)ctx.sim.cars.size() - 1;
    ctx.run(1.0);
    Car& c = ctx.sim.cars[idx];
    const double t0 = meanTemp(c, 0), g0 = tireTempGrip(TIRE_RACE_SLICK, t0);
    double t10 = 0, t30 = 0;
    // Rear wheels spun at ~8 m/s on the rock, the truck held where it is.
    dyno(ctx, idx, 30, 8 / c.wheels[2].R, 0, [&](double t) {
        const double tr = meanTemp(ctx.sim.cars[idx], 0);
        ctx.sample("tread temperature", "rear, spinning", tr, "C");
        if (std::fabs(t - 10) < 0.01) t10 = tr;
        t30 = tr;
    });
    const double g30 = tireTempGrip(TIRE_RACE_SLICK, t30);
    ctx.metric("spinning on rock: rear tread after 10 s", t10 - t0, "C up", 8, 60);
    ctx.metric("spinning on rock: rear tread after 30 s", t30 - t0, "C up", 25, 120);
    ctx.metric("slick grip from temperature: before, after 30 s", g30 - g0, "up", 0.08, 0.3);
    ctx.check("the undriven fronts stay near the air", std::fabs(meanTemp(ctx.sim.cars[idx], 1) - c.ambientC) < 5);
    // Parked: they cool back toward the air.
    const double hot = meanTemp(ctx.sim.cars[idx], 0);
    ctx.run(60, [&] { ctx.sim.cars[idx].handbrake = 1; });
    const double parked = meanTemp(ctx.sim.cars[idx], 0);
    ctx.metric("parked 60 s: rear tread cooled by", hot - parked, "C", 3, 60);
    ctx.check("temperatures stay finite and bounded", std::isfinite(hot) && hot < 250 && parked > -50);
}

SCENARIO(tireSteady, "tires/steady", "A long fast run (rolled on a treadmill at 120 km/h): the tread warms some tens of degrees over the air and settles; nothing runs away") {
    paintStrip(ctx.sim.world, -140, -110, 30, SURF_PAVED);
    ctx.add(vehicleCatalog()[2], -125, 30, 0);
    const int idx = (int)ctx.sim.cars.size() - 1;
    ctx.run(1.0);
    const double t0 = meanTemp(ctx.sim.cars[idx], -1);
    double at20 = 0, at80 = 0, peak = -1e9;
    bool finite = true;
    treadmill(ctx, idx, 100, 33.3, [&](double t) {
        const double tr = meanTemp(ctx.sim.cars[idx], -1);
        finite &= std::isfinite(tr);
        peak = std::max(peak, tr);
        ctx.sample("tread temperature", "120 km/h, rolling", tr, "C");
        if (std::fabs(t - 20) < 0.01) at20 = tr;
        if (std::fabs(t - 80) < 0.01) at80 = tr;
    });
    const double at100 = meanTemp(ctx.sim.cars[idx], -1);
    ctx.metric("120 km/h: tread over the air after 100 s", at100 - ctx.sim.cars[idx].ambientC, "C", 8, 60);
    ctx.check("120 km/h: settling (the last 20 s rise well under the first 20 s)", at100 - at80 < 0.6 * (at20 - t0),
              std::to_string(at20 - t0) + " vs " + std::to_string(at100 - at80));
    ctx.check("temperatures stay finite and bounded", finite && peak < 250 && peak > -50);
}

SCENARIO(tireWinter, "tires/winter", "In snow country the air is below freezing and the tires start there: cold street tires grip less than they would in summer") {
    paintStrip(ctx.sim.world, -140, -100, 50, SURF_SNOW);
    ctx.add(broncoOn(TIRE_STREET), -120, 50, 0);
    const int idx = (int)ctx.sim.cars.size() - 1;
    ctx.run(1.0);
    const Car& c = ctx.sim.cars[idx];
    ctx.metric("alpine air", c.ambientC, "C", -20, 0);
    ctx.metric("street tread at the start", meanTemp(c, -1), "C", -20, 2);
    ctx.check("cold street tires lose grip", tireTempGrip(TIRE_STREET, meanTemp(c, -1)) < 0.94);
    ctx.check("night is colder than the afternoon", ambientTempC(BIOME_DEFAULT, 4) < ambientTempC(BIOME_DEFAULT, 15) - 5);
    ctx.check("the desert is hot", ambientTempC(BIOME_DESERT, 12) > 30);
}
