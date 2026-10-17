// The truck's cargo: E at the back of your truck opens its inventory (invmenu.h): pick a thing there to
// take it into your hands, or put back what you hold. Nothing is laid out in the back to see and the gate
// stays shut; what you carry is the stock below (counts, in hand, out working).
//
// The rules (everything you can tune is here):
//  - Your kit (tools and repair parts) is usable while you hold the thing, or within kReach of
//    your own truck's back. A truck with no cargo area (a buggy, the Jesko) carries it with it:
//    always within reach there. Otherwise the job says what's missing and where it is.
//  - Chains and the jack are each truck's own (Car::chains, Car::hasJack). Tools, repair parts and
//    anchor gear are the player's (career.h keeps them, sandbox has them all): they're in the
//    truck you drive (the primary), not in each of them.
//  - A tool, part or anchor in your hands is the same one: it doesn't leave the kit, it just isn't
//    in the back while you hold it.
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

// Everything you own, as the trunk counts it (set by the game: career.kit() and the anchor gear).
struct TrunkStock {
    Kit kit;
    AnchorStock gear;
};

namespace trunk {
constexpr double kReach = 8.0;     // m: your kit is usable this close to your own truck's back

std::string name(TrunkRef r);      // "HAMMER", "TREE STRAP"
const char* model(TrunkRef r);     // its modelkit id
const mk::Mesh& itemMesh(const std::string& model);  // its icon in the inventory: built with Placed::bare, as the renderer does

// A pickup's bed, in the rest frame (where its gate is drawn).
struct Box {
    V3 lo, hi;
};
// False for anything but a pickup.
bool cargoBox(const Car& c, Box& out);

// What's in this truck's back: `own` is yours (the primary truck holds it); the chains and the
// jack are the car's own. The whole load: what's out of it just now (in your hands: `inHand`, a jack
// or chain you carry, the jack under a corner) is marked `taken`, so the cards don't shuffle.
std::vector<TrunkItem> wholeLoad(const Car& c, const TrunkStock& own, bool primary, TrunkRef inHand, bool holdingJack, bool holdingChain = false);
// ...and what's lying in it now (the whole load less what's taken: the list's, the count).
std::vector<TrunkItem> contents(const Car& c, const TrunkStock& own, bool primary, TrunkRef inHand, bool holdingJack, bool holdingChain = false);

// A point in the rest frame, carried by the body: the nearest nodes' positions plus the turn of
// the body (so a dent moves it, and it follows the truck).
V3 world(const Car& c, const Frame& fr, const V3& rest);
// The back: where to stand for E (the middle of the gate's opening, world).
V3 gateAt(const Car& c);
const char* gateName(const Car& c);  // "TAILGATE" / "LIFTGATE"

// A pickup's drawn gate: its placement for the gate's current angle (the model stands on its
// hinge line, `width` m across, `height` m tall).
bool bedGatePlace(const Car& c, const Frame& fr, V3& at, V3 ax[3], double& width, double* height = nullptr);
// ...as a placed model (shut: nothing opens it any more); anything else adds nothing.
void gateModels(const Sim& sim, int car, std::vector<Placed>& out);

// Whether your kit is within reach: carried things aside, you're at your own truck's back
// (within kReach) or at a truck with nothing to open.
bool kitInReach(const Sim& sim, const V3& at);
// The kit you can use right now: all of it within reach, else only what's in your hands.
Kit reachKit(const Kit& kit, bool inReach, TrunkRef inHand, bool holdingJack);
}  // namespace trunk
