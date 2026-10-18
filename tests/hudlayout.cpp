// The HUD's layout (hudlayout.h): at every common resolution and aspect ratio, with the longest
// text each element can carry, no two HUD rects touch and all of them are on screen.
// Pure arithmetic: no GL.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "../src/hudcontext.h"
#include "../src/hudlayout.h"
#include "harness.h"

using namespace hudlayout;

SCENARIO(hudLayout, "hud/layout", "At 720p to 1600p, 16:10 and 4:3, with the longest tag, fuel, anchor, winch and job text and every top-left panel up: no two HUD rects overlap and all stay on screen") {
    const int sizes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {2560, 1600}, {1024, 768}, {1366, 768}, {1440, 900}};
    int bad = 0;
    std::string first;
    for (const auto& sz : sizes) {
        const int W = sz[0], H = sz[1];
        const float u = uiScale(H);
        std::vector<std::pair<std::string, Rect>> r;
        const Column col = rightColumn(u, W, H, 70, 22, 48);  // "SNOW  MUD-TERRAIN TIRES  SUNK 12 CM  TREAD PACKED WITH MUD" and more
        r.push_back({"dial", dial(u, W, H)});
        r.push_back({"driveline", driveline(u, W, H)});
        r.push_back({"tag", col.tag});
        r.push_back({"fuel", col.fuel});
        r.push_back({"anchors", col.anchors});
        r.push_back({"compass", compass(u, W)});
        r.push_back({"level", levelPanel(u, W)});
        r.push_back({"card", card(u, W, H, 70, true, false)});
        // chat: seven lines, as wide as it's allowed
        const float cs = px(1.2f * u), lh = 8 * cs + 5 * u;
        r.push_back({"chat", {14 * u - 4 * u, H - 250 * u - 7 * lh - 2 * u, chatMaxW(u, W) + 8 * u, 7 * lh + lh + 8 * u}});
        // bottom centre: the prompt stack (a long line in each of two slots), top right: FPS block and eight players under it
        r.push_back({"prompt0", prompt(u, W, H, 0, 72)});
        r.push_back({"prompt1", prompt(u, W, H, 1, 52)});
        r.push_back({"fps", fpsBlock(u, W, 33)});
        r.push_back({"players", playersList(u, W, 8, 30, true)});
        TopLeftState tl;
        tl.devKeys = true;
        for (int k = 0; k < TL_COUNT; k++) tl.on[k] = true;
        static const char* names[TL_COUNT] = {"playtest", "cash", "job", "winch"};
        for (int k = 0; k < TL_COUNT; k++) r.push_back({names[k], {12 * u, topLeftY(tl, k, u), topLeftMaxW(u, W), topLeftHeight(k, u)}});
        for (size_t a = 0; a < r.size(); a++) {
            const Rect& q = r[a].second;
            if (q.x < 0 || q.y < 0 || q.r() > W || q.b() > H) {
                bad++;
                if (first.empty()) first = r[a].first + " off screen at " + std::to_string(W) + "x" + std::to_string(H);
            }
            for (size_t b = a + 1; b < r.size(); b++)
                if (overlap(q, r[b].second)) {
                    bad++;
                    if (first.empty()) first = r[a].first + " overlaps " + r[b].first + " at " + std::to_string(W) + "x" + std::to_string(H);
                }
        }
    }
    ctx.check("no HUD rect overlaps another or leaves the screen", bad == 0, first);
    // The bug this came from: the fuel gauge sat on the terrain tag.
    const float u = 1;
    const Column c = rightColumn(u, 1280, 720, 30, 12, 0);
    ctx.check("the fuel gauge is wholly above the terrain tag", c.fuel.b() < c.tag.y);
    ctx.check("the terrain tag is wholly above the dial", c.tag.b() < dial(u, 1280, 720).y);
    // A line longer than the column shrinks to fit, and is clipped only past scale 1.
    const Column wide = rightColumn(2, 2560, 1440, 200, 200, 200);
    ctx.check("long lines stay inside the column", wide.tag.w <= kColW * 2 && wide.fuel.w <= kColW * 2 + 1 && wide.anchors.w <= kColW * 2);
}

SCENARIO(hudLayoutPrompts, "hud/layout-prompts", "The bottom-centre prompts stay inside their gap, in order, at every size; the players list sits under the FPS block; the dial is 200u across") {
    const int sizes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {2560, 1600}, {1024, 768}, {1366, 768}, {1440, 900}};
    int bad = 0;
    std::string first;
    for (const auto& sz : sizes) {
        const int W = sz[0], H = sz[1];
        const float u = uiScale(H);
        float s0 = 0;
        const Rect p0 = prompt(u, W, H, 0, 90, &s0), p1 = prompt(u, W, H, 1, 90);
        if (p0.w > promptMaxW(u, W, H) + 0.5f || p0.x < 0 || p1.y >= p0.y || overlap(p0, p1) || p0.y < 0) bad++, first = "prompts at " + std::to_string(W) + "x" + std::to_string(H);
        if (s0 < 1 || s0 > 1.6f * u + 1) bad++, first = "prompt scale at " + std::to_string(W);
        if (std::fabs(prompt(u, W, H, 0, 20).x + prompt(u, W, H, 0, 20).w / 2 - W / 2.0f) > 0.5f) bad++, first = "prompt not centred";
        if (overlap(fpsBlock(u, W, 33), playersList(u, W, 1, 30, true))) bad++, first = "players under fps at " + std::to_string(W);
        if (playersList(u, W, 8, 30, false).y < 30 * u) bad++, first = "players without fps";
        if (std::fabs(dial(u, W, H).w - 200 * u) > 0.5f) bad++, first = "dial size";
    }
    ctx.check("prompts, players list and dial sit where the layout says", bad == 0, first);
}

// ---- the contextual HUD's rules (hudcontext.h)
namespace {
hudctx::Sample drivingSample() {
    hudctx::Sample s;
    s.fuelFrac = 0.8f, s.fuelL = 80, s.speed = 15, s.surface = 2, s.hasDriveline = true, s.mode = 1;
    s.psi = s.streetPsi = 32;
    return s;
}
// Run `secs` of frames with the sample.
void run(hudctx::Context& c, const hudctx::Sample& s, float secs) {
    for (int k = 0; k < (int)(secs * 60); k++) c.update(s, 1 / 60.0f);
}
}  // namespace

SCENARIO(hudContext, "hud/context", "Driving normally shows none of the optional pieces; low fuel, a hit, a surface change, a mode change, pressure, anchors and stopping each bring theirs up and fade it out") {
    using namespace hudctx;
    Context c;
    Sample s = drivingSample();
    run(c, s, 8);  // (past the first look)
    bool quiet = true;
    for (int e = 0; e < E_COUNT; e++) quiet &= c.alpha(e) == 0;
    ctx.check("driving normally: every optional piece is faded out", quiet);

    s.fuelFrac = 0.2f, s.fuelL = 20;
    run(c, s, 1);
    ctx.check("under 25% fuel: the gauge is up and stays up", c.alpha(E_FUEL) > 0.99f);
    run(c, s, 30);
    ctx.check("... as long as it is low", c.alpha(E_FUEL) > 0.99f);
    s.fuelFrac = 0.8f, s.fuelL = 80;  // (refuelled)
    run(c, s, 1);
    ctx.check("refuelling brings it up for a few seconds", c.alpha(E_FUEL) > 0.99f);
    run(c, s, 7);
    ctx.check("... then it fades", c.alpha(E_FUEL) == 0);

    s.damage = 0.2f;
    run(c, s, 0.5f);
    ctx.check("a hit shows the damage line", c.alpha(E_DAMAGE) > 0.99f);
    run(c, s, 8);
    ctx.check("... which fades", c.alpha(E_DAMAGE) == 0);
    s.damage = 0.7f;
    run(c, s, 8);
    ctx.check("bad damage (50% or more) stays up", c.alpha(E_DAMAGE) > 0.99f);
    s.damage = 0;

    s.surface = 4;
    run(c, s, 1);
    ctx.check("a new surface shows the terrain tag", c.alpha(E_TERRAIN) > 0.99f);
    run(c, s, 8);
    ctx.check("... then fades", c.alpha(E_TERRAIN) == 0);
    s.sunk = true;
    run(c, s, 8);
    ctx.check("sunk in it: the tag stays", c.alpha(E_TERRAIN) > 0.99f);
    s.sunk = false;
    run(c, s, 8);

    s.mode = 2;
    run(c, s, 1);
    ctx.check("a drive mode change shows the 4x4 widget", c.alpha(E_DRIVELINE) > 0.99f);
    run(c, s, 8);
    ctx.check("... then fades", c.alpha(E_DRIVELINE) == 0);
    s.lowRange = true;
    run(c, s, 8);
    ctx.check("4L stays up", c.alpha(E_DRIVELINE) > 0.99f);
    s.lowRange = false;
    s.lockR = true;
    run(c, s, 8);
    ctx.check("a locked axle stays up", c.alpha(E_DRIVELINE) > 0.99f);
    s.lockR = false;
    run(c, s, 8);

    s.psi = 20;
    run(c, s, 8);
    ctx.check("aired down: the pressure stays", c.alpha(E_PRESSURE) > 0.99f);
    s.psi = 32;
    run(c, s, 8);
    ctx.check("back at street pressure it goes", c.alpha(E_PRESSURE) == 0);

    s.anchorSig = 7;
    run(c, s, 1);
    ctx.check("an anchor count change shows the inventory line", c.alpha(E_ANCHORS) > 0.99f);
    run(c, s, 8);
    s.nearTruck = true;
    run(c, s, 1);
    ctx.check("on foot by the truck it shows", c.alpha(E_ANCHORS) > 0.99f);
    s.nearTruck = false;
    s.trunkOpen = true;
    run(c, s, 8);
    ctx.check("in the trunk it stays", c.alpha(E_ANCHORS) > 0.99f);
    s.trunkOpen = false;
    run(c, s, 8);

    s.speed = 0;
    run(c, s, 1);
    ctx.check("stopped: fuel and the car's weight line show", c.alpha(E_FUEL) > 0.99f && c.alpha(E_CARINFO) > 0.99f);
    s.speed = 0.6f;  // (a crawl between the two thresholds does not flicker)
    run(c, s, 1);
    ctx.check("a crawl keeps them (hysteresis)", c.alpha(E_FUEL) > 0.99f);
    s.speed = 10;
    run(c, s, 8);
    ctx.check("moving again, they fade", c.alpha(E_FUEL) == 0 && c.alpha(E_CARINFO) == 0);

    s.fuelFrac = 0.01f;
    s.outOfFuel = true;
    run(c, s, 1);
    ctx.check("out of fuel is never hidden", c.alpha(E_FUEL) > 0.99f);
}

SCENARIO(hudFull, "hud/context-full", "SHOW FULL HUD (the setting or the held key) brings up every piece, and releasing the key fades them; fades are monotonic with no pops") {
    using namespace hudctx;
    Context c;
    const Sample s = drivingSample();
    run(c, s, 8);
    c.holdFull = true;
    run(c, s, 0.5f);
    bool all = true;
    for (int e = 0; e < E_COUNT; e++) all &= c.alpha(e) > 0.99f;
    ctx.check("holding the key shows everything within half a second", all);
    c.holdFull = false;
    run(c, s, 1.2f);
    bool none = true;
    for (int e = 0; e < E_COUNT; e++) none &= c.alpha(e) == 0;
    ctx.check("letting go fades everything out again", none);
    c.settingFull = true;
    run(c, s, 8);
    all = true;
    for (int e = 0; e < E_COUNT; e++) all &= c.alpha(e) > 0.99f;
    ctx.check("the setting keeps everything up", all);
    // A fade-out never jumps by more than a frame's share, and never goes up on its own.
    c.settingFull = false;
    float prev = c.alpha(E_FUEL), worst = 0;
    bool rises = false;
    for (int k = 0; k < 120; k++) {
        c.update(s, 1 / 60.0f);
        const float a = c.alpha(E_FUEL);
        worst = std::max(worst, prev - a);
        rises |= a > prev + 1e-6f;
        prev = a;
    }
    ctx.check("a fade out is smooth (no frame drops more than 6% of full)", worst < 0.06f && !rises, std::to_string(worst));
    ctx.check("a new vehicle shows what it has for a moment", [] {
        Context d;
        Sample t = drivingSample();
        run(d, t, 8);
        t.carId = 3;
        run(d, t, 1);
        return d.alpha(E_TERRAIN) > 0.99f && d.alpha(E_DRIVELINE) > 0.99f;
    }());
}
