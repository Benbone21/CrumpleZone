#include "harness.h"

#include <algorithm>
#include <cstdio>

std::vector<Scenario>& scenarioRegistry() {
    static std::vector<Scenario> registry;
    return registry;
}

void Ctx::step() {
    sim.updateAI(kFrame);
    double scale = sim.timeScale;  // scenarios always step one real 60 Hz frame
    bool paused = sim.paused;
    sim.timeScale = 1;
    sim.paused = false;
    sim.advance(kFrame, 1000);
    sim.timeScale = scale;
    sim.paused = paused;
    for (Car& c : sim.cars) {
        c.updateStats();
        if (c.glassBursts.size() > 256) c.glassBursts.clear();  // nobody consumes them headless
    }
    if (sim.events.size() > 64) sim.events.clear();
    t += kFrame;
    checkInvariants();
    if (onStep) onStep();
}

void Ctx::run(double seconds, const std::function<void()>& perFrame) {
    double end = t + seconds - 1e-9;
    while (t < end) {
        if (abortFlag && *abortFlag) return;
        if (perFrame) perFrame();
        step();
    }
}

bool Ctx::runUntil(double maxSeconds, const std::function<bool()>& done, const std::function<void()>& perFrame) {
    double end = t + maxSeconds - 1e-9;
    while (t < end) {
        if (abortFlag && *abortFlag) return false;
        if (done()) return true;
        if (perFrame) perFrame();
        step();
    }
    return done();
}

void Ctx::checkInvariants() {
    static const char* names[3] = {"invariant: all node positions finite", "invariant: no node faster than 150 m/s",
                                   "invariant: nothing tunnels below the terrain"};
    for (size_t ci = 0; ci < sim.cars.size(); ci++) {
        const Car& c = sim.cars[ci];
        for (const Node& n : c.nodes) {
            // (Sand, mud and snow give under a landing: a body or axle can plough ~40 cm in.)
            const double gy = sim.world.height(n.p.x, n.p.z);
            const double give = n.p.y < gy - 0.35 ? surfaceInfo(sim.world.surfaceAt(n.p.x, n.p.z)).sink * 4 : 0.0;
            bool bad[3] = {!std::isfinite(n.p.x + n.p.y + n.p.z), dot(n.v, n.v) > 150.0 * 150.0,
                           n.p.y < gy - 0.35 - give};
            for (int k = 0; k < 3; k++)
                if (bad[k] && !invariantFailed[k]) {
                    invariantFailed[k] = true;
                    char buf[160];
                    std::snprintf(buf, sizeof(buf), "%s #%zu at t=%.2f s (node %d, %.2f m under %s)", c.spec->name, ci, t,
                                  (int)(&n - c.nodes.data()), gy - n.p.y, surfaceInfo(sim.world.surfaceAt(n.p.x, n.p.z)).name);
                    check(names[k], false, buf);
                }
        }
    }
}

Car& Ctx::add(const VehicleSpec& spec, double x, double z, double headingDeg, bool ai) {
    double yaw = headingDeg * M_PI / 180;
    // High enough that no corner starts inside a ramp (it then drops and settles on it).
    double y = sim.world.clearY(x, z);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            double lx = sx * spec.length / 2, lz = sz * spec.width / 2;
            y = std::max(y, sim.world.clearY(x + lx * std::cos(yaw) + lz * std::sin(yaw), z - lx * std::sin(yaw) + lz * std::cos(yaw)));
        }
    sim.addCar(spec, V3(x, y + 0.05, z), yaw, sim.nextColor());
    Car& c = sim.cars.back();
    if (ai && !sim.world.loopRoad.empty()) {
        c.ai = true;
        c.aiIndex = sim.nearestRoadIndex(c.frame().com, -1);
        const auto& road = sim.world.loopRoad;
        V3 d = road[(c.aiIndex + 1) % road.size()] - road[c.aiIndex];
        V3 fwd(std::cos(yaw), 0, -std::sin(yaw));
        c.aiDir = dot(d, fwd) >= 0 ? 1 : -1;
    }
    return c;
}

Car& Ctx::addProp(const VehicleSpec& spec, double x, double z, double headingDeg) {
    sim.addCar(spec, V3(x, sim.world.clearY(x, z) + 0.05, z), headingDeg * M_PI / 180, V3(0.9, 0.5, 0.15));
    return sim.cars.back();
}

void Ctx::launch(Car& car, double speed) {
    Frame fr = car.frame();
    // Level heading: the body's own forward axis tilts a little with the nose shape, and
    // launching along it would slam the car into (or lift it off) the road.
    V3 dir = norm(V3(fr.fwd.x, 0, fr.fwd.z));
    for (Node& n : car.nodes) n.v = dir * speed;
    for (Wheel& w : car.wheels) w.omega = speed / w.R;
    // A gear that suits the speed (as if the car had been driven up to it), so the engine
    // isn't dropped into an over-rev that locks the driven wheels.
    if (!car.wheels.empty() && speed > 1) {
        const VehicleSpec& vs = *car.spec;
        car.gear = 1;
        for (int g = 1; g <= (int)vs.gears.size(); g++) {
            double rpm = speed / car.wheels[0].R * vs.gears[g - 1] * vs.finalDrive * 60 / (2 * M_PI);
            car.gear = g;
            if (rpm < vs.redline - 1200) break;
        }
    }
    car.wake();
}

void Ctx::focus(const Car& car) {
    for (size_t i = 0; i < sim.cars.size(); i++)
        if (&sim.cars[i] == &car) sim.player = (int)i;
}

bool Ctx::check(const std::string& name, bool ok, const std::string& detail) {
    checks.push_back({name, ok, detail});
    return ok;
}

bool Ctx::metric(const std::string& name, double value, const std::string& unit, double lo, double hi) {
    bool ok = std::isfinite(value) && value >= lo && value <= hi;
    metrics.push_back({name, unit, value, lo, hi, ok});
    return ok;
}

void Ctx::sample(const std::string& chart, const std::string& line, double value, const std::string& unit) {
    for (SeriesData& s : series)
        if (s.chart == chart && s.line == line) {
            s.t.push_back(t);
            s.v.push_back(value);
            return;
        }
    series.push_back({chart, line, unit, {t}, {value}});
}

double speedKmh(const Car& car) { return len(car.frame().vel) * 3.6; }

double headingDeg(const Car& car) {
    Frame f = car.frame();
    return std::atan2(-f.fwd.z, f.fwd.x) * 180 / M_PI;
}

double travelled(const Car& car, const V3& from, const V3& dir) { return dot(car.frame().com - from, dir); }

int intrusions(const Car& a, const Car& b) {
    // b's current (possibly crumpled) extent along its own axes, shrunk to its core.
    // Percentiles rather than min/max, so a few nodes flung loose don't inflate it.
    Frame f = b.frame();
    std::vector<double> xs, zs;
    for (int i = 0; i < b.bodyNodes; i++) {
        V3 d = b.nodes[i].p - f.com;
        xs.push_back(dot(d, f.fwd));
        zs.push_back(dot(d, f.left));
    }
    std::sort(xs.begin(), xs.end());
    std::sort(zs.begin(), zs.end());
    auto pct = [](const std::vector<double>& v, double q) { return v[(size_t)(q * (v.size() - 1))]; };
    double x0 = pct(xs, 0.05) + 0.45, x1 = pct(xs, 0.95) - 0.45;
    double z0 = pct(zs, 0.05) + 0.4, z1 = pct(zs, 0.95) - 0.4;
    int n = 0;
    for (int i = 0; i < a.bodyNodes; i++) {
        V3 d = a.nodes[i].p - f.com;
        double x = dot(d, f.fwd), z = dot(d, f.left), y = dot(d, f.up);
        if (x > x0 && x < x1 && z > z0 && z < z1 && y > -0.15 && y < 0.25) n++;
    }
    return n;
}

double loopLateral(const World& w, const V3& p, int idx) {
    const auto& road = w.loopRoad;
    const V3& a = road[idx];
    const V3& b = road[(idx + 1) % road.size()];
    V3 t = norm(V3(b.x - a.x, 0, b.z - a.z));
    return (p.x - a.x) * -t.z + (p.z - a.z) * t.x;
}
