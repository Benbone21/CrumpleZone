// Playing a level: the countdown, the gates, laps, the clock, medals and who's out.
//
// Each machine runs its own player's attempt; in multiplayer the progress goes out as short
// text messages (outbox) and comes back in from everyone else (onMessage), which is all the
// leaderboard needs. The host starts everyone at once ("go").
#pragma once
#include <string>
#include <vector>

#include "level.h"
#include "softbody.h"

struct RunEntry {
    int player = -1;     // multiplayer id (-1: this machine, offline)
    std::string name;
    int gates = 0;       // gates passed in all (laps included): the order on the board
    double time = -1;    // finish time (s), -1 still going
    bool out = false;
    std::string why;     // out: why
};

class LevelRun {
public:
    enum State { OFF, COUNTDOWN, RUNNING, FINISHED, OUT };
    Level level;
    State state = OFF;
    double clock = 0;      // s since the green light (negative while counting down)
    int gate = 0, lap = 0; // the next gate, the lap it's on (0-based)
    double finishTime = -1;
    std::string outWhy;
    std::vector<double> splits;  // time at each gate passed
    double best = -1;      // this machine's best finish of this level (this session)
    std::vector<RunEntry> board;
    std::vector<std::string> outbox;  // for the network: "prog N T", "fin T", "out WHY"
    std::string banner;    // big text for the HUD ("3", "GO", "LAP 2", "FINISHED 1:02.4")
    double bannerTime = 0;

    // Puts the player's car at its place on the grid (slot 0 on the start, then pairs
    // behind), fixes it up, and starts the countdown.
    void begin(Sim& sim, int slot, double countdown = 3.0);
    // Every frame (after the physics).
    void update(Sim& sim, double dt);
    void stop() { state = OFF; }
    bool active() const { return state != OFF; }
    // A message from another player in the session.
    void onMessage(int player, const std::string& name, const std::string& msg);
    void setSelf(int player, const std::string& name);
    // Medal for a time: 0 gold, 1 silver, 2 bronze, -1 none.
    int medalFor(double t) const;
    int totalGates() const { return (int)level.gates.size() * (level.mode == LEVEL_RACE ? level.laps : 1); }
    // R (repair in place) allowed right now.
    bool repairsAllowed() const { return state == OFF || state == FINISHED || state == OUT || level.resets; }

    // Where grid slot k starts.
    static void gridSlot(const Level& lv, int k, V3& pos, double& yaw);

private:
    V3 last;
    bool haveLast = false;
    int selfId = -1;
    std::string selfName = "YOU";
    RunEntry& entry(int player, const std::string& name);
    void setBanner(const std::string& s, double t = 1.6) { banner = s, bannerTime = t; }
};

std::string formatRaceTime(double s);  // "1:02.45"
