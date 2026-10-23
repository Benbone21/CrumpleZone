#include "studio.h"

#include <cmath>

#include "softbody.h"

// A flat studio out on the proving ground: a concrete pad in the grass, nothing else.
World studioWorld() {
    const bool showcase = World::withShowcase;  // (no need to lay out the yard and park just to clear them)
    World::withShowcase = false;
    World w(MAP_PROVING_GROUND);
    World::withShowcase = showcase;
    w.boxes.clear(), w.rocks.clear(), w.trees.clear(), w.bushes.clear(), w.logs.clear(), w.poles.clear();
    w.fixtures.clear(), w.stations.clear(), w.models.clear(), w.waters.clear(), w.tags.clear();
    for (double x = -110; x <= 110; x += 1)
        for (double z = -110; z <= 110; z += 1) {
            const int id = w.cellIndex(x, z);
            if (id < 0) continue;
            const double r = std::hypot(x, z);
            w.setCellHeight(id, 0);
            if (r < 9) w.setCellSurface(id, SURF_PAVED, PAINT_PAD);
            else w.setCellSurface(id, SURF_GRASS, PAINT_NONE);
        }
    w.buildGrid();
    return w;
}
