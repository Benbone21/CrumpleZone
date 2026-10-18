// The proving ground's lap timer (laptimer.h): a lap counts when the car comes round the loop road past every checkpoint in order,
// crossing the start/finish line twice; shortcuts, back-and-forth over the line and leaving the road don't; the best lap per
// vehicle is kept in a file.
#include <cmath>
#include <cstdio>

#include "../src/laptimer.h"
#include "../src/platform.h"
#include "harness.h"

namespace {

// Drives the timer along the loop road's samples (2 m apart) from `from` for `count` samples (negative: backwards) at 20 m/s.
void driveLoop(LapTimer& t, const World& w, int from, int count) {
    const int n = (int)w.loopRoad.size(), step = count < 0 ? -1 : 1;
    for (int k = 0; k != count; k += step) t.update(w.loopRoad[((from + k) % n + n) % n], 2.0 / 20);
}

}  // namespace

SCENARIO(lapTimerLap, "laps/lap", "A lap round the loop road starts at the line and counts when the line is crossed again: its time is the drive's, and the best is kept") {
    World w(MAP_PROVING_GROUND);
    LapTimer t;
    t.setLoop(w);
    t.setVehicle("TEST TRUCK");
    const int n = (int)w.loopRoad.size(), line = (int)LapTimer::lineIndex(w);
    ctx.check("the loop is there", n > 100 && w.loopLength > 1000);
    driveLoop(t, w, line - 10, 10);  // the run-up
    ctx.check("not timing before the line", !t.running && t.laps == 0);
    driveLoop(t, w, line, n + 12);
    ctx.check("one lap counted", t.laps == 1, std::to_string(t.laps));
    const double expect = n * 0.1;  // (samples a tenth of a second apart)
    ctx.check("its time is the drive's", std::fabs(t.last - expect) < 1.5, std::to_string(t.last) + " vs " + std::to_string(expect));
    ctx.check("it is the best, and the next lap is already running", t.best() == t.last && t.running);
    // A slower second lap: counted, but the best stays.
    for (int k = 0; k < n; k++) t.update(w.loopRoad[(line + 12 + k) % n], 0.2);
    ctx.check("two laps; a slower one is not the best", t.laps == 2 && t.last > t.best() + 1);
}

SCENARIO(lapTimerCheats, "laps/no-cheating", "Cutting across the loop, driving back and forth over the line, backwards laps, skipping ahead and leaving the road never count") {
    World w(MAP_PROVING_GROUND);
    const int n = (int)w.loopRoad.size(), line = (int)LapTimer::lineIndex(w);
    {  // back and forth over the line
        LapTimer t;
        t.setLoop(w), t.setVehicle("X");
        for (int r = 0; r < 12; r++) driveLoop(t, w, line - 6, 12), driveLoop(t, w, line + 6, -12);
        ctx.check("back and forth: no lap", t.laps == 0);
    }
    {  // across the middle, from just past the line to just before it
        LapTimer t;
        t.setLoop(w), t.setVehicle("X");
        driveLoop(t, w, line - 5, 10);
        ctx.check("timing", t.running);
        const V3 a = w.loopRoad[(line + 10) % n], b = w.loopRoad[(line + n - 10) % n];
        for (int k = 0; k <= 200; k++) t.update(a + (b - a) * (k / 200.0), 0.1);
        driveLoop(t, w, line - 10, 20);
        ctx.check("a shortcut: no lap", t.laps == 0);
    }
    {  // a jump along the road (a respawn) past the checkpoints
        LapTimer t;
        t.setLoop(w), t.setVehicle("X");
        driveLoop(t, w, line - 5, 10);
        driveLoop(t, w, line + n / 2, 1);
        driveLoop(t, w, line + n - 10, 20);
        ctx.check("a skip ahead: no lap", t.laps == 0);
    }
    {  // the whole loop backwards
        LapTimer t;
        t.setLoop(w), t.setVehicle("X");
        driveLoop(t, w, line + 5, -(n + 20));
        ctx.check("backwards: no lap", t.laps == 0);
    }
    {  // off the road for a moment, then on and round
        LapTimer t;
        t.setLoop(w), t.setVehicle("X");
        driveLoop(t, w, line - 5, n / 3);
        t.update(w.loopRoad[(line + n / 3) % n] + V3(0, 0, 200) * 0.0 + V3(100, 0, 100), 0.1);
        ctx.check("off the road voids it", !t.running && t.event == LapTimer::VOIDED);
        driveLoop(t, w, line + n / 3, n - n / 3 + 10);
        ctx.check("and the rest of that lap doesn't count", t.laps == 0);
    }
}

SCENARIO(lapTimerBests, "laps/best-persists", "The best lap per vehicle is written when it is set and read back by a new timer; a slower lap doesn't replace it") {
    World w(MAP_PROVING_GROUND);
    makeDir("build/test-laps");
    const std::string path = "build/test-laps/laps.txt";
    std::remove(path.c_str());
    const int n = (int)w.loopRoad.size(), line = (int)LapTimer::lineIndex(w);
    {
        LapTimer t;
        t.savePath = path;
        t.setLoop(w), t.setVehicle("ALPHA");
        driveLoop(t, w, line - 5, 5 + n + 5);
        const double a = t.best();
        ctx.check("a best is set", a > 10);
        t.setVehicle("BETA");
        ctx.check("another vehicle has none", t.best() == 0);
        for (int k = 0; k < n + 8; k++) {
            t.update(w.loopRoad[(line - 5 + k + n) % n], 0.2);
        }
        ctx.check("and sets its own", t.best() > a + 1);
    }
    LapTimer u;
    ctx.check("the file reads back", u.load(path) && u.bests.size() == 2);
    u.setVehicle("ALPHA");
    const double a = u.best();
    u.setLoop(w), u.setVehicle("ALPHA");
    ctx.check("ALPHA's best is its own", a > 10 && a < u.bests["BETA"] - 1);
    u.savePath = path;
    for (int k = 0; k < n + 8; k++) u.update(w.loopRoad[(line - 5 + k + n) % n], 0.4);
    LapTimer v;
    v.load(path);
    ctx.check("a slower lap leaves the file's best", std::fabs(v.bests["ALPHA"] - a) < 0.01);
    std::remove(path.c_str());
}
