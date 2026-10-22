// The showroom's heart, with no GL: the studio Sim with one car on the turntable, rebuilt from an owned
// VehicleSpec on every pick, the bounded set of those specs, and the screen layout and fades. The game
// wires VisualHooks to the template cache (carvisual.h); tests/showroom.cpp drives it with a fake one.
#pragma once
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mods.h"
#include "softbody.h"

struct Career;

struct VisualHooks {  // the template cache (carvisual.h) in the game; fakes in tests
    std::function<void(const VehicleSpec&, const V3&)> prebuild;
    std::function<bool(const VehicleSpec&)> ready;
    std::function<bool(const VehicleSpec&)> evict;  // false: still building, try again later
};

class ShowroomCore {
public:
    // kMaxSpecs: picks kept built (back and forth costs no rebuild); kMaxInFlight: builds at once (the
    // pick and its two neighbours). kDebounce: a pick held this long (s) is built, so mashing builds once.
    // kDrop (m): the car is set down this far above the pad. kBuildTimeout (s): a build not in by then is
    // let go (the pick's: failed()). kUnstable (m): see failed().
    static constexpr int kMaxSpecs = 8, kMaxInFlight = 3;
    static constexpr double kDebounce = 0.15, kDrop = 0.03, kBuildTimeout = 5.0, kUnstable = 5.0;
    explicit ShowroomCore(VisualHooks hooks);
    void open(int vehicle, const ModSet& fitted, const V3& color);  // clamps fitted like applyMods
    void close();                                    // queues every spec for eviction
    bool isOpen() const { return isOpen_; }
    int vehicle() const { return vehicle_; }
    const ModSet& fitted() const { return fitted_; }
    const ModSet& pick() const { return pick_; }
    void setPick(int slot, int option);              // clamped; restarts the debounce
    void setPicks(const ModSet& m);                  // every slot at once (BUILDS): clamped; one restart; neighbours only if one slot changed
    void update(double dt);                          // debounce, build, swap, prebuild neighbours, step studio, evict (also while closed)
    const Car* preview() const;                      // null until the first build is in
    bool fitting() const;                            // a newer pick is building
    bool failed() const { return failed_; }          // build timed out or the preview blew up
    int liveSpecs() const;                           // built + building + awaiting eviction
    Sim& studio() { return sim; }

private:
    VisualHooks hooks;
    Sim sim;
    bool isOpen_ = false, failed_ = false;
    int vehicle_ = 0, lastSlot = -1;  // lastSlot: the slot setPick last changed (-1: none since open)
    ModSet fitted_, pick_;
    V3 color;
    // Owned specs, most recently used first; the preview's car points at one of them.
    std::list<std::unique_ptr<VehicleSpec>> specs;
    std::vector<std::unique_ptr<VehicleSpec>> evicting;  // let go of, until hooks.evict takes them
    const VehicleSpec* shown = nullptr;                   // the preview car's spec
    VehicleSpec* want = nullptr;                          // the pick's spec, from the end of its debounce
    std::map<const VehicleSpec*, double> building;        // held specs still building: s since their prebuild
    double held = 0;                                      // s since the last setPick
    bool debouncing = false, neighboursDone = false;
    VehicleSpec* specFor(const ModSet& m, bool build);
    void removePreview();
    void trim();
};

struct Rect { float x, y, w, h; };
// left: tabs, then (level 2) a breadcrumb line, then the tiles (level 1: tileCols x tileRows whole tiles, scrolling) or the
// rows (level 2: listRows rows of rowH under the breadcrumb: the name, then its detail line). A tile is a 4:3 picture
// over two caption lines.
struct ShowroomLayout {
    float u;
    Rect left, right, bottom, car;
    int listRows;
    float rowH;  // a level 2 row: name line, detail line
    int tileCols, tileRows;
    float tileTop, tileW, tileH, tileGap;  // the first tile row's y; a tile's size; the space between tiles and from the panel's edge
    Rect rowRect(int i) const { return {left.x + 6 * u, left.y + 78 * u + i * rowH, left.w - 12 * u, rowH}; }  // (78u: title, tabs, breadcrumb)
    Rect tileRect(int col, int row) const { return {left.x + tileGap + col * (tileW + tileGap), tileTop + row * (tileH + tileGap), tileW, tileH}; }
};
ShowroomLayout showroomLayout(int W, int H);

// B (the pad's Y): buys the part shown, in a career and at a repair station. True if bought; `toast` is what to say
// ("" in a sandbox, where there's nothing to buy).
bool showroomBuy(Career& career, int vehicle, int slot, int option, bool atStation, std::string& toast);

struct ShowroomFade {  // black fade between worlds: start() -> black (swap) -> clear
    static constexpr double kTime = 0.25;
    bool start();                                    // false if one is running already
    bool update(double dt, bool hold = false);       // true once, on the frame it reaches black; hold: stay black
    float alpha() const;
    bool busy() const { return on; }
    bool isBlack() const { return on && black && t <= kTime; }  // at the swap's black (held there, or its one frame)

private:
    double t = 0;
    bool on = false, black = false;
};
