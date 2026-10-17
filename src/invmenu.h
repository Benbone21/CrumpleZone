// The inventory menu: E at the back of your truck opens it, with no gate swinging and nothing laid out in the
// bed. A compact grid of square slots, five across: one slot per kind of thing you carry (the jack, snow
// chains, tools, repair parts, anchor gear) with its model as the icon, a count badge in one corner and a dot for
// its state in the other; the chosen slot's name and what it's for show in a strip under the grid. Choose one and
// it's in your hands (the same take as lifting it out of a bed: Interact::takeFromBack); choose the one you hold
// to put it back. No GL here (the tests link it): the layout is plain rectangles and invdraw.cpp paints them.
//
// Keys: WASD or the arrows move, E or Enter chooses, Esc / Tab / I close; the mouse hovers and clicks; a pad's
// D-pad or stick moves, A chooses, B closes.
#pragma once
#include <string>
#include <vector>

#include "interact.h"

// One slot.
struct InvSlot {
    TrunkRef ref;
    std::string name, model, desc;
    std::string state;      // "IN YOUR HAND", "UNDER THE TRUCK", "2 ON TIRES", "1 SET OUT" ("" none)
    int left = 0;           // in the back now
    bool endless = false;   // sandbox: the supply never runs out
    bool inHand = false;
    bool takable = false;   // there's one in the back to take
    int group = 0;          // 0 tools, 1 repair parts, 2 recovery and anchor gear
};

struct InvRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool has(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
};

constexpr int kInvCols = 5;

// Where everything goes for a window of W x H and a UI scale `u` (H / 720, at least 0.75).
struct InvLayout {
    InvRect panel, title, desc, foot;
    std::vector<InvRect> cards;  // per slot (square)
    float slot = 0;              // a slot's side
    int titleScale = 2, nameScale = 1, bodyScale = 1;  // whole-number text sizes (the badge uses bodyScale)
    int descLines = 1;           // lines reserved for a description
    float u = 1;
};

enum { INV_NONE = -1 };  // no choice (slots are 0..)

// Breaks `t` into lines no wider than `w` px at text scale `s`.
std::vector<std::string> invWrap(const std::string& t, float s, float w);

struct InvMenu {
    bool open = false;
    int car = -1;              // whose back (sim.cars)
    int sel = INV_NONE;        // the chosen slot
    std::vector<InvSlot> slots;
    bool holding = false;      // something's in your hands

    // What the back holds now, as slots (the whole load: what's out of it is marked, and a thing that's out on the
    // truck, in the ground or on a line still has its slot).
    static std::vector<InvSlot> build(const Sim& sim, int car, const TrunkStock& own, const Walker& wk);
    static std::string describe(TrunkRef r);  // "CV AXLE: REPLACES A SNAPPED FRONT OR REAR SHAFT"
    static std::string blurb(const InvSlot& s);  // ...without its name: "REPLACES A SNAPPED FRONT OR REAR SHAFT"
    static std::string keys(bool padLast);       // the hint line

    // Opens it for car `ci`'s back; refresh() keeps it up to date (the choice stays on the same slot).
    void show(int ci);
    void refresh(const Sim& sim, const TrunkStock& own, const Walker& wk);
    void close() { open = false, car = -1, sel = INV_NONE; }

    InvLayout layout(int W, int H, float u, bool padLast = false) const;
    // Moves the choice one slot the way you pressed (-1 left / +1 right, -1 up / +1 down), wrapping round the grid.
    void move(int dx, int dy);
    // What's under a point (a slot index or INV_NONE).
    int hit(const InvLayout& lay, float x, float y) const;
    const InvSlot* chosen() const { return sel >= 0 && sel < (int)slots.size() ? &slots[sel] : nullptr; }

    // Does what choice `what` is. Returns what to announce; `closed` is set when the menu is done with (something's
    // in your hands now, or it's back in the truck), else it stays up.
    std::string choose(Interact& ia, Sim& sim, Walker& wk, const TrunkStock& own, int what, bool& closed);
};
