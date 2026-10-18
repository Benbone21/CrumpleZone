// The hang watchdogs (src/watchdog.h). watchdog/unit tests the functions directly; selftest/hang sleeps on purpose and
// only runs with `run_tests --selftest` (see tests/watchdog_selftest.sh, which checks the runner reports it and exits non-zero).
#include <thread>

#include "../src/watchdog.h"
#include "harness.h"

namespace {
void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}  // namespace

SCENARIO(watchdogUnit, "watchdog/unit", "The hang watchdogs trip on a stalled loop or an overlong run, and stay quiet while frames flow") {
    std::string got;
    {  // no frame ever: trips with the loading message
        wd::FrameWatchdog w(0.2, 100, 0.3, [&](const std::string& m) { got = m; });
        sleepMs(900);
        ctx.check("a loop that never starts is reported", w.tripped() && got.find("HANG: no frame for") == 0, got);
    }
    {  // frames, then a stall: names the frame
        std::string m2;
        wd::FrameWatchdog w(0.3, 100, 5, [&](const std::string& m) { m2 = m; });
        for (int f = 0; f < 20; f++) w.frame(f), sleepMs(20);
        ctx.check("frames flowing do not trip it", !w.tripped());
        sleepMs(900);
        ctx.check("a stalled loop is reported with its frame", w.tripped() && m2.find("at frame 19") != std::string::npos, m2);
    }
    {  // a run over its total limit even though frames keep coming
        std::string m3;
        wd::FrameWatchdog w(5, 0.3, 5, [&](const std::string& m) { m3 = m; });
        for (int f = 0; f < 30; f++) w.frame(f), sleepMs(20);
        ctx.check("a run over its total limit is reported", w.tripped() && m3.find("limit") != std::string::npos, m3);
    }
    {  // the generic watchdog: quiet until check() returns something, trips once
        std::atomic<int> trips{0};
        std::atomic<bool> on{false};
        wd::Watchdog w([&]() -> std::string { return on ? "x" : ""; }, [&](const std::string&) { trips++; }, 20);
        sleepMs(100);
        ctx.check("quiet while the condition is false", trips == 0);
        on = true;
        sleepMs(200);
        ctx.check("trips exactly once", trips == 1);
    }
}

SCENARIO(selftestHang, "selftest/hang", "Sleeps 20 s: with a tiny --scenario-timeout the runner must report HANG and exit non-zero") {
    std::this_thread::sleep_for(std::chrono::seconds(20));
    ctx.check("finished sleeping", true);
}
