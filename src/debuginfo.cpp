#include "debuginfo.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "biome.h"
#include "daylight.h"
#include "levelrun.h"
#include "net.h"
#include "throttle.h"
#include "trunk.h"
#include "vehicles.h"
#include "walker.h"
#include "world.h"

void DebugColumn::add(DebugTone tone, const char* fmt, ...) {
    if (used == lines.size()) lines.emplace_back();
    DebugLine& l = lines[used++];
    l.tone = tone;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(l.text, sizeof l.text, fmt, ap);
    va_end(ap);
}

void FrameHistory::push(double ms) {
    if (!std::isfinite(ms) || ms < 0) return;
    ms_[head_] = (float)ms;
    head_ = (head_ + 1) % kFrames;
    count_ = std::min(count_ + 1, kFrames);
}

double FrameHistory::ms(int back) const {
    if (back < 0 || back >= count_) return 0;
    return ms_[(head_ - 1 - back + 2 * kFrames) % kFrames];
}

FrameHistory::Stats FrameHistory::stats() const {
    Stats s;
    if (count_ == 0) return s;
    float sorted[kFrames];
    double sum = 0, recent = 0;
    const int nRecent = std::min(count_, 8);
    for (int i = 0; i < count_; i++) {
        sorted[i] = (float)ms(i);
        sum += sorted[i];
        if (i < nRecent) recent += sorted[i];
        s.worstMs = std::max(s.worstMs, (double)sorted[i]);
    }
    s.avgMs = sum / count_;
    s.avgFps = s.avgMs > 0 ? 1000.0 / s.avgMs : 0;
    s.fps = recent > 0 ? 1000.0 * nRecent / recent : 0;
    // The 1% low: the frame rate of the slowest hundredth of frames (the 99th percentile frame time).
    const int k = std::min(count_ - 1, (int)std::floor(count_ * 0.99));
    std::nth_element(sorted, sorted + k, sorted + count_);
    s.lowFps = sorted[k] > 0 ? 1000.0 / sorted[k] : 0;
    return s;
}

bool processMemory(long& rssKb, long& peakKb) {
    rssKb = peakKb = -1;
#ifdef __linux__
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return false;
    char line[160];
    while (std::fgets(line, sizeof line, f)) {
        if (!std::strncmp(line, "VmRSS:", 6)) rssKb = std::atol(line + 6);
        else if (!std::strncmp(line, "VmHWM:", 6)) peakKb = std::atol(line + 6);
    }
    std::fclose(f);
    return rssKb >= 0;
#else
    return false;
#endif
}

const char* compassPoint(double degrees) {
    static const char* const names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    double d = std::fmod(degrees, 360.0);
    if (d < 0) d += 360;
    return names[(int)std::floor((d + 22.5) / 45.0) % 8];
}

double wheelSlipAngle(const Car& car, const Wheel& wh) {
    if (wh.inner < 0 || wh.outer < 0 || wh.inner >= (int)car.nodes.size() || wh.outer >= (int)car.nodes.size()) return 0;
    const Node &a = car.nodes[wh.inner], &b = car.nodes[wh.outer];
    V3 axis = b.p - a.p;
    if (len(axis) < 1e-6) return 0;
    axis = norm(axis);
    const V3 vel = (a.v + b.v) * 0.5;
    V3 head = cross(axis, V3(0, 1, 0));  // the way it rolls: level, across the axle
    if (len(head) < 1e-6) return 0;
    head = norm(head);
    if (dot(head, car.frame().fwd) < 0) head = head * -1.0;
    const double vl = dot(vel, head), vs = dot(vel, cross(V3(0, 1, 0), head));  // along, and to the left of the way it points
    if (std::fabs(vl) < 0.8 && std::fabs(vs) < 0.8) return 0;
    return std::atan2(vs, std::fabs(vl));
}

double wheelSpringTravel(const Car& car, const Wheel& wh) {
    if (wh.spring < 0 || wh.spring >= (int)car.beams.size()) return 0;
    const Beam& bm = car.beams[wh.spring];
    if (bm.a < 0 || bm.b < 0 || bm.a >= (int)car.nodes.size() || bm.b >= (int)car.nodes.size()) return 0;
    return (wh.springRest - len(car.nodes[bm.a].p - car.nodes[bm.b].p)) / std::max(0.2, wh.motionRatio);
}

double engineTorqueNow(const Car& car) {
    if (!car.spec || car.spec->torque.empty()) return 0;
    const auto& pts = car.spec->torque;
    double t;
    if (car.rpm <= pts.front().rpm) t = pts.front().nm;
    else {
        t = pts.back().nm;
        for (size_t i = 1; i < pts.size(); i++)
            if (car.rpm < pts[i].rpm) {
                const double u = (car.rpm - pts[i - 1].rpm) / (pts[i].rpm - pts[i - 1].rpm);
                t = pts[i - 1].nm + u * (pts[i].nm - pts[i - 1].nm);
                break;
            }
    }
    return car.engineDead ? 0 : t * std::clamp(car.throttleFill, 0.0, 1.0);
}

namespace {

const char* paintName(int p) {
    static const char* const names[] = {"NONE", "ROAD", "TRAIL", "ROCK", "GRAVEL", "PAD", "SCREE", "SAND", "MUD",
                                        "SNOW", "ICE", "WET", "FREEWAY", "TRACK", "DESERT", "RED ROCK"};
    return p >= 0 && p < (int)(sizeof names / sizeof *names) ? names[p] : "?";
}

const char* camName(int mode) {
    static const char* const names[] = {"CHASE", "ORBIT", "COCKPIT", "DRONE"};
    return mode >= 0 && mode < 4 ? names[mode] : "?";
}

const char* runState(int s) {
    static const char* const names[] = {"OFF", "COUNTDOWN", "RUNNING", "FINISHED", "OUT"};
    return s >= 0 && s < 5 ? names[s] : "?";
}

const char* wheelTag(const Car& car, int i, char* buf) {
    const Wheel& wh = car.wheels[i];
    if (car.wheels.size() == 4) std::snprintf(buf, 4, "%c%c", wh.front ? 'F' : 'R', wh.left ? 'L' : 'R');
    else std::snprintf(buf, 4, "%c%d", wh.left ? 'L' : 'R', i / 2 + 1);
    return buf;
}

// Speed the way the settings want it, and the other way beside it.
void speedText(char* out, size_t n, double ms, bool mph) {
    if (mph) std::snprintf(out, n, "%.0f MPH  %.0f KM/H  %.1f M/S", ms * 2.23694, ms * 3.6, ms);
    else std::snprintf(out, n, "%.0f KM/H  %.0f MPH  %.1f M/S", ms * 3.6, ms * 2.23694, ms);
}

void timeText(char* out, size_t n, double seconds) {
    const int t = (int)seconds;
    if (t >= 3600) std::snprintf(out, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else std::snprintf(out, n, "%d:%02d.%d", t / 60, t % 60, (int)(seconds * 10) % 10);
}

void gatherLeft(const DebugInputs& in, DebugColumn& L) {
    const Sim& sim = *in.sim;
    const World& w = sim.world;
    const bool foot = in.walker && in.walker->active;
    const Car& car = sim.cars[sim.player];
    const V3 me = foot ? in.walker->p : car.frame().com;

    {  // "A1B2C3D  DEV BUILD  BUILT OCT 17 2026 ...": the commit and kind first, when it was built under it
        const char* built = std::strstr(in.build, "  BUILT ");
        L.add(DT_HEAD, "CRUMPLE ZONE  %.*s", built ? (int)(built - in.build) : 60, in.build);
        if (built) L.add(DT_DIM, "%s", built + 2);
    }
    const FrameHistory::Stats& f = in.frame;
    L.add(f.fps >= 55 ? DT_GOOD : f.fps >= 30 ? DT_WARN : DT_BAD, "%.0f FPS  AVG %.0f  1%% LOW %.0f  (%.1f MS, WORST %.1f)", f.fps, f.avgFps, f.lowFps, f.avgMs, f.worstMs);
    L.add(DT_TEXT, "SIM %.2f MS  DRAW CPU %.2f MS  %d STEPS AT %.0f HZ", in.physicsMs, in.drawCpuMs, in.substeps, 1.0 / sim.h);
    L.add(DT_TEXT, "PHYSICS THREADS %d OF %d   %dx%d", in.physicsThreads, in.physicsThreadLimit, in.width, in.height);
    if (*in.gpu) L.add(DT_DIM, "%.52s", in.gpu);
    if (in.terrainFullTris) {
        L.add(DT_TEXT, "RENDER %.2fX  SHADOWS %d  MSAA %d", in.renderScale, in.shadows, in.msaa);
        L.add(DT_TEXT, "TERRAIN %uK/%uK TRIS  %u/%u CHUNKS", in.terrainTris / 1000, in.terrainFullTris / 1000, in.terrainVisible, in.terrainChunks);
        L.add(DT_TEXT, "SCENERY %uK VERTS  SHADOW %uK", in.sceneryVerts / 1000, in.shadowVerts / 1000);
        L.add(DT_TEXT, "PROPS %d DRAWS  %d INST  %d BILLB  %lldK TRIS", in.propDraws, in.propInstances, in.propBillboards, in.propTris / 1000);
        if (in.propGpuMs >= 0) L.add(DT_TEXT, "PROPS GPU %.2f MS", in.propGpuMs);
    }
    if (in.rssKb >= 0) L.add(DT_TEXT, "MEMORY %.0f MB  (PEAK %.0f MB)", in.rssKb / 1024.0, in.peakRssKb / 1024.0);
    L.add(DT_DIM, "");

    // Where we are.
    L.add(DT_HEAD, "WORLD");
    const char* mapStr = mapName(w.map);
    if (w.map == MAP_WILDS) L.add(DT_TEXT, "MAP %d %s (%s)", w.map, mapStr, wildsKindName(w.wildsKind));
    else L.add(DT_TEXT, "MAP %d %s", w.map, mapStr);
    if (in.playtest) L.add(DT_WARN, "PLAYTEST OF THE EDITOR'S LEVEL");
    if (in.run && in.run->active()) {
        const LevelRun& r = *in.run;
        char t[24];
        timeText(t, sizeof t, std::max(0.0, r.state == LevelRun::FINISHED ? r.finishTime : r.clock));
        L.add(r.state == LevelRun::OUT ? DT_BAD : r.state == LevelRun::FINISHED ? DT_GOOD : DT_TEXT, "LEVEL %s", r.level.name.c_str());
        const int gates = (int)r.level.gates.size();
        L.add(DT_TEXT, "  %s  GATE %d/%d  LAP %d  TIME %s", runState(r.state), std::min(r.gate + 1, std::max(1, gates)), gates, r.lap + 1, t);
    }
    L.add(DT_TEXT, "BIOME %s", biomeName(in.biome));
    if (*in.place) L.add(DT_TEXT, "NEAR %s  %.0f M", in.place, in.placeDist);
    const int hh = (int)in.dayHours % 24, mm = (int)(std::fmod(in.dayHours, 1.0) * 60);
    L.add(DT_TEXT, "TIME OF DAY %02d:%02d  (%s)", hh, mm, in.dayHours >= 6 && in.dayHours < 18 ? "DAY" : "NIGHT");
    char st[24];
    timeText(st, sizeof st, in.simSeconds);
    if (sim.paused) L.add(DT_WARN, "SIM TIME %s  PAUSED  SCALE %.2fX", st, sim.timeScale);
    else L.add(sim.timeScale != 1.0 ? DT_WARN : DT_TEXT, "SIM TIME %s  SCALE %.2fX", st, sim.timeScale);
    L.add(DT_TEXT, "CAMERA %s", camName(in.camMode));
    L.add(DT_DIM, "");

    // The player.
    L.add(DT_HEAD, foot ? "YOU (ON FOOT)" : "YOU (DRIVING)");
    L.add(DT_TEXT, "XYZ %.2f / %.2f / %.2f", me.x, me.y, me.z);
    double headDeg;
    if (foot) headDeg = std::atan2(std::cos(in.walker->yaw), -std::sin(in.walker->yaw)) * 180 / M_PI;
    else headDeg = std::atan2(car.frame().fwd.x, car.frame().fwd.z) * 180 / M_PI;
    if (headDeg < 0) headDeg += 360;
    L.add(DT_TEXT, "FACING %.1f DEG %s  (N +Z, E +X)", headDeg, compassPoint(headDeg));
    const double ground = w.height(me.x, me.z);
    L.add(DT_TEXT, "TERRAIN HEIGHT %.2f M  (%.2f ABOVE IT)", ground, me.y - ground);
    const V3 n = w.normal(me.x, me.z);
    L.add(DT_TEXT, "SLOPE %.1f DEG", std::acos(std::clamp(n.y, -1.0, 1.0)) * 180 / M_PI);
    const Surface sf = w.surfaceAt(me.x, me.z);
    const RoadInfo ri = w.roadAt(me.x, me.z);
    L.add(DT_TEXT, "SURFACE %s  PAINT %s%s", surfaceInfo(sf).name, paintName(ri.paint), ri.near ? "  ON ROAD" : "");
    const double wl = w.waterLevel(me.x, me.z);
    if (wl > -1e8 && wl > ground) L.add(DT_WARN, "WATER %.2f M DEEP  (SURFACE %.2f)", wl - ground, wl);
    else L.add(DT_DIM, "WATER NONE");
    if (w.wrapPeriod > 0) L.add(DT_TEXT, "FREEWAY LOOPS %.0f M  (SHIFTED %.0f M)", w.wrapPeriod, sim.wrapShift);
    L.add(DT_DIM, "");

    // What's in the world.
    L.add(DT_HEAD, "OBJECTS");
    L.add(DT_TEXT, "ROCKS %zu  TREES %zu  BOXES %zu  LOGS %zu", w.rocks.size(), w.trees.size(), w.boxes.size(), w.logs.size());
    L.add(DT_TEXT, "POLES %zu  BUSHES %zu  FIXTURES %zu  MODELS %zu", w.poles.size(), w.bushes.size(), w.fixtures.size(), w.models.size());
    L.add(DT_TEXT, "WATERS %zu  STATIONS %zu  SECTIONS %zu  PADS %zu", w.waters.size(), w.stations.size(), w.sections.size(), w.spawnPads.size());
    size_t vehicles = 0, remote = 0, nodes = 0, beams = 0, broken = 0;
    for (const Car& c : sim.cars) {
        if (!c.isProp()) vehicles++;
        if (c.remote) remote++;
        nodes += c.nodes.size(), beams += c.beams.size(), broken += c.brokenBeams;
    }
    L.add(DT_TEXT, "CARS %zu  PROPS %zu  REMOTE %zu  LOOSE WHEELS %zu  LINES %zu", vehicles, sim.cars.size() - vehicles, remote, sim.looseWheels.size(), sim.ropes.size());
    L.add(DT_TEXT, "NODES %zu  BEAMS %zu  BROKEN %zu", nodes, beams, broken);
}

void gatherVehicle(const DebugInputs& in, DebugColumn& R) {
    const Sim& sim = *in.sim;
    const Car& car = sim.cars[sim.player];
    if (car.isProp() || !car.spec) return;
    const VehicleSpec& vs = *car.spec;
    const Frame fr = car.frame();
    R.add(DT_HEAD, "%s", vs.name);
    if (*vs.engineName || *vs.gearboxName) R.add(DT_DIM, "%s / %s", vs.engineName, vs.gearboxName);
    R.add(DT_TEXT, "MASS %.0f KG  %zu NODES  %zu BEAMS", car.mass, car.nodes.size(), car.beams.size());
    R.add(car.brokenBeams ? DT_WARN : DT_TEXT, "DAMAGE %.0f%%  BROKEN BEAMS %d", car.damage * 100, car.brokenBeams);
    char sp[64];
    speedText(sp, sizeof sp, len(fr.vel), in.mph);
    R.add(DT_TEXT, "SPEED %s", sp);
    R.add(car.rpm > vs.redline ? DT_BAD : DT_TEXT, "RPM %.0f / %.0f  BOOST %.0f%%", car.rpm, vs.redline, car.boost * 100);
    const char* gearName = car.gear < 0 ? "R" : car.gear == 0 ? "N" : nullptr;
    char gear[8];
    if (!gearName) std::snprintf(gear, sizeof gear, "%d", car.gear), gearName = gear;
    R.add(DT_TEXT, "GEAR %s OF %zu  %s  %s", gearName, vs.gears.size(), car.manualShift ? "MANUAL" : "AUTO", car.lowRange ? "LOW RANGE" : "HIGH RANGE");
    if (const DriveMode* m = car.mode()) {
        R.add(DT_TEXT, "TRANSFER CASE %s%s%s", m->name, m->front ? "  FRONT DRIVEN" : "", m->centerLock ? "  CENTER LOCKED" : "");
    } else R.add(DT_DIM, "TRANSFER CASE NONE  (%s)", vs.drive == VehicleSpec::AWD ? "AWD" : vs.drive == VehicleSpec::FWD ? "FWD" : "RWD");
    const bool anyLocker = vs.lockerFront || vs.lockerRear;
    if (anyLocker) R.add(car.lockFront || car.lockRear ? DT_WARN : DT_TEXT, "LOCKERS FRONT %s  REAR %s", !vs.lockerFront ? "NONE" : car.lockFront ? "ON" : "OFF", !vs.lockerRear ? "NONE" : car.lockRear ? "ON" : "OFF");
    else R.add(DT_DIM, "LOCKERS NONE");
    R.add(DT_TEXT, "SWAY BAR %s  TRACTION CONTROL %s%s  ABS %s", vs.swayDisconnect ? (car.swayDisconnected ? "OFF" : "ON") : "FIXED",
          vs.tractionFitted ? (car.tractionControl ? "ON" : "OFF") : "NONE", car.tractionControl && car.tcCut < 0.97 ? " (CUTTING)" : "", vs.absEnabled ? "ON" : "OFF");
    R.add(DT_DIM, "");

    R.add(DT_HEAD, "INPUTS");
    if (in.throttleCap < 0.995f) R.add(DT_WARN, "THROTTLE %.0f%%  CAP %s", car.throttle * 100, throttle::percent(in.throttleCap).c_str());
    else R.add(DT_TEXT, "THROTTLE %.0f%%", car.throttle * 100);
    R.add(DT_TEXT, "BRAKE %.0f%%  HANDBRAKE %.0f%%", car.brake * 100, car.handbrake * 100);
    R.add(DT_TEXT, "STEER INPUT %+.2f  ANGLE %+.1f DEG", car.steerInput, car.steer * 180 / M_PI);
    if (car.pilot.active) R.add(DT_GOOD, "AUTOPILOT ON  %.0f M TO GO", std::sqrt(std::pow(car.pilot.goal.x - fr.com.x, 2) + std::pow(car.pilot.goal.z - fr.com.z, 2)));
    else if (car.ai) R.add(DT_GOOD, "AI DRIVER");
    R.add(DT_DIM, "");

    R.add(DT_HEAD, "ENGINE");
    const double tq = engineTorqueNow(car), kw = tq * car.rpm * 2 * M_PI / 60 / 1000;
    R.add(DT_TEXT, "TORQUE %.0f NM  POWER %.0f KW  %.0f HP", tq, kw, kw * 1.34102);
    double pk = peakPower(vs) / 1000;
    R.add(DT_DIM, "PEAK %.0f KW  %.0f NM", pk, peakTorque(vs));
    if (car.fuel >= 0) R.add(car.outOfFuel() ? DT_BAD : DT_TEXT, "FUEL %.1f / %.0f L%s", car.fuel, vs.fuelCapacity, car.jerry > 0.5 ? "  + CAN" : "");
    else R.add(DT_TEXT, "FUEL FULL (%.0f L)", vs.fuelCapacity);
    R.add(car.engineDead || car.engineHealth < 0.8 ? DT_WARN : DT_TEXT, "ENGINE HEALTH %.0f%%%s", car.engineHealth * 100, car.engineDead ? (car.hydrolocked ? "  FLOODED" : "  DEAD") : "");
    R.add(DT_TEXT, "TIRES %s  %.0f PSI%s", tireTypeName(vs.tireType), car.tirePsi, car.hasSpare ? "  SPARE FITTED" : "");
    R.add(DT_DIM, "");

    R.add(DT_HEAD, "WHEELS  LOAD RATIO ANGLE TRAVEL");
    for (int i = 0; i < (int)car.wheels.size() && i < 8; i++) {
        const Wheel& wh = car.wheels[i];
        char tag[4];
        wheelTag(car, i, tag);
        const bool down = wh.load > 0;
        char flags[40] = "";
        if (!wh.mounted) std::strcat(flags, " OFF");
        else if (wh.debeaded) std::strcat(flags, " DEBEAD");
        else if (wh.popped) std::strcat(flags, " FLAT");
        if (wh.shaftBroken) std::strcat(flags, " SHAFT");
        const bool bad = !wh.mounted || wh.debeaded || wh.popped || wh.shaftBroken;
        R.add(bad ? DT_WARN : down ? DT_TEXT : DT_DIM, "%-3s %6.0f N %+5.2f %+5.1f %+4.0f MM %s%s", tag, down ? (double)wh.load : 0.0, wh.sx,
              wheelSlipAngle(car, wh) * 180 / M_PI, wheelSpringTravel(car, wh) * 1000, down ? surfaceInfo(wh.surface).name : "AIR", flags);
    }
    int onGround = 0;
    for (const Wheel& wh : car.wheels) onGround += wh.load > 0;
    R.add(DT_DIM, "%d OF %zu ON THE GROUND", onGround, car.wheels.size());
}

void gatherFoot(const DebugInputs& in, DebugColumn& R) {
    const Sim& sim = *in.sim;
    const Walker& wk = *in.walker;
    R.add(DT_HEAD, "ON FOOT");
    char sp[64];
    speedText(sp, sizeof sp, len(V3(wk.v.x, 0, wk.v.z)), in.mph);
    R.add(DT_TEXT, "SPEED %s", sp);
    R.add(DT_TEXT, "VERTICAL %+.1f M/S  %s", wk.v.y, wk.grounded ? "ON THE GROUND" : "AIRBORNE");
    if (wk.inWater > 0.05) R.add(DT_WARN, "IN WATER %.2f M DEEP", wk.inWater);
    if (wk.carrying) R.add(DT_TEXT, "HOLDING A WHEEL  %s%s", wk.carrying->sizeName().c_str(), wk.carrying->tireOnly ? " (TIRE ONLY)" : "");
    else if (wk.holdingJack) R.add(DT_TEXT, "HOLDING THE JACK");
    else if (wk.holdingChain) R.add(DT_TEXT, "HOLDING SNOW CHAINS");
    else if (wk.hand.any()) R.add(DT_TEXT, "HOLDING %s", trunk::name(wk.hand).c_str());
    else R.add(DT_DIM, "HOLDING NOTHING");
    const int near = wk.carToEnter(sim);
    if (near >= 0) R.add(DT_GOOD, "CAN GET INTO %s  (%.1f M)", sim.cars[near].spec->name, len(sim.cars[near].frame().com - wk.p));
    else R.add(DT_DIM, "NO VEHICLE IN REACH");
    R.add(DT_DIM, "");
    // The truck stays on screen: the numbers that matter parked.
    const Car& car = sim.cars[sim.player];
    if (!car.isProp() && car.spec) {
        R.add(DT_HEAD, "YOUR VEHICLE: %s", car.spec->name);
        R.add(DT_TEXT, "DAMAGE %.0f%%  BROKEN BEAMS %d", car.damage * 100, car.brokenBeams);
        if (car.fuel >= 0) R.add(DT_TEXT, "FUEL %.1f / %.0f L", car.fuel, car.spec->fuelCapacity);
        R.add(DT_TEXT, "TIRES %.0f PSI  %d OF %zu ON THE GROUND", car.tirePsi, (int)std::count_if(car.wheels.begin(), car.wheels.end(), [](const Wheel& w) { return w.load > 0; }), car.wheels.size());
        R.add(DT_DIM, "");
    }
}

void gatherNet(const DebugInputs& in, DebugColumn& R) {
    if (!in.net || !in.net->active()) return;
    const NetSession& n = *in.net;
    R.add(DT_DIM, "");
    R.add(DT_HEAD, "MULTIPLAYER");
    if (n.role == NetSession::HOST) R.add(DT_GOOD, "HOSTING ON PORT %d  %s", n.port(), n.sessionName.c_str());
    else R.add(DT_GOOD, "CONNECTED TO %s", *in.peer ? in.peer : "THE HOST");
    R.add(DT_TEXT, "PLAYERS %d / %d  PROTOCOL v%d", n.playerCount(), n.maxPlayers, (int)net::kVersion);
    R.add(DT_TEXT, "TRAFFIC IN %.1f KB/S  OUT %.1f KB/S  (%.0f STATES/S)", in.rateInKB, in.rateOutKB, net::kSendRate);
    if (!n.clockSynced()) R.add(DT_WARN, "SETTING THE CLOCK BY THE HOST'S");
    else R.add(n.viewDelay() > 0.3 ? DT_WARN : DT_TEXT, "OTHERS SHOWN %.0f MS BEHIND THE SHARED CLOCK", n.viewDelay() * 1000);
    for (int k = 0; k < net::kMaxPeers; k++) {
        const NetPlayer& p = n.players[k];
        if (!p.active) continue;
        if (k == n.self) R.add(DT_TEXT, "%d %s (YOU)%s", k, p.name.c_str(), k == 0 ? " HOST" : "");
        else R.add(p.ping > 0.15 ? DT_WARN : DT_TEXT, "%d %s  %.0f MS%s", k, p.name.c_str(), p.ping * 1000, k == 0 ? " HOST" : "");
    }
}

}  // namespace

void gatherDebugInfo(const DebugInputs& in, DebugColumn& left, DebugColumn& right) {
    left.clear();
    right.clear();
    if (!in.sim || in.sim->cars.empty() || in.sim->player < 0 || in.sim->player >= (int)in.sim->cars.size()) return;
    gatherLeft(in, left);
    if (in.walker && in.walker->active) gatherFoot(in, right);
    else gatherVehicle(in, right);
    gatherNet(in, right);
}
