#include "collision.h"
#include "softbody.h"
#include <algorithm>
#include <numeric>

namespace collision {
static bool overlap(const Car& a, const Car& b) {
    return a.bbMin.x <= b.bbMax.x && b.bbMin.x <= a.bbMax.x &&
           a.bbMin.y <= b.bbMax.y && b.bbMin.y <= a.bbMax.y &&
           a.bbMin.z <= b.bbMax.z && b.bbMin.z <= a.bbMax.z;
}

void candidates(const std::vector<Car>& cars, std::vector<std::pair<int, int>>& pairs,
                std::vector<int>& order) {
    pairs.clear();
    const int n = (int)cars.size();
    if (n <= 16) {  // Sorting costs more than the bounds checks for a small scene.
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++)
                if (overlap(cars[i], cars[j])) pairs.emplace_back(i, j);
        return;
    }
    if ((int)order.size() != n) {
        order.resize(n);
        std::iota(order.begin(), order.end(), 0);
    }
    // Choose the widest axis: freeway convoys and vertical stacks need different sweeps.
    V3 lo = cars[0].bbMin, hi = cars[0].bbMax;
    for (const Car& c : cars) {
        lo = V3(std::min(lo.x, c.bbMin.x), std::min(lo.y, c.bbMin.y), std::min(lo.z, c.bbMin.z));
        hi = V3(std::max(hi.x, c.bbMax.x), std::max(hi.y, c.bbMax.y), std::max(hi.z, c.bbMax.z));
    }
    const V3 extent = hi - lo;
    const int axis = extent.y > extent.x && extent.y > extent.z ? 1 : extent.z > extent.x ? 2 : 0;
    auto coord = [axis](const V3& p) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; };
    auto less = [&](int a, int b) {
        const double x = coord(cars[a].bbMin), y = coord(cars[b].bbMin);
        return x < y || (x == y && a < b);
    };
    // Adjacent 0.5 ms steps almost always keep their order. Only sort after crossings.
    if (!std::is_sorted(order.begin(), order.end(), less)) std::sort(order.begin(), order.end(), less);
    for (int a = 0; a < n; a++) {
        const int i = order[a];
        const double end = coord(cars[i].bbMax);
        for (int b = a + 1; b < n; b++) {
            const int j = order[b];
            if (coord(cars[j].bbMin) > end) break;
            if (overlap(cars[i], cars[j])) pairs.emplace_back(std::min(i, j), std::max(i, j));
        }
    }
    std::sort(pairs.begin(), pairs.end());
}
}
