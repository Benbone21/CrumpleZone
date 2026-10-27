#include "daylight.h"

namespace {

// The sky's place: 38 degrees north (the compass's north is +z, east +x), early summer
// (the sun 12 degrees north of the equator: up at about 5:20, down at about 18:40, 64
// degrees high at noon). The moon is just past full and low in the south, as a summer
// full moon is (declination -15): it rises an hour after sunset and is up all night.
const double kLat = 38 * M_PI / 180, kSunDecl = 12 * M_PI / 180, kMoonDecl = -15 * M_PI / 180;
const double kMoonPhase = 0.55;  // 0 new, 0.5 full: 198 degrees from the sun

// Direction of a body at declination `decl` and hour angle `H` (0 = due south, at its
// highest), in world axes: x east, y up, z north.
V3 skyDir(double decl, double H) {
    const double e = -std::cos(decl) * std::sin(H);
    const double n = std::cos(kLat) * std::sin(decl) - std::sin(kLat) * std::cos(decl) * std::cos(H);
    const double u = std::sin(kLat) * std::sin(decl) + std::cos(kLat) * std::cos(decl) * std::cos(H);
    return V3(e, u, n);
}

// Sky and light keyframes against the sun's height (sine of its elevation): from deep
// night through twilight, sunset and golden hour to high day. Linear HDR.
struct Key {
    double e;
    V3 zen, hor, ambSky, ambGround, dusk, cloudLit, cloudDark;
    double exposure;
};
const Key kKeys[] = {
    {-0.30, {0.0016, 0.0030, 0.0085}, {0.0060, 0.0085, 0.0150}, {0.015, 0.019, 0.032}, {0.004, 0.005, 0.006},
     {0, 0, 0}, {0.016, 0.018, 0.025}, {0.006, 0.007, 0.010}, 1.7},
    {-0.12, {0.0045, 0.0090, 0.0270}, {0.0220, 0.0250, 0.0400}, {0.032, 0.036, 0.056}, {0.010, 0.010, 0.012},
     {0.05, 0.022, 0.018}, {0.05, 0.045, 0.06}, {0.014, 0.015, 0.022}, 1.9},
    {-0.04, {0.018, 0.035, 0.100}, {0.17, 0.12, 0.13}, {0.08, 0.085, 0.12}, {0.032, 0.030, 0.032},
     {0.42, 0.13, 0.05}, {0.55, 0.22, 0.18}, {0.07, 0.06, 0.09}, 1.6},
    {0.02, {0.040, 0.080, 0.220}, {0.42, 0.29, 0.24}, {0.16, 0.16, 0.22}, {0.08, 0.06, 0.05},
     {0.85, 0.32, 0.08}, {1.20, 0.55, 0.30}, {0.20, 0.16, 0.22}, 1.0},
    {0.10, {0.060, 0.140, 0.380}, {0.55, 0.52, 0.50}, {0.27, 0.30, 0.38}, {0.14, 0.12, 0.09},
     {0.30, 0.14, 0.04}, {1.30, 0.95, 0.65}, {0.34, 0.33, 0.40}, 0.9},
    {0.25, {0.070, 0.180, 0.480}, {0.52, 0.60, 0.72}, {0.34, 0.42, 0.57}, {0.16, 0.15, 0.12},
     {0.05, 0.03, 0.0}, {1.25, 1.15, 1.00}, {0.40, 0.43, 0.52}, 0.85},
    {0.60, {0.075, 0.200, 0.520}, {0.50, 0.60, 0.74}, {0.36, 0.45, 0.62}, {0.17, 0.16, 0.13},
     {0, 0, 0}, {1.25, 1.18, 1.08}, {0.40, 0.45, 0.54}, 0.85},
};

V3 mixv(const V3& a, const V3& b, double t) { return a + (b - a) * t; }

}  // namespace

const char* timeOfDayName(int t) {
    static const char* names[TOD_COUNT] = {"CYCLE", "MORNING", "NOON", "EVENING", "NIGHT"};
    return names[t < 0 || t >= TOD_COUNT ? 0 : t];
}

double timeOfDayHours(int t) {
    // Morning: low sun from the east. Noon: just past, so it isn't straight overhead.
    // Evening: the last minutes of sun, red and long. Night: the moon well up.
    static const double hours[TOD_COUNT] = {-1, 7.2, 13.0, 18.0, 23.0};
    return hours[t < 0 || t >= TOD_COUNT ? 0 : t];
}

Daylight computeDaylight(double hours) {
    Daylight d;
    hours = std::fmod(std::fmod(hours, 24.0) + 24.0, 24.0);
    d.hours = hours;
    const double H = (hours - 12) / 24 * 2 * M_PI;  // the sun's hour angle
    d.sunDir = skyDir(kSunDecl, H);
    d.moonDir = skyDir(kMoonDecl, H - kMoonPhase * 2 * M_PI);
    d.moonPhase = (float)kMoonPhase;
    const double e = d.sunDir.y, m = d.moonDir.y;

    // Sky colors from the keyframes.
    const int nk = sizeof(kKeys) / sizeof(kKeys[0]);
    int i = 0;
    while (i < nk - 2 && e > kKeys[i + 1].e) i++;
    const Key &a = kKeys[i], &b = kKeys[i + 1];
    const double t = clampd((e - a.e) / (b.e - a.e), 0, 1);
    d.skyZen = mixv(a.zen, b.zen, t);
    d.skyHor = mixv(a.hor, b.hor, t);
    d.ambSky = mixv(a.ambSky, b.ambSky, t);
    d.ambGround = mixv(a.ambGround, b.ambGround, t);
    d.duskCol = mixv(a.dusk, b.dusk, t);
    d.cloudLit = mixv(a.cloudLit, b.cloudLit, t);
    d.cloudDark = mixv(a.cloudDark, b.cloudDark, t);
    d.exposure = (float)(a.exposure + (b.exposure - a.exposure) * t);
    d.night = (float)(1 - smoothstep(-0.20, 0.03, e));
    d.stars = (float)(1 - smoothstep(-0.16, -0.03, e));

    // The sun: white-gold high up, orange and weaker through more air near the horizon.
    const double sunUp = smoothstep(-0.02, 0.06, e);
    const V3 sunCol = mixv(V3(1.0, 0.45, 0.16), V3(1.0, 0.93, 0.80), smoothstep(0.0, 0.40, e)) *
                      (2.6 * sunUp * (0.45 + 0.55 * smoothstep(0.0, 0.35, e)));
    d.sunGlow = mixv(V3(1.0, 0.45, 0.15), V3(1.0, 0.78, 0.50), smoothstep(0.0, 0.3, e)) * smoothstep(-0.10, 0.02, e);
    d.sunDisc = mixv(V3(1.0, 0.42, 0.12) * 20.0, V3(1.0, 0.92, 0.8) * 40.0, smoothstep(0.0, 0.25, e));

    // The moon: lit by the sun as seen from here (phase), bluish once the eye adapts to
    // the dark. It takes over as the key light once the sun's light has gone.
    const double moonLit = 0.5 * (1 - std::cos(2 * M_PI * kMoonPhase));
    const double moonUp = smoothstep(-0.02, 0.12, m);
    const double moonKey = moonUp * moonLit * smoothstep(-0.02, -0.12, e);
    const V3 moonLight = V3(0.50, 0.62, 0.95) * (0.17 * moonKey);
    d.moonCol = V3(0.90, 0.92, 0.95) * (1 + 0.3 * d.night);  // (brighter against the dark: the eye has adapted)
    // Moonlight brightens the night sky a little, and a moonlit sky hides the faint stars.
    const double moonSky = moonUp * moonLit * d.night;
    d.ambSky += V3(0.010, 0.014, 0.026) * moonSky;
    d.skyZen += V3(0.0015, 0.0030, 0.0065) * moonSky;
    d.skyHor += V3(0.0040, 0.0060, 0.0100) * moonSky;
    d.cloudLit += V3(0.08, 0.10, 0.14) * moonSky;
    d.cloudDark += V3(0.006, 0.008, 0.012) * moonSky;
    d.stars *= (float)(1 - 0.35 * moonSky);

    // Key light: the sun while it's up, the moon after. Both are dark at the hand-over.
    // Kept 6 degrees above the horizon: grazing light only smears the shadow maps.
    const bool sunKey = e > -0.02;
    V3 key = sunKey ? d.sunDir : d.moonDir;
    d.keyCol = sunKey ? sunCol : moonLight;
    V3 flat(key.x, 0, key.z);
    if (len(flat) < 1e-6) flat = V3(1, 0, 0);
    flat = norm(flat);
    const double el = std::max(std::asin(clampd(key.y, -1, 1)), 6 * M_PI / 180);
    d.keyDir = flat * std::cos(el) + V3(0, std::sin(el), 0);

    d.groundCol = mixv(V3(0.004, 0.005, 0.006), V3(0.06, 0.065, 0.05), smoothstep(-0.15, 0.15, e));

    // Star sphere: world direction -> the hour-angle frame (z to the celestial pole,
    // x to the meridian), then turned with the sun so the stars wheel round the pole.
    const double sl = std::sin(kLat), cl = std::cos(kLat), c = std::cos(H), s = std::sin(H);
    // q.x = cl*y - sl*z, q.y = -x, q.z = sl*y + cl*z (from skyDir's inverse); then rotate by H.
    const double A[9] = {0, cl, -sl, -1, 0, 0, 0, sl, cl};
    const double R[9] = {c, s, 0, -s, c, 0, 0, 0, 1};  // by -H: a star keeps its place relative to the sun
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 3; k++) d.skyRot[r * 3 + k] = R[r * 3] * A[k] + R[r * 3 + 1] * A[3 + k] + R[r * 3 + 2] * A[6 + k];
    return d;
}
