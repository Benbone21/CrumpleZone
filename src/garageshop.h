// The garage shop (docs/plans/garage-shop.md): your truck where it stands, the parts by category, each part's
// model turning on a plinth beside it, its numbers against what's fitted, and buying, fitting, trying it on the
// truck and putting the factory part back. Everything it lists comes from career.h (owned, price, tier, the
// station rack) and mods.h (the options, tunedSpec): it keeps no parts or prices of its own.
//
// No GL or SDL here (the tests drive it): garageshopdraw.cpp paints it and main.cpp feeds it keys and the mouse,
// places the model and takes the camera.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "career.h"
#include "mods.h"
#include "paint.h"

class Sim;
struct Frame;

namespace shop {

enum Group { G_MOD, G_GEAR, G_PAINT };

// A category: a mod slot, or the station rack's items whose key starts with `prefix` ("TOOL:").
struct Category {
    std::string name, blurb;
    Group group = G_MOD;
    int slot = -1;
    std::string prefix;
};
// Every category there are parts for, in order: the mod slots, then the rack's kinds.
const std::vector<Category>& categories();

enum State {
    ST_FITTED,    // on the truck now
    ST_OWNED,     // yours: fits (at a station, in a career)
    ST_FOR_SALE,  // a station sells it
    ST_LOCKED,    // not sold: found out there or won
    ST_GEAR,      // a rack item (`carried` says how many you have)
};

struct Item {
    Group group = G_MOD;
    int slot = -1, option = -1;  // G_MOD
    int rack = -1;               // G_GEAR: index into stationRack()
    std::string key, name, detail;
    State state = ST_OWNED;
    int price = 0, tier = 1;
    int carried = 0;             // G_GEAR: how many you have (a tool: 0 or 1)
    bool tool = false;           // G_GEAR: a tool (owned once)
    std::string where;           // ST_LOCKED: where it's found ("" unknown)
};
std::vector<Item> items(const Category& c, int vehicle, const ModSet& fitted, const Career& career);

// One line of the comparison: the truck as fitted (`now`) and with the part (`with`).
struct Stat {
    std::string label;
    double now = 0, with = 0;
    std::string unit;
    int better = 0;     // +1 more is better, -1 less is better, 0 neither
    int decimals = 0;
};
// What the part changes: power, torque, weight, grip, clearance, tire size, gearing... (only the lines that
// move, plus power and weight always).
std::vector<Stat> compare(int vehicle, const ModSet& fitted, const ModSet& with);
// The truck as fitted with `it` in its slot (a rack item: as fitted).
ModSet withItem(const ModSet& fitted, const Item& it);

// What a part looks like on the turntable: a model (modelkit.h), its params, and how far it's scaled up.
struct Display {
    std::string id;
    int variant = -1;      // the design (an engine's or gearbox's own: drivetrainlook.h); -1 the lab's pick
    std::vector<std::pair<std::string, double>> set;
    double scale = 1;
    double size = 1;       // about how big it is as shown (m): how far back the camera sits
    bool onDrum = false;   // small: stood on an oil drum
    bool standUp = false;  // a tire (built lying flat): stood on its tread, its axle across
};
Display displayFor(const Item& it, int vehicle, const ModSet& fitted);
// The model's bounds as built (picked design, `set` applied, before scaling): its lowest point sits on the plinth.
void modelBounds(const Display& d, V3& lo, V3& hi);

// Keys (SDL scancodes; kept here so the tests need no SDL).
enum Key {
    K_ESC = 41, K_ENTER = 40, K_KPENTER = 88, K_SPACE = 44, K_BACKSPACE = 42,
    K_UP = 82, K_DOWN = 81, K_LEFT = 80, K_RIGHT = 79, K_W = 26, K_S = 22, K_A = 4, K_D = 7,
    K_Q = 20, K_E = 8, K_T = 23, K_R = 21, K_B = 5, K_C = 6, K_Z = 29, K_X = 27, K_G = 10,
};

enum Focus { F_CATS, F_ITEMS, F_PICK };  // F_PICK: the PAINT page's colour picker (a preset, hue, saturation or brightness)

// The PAINT page's rows (Item::option), and the picker's controls (Shop::ctl).
enum PaintRow { PR_BODY, PR_ACCENT, PR_FINISH, PR_FACTORY, PAINT_ROWS };
enum PickCtl { PC_PRESET, PC_HUE, PC_SAT, PC_VAL, PICK_CTLS };

struct Result {
    bool close = false;        // the shop shut (the fitted parts go back on: liftSync)
    bool fitted = false;       // `Shop::fitted` changed: save the garage, the truck is rebuilt on it
    bool saveCareer = false;   // cash or what you own changed
    bool painted = false;      // `Shop::paint` changed: save the garage, the truck wears it now
    std::string toast;
};

class Shop {
public:
    bool open = false;
    bool career = false;     // career rules (else sandbox: everything yours, free)
    bool atStation = true;   // career: buying and fitting only at a repair station
    int vehicle = 0;         // catalog index
    ModSet fitted;           // what's on the truck (the garage's)
    CarPaint paint;             // ... and the paint it wears (the PAINT page edits it live)
    bool previewing = false; // trying the focused part on the truck
    bool recal = false;      // recalibrate the speedo to the tires when fitting
    Focus focus = F_CATS;
    int cat = 0, sel = 0;
    std::vector<Item> list;  // the focused category's items

    // The turntable: the model's turn (rad), the camera's tilt and zoom; idle counts up from the last time
    // it was turned by hand (it turns itself after a moment).
    double spin = 0, tilt = 0.32, zoom = 1, idle = 99;

    void begin(int vehicle, const ModSet& fitted, const Career& c, bool career, bool atStation, const CarPaint& paint = CarPaint());
    void refresh(const Career& c);     // the list again (after a buy, or the career changed)
    const Category& category() const;
    const Item* item() const;          // the focused item (null on the category list... or an empty one)
    // What the truck should wear right now: the fitted parts, or with the focused part tried on.
    ModSet shown() const;

    Result key(int scancode, Career& c);
    Result activate(Career& c);        // ENTER on the focused row
    Result back();                     // ESC
    Result tryOn();                    // T: the focused part on the truck (again: off)
    Result factory(Career& c);         // R: the factory part back in the focused part's slot
    Result close();
    // The PAINT page. paintRow(): the focused row (PaintRow) or -1 when the page isn't the category in use.
    int paintRow() const;
    int pickRow = PR_BODY;  // which colour the picker edits
    int ctl = PC_PRESET;    // ... and with which control
    int dragCtl = -1;       // a slider being dragged with the mouse
    double hsv[3] = {0, 0.8, 0.8};  // the picker's working colour
    Result pickAdjust(int dir);     // left / right in the picker (a preset, or a step of hue, saturation, brightness)
    Result pointer(float x, float y, bool press, bool held);  // the mouse over the picker: a press picks a swatch or starts a slider; held drags it
    Result cycleFinish(int dir);
    void move(int dir);
    void setCat(int i);
    void setSel(int i);
    void nextCat(int dir);
    // Mouse: the row under the pointer (col 0 categories, 1 items).
    void hover(int col, int row);
    Result click(int col, int row, Career& c);
    void turn(double dYaw, double dTilt);  // the turntable by hand
    void zoomBy(double f);
    void tick(double dt);                  // the turntable turning itself

    // What ENTER does to the focused item, as a word or two ("BUY AND FIT $2500", "FIT", "FITTED"...).
    std::string actionLabel(const Career& c) const;
    std::string hint(bool pad) const;
    std::string title() const;

    // Where each row was drawn (drawable pixels), for the mouse: set by the painter.
    struct Hit { float x, y, w, h; int col, row; };
    std::vector<Hit> hits;
    float turntableX = 0;
    struct Slider { float x, w; };  // (the picker's slider tracks as drawn, for the mouse)
    Slider sliders[3] = {};  // the mouse drags the model to the right of this
    bool hitAt(float x, float y, int& col, int& row) const;

private:
    const Career* careerRef_ = nullptr;  // (what the lists are made against: the one begin() was given)
    Result fit(int slot, int option, const std::string& said);
    std::vector<Item> listFor(const Career& c) const;
    void enterPick(int row);
    void setPickColour(Result& r);
    void applyPreset(int i, Result& r);
};

// The garage camera: the truck where it stands (`fr`, its spec), the plinth beside it. `t` drifts it.
struct Cam {
    V3 eye, target;
};
V3 plinthSpot(const Frame& fr, const VehicleSpec& vs);  // on the truck's right, a little ahead of its middle (y: 0, the caller sets the ground)
Cam camera(const Shop& s, const Frame& fr, const VehicleSpec& vs, const V3& plinth, double partSize, double t);

// Puts on the player's car what the shop shows (shop.shown(); closed: the fitted parts), rebuilding it on
// that spec where it stands. `ready`: whether the spec's visual is built (else nothing yet; null: always).
// Returns whether it rebuilt the car.
bool liftSync(Sim& sim, const Shop& s, const std::function<bool(const VehicleSpec&)>& ready = nullptr);

}  // namespace shop
