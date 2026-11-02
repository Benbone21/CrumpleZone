// Garage attachments: the winch and the steel bumper (attachments.h).
#include "attachments.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

// The top of the body at its very front (the nose's top edge, under the hood's leading edge).
double noseTop(const VehicleSpec& vs) {
    if (!vs.topLine.empty() && vs.topLine[0] > 0) return vs.topLine[0];
    int top = 0;
    for (int k = 0; k < (int)vs.profile.size(); k++)
        if (!vs.profile[k].empty() && vs.profile[k][0] != '.') top = k + 1;
    return vs.layerY[std::min(top, (int)vs.layerY.size() - 1)] - vs.noseDrop;
}

void addWinch(VehicleSpec& s) {
    s.extras |= EXTRA_WINCH;
    s.winchKit = true;
    s.winchPull = kWinchKitPull;
    const AttachFit f = attachFit(s);  // (where it sits on the car as it is so far: carbuild puts the mass on the nodes there)
    s.items.push_back({"winch, line and mount plate", kWinchKitKg, f.winch.x, f.winch.y, 0});
}

// Off a truck that came with one: its weight comes off the front (an item that's the winch alone goes;
// a bumper hoop it shares an item with stays, less the winch).
void removeWinch(VehicleSpec& s) {
    s.extras &= ~(unsigned)EXTRA_WINCH;
    for (size_t i = 0; i < s.items.size();) {
        const std::string n = s.items[i].name;
        if (n.find("winch") == std::string::npos) {
            i++;
            continue;
        }
        if (n.find("bumper") == std::string::npos) {
            s.items.erase(s.items.begin() + i);
            continue;
        }
        s.items[i].kg = std::max(0.0, s.items[i].kg - 32);  // (a 9,500 lb winch and its line)
        i++;
    }
}

void addBumper(VehicleSpec& s) {
    s.steelBumper = true;
    const AttachFit f = attachFit(s);
    // (One item for the weight shown and summed; carbuild hangs it on the bumper's own nodes.)
    s.items.push_back({"steel bumper and grille guard", kSteelBumperKg, f.noseX + f.depth * 0.5, 0.5 * (f.y0 + f.y1), 0});
}

ModOption option(int id, int tier, const char* name, const char* detail, void (*apply)(VehicleSpec&)) {
    ModOption o{name, detail, [apply](VehicleSpec& s) {
                    if (apply) apply(s);
                }};
    o.fromOption = id;
    o.tier = tier;
    return o;
}

}  // namespace

AttachFit attachFit(const VehicleSpec& vs) {
    AttachFit f;
    f.noseX = vs.length / 2;
    // The lowest row of the nose (lifted for the approach angle), and the row above it.
    const double bottom = vs.layerY[0] + vs.bumperLift;
    const double row1 = vs.layerY.size() > 1 ? vs.layerY[1] : bottom + 0.3;
    f.y0 = bottom - 0.03;
    f.y1 = clampd(row1, f.y0 + 0.26, f.y0 + 0.38);
    const double bw = vs.bodyWidth.empty() ? 1.0 : vs.bodyWidth[0];
    f.halfW = std::max(0.45, vs.width / 2 * bw - vs.cornerRound * 0.5 + 0.02);
    f.depth = 0.17;
    f.faceX = f.noseX + f.depth;
    // The guard reaches up to just over the nose's top edge (the headlights and grille it covers), at most 0.58 m over the bumper.
    f.guardTop = std::max(f.y1 + 0.20, std::min(noseTop(vs) + 0.04, f.y1 + 0.58));
    f.guardHalfW = f.halfW * 0.78;
    // A 12,000 lb winch (or the factory one, smaller by its pull).
    f.winchScale = clampd(std::cbrt((vs.winchPull > 0 ? vs.winchPull : kWinchKitPull) / kWinchKitPull), 0.7, 1.0);
    if (vs.steelBumper) {
        // In the bumper's cradle, its fairlead on the bumper's face.
        f.winch = V3(f.noseX + 0.075, f.y0 + 0.5 * (f.y1 - f.y0), 0);
        f.fairlead = V3(f.faceX + 0.01, f.winch.y, 0);
    } else {
        // On its own plate bolted ahead of the nose, low in front of the bumper cover.
        f.winch = V3(f.noseX + 0.095 * f.winchScale, bottom + 0.11, 0);
        f.fairlead = V3(f.winch.x + 0.10 * f.winchScale, f.winch.y, 0);
    }
    return f;
}

std::vector<ModOption> winchOptions(const VehicleSpec& vs) {
    std::vector<ModOption> v;
    if (vs.extras & EXTRA_WINCH) {
        char d[64];
        const int lb = (int)std::lround(vs.winchPull / 4.448 / 500) * 500;
        std::snprintf(d, sizeof d, "FACTORY %d,%03d LB WINCH", lb / 1000, lb % 1000);
        v.push_back(option(0, 1, "WINCH", d, nullptr));
        ModOption off = option(1, 1, "NO WINCH", "TAKES THE WINCH OFF", removeWinch);
        off.removal = true;
        v.push_back(off);
    } else {
        v.push_back(option(0, 1, "NO WINCH", "NONE FITTED", nullptr));
        v.push_back(option(1, 1, "WINCH", "12,000 LB WINCH  30 M LINE  +40 KG", addWinch));
    }
    return v;
}

std::vector<ModOption> bumperOptions(const VehicleSpec&) {
    return {option(0, 1, "STOCK BUMPER", "AS IT CAME", nullptr),
            option(1, 1, "STEEL BUMPER", "GRILLE GUARD  TOUGHER NOSE  +75 KG", addBumper)};
}
