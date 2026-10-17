// F3: the debug screen, drawn over the game like Minecraft's: the game and the world on the left, the vehicle (or you,
// on foot) and the network on the right, one dark strip behind each line. The numbers come from gatherDebugInfo()
// (debuginfo.h); this paints them, and keeps the little state they need: the frame times, and the slow readings
// (memory, biome, nearest place, network rates) that are only refreshed twice a second.
#pragma once
#include "debuginfo.h"

class Renderer;

class DebugScreen {
public:
    bool on = false;
    bool graph = false;  // Shift+F3: the frame-time graph along the bottom

    // F3 toggles the screen; Shift+F3 toggles the frame graph (and brings the screen up).
    void toggle(bool shift);
    // Every frame while it's up: `ms` is the frame's time. True about twice a second: time to refresh the slow readings in `in`.
    bool tick(double ms, double dt);
    // Fills the slow readings that need nothing from the game: memory and network rates.
    void readSlow(DebugInputs& in, double dt);
    // Paints it. `top` is how far down the left column starts (px), so it clears a panel in the corner.
    void draw(Renderer& r, const DebugInputs& in, int W, int H, float top);

    FrameHistory history;
    // The most recent readings, for the lines and for tests.
    DebugColumn left, right;
    // The slow readings (refreshed by the game when tick() says so).
    int biome = 0;
    char place[64] = "";
    double placeDist = 0;
private:
    double slowTimer_ = 1e9;
    long rss_ = -1, peak_ = -1;
    uint64_t lastIn_ = 0, lastOut_ = 0;
    double rateIn_ = 0, rateOut_ = 0, rateTimer_ = 0;
};
