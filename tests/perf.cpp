#include "../src/perf.h"
#include "harness.h"
#include <limits>

SCENARIO(adaptiveQuality, "engine/adaptive-quality", "Adaptive quality respects saved limits and recovers under VSync") {
    RenderSettings chosen, runtime;
    chosen.renderScale = 1.75f;
    chosen.msaa = 2;
    chosen.props = 1;
    chosen.ao = false;
    runtime = chosen;
    AdaptiveQuality controller;
    controller.init(chosen);
    controller.evaluate(std::numeric_limits<float>::quiet_NaN(), runtime);
    controller.evaluate(0, runtime);
    controller.evaluate(2000, runtime);
    ctx.check("invalid timings do not change quality", runtime.renderScale == chosen.renderScale);
    for (int i = 0; i < 1800; ++i) controller.evaluate(40, runtime);
    ctx.check("sustained slow frames lower quality", runtime.renderScale == 0.5f && runtime.shadows == 0);
    ctx.check("saved settings remain untouched", chosen.renderScale == 1.75f && chosen.shadows == 3 && !chosen.ao);
    bool limits = true;
    for (int i = 0; i < 6000; ++i) {
        controller.evaluate(1000.0f / 60, runtime);
        limits &= runtime.renderScale <= chosen.renderScale && runtime.msaa <= chosen.msaa && runtime.props <= chosen.props && !runtime.ao;
    }
    ctx.check("recovery never exceeds chosen quality", limits);
    ctx.check("60 Hz VSync permits full recovery including supersampling", runtime.renderScale == chosen.renderScale && runtime.shadows == chosen.shadows && runtime.msaa == chosen.msaa);
    chosen.renderScale = 0.5f;
    chosen.shadows = 0;
    chosen.msaa = 0;
    runtime = chosen;
    controller.reset(chosen);
    for (int i = 0; i < 1200; ++i) controller.evaluate(10, runtime);
    ctx.check("reset honors new manual choices", runtime.renderScale == 0.5f && runtime.shadows == 0 && runtime.msaa == 0);
}
