// Runs the named scenarios one after another on this thread, the way run_tests' pool does,
// and prints each one's car address and its metrics (state left behind by one run can reach
// the next).
#include "../../../tests/harness.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    for (int a = 1; a < argc; a++)
        for (const Scenario& sc : scenarioRegistry()) {
            if (sc.name != argv[a]) continue;
            Sim sim;
            sim.setupEmpty();
            Ctx ctx(sim);
            sc.body(ctx);
            std::printf("%s: car at %p\n", sc.name.c_str(), (void*)sim.cars.data());
            for (const MetricResult& m : ctx.metrics) std::printf("   %s %s: %.2f [%.2f .. %.2f]\n", m.ok ? "ok  " : "FAIL", m.name.c_str(), m.value, m.lo, m.hi);
        }
}
