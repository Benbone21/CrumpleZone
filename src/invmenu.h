// The inventory menu: E at the back of your truck opens it. One card per kind of thing you carry (the
// jack, snow chains, tools, repair parts, anchor gear) with its model as an icon, its name and how many
// are in the back; choose one and it's in your hands (the same take as lifting it out of the bed:
// Interact::takeFromBack), choose what you hold to put it back. No GL here (the tests link it):
// the layout is plain rectangles and invdraw.cpp paints them.
//
// Keys: WASD or the arrows move, E or Enter chooses, C shuts the gate, Esc / Tab / I close; the mouse
// hovers and clicks; a pad's D-pad or stick moves, A chooses, Y shuts the gate, B closes.
#pragma once
#include <string>
#include <vector>

#include "interact.h"

// One card.
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

// Where everything goes for a window of W x H and a UI scale `u` (H / 720, at least 0.75).
struct InvLayout {
    InvRect panel, title, putBack, shut, iconArea, desc, foot;
    std::vector<InvRect> cards;  // per slot
    struct Head {
        std::string text;
        float x = 0, y = 0;
    };
    std::vector<Head> heads;
    int titleScale = 2, nameScale = 2, smallScale = 1, bodyScale = 2;  // whole-number text sizes
    float u = 1;
};

enum { INV_NONE = -2, INV_PUT_BACK = -1, INV_SHUT = -3 };  // a choice that isn't a card (cards are 0..)

struct InvMenu {
    bool open = false;
    int car = -1;              // whose back (sim.cars)
    int sel = INV_NONE;        // the chosen card (or INV_PUT_BACK / INV_SHUT)
    std::vector<InvSlot> slots;
    bool holding = false;      // something's in your hands: PUT BACK is offered
    std::string heldName, heldModel;
    bool canShut = false;      // the back has a gate that's open to shut
    std::string gate;          // "TAILGATE", "LIFTGATE", "TRUNK"

    // What the back holds now, as cards (the whole load: what's out of it is marked, and a thing that's out on the
    // truck, in the ground or on a line still has its card).
    static std::vector<InvSlot> build(const Sim& sim, int car, const TrunkStock& own, const Walker& wk);
    static std::string describe(TrunkRef r);  // "CV AXLE: REPLACES A SNAPPED FRONT OR REAR SHAFT"

    // Opens it for car `ci`'s back; refresh() keeps it up to date (the choice stays on the same card).
    void show(int ci);
    void refresh(const Sim& sim, const TrunkStock& own, const Walker& wk);
    void close() { open = false, car = -1, sel = INV_NONE; }

    InvLayout layout(int W, int H, float u) const;
    // Moves the choice to the nearest card the way you pressed (-1 left / +1 right, -1 up / +1 down).
    void move(const InvLayout& lay, int dx, int dy);
    // What's under a point (a card index, INV_PUT_BACK, INV_SHUT or INV_NONE).
    int hit(const InvLayout& lay, float x, float y) const;
    const InvSlot* chosen() const { return sel >= 0 && sel < (int)slots.size() ? &slots[sel] : nullptr; }

    // Does what choice `what` is. Returns what to announce; `closed` is set when the menu is done with (something's
    // in your hands now, it's back in the truck or the gate's shut), else it stays up.
    std::string choose(Interact& ia, Sim& sim, Walker& wk, const TrunkStock& own, int what, bool& closed);
};
