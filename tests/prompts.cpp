// On-foot prompts (src/prompts.*): the aim (a cone round the crosshair that counts the size of the target), the priority
// and hysteresis between targets, the markers and the prompt/hint text, headless.
#include <cmath>

#include "../src/interact.h"
#include "../src/prompts.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

const VehicleSpec* byId(const char* id) {
    for (const VehicleSpec& v : vehicleCatalog())
        if (std::string(v.id) == id) return &v;
    return nullptr;
}

V3 hubOf(const Car& c, int w) { return (c.nodes[c.wheels[w].inner].p + c.nodes[c.wheels[w].outer].p) * 0.5; }

// `d` turned `deg` degrees round the vertical (+: to the left).
V3 turned(const V3& d, double deg) {
    const double a = deg * M_PI / 180, ca = std::cos(a), sa = std::sin(a);
    return V3(d.x * ca + d.z * sa, d.y, -d.x * sa + d.z * ca);
}

V3 eyeOf(const Walker& wk) { return wk.p + V3(0, 1.62, 0); }

void standAt(const World& w, Walker& wk, const V3& q) {
    wk.p = V3(q.x, w.height(q.x, q.z), q.z);
    wk.v = V3();
    wk.active = true;
}

TrunkStock tools(bool all) {
    TrunkStock s;
    s.kit.unlimited = false;
    for (int t = 0; t < TOOL_COUNT; t++) s.kit.tools[t] = all;
    return s;
}

int cornerOf(const Car& c, bool front, bool left) {
    for (int k = 0; k < (int)c.wheels.size(); k++)
        if (c.wheels[k].front == front && c.wheels[k].left == left) return k;
    return -1;
}

}  // namespace

SCENARIO(promptsAim, "prompts/aim", "the aim counts the size of the target: a wheel is hit well off its middle, one 60 degrees off is not; out of reach is out; the target you have keeps its cone wider and wins a close call by the sticky bonus; every InteractKind has a row in the table") {
    const V3 eye(0, 1.6, 0), chest(0, 1.0, 0), fwd(1, 0, 0);
    const V3 wheel(2.0, 1.0, 0);
    double sc = 0;
    ctx.check("dead on is in", prompts::aimScore(eye, fwd, chest, wheel, 0.45, 3, 0, false, 0.12, sc));
    ctx.check("40 degrees off the middle of a wheel 2 m away is in (35 was the old cone to the middle)", prompts::aimScore(eye, turned(fwd, 40), chest, wheel, 0.45, 3, 0, false, 0.12, sc));
    ctx.check("60 degrees off is out", !prompts::aimScore(eye, turned(fwd, 60), chest, wheel, 0.45, 3, 0, false, 0.12, sc));
    ctx.check("...unless it is the one you have (the cone is wider)", prompts::aimScore(eye, turned(fwd, 55), chest, wheel, 0.45, 3, 0, true, 0.12, sc) &&
                                                                           !prompts::aimScore(eye, turned(fwd, 55), chest, wheel, 0.45, 3, 0, false, 0.12, sc));
    ctx.check("beyond reach is out", !prompts::aimScore(eye, fwd, chest, V3(3.6, 1, 0), 0.45, 3, 0, false, 0.12, sc));
    double mid = 0, off = 0;
    prompts::aimScore(eye, fwd, chest, wheel, 0.45, 3, 0, false, 0.12, mid);
    prompts::aimScore(eye, turned(fwd, 6), chest, wheel, 0.45, 3, 0, false, 0.12, off);
    ctx.check("the middle still scores best (a few degrees off it costs a little)", mid < off, std::to_string(mid) + " vs " + std::to_string(off));
    double st = 0;
    prompts::aimScore(eye, fwd, chest, wheel, 0.45, 3, 0, true, 0.12, st);
    ctx.check("the sticky bonus is taken off", std::fabs((mid - st) - 0.12) < 1e-9);
    for (int k = IA_TAKE_SPARE; k <= IA_RIDE_ALONG; k++) {
        const prompts::KindInfo& ki = prompts::info((InteractKind)k);
        ctx.check("kind " + std::to_string(k) + " has its own row (a new IA_ kind adds one to prompts.cpp)", std::string(ki.noun) != "A MARKER" && ki.radius > 0, ki.noun);
    }
}

SCENARIO(promptsPick, "prompts/pick", "beside a Blazer: a wheel is picked looking well off its hub; priority goes to what can be done; the target holds as the aim crosses to the next wheel (hysteresis, and more so while E is held); the markers list each wheel, the back and the door, the target flagged; blocked jobs say why; looking away says what to look at; far away: nothing") {
    const VehicleSpec* spec = byId("blazer");
    Car& c = ctx.add(*spec, -140, -50, 0);
    ctx.focus(c);
    c.handbrake = 1;
    c.setJack(true);
    ctx.run(1.0);
    const int fl = cornerOf(c, true, true), rl = cornerOf(c, false, true);
    ctx.check("a left front and left rear corner", fl >= 0 && rl >= 0);
    if (fl < 0 || rl < 0) return;
    const Frame f = c.frame();
    const V3 fwd = norm(V3(f.fwd.x, 0, f.fwd.z)), left(fwd.z, 0, -fwd.x);
    Walker wk;
    Interact ia;
    ia.trunkOn = true;
    ia.own = tools(true);
    ia.kit = ia.own.kit;
    const V3 hubF = hubOf(c, fl), hubR = hubOf(c, rl);
    standAt(ctx.sim.world, wk, (hubF + hubR) * 0.5 + left * 2.0);
    const V3 eye = eyeOf(wk);
    auto pickAt = [&](const V3& dir, const PickOpts* po = nullptr) { return Interact::pick(ctx.sim, wk, eye, dir, nullptr, ia.kit, &ia.own, nullptr, po); };

    // The generous aim.
    const V3 toF = norm(hubF - eye);
    InteractOption o = pickAt(toF);
    ctx.check("looking at the front wheel's hub: take the wheel off (the corner not up yet: it says so)", o.kind == IA_REMOVE_WHEEL && o.wheel == fl && o.blocked && o.prompt.find("JACK") != std::string::npos, o.prompt);
    o = pickAt(norm(hubF + V3(0, 0.35, 0) - eye));
    ctx.check("...and at the top of the tire", o.kind == IA_REMOVE_WHEEL && o.wheel == fl);
    // Hysteresis: from where the front wheel and the driver's door are only some degrees apart, sweep from the wheel to the door.
    standAt(ctx.sim.world, wk, f.com + fwd * 0.9 + left * 2.0);
    const V3 eyeS = eyeOf(wk);
    auto pickS = [&](const V3& dir, const PickOpts* p2 = nullptr) { return Interact::pick(ctx.sim, wk, eyeS, dir, nullptr, ia.kit, &ia.own, nullptr, p2); };
    auto isF = [&](const InteractOption& x) { return x.kind == IA_REMOVE_WHEEL && x.wheel == fl; };
    const V3 toFS = norm(hubF - eyeS), toDS = norm(prompts::doorAt(c) - eyeS);
    const double span = std::acos(clampd(dot(toFS, toDS), -1, 1)) * 180 / M_PI;
    const double sign = dot(cross(toFS, toDS), V3(0, 1, 0)) > 0 ? 1 : -1;
    ctx.check("the wheel and the door are some degrees apart", span > 8 && span < 60, std::to_string(span));
    double flip = -1;
    for (double a = 0; a <= span + 10; a += 0.25)
        if (pickS(turned(toFS, sign * a)).kind == IA_ENTER_CAR) {
            flip = a;
            break;
        }
    ctx.check("sweeping from the wheel to the door the plain pick changes over somewhere in between", flip > 0 && flip < span + 10 && isF(pickS(toFS)), std::to_string(flip) + " of " + std::to_string(span));
    const InteractOption was = pickS(toFS);
    PickOpts po;
    po.sticky = &was;
    po.stickyBonus = prompts::kStickyBonus;
    ctx.check("...but with the wheel as the target it keeps it a little past that (no flicker)", isF(pickS(turned(toFS, sign * (flip + 1.5)), &po)));
    ctx.check("...and lets go once the aim is clearly on the door", pickS(turned(toFS, sign * (flip + 12)), &po).kind == IA_ENTER_CAR);
    po.stickyBonus = prompts::kHoldBonus;
    ctx.check("holding E on it, it takes a clear change of aim to let go", isF(pickS(turned(toFS, sign * (flip + 12)), &po)));
    {  // an aim shaking across the crossover
        InteractOption cur = was;
        int changes = 0;
        for (int i = 0; i < 40; i++) {
            PickOpts p2;
            p2.sticky = &cur;
            const InteractOption n = pickS(turned(toFS, sign * (flip + ((i & 1) ? 1.2 : -1.2))), &p2);
            if (!n.same(cur)) changes++;
            cur = n;
        }
        ctx.check("an aim shaking across the crossover changes the target at most once", changes <= 1, std::to_string(changes));
        cur = was;
        changes = 0;
        for (int i = 0; i < 40; i++) {
            const InteractOption n = pickS(turned(toFS, sign * (flip + ((i & 1) ? 1.2 : -1.2))));
            if (!n.same(cur)) changes++;
            cur = n;
        }
        ctx.check("...where without it, it changes every frame", changes >= 30, std::to_string(changes));
    }
    // Far off: nothing.
    o = pickAt(left);
    ctx.check("looking away from the truck: no job", o.kind == IA_NONE && !o.passive, o.prompt);

    // Priority: the one that can be done beats one that can't, aimed alike.
    {
        Interact bare;
        bare.trunkOn = true;
        bare.own = tools(false);
        bare.kit = bare.own.kit;
        const InteractOption b = Interact::pick(ctx.sim, wk, eye, toF, nullptr, bare.kit, &bare.own);
        ctx.check("no lug wrench: the wheel is still offered, blocked, and says what it needs", b.kind == IA_REMOVE_WHEEL && b.blocked && b.prompt.find("NEEDS") != std::string::npos, b.prompt);
        std::string label, reason;
        prompts::split(b, label, reason);
        ctx.check("...as two lines: what, and why not", label == "REMOVE WHEEL" && reason.rfind("NEEDS", 0) == 0, label + " | " + reason);
    }

    // The markers and the prompt.
    ia.update(ctx.sim, wk, eye, toF, false, Ctx::kFrame);
    ia.update(ctx.sim, wk, eye, toF, false, Ctx::kFrame);
    prompts::Prompter pr;
    prompts::Focus fo = pr.update(ctx.sim, wk, ia, ia.nearby, eye, toF);
    int wheels = 0, doors = 0, backs = 0, currents = 0;
    for (const prompts::Marker& m : fo.markers) {
        wheels += m.kind == IA_REMOVE_WHEEL, doors += m.key == prompts::KEY_GET_IN, backs += m.kind == IA_OPEN_INVENTORY, currents += m.current;
    }
    ctx.check("a marker on each wheel within a few metres, the driver's door; one target", wheels >= 2 && doors == 1 && currents == 1 && fo.target >= 0, std::to_string(wheels) + " wheels " + std::to_string(doors) + " door " + std::to_string(currents) + " current");
    ctx.check("the target is the front wheel, a hold job on E", fo.current() && fo.current()->kind == IA_REMOVE_WHEEL && fo.current()->hold && fo.current()->key == prompts::KEY_INTERACT && fo.hint.empty());
    ctx.check("no two markers on one spot", [&] {
        for (size_t i = 0; i < fo.markers.size(); i++)
            for (size_t j = i + 1; j < fo.markers.size(); j++)
                if (fo.markers[i].car == fo.markers[j].car && len(fo.markers[i].at - fo.markers[j].at) < 0.4) return false;
        return true;
    }());
    // The door: J, not E, when nothing else is aimed at.
    const V3 door = prompts::doorAt(c);
    standAt(ctx.sim.world, wk, door + left * 1.7 - fwd * 0.2);
    const V3 eye2 = eyeOf(wk), toD = norm(door - eye2);
    ia.update(ctx.sim, wk, eye2, toD, false, Ctx::kFrame);
    fo = pr.update(ctx.sim, wk, ia, ia.nearby, eye2, toD);
    ctx.check("looking at the driver's door: the target is get in, on J", ia.current.kind == IA_ENTER_CAR && fo.current() && fo.current()->key == prompts::KEY_GET_IN && fo.current()->label.rfind("GET IN", 0) == 0, fo.current() ? fo.current()->label : "none");
    // Nothing aimed at: what to look at.
    const V3 away = left;
    ia.update(ctx.sim, wk, eye2, away, false, Ctx::kFrame);
    fo = pr.update(ctx.sim, wk, ia, ia.nearby, eye2, away);
    ctx.check("looking away with things near: nothing targeted, a hint to look at one", fo.target < 0 && fo.hint.rfind("LOOK AT ", 0) == 0 && !fo.markers.empty(), fo.hint);
    // 4.5 m off: markers, but walk up.
    standAt(ctx.sim.world, wk, f.com + left * 5.2);
    const V3 eye3 = eyeOf(wk), away3 = left;
    ia.update(ctx.sim, wk, eye3, away3, false, Ctx::kFrame);
    fo = pr.update(ctx.sim, wk, ia, ia.nearby, eye3, away3);
    ctx.check("a few metres off, looking away: the hint is still there", fo.target < 0, fo.hint);
    // Far: none.
    standAt(ctx.sim.world, wk, f.com + left * 40.0);
    ia.update(ctx.sim, wk, eyeOf(wk), left, false, Ctx::kFrame);
    fo = pr.update(ctx.sim, wk, ia, ia.nearby, eyeOf(wk), left);
    ctx.check("40 m away: no markers, no hint", fo.markers.empty() && fo.hint.empty() && fo.target < 0);
}

SCENARIO(promptsAir, "prompts/air", "on-foot tire airing: AIR UP and LET AIR OUT appear at the tire when the car is stopped and the pressure is off") {
    const VehicleSpec* spec = byId("blazer");
    Car& c = ctx.add(*spec, -140, -50, 0);
    ctx.focus(c);
    c.handbrake = 1;
    c.setJack(true);
    c.tirePsi = 20.0;  // well below the spec's target
    ctx.run(1.0);
    const int fl = cornerOf(c, true, true);
    ctx.check("a left front corner", fl >= 0);
    if (fl < 0) return;
    const Frame f = c.frame();
    const V3 fwd = norm(V3(f.fwd.x, 0, f.fwd.z)), left(fwd.z, 0, -fwd.x);
    Walker wk;
    Interact ia;
    ia.trunkOn = false;
    ia.own = tools(true);
    ia.kit = ia.own.kit;
    const V3 hubF = hubOf(c, fl);
    standAt(ctx.sim.world, wk, hubF + left * 2.0);
    const V3 eye = eyeOf(wk);
    auto pickAt = [&](const V3& dir) { return Interact::pick(ctx.sim, wk, eye, dir, nullptr, ia.kit, &ia.own, nullptr, nullptr); };
    // Aim at the hub: should pick the wheel job, not airing.
    InteractOption o = pickAt(norm(hubF - eye));
    ctx.check("aiming at the hub: the target is the wheel job, not airing", o.kind == IA_REMOVE_WHEEL && o.wheel == fl, o.prompt);
    // Aim at the sidewall (offset from the hub): should pick air up.
    V3 wheelOut = c.nodes[c.wheels[fl].outer].p - c.nodes[c.wheels[fl].inner].p;
    wheelOut = len(wheelOut) > 1e-6 ? norm(wheelOut) : f.fwd;
    const V3 sidewall = hubF + wheelOut * c.wheels[fl].R;
    // First, check that the air up option is generated (in the nearby list).
    std::vector<InteractOption> nearby;
    PickOpts po;
    po.nearby = &nearby;
    Interact::pick(ctx.sim, wk, eye, norm(hubF - eye), nullptr, ia.kit, &ia.own, nullptr, &po);
    bool sawAirUp = false;
    for (const InteractOption& opt : nearby) {
        if (opt.kind == IA_AIR_UP_TIRE) sawAirUp = true;
    }
    ctx.check("low pressure: AIR UP option is generated", sawAirUp);
    o = pickAt(norm(sidewall - eye));
    ctx.check("aiming at the sidewall: the target is air up", o.kind == IA_AIR_UP_TIRE && o.wheel == fl, o.prompt);
}

SCENARIO(promptsRemoveFit, "prompts/remove-fit", "a field repair's prompts are short: REMOVE <part> then FIT <part>, no job-name prefix, the reason of a blocked one split off") {
    Car& c = ctx.add(*byId("blazer"), -140, -50, 0);
    ctx.run(0.3);
    c.wheels[1].tieOffset = 0.05;
    c.wake();
    const std::vector<Fix> fx = fixesFor(c);
    const Fix* tr = nullptr;
    for (const Fix& f : fx)
        if (f.kind == Fix::TIE_ROD) tr = &f;
    ctx.check("a bent tie rod is a repair", tr != nullptr);
    if (!tr) return;
    Walker wk;
    Kit kit;
    InteractOption o = Interact::fixOption(ctx.sim, wk, 0, *tr, kit, nullptr);
    ctx.check("first: REMOVE TIE ROD (WRENCHES)", o.kind == IA_FIX && !o.blocked && o.prompt == "REMOVE TIE ROD (WRENCHES)", o.prompt);
    c.fieldJobs.push_back({Fix::TIE_ROD, tr->wheel, tr->axle, tr->beam, 1, false});
    o = Interact::fixOption(ctx.sim, wk, 0, *tr, kit, nullptr);
    ctx.check("then: FIT TIE ROD", o.kind == IA_FIX && o.prompt == "FIT TIE ROD", o.prompt);
    kit.unlimited = false;
    o = Interact::fixOption(ctx.sim, wk, 0, *tr, kit, nullptr);
    std::string label, reason;
    prompts::split(o, label, reason);
    ctx.check("no part: blocked, the label stays short", o.blocked && !reason.empty(), o.prompt);
}
