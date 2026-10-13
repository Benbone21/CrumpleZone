// Autopilot: drives a vehicle to a destination you pick, at the speed you pick, on roads
// only, on roads and trails, or anywhere it can get through.
//
//  1. Route   A* over the map on a 2 m grid. Each step costs its length, more on rough or
//             loose ground, steep grades and side slopes, and over rocks, logs and ledges
//             in proportion to how much of what the vehicle can climb they take. Too steep,
//             too tall to climb (for this vehicle, in its lowest gear), trees, walls and
//             deep water are off limits. Roads-only and trails modes make leaving the
//             track expensive rather than impossible.
//  2. See     Ten times a second it scans the ground ahead (pilotsense.h: exact heights
//             of the terrain, boulders, ledges, logs, trees and other vehicles) and tries
//             ~50 lines: hold a steering angle for a while, then head back to the route.
//             Each line is driven in its head a quarter meter at a time: where every tire
//             sits and what it has to climb or drop off, whether anything under the body
//             touches (diffs, belly, bumpers), how far the truck leans and pitches. Lines
//             that hit something, tip it past a safe angle or need a ledge taller than it
//             can climb are out; the rest are scored on how far they stray from the
//             route, how slowly it would have to crawl, and how rough they are. So a rock
//             it can easily clear is straddled or climbed, and one that would mean
//             crawling or scraping is driven around if there's room.
//  3. Setup   It picks the transfer case position for what's ahead (2H on pavement, 4WD
//             on dirt, the center locked on loose or steep ground, low range for ledges,
//             rock and steep grades), stopping to shift into or out of low. Lockers go in
//             for ledges, side hills and wheel spin and come out for tight turns and
//             speed; the sway bar comes off for crawling; before a long rocky stretch it
//             stops and airs the tires down.
//  4. Drive   Pure pursuit along the chosen line; speed slowed for bends, descents,
//             ledges, scrapes and lean, and the throttle leaned on when a tire is up
//             against a ledge.
//  5. Unstick No progress for a few seconds: more drive (lower range, lockers), back up and
//             try again, and remember that this setup couldn't climb that; stuck at the
//             same spot twice, plan a new route around it.
#pragma once
#include <string>
#include <vector>

#include "mathx.h"

class World;
class Car;
struct VehicleSpec;

enum PilotRoute { PILOT_ROADS, PILOT_TRAILS, PILOT_ANYWHERE };
const char* pilotRouteName(int route);

// Something the autopilot noticed ahead, and what it's doing about it.
enum PilotVerdict { VERDICT_PASSING, VERDICT_OVER, VERDICT_STRADDLE, VERDICT_AROUND_LEFT, VERDICT_AROUND_RIGHT, VERDICT_BLOCKING };
struct PilotSeen {
    V3 at;           // top of it (world)
    double height;   // above the ground (m)
    int kind;        // SeenKind
    int verdict;
};

// The vehicle as the autopilot sees itself (from its build): wheels and underbody.
struct PilotBody {
    int build = -1;              // the car's buildCount it was taken from
    double R = 0.4, width = 0.3; // tire radius and tread width
    double wb = 2.5, trackF = 1.6, trackR = 1.6;
    double front = 3.5, back = -1;  // body's reach ahead of / behind the rear axle
    double rollMax = 0.5;        // lean it may take (rad): well short of tipping over
    struct Under {
        float x, z, c;           // from the rear axle center (x forward, z left), clearance (m)
    };
    std::vector<Under> under;
};

struct Autopilot {
    bool active = false;
    V3 goal;
    std::string goalName;
    double speed = 30 / 3.6;  // m/s the driver asked for
    int route = PILOT_TRAILS;
    int followCar = -1;       // >= 0: follow that vehicle (the goal moves with it)

    std::vector<V3> path;     // planned route, ~2 m apart
    int idx = 0;              // where along it the vehicle is
    std::vector<V3> avoid;    // places it got stuck: planned around next time
    std::string status;       // for the HUD
    std::string thought;      // what it sees and is doing about it ("ROCK 0.6 M: AROUND LEFT")
    std::vector<PilotSeen> seen;  // what it's noticed ahead
    std::vector<V3> line;     // the line it's taking (the next few meters)
    std::vector<std::vector<V3>> options;  // other lines it tried
    std::vector<uint8_t> optionOk;         // ... and whether each was passable

    // Internal state.
    double stuckTime = 0, backTime = 0, lowTime = 0, replanTime = 0;
    V3 lastPos;
    int stuckCount = 0;
    double backSteer = 0;
    bool arrived = false;
    PilotBody body;
    double senseTime = 0;     // s until the next look ahead
    double kappa = 0;         // chosen line: steering curvature (1/m) ...
    bool onRoute = true;      // ... or just follow the route (pure pursuit)
    double lineSpeed = 1e9;   // fastest it can take the chosen line from here (m/s)
    int needLevel = 0;        // drive setup the line needs, and where (m ahead)
    double needDist = 1e9, needStep = 0;
    bool needAir = false;     // a ledge ahead needs the tires aired down
    double lineRoll = 0;      // most the chosen line leans the truck (rad)
    double capScale[4] = {1, 1, 1, 1};  // what each setup turned out to climb, vs the table
    int wantLevel = 0;
    double easyTime = 0, lockTime[2] = {0, 0}, slipTime[2] = {0, 0};
    double push = 0;          // extra throttle while a tire is up against a ledge
    double spinCap = 1;       // crawling: the most throttle it gives before the tires start to spin
    bool holdFirst = false;   // crawling in low range: the gearbox stays in first
    double airTarget = 0;     // psi it's letting the tires down to (0 = not)
    bool tcWas = true;        // traction control setting to put back
    bool blocked = false;     // nothing ahead it can take
};

// Plans a route from `from` to `to` for this vehicle; false if there's no way through.
// `capScale` scales what it reckons the vehicle can climb (after it's failed to).
bool planRoute(const World& w, const VehicleSpec& vs, const V3& from, const V3& to, int route,
               const std::vector<V3>& avoid, std::vector<V3>& out, double capScale = 1.0);
// Starts the autopilot on `car` towards `goal` (plans the route). Returns a message.
std::string pilotStart(Car& car, const World& w, const V3& goal, const std::string& name, double speed, int route);
// One frame of driving: sets the car's controls. `cars` (optional) are the vehicles it
// should see and keep clear of. Returns false once it's done.
bool pilotDrive(Car& car, const World& w, double dt, const std::vector<Car>* cars = nullptr);
// The tallest ledge (m) the car could climb now, and set up as well as it can be.
double pilotCanClimb(const Car& car, bool setUp);
