#include "debugscreen.h"

#include <algorithm>
#include <cmath>

#include "hudkit.h"
#include "net.h"
#include "render.h"

using namespace hud;

void DebugScreen::toggle(bool shift) {
    if (shift) {
        graph = on ? !graph : true;
        on = true;
    } else on = !on;
    if (on) history.reset(), slowTimer_ = 1e9;
}

bool DebugScreen::tick(double ms, double dt) {
    history.push(ms);
    slowTimer_ += dt;
    if (slowTimer_ < 0.5) return false;
    slowTimer_ = 0;
    processMemory(rss_, peak_);
    return true;
}

void DebugScreen::readSlow(DebugInputs& in, double dt) {
    in.rssKb = rss_, in.peakRssKb = peak_;
    in.biome = biome;
    in.place = place, in.placeDist = placeDist;
    // Network rates from the byte counters, over about a second.
    if (in.net && in.net->active()) {
        rateTimer_ += dt;
        if (rateTimer_ >= 1.0) {
            rateIn_ = (double)(in.net->bytesIn - lastIn_) / 1024.0 / rateTimer_;
            rateOut_ = (double)(in.net->bytesOut - lastOut_) / 1024.0 / rateTimer_;
            lastIn_ = in.net->bytesIn, lastOut_ = in.net->bytesOut;
            rateTimer_ = 0;
        }
    } else lastIn_ = lastOut_ = 0, rateTimer_ = 0;
    in.rateInKB = rateIn_, in.rateOutKB = rateOut_;
}

namespace {

void tone(DebugTone t, float& r, float& g, float& b) {
    switch (t) {
        case DT_HEAD: r = 1.0f, g = 0.78f, b = 0.3f; break;
        case DT_DIM: r = 0.74f, g = 0.77f, b = 0.82f; break;
        case DT_GOOD: r = 0.5f, g = 0.95f, b = 0.55f; break;
        case DT_WARN: r = 1.0f, g = 0.72f, b = 0.28f; break;
        case DT_BAD: r = 1.0f, g = 0.4f, b = 0.35f; break;
        default: r = 0.94f, g = 0.95f, b = 0.97f; break;
    }
}

// One column of strips down the left or right edge; stops where the screen (or the graph) does.
void column(Renderer& r, const DebugColumn& col, bool rightSide, float s, float u, int W, float y, float yEnd) {
    const float lh = 8 * s + 2 * u, pad = 3 * u, edge = 6 * u;
    for (size_t i = 0; i < col.size(); i++) {
        const DebugLine& l = col[i];
        if (!l.text[0]) {  // a gap between groups
            y += lh * 0.5f;
            continue;
        }
        if (y + lh > yEnd) break;
        static thread_local std::string txt;  // (a line a frame: its storage is kept)
        txt.assign(l.text);
        const float w = r.textWidth(txt, s) + 2 * pad;
        const float x = rightSide ? W - edge - w : edge;
        r.rect(x, y, w, lh, 0.02f, 0.025f, 0.035f, 0.7f);
        float cr, cg, cb;
        tone((DebugTone)l.tone, cr, cg, cb);
        r.text(x + pad, y + u, s, txt, cr, cg, cb);
        y += lh;
    }
}

// The frame-time graph: a bar per frame, the newest on the right, with the 60 and 30 FPS lines.
void frameGraph(Renderer& r, const FrameHistory& h, float u, int W, int H) {
    const int n = FrameHistory::kFrames;
    const float bw = std::max(1.0f, std::floor(u)), gh = 72 * u, rangeMs = 50;
    // (Between the vehicle card, bottom left, and the gauge, bottom right.)
    const float w = n * bw, x0 = std::max(6 * u, std::min(735 * u, W - w - 250 * u)), y1 = H - 8 * u, y0 = y1 - gh;
    r.rect(x0 - 4 * u, y0 - 4 * u, w + 8 * u, gh + 8 * u, 0.02f, 0.025f, 0.035f, 0.7f);
    for (int k = 0; k < h.count(); k++) {
        const double ms = h.ms(k);
        const float bh = (float)std::min(1.0, ms / rangeMs) * gh;
        const bool ok = ms <= 17.5, meh = ms <= 34.0;
        r.rect(x0 + w - (k + 1) * bw, y1 - bh, bw, bh, ok ? 0.35f : meh ? 0.95f : 0.95f, ok ? 0.85f : meh ? 0.78f : 0.3f, ok ? 0.4f : meh ? 0.25f : 0.28f, 0.9f);
    }
    const float s = std::max(1.0f, std::round(u));
    for (double fps : {60.0, 30.0}) {
        const float ly = y1 - (float)(1000.0 / fps / rangeMs) * gh;
        r.rect(x0, ly, w, std::max(1.0f, u * 0.5f), 1, 1, 1, 0.55f);
        r.text(x0 + 2 * u, ly - 8 * s - u, s, fps > 45 ? "60 FPS" : "30 FPS", 0.9f, 0.92f, 0.95f);
    }
}

}  // namespace

void DebugScreen::draw(Renderer& r, const DebugInputs& in, int W, int H, float top) {
    gatherDebugInfo(in, left, right);
    const float u = std::max(0.75f, H / 720.0f);
    // The 5x7 font wants whole scales; 2 below ~900 lines tall would be too big for the columns.
    const float s = H >= 900 ? 2.0f : 1.5f;
    const float yEnd = graph ? H - 92 * u : H - 6 * u;
    column(r, left, false, s, u, W, std::max(6 * u, top), yEnd);
    column(r, right, true, s, u, W, 6 * u, yEnd);
    if (graph) frameGraph(r, history, u, W, H);
}
