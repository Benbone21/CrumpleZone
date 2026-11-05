// Fuel by hand: the fuel can and the pumps.
//
// THE CAN. A FUEL CAN is a repair part as far as your inventory is concerned (fixes.h PART_FUEL_CAN: career stock, sandbox's endless
// supply, the back of the truck, a rack at a station, a find in caches and wrecks). Take it from the back, aim at a vehicle's filler and hold
// E: it pours in whole, up to kCanLiters (what the tank can't take is spilled). The can stays with you, empty (PART_EMPTY_CAN), until you hold E at a
// pump with it in your hand. Sandbox's can is free and never runs out. (Car::jerry, the spare can some trucks carry, is still poured with
// the F key.)
//
// THE PUMP. Any fuel_pump model is a working pump. On foot, aim at it and press E: the hose comes into your hand, running back to the
// pump. Walk it to a vehicle, aim at the filler and press E: it's plugged in, and fuel flows on its own (Sim::FuelHose, fuel::step), the
// gauge and the cost on the HUD, until the tank's full or you unplug it (E again) with the money you have. Hang it back on the pump (E), or
// walk too far and it springs back. Drive off with it in and it pops out: the hose is never tied to the physics.
//
// MULTIPLAYER: short text over the session's chatter, no protocol change. "fuel CAR LITERS" (it was already there) goes to the owner of a
// vehicle that's being filled, a litre or so at a time, "tankfull CAR" comes back when it won't take any more, and "hose X Y Z CAR" /
// "hose -" tell the others where your hose runs (when it changes and every kHoseRefresh s; unheard for kHoseExpiry s, it's gone).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "mathx.h"
#include "softbody.h"

struct Walker;
struct Interact;
struct Career;
class World;

namespace fuel {

constexpr double kCanLiters = 20;     // L in a full can
constexpr double kFlow = 5;           // L/s out of a pump
constexpr int kPricePerLiter = 2;     // $ (a career; sandbox is free)
constexpr int kCanFillCost = 40;      // $ to fill a can at a pump
constexpr double kHoseLen = 6.5;      // m of hose
constexpr double kHoseSlack = 1.5;    // ...walk this much further and it springs back
constexpr double kPopAt = 1.2;        // m the filler can move, plugged in, before the hose pops out
constexpr double kHoseRefresh = 2.0, kHoseExpiry = 6.0;

// Where you put fuel in `c`, world (on its side, or on top, by the vehicle), and what the tank can still take (remote vehicle: unknown, 1e9).
V3 filler(const Car& c);
V3 fillerRest(const Car& c);
double room(const Car& c);
// Every pump's hose in `w`, world.
void findPumps(const World& w, std::vector<V3>& out);

// Once a frame, on foot or not: the hose's flow, and what ends it. `wk` null: not on foot. `cash`: a career's money (null: sandbox, free).
// Returns what to announce. Messages for the others go out through sim.hose.outbox.
std::string step(Sim& sim, const Walker* wk, double dt, int* cash);
// The HUD's line while the hose is plugged in ("" if not): "PUMPING 23.4 L  $47  TANK 61 / 79 L".
std::string gauge(const Sim& sim);
// The can jobs' effect on your stock (Interact::canPoured, canFilled): a career's can goes empty, or full for its cost. Whether anything changed.
bool settle(Interact& ia, Career& career);
// Fuel poured into `t`, a vehicle of yours or a buddy's. Returns what to announce.
std::string pour(Sim& sim, int car, double liters);

// A "fuel CAR LITERS" for one of your vehicles: tops it up. True if it's one of those; `who`: announce with their name; `poured`: what went in.
bool takeFuel(Sim& sim, const std::string& text, double& poured);
// A "hose ..." or "tankfull ..." chatter line from `player`: true if it was one.
bool onMessage(Sim& sim, int player, const std::string& text, double now);
// Once a frame: the others' hoses, from the pump to the filler or to where `handOf` says their hand is (false: out of sight); the unheard forgotten.
void updateRemote(Sim& sim, double now, const std::function<bool(int player, V3& hand)>& handOf);

}  // namespace fuel
