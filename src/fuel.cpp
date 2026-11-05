// Fuel by hand: the can and the pumps (fuel.h).
#include "fuel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "career.h"
#include "interact.h"
#include "trunk.h"
#include "walker.h"
#include "world.h"

namespace fuel {

namespace {

// Where each vehicle's filler is: m ahead of its tail, and on which side (+z is its right), as its body's drawn. `top`: up through the deck.
struct Filler {
    const char* id;
    double back;
    double side;
    bool top;
};
const Filler kFillers[] = {
    {"civic", 0.55, 1, false},  {"jesko", 1.15, -1, false}, {"bronco", 0.78, -1, false}, {"4runner", 0.78, -1, false}, {"buggy", 0.45, 0, true},
    {"utv", 0.7, -1, false},    {"tacoma", 1.1, -1, false}, {"blazer", 0.8, -1, false},  {"baja", -0.65, 0, true},  // (back < 0: from the nose)
};

int carOf(const Sim& sim, uint32_t id) {
    for (int i = 0; i < (int)sim.cars.size(); i++)
        if (sim.cars[i].netId == id) return i;
    return -1;
}

std::string liters(double l) {
    char b[32];
    std::snprintf(b, sizeof b, "%.0f L", l);
    return b;
}

// Where the hose's message says it runs now.
std::string hoseText(const Sim& sim) {
    const Sim::FuelHose& h = sim.hose;
    if (!h.on()) return "hose -";
    char b[96];
    const uint32_t id = h.state == Sim::FuelHose::PLUGGED && h.car >= 0 && h.car < (int)sim.cars.size() ? sim.cars[h.car].netId : 0;
    std::snprintf(b, sizeof b, "hose %.2f %.2f %.2f %u", h.pump.x, h.pump.y, h.pump.z, id);
    return b;
}

}  // namespace

V3 fillerRest(const Car& c) {
    double xr = 1e9, xf = -1e9, yLo = 1e9, yHi = -1e9;
    for (int i = 0; i < c.bodyNodes; i++) {
        const V3& p = c.restLocal[i];
        xr = std::min(xr, p.x), xf = std::max(xf, p.x), yLo = std::min(yLo, p.y), yHi = std::max(yHi, p.y);
    }
    double back = 0.16 * (xf - xr), side = -1;
    bool top = false;
    for (const Filler& f : kFillers)
        if (c.spec->id && !std::strcmp(c.spec->id, f.id)) back = f.back, side = f.side, top = f.top;
    const double x = back < 0 ? xf + back : xr + back;
    if (top) {  // (on the deck: the highest of the body nodes near there, on the centre line)
        double y = yLo;
        for (int i = 0; i < c.bodyNodes; i++) {
            const V3& p = c.restLocal[i];
            if (std::fabs(p.x - x) < 0.3 && std::fabs(p.z) < 0.4) y = std::max(y, p.y);
        }
        return V3(x, y + 0.03, 0);
    }
    const double y = yLo + 0.62 * (yHi - yLo);
    double z = 0;  // (the body's side there)
    for (int i = 0; i < c.bodyNodes; i++) {
        const V3& p = c.restLocal[i];
        if (std::fabs(p.x - x) < 0.4 && std::fabs(p.y - y) < 0.35) z = std::max(z, p.z * side);
    }
    return V3(x, y, side * (z + 0.03));
}

V3 filler(const Car& c) { return trunk::world(c, c.frame(), fillerRest(c)); }

double room(const Car& c) {
    if (c.remote) return 1e9;
    return std::max(0.0, c.spec->fuelCapacity - (c.fuel < 0 ? c.spec->fuelCapacity : c.fuel));
}

void findPumps(const World& w, std::vector<V3>& out) {
    out.clear();
    for (const Placed& m : w.models) {
        if (m.id != "fuel_pump") continue;
        const V3 f(std::cos(m.yaw), 0, -std::sin(m.yaw)), l(std::sin(m.yaw), 0, std::cos(m.yaw));
        auto spot = [&](double x, double y, double z) { out.push_back(m.p + f * x + V3(0, y, 0) + l * z); };
        if (m.variant == 1) spot(-1.05, 1.1, 0), spot(1.05, 1.1, 0);  // (the island's two)
        else if (m.variant == 2) spot(0.2, 0.9, 0.3);                 // (the drum's hand pump)
        else spot(0.24, 0.95, 0);
    }
}

std::string pour(Sim& sim, int car, double l) {
    Car& t = sim.cars[car];
    if (t.remote) {
        char b[64];
        std::snprintf(b, sizeof b, "fuel %u %.1f", t.netId, l);
        sim.hose.outbox.push_back(b);
        return "POURED " + liters(l) + " IN";
    }
    const double in = std::min(l, room(t));
    if (t.fuel < 0) t.fuel = t.spec->fuelCapacity;
    t.fuel = std::min(t.spec->fuelCapacity, t.fuel + in);
    return "POURED " + liters(in) + " INTO THE " + t.spec->name;
}

std::string step(Sim& sim, const Walker* wk, double dt, int* cash) {
    Sim::FuelHose& h = sim.hose;
    std::string said;
    auto stopNote = [&](const char* why) {
        char b[96];
        if (h.owed > 0 || h.paid > 0) std::snprintf(b, sizeof b, "%s %.1f L, $%d", why, h.pumped, h.paid);
        else std::snprintf(b, sizeof b, "%s %.1f L", why, h.pumped);
        return std::string(b);
    };
    auto flushRemote = [&](Car& c) {
        if (h.unsent <= 0.01) return;
        char b[64];
        std::snprintf(b, sizeof b, "fuel %u %.2f", c.netId, h.unsent);
        h.outbox.push_back(b);
        h.unsent = 0, h.sendIn = 0.5;
    };
    if (h.state == Sim::FuelHose::HAND) {
        if (!wk) said = "THE HOSE GOES BACK ON THE PUMP", h.release();
        else if (len(h.hand - h.pump) > kHoseLen + kHoseSlack) said = "TOO FAR: THE HOSE SPRINGS BACK TO THE PUMP", h.release();
    } else if (h.state == Sim::FuelHose::PLUGGED) {
        if (h.car < 0 || h.car >= (int)sim.cars.size()) h.release();
        else {
            Car& c = sim.cars[h.car];
            const V3 f = filler(c);
            h.hand = f;
            if (!std::isfinite(f.x) || len(f - h.plugAt) > kPopAt || len(f - h.pump) > kHoseLen + kHoseSlack) {  // driven off, or rolled away
                if (c.remote) flushRemote(c);
                said = h.pumped > 0.05 ? stopNote("THE HOSE POPPED OUT:") : "THE HOSE POPPED OUT";
                h.release();
            } else if (!h.full && !h.broke) {
                const double spare = !c.remote && c.jerry >= 0 ? std::max(0.0, c.spec->jerryCan - c.jerry) : 0;  // (the truck's spare can tops up after the tank)
                const double rm = room(c) + spare;
                double l = std::min(kFlow * dt, rm);
                if (cash && l > 0) {  // (a dollar at a time, and no further than the money goes)
                    const double affordable = (*cash - h.owed) / kPricePerLiter;
                    if (affordable <= 0.001) {
                        h.broke = true, l = 0;
                        said = stopNote("OUT OF MONEY:");
                    }
                    l = std::min(l, std::max(0.0, affordable));
                    h.owed += l * kPricePerLiter;
                    const int whole = (int)h.owed;
                    *cash -= whole, h.owed -= whole, h.paid += whole;
                }
                if (l > 0) {
                    h.pumped += l;
                    if (c.remote) h.unsent += l;
                    else {
                        if (c.fuel < 0) c.fuel = c.spec->fuelCapacity;
                        const double tank = std::min(l, room(c));
                        c.fuel = std::min(c.spec->fuelCapacity, c.fuel + tank);
                        if (l > tank) c.jerry = std::min(c.spec->jerryCan, c.jerry + l - tank);
                    }
                }
                if (!c.remote && rm - l <= 1e-6) h.full = true, said = stopNote("TANK FULL:");
            }
            if (h.on() && c.remote) {
                h.sendIn -= dt;
                if (h.sendIn <= 0) flushRemote(c);
            }
        }
    }
    // The others see where it runs.
    const std::string now = hoseText(sim);
    h.tellIn -= dt;
    if (now != h.told || (h.on() && h.tellIn <= 0)) {
        h.outbox.push_back(now);
        h.told = now, h.tellIn = kHoseRefresh;
    }
    return said;
}

std::string gauge(const Sim& sim) {
    const Sim::FuelHose& h = sim.hose;
    if (h.state != Sim::FuelHose::PLUGGED || h.car < 0 || h.car >= (int)sim.cars.size()) return "";
    const Car& c = sim.cars[h.car];
    char b[96];
    const char* what = h.full ? "TANK FULL" : h.broke ? "OUT OF MONEY" : "PUMPING";
    int n = std::snprintf(b, sizeof b, "%s %.1f L", what, h.pumped);
    if (h.paid > 0 || h.owed > 0) n += std::snprintf(b + n, sizeof b - n, "  $%d", h.paid);
    if (!c.remote && c.fuel >= 0) std::snprintf(b + n, sizeof b - n, "   TANK %.0f / %.0f L", c.fuel, c.spec->fuelCapacity);
    return b;
}

bool settle(Interact& ia, Career& career) {
    const bool any = ia.canPoured || ia.canFilled;
    if (career.on) {
        if (ia.canPoured) career.usePart(PART_FUEL_CAN), career.grant(repairPartKey(PART_EMPTY_CAN));
        if (ia.canFilled) career.usePart(PART_EMPTY_CAN), career.grant(repairPartKey(PART_FUEL_CAN)), career.cash -= kCanFillCost;
    }
    ia.canPoured = ia.canFilled = false;
    return any && career.on;
}

bool takeFuel(Sim& sim, const std::string& text, double& poured) {
    unsigned id = 0;
    double l = 0;
    poured = 0;
    if (text.rfind("fuel ", 0) != 0) return false;
    if (std::sscanf(text.c_str(), "fuel %u %lf", &id, &l) != 2 || !std::isfinite(l) || l <= 0 || l > 60) return true;  // (a can's worth: not whatever a buddy typed)
    for (Car& c : sim.cars)
        if (c.netId == id && !c.remote) {
            poured = std::min(l, room(c));
            if (c.fuel < 0) c.fuel = c.spec->fuelCapacity;
            c.fuel = std::min(c.spec->fuelCapacity, c.fuel + poured);
            if (room(c) <= 1e-6) sim.hose.outbox.push_back("tankfull " + std::to_string(id));
        }
    return true;
}

bool onMessage(Sim& sim, int player, const std::string& text, double now) {
    if (text.rfind("tankfull ", 0) == 0) {
        const int i = carOf(sim, (uint32_t)std::strtoul(text.c_str() + 9, nullptr, 10));
        if (i >= 0 && sim.hose.state == Sim::FuelHose::PLUGGED && sim.hose.car == i) sim.hose.full = true;
        return true;
    }
    if (text.rfind("hose ", 0) != 0) return false;
    auto& v = sim.remoteHoses;
    auto it = std::find_if(v.begin(), v.end(), [&](const Sim::RemoteHose& r) { return r.player == player; });
    V3 p;
    unsigned id = 0;
    if (text == "hose -" || std::sscanf(text.c_str(), "hose %lf %lf %lf %u", &p.x, &p.y, &p.z, &id) != 4 || !std::isfinite(p.x + p.y + p.z)) {
        if (it != v.end()) v.erase(it);
        return true;
    }
    if (it == v.end()) v.push_back({}), it = v.end() - 1;
    it->player = player, it->pump = p, it->carId = id, it->heard = now;
    return true;
}

void updateRemote(Sim& sim, double now, const std::function<bool(int, V3&)>& handOf) {
    auto& v = sim.remoteHoses;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Sim::RemoteHose& r) { return now - r.heard > kHoseExpiry; }), v.end());
    for (Sim::RemoteHose& r : v) {
        const int i = r.carId ? carOf(sim, r.carId) : -1;
        r.shown = true;
        if (i >= 0) r.end = filler(sim.cars[i]);
        else r.shown = handOf(r.player, r.end);
    }
}

}  // namespace fuel
