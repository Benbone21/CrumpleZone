// Exact queries on the real shapes of the world's objects: a rock's convex hull, a log's capsule,
// a trunk's cylinder, a box. Rays (nearest hit and its normal), the nearest point on a surface,
// and what a strap wraps: the hull's horizontal cross-section at a height, as a tight path.
// Pure geometry (no GL, no sim): the game's anchors and aiming use it, and the editor can too.
// A Rock's bounding sphere (c, r) is only ever a quick reject here, never the answer.
#pragma once
#include <vector>

#include "world.h"

namespace objshape {

// A point in plan view (x, z).
struct P2 {
    double x = 0, z = 0;
};

// ---- Rays ----------------------------------------------------------------------------------
struct Hit {
    double t = 0;  // along the (unit) direction
    V3 p, n;       // where, and the surface normal there (out of the object)
};
// The first surface a ray from o along d (unit) meets within tMax. A ray that starts inside
// reports t = 0 and n = -d.
bool rayRock(const Rock& k, const V3& o, const V3& d, double tMax, Hit& out);
bool rayLog(const Log& g, const V3& o, const V3& d, double tMax, Hit& out);
bool rayPole(const Pole& p, const V3& o, const V3& d, double tMax, Hit& out);  // a trunk: a vertical cylinder y0..y1, capped
bool rayBox(const Box& b, const V3& o, const V3& d, double tMax, Hit& out);

enum Kind { SH_NONE, SH_ROCK, SH_LOG, SH_POLE, SH_BOX };
struct WorldHit {
    Kind kind = SH_NONE;
    int index = -1;  // into world.rocks / logs / poles / boxes
    Hit hit;
};
// The nearest solid object along a ray (solid boxes, not drawn-only ones; not the terrain).
bool rayWorld(const World& w, const V3& o, const V3& d, double tMax, WorldHit& out);

// ---- Nearest point on a surface -----------------------------------------------------------------
struct Near {
    V3 p, n;          // the closest point on the surface, and its normal
    double dist = 0;  // from the query point to it (0 when the point is inside)
};
Near closestOnRock(const Rock& k, const V3& q);
Near closestOnLog(const Log& g, const V3& q);
Near closestOnPole(const Pole& p, const V3& q);
Near closestOnBox(const Box& b, const V3& q);
// The nearest object surface to q within `maxDist` (whatever the nearest is: rock, log, trunk, solid box).
bool nearestSolid(const World& w, const V3& q, double maxDist, Kind& kind, int& index, Near& out);

// ---- Cross-sections: what a strap wraps ----------------------------------------------------------
// The convex outline where the horizontal plane at height y cuts the rock's hull, going round
// counter-clockwise (x to z). Empty when y is above or below it.
std::vector<P2> rockSection(const Rock& k, double y);
double rockTop(const Rock& k);
double rockBottom(const Rock& k);
double polyArea(const std::vector<P2>& poly);
// The trunk's radius at height y (a trunk is a plain cylinder: the collider's radius).
inline double trunkRadius(const Pole& p, double) { return p.r; }

// A closed strap loop in 3D at one height, hugging an outline.
struct Wrap {
    std::vector<V3> loop;  // counter-clockwise (x to z), loop[0] is the attach point
    V3 attach;             // where the line ties on (on the strap)
    V3 out;                // horizontal unit vector, out of the object at the attach point
    double y = 0;
    bool ok = false;
};
constexpr double kStandoff = 0.012;  // m: how far the strap's centre line sits off the surface (a chain link's wire is ~1 cm)

// A strap round the rock at height y, the attach point the nearest to `from` (plan view).
Wrap wrapRock(const Rock& k, double y, const V3& from, double standoff = kStandoff);
// ...round a trunk (a circle of the trunk's radius at that height).
Wrap wrapTrunk(const Pole& p, double y, const V3& from, double standoff = 0.0);
// Where on the rock a strap sits (what you'd choose by hand): a bit below its widest part above the
// ground, never buried or under the grass line. `y` is that height; false when almost none of it shows.
bool rockStrapHeight(const World& w, const Rock& k, double& y);
// A strap round the rock where the line pulls: height chosen, attach nearest to `from`.
Wrap wrapRockForPull(const World& w, const Rock& k, const V3& from);
// The loop in the frame of its attach point (x out of the object, z to the left of that, y 0), as the
// open path a strap or chain model follows: starts `gap` m round the perimeter one way from the
// attach point and ends `gap` m the other way (the two tails that meet at its shackle).
std::vector<V3> localPath(const Wrap& w, double gap);
// The rock a point on a strap belongs to: the one whose hull is nearest q, within maxDist (-1 none).
int rockAt(const World& w, const V3& q, double maxDist);

}  // namespace objshape
