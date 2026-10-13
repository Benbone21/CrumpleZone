// Time of day: where the sun, moon and stars are, and the light, sky colors and
// exposure that go with them. The game runs the clock; the renderer reads a Daylight.
#pragma once
#include "mathx.h"

// The pause menu's TIME OF DAY setting.
enum TimeOfDay { TOD_CYCLE = 0, TOD_MORNING, TOD_NOON, TOD_EVENING, TOD_NIGHT, TOD_COUNT };
const char* timeOfDayName(int t);
double timeOfDayHours(int t);  // clock time of the fixed settings (CYCLE: -1)

struct Daylight {
    double hours = 12;      // 0..24
    V3 sunDir, moonDir;     // toward the sun and moon (below the horizon at night / day)
    V3 keyDir;              // the light that shades and casts shadows: the sun, or the moon at night
    V3 keyCol;              // its color x intensity (linear HDR)
    V3 ambSky, ambGround;   // ambient light from above and below
    V3 skyZen, skyHor;      // sky gradient, zenith to horizon
    V3 duskCol;             // glow along the horizon on the sun's side at sunrise and sunset
    V3 sunGlow;             // halo round the sun
    V3 sunDisc;             // the sun's disk (0 below the horizon)
    V3 cloudLit, cloudDark; // clouds facing the key light, and in their own shade
    V3 moonCol;             // the moon's disk, fully lit
    V3 groundCol;           // what the sky shows below the horizon
    double skyRot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // world direction -> star sphere (row major)
    float stars = 0;        // how much of the starfield shows, 0..1
    float night = 0;        // 0 day .. 1 full night (exposure, the automatic headlights)
    float moonPhase = 0.55f;  // 0 new, 0.5 full
    float exposure = 0.85f;
};

// The sky at `hours` (0..24).
Daylight computeDaylight(double hours);

// A day of `dayMinutes` real minutes: how many clock hours pass in dt seconds.
inline double clockAdvance(double dt, double dayMinutes) { return dt * 24.0 / (std::max(1.0, dayMinutes) * 60.0); }
