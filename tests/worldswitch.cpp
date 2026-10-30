// Switching worlds (a map, a level, the editor, the menu's camp): nothing of the last world's comes along. The vehicle cap counts
// only this world's vehicles, and switching back and forth leaves the same sim behind each time.
#include <cmath>

#include "harness.h"

namespace {
// Parks vehicles ahead of the player until the cap says no; returns how many went out.
int fillToCap(Sim& sim) {
    const auto& lv = listedVehicles();
    int n = 0;
    while (n < 64 && sim.spawnAhead(vehicleCatalog()[lv[n % lv.size()]])) n++;
    return n;
}

// Leaves something of everything lying about: wheels off, a part dropped, a strap between two vehicles, a stake in the ground,
// cables on the batteries, you out on foot.
void litter(Sim& sim) {
    const int other = sim.player == 0 ? 1 : 0;
    sim.shedWheel(other, 0, false);
    sim.droppedParts.push_back({0, sim.cars[other].frame().com + V3(3, 0, 0), 0});
    sim.cables.push_back({sim.player, other, -1, 0});
    Rope r;
    r.a.kind = ANCHOR_CAR, r.a.car = other;
    r.b.kind = ANCHOR_CAR, r.b.car = (int)sim.cars.size() - 1;
    sim.ropes.push_back(r);
    sim.setAnchors.push_back(SetAnchor());
    sim.playerAway = true;
}

// Every vehicle is near the world's start or its parking: none is left where the last world had them.
bool allNearStart(const Sim& sim, double& worst) {
    worst = 0;
    for (const Car& c : sim.cars) {
        const V3 p = c.frame().com;
        double d = std::hypot(p.x - sim.world.spawn.x, p.z - sim.world.spawn.z);
        for (const V3& q : sim.world.parking) d = std::min(d, std::hypot(p.x - q.x, p.z - q.z));
        worst = std::max(worst, d);
    }
    return worst < 80;
}

void newWorld(Sim& sim, int map) {
    sim.world = World(map);
    sim.setup();
}
}  // namespace

SCENARIO(worldSwitchCap, "worldswitch/cap", "Vehicles spawned up to the cap on one map are gone on the next: it can be filled to the cap again, and nothing of the last world's is left lying about") {
    Sim& sim = ctx.sim;
    sim.lineup = listedVehicles();
    newWorld(sim, MAP_MOUNTAIN);
    const int lineup = (int)sim.cars.size();
    const int first = fillToCap(sim);
    ctx.check("mountain: filled to the cap", sim.vehicleCount() == Sim::kMaxVehicles, std::to_string(sim.vehicleCount()));
    litter(sim);
    ctx.check("mountain: litter is there", !sim.looseWheels.empty() && !sim.droppedParts.empty() && !sim.cables.empty() && !sim.ropes.empty());

    newWorld(sim, MAP_FRONTIER);
    double worst = 0;
    ctx.check("frontier: just its own lineup", (int)sim.cars.size() == lineup, std::to_string(sim.cars.size()) + " cars");
    ctx.check("frontier: every vehicle is at its start", allNearStart(sim, worst), std::to_string(worst) + " m");
    ctx.check("frontier: no loose wheels, parts, lines, cables or stakes",
              sim.looseWheels.empty() && sim.droppedParts.empty() && sim.ropes.empty() && sim.cables.empty() && sim.setAnchors.empty());
    ctx.check("frontier: you are in your seat", !sim.playerAway && !sim.handLine.on() && !sim.handCable.on());
    const int second = fillToCap(sim);
    ctx.check("frontier: can be filled to the cap again", second == first && sim.vehicleCount() == Sim::kMaxVehicles,
              std::to_string(second) + " spawned (mountain " + std::to_string(first) + ")");

    // Ten switches, each world filled and littered: the same sim behind each time.
    size_t cap0 = 0;
    int nodes0 = 0;
    bool steady = true;
    for (int k = 0; k < 10; k++) {
        newWorld(sim, k % 2 ? MAP_FRONTIER : MAP_MOUNTAIN);
        int nodes = 0;
        for (const Car& c : sim.cars) nodes += (int)c.nodes.size();
        if (k < 2) {
            if (k == 0) cap0 = sim.cars.capacity(), nodes0 = nodes;
        } else if ((int)sim.cars.size() != lineup || sim.cars.capacity() != cap0 || !sim.looseWheels.empty() || !sim.droppedParts.empty() || nodes != nodes0) {
            steady = false;
            ctx.check("switch " + std::to_string(k), false,
                      std::to_string(sim.cars.size()) + " cars, capacity " + std::to_string(sim.cars.capacity()) + ", nodes " + std::to_string(nodes));
        }
        if (k % 2 == 0 && fillToCap(sim) != first) steady = false, ctx.check("switch " + std::to_string(k) + ": filled to the cap", false);
        litter(sim);
    }
    ctx.check("ten switches: the same vehicles, the same memory for them each time", steady);
}

SCENARIO(worldSwitchKeep, "worldswitch/keep", "Going into a level or the editor keeps only your vehicle (and in a session the other players'): the last world's parked, spawned and AI vehicles go") {
    Sim& sim = ctx.sim;
    sim.lineup = listedVehicles();
    newWorld(sim, MAP_MOUNTAIN);
    fillToCap(sim);
    sim.player = 3;
    const VehicleSpec* mine = sim.cars[3].spec;
    sim.cars[5].remote = true, sim.cars[5].netId = (1u << 16) | 7;  // (another player's, in a session)
    litter(sim);
    {
        Sim s2;  // (the same thing offline: a copy, without the remote)
        s2.lineup = sim.lineup;
        s2.world = World(MAP_MOUNTAIN);
        s2.setup();
        fillToCap(s2);
        s2.player = 2;
        litter(s2);
        const VehicleSpec* m2 = s2.cars[2].spec;
        s2.clearWorldLeftovers(s2.player);
        ctx.check("offline: just you", s2.cars.size() == 1 && s2.player == 0 && s2.cars[0].spec == m2, std::to_string(s2.cars.size()) + " cars");
        ctx.check("offline: nothing lying about", s2.looseWheels.empty() && s2.droppedParts.empty() && s2.ropes.empty() && s2.cables.empty() && !s2.playerAway);
        ctx.check("offline: the cap is free again", fillToCap(s2) == Sim::kMaxVehicles - 1);
    }
    sim.clearWorldLeftovers(sim.player, true);
    int remotes = 0;
    for (const Car& c : sim.cars) remotes += c.remote;
    ctx.check("session: you and the other player", sim.cars.size() == 2 && remotes == 1 && sim.cars[sim.player].spec == mine && !sim.cars[sim.player].remote,
              std::to_string(sim.cars.size()) + " cars, " + std::to_string(remotes) + " remote");
    ctx.check("session: nothing lying about", sim.looseWheels.empty() && sim.droppedParts.empty() && sim.setAnchors.empty() && !sim.playerAway);
}
