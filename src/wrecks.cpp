// Wrecks out in the country (wrecks.h).
#include "wrecks.h"

#include <algorithm>
#include <cstdlib>

#include "detmath.h"
#include "fixes.h"
#include "modelkit.h"
#include "onemap.h"
#include "procworld.h"
#include "softbody.h"
#include "world.h"

namespace wrecks {

namespace {

constexpr double kDeg = 3.14159265358979323846 / 180;

// A design and its collider: the box round what stands up (the pickup's bits lying about don't count).
struct Kind {
    const char* model;
    int variant;
    V3 center, half;
    bool lying;  // already on its side or roof: not rolled again
    bool big;    // a truck: doesn't end up on its roof
};
const Kind kKinds[] = {
    {"wreck_pickup", 0, V3(0, 0.95, -0.95), V3(2.65, 0.95, 0.95), true, false},
    {"wreck_pickup", 1, V3(0, 0.85, 0), V3(2.65, 0.8, 0.95), true, false},
    {"wreck_pickup", 2, V3(0, 1.0, 0), V3(2.65, 0.7, 0.95), false, false},
    {"wreck_sedan", 0, V3(0, 0.72, 0), V3(2.38, 0.7, 0.88), false, false},
    {"wreck_sedan", 1, V3(0, 0.72, 0), V3(2.38, 0.7, 0.88), false, false},
    {"wreck_sedan", 2, V3(-0.25, 0.72, 0), V3(2.1, 0.7, 0.88), false, false},
    {"wreck_van", 0, V3(0, 1.02, 0), V3(2.42, 1.0, 0.98), false, false},
    {"wreck_van", 1, V3(0, 1.02, 0), V3(2.42, 1.0, 0.98), false, false},
    {"wreck_jeep", 0, V3(-0.05, 0.7, 0), V3(1.85, 0.7, 0.84), false, false},
    {"wreck_jeep", 1, V3(-0.1, 0.94, 0), V3(2.05, 0.93, 0.86), false, false},
    {"wreck_boxtruck", 0, V3(-0.3, 1.52, 0), V3(3.25, 1.5, 1.14), false, true},
    {"wreck_boxtruck", 1, V3(-0.3, 1.52, 0), V3(3.25, 1.5, 1.14), false, true},
    {"wreck_flatbed", 0, V3(-0.3, 1.25, 0), V3(3.05, 1.25, 1.1), false, true},
    {"wreck_flatbed", 1, V3(-0.3, 1.25, 0), V3(3.05, 1.25, 1.1), false, true},
};
constexpr int kKindCount = (int)(sizeof(kKinds) / sizeof(kKinds[0]));

uint32_t mix32(uint32_t h) {
    h ^= h >> 16, h *= 0x7feb352du, h ^= h >> 15, h *= 0x846ca68bu;
    return h ^ (h >> 16);
}

uint64_t seedOf(const World& w) { return w.map == MAP_PROCEDURAL ? w.proc.seed : 0; }

}  // namespace

uint32_t hashOf(const std::string& id, uint64_t seed) {
    uint32_t h = 2166136261u ^ (uint32_t)seed ^ (uint32_t)(seed >> 32);
    for (unsigned char ch : id) h = (h ^ ch) * 16777619u;
    return mix32(h);
}

Look lookOf(uint32_t h) {
    const Kind& k = kKinds[h % kKindCount];
    Look l;
    l.model = k.model, l.variant = k.variant, l.center = k.center, l.half = k.half;
    const uint32_t g = mix32(h + 0x51u);
    const double u = (g >> 8 & 1023) / 1023.0;
    const double sign = g & 1 ? 1 : -1;
    int r = (int)(g >> 20) % 100;
    if (k.big && r >= 55) r = r % 55;  // (a big truck settles or sinks: it doesn't roll about)
    if (k.lying && r >= 84) r = r % 40;
    if (r < 40) l.sink = 0.05;                                                             // sat where it stopped
    else if (r < 55) l.roll = sign * (9 + 15 * u) * kDeg, l.sink = 0.14;                   // leaning into a ditch
    else if (r < 70) l.pitch = -(12 + 16 * u) * kDeg, l.sink = 0.55 + 0.35 * u;            // nose down in the dirt
    else if (r < 78) l.pitch = (9 + 12 * u) * kDeg, l.sink = 0.4 + 0.3 * u;                // tail down
    else if (r < 84) l.sink = 0.35 + 0.3 * u, l.roll = sign * 4 * u * kDeg;                // sunk to the sills
    else if (r < 94) l.roll = sign * (86 + 8 * u) * kDeg, l.sink = 0.1;                    // on its side
    else l.roll = (176 + 8 * u) * kDeg, l.pitch = sign * 3 * kDeg, l.sink = 0.12;          // on its roof
    return l;
}

int modelCount() { return kKindCount; }
const char* modelAt(int i) { return kKinds[i].model; }
int variantAt(int i) { return kKinds[i].variant; }

Pose poseOf(const World& w, const MapPlace& pl) {
    Pose p;
    p.look = lookOf(hashOf(pl.id, seedOf(w)));
    const double yaw = pl.yaw, cy = dm::cos(yaw), sy = dm::sin(yaw);
    const V3 f(cy, 0, -sy), l(sy, 0, cy);
    // The slope under it, across its length and its width.
    const double a = std::min(p.look.half.x, 2.4), b = std::min(p.look.half.z, 1.0);
    auto h = [&](const V3& o) { return w.height(pl.p.x + o.x, pl.p.z + o.z); };
    const double sf = std::clamp((h(f * a) - h(f * -a)) / (2 * a), -0.55, 0.55), sl = std::clamp((h(l * b) - h(l * -b)) / (2 * b), -0.55, 0.55);
    V3 F = norm(f + V3(0, sf, 0)), L = norm(l + V3(0, sl, 0));
    V3 U = norm(cross(L, F));
    L = cross(F, U);
    const V3 ground(pl.p.x, (h(f * a) + h(f * -a) + h(l * b) + h(l * -b)) / 4, pl.p.z);
    // Pitched, then rolled, about its own axes.
    const double cp = dm::cos(p.look.pitch), sp = dm::sin(p.look.pitch), cr = dm::cos(p.look.roll), sr = dm::sin(p.look.roll);
    const V3 F1 = F * cp + U * sp, U1 = U * cp - F * sp;
    const V3 U2 = U1 * cr + L * sr, L2 = L * cr - U1 * sr;
    p.ax[0] = F1, p.ax[1] = U2, p.ax[2] = L2;
    // Lifted (or let down) so the lowest corner of its box is `sink` under the ground's plane.
    double lowest = 1e9;
    for (int i = 0; i < 8; i++) {
        const V3 c(p.look.center.x + (i & 1 ? 1 : -1) * p.look.half.x, p.look.center.y + (i & 2 ? 1 : -1) * p.look.half.y, p.look.center.z + (i & 4 ? 1 : -1) * p.look.half.z);
        lowest = std::min(lowest, dot(F1 * c.x + U2 * c.y + L2 * c.z, U));
    }
    p.at = ground + U * (-lowest - p.look.sink);
    return p;
}

void place(World& w, const MapPlace& pl) {
    const Pose p = poseOf(w, pl);
    if (!w.placeModel(p.look.model, p.at, pl.yaw)) return;
    Placed& m = w.models.back();
    m.variant = p.look.variant, m.oriented = true;
    for (int i = 0; i < 3; i++) m.ax[i] = p.ax[i];
    Box b;
    b.c = p.middle(), b.h = p.look.half, b.bound = len(p.look.half), b.hidden = true;
    for (int i = 0; i < 3; i++) b.ax[i] = p.ax[i], b.col[i] = 0.3f;
    w.boxes.push_back(b);
    if (w.map == MAP_REAL) {  // (its rocks are scattered after the plan's places are: not through the wreck)
        const V3 mid = p.middle();
        w.rocks.erase(std::remove_if(w.rocks.begin(), w.rocks.end(), [&](const Rock& k) { return len(k.c - mid) < k.r + p.look.half.x; }), w.rocks.end());
    }
}

void placeAll(World& w) {
    if (w.map == MAP_PROCEDURAL) return;
    for (const MapPlace* pl : placesOfKind(w.map, PK_WRECK))
        if (std::max(std::fabs(pl->p.x), std::fabs(pl->p.z)) < w.halfSize() - 20) place(w, *pl);
}

CacheSpot spotOf(const World& w, const MapPlace& pl) {
    CacheSpot c;
    c.wreck = true, c.map = w.map;
    c.id = w.map == MAP_PROCEDURAL ? proc::placeKey(w.proc.seed, pl.id) : pl.id;
    const Pose p = poseOf(w, pl);
    c.at = p.middle() - V3(0, 0.5, 0);  // (Interact aims half a metre over a spot)
    const uint32_t h = mix32(hashOf(pl.id, seedOf(w)) + 0x10071u);
    if ((int)(h % 100) >= kLootPercent) return c;
    // What's in it: mostly something for the trail.
    static const RepairPart parts[] = {PART_TIRE, PART_CV_AXLE, PART_TIE_ROD, PART_CONTROL_ARM, PART_SPARK_PLUGS, PART_ENGINE_OIL, PART_SPRING, PART_DRIVE_SHAFT};
    static const Tool tools[] = {TOOL_WRENCH, TOOL_RATCHET, TOOL_HAMMER, TOOL_PRY_BAR, TOOL_TIRE_IRONS, TOOL_LUG_WRENCH, TOOL_CABLES, TOOL_JUMP_PACK};
    static const AnchorGear gear[] = {GEAR_STAKE, GEAR_STRAP, GEAR_SLING, GEAR_BOLT};
    const int roll = (int)(h >> 8) % 100;
    const uint32_t pick = h >> 16;
    if (roll < 36) c.gear.push_back(repairPartKey(PART_FUEL_CAN));
    else if (roll < 62) c.gear.push_back(repairPartKey(parts[pick % 8]));
    else if (roll < 72) c.gear.push_back(toolKey(tools[pick % 8]));
    else if (roll < 84) c.gear.push_back(anchors::gearKey(gear[pick % 4]));
    const int cash = pl.reward > 0 ? pl.reward : 60 + 10 * (int)(pick % 30);
    if (roll >= 84 || (pick >> 8) % 100 < 25) c.cash = cash;
    return c;
}

std::vector<CacheSpot> spots(const World& w) {
    std::vector<CacheSpot> out;
    for (const MapPlace* pl : placesOfKind(w.map, PK_WRECK))
        if (std::max(std::fabs(pl->p.x), std::fabs(pl->p.z)) < w.halfSize() - 20) out.push_back(spotOf(w, *pl));
    return out;
}

bool isSearched(const CareerSession& run, const Searched& done, const CacheSpot& c) {
    return run.profile.on ? careersession::cacheOpened(run, c) : done.keys.count(Career::cacheKey(c)) > 0;
}

std::string search(CareerSession& run, const Sim& sim, Searched& done, const CacheSpot& c) {
    if (run.profile.on) return careersession::openCache(run, sim, c);
    if (!done.keys.insert(Career::cacheKey(c)).second) return "";
    Career sandbox;  // (what it would have been, on a scratch profile: nothing is granted)
    return sandbox.takeCache(c);
}

std::string message(int map, const std::string& id) { return "wreck " + std::to_string(map) + " " + id; }

bool takeMessage(const std::string& text, int& map, std::string& id) {
    if (text.rfind("wreck ", 0) != 0) return false;
    const size_t sp = text.find(' ', 6);
    if (sp == std::string::npos || sp + 1 >= text.size()) return false;
    map = std::atoi(text.c_str() + 6);
    id = text.substr(sp + 1);
    return true;
}

}  // namespace wrecks
