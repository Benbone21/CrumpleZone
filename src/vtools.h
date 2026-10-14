// Vehicle tools: what to call each vehicle, taking vehicles away, and the click popup that
// sends them places. No graphics in here (the tests link it); src/vtoolsdraw.cpp draws it.
//
//  Names     Every vehicle gets a short unique label that follows it for the whole session:
//            "BRONCO 2 (RED)" (the number counts the vehicles of that model, the colour is
//            its paint), "YOU: BRONCO 1 (RED)" for the one you're in, "ALEX: BRONCO (BLUE)"
//            for another player's. Lists add how far away it is and which way:
//            "BRONCO 2 - RED - 45 M AHEAD".
//  Removing  `removeVehicle` takes one of YOUR vehicles out and fixes every index that
//            pointed past it (the player, autopilots following, lines, the job's wreck).
//            Another player's vehicle, a prop and the one you're driving are refused.
//            The multiplayer layer notices the car is gone and tells the others.
//  Popup     A click on a vehicle or the ground opens a small panel of actions. `Popup` is
//            the model: rows, keys, mouse hits and what they do to the Sim.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "jobs.h"
#include "softbody.h"

namespace vtools {

using OwnerName = std::function<std::string(uint32_t netId)>;  // another player's name

// ---- Names -----------------------------------------------------------------------------------

const char* modelName(const VehicleSpec& vs);  // "BRONCO"
std::string colourName(const V3& paint);       // "RED"
// Gives every vehicle that lacks one a uid, and a number among the vehicles of its model
// (and owner). Cheap: call it once a frame, and before asking for names.
void assign(Sim& sim);
// "BRONCO 2", "YOU: BRONCO 1", "ALEX: BRONCO" (the number only where it tells them apart).
std::string tag(const Sim& sim, int car, const OwnerName& owner);
// tag + the paint: "BRONCO 2 (RED)".
std::string name(const Sim& sim, int car, const OwnerName& owner);
// "BRONCO 2 - RED - 45 M AHEAD": for lists. `from` and `facing` (flat) say where "ahead" is.
std::string listLine(const Sim& sim, int car, const V3& from, const V3& facing, const OwnerName& owner);
std::string distanceText(double meters);
// "AHEAD", "BEHIND LEFT" ... of `facing`, seen from `from`.
std::string directionText(const V3& from, const V3& facing, const V3& to);
int indexOfUid(const Sim& sim, uint32_t uid);  // -1 if it's gone

// What a vehicle is doing: "PARKED", "AUTOPILOT: TRAILHEAD", "AI TRAFFIC", "DRIVEN BY YOU", "ANOTHER PLAYER".
std::string status(const Sim& sim, int car);

// ---- Finding vehicles ------------------------------------------------------------------------

// The vehicle a ray hits first (not props); -1 if none. Its box is grown by `slack` m so thin
// views and distant cars are still easy to hit.
int pickVehicle(const Sim& sim, const V3& origin, const V3& dir, double maxDist = 400, double slack = 0.5);
// The vehicle nearest the middle of the view within `maxAngle` radians (not props, not `skip`).
int aimedVehicle(const Sim& sim, const V3& eye, const V3& dir, int skip = -1, double maxDist = 150, double maxAngle = 0.28);

// ---- Removing --------------------------------------------------------------------------------

// Yours to remove: a local vehicle that is no prop and isn't the one you're driving.
bool canRemove(const Sim& sim, int car);
// Takes it out (and tells `job` if it was the wreck being recovered). False if refused.
bool removeVehicle(Sim& sim, Job* job, bool* jobOn, int car);
// Every vehicle of yours except the one you're driving (parked, spawned, AI traffic). Returns how many went.
int removeSpares(Sim& sim, Job* job, bool* jobOn);
int spareCount(const Sim& sim);
// Fixes the job's wreck index after cars[removed] is erased (false: the wreck itself was removed).
bool jobAfterRemoval(Job& job, int removed);

// ---- Sending vehicles ------------------------------------------------------------------------

struct Dest {
    std::string name;
    V3 p;
    int kind = 0;  // 0 a spot, 1 the nearest repair station, 4 cruise the loop road
};

enum FollowGap { GAP_CLOSE, GAP_MEDIUM, GAP_FAR, GAP_COUNT };
const char* gapName(int g);
double gapMeters(int g);

// What a vehicle is asked to do with: its own speed and strictness, or the settings' default.
double kmhFor(const Car& c, double defKmh);
int routeFor(const Car& c, double defRoute);
// Autopilot commands on one of your vehicles. Each returns what to tell the player.
std::string sendTo(Sim& sim, int car, const V3& goal, const std::string& goalName, double defKmh, int defRoute);
std::string sendToStation(Sim& sim, int car, double defKmh, int defRoute);
std::string cruiseLoop(Sim& sim, int car, double defKmh);
std::string follow(Sim& sim, int car, int leader, double defKmh, int defRoute, const OwnerName& owner, double gap = 0);
std::string stop(Sim& sim, int car);
// Where a follower aims: the leader's position, `gap` m behind it along its heading.
V3 followGoal(const Car& leader, double gap);

// ---- The popup -------------------------------------------------------------------------------

struct Row {
    enum Kind { HEADER, INFO, BUTTON, LINK, CHOICE, SLIDER, BACK, ITEM };
    Kind kind = INFO;
    std::string text, value;
    bool enabled = true;
    bool danger = false;
    float frac = 0;  // SLIDER: 0..1
    int id = 0;      // what it does (Popup::Id)
    int arg = 0;     // list items: which
};

struct Env {
    Sim& sim;
    Job* job = nullptr;
    bool* jobOn = nullptr;
    OwnerName owner;
    std::vector<Dest> dests;
    double defKmh = 30;
    int defRoute = PILOT_TRAILS;
    bool mph = false;
    V3 refPos, refFacing;  // distances are from here; "ahead" is the way it faces
    bool pad = false;      // opened with a controller: no clicking the ground
};

class Popup {
public:
    enum Kind { VEHICLE, GROUND };
    enum Key { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_ESC };
    enum Page { PG_MAIN, PG_PLACES, PG_FOLLOW, PG_VEHICLES };
    enum Id {
        ID_NONE, ID_PLACES, ID_FOLLOW, ID_SPEED, ID_ROUTE, ID_GAP, ID_GO, ID_STOP, ID_DRIVE, ID_REMOVE, ID_FOLLOW_THIS,
        ID_SEND_SEL, ID_SEND_ALL, ID_PICK_VEHICLE, ID_BACK, ID_ITEM
    };
    static constexpr int kVisible = 9;  // list rows shown at once

    bool open = false;
    Kind kind = VEHICLE;
    uint32_t target = 0;  // VEHICLE: whose
    V3 point;             // GROUND: where
    uint32_t sel = 0;     // the vehicle of yours that is being told things (0: the one you're in)
    float x = 0, y = 0;   // where it was opened (HUD pixels)
    Page page = PG_MAIN;
    int cursor = 0, scroll = 0;
    // What GO does: nothing yet / a place / follow a vehicle.
    enum Dst { D_NONE, D_PLACE, D_FOLLOW } dst = D_NONE;
    int destIdx = 0;
    uint32_t followUid = 0;
    int gap = GAP_CLOSE;
    double kmh = 30;
    int route = PILOT_TRAILS;
    bool armedRemove = false;
    // Outputs for the game to pick up.
    std::string say;          // a toast
    uint32_t pickPointFor = 0; // "HERE": the next click on the ground sends this vehicle there
    bool pickPointPending() const { return pickPointFor != 0; }

    void openOnVehicle(Env& c, int car, float px, float py);
    void openOnGround(Env& c, const V3& at, float px, float py);
    void close();
    // Rows for the current page (rebuilt from the Sim each time: names and distances are live).
    std::vector<Row> rows(const Env& c) const;
    // Keyboard / pad. Returns true if the popup used the key.
    bool key(Env& c, Key k);
    // The mouse: a click on row `index` (frac: where along a slider, 0..1), the wheel.
    void click(Env& c, int index, float frac);
    void wheel(Env& c, int dir);
    void hover(int index) { if (index >= 0) cursor = index; }
    // The vehicle it's about / that is told things, as an index (-1 gone).
    int selIndex(const Sim& sim) const;
    int targetIndex(const Sim& sim) const;

private:
    void activate(Env& c, const Row& r, float frac, bool fromKey, int dir);
    void adjustSpeed(Env& c, int dir);
    void go(Env& c);
    void clampCursor(const std::vector<Row>& rs);
    void applyToSel(Env& c);  // speed and strictness onto the selected vehicle (live)
};

}  // namespace vtools
