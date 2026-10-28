// The dev FREE CAM (devmenu.h FREE_CAM, dev key F8): the drone camera made a dev tool. It flies anywhere
// (through the ground too, if asked), in speed steps for crossing kilometres, with the world streamed in
// round it; it picks out the vehicle under the crosshair, carries vehicles about and sets them down, and
// spawns new ones where it looks. No SDL or GL in here (the test runner links this file): main.cpp has the
// keys, the HUD and the camera; this has what those do to the sim.
//
// Carrying is rigid: on pickup the vehicle is turned upright (its heading kept) and its shape is taken, node
// by node, in its own frame. While held, every frame puts each node back at that shape round the hang point,
// at rest, the chassis frames turned with it, so nothing in it is stretched or wound up (no explosion when it
// is let go). Set down, it goes through Sim::teleportCar (upright, on the ground, clear of trunks).
#pragma once
#include <vector>

#include "mathx.h"

class Sim;
class World;
class Car;
struct VehicleSpec;

namespace freecam {

// Flying speed steps (m/s), mouse wheel or - and = (Shift: three times as fast).
constexpr double kSpeeds[] = {3, 8, 20, 50, 120, 300, 800};
constexpr int kSpeedCount = (int)(sizeof kSpeeds / sizeof kSpeeds[0]);
constexpr int kDefaultSpeed = 2;
inline double speedOf(int step) { return kSpeeds[step < 0 ? 0 : step >= kSpeedCount ? kSpeedCount - 1 : step]; }

// The keys free cam reads (SDL scancodes, as pressed: free cam takes every key but the always-on ones
// below, so none of them reach the driving keys while it flies). Kept apart from each other.
enum Key : int {
    K_FORWARD = 26, K_BACK = 22, K_LEFT = 4, K_RIGHT = 7,  // W S A D
    K_DOWN = 20, K_UP = 8,                                 // Q E
    K_GRAB = 10,          // G: pick up the vehicle under the crosshair / set the carried one down at the crosshair
    K_LET_GO = 9,         // F: let go of it where it hangs (it falls)
    K_TURN = 21,          // R: turn the carried vehicle 15 degrees (Shift: the other way)
    K_SPAWN = 5,          // B: the spawn picker
    K_DRIVE = 25,         // V: drive the carried or targeted vehicle
    K_THROUGH = 27,       // X: fly through the ground on / off
    K_SLOWER = 45, K_FASTER = 46,  // - =
    K_EXIT = 41,          // Esc (and the FREE CAM key itself)
};
// Every key free cam answers, for the clash test.
const std::vector<int>& keys();
// Keys free cam leaves to the game while it flies (the dev menu, the key mode, debug info, reload, photo, fullscreen).
bool passesThrough(int scancode);

// The vehicle (not a prop) the ray from `eye` along `dir` (unit) first hits, by its bounding box, within maxDist
// (-1: none). `skip`: one to look through (the carried one). `at`: where along the ray it hit.
int pickVehicle(const Sim& sim, const V3& eye, const V3& dir, double maxDist, int skip = -1, double* at = nullptr);
// Where the ray first meets the ground (the terrain), within maxDist. False: it doesn't (the sky).
bool groundHit(const World& w, const V3& eye, const V3& dir, double maxDist, V3& out);

struct Carry {
    int car = -1;
    double yaw = 0;              // the heading it's held at (the game's yaw: 0 = +x)
    std::vector<V3> local;       // each node, from the center of mass, in its own upright frame
    std::vector<char> torn;      // panels torn off it: left lying where they are
    Quat railRel[2];             // its ladder frame rails' and knuckles' turn against its body's
    std::vector<Quat> knuckleRel;
    const void* spec = nullptr;  // (sanity: the same vehicle still at that index)
    size_t nodes = 0;
    bool on() const { return car >= 0; }
};
// Can this one be carried (a vehicle, not a prop, not another player's)?
bool carriable(const Sim& sim, int car);
// Picks it up: upright, its heading kept, its shape taken, at rest. False if it can't be.
bool pickUp(Sim& sim, int car, Carry& cr);
// Still the vehicle that was picked up (the cars list may have changed under it)?
bool valid(const Sim& sim, const Carry& cr);
// Each frame while held: the whole vehicle rigidly at center of mass `at`, heading cr.yaw, at rest.
void hold(Sim& sim, const Carry& cr, const V3& at);
// Where it hangs: `ahead` m in front of the eye along the flat look direction and `below` m under it.
V3 hangPoint(const V3& eye, double lookYaw, double ahead = 9, double below = 3);
// Sets it down on its wheels at (x, z), heading cr.yaw, clear of trunks; the carry ends.
void dropAt(Sim& sim, Carry& cr, double x, double z);
// Lets go where it hangs: at rest, it falls from there; the carry ends.
void letGo(Sim& sim, Carry& cr);

// A new vehicle set down at (x, z) facing yaw, its handbrake on: its index, or -1 when the vehicle cap is reached.
int spawnAt(Sim& sim, const VehicleSpec& vs, double x, double z, double yaw);

}  // namespace freecam
