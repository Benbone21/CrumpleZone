// Which engine, gearbox and transfer case model (and which design of it, set how) a car's spec is drawn with: the
// small block or the 454 or the LS in the Blazer, a VTEC head or the SOHC in the Civic, a TH350 or the SM465 behind
// it (src/models/drivetrain.h lists the designs). Read off the spec's engine and gearbox names and its layout, so a
// swapped-in engine looks like itself in any car. Used under the car and in the engine bay (carparts.cpp) and on
// the garage shop's turntable (garageshop.cpp). No GL; nothing allocated (it's asked every frame).
#pragma once
#include "vehicles.h"

struct PartLook {
    const char* id = "";  // "" for none (no transfer case drawn)
    int variant = 0;
    int n = 0;            // params set: set[0..n)
    struct Set {
        const char* key;
        double value;
    } set[4] = {};
    void add(const char* key, double value) {
        if (n < 4) set[n++] = {key, value};
    }
};

PartLook engineLook(const VehicleSpec& vs);
PartLook gearboxLook(const VehicleSpec& vs);
PartLook transferLook(const VehicleSpec& vs);  // id "" when none is drawn (no transfer case; an on-demand front; a mid engine's)
