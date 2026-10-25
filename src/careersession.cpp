#include "careersession.h"

#include <fstream>

#include "carsave.h"
#include "mods.h"
#include "softbody.h"
#include "vehicles.h"

namespace careersession {

namespace {

bool exists(const std::string& path) { return (bool)std::ifstream(path); }

// The first vehicle a career can start in (the catalog's order).
int firstStarter() {
    for (int v : listedVehicles())
        if (startableVehicle(v)) return v;
    return 0;
}

// Where the n-th factory-fresh vehicle (no saved place) goes: the spawn, then the parking.
void freshSpot(const Sim& sim, int n, V3& at, double& yaw) {
    const World& w = sim.world;
    const V3 p = n == 0 || w.parking.empty() ? w.spawn : w.parking[(size_t)(n - 1) % w.parking.size()];
    yaw = n == 0 || w.parking.empty() ? w.spawnYaw : w.parkingYaw;
    at = V3(p.x, w.map == MAP_PROVING_GROUND ? 0.02 : w.clearY(p.x, p.z) + 0.05, p.z);  // (as Sim::setup puts them)
}

// One saved vehicle into the sim: built as the condition says (else as `fallback`, stock), and put back where and how
// it was left; factory fresh at the next free spot when it has no saved place. Returns its index in sim.cars.
int place(Sim& sim, int slot, const std::string& condition, int fallback, ModSet mods, int& freshCount) {
    int vehicle = carsave::vehicleOf(condition);
    if (vehicle >= 0) mods = carsave::modsOf(condition);
    else {  // (no condition yet, or one for a vehicle the catalog no longer has: this one, stock, at the saved place)
        vehicle = fallback >= 0 && fallback < (int)vehicleCatalog().size() ? fallback : firstStarter();
        if (!condition.empty()) mods = ModSet();
    }
    V3 at;
    double yaw;
    freshSpot(sim, freshCount, at, yaw);
    sim.addCar(tunedSpec(vehicle, mods), at, yaw, sim.nextColor());
    const int ci = (int)sim.cars.size() - 1;
    Car& c = sim.cars[ci];
    c.careerSlot = slot;
    c.handbrake = 1;
    const V3 built = c.frame().com;
    carsave::restore(sim, c, condition);  // (false: left as built, at the saved place if the text has one)
    if (len(c.frame().com - built) < 1e-9) freshCount++;  // (it took the spot)
    // The wheels lying about aren't in the save: one that was off its hub (taken off, shed, the tire rolled off the rim)
    // is back on it, so the vehicle can be driven away.
    for (int k = 0; k < (int)c.wheels.size(); k++) {
        Wheel& wh = c.wheels[k];
        if (wh.detached) continue;
        WheelItem it = wh.item;
        it.popped = false;
        if (!wh.mounted) it.tireOnly = false, sim.fitWheel(ci, k, it);
        else if (wh.debeaded) it.tireOnly = true, sim.fitWheel(ci, k, it);
    }
    return ci;
}

}  // namespace

void begin(CareerSession& s, Sim& sim, const WorldSave& w, const std::string& playerName, int startVehicle) {
    s.world = w;
    s.player = playerName;
    s.sinceSave = 0;
    // (Only the old career.txt: its progress comes along, nothing lost. Nothing at all: a fresh start at this world's difficulty.)
    const bool migrating = !exists(Career::profilePath()) && !exists(Career::profilePath() + ".bak") && exists(Career::defaultPath());
    if (!s.profile.loadProfile()) s.profile.startFresh(w.diff);
    else if (startVehicle >= 0 && s.profile.vehicles.empty() && !migrating) s.profile.startFresh(w.diff);
    s.profile.on = true;
    s.active = true;

    // Out with what the map put out (the lineup): a career world has the vehicles you have.
    for (int i = (int)sim.cars.size() - 1; i >= 0; i--)
        if (!sim.cars[i].isProp() && !sim.cars[i].remote) {
            sim.carRemoved(i);
            sim.cars.erase(sim.cars.begin() + i);
            if (sim.player > i) sim.player--;
        }
    sim.playerAway = false;

    if (startVehicle >= 0) {
        OwnedVehicle ov;
        ov.vehicle = startVehicle;
        ov.world = w.slug;
        s.profile.vehicles.push_back(ov);
    }
    int freshCount = 0, you = -1;
    double latest = -2;
    for (int k = 0; k < (int)s.profile.vehicles.size(); k++) {
        const OwnedVehicle& ov = s.profile.vehicles[k];
        if (ov.world != w.slug) continue;
        const int ci = place(sim, k, ov.condition, ov.vehicle, ov.mods, freshCount);
        const double when = startVehicle >= 0 && k + 1 == (int)s.profile.vehicles.size() ? 1e300 : ov.drove;  // (a new world: you start in the new one)
        if (when > latest) latest = when, you = ci;
    }
    if (w.mode == MODE_CREW)
        for (int k = 0; k < (int)s.world.left.size(); k++) {
            const LeftVehicle& lv = s.world.left[k];
            place(sim, -2 - k, lv.condition, lv.vehicle, ModSet(), freshCount);
        }
    if (you < 0) {  // (nothing of yours here: a starter, so you aren't left on foot with nothing)
        OwnedVehicle ov;
        ov.vehicle = firstStarter();
        ov.world = w.slug;
        s.profile.vehicles.push_back(ov);
        you = place(sim, (int)s.profile.vehicles.size() - 1, "", ov.vehicle, ModSet(), freshCount);
    }
    sim.player = you;
    sim.cars[you].handbrake = 0;
    sim.cars[you].wake();
}

void capture(CareerSession& s, const Sim& sim) {
    if (!s.active) return;
    for (int ci = 0; ci < (int)sim.cars.size(); ci++) {
        const Car& c = sim.cars[ci];
        if (c.remote || c.careerSlot == -1) continue;
        const std::string text = carsave::capture(sim, c);
        if (text.empty()) continue;
        if (c.careerSlot >= 0 && c.careerSlot < (int)s.profile.vehicles.size()) {
            OwnedVehicle& ov = s.profile.vehicles[c.careerSlot];
            ov.condition = text;
            ov.vehicle = carsave::vehicleOf(text);
            ov.mods = carsave::modsOf(text);
            ov.world = s.world.slug;
            if (ci == sim.player) ov.drove = s.world.playedSeconds;
        } else if (c.careerSlot <= -2 && -2 - c.careerSlot < (int)s.world.left.size()) {
            LeftVehicle& lv = s.world.left[-2 - c.careerSlot];
            lv.condition = text;
            lv.vehicle = carsave::vehicleOf(text);
        }
    }
}

namespace {
void saveBoth(CareerSession& s, const Sim& sim) {
    capture(s, sim);
    s.profile.saveProfile();
    saveWorld(s.world);
    s.sinceSave = 0;
}
}  // namespace

bool tick(CareerSession& s, const Sim& sim, double dt) {
    if (!s.active) return false;
    s.world.playedSeconds += dt;
    s.sinceSave += dt;
    if (s.sinceSave < CareerSession::kAutosaveEvery) return false;
    saveBoth(s, sim);
    return true;
}

void end(CareerSession& s, const Sim& sim) {
    if (!s.active) return;
    saveBoth(s, sim);
    s.active = false;
}

}  // namespace careersession
