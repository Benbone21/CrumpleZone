// Multiplayer on the procedural world: how far apart the players may get.
//
// A game has one window of land (procworld.h), 3072 m a side, that the host slides for everyone. Past its edge there is no land: the
// edge row's heights stretched out, no rocks, no trees, no grid. So the players have to fit in it, and this keeps them there. The
// host (or the dedicated server) is the referee; a guest only shows what it's told and goes where it's sent. Nothing here runs on a
// fixed map or in single player.
//
//   past kWarn:   a banner, with the distance
//   past kLimit:  the banner counts down kCountdown seconds; turning back cancels it
//   at the end:   brought back beside the group, on real land, damage and all, and a banner says so (at kHard, sooner: see below)
//
// Distances are the window's: the longer of the two axes between two players (it's a square), not the straight line.
//
// Why these numbers. Everyone sits inside the window while the box round them, at most kHard wide, is centred in it, within what a
// slide lags by. A slide is told kLead ahead and goes in on time on every machine, so the window is one lead late at most; it's aimed
// at where the players will be by then (the lead cancels for a steady convoy), and it's asked for as soon as a player is kEdge out
// from the middle, not only when the middle has moved kSlide (from kSlide, 512, the window would give up 2 x 512 m of the 3072).
//   the group at kLimit:         1100 m out + 32 (a chunk's rounding) + 3 s at 68 m/s = 1336: homeSpot's 200 m margin kept at 245 km/h
//   one player leaving at kHard: 1300 m out + 32 + 3 s at half of 130 m/s = 1527: on the land at 470 km/h
// (net/mp-distance-rules runs these.)
// (Two players leaving each other don't move the middle at all.)
//
// Who's too far: the biggest set of players that fits in a kLimit square; everyone else is. On a host's game the set has to have the
// host in it, so guests are measured against the host; on the dedicated server one straggler can't take the rest with them, and of
// two sets as big, the one that was the group goes on being it. The window keeps holding a straggler until kHard (its land, for the
// countdown); past that it follows the group alone.
//
// On the wire: chatter from the host (NetSession::sendGame), so kVersion stays and an older guest just doesn't hear it (it's left
// behind, as before, and the window goes on without it):
//   mpdist <peer> w <distance m> <limit m> <seconds left, -1: not counting>     every second while it's so
//   mpdist <peer> ok                                                            back inside
//   mpdist <peer> back <car id> <global x> <global z> <yaw>                     come back here (global: it holds across a slide)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mathx.h"
#include "net.h"

class Sim;

namespace mpdist {

constexpr double kWarn = 1900, kLimit = 2200, kHard = 2600;  // m
constexpr double kWarnClear = kWarn - 100, kLimitClear = kLimit - 100;  // m: what counts as having turned back
constexpr double kCountdown = 6;   // s from past kLimit to being brought back
constexpr double kMinWarned = 2;   // s: at kHard it's now, but never without the banner up this long first
constexpr double kJoinGrace = 8;   // s a new player is left alone (the spawn logic has placed them; their first states are coming)
constexpr double kCool = 8;        // s after being sent back: the guest's move has to get here; nothing is said or done again meanwhile
constexpr double kLead = 3;        // s a slide is told ahead (main.cpp, server_main.cpp)
constexpr double kEdge = 1200;     // m from the window's middle: a player out here asks for a slide...
constexpr double kEdgeMove = 128;  // m: ...if it'd move the window at least this (so a group at rest never slides for nothing)
constexpr double kLeadSpeed = 150; // m/s: the most a state's speed is believed

// A player where they are. peer: their id (the host's own player is 0); car: the vehicle that goes back with them (-1: none).
struct Body {
    int peer = 0;
    V3 p, vel;
    int car = -1;
};

double apart(const V3& a, const V3& b);  // the window's distance
// The peers (bits) in the biggest set that fits in a kLimit square. `must`: a peer it has to have (-1: none). Of sets as big: the
// most of `was` (the last group), then the nearest the window's middle.
uint32_t groupOf(const std::vector<Body>& b, int must, uint32_t was);
// How far each is from the farthest of the group (bar themselves): over kLimit only for someone outside it.
std::vector<double> distances(const std::vector<Body>& b, uint32_t group);
// Where the window should go for these players (local). edge: a slide is wanted now, whatever kSlide says. False: nobody.
bool focusOf(const std::vector<Body>& b, V3& focus, bool& edge);

// The host's side. update() every frame on the procedural map; hear() every chatter message.
class Referee {
public:
    void hear(const Sim& sim, const NetSession& net, const NetSession::GameMsg& m, double now);  // (where players on foot are)
    // me: where the host's own player is (null: a dedicated server, there's no one). `now`: seconds, any steady clock.
    void update(Sim& sim, NetSession& net, double now, const V3* me);
    // What update() made of it: where the window should go (see focusOf).
    bool focus(V3& f, bool& edge) const { return f = focus_, edge = edge_, have_; }
    std::vector<std::string> events;  // for the HUD or the server's log
    uint32_t group() const { return group_; }

private:
    enum Phase { CLEAR, WARNED, COUNTING, COOLING };
    struct Seat {
        Phase phase = CLEAR;
        double seen = -1;      // first seen (-1: not here)
        double since = 0;      // the banner's been up since
        double due = 0;        // COUNTING: when; COOLING: until
        double said = -1e9;    // told last
        double walkAt = -1e9;  // on foot: heard then, here (global)
        V3 walk;
    };
    Seat seats_[net::kMaxPeers];
    uint32_t group_ = 0;
    V3 focus_;
    bool have_ = false, edge_ = false;
    std::vector<Body> bodies(const Sim& sim, const NetSession& net, double now, const V3* me) const;
    void sendBack(Sim& sim, NetSession& net, const std::vector<Body>& b, size_t who, double now);
};

// Where someone comes back to: a clear spot of their own round the middle of the group, else round the station nearest it, else the
// window's spawn; in the window, homeSpot's margin in from its edge.
void returnSpot(const Sim& sim, const std::vector<Body>& b, uint32_t group, size_t who, V3& p, double& yaw);

// A guest's side. hear() every chatter message; apply() and tick() every frame.
class Guest {
public:
    // True: it was one of these (a guest saying it is ignored: only the host's count). Lines to someone else's vehicle that's being
    // taken away go now, here (they'd stretch km).
    bool hear(Sim& sim, const NetSession& net, const NetSession::GameMsg& m);
    // Goes where the host said, if it did (after a slide on its way: the spot's in the land it brings). True: moved, the car (index) in
    // `car`, for the game to bring what it holds along (you on foot, the camera).
    bool apply(Sim& sim, const NetSession& net, int& car);
    void tick(double dt);
    void reset() { *this = Guest(); }
    std::string banner() const;  // what to show ("": nothing)
    std::vector<std::string> events;  // for the HUD
    double seconds() const { return secs_; }  // (the tests)

private:
    bool warned_ = false, sent_ = false;
    double dist_ = 0, limit_ = 0, secs_ = -1, quiet_ = 0, done_ = 0;
    V3 to_;
    double yaw_ = 0;
    uint32_t car_ = 0;
};

}  // namespace mpdist
