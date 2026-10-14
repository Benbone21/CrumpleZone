// The map lookup must never omit a nearby obstacle, and repeats from grid cells are removed.
#include "harness.h"
#include <algorithm>
#include <random>
#include <set>

SCENARIO(nearbySolids, "autopilot/nearby-solids", "Spatial queries retain every obstacle that can intersect the query and return each index once") {
    const World& w = ctx.sim.world;
    World::NearbySolids near;
    std::mt19937 random(4198);
    std::uniform_real_distribution<double> position(-200, 200), radius(1, 80);
    bool complete = true, unique = true, valid = true;
    auto intersects = [](double x, double z, double r, double x0, double z0, double x1, double z1) {
        return x + r >= x0 && x - r <= x1 && z + r >= z0 && z - r <= z1;
    };
    for (int trial = 0; trial < 128; trial++) {
        const double x = position(random), z = position(random), r = radius(random);
        const double x0 = x-r, x1 = x+r, z0 = z-r, z1 = z+r;
        w.solidsNear(x0, z0, x1, z1, near);
        for (const auto* ids : {&near.poles, &near.boxes, &near.rocks, &near.logs})
            unique &= std::set<int>(ids->begin(), ids->end()).size() == ids->size();
        auto has = [](const std::vector<int>& ids, int i) { return std::find(ids.begin(), ids.end(), i) != ids.end(); };
        for (int i : near.poles) valid &= i >= 0 && i < (int)w.poles.size();
        for (int i : near.boxes) valid &= i >= 0 && i < (int)w.boxes.size();
        for (int i : near.rocks) valid &= i >= 0 && i < (int)w.rocks.size();
        for (int i : near.logs) valid &= i >= 0 && i < (int)w.logs.size();
        for (int i = 0; i < (int)w.poles.size(); i++) {
            const Pole& p = w.poles[i];
            if (intersects(p.x,p.z,p.r,x0,z0,x1,z1)) complete &= has(near.poles,i);
        }
        for (int i = 0; i < (int)w.boxes.size(); i++) {
            const Box& b = w.boxes[i];
            if (b.solid && intersects(b.c.x,b.c.z,b.bound,x0,z0,x1,z1)) complete &= has(near.boxes,i);
        }
        for (int i = 0; i < (int)w.rocks.size(); i++) {
            const Rock& rock = w.rocks[i];
            if (intersects(rock.c.x,rock.c.z,rock.r,x0,z0,x1,z1)) complete &= has(near.rocks,i);
        }
        for (int i = 0; i < (int)w.logs.size(); i++) {
            const Log& log = w.logs[i];
            // Capsule bounding boxes are tighter than this circle: require its actual x/z box.
            if (std::max(log.a.x,log.b.x)+log.r >= x0 && std::min(log.a.x,log.b.x)-log.r <= x1 &&
                std::max(log.a.z,log.b.z)+log.r >= z0 && std::min(log.a.z,log.b.z)-log.r <= z1)
                complete &= has(near.logs,i);
        }
    }
    ctx.check("all intersecting obstacle bounds retained", complete);
    ctx.check("indices refer to the correct map objects", valid);
    ctx.check("each object occurs once even across many lookup cells", unique);
}
