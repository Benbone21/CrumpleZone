// The sounds of repair work: what each tool and step makes, at the place the work is happening (handwork.h tells where). Fed the
// job you're holding E on each frame, it plays through Audio (3D: louder near, panned to the side the work is on):
//   wrench ticks on each return stroke, a seized nut's squeal as a nut breaks loose, a nut dropping when it comes off;
//   hammer blows; the jack's pawl clicking up, the saddle groaning down and a thunk when the tire meets the ground;
//   a pry bar's creak; a tire iron's clink; a wheel thumping off its studs and clinking back on.
// Only your own job is seen here: other players' repairs aren't visible to this client yet (the multiplayer sync, a separate task).
#pragma once
#include "audio.h"
#include "handwork.h"
#include "interact.h"

class ToolSounds {
public:
    // Call once a frame with the current job (`ia`), what handwork() drew for it (`hw`) and the listener's place and right-hand direction.
    void update(Audio& audio, const Interact& ia, const Handwork& hw, const V3& listener, const V3& right);

private:
    bool working_ = false;
    InteractKind kind_ = IA_NONE;
    int car_ = -1, wheel_ = -1, step_ = -1, tool_ = -1;
    double share_ = 0, secs_ = 0, lastSwing_ = -1;
    int lastNut_ = -1, beat_ = -1;
    bool lastOff_ = false;
    uint32_t rng_ = 0x2545f491u;
    float rnd() { rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5; return (rng_ >> 8) * (1.0f / 16777216.0f); }  // 0..1
};
