#include "careersession.h"

#include <cmath>
#include <fstream>

#include "carsave.h"
#include "mods.h"
#include "net.h"
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
// it was left; factory fresh at the next free spot when it has no saved place. With `there`: as it was saved (stock if
// the catalog lost it), but stood there, facing thereYaw. Returns its index in sim.cars.
int place(CareerSession& s, Sim& sim, int slot, const std::string& condition, int fallback, ModSet mods, int& freshCount,
          const V3* there = nullptr, double thereYaw = 0) {
    int vehicle = carsave::vehicleOf(condition);
    if (vehicle >= 0) mods = carsave::modsOf(condition);
    else {  // (no condition yet, or one for a vehicle the catalog no longer has: this one, stock, at the saved place)
        vehicle = fallback >= 0 && fallback < (int)vehicleCatalog().size() ? fallback : firstStarter();
        if (!condition.empty()) mods = ModSet();
    }
    V3 at = there ? *there : V3();
    double yaw = thereYaw;
    if (!there) freshSpot(sim, freshCount, at, yaw);
    sim.addCar(tunedSpec(vehicle, mods), at, yaw, sim.nextColor());
    const int ci = (int)sim.cars.size() - 1;
    Car& c = sim.cars[ci];
    c.careerSlot = slot;
    s.placed[slot] = vehicle;
    c.handbrake = 1;
    const V3 built = c.frame().com;
    if (there) {  // (its condition, with the place it was built at)
        V3 com;
        Quat rot;
        net::dentPose(c, com, rot);
        if (carsave::vehicleOf(condition) >= 0) carsave::restore(sim, c, carsave::posed(condition, com, rot));
    } else {
        carsave::restore(sim, c, condition);  // (false: left as built, at the saved place if the text has one)
        if (len(c.frame().com - built) < 1e-9) freshCount++;  // (it took the spot)
    }
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
    s.placed.clear();
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
        const int ci = place(s, sim, k, ov.condition, ov.vehicle, ov.mods, freshCount);
        const double when = startVehicle >= 0 && k + 1 == (int)s.profile.vehicles.size() ? 1e300 : ov.drove;  // (a new world: you start in the new one)
        if (when > latest) latest = when, you = ci;
    }
    if (w.mode == MODE_CREW)
        for (int k = 0; k < (int)s.world.left.size(); k++) {
            const LeftVehicle& lv = s.world.left[k];
            place(s, sim, -2 - k, lv.condition, lv.vehicle, ModSet(), freshCount);
        }
    if (you < 0) {  // (nothing of yours here: a starter, so you aren't left on foot with nothing)
        OwnedVehicle ov;
        ov.vehicle = firstStarter();
        ov.world = w.slug;
        s.profile.vehicles.push_back(ov);
        you = place(s, sim, (int)s.profile.vehicles.size() - 1, "", ov.vehicle, ModSet(), freshCount);
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
        const auto put = s.placed.find(c.careerSlot);
        if (put == s.placed.end() || !c.spec || c.spec->model != put->second) continue;
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
    if (s.write) s.profile.saveProfile(), saveWorld(s.world);
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
    s.profile.on = false;  // (no career without a world: the game goes back to sandbox)
}

bool canSwapHere(const Sim& sim) {
    if (sim.player < 0 || sim.player >= (int)sim.cars.size()) return false;
    return sim.stationAt(sim.cars[sim.player].frame().com) >= 0;  // (later: outposts and camps too)
}

int ownedOf(const CareerSession& s, const Sim& sim, int vehicle) {
    const int in = sim.player >= 0 && sim.player < (int)sim.cars.size() ? sim.cars[sim.player].careerSlot : -1;
    int any = -1;
    for (int k = 0; k < (int)s.profile.vehicles.size(); k++) {
        const OwnedVehicle& ov = s.profile.vehicles[k];
        if (k == in || ov.vehicle != vehicle) continue;
        if (ov.world == s.world.slug) return k;
        if (any < 0) any = k;
    }
    return any;
}

bool swapTo(CareerSession& s, Sim& sim, int k, std::string& why) {
    if (!s.active) return why = "NOT IN A CAREER WORLD", false;
    if (k < 0 || k >= (int)s.profile.vehicles.size()) return why = "YOU DON'T OWN THAT VEHICLE", false;
    if (sim.cars[sim.player].careerSlot == k) return why = "YOU'RE IN IT ALREADY", false;
    if (!canSwapHere(sim)) return why = "SWAP VEHICLES AT A STATION", false;
    capture(s, sim);  // (the one you're in stays parked here, as it is now; the one wanted as it was left)
    for (int i = (int)sim.cars.size() - 1; i >= 0; i--)  // (out of wherever it stands in this world)
        if (sim.cars[i].careerSlot == k && !sim.cars[i].remote && i != sim.player) {
            sim.carRemoved(i);
            sim.cars.erase(sim.cars.begin() + i);
            if (sim.player > i) sim.player--;
        }
    OwnedVehicle& ov = s.profile.vehicles[k];
    ov.world = s.world.slug;  // (from another world, or none: delivered here)
    // Beside the one you're in, facing the same way.
    Car& cur = sim.cars[sim.player];
    const Frame f = cur.frame();
    V3 side = V3(f.left.x, 0, f.left.z);
    side = len(side) > 0.1 ? norm(side) : V3(0, 0, 1);
    const int vehicle = carsave::vehicleOf(ov.condition) >= 0 ? carsave::vehicleOf(ov.condition) : ov.vehicle;
    const double width = vehicle >= 0 && vehicle < (int)vehicleCatalog().size() ? vehicleCatalog()[vehicle].width : 2.0;
    const V3 p = f.com + side * (cur.spec->width / 2 + width / 2 + 1.5);
    const V3 at(p.x, sim.world.map == MAP_PROVING_GROUND ? 0.02 : sim.world.clearY(p.x, p.z) + 0.05, p.z);
    const double yaw = std::atan2(-f.fwd.z, f.fwd.x);
    cur.handbrake = 1;
    int fresh = 0;
    const int ci = place(s, sim, k, ov.condition, ov.vehicle, ov.mods, fresh, &at, yaw);
    sim.player = ci;
    sim.cars[ci].handbrake = 0;
    sim.cars[ci].wake();
    return true;
}

WorldSave newWorldFor(const Career& profile, const std::string& name, int map, Difficulty d, WorldMode m) {
    std::vector<WorldSave> taken = listWorlds();
    for (const OwnedVehicle& ov : profile.vehicles)
        if (!ov.world.empty()) {
            WorldSave w;
            w.slug = ov.world;
            taken.push_back(w);
        }
    return newWorld(name, map, d, m, taken);
}

bool forgetWorld(Career& profile, const std::string& slug, bool write) {
    bool moved = false;
    for (OwnedVehicle& ov : profile.vehicles)
        if (ov.world == slug) ov.world.clear(), ov.drove = -1, moved = true;
    if (!write) return true;
    if (moved) profile.saveProfile();
    return deleteWorld(slug);
}

}  // namespace careersession
