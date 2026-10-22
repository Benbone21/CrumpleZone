// Scenario test harness.
//
// A scenario is a short scripted situation (brake from 100 km/h, AI laps the
// loop, coupe hits a pole...) that drives the real simulation and records:
//   checks  - pass/fail assertions
//   metrics - measured numbers with an expected range (e.g. braking distance 33..45 m)
//   series  - time series for charts in the HTML report
// Every frame the harness also enforces global invariants (no NaNs, no
// explosions, nothing tunnelling through the ground).
//
// The same scenarios run headless in the test runner (tests/run_tests.cpp) and
// visually in the game (`crumplezone --scenario NAME`).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../src/softbody.h"

struct CheckResult {
    std::string name;
    bool ok;
    std::string detail;
};

struct MetricResult {
    std::string name, unit;
    double value, lo, hi;
    bool ok;
};

struct SeriesData {
    std::string chart, line, unit;
    std::vector<double> t, v;
};

class Ctx {
public:
    explicit Ctx(Sim& sim) : sim(sim) {}
    Sim& sim;
    double t = 0;                     // scenario time (s)
    static constexpr double kFrame = 1.0 / 60.0;

    // ---- Driving the simulation ---------------------------------------------
    // One 60 Hz frame: AI drivers, physics substeps, damage stats, invariants.
    void step();
    // Runs for `seconds`, calling perFrame (if given) before each frame.
    void run(double seconds, const std::function<void()>& perFrame = nullptr);
    // Runs until done() returns true or maxSeconds pass; returns whether done() fired.
    bool runUntil(double maxSeconds, const std::function<bool()>& done,
                  const std::function<void()>& perFrame = nullptr);
    // Runs until the car sits still (every node under 5 cm/s for a quarter second; at once if
    // it already is), at most maxSeconds.
    void settle(const Car& car, double maxSeconds);

    // ---- Setting up ------------------------------------------------------------
    // Adds a vehicle on the ground at (x, z); heading in degrees, 0 = +X, 90 = -Z.
    Car& add(const VehicleSpec& spec, double x, double z, double headingDeg, bool ai = false);
    Car& addProp(const VehicleSpec& spec, double x, double z, double headingDeg);
    // Sets every node moving at `speed` (m/s) along the car's heading, wheels spinning to match.
    void launch(Car& car, double speed);
    // Makes this car the one the camera follows in playback.
    void focus(const Car& car);
    Car& car(int index) { return sim.cars[index]; }

    // ---- Recording results -----------------------------------------------------
    bool check(const std::string& name, bool ok, const std::string& detail = "");
    bool metric(const std::string& name, double value, const std::string& unit, double lo, double hi);
    // Appends a sample at the current time to a chart line.
    void sample(const std::string& chart, const std::string& line, double value, const std::string& unit = "");
    void note(const std::string& text) { notes.push_back(text); }

    std::vector<CheckResult> checks;
    std::vector<MetricResult> metrics;
    std::vector<SeriesData> series;
    std::vector<std::string> notes;

    // Called after every frame; the game uses it to hand the frame to the renderer.
    std::function<void()> onStep;
    // Set by the game to abort playback early.
    bool* abortFlag = nullptr;

private:
    bool invariantFailed[3] = {false, false, false};
    void checkInvariants();
};

// Handy measurements.
double speedKmh(const Car& car);
double headingDeg(const Car& car);
// Signed forward displacement of the center of mass since `from`, along `dir`.
double travelled(const Car& car, const V3& from, const V3& dir);
// Body nodes of `a` lying deep inside `b`'s (deformed) box: cars passing through each other.
int intrusions(const Car& a, const Car& b);
// Signed offset of p from the loop road's center line at sample idx (+ = right of the +1 direction).
double loopLateral(const World& w, const V3& p, int idx);

struct Scenario {
    std::string name;
    std::string description;
    std::function<void(Ctx&)> body;
};

std::vector<Scenario>& scenarioRegistry();

struct ScenarioRegistrar {
    ScenarioRegistrar(const std::string& name, const std::string& description, std::function<void(Ctx&)> body) {
        scenarioRegistry().push_back({name, description, std::move(body)});
    }
};

// Registers a scenario once:            SCENARIO(fn, "group/name", "what it checks") { ... }
#define SCENARIO(fn, NAME_, DESC_)                                  \
    static void fn(Ctx& ctx);                                       \
    static ScenarioRegistrar fn##_registrar(NAME_, DESC_, fn);      \
    static void fn(Ctx& ctx)

// Registers one scenario per vehicle:   VEHICLE_SCENARIO(fn, "group/name", "...") { use `spec` }
#define VEHICLE_SCENARIO(fn, NAME_, DESC_)                                                        \
    static void fn(Ctx& ctx, const VehicleSpec& spec);                                            \
    static int fn##_registrar = [] {                                                              \
        for (const VehicleSpec& s : vehicleCatalog()) {                                           \
            std::string lower = s.name;                                                           \
            for (char& ch : lower) ch = (char)std::tolower((unsigned char)ch);                    \
            scenarioRegistry().push_back({std::string(NAME_) + "/" + lower, DESC_,                \
                                          [&s](Ctx& c) { fn(c, s); }});                           \
        }                                                                                         \
        return 0;                                                                                 \
    }();                                                                                          \
    static void fn(Ctx& ctx, const VehicleSpec& spec)
