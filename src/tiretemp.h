// Tire temperature: how warm the tread is changes how well the rubber grips. Each compound
// has its window: a race slick's is high and narrow (cold, it's a hard plastic puck), a
// semi-slick's lower, a street or all-terrain tire's wide and forgiving (cold off the drive on
// a mild day it grips ~3% less, and full once a few minutes' driving has it at ~40C; on a
// frosty morning ~10% less), and a winter tire's soft compound works best cold and goes greasy
// when warm. Past the window the rubber overheats: it greases off and the carcass is at risk.
//
// Grip from temperature (x the tread's own grip), at:   -20C  0C  20C  50C  80C  100C  130C
//   street, all-terrain, mud-terrain                    .86  .91  .97  1.0  1.0  1.0   .90
//   semi-slick                                          .84  .89  .97  1.0  1.0  1.0   .93
//   race slick                                          .72  .72  .78  .94  1.0  1.0   .95
//   winter                                              1.0  1.0  1.0  .90  .80  .80   .80
//   chains (steel does the gripping)                    1.0 everywhere
// (All of it on a hard face, ~60% on dirt, gravel and grass, ~40% in sand, mud and snow,
// where the ground shears before the rubber does.) Warm-up: ~6 kJ/K of tread against a third
// of the slip work, the carcass flexing and the air and ground carrying it off: half a
// minute of wheelspin brings a tire up ~30C, a long run at 120 km/h settles ~25-45C over the
// air over a few minutes, and parked it cools back over ~5 minutes.
#pragma once
#include "mathx.h"
#include "vehicles.h"

struct TireWindow {
    double lo, hi;          // the window: full grip between these (C)
    double coldSpan, coldMax;  // below lo: grip falls off over coldSpan C, down by coldMax
    double hotSpan, hotMax;    // above hi: the same, overheating
};

inline const TireWindow& tireWindow(int tread) {
    static const TireWindow w[TIRE_TYPES] = {
        {40, 100, 70, 0.15, 50, 0.15},   // street
        {35, 105, 55, 0.16, 50, 0.15},   // semi-slick
        {40, 100, 70, 0.15, 50, 0.15},   // all-terrain
        {40, 100, 70, 0.15, 50, 0.15},   // mud-terrain
        {75, 115, 80, 0.28, 45, 0.20},   // race slick
        {-40, 25, 30, 0.10, 50, 0.20},   // winter
        {-99, 999, 1, 0, 1, 0},          // chains
    };
    return w[tread >= 0 && tread < TIRE_TYPES ? tread : 0];
}

// Grip from the tread's temperature (1 in the window).
inline double tireTempGrip(int tread, double t) {
    const TireWindow& w = tireWindow(tread);
    if (t < w.lo) return 1 - w.coldMax * smoothstep(0, w.coldSpan, w.lo - t);
    if (t > w.hi) return 1 - w.hotMax * smoothstep(0, w.hotSpan, t - w.hi);
    return 1;
}

// -1 cold (losing more than 3%), +1 overheating, 0 in the window: the HUD's blue / green / red.
inline int tireTempBand(int tread, double t) {
    const TireWindow& w = tireWindow(tread);
    if (tireTempGrip(tread, t) > 0.97) return 0;
    return t < w.lo ? -1 : 1;
}

// How far past its window the tread is, 0..1 (1: hotSpan over hi): for the carcass (blowouts).
inline double tireOverheat(int tread, double t) {
    const TireWindow& w = tireWindow(tread);
    return clampd((t - w.hi) / w.hotSpan, 0, 1);
}

// The air temperature (C) by the country (biome.h) and the clock: desert hot, alpine and
// snow below freezing, woods and water cool; ~5C colder at night than at mid-afternoon.
inline double ambientTempC(int biome, double hours) {
    static const double base[] = {20, 36, 14, 30, 16, -8};  // default, desert, pine, canyon, river, alpine
    const double b = base[biome >= 0 && biome < 6 ? biome : 0];
    // Coolest at ~5:00, warmest at ~15:00 (0 at noon, so the tests' midday sky is the base).
    const double day = std::cos((hours - 15.0) / 24.0 * 2 * M_PI);
    const double noon = std::cos((12.0 - 15.0) / 24.0 * 2 * M_PI);
    return b + 3.5 * (day - noon);
}
