// Anchor gear (anchors.h): the surface rules and the names.
#include "anchors.h"

namespace anchors {

const char* gearName(AnchorGear g) {
    static const char* n[] = {"TREE STRAP", "CHAIN SLING", "GROUND STAKE", "ROCK BOLT", "ICE ANCHOR"};
    return n[g];
}
std::string gearKey(AnchorGear g) { return std::string("ANCHOR:") + gearName(g); }
const char* gearModel(AnchorGear g) {
    static const char* m[] = {"tree_saver", "chain_sling", "ground_stake", "rock_bolt", "ice_anchor"};
    return m[g];
}
const char* gearShort(AnchorGear g) {
    static const char* s[] = {"STRAPS", "SLING", "STAKES", "BOLTS", "ICE"};
    return s[g];
}

GroundRule groundRule(Surface s, double water) {
    GroundRule r;
    if (water > kGroundMaxWater) {  // (mud under water, a stream bed: it'd pull straight out)
        r.why = "TOO DEEP TO STAKE";
        return r;
    }
    switch (s) {
        case SURF_DIRT: case SURF_GRASS: case SURF_GRAVEL: case SURF_SAND: case SURF_MUD:
            r.gear = GEAR_STAKE, r.needs = "NO GROUND STAKES LEFT";
            break;
        case SURF_ROCK:
            r.gear = GEAR_BOLT, r.needs = "CAN'T STAKE ON ROCK: NEEDS A ROCK BOLT";
            break;
        case SURF_SNOW:
            r.gear = GEAR_ICE, r.needs = "CAN'T STAKE ON SNOW: NEEDS AN ICE ANCHOR";
            break;
        case SURF_ICE:
            r.gear = GEAR_ICE, r.needs = "CAN'T STAKE ON ICE: NEEDS AN ICE ANCHOR";
            break;
        case SURF_SCREE:
            r.why = "CAN'T STAKE ON SCREE: IT SLIDES";
            break;
        default:  // paved, wet tarmac
            r.why = "CAN'T STAKE ON PAVEMENT";
    }
    return r;
}

std::string hudLine(const AnchorStock& s) {
    std::string out;
    for (int g = 0; g < GEAR_COUNT; g++) out += (g ? " " : "") + std::string(gearShort((AnchorGear)g)) + " " + std::to_string(s.n[g]);
    return out;
}

}  // namespace anchors
