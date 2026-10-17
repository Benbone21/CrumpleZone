// The F3 debug screen's numbers (debuginfo.h): the frame statistics, and the lines gathered for a Blazer parked on the
// proving ground, driving, and for the walker beside it. No SDL or GL: it reads the Sim.
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/debuginfo.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

// The first line that starts with `prefix`, or nullptr.
const DebugLine* find(const DebugColumn& c, const char* prefix) {
    for (size_t i = 0; i < c.size(); i++)
        if (!std::strncmp(c[i].text, prefix, std::strlen(prefix))) return &c[i];
    return nullptr;
}

// The number after `prefix` in the line that starts with it (NAN if there is none).
double numberAfter(const DebugColumn& c, const char* prefix) {
    const DebugLine* l = find(c, prefix);
    return l ? std::atof(l->text + std::strlen(prefix)) : NAN;
}

const VehicleSpec& blazer() {
    for (const VehicleSpec& s : vehicleCatalog())
        if (std::string(s.id) == "blazer") return s;
    return vehicleCatalog()[0];
}

bool allFit(const DebugColumn& c) {
    for (size_t i = 0; i < c.size(); i++)
        if (std::strlen(c[i].text) >= sizeof c[i].text - 1) return false;
    return true;
}

}  // namespace

SCENARIO(f3Frames, "f3info/frame-stats", "FPS now, average and 1% low from the frame times; the ring keeps the newest 240") {
    FrameHistory h;
    for (int i = 0; i < 300; i++) h.push(i % 60 == 30 ? 50.0 : 10.0);  // a slow frame every 60
    FrameHistory::Stats s = h.stats();
    ctx.check("it keeps 240", h.count() == FrameHistory::kFrames);
    ctx.metric("fps now (10 ms frames)", s.fps, "fps", 99, 101);
    ctx.metric("average fps", s.avgFps, "fps", 80, 100);
    ctx.metric("1% low (the 50 ms frames)", s.lowFps, "fps", 19, 21);
    ctx.metric("worst frame", s.worstMs, "ms", 49, 51);
    ctx.check("newest first", std::fabs(h.ms(0) - 10) < 1e-6 && std::fabs(h.ms(29) - 50) < 1e-6 && std::fabs(h.ms(28) - 10) < 1e-6);  // (i = 270 was the last slow one)
    FrameHistory e;
    ctx.check("empty is quiet", e.stats().fps == 0 && e.count() == 0);
    e.push(NAN), e.push(-3);
    ctx.check("junk is ignored", e.count() == 0);
}

SCENARIO(f3Helpers, "f3info/helpers", "Compass points, process memory") {
    ctx.check("0 deg is N, 90 E, 225 SW, -10 N", !std::strcmp(compassPoint(0), "N") && !std::strcmp(compassPoint(90), "E") && !std::strcmp(compassPoint(225), "SW") &&
                                                  !std::strcmp(compassPoint(-10), "N") && !std::strcmp(compassPoint(350), "N"));
    long rss = 0, peak = 0;
#ifdef __linux__
    const bool got = processMemory(rss, peak);
    ctx.check("memory is read", got && rss > 1000 && peak >= rss, std::to_string(rss) + " KB");
#else
    ctx.check("memory is guarded", !processMemory(rss, peak) && rss == -1);
#endif
}

SCENARIO(f3Parked, "f3info/parked-and-driving", "A Blazer parked: position, mass, four loaded wheels, zero speed; then driven: the speed line and a slip angle in a turn") {
    Car& c = ctx.add(blazer(), -60, -40, 90);  // facing -Z: south
    ctx.focus(c);
    ctx.run(2.0);
    DebugInputs in;
    in.sim = &ctx.sim;
    in.build = "TEST", in.gpu = "", in.place = "";
    in.dayHours = 14.5;
    in.frame.fps = 60;
    DebugColumn L, R;
    gatherDebugInfo(in, L, R);
    ctx.check("both columns have lines, none cut off", L.size() > 20 && R.size() > 15 && allFit(L) && allFit(R));
    const Frame f = c.frame();
    ctx.check("the position is the car's", std::fabs(numberAfter(L, "XYZ ") - f.com.x) < 0.01, find(L, "XYZ") ? find(L, "XYZ")->text : "none");
    const DebugLine* xyz = find(L, "XYZ");
    ctx.check("...all three coordinates", xyz && std::strstr(xyz->text, "/") && std::fabs(std::atof(std::strrchr(xyz->text, '/') + 1) - f.com.z) < 0.01);
    const DebugLine* face = find(L, "FACING");
    ctx.check("facing south (heading 180)", face && std::fabs(std::atof(face->text + 7) - 180) < 8, face ? face->text : "none");
    ctx.check("south is named", face && std::strstr(face->text, " S "));
    ctx.metric("mass on the screen", numberAfter(R, "MASS "), "kg", c.mass - 1, c.mass + 1);
    ctx.metric("the slope is gentle on the pad", numberAfter(L, "SLOPE "), "deg", 0, 6);
    ctx.metric("parked speed", numberAfter(R, "SPEED "), "km/h", 0, 1.5);
    ctx.check("time of day 14:30", find(L, "TIME OF DAY 14:30") != nullptr);
    ctx.check("all four wheels down", find(R, "4 OF 4 ON THE GROUND") != nullptr);
    // The wheel rows: FL FR RL RR, loads adding up to the weight.
    double total = 0;
    int rows = 0;
    for (const char* tag : {"FL ", "FR ", "RL ", "RR "}) {
        const DebugLine* w = find(R, tag);
        if (w) rows++, total += std::atof(w->text + 3);
    }
    ctx.check("four wheel rows", rows == 4);
    ctx.metric("wheel loads over weight", total / (c.mass * 9.81), "x", 0.85, 1.15);
    // Driving: speed matches the car's, in the unit asked for.
    ctx.launch(c, 12.0);
    ctx.run(0.4, [&] { ctx.sim.drive(true, false, false, false, false, Ctx::kFrame); });
    gatherDebugInfo(in, L, R);
    ctx.metric("speed line vs the car (km/h)", numberAfter(R, "SPEED "), "km/h", len(c.frame().vel) * 3.6 - 1, len(c.frame().vel) * 3.6 + 1);
    in.mph = true;
    gatherDebugInfo(in, L, R);
    ctx.metric("...and in mph", numberAfter(R, "SPEED "), "mph", len(c.frame().vel) * 2.23694 - 1, len(c.frame().vel) * 2.23694 + 1);
    // Into a turn at speed: the front tires slip sideways.
    double worst = 0;
    ctx.launch(c, 14.0);
    ctx.run(1.5, [&] {
        ctx.sim.drive(true, false, true, false, false, Ctx::kFrame);
        for (const Wheel& wh : c.wheels) worst = std::max(worst, std::fabs(wheelSlipAngle(c, wh)) * 180 / M_PI);
    });
    ctx.metric("worst slip angle in a turn", worst, "deg", 0.5, 40);
    ctx.metric("coil-over travel while parked-ish is small", std::fabs(wheelSpringTravel(c, c.wheels[0])) * 1000, "mm", 0, 250);
    ctx.metric("torque at idle-ish rpm is below the peak", engineTorqueNow(c), "nm", 0, peakTorque(*c.spec) + 1);
}

SCENARIO(f3Foot, "f3info/on-foot", "On foot: the right column is the walker's, holding a jack shows, and a bare Sim with no walker still gathers") {
    Car& c = ctx.add(blazer(), -60, -40, 0);
    ctx.focus(c);
    ctx.run(1.0);
    Walker wk;
    wk.exitCar(ctx.sim.world, c);
    wk.step(ctx.sim.world, ctx.sim, Ctx::kFrame, 0, 0, false, false, 0);
    DebugInputs in;
    in.sim = &ctx.sim, in.walker = &wk, in.build = "TEST", in.gpu = "", in.place = "";
    DebugColumn L, R;
    gatherDebugInfo(in, L, R);
    ctx.check("on foot heading", find(R, "ON FOOT") != nullptr);
    ctx.check("nothing in hand", find(R, "HOLDING NOTHING") != nullptr);
    ctx.check("the truck is in reach", find(R, "CAN GET INTO") != nullptr);
    ctx.check("left column says on foot", find(L, "YOU (ON FOOT)") != nullptr);
    wk.holdingJack = true;
    gatherDebugInfo(in, L, R);
    ctx.check("the jack is in hand", find(R, "HOLDING THE JACK") != nullptr);
    DebugInputs none;
    gatherDebugInfo(none, L, R);
    ctx.check("no sim, no lines, no crash", L.size() == 0 && R.size() == 0);
    // Lines reuse their storage: gathering again doesn't grow the list.
    const size_t cap = L.lines.capacity();
    gatherDebugInfo(in, L, R);
    gatherDebugInfo(in, L, R);
    ctx.check("the lines are reused", L.lines.capacity() >= cap && L.lines.capacity() < 200);
}
