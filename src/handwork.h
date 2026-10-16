// The work in front of you, in first person: what the job you're holding E on looks like while
// it's being done. Just the tools, working on the car (no hands, no arms): the lug wrench ratcheting
// round the nuts (its socket on each one, the handle swinging about it) and the wheel sliding off
// its studs; the wheel you carry going onto the hub; a chain wrapping round the tread; the jack
// going under a corner handle-out; a socket set, a hammer, a pry bar at a broken corner and the
// new part going in; a pry bar at a supply crate's lid. And what you carry while you walk: the jack, a chain.
#pragma once
#include <vector>

#include "interact.h"
#include "render.h"

struct Handwork {
    std::vector<Placed> models;     // tools, a part, the jack: drawn this frame
    Renderer::WheelWork wheel;      // a wheel slid out / a chain part on
    bool carriedPose = false;       // the wheel you carry, moved onto the hub
    V3 carriedC, carriedAxis;
};

// eye, look: the first-person view; time: seconds (for the to-and-fro of the work).
Handwork handwork(const Sim& sim, const Walker& wk, const Interact& ia, const V3& eye, const V3& look, double time);
