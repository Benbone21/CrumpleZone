// Tests at the joints between systems: a damaged part must affect the thing it
// carries, and tires must forget old road contact after leaving the ground.
#include "harness.h"
#include "../src/carvisual.h"
#include "../src/mods.h"

VEHICLE_SCENARIO(airbornePatch, "components/airborne-tires", "A jump clears contact memory; free wheels coast with their own inertia") {
    Car& c = ctx.add(spec, -100, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    for (Node& n : c.nodes) { n.p.y += 20; n.v = V3(); }
    c.engineDead = true;
    c.rpm = 0;
    for (Wheel& w : c.wheels) {
        w.omega = 30;
        w.stickX = w.stickY = 0.01;
        w.groundSpeed = 50;
        w.abs = 0.1;
    }
    ctx.run(0.1);
    for (size_t i = 0; i < c.wheels.size(); i++) {
        const Wheel& w = c.wheels[i];
        std::string at = "wheel " + std::to_string(i);
        ctx.check(at + " has no stale contact", w.load == 0 && w.stickX == 0 && w.stickY == 0);
        ctx.check(at + " resets ABS after leaving ground", w.abs == 1);
        ctx.metric(at + " angular momentum lost", (30 - w.omega) * w.I, "kg m2/s", 0.045, 0.055);
        ctx.metric(at + " tread/road speed", std::fabs(w.groundSpeed), "m/s", 0, 0.2);
    }
}

VEHICLE_SCENARIO(brokenDifferential, "components/broken-halfshaft", "An open or helical differential cannot send full power through one broken output") {
    Car& c = ctx.add(spec, -100, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    for (const Axle& ax : c.axles) {
        Wheel& w = c.wheels[ax.wheel[0]];
        Wheel& other = c.wheels[ax.wheel[1]];
        if (!w.driven || w.shafts.empty()) continue;
        c.beams[w.shafts.back()].broken = true;
        ctx.check("broken axle output loses drive", !c.wheelPowered(w));
        ctx.check("differential loses drive through the other output", !c.wheelPowered(other));
    }
    c.throttle = 1;
    ctx.run(1.0);
    ctx.metric("car cannot accelerate with shafts disconnected", speedKmh(c), "km/h", 0, 0.5);
}

VEHICLE_SCENARIO(detachedDoorGlass, "components/door-glass", "A removed door carries its glass and collision surface with it") {
    Car& c = ctx.add(spec, -100, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    Panel* door = nullptr;
    for (Panel& p : c.panels) if (std::string(p.name) == "LEFT DOOR") door = &p;
    ctx.check("left door exists", door != nullptr);
    if (!door) return;
    auto glassCenter = [&]() {
        V3 p; int n=0;
        for (int q : door->quads) for (int k=0;k<2;k++) {
            const Tri& t=c.tris[c.quads[q].tri+k];
            if(t.mat != MAT_GLASS) continue;
            for(int v : t.v) {p+=c.nodes[v].p; n++;}
        }
        ctx.check("door includes glass", n>0);
        return p / std::max(1,n);
    };
    V3 before=glassCenter(), delta(0,0,-2);
    for (int p : door->pins) c.pins[p].broken=true;
    for (int n : door->panelNodes) c.nodes[n].p+=delta;
    c.updateStats();
    c.updateTris();
    ctx.metric("glass follows the detached door",len(glassCenter()-before-delta),"m",0,1e-6);
    ctx.check("door reports detached",door->state==2);
    ctx.run(0.15);
    ctx.check("detached assembly remains finite",c.finite());
}

SCENARIO(lockedHalfshaft, "components/locked-differential", "A locked differential still drives its surviving output after a halfshaft breaks") {
    VehicleSpec locker=vehicleCatalog()[0];
    locker.diffFront=DIFF_LOCKED;
    Car& c=ctx.add(locker,-100,-50,0);
    ctx.focus(c);
    ctx.run(1.0);
    Wheel& a=c.wheels[0];
    Wheel& b=c.wheels[1];
    c.beams[a.shafts.back()].broken=true;
    ctx.check("broken output is disconnected",!c.wheelPowered(a));
    ctx.check("locked differential retains its intact output",c.wheelPowered(b));
    c.throttle=0.5;
    ctx.run(1);
    ctx.metric("surviving wheel can propel the car",speedKmh(c),"km/h",1,30);
}

// ---- Solid rear axles: travel and clearance ------------------------------------
// The Bronco's and 4Runner's live rear axles hang under the frame, the exhaust and the
// tank (carvisual.cpp lays those out to clear axleReach()). Hard landings must not drive
// the axle further up than that, the four-link must not go over centre (it used to: the
// housing rolled pinion-up and stayed jammed in the floor), and on articulation it must
// come back level.
namespace {

struct AxleWatch {
    const Car& c;
    const Axle* ax = nullptr;
    int pick[3] = {-1, -1, -1};  // left and right coil tops, the upper link's frame end
    std::vector<std::pair<int, double>> line;  // points along the tube: (diff -> hub inner node, fraction)
    double rise = -1, droop = -1, pitch = 0;  // worst so far (m, m, degrees)
    explicit AxleWatch(const Car& car) : c(car) {
        for (const Axle& a : c.axles)
            if (!a.front && !a.housing.empty()) ax = &a;
        if (!ax) return;
        for (int s = 0; s < 2; s++) {
            const Beam& sp = c.beams[c.wheels[ax->wheel[s]].spring];
            pick[s] = sp.a < c.frameNodes ? sp.a : sp.b;
        }
        for (const SuspLink& l : c.links) {
            const Beam& b = c.beams[l.beam];
            if (l.kind == LINK_TRAILING && l.wheel < 0 && (b.a == ax->housing[2] || b.b == ax->housing[2]))
                pick[2] = b.a == ax->housing[2] ? b.b : b.a;
        }
        line.push_back({-1, 0});
        for (int s = 0; s < 2; s++)
            for (double f : {0.4, 0.7}) line.push_back({c.wheels[ax->wheel[s]].inner, f});  // under the rails
    }
    // A point in the frame of the body around the axle (its coil towers and upper link
    // mount), at rest or now: so it's what you'd see against the frame and floor above.
    V3 local(const V3& p, bool rest) const {
        auto P = [&](int n) { return rest ? c.restLocal[n] : c.nodes[n].p; };
        const V3 a = P(pick[0]), b = P(pick[1]), mid = (a + b) * 0.5, ez = norm(b - a);
        V3 ex = P(pick[2]) - mid;
        ex = norm(ex - ez * dot(ex, ez));
        const V3 ey = cross(ez, ex), d = p - mid;
        return V3(dot(d, ex), dot(d, ey), dot(d, ez));
    }
    V3 point(int i, bool rest) const {
        auto P = [&](int n) { return rest ? c.restLocal[n] : c.nodes[n].p; };
        const V3 diff = P(ax->housing[0]);
        return line[i].first < 0 ? diff : diff + (P(line[i].first) - diff) * line[i].second;
    }
    // Housing pitch against the body, degrees (0 = as built; + = top rolled back).
    double housingPitch() const {
        V3 d0 = local(c.nodes[ax->housing[0]].p, false), t0 = local(c.nodes[ax->housing[2]].p, false);
        V3 d1 = local(c.restLocal[ax->housing[0]], true), t1 = local(c.restLocal[ax->housing[2]], true);
        return (std::atan2(-(t0.x - d0.x), t0.y - d0.y) - std::atan2(-(t1.x - d1.x), t1.y - d1.y)) * 180 / M_PI;
    }
    double riseNow() const {
        double r = -1;
        for (size_t i = 0; i < line.size(); i++) r = std::max(r, local(point((int)i, false), false).y - local(point((int)i, true), true).y);
        return r;
    }
    void sample() {
        for (size_t i = 0; i < line.size(); i++) {
            double dy = local(point((int)i, false), false).y - local(point((int)i, true), true).y;
            rise = std::max(rise, dy);
            droop = std::max(droop, -dy);
        }
        double p = housingPitch();
        if (std::fabs(p) > std::fabs(pitch)) pitch = p;
    }
};

// Drops a truck from `h` (optionally pitched nose-up, rolled, moving forward) and watches
// its rear axle through the landing and the settle.
[[maybe_unused]] void axleLanding(Ctx& ctx, const VehicleSpec& spec, const std::string& tag, double z, double h, double pitchDeg, double rollDeg, double speed) {
    Car& c = ctx.add(spec, -100, z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    if (pitchDeg != 0) c.rotateRigid(c.frame().left, pitchDeg * M_PI / 180);
    if (rollDeg != 0) c.rotateRigid(c.frame().fwd, rollDeg * M_PI / 180);
    const V3 fwd = c.frame().fwd;
    for (Node& n : c.nodes) {
        n.p.y += h;
        n.v = fwd * speed;
    }
    for (Wheel& w : c.wheels) w.omega = speed / w.R;
    AxleWatch aw(c);
    ctx.check(tag + ": rear axle found", aw.ax && aw.pick[2] >= 0);
    if (!aw.ax || aw.pick[2] < 0) return;
    c.brake = speed > 0 ? 0.4 : 0;
    ctx.run(3.0, [&] {
        aw.sample();
        ctx.sample("axle rise under the frame", tag, aw.riseNow() * 100, "cm");
    });
    const double bump = spec.bumpRear;
    ctx.metric(tag + ": axle past its spec bump (" + std::to_string((int)std::lround(bump * 100)) + " cm), squashing the stop", (aw.rise - bump) * 100, "cm", -6, kAxleCrush * 100);
    ctx.check(tag + ": housing stays upright on its links (worst pitch " + std::to_string((int)std::lround(aw.pitch)) + " deg)", std::fabs(aw.pitch) < 35);
    ctx.check(tag + ": settles back level", std::fabs(aw.housingPitch()) < 3 && aw.riseNow() < 0.04);
    ctx.sim.carRemoved((int)ctx.sim.cars.size() - 1);
    ctx.sim.cars.pop_back();
}

}  // namespace

// Articulation: the crawl park's twister lifts diagonal wheels in turn, so the rear axle
// works through full bump on one side and droop on the other.
SCENARIO(axleTwister, "components/axle-articulation", "Bronco and 4Runner crawl the twister in 4L with lockers: the rear axle works bump and droop and stays under its frame clearance") {
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 tw = mountainFeature("twister");
    for (int v : {2, 3}) {
        VehicleSpec spec = vehicleCatalog()[v];
        spec.lockerFront = spec.lockerRear = true;
        static VehicleSpec keep[4];  // the car keeps a pointer to its spec
        keep[v] = spec;
        const std::string tag = v == 2 ? "Bronco" : "4Runner";
        Car& c = ctx.add(keep[v], tw.x, tw.z - 6, -90);
        ctx.focus(c);
        ctx.run(1.0);
        c.setDriveMode((int)driveModes(spec.transfer).size() - 1);  // 4L
        c.setLocker(true, true);
        c.setLocker(false, true);
        c.tractionControl = false;
        AxleWatch aw(c);
        double roll = 0, lifted = 0;
        ctx.runUntil(30, [&] { return c.frame().com.z > tw.z + 40; }, [&] {
            Frame fr = c.frame();
            V3 target(tw.x, fr.com.y, fr.com.z + 6), d = target - fr.com;
            c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 1.5, -0.55, 0.55);
            const double sp = dot(fr.vel, fr.fwd);
            c.throttle = clampd(0.3 + (1.0 - sp) * 0.9, 0, 1);
            c.brake = sp > 2.0 ? 0.3 : 0;
            aw.sample();
            // Axle roll against the body: one tube up, the other down.
            V3 l = aw.local(c.nodes[c.wheels[aw.ax->wheel[0]].inner].p, false), r = aw.local(c.nodes[c.wheels[aw.ax->wheel[1]].inner].p, false);
            roll = std::max(roll, std::fabs(std::atan2(l.y - r.y, r.z - l.z)) * 180 / M_PI);
            for (const Wheel& w : c.wheels) lifted = std::max(lifted, w.load < 50 ? 1.0 : 0.0);
            ctx.sample("axle rise under the frame", tag, aw.riseNow() * 100, "cm");
        });
        ctx.metric(tag + ": rear axle articulation against the body", roll, "deg", 6, 30);
        ctx.check(tag + ": a wheel came off the ground", lifted > 0);
        ctx.metric(tag + ": axle past its spec bump (" + std::to_string((int)std::lround(spec.bumpRear * 100)) + " cm)", (aw.rise - spec.bumpRear) * 100, "cm", -12, kAxleCrush * 100);
        ctx.metric(tag + ": axle droop under the rails past its spec (" + std::to_string((int)std::lround(spec.droopRear * 100)) + " cm)", (aw.droop - spec.droopRear) * 100, "cm", -15, 3);
        ctx.check(tag + ": housing stays upright (worst pitch " + std::to_string((int)std::lround(aw.pitch)) + " deg)", std::fabs(aw.pitch) < 20);
        ctx.sim.carRemoved((int)ctx.sim.cars.size() - 1);
        ctx.sim.cars.pop_back();
    }
}

// ---- Brake traction control on a split-friction pull-away ------------------------
// Left wheels on the ice strip, right wheels on the concrete apron beside it, rear-drive
// only (2H). An open diff gives both rear wheels the torque the one on ice can hold, so
// with the system off the ice wheel spins and the truck barely creeps. Braking that wheel
// (Bronco: AdvanceTrac's brake traction control) hands the concrete side its own share.
SCENARIO(splitMuPullAway, "components/brake-traction-control", "Bronco in 2H, left wheels on ice: brake traction control gets it moving, the open diff alone barely does") {
    auto pull = [&](bool tc) {
        ctx.sim.cars.clear();
        ctx.sim.player = 0;
        // Ice strip z 117..125, concrete from 125: centre the truck on the edge.
        Car& c = ctx.add(vehicleCatalog()[2], -100, 125.2, 0);
        ctx.focus(c);
        const auto& modes = driveModes(c.spec->transfer);
        for (size_t m = 0; m < modes.size(); m++)
            if (!modes[m].front) c.setDriveMode((int)m);
        c.tractionControl = tc;
        ctx.run(1.0);
        Surface l = SURF_PAVED, r = SURF_PAVED;
        for (const Wheel& w : c.wheels)
            if (!w.front) (w.left ? l : r) = w.surface;
        ctx.check(std::string(tc ? "on: " : "off: ") + "left rear on ice, right rear on concrete", l == SURF_ICE && r == SURF_PAVED);
        double brakeMax = 0;
        ctx.run(4.0, [&] {
            c.throttle = 1;
            c.steer = 0;
            for (const Wheel& w : c.wheels) brakeMax = std::max(brakeMax, w.tcBrake);
        });
        return std::make_pair(speedKmh(c), brakeMax);
    };
    const auto off = pull(false);
    const auto on = pull(true);
    // Off: ~0.1 g of grip on the ice wheel, twice over: ~0.05 g, about 7 km/h in 4 s.
    ctx.metric("speed after 4 s, traction control off", off.first, "km/h", 1, 12);
    // On: the concrete wheel pulls on its own grip (less what the brake costs the engine).
    ctx.metric("speed after 4 s, traction control on", on.first, "km/h", 15, 45);
    ctx.metric("most brake it used on the ice wheel", on.second * 100, "%", 10, 100);
}
