// On-foot prompts (prompts.h): the table of kinds, the aim, the markers and the hint.
#include "prompts.h"

#include <algorithm>
#include <cmath>

#include "walker.h"

namespace prompts {

const KindInfo& info(InteractKind k) {
    // noun, verb, radius, key. A new IA_ kind adds a row (and gets a marker, a prompt and a hint for free).
    static const KindInfo kTake{"THE SPARE", "TAKE IT", 0.5, KEY_INTERACT}, kWheel{"THE WHEEL", "REMOVE IT", 0.45, KEY_INTERACT},
        kFit{"THE BARE HUB", "MOUNT WHEEL", 0.45, KEY_INTERACT}, kJackStow{"THE JACK", "TAKE IT", 0.35, KEY_INTERACT},
        kChain{"THE SNOW CHAINS", "TAKE ONE", 0.35, KEY_INTERACT}, kLoose{"THE WHEEL ON THE GROUND", "PICK IT UP", 0.4, KEY_INTERACT},
        kJack{"THE JACK", "WORK IT", 0.4, KEY_INTERACT}, kCache{"THE SUPPLY CACHE", "OPEN IT", 0.5, KEY_INTERACT},
        kFix{"THE PART THAT NEEDS WORK", "FIX IT", 0.3, KEY_INTERACT}, kBack{"THE BACK", "OPEN TRUNK", 0.6, KEY_INTERACT},
        kItem{"THE BACK", "TAKE OR PUT BACK", 0.6, KEY_INTERACT}, kLine{"THE RECOVERY POINT", "GRAB LINE", 0.3, KEY_INTERACT},
        kHook{"AN ANCHOR", "HOOK LINE", 0.4, KEY_INTERACT}, kUnhook{"THE HOOKED LINE", "UNHOOK IT", 0.3, KEY_INTERACT},
        kTire{"THE WHEEL ON THE GROUND", "MOUNT NEW TIRE", 0.4, KEY_INTERACT}, kDoor{"THE DRIVER DOOR", "GET IN", 0.55, KEY_GET_IN},
        kRide{"THE PASSENGER DOOR", "RIDE ALONG", 0.55, KEY_GET_IN},
        kAirUp{"THE TIRE", "AIR UP", 0.4, KEY_INTERACT}, kAirDown{"THE TIRE", "LET AIR OUT", 0.4, KEY_INTERACT},
        kHoodUp{"THE HOOD", "OPEN IT", 0.5, KEY_INTERACT}, kHoodDown{"THE HOOD", "CLOSE IT", 0.5, KEY_INTERACT},
        kJump{"THE BATTERY", "JUMP START IT", 0.35, KEY_INTERACT}, kClamp{"THE BATTERY", "CLAMP THE CABLES ON", 0.35, KEY_INTERACT},
        kUnclamp{"THE CLAMPS", "TAKE THE CABLES OFF", 0.3, KEY_INTERACT}, kOther{"A MARKER", "USE IT", 0.3, KEY_INTERACT};
    switch (k) {
        case IA_TAKE_SPARE: case IA_STOW_SPARE: return kTake;
        case IA_REMOVE_WHEEL: case IA_RESEAT_TIRE: return kWheel;
        case IA_FIT_WHEEL: return kFit;
        case IA_PICK_UP: return kLoose;
        case IA_TAKE_JACK: case IA_STOW_JACK: return kJackStow;
        case IA_PLACE_JACK: case IA_RAISE_JACK: case IA_LOWER_JACK: case IA_REMOVE_JACK: return kJack;
        case IA_TAKE_CHAIN: case IA_STOW_CHAIN: case IA_FIT_CHAIN: case IA_REMOVE_CHAIN: return kChain;
        case IA_OPEN_CACHE: return kCache;
        case IA_FIX: return kFix;
        case IA_OPEN_INVENTORY: return kBack;
        case IA_TAKE_ITEM: case IA_STOW_ITEM: return kItem;
        case IA_TIRE_WORK: return kTire;
        case IA_GRAB_LINE: case IA_STOW_LINE: return kLine;
        case IA_HOOK_LINE: return kHook;
        case IA_UNHOOK_LINE: return kUnhook;
        case IA_ENTER_CAR: return kDoor;  // (carentry.h)
        case IA_RIDE_ALONG: return kRide;
        case IA_AIR_UP_TIRE: return kAirUp;
        case IA_AIR_DOWN_TIRE: return kAirDown;
        case IA_OPEN_HOOD: return kHoodUp;
        case IA_CLOSE_HOOD: return kHoodDown;
        case IA_JUMP_PACK: return kJump;
        case IA_CLAMP_CABLE: return kClamp;
        case IA_UNCLAMP_CABLE: return kUnclamp;
        case IA_NONE: return kDoor;  // (the door is the one marker that is no InteractKind: J, not E)
        default: return kOther;
    }
}

bool aimScore(const V3& eye, const V3& dir, const V3& chest, const V3& at, double radius, double reach, double bias, bool sticky, double stickyBonus, double& score) {
    if (len(at - chest) > reach) return false;
    const V3 to = at - eye;
    const double d = len(to);
    double off = 0, edge = 1.5;
    if (d > 1e-6) off = std::acos(clampd(dot(to / d, dir), -1, 1)), edge = std::atan2(radius, d);
    if (off - edge > kCone * (sticky ? 1.35 : 1.0)) return false;
    score = off + bias - (sticky ? stickyBonus : 0.0);
    return true;
}

void split(const InteractOption& o, std::string& label, std::string& reason) {
    label = o.prompt, reason.clear();
    if (o.kind == IA_OPEN_INVENTORY && o.prompt == "INVENTORY") label = "OPEN TRUNK";
    if (!o.blocked) return;
    const size_t c = o.prompt.rfind(": ");
    if (c == std::string::npos) return;
    label = o.prompt.substr(0, c), reason = o.prompt.substr(c + 2);
}

V3 doorAt(const Car& c) {
    const Frame f = c.frame();
    const V3 fwd = norm(V3(f.fwd.x, 0, f.fwd.z)), left(fwd.z, 0, -fwd.x);
    return f.com + left * (c.spec->width / 2) + fwd * 0.3 + V3(0, 0.2, 0);
}

Focus Prompter::update(const Sim& sim, const Walker& wk, const Interact& ia, const std::vector<InteractOption>& nearby, const V3& eye, const V3& look) {
    (void)sim;
    Focus fo;
    const V3 chest = wk.p + V3(0, 1.0, 0), dir = norm(look);
    const bool haveCurrent = ia.current.kind != IA_NONE || ia.current.passive;
    for (const InteractOption& o : nearby) {
        if (o.kind == IA_NONE && !o.passive) continue;
        Marker m;
        m.at = o.at, m.radius = o.radius > 0 ? o.radius : info(o.kind).radius, m.kind = o.kind, m.key = info(o.kind).key;
        m.hold = o.hold > 0 || o.continuous, m.blocked = o.blocked, m.car = o.car;
        m.current = haveCurrent && o.same(ia.current);
        const bool door = o.passive || o.kind == IA_ENTER_CAR || o.kind == IA_RIDE_ALONG;
        m.far = len(o.at - chest) > (door ? kReach + 1.0 : o.kind == IA_OPEN_INVENTORY ? 3.2 : kReach);
        split(o, m.label, m.reason);
        // One marker to a spot: the target, then the one that can be done, then the first.
        bool merged = false;
        for (Marker& e : fo.markers) {
            if (e.car != m.car || len(e.at - m.at) > 0.4) continue;
            merged = true;
            if (!e.current && (m.current || (e.blocked && !m.blocked))) e = m;
            break;
        }
        if (!merged) fo.markers.push_back(m);
    }
    // Nearest first (the target kept), and not a forest of them.
    std::stable_sort(fo.markers.begin(), fo.markers.end(), [&](const Marker& a, const Marker& b) {
        if (a.current != b.current) return a.current;
        return len(a.at - chest) < len(b.at - chest);
    });
    if (fo.markers.size() > 14) fo.markers.resize(14);
    for (size_t i = 0; i < fo.markers.size(); i++)
        if (fo.markers[i].current) fo.target = (int)i;
    // Nothing targeted, something near: what to look at.
    if (fo.target < 0 && !fo.markers.empty()) {
        double bestOff = 1e9;
        const Marker* b = nullptr;
        for (const Marker& m : fo.markers) {
            const V3 to = m.at - eye;
            const double d = std::max(1e-6, len(to)), off = std::acos(clampd(dot(to / d, dir), -1, 1)) + (m.blocked ? 0.15 : 0) + (m.kind == IA_NONE || m.kind == IA_ENTER_CAR || m.kind == IA_RIDE_ALONG ? 0.1 : 0) + (m.far ? 0.5 : 0);
            if (off < bestOff) bestOff = off, b = &m;
        }
        if (b) {
            const KindInfo& ki = info(b->kind);
            fo.hint = std::string(b->far ? "WALK UP TO " : "LOOK AT ") + ki.noun + " TO " + ki.verb;
        }
    }
    return fo;
}

}  // namespace prompts
