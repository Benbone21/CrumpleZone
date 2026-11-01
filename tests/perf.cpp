#include "../src/perf.h"
#include "harness.h"
#include "../src/menu.h"
#include "../src/collision.h"
#include <algorithm>
#include <chrono>
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
    ctx.check("sustained slow frames lower quality", runtime.renderScale == 0.5f && runtime.shadows == 1 && runtime.msaa == 0 && !runtime.bloom && !runtime.clouds);
    ctx.check("shadows and grass stay on (LOW), props and ground as chosen", runtime.shadows >= 1 && runtime.grass >= 1 && runtime.props == chosen.props && runtime.ground == chosen.ground);
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

SCENARIO(adaptiveKeepsGameplay, "engine/adaptive-keeps-props", "Adaptive quality never takes away the scanned props or the ground shading, however slow") {
    RenderSettings chosen, runtime;  // (the HIGH defaults: props 2, ground 2)
    chosen.renderScale = 0.5f;       // (already at the floor, as the report's settings were)
    runtime = chosen;
    AdaptiveQuality controller;
    controller.init(chosen);
    bool kept = true;
    int changes = 0;
    for (int i = 0; i < 6000; ++i) {
        changes += controller.evaluate(i % 7 ? 60.0f : 120.0f, runtime);
        kept &= runtime.props == chosen.props && runtime.ground == chosen.ground && runtime.props > 0 && runtime.ground > 0;
    }
    ctx.check("props and ground untouched through 6000 slow frames", kept);
    ctx.check("it still cut the cosmetic costs", changes >= 4 && !runtime.ao && !runtime.bloom && !runtime.clouds && runtime.msaa == 0);
    ctx.check("shadows and grass never off", runtime.shadows == 1 && runtime.grass == 1);
}

SCENARIO(adaptiveSpikes, "engine/adaptive-spikes", "A spike, or a short hitch, doesn't step quality down; a long slow patch costs one step at a time") {
    RenderSettings chosen, runtime;
    runtime = chosen;
    AdaptiveQuality controller;
    controller.init(chosen);
    int changes = 0;
    for (int i = 0; i < 3000; ++i) changes += controller.evaluate(i == 1500 ? 200.0f : 1000.0f / 60, runtime);
    ctx.check("one 200 ms spike in 50 s at 60 fps: no change", changes == 0 && runtime.ao && runtime.bloom);
    for (int i = 0; i < 3000; ++i) changes += controller.evaluate(i % 300 < 20 ? 90.0f : 1000.0f / 60, runtime);  // (a 20-frame hitch every 5 s)
    ctx.check("hitches of 20 frames every 5 s: no change", changes == 0, std::to_string(changes) + " changes");
    int steps = 0;
    for (int i = 0; i < 45; ++i) steps += controller.evaluate(40, runtime);  // (0.75 s of 25 fps: the window fills, then 750 ms slow)
    for (int i = 0; i < 60; ++i) steps += controller.evaluate(1000.0f / 60, runtime);
    ctx.check("a 1.8 s slow patch: at most one step", steps <= 1, std::to_string(steps) + " steps");
}

SCENARIO(qualityPresets, "engine/quality-presets", "The presets: HIGH is the default and unchanged, POTATO is a real low option, each round-trips") {
    RenderSettings def;
    ctx.check("the defaults are HIGH", presetOf(def) == Q_HIGH);
    RenderSettings g;
    applyPreset(g, Q_HIGH);
    ctx.check("HIGH: shadows 3, MSAA 4, AO, bloom, clouds, grass 2, scenery 2, full scale, every distance",
              g.shadows == 3 && g.msaa == 4 && g.ao && g.bloom && g.clouds && g.grass == 2 && g.ground == 2 && g.props == 2 &&
                  g.renderScale == 1.0f && g.drawDistance == 0 && !g.sharpen);
    for (int q = 0; q < 5; q++) {
        RenderSettings r;
        applyPreset(r, q);
        ctx.check(std::string(presetName(q)) + " round-trips", presetOf(r) == q);
    }
    RenderSettings p;
    p.renderScale = 0.75f;
    applyPreset(p, Q_POTATO);
    ctx.check("POTATO: no shadows, MSAA, AO, bloom, clouds or grass", p.shadows == 0 && p.msaa == 0 && !p.ao && !p.bloom && !p.clouds && p.grass == 0);
    ctx.check("POTATO: half scale, sharpened, 300 m in fog", p.renderScale == 0.5f && p.sharpen && p.drawDistance == 300);
    ctx.check("POTATO keeps the scanned props and one-tap ground (what you hit, what you drive on)", p.props >= 1 && p.ground >= 1);
    applyPreset(p, Q_LOW);
    ctx.check("leaving POTATO: full scale again, no draw distance", p.renderScale == 1.0f && p.drawDistance == 0 && !p.sharpen);
    RenderSettings own;
    own.renderScale = 1.5f;
    applyPreset(own, Q_ULTRA);
    ctx.check("between the others the player's render scale stays", own.renderScale == 1.5f);
    ctx.check("names", std::string(presetName(Q_POTATO)) == "POTATO" && std::string(presetName(Q_CUSTOM)) == "CUSTOM");
    ctx.check("FPS targets", kFpsTargets[0] == kFpsMatchDisplay && kFpsTargets[1] == 60 && kFpsTargets[3] == 144 && kFpsTargets[4] == 240 && kFpsTargets[5] == 360 && kFpsTargets[6] == 0);
}

SCENARIO(adaptiveTarget, "engine/adaptive-target", "Adaptive quality aims for the FPS target: 144 Hz cuts where 60 wouldn't") {
    RenderSettings chosen, at60, at144;
    at60 = at144 = chosen;
    AdaptiveQuality c60, c144;
    c60.init(chosen, 60), c144.init(chosen, 144);
    int n60 = 0, n144 = 0;
    for (int i = 0; i < 1200; ++i) n60 += c60.evaluate(12, at60), n144 += c144.evaluate(12, at144);  // (83 fps)
    ctx.check("83 fps: fine for 60, too slow for 144", n60 == 0 && n144 >= 1, std::to_string(n60) + " / " + std::to_string(n144));
}

SCENARIO(fpsTargetChoices, "engine/fps-target", "FPS target: saved values parse, the default follows the display, adaptive quality never aims above 144") {
    ctx.check("default is match display", Settings().fpsTarget == kFpsMatchDisplay);
    ctx.check("saved 0 stays uncapped", fpsTargetFromSaved(0) == 0);
    ctx.check("saved -1 is match display", fpsTargetFromSaved(-1) == kFpsMatchDisplay);
    ctx.check("old values stay", fpsTargetFromSaved(60) == 60 && fpsTargetFromSaved(120) == 120 && fpsTargetFromSaved(144) == 144);
    ctx.check("new values", fpsTargetFromSaved(240) == 240 && fpsTargetFromSaved(360) == 360 && fpsTargetFromSaved(500) == 360);
    ctx.check("in-between snaps down, low snaps to 60", fpsTargetFromSaved(165) == 144 && fpsTargetFromSaved(30) == 60);
    ctx.check("garbage is the default", fpsTargetFromSaved(std::numeric_limits<float>::quiet_NaN()) == kFpsMatchDisplay);
    ctx.check("match display follows the refresh rate", resolveFpsTarget(kFpsMatchDisplay, 360) == 360 && resolveFpsTarget(kFpsMatchDisplay, 59) == 59);
    ctx.check("match display, rate unknown: uncapped", resolveFpsTarget(kFpsMatchDisplay, 0) == 0 && resolveFpsTarget(kFpsMatchDisplay, -1) == 0);
    ctx.check("explicit targets ignore the display", resolveFpsTarget(120, 360) == 120 && resolveFpsTarget(0, 360) == 0);
    ctx.check("aim capped at 144, uncapped aims 60", adaptiveAim(360) == 144 && adaptiveAim(240) == 144 && adaptiveAim(100) == 100 && adaptiveAim(0) == 60);
    RenderSettings chosen, at360, at144;
    at360 = at144 = chosen;
    AdaptiveQuality c360, c144;
    c360.init(chosen, adaptiveAim(360)), c144.init(chosen, 144);
    int n360 = 0, n144 = 0, n360raw = 0;
    for (int i = 0; i < 1200; ++i) n360 += c360.evaluate(6, at360), n144 += c144.evaluate(6, at144);  // (167 fps: above 144)
    ctx.check("167 fps at a 360 target: no downgrades", n360 == 0 && n144 == 0, std::to_string(n360));
    RenderSettings slow = chosen;
    AdaptiveQuality c;
    c.init(slow, adaptiveAim(360));
    for (int i = 0; i < 1200; ++i) n360raw += c.evaluate(10, slow);  // (100 fps: below the 144 aim, so it does cut)
    ctx.check("100 fps still cuts", n360raw >= 1);
}

// The owner's "past a certain amount of damage the game drops to ~10 fps on every machine": a torn-off
// bumper stayed in the car's bounds wherever it lay, so driving off from it stretched the box over the
// map. Every parked car and prop paired with it (and woke, and stayed awake), and the pole queries swept
// the whole box: ~13x the sim time of a healthy car. Parts lying far off are left out of the box now.
SCENARIO(wreckCost, "engine/wreck-cost", "A wreck that shed a panel and drove off costs about what a healthy car does: no map-wide bounds, nothing woken") {
    ctx.sim.setup();  // (the default map: parked vehicles and the prop stacks, to be woken)
    for (Car& o : ctx.sim.cars) o.handbrake = 1;
    // (The Civic, 14 m from the wall at x = 114, in a lane clear of spawn, the ramp and the platform.)
    Car* cp = &ctx.add(vehicleCatalog()[0], 100, -18, 0);
    const int me = (int)ctx.sim.cars.size() - 1;
    ctx.sim.player = me;
    ctx.focus(*cp);
    ctx.run(8.0);  // (everything settles and sleeps)
    std::vector<std::pair<int, int>> pairs;
    std::vector<int> order;
    auto mine = [&] {
        collision::candidates(ctx.sim.cars, pairs, order);
        int n = 0;
        for (const auto& p : pairs) n += p.first == me || p.second == me;
        return n;
    };
    auto awake = [&] {
        int n = 0;
        for (const Car& o : ctx.sim.cars) n += !o.sleeping;
        return n;
    };
    // Pushed off along -X at 30 m/s (all but what lies torn off), the frames timed: the median, so a
    // busy machine's hiccups don't count.
    std::vector<char> left;
    auto drive = [&](double seconds) {
        Car& c = ctx.sim.cars[me];
        std::vector<double> ms;
        int f = 0;
        ctx.run(seconds, [&] {
            if (f++ % 30 == 0)
                for (size_t k = 0; k < c.nodes.size(); k++)
                    if (k >= left.size() || !left[k]) c.nodes[k].v.x = -30, c.nodes[k].v.z = 0;
            c.wake();
        });
        f = 0;
        ctx.onStep = nullptr;
        for (int i = 0; i < 120; i++) {
            if (f++ % 30 == 0)
                for (size_t k = 0; k < c.nodes.size(); k++)
                    if (k >= left.size() || !left[k]) c.nodes[k].v.x = -30, c.nodes[k].v.z = 0;
            const auto t0 = std::chrono::steady_clock::now();
            ctx.step();
            ms.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 1000);
        }
        std::nth_element(ms.begin(), ms.begin() + ms.size() / 2, ms.end());
        return ms[ms.size() / 2];
    };
    const double healthy = drive(1.0);
    const int healthyAwake = awake();
    // Back to the wall and nose into it at 70 km/h until a panel comes off.
    int torn = 0;
    for (int hit = 0; hit < 6 && !torn; hit++) {
        Car& c = ctx.sim.cars[me];
        c.translate(V3(114 - 10 - c.frame().com.x, 0, -18 - c.frame().com.z));
        for (Node& n : c.nodes) n.v = V3(70 / 3.6, 0, 0);
        c.wake();
        ctx.run(2.5);
        for (const Panel& p : c.panels) torn += p.state == 2;
        ctx.note("hit " + std::to_string(hit + 1) + ": damage " + std::to_string(c.damage * 100) + "%, x " + std::to_string(c.frame().com.x) + ", heading " + std::to_string(headingDeg(c)));
    }
    Car& c = ctx.sim.cars[me];
    if (!torn)  // (which hit tears what is chaotic: failing that, the hits' worst, its bumper's clips go)
        for (Panel& p : c.panels)
            if (std::string(p.name) == "FRONT BUMPER")
                for (int pi : p.pins) c.pins[pi].broken = true;
    ctx.run(2.0);
    torn = 0;
    for (const Panel& p : c.panels) torn += p.state == 2;
    if (!ctx.check("a panel was torn off", torn > 0)) return;
    left.assign(c.nodes.size(), 0);
    for (const Panel& p : c.panels)
        if (p.state == 2)
            for (int k : p.panelNodes) left[k] = 1;
    ctx.run(3.0);  // (the parked ones it shook awake settle again)
    const double wrecked = drive(12.0);  // (~360 m off)
    const double far = c.frame().com.x;
    int strays = 0;
    for (int k = 0; k < (int)c.nodes.size(); k++) strays += left[k] && c.stray[k];
    ctx.check("it drove off from what it shed", far < 114 - 250 && strays > 0, "x " + std::to_string(far) + ", strays " + std::to_string(strays));
    for (size_t i = 0; i < ctx.sim.cars.size(); i++)
        if (!ctx.sim.cars[i].sleeping) ctx.note(std::string("awake: ") + ctx.sim.cars[i].spec->name + " #" + std::to_string(i) + " at x " + std::to_string(ctx.sim.cars[i].frame().com.x) + " z " + std::to_string(ctx.sim.cars[i].frame().com.z) + " still " + std::to_string(ctx.sim.cars[i].stillTime));
    const V3 box = c.bbMax - c.bbMin;
    ctx.metric("its bounds, longest side", std::max(box.x, std::max(box.y, box.z)), "m", 1, 15);
    ctx.metric("bodies its bounds meet", mine(), "", 0, 0);
    ctx.metric("bodies awake (healthy run: " + std::to_string(healthyAwake) + ")", awake(), "", 0, healthyAwake + 1);
    ctx.metric("sim time per frame, wreck / healthy (median; was ~13x)", wrecked / std::max(1e-6, healthy), "x", 0, 2.0);
}
