#include "toolsounds.h"

#include <algorithm>
#include <cmath>

#include "fixes.h"

void ToolSounds::update(const Play& play, const Interact& ia, const Handwork& hw, const V3& listener) {
    const InteractOption& o = ia.current;
    const bool working = ia.progress > 0 && o.kind != IA_NONE && !o.blocked;
    const double secs = ia.progress, share = ia.share();
    const bool same = working_ && working && o.kind == kind_ && o.car == car_ && o.wheel == wheel_ && o.step == step_ && o.tool == tool_;
    const V3 at = hw.hasWork ? hw.workAt : listener;
    auto pitch = [&](float spread) { return 1.0f - spread * 0.5f + spread * rnd(); };
    auto bolt = [&]() { play(SFX_BOLT_DROP, at - V3(0, 0.35, 0), 0.45f, 0.05f, pitch(0.25f)); };
    if (working_ && !same) {  // the last job ended or changed
        const bool finished = !working || share_ > 0.8;
        if (lastNut_ >= 0 && lastOff_ && share_ > 0.8) bolt();  // (the last nut off)
        if (kind_ == IA_LOWER_JACK && secs_ > 0.2) play(SFX_CLUNK, at, 0.8f, 0, 0.6f);  // (the tire meets the ground)
        if (kind_ == IA_PLACE_JACK && share_ > 0.85) play(SFX_CLUNK, at, 0.6f, 0, 0.8f);  // (the jack settles)
        (void)finished;
        lastNut_ = -1, beat_ = -1, lastSwing_ = -1;
    }
    working_ = working;
    if (!working) {
        share_ = secs_ = 0;
        return;
    }
    if (!same) kind_ = o.kind, car_ = o.car, wheel_ = o.wheel, step_ = o.step, tool_ = o.tool, lastNut_ = -1, beat_ = -1, lastSwing_ = -1;
    // Wrenches: a tick as the free swing back begins; a nut that breaks loose squeals; a nut taken off drops.
    if (hw.nut != lastNut_) {
        if (lastNut_ >= 0 && lastOff_) bolt();
        if (hw.nut >= 0 && hw.nutOff) play(SFX_NUT_SQUEAL, at, 0.3f, 0.03f, 0.85f + 0.4f * rnd());
        lastNut_ = hw.nut, lastOff_ = hw.nutOff;
    }
    if (hw.swing >= 0 && lastSwing_ >= 0 && lastSwing_ < 0.7 && hw.swing >= 0.7) {
        const V3 hand = hw.hands[1].on ? hw.hands[1].p : at;
        play(SFX_WRENCH, hand, 0.55f, 0, pitch(0.1f));
    }
    lastSwing_ = hw.swing;
    // Beat-driven sounds: `period` s apart, the first now (or not).
    auto beat = [&](double period, bool first) {
        const int b = (int)(secs / period);
        const bool fire = beat_ != b && (beat_ >= 0 || first);
        beat_ = b;
        return fire;
    };
    switch (o.kind) {
    case IA_RAISE_JACK:
        if (beat(0.5, true)) play(SFX_JACK_PUMP, at, 0.7f, 0, std::min(1.25f, 0.92f + (float)secs * 0.04f) * pitch(0.08f));
        break;
    case IA_LOWER_JACK:
        if (beat(0.4, true)) play(SFX_JACK_LOWER, at, 0.6f, 0, pitch(0.15f));
        break;
    case IA_REMOVE_WHEEL:
        if (share_ < 0.9 && share >= 0.9) play(SFX_WHEEL_OFF, at, 0.9f, 0, pitch(0.1f));
        break;
    case IA_FIT_WHEEL:
        if (share_ < 0.12 && share >= 0.12) play(SFX_WHEEL_ON, at, 0.8f, 0, pitch(0.1f));
        break;
    case IA_OPEN_CACHE:
        if (beat(1.1, true)) play(SFX_PRY_CREAK, at, 0.6f, 0, pitch(0.15f));
        break;
    case IA_FIX:
    case IA_TIRE_WORK:
        if (o.tool < 0 || o.part >= 0) break;
        if (o.tool == TOOL_HAMMER) {
            if (beat(0.4, false)) play(SFX_HAMMER, at, 0.8f, 0, pitch(0.18f));
        } else if (o.tool == TOOL_PRY_BAR) {
            if (beat(1.4, true)) play(SFX_PRY_CREAK, at, 0.6f, 0, pitch(0.15f));
        } else if (o.tool == TOOL_TIRE_IRONS) {
            if (beat(0.7, true)) play(SFX_TIRE_IRON, at, 0.7f, 0, pitch(0.2f));
        }
        break;
    default: break;
    }
    share_ = share, secs_ = secs;
}
