#include "levelrun.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

std::string formatRaceTime(double s) {
    if (s < 0) return "--:--.--";
    const int m = (int)(s / 60);
    char b[32];
    std::snprintf(b, sizeof b, "%d:%05.2f", m, s - m * 60);
    return b;
}

void LevelRun::gridSlot(const Level& lv, int k, V3& pos, double& yaw) {
    yaw = lv.startYaw;
    const V3 f(std::cos(yaw), 0, -std::sin(yaw)), l(-f.z, 0, f.x);
    // Two abreast (offset from the middle: the left car a little ahead), 7 m between rows.
    const int row = k / 2, side = k % 2;
    pos = lv.start - f * (7.0 * row + side * 2.0) + l * (side ? -2.8 : 2.8);
}

RunEntry& LevelRun::entry(int player, const std::string& name) {
    for (RunEntry& e : board)
        if (e.player == player) {
            if (!name.empty()) e.name = name;
            return e;
        }
    RunEntry e;
    e.player = player;
    e.name = name;
    board.push_back(e);
    return board.back();
}

void LevelRun::setSelf(int player, const std::string& name) {
    for (RunEntry& e : board)
        if (e.player == selfId) e.player = player, e.name = name;
    selfId = player;
    selfName = name;
}

void LevelRun::begin(Sim& sim, int slot, double countdown) {
    state = COUNTDOWN;
    clock = -countdown;
    gate = lap = 0;
    finishTime = -1;
    outWhy.clear();
    splits.clear();
    haveLast = false;
    // Everyone's back to the start of the board.
    for (RunEntry& e : board) e.gates = 0, e.time = -1, e.out = false, e.why.clear();
    entry(selfId, selfName);
    if (sim.cars.empty() || !level.hasStart) return;
    V3 p;
    double yaw;
    gridSlot(level, slot, p, yaw);
    Car& c = sim.cars[sim.player];
    sim.dropRopes(sim.player);
    c.respawn(V3(p.x, sim.world.clearY(p.x, p.z) + 0.05, p.z), yaw);
    c.setDown(sim.world);
    c.spawnPos = c.frame().com;  // (R R from here goes back to the grid)
    c.spawnYaw = yaw;
    c.pilot.active = false;
    setBanner(std::to_string((int)std::ceil(countdown)), 1.0);
}

int LevelRun::medalFor(double t) const {
    for (int k = 0; k < 3; k++)
        if (level.medals[k] > 0 && t <= level.medals[k]) return k;
    return -1;
}

void LevelRun::update(Sim& sim, double dt) {
    bannerTime = std::max(0.0, bannerTime - dt);
    if (state == OFF || sim.cars.empty()) return;
    Car& c = sim.cars[sim.player];
    const Frame f = c.frame();
    if (state == COUNTDOWN) {
        const int before = (int)std::ceil(-clock);
        clock += dt;
        // Held on the brakes till the green light.
        c.brake = 1;
        c.throttle = 0;
        if (std::fabs(dot(f.vel, f.fwd)) < 0.5) c.handbrake = 1;
        const int now = (int)std::ceil(-clock);
        if (clock >= 0) {
            state = RUNNING;
            clock = 0;
            setBanner("GO!", 1.2);
            c.handbrake = 0;
            c.brake = 0;
        } else if (now != before) {
            setBanner(std::to_string(now), 1.0);
        }
        last = f.com;
        haveLast = true;
        return;
    }
    if (state != RUNNING) return;
    clock += dt;
    if (!level.gates.empty() && haveLast) {
        const Gate& g = level.gates[gate];
        const V3 gf(std::cos(g.yaw), 0, -std::sin(g.yaw)), gl(-gf.z, 0, gf.x);
        const double a = dot(last - g.p, gf), b = dot(f.com - g.p, gf);
        // Crossed forwards, between the posts (pro-rated where it crossed).
        if (a < 0 && b >= 0) {
            const double t = a / (a - b);
            const V3 at = last + (f.com - last) * t;
            if (std::fabs(dot(at - g.p, gl)) <= g.width / 2) {
                splits.push_back(clock);
                const int n = (int)level.gates.size();
                const bool lapDone = gate == n - 1;
                const bool raceDone = level.mode != LEVEL_RACE || lap + 1 >= level.laps;
                if (lapDone && raceDone) {
                    state = FINISHED;
                    finishTime = clock;
                    if (best < 0 || clock < best) best = clock;
                    const int m = medalFor(clock);
                    static const char* medals[3] = {"  GOLD", "  SILVER", "  BRONZE"};
                    setBanner("FINISHED " + formatRaceTime(clock) + (m >= 0 ? medals[m] : ""), 5.0);
                    char b[48];
                    std::snprintf(b, sizeof b, "fin %.3f", clock);
                    outbox.push_back(b);
                } else {
                    if (lapDone) {
                        lap++;
                        gate = 0;
                        setBanner("LAP " + std::to_string(lap + 1) + " OF " + std::to_string(level.laps), 1.6);
                    } else {
                        gate++;
                        setBanner(formatRaceTime(clock), 1.0);
                    }
                    char b[48];
                    std::snprintf(b, sizeof b, "prog %d %.3f", lap * n + gate, clock);
                    outbox.push_back(b);
                }
                RunEntry& me = entry(selfId, selfName);
                me.gates = lap * n + (state == FINISHED ? n : gate);
                if (state == FINISHED) me.time = finishTime;
            }
        }
    }
    last = f.com;
    haveLast = true;
    if (state != RUNNING) return;
    // Out: too wrecked, or out of time.
    std::string why;
    if (level.maxDamage > 0 && c.damage * 100 > level.maxDamage) why = "TOO DAMAGED";
    else if (level.timeLimit > 0 && clock > level.timeLimit) why = "OUT OF TIME";
    else if (c.engineDead && c.hydrolocked) why = "DROWNED THE ENGINE";
    if (!why.empty()) {
        state = OUT;
        outWhy = why;
        setBanner("OUT: " + why, 4.0);
        outbox.push_back("out " + why);
        RunEntry& me = entry(selfId, selfName);
        me.out = true;
        me.why = why;
    }
}

void LevelRun::onMessage(int player, const std::string& name, const std::string& msg) {
    std::istringstream in(msg);
    std::string tag;
    in >> tag;
    RunEntry& e = entry(player, name);
    if (tag == "prog") {
        int n = 0;
        double t = 0;
        in >> n >> t;
        e.gates = std::max(e.gates, n);
    } else if (tag == "fin") {
        double t = 0;
        in >> t;
        e.time = t;
        e.gates = totalGates();
    } else if (tag == "out") {
        e.out = true;
        std::getline(in, e.why);
    }
}
