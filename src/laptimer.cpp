#include "laptimer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

#include "platform.h"

size_t LapTimer::lineIndex(const World& w) {
    size_t best = 0;
    double bd = 1e18;
    for (size_t i = 0; i < w.loopRoad.size(); i++) {  // (the loop's east side, where it's straightest and in the open)
        const double d = std::hypot(w.loopRoad[i].x - 300, w.loopRoad[i].z);
        if (d < bd) bd = d, best = i;
    }
    return best;
}

void LapTimer::linePose(const World& w, V3& p, double& yaw) {
    const size_t n = w.loopRoad.size(), i = lineIndex(w);
    p = w.loopRoad[i];
    const V3 a = w.loopRoad[(i + n - 1) % n], b = w.loopRoad[(i + 1) % n];
    yaw = std::atan2(-(b.z - a.z), b.x - a.x);
}

void LapTimer::setLoop(const World& w) {
    world = &w;
    L = w.loopLength;
    s0 = w.loopRoad.empty() ? 0 : w.loopAlong[lineIndex(w)];
    reset();
    lap = 0;
}

void LapTimer::setVehicle(const std::string& name) {
    if (name != vehicle) reset(), lap = 0;
    vehicle = name;
}

double LapTimer::best() const {
    auto it = bests.find(vehicle);
    return it == bests.end() ? 0 : it->second;
}

double LapTimer::wrap(double d) const {
    if (L <= 0) return d;
    d = std::fmod(d, L);
    if (d > L / 2) d -= L;
    if (d < -L / 2) d += L;
    return d;
}

void LapTimer::update(const V3& pos, double dt) {
    event = NONE;
    if (!world || world->loopRoad.empty() || L <= 0) return;
    if (running) lap += dt;
    const std::vector<V3>& road = world->loopRoad;
    size_t idx = 0;
    double bd = 1e18;
    for (size_t i = 0; i < road.size(); i++) {
        const double d = (road[i].x - pos.x) * (road[i].x - pos.x) + (road[i].z - pos.z) * (road[i].z - pos.z);
        if (d < bd) bd = d, idx = i;
    }
    if (std::sqrt(bd) > kRoadReach) {  // off the road: the lap is void, and the next sample starts fresh
        if (running) event = VOID;
        reset();
        return;
    }
    const double s = world->loopAlong[idx];
    if (!havePrev) {
        prevS = s, havePrev = true;
        return;
    }
    const double delta = wrap(s - prevS);
    if (std::fabs(delta) > 60) {  // a jump (a respawn): not driving
        if (running) event = VOID;
        reset();
        prevS = s, havePrev = true;
        return;
    }
    // Forward past a marker at arclength m this step?
    auto crossed = [&](double m) {
        const double ahead = wrap(m - prevS);
        return delta > 0 && ahead > 0 && ahead <= delta;
    };
    if (crossed(s0)) {
        if (running && next >= kChecks) {
            last = lap, laps++;
            auto it = bests.find(vehicle);
            const bool isBest = it == bests.end() || last < it->second;
            if (isBest) {
                bests[vehicle] = last;
                if (!savePath.empty()) save(savePath);
            }
            event = isBest ? BEST : LAP;
        } else {
            event = STARTED;
        }
        running = true, lap = 0, next = 0;
    } else if (running && next < kChecks && crossed(s0 + (next + 1) * L / (kChecks + 1))) {
        next++;
    }
    prevS = s;
}

std::string LapTimer::defaultPath() { return configDir() + "/laps.txt"; }

bool LapTimer::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const double t = std::atof(line.c_str() + tab + 1);
        if (t > 0) bests[line.substr(0, tab)] = t;
    }
    return true;
}

bool LapTimer::save(const std::string& path) const {
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) makeDir(path.substr(0, slash));
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    for (const auto& kv : bests) {
        char b[48];
        std::snprintf(b, sizeof b, "%.3f", kv.second);
        f << kv.first << '\t' << b << '\n';
    }
    return (bool)f;
}
