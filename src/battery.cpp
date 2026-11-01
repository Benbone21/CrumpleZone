#include "battery.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "lights.h"
#include "trunk.h"

namespace battery {

namespace {

constexpr double kAlternator = 0.02;  // /s running
constexpr double kLampDrain = 0.0035; // /s, headlights with the engine off (a flat battery in about five minutes)
constexpr double kWinchDrain = 0.008; // /s, the winch drum turning with the engine off
constexpr double kCrankDrain = 0.05;  // /s while the starter turns
constexpr double kCableFlow = 0.12;   // /s into a battery from a running vehicle
constexpr double kCableTop = 0.9;

bool localCar(const Sim& sim, int i) { return i >= 0 && i < (int)sim.cars.size() && !sim.cars[i].remote && !sim.cars[i].isProp(); }

const MassItem* batteryItem(const Car& c) {
    if (!c.spec) return nullptr;
    for (const MassItem& it : c.spec->items)
        if (!std::strcmp(it.name, "battery")) return &it;
    return nullptr;
}

V3 restOf(const Car& c, int which) {
    if (const MassItem* it = batteryItem(c)) return V3(it->x, it->y + 0.1, it->z + (which ? -0.04 : 0.04));
    return V3(c.spec->engineX, 0.7, which ? -0.2 : 0.2);
}

bool donor(const Car& c) { return running(c) && (c.remote || c.battery > kFlat); }

}  // namespace

bool running(const Car& c) { return !c.engineDead && !c.engineOff && !c.isProp(); }
bool flat(const Car& c) { return c.battery < kFlat; }

bool lampsOn(const Sim& sim, const Car& c) {
    if (c.lampMode == LAMPS_LOW || c.lampMode == LAMPS_HIGH) return true;
    if (c.lampMode == LAMPS_AUTO) return sim.dayHours < 6 || sim.dayHours > 20;
    return false;
}

V3 post(const Car& c, int which) { return trunk::world(c, c.frame(), restOf(c, which)); }
V3 aim(const Car& c) { return (post(c, 0) + post(c, 1)) * 0.5; }

int cableOn(const Sim& sim, int car) {
    for (int i = 0; i < (int)sim.cables.size(); i++)
        if (sim.cables[i].a == car || sim.cables[i].b == car) return i;
    return -1;
}

double supply(const Sim& sim, int car) {
    const Car& c = sim.cars[car];
    double s = c.battery;
    for (const Sim::JumperCable& j : sim.cables) {
        const int other = j.a == car ? j.b : j.b == car ? j.a : -1;
        if (other >= 0 && other < (int)sim.cars.size() && donor(sim.cars[other])) s = std::max(s, 0.8);
    }
    return s;
}

void step(Sim& sim, double dt) {
    if (!(dt > 0)) return;
    for (int i = 0; i < (int)sim.cars.size(); i++) {
        if (!localCar(sim, i)) continue;
        Car& c = sim.cars[i];
        // Charge and drain.
        if (running(c)) {
            if (c.rpm > 300) c.battery = std::min(1.0, c.battery + kAlternator * dt);
        } else {
            double load = 0;
            if (lampsOn(sim, c)) load += kLampDrain;
            for (Rope& r : sim.ropes)
                if (c.engineOff && r.kind == ROPE_WINCH && r.a.kind == ANCHOR_CAR && r.a.car == i && r.winch != 0) {  // (a broken engine's winch runs on: the key was never turned off)
                    if (c.battery < kFlat) r.winch = 0;
                    else load += kWinchDrain;
                }
            c.battery = std::max(0.0, c.battery - load * dt);
        }
        // Under water.
        const V3 p = post(c, 0);
        if (p.y < sim.world.waterLevel(p.x, p.z)) {
            c.soak += dt;
            if (c.soak > 1.0) c.battery = std::max(0.0, c.battery - 0.3 * dt);
        } else c.soak = std::max(0.0, c.soak - dt);
        // The starter.
        c.cranking = false;
        if (c.engineOff && c.occupied && !c.engineDead) {
            const double sup = supply(sim, i);
            const double strength = clampd((sup - kFlat) / (kStrong - kFlat), 0, 1);
            c.cranking = true;
            if (strength > 0) {
                c.crank += dt * strength / kCrankTime;
                c.battery = std::max(0.0, c.battery - kCrankDrain * dt * (sup > c.battery ? 0.2 : 1.0));
                if (c.crank >= 1) {
                    c.engineOff = false, c.crank = 0, c.cranking = false;
                    c.rpm = std::max(c.rpm, 600.0);
                    c.wake();
                }
            } else c.crank = std::max(0.0, c.crank - dt);
        } else c.crank = std::max(0.0, c.crank - dt);
    }
    // Cables: the running one feeds the other; pulled off when the vehicles drift too far apart.
    for (size_t k = 0; k < sim.cables.size();) {
        const Sim::JumperCable& j = sim.cables[k];
        if (j.a < 0 || j.b < 0 || j.a >= (int)sim.cars.size() || j.b >= (int)sim.cars.size() || len(post(sim.cars[j.a], 0) - post(sim.cars[j.b], 1)) > kCableLen + 1.5) {
            sim.events.push_back("THE JUMPER CABLES CAME OFF");
            sim.cables.erase(sim.cables.begin() + k);
            continue;
        }
        const int pair[2][2] = {{j.a, j.b}, {j.b, j.a}};
        for (const auto& pr : pair) {
            Car &from = sim.cars[pr[0]], &to = sim.cars[pr[1]];
            if (!donor(from) || to.remote || running(to) || to.isProp()) continue;
            if (to.battery < kCableTop) to.battery = std::min(kCableTop, to.battery + kCableFlow * dt);
            if (!from.remote && from.battery > 0.3) from.battery -= 0.01 * dt;
        }
        k++;
    }
    if (sim.handCable.on() && (sim.handCable.car >= (int)sim.cars.size() || len(sim.handCable.hand - post(sim.cars[sim.handCable.car], 0)) > kCableLen + 0.5)) {
        sim.events.push_back("THE JUMPER CABLE SLIPPED OFF THE CLAMP");
        sim.handCable = Sim::HandCable();
    }
}

std::string jump(Sim& sim, int car) {
    if (car < 0 || car >= (int)sim.cars.size()) return "";
    Car& c = sim.cars[car];
    if (running(c)) return "IT'S RUNNING ALREADY";
    if (c.engineDead) return "THE ENGINE'S DEAD, NOT THE BATTERY";
    if (!c.remote) c.battery = std::max(c.battery, kPackLevel), c.soak = 0;
    return "JUMP PACK ON: START IT";
}

std::string clamp(Sim& sim, int car, bool& done) {
    done = false;
    if (car < 0 || car >= (int)sim.cars.size()) return "";
    if (!sim.handCable.on()) {
        sim.handCable.car = car;
        sim.handCable.hand = post(sim.cars[car], 0);
        return "CLAMPED: WALK THE CABLES TO THE OTHER VEHICLE";
    }
    if (sim.handCable.car == car) return "THAT'S THE SAME BATTERY";
    Sim::JumperCable j;
    j.a = sim.handCable.car, j.b = car;
    sim.cables.push_back(j);
    sim.handCable = Sim::HandCable();
    done = true;
    return "CABLES ON";
}

std::string unclamp(Sim& sim, int cable) {
    if (cable < 0 || cable >= (int)sim.cables.size()) return "";
    sim.cables.erase(sim.cables.begin() + cable);
    return "CABLES OFF";
}

void Sounds::update(const Sim& sim, double dt, const Play& play) {
    bool any = false;
    for (const Car& c : sim.cars) {
        if (c.remote || c.isProp() || !c.cranking) continue;
        any = true;
        const V3 at = c.frame().com;
        const double sup = c.battery;
        if (sup <= kFlat && !(wasCranking)) clickT = 0;
        if (sup <= kFlat) {
            clickT -= dt;
            if (clickT <= 0) play(SFX_CLICK, at, 0.7f, 0, 0.95f + 0.1f * (float)std::fmod(c.crank + sim.dayHours, 1.0)), clickT = 0.55;
        } else {
            crankT -= dt;
            const double slow = clampd((c.battery - kFlat) / (kStrong - kFlat), 0.15, 1.0);
            if (crankT <= 0) play(SFX_STARTER, at, 0.8f, 0, (float)(0.7 + 0.3 * slow)), crankT = 0.3 / slow;
        }
    }
    wasCranking = any;
}

}  // namespace battery
