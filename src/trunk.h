// The truck's cargo: open the tailgate (or the liftgate; a pickup's gate drops), and what you
// carry is laid out in the back as its own model: the jack, the snow chains, your tools, spare
// parts and anchor gear, on a shelf-packed grid in the cargo area, riding the body nodes so it
// moves with the truck. With the back open you look at a thing and hold E to take it into your
// hands; hold E at the open back with something in your hands to put it back.
//
// The rules (everything you can tune is here):
//  - Your kit (tools and repair parts) is usable while you hold the thing, or within kReach of
//    your own truck's open back. A truck with no cargo area to open (a buggy, the Jesko) carries
//    it with it: always within reach there. Otherwise the job says what's missing and where it is.
//  - Chains and the jack are each truck's own (Car::chains, Car::hasJack). Tools, repair parts and
//    anchor gear are the player's (career.h keeps them, sandbox has them all): they're drawn in the
//    truck you drive (the primary), not in each of them.
//  - A tool, part or anchor in your hands is the same one: it doesn't leave the kit, it just isn't
//    lying in the back while you hold it.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "anchors.h"
#include "fixes.h"
#include "modelkit.h"
#include "softbody.h"

enum TrunkKind { TK_NONE = -1, TK_TOOL, TK_PART, TK_GEAR, TK_CHAINS, TK_JACK };

// Something in your hands out of the back: a tool, a repair part, an anchor (the chains, the
// jack and wheels have their own flags on the Walker).
struct TrunkRef {
    int kind = TK_NONE;
    int id = 0;
    bool any() const { return kind != TK_NONE; }
    bool operator==(const TrunkRef& o) const { return kind == o.kind && id == o.id; }
    bool operator!=(const TrunkRef& o) const { return !(*this == o); }
};

// One kind of thing lying in the back.
struct TrunkItem {
    TrunkRef ref;
    int count = 1;         // how many there are (a stack)
    int taken = 0;         // ...of which are out of the back just now (in your hands, or the jack under a corner)
    bool endless = false;  // sandbox: the supply never runs out
    std::string name;      // "HAMMER"
    std::string model;     // its modelkit id
};

// Where one drawn copy of an item sits, in the car's rest frame (x forward, y up, z right: the
// same frame as Car::restLocal, and the models').
struct TrunkSlot {
    int item = 0;     // index into the contents
    int copy = 0;     // 0 .. min(count, kMaxCopies) - 1 (the stack, bottom first)
    V3 at;            // where the model's origin goes
    V3 ax[3];         // its front, up and right in that frame
    double scale = 1;
    V3 lo, hi;        // the space it takes (rest frame)
};

// Everything you own, as the trunk counts it (set by the game: career.kit() and the anchor gear).
struct TrunkStock {
    Kit kit;
    AnchorStock gear;
};

namespace trunk {
constexpr double kReach = 8.0;     // m: your kit is usable this close to your own truck's open back
constexpr int kMaxCopies = 4;      // drawn per stack
constexpr double kReachIn = 3.3;   // m from your chest: how far into the back you can reach for a thing

std::string name(TrunkRef r);      // "HAMMER", "TREE STRAP"
const char* model(TrunkRef r);     // its modelkit id
const mk::Mesh& itemMesh(const std::string& model);  // what's drawn of it in the back: built with Placed::bare, as the renderer does
std::string lineFor(const TrunkItem& it);  // "HAMMER", "STAKES X3", "CV AXLE (ENDLESS)" for the list

// Where the cargo is, in the rest frame.
struct Box {
    V3 lo, hi;
};
// The cargo area (false: this vehicle has none). Behind the rear seat to the gate, between the
// wheel wells, from the floor up; a pickup's is the bed.
bool cargoBox(const Car& c, Box& out);

// What's in this truck's back: `own` is yours (the primary truck holds it); the chains and the
// jack are the car's own. The whole load: what's out of it just now (in your hands: `inHand`, a jack
// or chain you carry, the jack under a corner) is marked `taken`, so the layout doesn't shuffle.
std::vector<TrunkItem> wholeLoad(const Car& c, const TrunkStock& own, bool primary, TrunkRef inHand, bool holdingJack, bool holdingChain = false);
// ...and what's lying in it now (the whole load less what's taken: the list's, the count).
std::vector<TrunkItem> contents(const Car& c, const TrunkStock& own, bool primary, TrunkRef inHand, bool holdingJack, bool holdingChain = false);
// How many copies of an item are drawn now.
inline int copiesShown(const TrunkItem& it) { return std::min(std::max(it.count - it.taken, 0), kMaxCopies); }

// Lays the contents out on the cargo floor: packed tight from the gate forward (each kind turned
// whichever way it fits), stacks piled up. Everything stays inside cargoBox and nothing lies on
// anything else: the whole lot shrinks (the big things most) until every kind is on the floor.
std::vector<TrunkSlot> layout(const Car& c, const std::vector<TrunkItem>& items);

// An SUV's cargo deck is a slide: with the back open it runs out behind the bumper (so what's on it is in
// the open, in the light, and not behind a high sill), and back in when it's shut. The deck is kOver longer
// than its bay (its back end is what hangs out): slide() is how far it's moved back from where its box is
// (kSlide open; shut, it rides kOver forward, all of it in the bay).
constexpr double kSlide = 0.62;
constexpr double kOver = 0.25;
double slide(const Car& c);

// A point in the rest frame, carried by the body: the nearest nodes' positions plus the turn of
// the body (so a dent moves it, and it follows the truck).
V3 world(const Car& c, const Frame& fr, const V3& rest);
// A slot's model placement in the world (origin and front/up/right axes).
void place(const Car& c, const Frame& fr, const TrunkSlot& s, V3& at, V3 ax[3]);
// The middle of an item's stack top (where to aim), in the world; false if none of it is lying there.
bool aimAt(const Car& c, const Frame& fr, const std::vector<TrunkItem>& items, const std::vector<TrunkSlot>& slots, int item, V3& at);

// The gate itself: where to hold E (the middle of its opening, world), and the middle of the cargo.
V3 gateAt(const Car& c);
V3 cargoCenter(const Car& c);
const char* gateName(const Car& c);  // "TAILGATE" / "LIFTGATE"

// A pickup's drawn gate: its placement for the gate's current angle (the model stands on its
// hinge line, `width` m across).
bool bedGatePlace(const Car& c, const Frame& fr, V3& at, V3 ax[3], double& width);

// The back's picture: every drawn copy as a placed model (a car with a back that's open, or a
// pickup's bed), and a pickup's gate. Only your own trucks show a load.
void models(const Sim& sim, int car, const TrunkStock& own, TrunkRef inHand, bool holdingJack, bool holdingChain, std::vector<Placed>& out);

// Whether your kit is within reach: carried things aside, you're at your own truck's open back
// (within kReach) or at a truck with nothing to open.
bool kitInReach(const Sim& sim, const V3& at);
// The kit you can use right now: all of it within reach, else only what's in your hands.
Kit reachKit(const Kit& kit, bool inReach, TrunkRef inHand, bool holdingJack);
}  // namespace trunk
