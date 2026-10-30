// The procedural world's set pieces as models (docs/plans: procworld3, "procmodels"): what the generator lays out as numbers is
// dressed here, in each window as it's built (World::buildProcedural), so the look streams with the chunk window like everything else.
//
//  - Waterfalls (procworld.h fallSpans): a World::falls each, so the existing "waterfall" model (src/models/water.cpp: the falling
//    sheet, foam, spray, mist and pool ripples, drawn by the water shader; watermesh.cpp) and the falls' roar (worldsound.h) come with
//    it. The water grid's stepped levels down the rock step are taken out (the model is the water there), the step and the walls
//    beside it are bare rock, and dark wet boulders frame the lip and the plunge pool.
//  - Timber bridges (TrailEdge::bridges): the "timber_bridge" model (src/models/bridges.cpp), its design by its span, worn by the
//    years (bridgelayout.h: most rough, some fair; rules bridges.*). A fair one drives on the generator's stand-in boxes (hidden);
//    a worn one's holes, broken and sagging planks, snapped rails and leaning posts are real in its collision, piece by piece.
#pragma once
#include <cstddef>

#include "procworld.h"

class World;

namespace proc {

// Whether windows are dressed (on; the tests turn it off to compare against the bare generator's window).
extern thread_local bool dressWindows;

// The falls in a window (from the generator), as World::falls, their water and ground made ready for the model.
void dressFalls(World& w);
// The bridges: a model on each span over buildBridges' stand-ins (the boxes from `firstBox` on, the posts from `firstPole`), which
// are made collision-only. A bridge in fair shape keeps them as its collision; a worn one's deck, rails and posts are replaced by a
// box for each plank piece, tie, inner stringer and whole rail, and a post for each one standing (bridgelayout.h), as drawn.
void dressBridges(World& w, size_t firstBox, size_t firstPole);
// How worn the bridge on span `b` is (0..1; under kFairDamage: fair), by the rules (bridges.*) and where it is.
double bridgeWear(const TrailEdge::Span& b, bool mainTrail);

// The bridge model's design for a span `L` m long (0 SHORT, 1 MEDIUM, 2 LONG), and whether the generator gave it a middle pier.
int bridgeVariant(double L);
inline bool bridgeHasPier(double L) { return L > 22; }

}  // namespace proc
