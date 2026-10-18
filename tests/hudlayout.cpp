// The HUD's layout (hudlayout.h): at every common resolution and aspect ratio, with the longest
// text each element can carry, no two HUD rects touch and all of them are on screen.
// Pure arithmetic: no GL.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

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
