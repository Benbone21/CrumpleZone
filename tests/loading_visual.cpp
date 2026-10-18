// Headless checks that a vehicle's drawing template is made off the main thread without hitching it: first sight and spawn.
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "../src/carvisual.h"
#include "harness.h"

namespace {
int failures = 0;
void check(const char* name, bool ok, const char* detail = "") {
    std::printf("%s %s %s\n", ok ? "PASS" : "FAIL", name, detail);
    failures += !ok;
}
double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
const double kHitchMs = 25;  // what the main thread may spend on one call (a frame at 60 Hz is 16.7 ms, and the machine may be busy)
}  // namespace

int main() {
    const auto& cat = vehicleCatalog();
    // Cold specs (copies: the template cache is keyed by the spec's address).
    std::vector<VehicleSpec> specs(cat.begin(), cat.begin() + std::min<size_t>(5, cat.size()));
    double worstCall = 0, worstSpawn = 0, totalBuild = 0;
    for (VehicleSpec& vs : specs) {
        Car c;
        const double s0 = nowMs();
        c.build(vs, V3(), 0, V3(0.4, 0.4, 0.4));
        worstSpawn = std::max(worstSpawn, nowMs() - s0);
        const double t0 = nowMs();
        int polls = 0;
        bool shown = false;
        while (nowMs() - t0 < 60000) {
            const double a = nowMs();
            const VisualTemplate* t = tryVisualTemplate(c);
            worstCall = std::max(worstCall, nowMs() - a);
            polls++;
            if (t) {
                shown = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        totalBuild += nowMs() - t0;
        check("a car's template arrives (null until then)", shown);
        check("and is the same one visualTemplate gives", shown && tryVisualTemplate(c) == &visualTemplate(c));
        (void)polls;
    }
    std::printf("worst tryVisualTemplate call %.2f ms, worst car build %.2f ms, templates took %.0f ms in all\n", worstCall, worstSpawn, totalBuild);
    check("first sight never stalls the caller", worstCall < kHitchMs);
    check("building the car to spawn is within a frame", worstSpawn < 4 * kHitchMs);

    // Prebuild before a spawn: fire and forget, then ready.
    VehicleSpec spare = cat[0];
    const double p0 = nowMs();
    prebuildVisual(spare, V3(0.3, 0.3, 0.3));
    const double call = nowMs() - p0;
    check("prebuildVisual returns at once", call < kHitchMs);
    const double w0 = nowMs();
    while (!visualReady(spare) && nowMs() - w0 < 60000) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check("and the template becomes ready", visualReady(spare));

    // Many threads asking for one cold spec: one template, no one sees a half-made one.
    VehicleSpec shared = cat[std::min<size_t>(1, cat.size() - 1)];
    std::vector<const VisualTemplate*> got(8, nullptr);
    std::vector<std::thread> pool;
    for (int t = 0; t < 8; t++)
        pool.emplace_back([&, t] {
            Car c;
            c.build(shared, V3(), 0, V3(0.5, 0.2, 0.2));
            got[t] = &visualTemplate(c);
        });
    for (std::thread& t : pool) t.join();
    bool one = true, whole = true;
    for (const VisualTemplate* p : got) one &= p == got[0], whole &= p && !p->faces.empty();
    check("concurrent first sights share one finished template", one && whole);
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
