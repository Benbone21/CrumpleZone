// Vehicle lamps: where each car's headlamps and tail lamps sit on its body as it moves
// and crumples, whether they survived the last crash, and which way the beams point.
// Rendering only: nothing here touches the physics.
#pragma once
#include "softbody.h"

struct VisualTemplate;

// The driver's headlight switch (Shift+L cycles it). AUTO turns the low beams on at dusk.
enum LampMode { LAMPS_AUTO = 0, LAMPS_LOW, LAMPS_HIGH, LAMPS_OFF, LAMP_MODES };
const char* lampModeName(int mode);
// The beam a switch position gives: 0 off, 1 low, 2 high. `night` is Daylight::night.
int lampBeam(int mode, float night);

struct CarLamps {
    int beam = 0;            // 0 off, 1 low, 2 high
    V3 headPos[2];           // headlamp centers, left and right (world)
    V3 tailPos[2];           // tail lamp centers (world)
    V3 fwd, up;              // which way the car points (the beams follow its pitch and roll)
    float head[2] = {1, 1};  // headlamps that still work: 1 whole, 0 smashed
    float tail[2] = {1, 1};  // tail lamps that still work
    bool placed = false;     // headPos is known (vehicles from the catalog)
};

// Finds this car's lamps on its body now (`beam` is what's switched on).
void carLamps(const Car& car, const VisualTemplate& t, int beam, CarLamps& out);
