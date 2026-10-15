#include "../src/visibility.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <set>

int main() {
    RenderBounds bounds;
    bounds.add(V3(-1, -1, -1));
    bounds.add(V3(1, 1, 1));
    assert(bounds.visible(m4identity()));
    const M4 vp = perspective(1, 1, 0.1f, 100) * lookAt(V3(0, 0, 5), V3(), V3(0, 1, 0));
    assert(bounds.visible(vp));
    RenderBounds behind, side;
    behind.add(V3(-1, -1, 6)); behind.add(V3(1, 1, 8));
    side.add(V3(30, -1, 0)); side.add(V3(31, 1, 1));
    assert(!behind.visible(vp) && !side.visible(vp));
    assert(bounds.distance(V3()) == 0);
    assert(bounds.distance(V3(4, 0, 5)) == 5);
    // A box crossing the eye/near plane must not be rejected by its center.
    RenderBounds crossing;
    crossing.add(V3(-10, -10, -10)); crossing.add(V3(10, 10, 10));
    assert(crossing.visible(vp));
    assert(bounds.visible(ortho(-2, 2, -2, 2, -2, 2)));
    assert(!behind.visible(ortho(-2, 2, -2, 2, -2, 2)));

    // Both map spacings have partial edge chunks. Check offset chunks as well
    // as the origin: every LOD must tile exactly, with the original winding
    // and exactly the same fine boundary edges as its full-resolution neighbor.
    for (int size : {24, 48, 64}) {
        std::set<std::pair<unsigned, unsigned>> baseline;
        for (int step : {1, 2, 4, 8}) {
            std::vector<unsigned> indices;
            appendTerrainLod(indices, 1201, 64, 128, 64 + size, 128 + size, step);
            std::map<std::pair<unsigned, unsigned>, int> edges;
            double area = 0;
            for (size_t i = 0; i < indices.size(); i += 3) {
                const unsigned a = indices[i], b = indices[i + 1], c = indices[i + 2];
                assert(a < 1201u * 1201 && b < 1201u * 1201 && c < 1201u * 1201);
                const double ax = a / 1201, az = a % 1201, bx = b / 1201, bz = b % 1201;
                const double cx = c / 1201, cz = c % 1201;
                const double twiceArea = (bz - az) * (cx - ax) - (bx - ax) * (cz - az);
                assert(twiceArea > 0);
                area += twiceArea / 2;
                for (auto edge : {std::minmax(a, b), std::minmax(b, c), std::minmax(c, a)}) ++edges[edge];
            }
            assert(area == size * size);
            std::set<std::pair<unsigned, unsigned>> perimeter;
            for (auto entry : edges) {
                assert(entry.second == 1 || entry.second == 2);
                if (entry.second == 1) perimeter.insert(entry.first);
            }
            if (step == 1) baseline = perimeter;
            else {
                assert(perimeter == baseline);
                assert(indices.size() < size_t(size * size * 6));
            }
            std::printf("PASS %d-cell LOD %d: %zu triangles; shared boundary, complete area, consistent winding\n",
                        size, step, indices.size() / 3);
        }
    }
    std::puts("PASS perspective/orthographic culling, behind-camera rejection, crossing bounds and distance");
}
