// On-foot prompts: what you can do where you stand and how the game says so.
//
// One table (prompts::info) says, for each kind of job (InteractKind), what to call the thing ("THE WHEEL"), what the job
// is in a few words ("TAKE IT OFF"), how big a target it is, and which key does it. The pick (Interact::pick) aims with a
// cone around the crosshair that counts the size of the target and stays on what it has (hysteresis); the markers and the
// prompt line near the crosshair are built from the same list of jobs, so a new IA_ kind gets all of it with one row in the
// table and an `at` where it is. Nothing here draws: the game turns a Focus into pixels (main.cpp).
#pragma once
#include <string>
#include <vector>

#include "interact.h"

namespace prompts {

enum Key { KEY_INTERACT, KEY_GET_IN };  // E (the pad's X), held or tapped; J (the pad's LB + Y)

struct KindInfo {
    const char* noun;   // "THE WHEEL": what a marker of this kind sits on (the hint: "LOOK AT THE WHEEL TO TAKE IT OFF")
    const char* verb;   // "TAKE IT OFF"
    double radius;      // m: how big a target it is (the aim counts the edge, not just the middle)
    Key key;
};
// The table. An unknown kind gets a generic row.
const KindInfo& info(InteractKind k);

constexpr double kReach = 3.0;                       // m from your chest that a job can be done
constexpr double kCone = 40 * 3.14159265358979 / 180;  // off the line of sight to the edge of the target
constexpr double kStickyBonus = 0.12;                // rad: what the target you have is ahead by
constexpr double kHoldBonus = 0.6;                   // ...while you're holding E on it
constexpr double kMarkerRange = 5.0;                 // m from your chest that markers show

// How well `at` (a target `radius` m across) is aimed at: `score`, lower is better; false if it is out of reach or out of the
// cone (to its edge: a big target is easier to hit; the score itself is the angle to the middle, as ever). `bias` is added (the caller's
// priorities, distance); `sticky`: the target you have already (it scores better and its cone is wider).
bool aimScore(const V3& eye, const V3& dir, const V3& chest, const V3& at, double radius, double reach, double bias, bool sticky, double stickyBonus, double& score);

// The label and the reason from an option's prompt: "TAKE THE WHEEL OFF: NEEDS LUG WRENCH" (blocked) is two lines.
void split(const InteractOption& o, std::string& label, std::string& reason);

struct Marker {
    V3 at;
    double radius = 0.3;
    InteractKind kind = IA_NONE;
    Key key = KEY_INTERACT;
    bool hold = false;     // hold the key (else tap)
    bool blocked = false;  // can't be done yet: the reason says why
    bool far = false;      // beyond reach: get closer
    bool current = false;  // the target
    int car = -1;
    std::string label, reason;
};

struct Focus {
    std::vector<Marker> markers;  // each spot there is something to do, nearest first
    int target = -1;              // the marker the crosshair is on (an E job in reach, else the door), -1 none
    std::string hint;             // nothing targeted but something near: "LOOK AT THE WHEEL TO TAKE IT OFF"
    const Marker* current() const { return target >= 0 ? &markers[(size_t)target] : nullptr; }
};

// Once a frame on foot (after Interact::update): the markers within a few metres of a car, the target, the hint.
// `nearby` is what Interact::pick listed (PickOpts::nearby; the vehicle's door among it: InteractOption::passive); the target
// is Interact::current, so the prompt and what E does are never two things, and the hysteresis is the pick's.
class Prompter {
public:
    Focus update(const Sim& sim, const Walker& wk, const Interact& ia, const std::vector<InteractOption>& nearby, const V3& eye, const V3& look);
};

// The driver's door of `c`: where you stand to get in, at hand height.
V3 doorAt(const Car& c);

}  // namespace prompts
