// The work in front of you, in first person: what the job you're holding E on looks like while
// it's being done. The tools working on the car, and where your hands go on them (people.h draws them): the lug wrench ratcheting
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
    double swing = -1;              // a wrench is turning a nut: how far through its swing (0..1; the free swing back starts at 0.7); -1 if none
    // For the repair sounds (toolsounds.h): where the work is happening (a nut, the jack's saddle, the wheel's hub), and which nut a
    // wrench is on (a count up through the job; -1 none) and whether it's coming off.
    bool hasWork = false;
    V3 workAt;
    int nut = -1;
    bool nutOff = false;
    bool hideTorso = false;         // first person: leave your chest out of the picture (both hands on a low job)
    bool carriedPose = false;       // the wheel you carry, moved onto the hub
    V3 carriedC, carriedAxis;
    // Where your hands are (left, right): on a tool's grip (axis: along the handle, toward its head), the jack's bar,
    // the chain, the part going in. Off: the hand's free (people.h swings it).
    struct Hold {
        bool on = false;
        V3 p, axis;
        double grip = 0.95;
    } hands[2];
};

// eye, look: the first-person view; time: seconds (for the to-and-fro of the work). `mine`: false for another player's
// work (repairsync.h: their walker and job as they told it), which leaves out this machine's line in hand (Sim::handLine).
Handwork handwork(const Sim& sim, const Walker& wk, const Interact& ia, const V3& eye, const V3& look, double time, bool mine = true);
