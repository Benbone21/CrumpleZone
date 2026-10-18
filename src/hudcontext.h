// Which HUD pieces show right now (the contextual HUD), with no GL: main.cpp feeds in what the
// game looks like this frame (a Sample) and draws each element at alpha(e). The rules are in
// docs/plans/ui-redesign.md. An element is on when its standing condition holds, or for a few seconds
// after something it reports changed, or when the player asked for the whole HUD (the setting, or
// the held key). Fades are short and smoothed, so nothing pops. The compass, the speedometer,
// faults (warning pills, OUT OF FUEL) and anything in use (winch, job, prompts) are not here: always on.
#pragma once
#include <algorithm>
#include <cmath>

namespace hudctx {

enum Elem {
    E_FUEL,       // the fuel gauge
    E_TERRAIN,    // the surface and tire tag
    E_ANCHORS,    // the inventory line (anchor gear)
    E_DAMAGE,     // the vehicle card's damage bar
    E_CARINFO,    // the vehicle card's weight and mod lines
    E_PRESSURE,   // tire pressure (and SWAY OFF) over the schematic
    E_DRIVELINE,  // the 4x4 schematic
    E_LAPS,       // the proving ground's lap timer (while a lap runs; a lap done or voided keeps it up a few seconds)
    E_TIRETEMP,   // the tire-temperature marks in the dial (blue cold, green in the window, red hot)
    E_TIRESPEED,  // TIRES OVER SPEED RATING: while a tire is past its rating (blowouts possible)
    E_COUNT
};

constexpr float kFadeIn = 0.15f;    // seconds to full
constexpr float kFadeOut = 0.7f;    // seconds to gone
constexpr float kHoldSecs = 4.0f;   // how long a change keeps an element up
constexpr float kDamageSecs = 5.0f;
constexpr float kLowFuel = 0.25f;   // fraction of the tank
constexpr float kStopMs = 0.4f, kMoveMs = 0.8f;  // "stopped" below the first, "moving" again above the second (m/s)
constexpr float kBadDamage = 0.5f;  // the bar's own scale (0..1): at this or above, it stays up
constexpr float kTireLoss = 0.06f;  // tires this far off their grip (cold or hot) keep their marks up

// What the HUD knows this frame.
struct Sample {
    int carId = 0;                  // a different vehicle: everything is new
    float fuelFrac = -1;            // 0..1; < 0: no gauge for this vehicle
    float fuelL = 0, jerryL = 0;    // litres in the tank and in the can (a rise: refuelling)
    bool outOfFuel = false;
    float speed = 0;                // m/s
    int surface = -1;               // what most tires are on; -1: airborne or on foot
    bool sunk = false, clogged = false;
    bool nearTruck = false, trunkOpen = false, ropeOn = false;  // anchors: on foot by it, its inventory, a line out
    int anchorSig = 0;              // a number that changes with the anchor counts
    float damage = 0;               // the bar's 0..1
    float psi = 0, streetPsi = 0;
    bool airing = false;            // an air key is down
    bool lapOn = false;             // a lap is being timed
    int tireBand = 0;               // the tires' temperature (tiretemp.h): -1 cold, 0 in the window, 1 hot (the worst)
    float tireLoss = 0;             // the most grip any of them has lost to it (0..1)
    bool hasDriveline = false;      // trucks with a transfer case or lockers
    int mode = 0;                   // drive mode
    float tireFrac = 0;             // the fastest tire's road speed over its rating (1 = at it)
    bool lowRange = false, lockF = false, lockR = false, lockC = false, sway = false;
};

struct Context {
    bool settingFull = false;  // SHOW FULL HUD: ALWAYS
    bool holdFull = false;     // the held key
    bool stopped = true;       // (with hysteresis)
    bool havePrev = false;
    Sample prev;
    float timer[E_COUNT] = {0};
    float level[E_COUNT] = {0};  // linear 0..1; alpha() smooths it

    bool full() const { return settingFull || holdFull; }
    float alpha(int e) const {
        const float t = std::clamp(level[e], 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    }
    // The standing condition of each element (true: stays up).
    bool standing(int e, const Sample& s) const {
        switch (e) {
            case E_FUEL: return s.fuelFrac >= 0 && (s.outOfFuel || s.fuelFrac < kLowFuel || stopped);
            case E_TERRAIN: return s.sunk || s.clogged;
            case E_ANCHORS: return s.nearTruck || s.trunkOpen || s.ropeOn;
            case E_DAMAGE: return s.damage >= kBadDamage;
            case E_CARINFO: return stopped;
            case E_PRESSURE: return s.airing || std::fabs(s.psi - s.streetPsi) > 1.0f;
            case E_LAPS: return s.lapOn;
            case E_TIRETEMP: return s.tireLoss > kTireLoss;
            case E_TIRESPEED: return s.tireFrac >= 1.0f;
            case E_DRIVELINE: return s.hasDriveline && (s.lowRange || s.lockF || s.lockR || s.lockC);
        }
        return false;
    }
    void trigger(int e, float secs) { timer[e] = std::max(timer[e], secs); }
    void update(const Sample& s, float dt) {
        dt = std::max(0.0f, dt);
        if (stopped ? s.speed > kMoveMs : s.speed < kStopMs) stopped = !stopped;
        if (!havePrev || prev.carId != s.carId) {  // first frame, a new vehicle: show what's there for a moment
            trigger(E_TERRAIN, kHoldSecs);
            trigger(E_DRIVELINE, kHoldSecs);
            trigger(E_PRESSURE, kHoldSecs);
            trigger(E_ANCHORS, kHoldSecs);
            trigger(E_TIRETEMP, kHoldSecs);
            stopped = s.speed < kMoveMs;
        } else {
            if (s.fuelL > prev.fuelL + 0.3f || std::fabs(s.jerryL - prev.jerryL) > 0.3f) trigger(E_FUEL, kHoldSecs);  // refuelling
            if (s.surface != prev.surface && s.surface >= 0) trigger(E_TERRAIN, kHoldSecs);
            if (s.anchorSig != prev.anchorSig) trigger(E_ANCHORS, kHoldSecs);
            if (s.tireBand != prev.tireBand) trigger(E_TIRETEMP, kHoldSecs);
            if (s.damage > prev.damage + 0.004f) trigger(E_DAMAGE, kDamageSecs);
            if (std::fabs(s.psi - prev.psi) > 0.05f) trigger(E_PRESSURE, kHoldSecs);
            if (s.mode != prev.mode || s.lockF != prev.lockF || s.lockR != prev.lockR || s.lockC != prev.lockC || s.sway != prev.sway) {
                trigger(E_DRIVELINE, kHoldSecs);
                if (s.sway != prev.sway) trigger(E_PRESSURE, kHoldSecs);
            }
        }
        prev = s, havePrev = true;
        for (int e = 0; e < E_COUNT; e++) {
            timer[e] = std::max(0.0f, timer[e] - dt);
            const bool on = full() || timer[e] > 0 || standing(e, s);
            const float rate = on ? (holdFull ? 1.0f / 0.08f : 1.0f / kFadeIn) : 1.0f / kFadeOut;
            level[e] = std::clamp(level[e] + (on ? 1 : -1) * rate * dt, 0.0f, 1.0f);
        }
    }
};

}  // namespace hudctx
