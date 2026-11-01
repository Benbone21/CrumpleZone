#include "mods.h"

#include "platform.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

const char* modSlotName(int slot) {
    static const char* names[MOD_SLOTS] = {"ENGINE", "SUSPENSION", "TRANSMISSION", "WHEELS", "TIRES"};
    return slot >= 0 && slot < MOD_SLOTS ? names[slot] : "";
}

namespace {

using Curve = std::vector<TorquePoint>;

// ---- Building blocks ----------------------------------------------------------

void engine(VehicleSpec& s, const char* name, const Curve& torque, double redline, double turboLag) {
    s.engineName = name;
    s.torque = torque;
    for (TorquePoint& tp : s.torque) tp.nm *= 1.15;  // (as the stock engines: see vehicleCatalog)
    s.redline = redline;
    s.turboLag = turboLag;
}

// Raise (+) or lower (-) the body on its springs, and scale the spring and bar rates.
void springs(VehicleSpec& s, double rideHeight, double rate, double bars, double damping) {
    raiseBody(s, rideHeight);
    s.rideFreqFront *= std::sqrt(rate);
    s.rideFreqRear *= std::sqrt(rate);
    s.arbFront *= bars;
    s.arbRear *= bars;
    s.dampingRatio = damping;
}

// New tires: the body rides higher by the change in radius (the axles move up with it).
void tires(VehicleSpec& s, double radius, double width, double extraKg, double grip, double lateral, double track) {
    raiseBody(s, radius - s.wheelRadius);
    s.wheelRadius = radius;
    s.wheelWidth = width;
    s.wheelMass += extraKg;
    s.tireGrip = grip;
    s.lateralGrip = lateral;
    if (track > 0) s.trackFront = s.trackRear = track;
}

void gears(VehicleSpec& s, const char* name, const std::vector<double>& g, double reverse, double finalDrive, double shiftTime) {
    s.gearboxName = name;
    s.gears = g;
    s.reverseRatio = reverse;
    s.finalDrive = finalDrive;
    s.shiftTime = shiftTime;
}

ModOption stock(const char* detail) {
    ModOption o{"STOCK", detail, [](VehicleSpec&) {}};
    o.tier = 1;
    return o;
}

// One of a vehicle's own parts. `id` is its number in the part's key ("ENGINE:blazer:2"): it stays
// the same when other parts come and go, because careers and saves name parts by it (the ids missing
// from a list are parts that were cut: docs/parts-simplified.md). `tier` is how a career gets it (career.h).
ModOption part(int id, int tier, const char* name, const char* detail, std::function<void(VehicleSpec&)> apply) {
    ModOption o{name, detail, std::move(apply)};
    o.fromOption = id;
    o.tier = tier;
    return o;
}

// ---- The catalog ----------------------------------------------------------------
// Each slot: stock and two or three upgrades, each clearly different (better / best, or off-road /
// street). The names are what they do, not what they're called in a parts catalog.

using Slots = std::vector<std::vector<ModOption>>;

Slots civicMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("1.6 FOUR  108 HP"),
        part(2, 1, "SPORT ENGINE", "REVVIER 1.6 FOUR  160 HP  +20 KG", [](VehicleSpec& s) {
            s.voice.intake = 0.9f, s.voice.rasp = 0.75f, s.voice.muffler = 0.45f, s.voice.exhaustHz = 320, s.voice.roar = 0.35f;
            engine(s, "1.6L DOHC B16A VTEC",
                   {{0, 65}, {1000, 86}, {3000, 120}, {5000, 137}, {6000, 145}, {7000, 150}, {7600, 150}, {8200, 130}}, 8200, 0);
            s.engineMass += 20;
            s.engineLen = 0.54;
        }),
        part(3, 2, "RACE ENGINE", "1.8 FOUR  195 HP  +25 KG", [](VehicleSpec& s) {
            s.voice.intake = 0.95f, s.voice.rasp = 0.8f, s.voice.muffler = 0.55f, s.voice.exhaustHz = 290, s.voice.roar = 0.5f;
            engine(s, "1.8L DOHC B18C TYPE R",
                   {{0, 85}, {1000, 105}, {3000, 146}, {5000, 160}, {6200, 176}, {7300, 175}, {8000, 174}, {8400, 155}}, 8400, 0);
            s.displacement = 1.8;
            s.engineMass += 25;
            s.engineLen = 0.55;
        }),
    };
    m[MOD_SUSPENSION] = {
        stock("SOFT ROAD SPRINGS"),
        part(2, 1, "LOWERED", "-35 MM  STIFFER SPRINGS AND BARS", [](VehicleSpec& s) {
            springs(s, -0.035, 1.65, 1.25, 0.52);
            s.lateralGrip += 0.03;
        }),
        part(3, 2, "RALLY KIT", "+30 MM  SOFTER  GRAVEL TIRES", [](VehicleSpec& s) {
            springs(s, 0.03, 0.8, 0.7, 0.45);
            tires(s, 0.302, 0.185, 2, 0.96, 0.84, 0);
            s.tireType = TIRE_ALL_TERRAIN;  // gravel rally tires: knobbly, for loose stuff
        }),
    };
    m[MOD_TRANSMISSION] = {
        stock("5-SPEED MANUAL  OPEN DIFF"),
        part(2, 1, "SPORT GEARBOX", "CLOSER GEARS  GRIPPY DIFF", [](VehicleSpec& s) {
            gears(s, "5-SPEED CLOSE RATIO", {3.230, 2.105, 1.458, 1.107, 0.848}, 3.153, 4.4, 0.25);
            s.diffFront = DIFF_LSD;
        }),
        part(3, 2, "RACE GEARBOX", "INSTANT SHIFTS  GRIPPY DIFF", [](VehicleSpec& s) {
            gears(s, "5-SPEED SEQUENTIAL", {3.0, 2.0, 1.50, 1.15, 0.90}, 3.153, 4.6, 0.07);
            s.diffFront = DIFF_LSD;
            s.lsdBias = 4.0;
        }),
    };
    return m;
}

Slots jeskoMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("TWIN-TURBO V8  1280 HP"),
        part(1, 3, "MORE BOOST", "ON FLEX FUEL  1600 HP", [](VehicleSpec& s) {
            s.voice.crackle = 0.45f, s.voice.exhaustHz = 300, s.voice.roar = 0.7f;
            engine(s, "5.0L TWIN-TURBO V8 E85",
                   {{0, 500}, {1500, 700}, {2700, 1050}, {4000, 1350}, {5100, 1500}, {6500, 1500}, {7800, 1460}, {8500, 1300}}, 8500, 0.25);
        }),
        part(2, 3, "RACE TUNE", "RACE FUEL AND EVEN MORE BOOST  1750 HP", [](VehicleSpec& s) {
            s.voice.crackle = 0.6f, s.voice.muffler = 0.85f, s.voice.exhaustHz = 390, s.voice.roar = 0.9f, s.voice.rasp = 0.85f;
            engine(s, "5.0L TWIN-TURBO V8 RACE",
                   {{0, 520}, {1500, 720}, {2700, 1100}, {4000, 1450}, {5100, 1600}, {6500, 1600}, {7800, 1600}, {8500, 1420}}, 8500, 0.28);
        }),
    };
    m[MOD_SUSPENSION] = {
        stock("ROAD SETTING"),
        part(1, 2, "TRACK SUSPENSION", "-20 MM  MUCH STIFFER", [](VehicleSpec& s) { springs(s, -0.02, 1.5, 1.4, 0.55); }),
        part(2, 1, "LIFT KIT", "+40 MM  SOFTER", [](VehicleSpec& s) { springs(s, 0.04, 0.75, 0.8, 0.45); }),
        part(3, 2, "NO WING", "LESS DRAG, ALMOST NO DOWNFORCE", [](VehicleSpec& s) {
            s.dragArea = 0.53;
            s.downforce = 0.1;
            s.downforceMax = 1470;
            s.extras &= ~(unsigned)EXTRA_WING;
        }),
    };
    m[MOD_TRANSMISSION] = {
        stock("9-SPEED"),
        part(1, 1, "SHORT GEARS", "QUICKER PULL, LOWER TOP SPEED", [](VehicleSpec& s) { s.finalDrive = 3.6; }),
        part(2, 1, "TALL GEARS", "HIGHER TOP SPEED, SLOWER PULL", [](VehicleSpec& s) { s.finalDrive = 3.0; }),
    };
    return m;
}

Slots broncoMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("TWIN-TURBO V6  330 HP"),
        part(2, 1, "TUNED V6", "ENGINE TUNE  390 HP", [](VehicleSpec& s) {
            s.voice.muffler = 0.45f, s.voice.crackle = 0.15f, s.voice.exhaustHz = 135, s.voice.roar = 0.45f;
            engine(s, "2.7L TWIN-TURBO V6 STAGE 1",
                   {{0, 260}, {1000, 360}, {2000, 600}, {3000, 690}, {4500, 610}, {5250, 529}, {5800, 470}}, 5800, 0.45);
        }),
        part(3, 3, "BIG V6", "3.0 TWIN-TURBO V6  418 HP  +15 KG", [](VehicleSpec& s) {
            s.voice.muffler = 0.6f, s.voice.crackle = 0.25f, s.voice.rasp = 0.45f, s.voice.exhaustHz = 125, s.voice.roar = 0.6f;
            engine(s, "3.0L TWIN-TURBO V6 RAPTOR",
                   {{0, 280}, {1000, 380}, {2000, 560}, {3250, 597}, {4500, 580}, {5600, 532}, {6000, 490}}, 6000, 0.45);
            s.displacement = 3.0;
            s.engineMass += 15;
        }),
    };
    m[MOD_SUSPENSION] = {
        stock("33 IN TIRES"),
        part(1, 1, "LIFT KIT", "+25 MM  35 IN TIRES  WIDER", [](VehicleSpec& s) {
            tires(s, 0.445, 0.315, 8, 0.96, 0.74, 1.699);
            springs(s, 0.025, 0.95, 1.0, 0.38);
        }),
        part(2, 2, "BIG LIFT", "+75 MM  37 IN MUD TIRES  LONG TRAVEL", [](VehicleSpec& s) {
            tires(s, 0.47, 0.32, 16, 0.95, 0.72, 1.74);
            s.tireType = TIRE_MUD_TERRAIN;
            springs(s, 0.075, 0.85, 0.8, 0.36);
            s.bumpFront = 0.11, s.droopFront = 0.16, s.bumpRear = 0.13, s.droopRear = 0.20;
        }),
        part(3, 1, "LOWERED", "-35 MM  ROAD TIRES  STIFFER", [](VehicleSpec& s) {
            tires(s, 0.40, 0.265, -6, 1.05, 0.90, 0);
            s.tireType = TIRE_STREET;
            springs(s, -0.035, 1.3, 1.5, 0.45);
            s.bumpFront = 0.065, s.bumpRear = 0.08;
        }),
    };
    m[MOD_TRANSMISSION] = {
        stock("10-SPEED AUTO  OPEN DIFFS"),
        part(2, 2, "LOCKERS", "LOWER AXLE GEARS  LOCKING DIFFS FRONT AND REAR", [](VehicleSpec& s) {
            s.finalDrive = 4.70;
            s.lockerFront = s.lockerRear = true;
        }),
        part(3, 2, "MANUAL GEARBOX", "7-SPEED MANUAL  EXTRA-LOW CRAWL GEAR", [](VehicleSpec& s) {
            gears(s, "7-SPEED MANUAL", {6.588, 4.283, 2.365, 1.453, 1.000, 0.776, 0.646}, 5.625, 4.70, 0.35);
        }),
    };
    return m;
}

Slots runnerMods() {
    // The 4Runner is modelled as one particular built truck, so option 0 is the truck as
    // it was sold; the others take it further.
    auto asBuilt = [](const char* detail) {
        ModOption o = stock(detail);
        o.name = "AS BUILT";
        return o;
    };
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        asBuilt("SUPERCHARGED V6  256 HP"),
        part(2, 1, "BIG V6", "4.0 V6  270 HP  LIGHTER", [](VehicleSpec& s) {
            s.voice.rasp = 0.55f, s.voice.muffler = 0.42f, s.voice.intake = 0.6f, s.voice.mech = 0.15f, s.voice.exhaustHz = 190, s.voice.roar = 0.4f;  // (crisper, an intake roar)
            engine(s, "4.0L V6 1GR-FE",
                   {{0, 220}, {1000, 280}, {2000, 345}, {3000, 362}, {4400, 377}, {5000, 368}, {5600, 343}, {6100, 310}}, 6100, 0);
            s.displacement = 4.0;
            s.induction = VehicleSpec::NATURAL;
            s.engineMass -= 33;
        }),
        part(3, 2, "BIG V6 SUPERCHARGED", "4.0 V6 WITH A SUPERCHARGER  340 HP", [](VehicleSpec& s) {
            s.voice.rasp = 0.4f, s.voice.muffler = 0.5f, s.voice.exhaustHz = 205, s.voice.roar = 0.45f;
            engine(s, "4.0L V6 1GR-FE SUPERCHARGED",
                   {{0, 290}, {1000, 370}, {2000, 440}, {3000, 465}, {4000, 470}, {5000, 455}, {5600, 425}, {6100, 380}}, 6100, 0);
            s.displacement = 4.0;
            s.engineMass -= 15;
        }),
    };
    m[MOD_SUSPENSION] = {
        asBuilt("2 IN LIFT  35 IN TIRES"),
        part(2, 2, "BIG LIFT", "+50 MM  LONG TRAVEL  37 IN MUD TIRES", [](VehicleSpec& s) {
            tires(s, 0.47, 0.318, 8, 0.93, 0.70, 1.66);
            s.tireType = TIRE_MUD_TERRAIN;
            springs(s, 0.05, 0.85, 0.75, 0.38);
            s.bumpFront = 0.11, s.droopFront = 0.16, s.bumpRear = 0.13, s.droopRear = 0.18;
        }),
        part(3, 1, "LOWERED", "FACTORY HEIGHT -25 MM  ROAD TIRES  STIFFER", [](VehicleSpec& s) {
            tires(s, 0.381, 0.275, -10, 1.02, 0.88, 1.53);
            s.tireType = TIRE_STREET;
            springs(s, -0.065 - 0.051 - 0.025, 1.15, 1.5, 0.45);
        }),
    };
    m[MOD_TRANSMISSION] = {
        asBuilt("4-SPEED AUTO  OPEN DIFFS"),
        part(2, 2, "LOCKERS", "LOWER AXLE GEARS  LOCKING DIFFS FRONT AND REAR", [](VehicleSpec& s) {
            s.finalDrive = 4.88;
            s.lockerFront = s.lockerRear = true;
            s.rearLockerLowOnly = false;  // air lockers: they lock in any range
        }),
        part(3, 2, "CRAWL GEARS", "LOCKERS  EXTRA-LOW LOW RANGE", [](VehicleSpec& s) {
            s.finalDrive = 4.88;
            s.lockerFront = s.lockerRear = true;
            s.rearLockerLowOnly = false;
            s.lowRange = 4.7;
            s.gearboxName = "A340F + 4.7 LOW RANGE";
        }),
    };
    return m;
}

Slots buggyMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("6.2 V8  450 HP"),
        part(1, 3, "BIG V8", "7.0 V8  505 HP  +8 KG", [](VehicleSpec& s) {
            // Estimated: LS7 crate figures (505 hp, 470 lb-ft) with a truck-friendly curve.
            s.voice.lope = 0.45f, s.voice.rasp = 0.25f, s.voice.exhaustHz = 85, s.voice.rumble = 0.75f, s.voice.roar = 0.95f;
            engine(s, "7.0L LS7 V8",
                   {{0, 340}, {1000, 410}, {2000, 520}, {3000, 590}, {4000, 630}, {4800, 650}, {5600, 620}, {6300, 574}, {6800, 520}}, 6800, 0);
            s.displacement = 7.0;
            s.engineMass += 8;
        }),
        part(2, 3, "SUPERCHARGED V8", "6.2 V8 WITH A SUPERCHARGER  650 HP  +30 KG", [](VehicleSpec& s) {
            s.voice.lope = 0.25f, s.voice.intake = 0.4f, s.voice.exhaustHz = 112, s.voice.rumble = 0.5f, s.voice.roar = 0.8f;
            engine(s, "6.2L LS V8 SUPERCHARGED",
                   {{0, 420}, {1000, 520}, {2000, 690}, {3000, 780}, {4000, 820}, {5000, 810}, {5800, 740}, {6400, 640}}, 6400, 0);
            s.induction = VehicleSpec::SUPERCHARGER;
            s.engineMass += 30;
        }),
    };
    m[MOD_SUSPENSION] = {
        stock("SOLID AXLES  40 IN TIRES"),
        part(1, 1, "HUGE TIRES", "42 IN TIRES  +25 MM  WIDER", [](VehicleSpec& s) {
            tires(s, 0.533, 0.368, 9, 0.95, 0.69, 1.92);
            s.bumpFront += 0.02, s.droopFront += 0.02, s.bumpRear += 0.02, s.droopRear += 0.02;
            s.coilTop += 0.03;
        }),
        part(2, 2, "DESERT KIT", "FIRMER  RACE SHOCKS  36 IN TIRES", [](VehicleSpec& s) {
            tires(s, 0.457, 0.30, -14, 0.98, 0.76, 1.84);
            springs(s, 0.0, 1.5, 1.0, 0.55);
            s.arbFront = s.arbRear = 6000;
        }),
    };
    m[MOD_TRANSMISSION] = {
        stock("4-SPEED AUTO  LOW RANGE  OPEN DIFFS"),
        part(1, 1, "3-SPEED AUTO", "THREE GEARS, FEWER SHIFTS", [](VehicleSpec& s) {
            gears(s, "TH400 3-SPEED + ATLAS 4.3 LOW", {2.48, 1.48, 1.00}, 2.07, 5.38, 0.35);
        }),
        part(2, 2, "CRAWL GEARS", "LOWER LOW RANGE: SLOWER, STRONGER CRAWL", [](VehicleSpec& s) {
            s.lowRange = 5.0;
            s.gearboxName = "4L80E + ATLAS 5.0 LOW";
        }),
    };
    return m;
}

Slots tacomaMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("3.5 V6  278 HP"),
        part(1, 1, "TUNED V6", "INTAKE AND ENGINE TUNE  295 HP", [](VehicleSpec& s) {
            s.voice.muffler = 0.45f, s.voice.intake = 0.55f, s.voice.exhaustHz = 190, s.voice.roar = 0.3f;
            engine(s, "3.5L V6 2GR-FKS TUNED",
                   {{0, 205}, {1000, 270}, {1600, 300}, {2000, 314}, {3000, 347}, {4000, 367}, {4600, 372}, {5000, 368}, {5500, 356}, {6000, 344}, {6400, 320}}, 6400, 0);
        }),
        part(2, 2, "SUPERCHARGED V6", "350 HP  +14 KG", [](VehicleSpec& s) {
            s.voice.muffler = 0.5f, s.voice.exhaustHz = 160, s.voice.roar = 0.35f;
            engine(s, "3.5L V6 2GR-FKS SUPERCHARGED",
                   {{0, 260}, {1000, 330}, {1600, 380}, {2000, 405}, {3000, 435}, {4000, 450}, {4600, 448}, {5000, 440}, {5500, 425}, {6000, 405}, {6400, 375}}, 6400, 0);
            s.induction = VehicleSpec::SUPERCHARGER;
            s.engineMass += 14;
        }),
    };
    m[MOD_SUSPENSION] = {
        stock("ALL-TERRAIN TIRES"),
        part(1, 1, "LIFT KIT", "+75 MM  33 IN TIRES", [](VehicleSpec& s) {
            tires(s, 0.42, 0.285, 6, 0.95, 0.76, 1.63);
            springs(s, 0.075, 1.05, 0.9, 0.40);
            s.bumpFront = 0.09, s.droopFront = 0.13, s.droopRear = 0.16;
        }),
        part(2, 2, "BIG LIFT", "+50 MM  LONG TRAVEL  35 IN MUD TIRES", [](VehicleSpec& s) {
            tires(s, 0.445, 0.318, 14, 0.93, 0.72, 1.70);
            s.tireType = TIRE_MUD_TERRAIN;
            springs(s, 0.05, 0.9, 0.7, 0.38);
            s.bumpFront = 0.12, s.droopFront = 0.17, s.bumpRear = 0.13, s.droopRear = 0.19;
            s.wellTireRadius = 0.46, s.wellTireWidth = 0.32;  // (cut for them)
        }),
        part(3, 1, "LOWERED", "-25 MM  ROAD TIRES  STIFFER", [](VehicleSpec& s) {
            tires(s, 0.385, 0.265, -4, 1.02, 0.88, 1.60);
            s.tireType = TIRE_STREET;
            springs(s, -0.025, 1.25, 1.4, 0.45);
            s.bumpFront = 0.065, s.bumpRear = 0.08;
        }),
    };
    m[MOD_TRANSMISSION] = {
        stock("6-SPEED AUTO  REAR LOCKER IN 4L"),
        part(2, 2, "LOCKERS", "LOWER AXLE GEARS  A FRONT LOCKER TOO", [](VehicleSpec& s) {
            s.finalDrive = 4.88;
            s.lockerFront = true;
        }),
        part(3, 2, "CRAWL GEARS", "LOCKERS  EXTRA-LOW LOW RANGE", [](VehicleSpec& s) {
            s.finalDrive = 4.88;
            s.lockerFront = true;
            s.lowRange = 4.3;
            s.gearboxName = "6-SPEED AUTO + 4.3 LOW RANGE";
        }),
    };
    return m;
}

Slots utvMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("TURBO TRIPLE  195 HP"),
        part(1, 2, "RACE TUNE", "BIGGER TURBO  230 HP", [](VehicleSpec& s) {
            // Estimated bolt-on figures for the Rotax 900 turbo.
            s.voice.muffler = 0.8f, s.voice.crackle = 0.5f, s.voice.exhaustHz = 335, s.voice.roar = 0.75f;
            engine(s, "900CC TURBO I3 STAGE 2",
                   {{0, 95}, {1500, 120}, {3000, 190}, {4500, 232}, {5500, 242}, {6500, 238}, {7500, 220}, {8000, 195}, {8400, 150}}, 8300, 0.5);
            s.engineMass += 4;
        }),
        part(2, 1, "TRAIL TUNE", "GENTLER FOR TRAIL RIDES  150 HP", [](VehicleSpec& s) {
            s.voice.muffler = 0.45f, s.voice.crackle = 0.0f, s.voice.exhaustHz = 270, s.voice.roar = 0.3f;
            engine(s, "900CC TURBO I3 ECO",
                   {{0, 85}, {1500, 100}, {3000, 140}, {4500, 165}, {5500, 172}, {6500, 168}, {7500, 150}, {8000, 130}, {8400, 100}}, 8000, 0.3);
        }),
    };
    m[MOD_SUSPENSION] = {
        stock("LONG-TRAVEL ARMS  32 IN TIRES"),
        part(1, 2, "RACE SHOCKS", "FIRMER SPRINGS, BARS AND DAMPING", [](VehicleSpec& s) { springs(s, 0.0, 1.35, 1.4, 0.5); }),
        part(2, 1, "SOFT RIDE", "+25 MM  SOFTER  MORE DROOP", [](VehicleSpec& s) {
            springs(s, 0.025, 0.8, 0.7, 0.36);
            s.droopFront += 0.03, s.droopRear += 0.03;
        }),
    };
    m[MOD_TRANSMISSION] = {
        stock("CVT  4WD  FRONT DIFF LOCK"),
        part(1, 1, "SHORT GEARS", "QUICKER PULL, LOWER TOP SPEED", [](VehicleSpec& s) { s.finalDrive = 10.2; }),
        part(2, 2, "REAR LOCKER", "LOCKING DIFFS FRONT AND REAR", [](VehicleSpec& s) { s.lockerRear = true; }),
    };
    return m;
}

Slots blazerMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("350 V8  165 HP"),
        part(2, 1, "BIG V8", "7.4 V8  240 HP  BIG TORQUE  +70 KG", [](VehicleSpec& s) {
            // 1972 LS5 truck rating ~230-240 hp net, 360-375 lb-ft (estimate from GM's 1972 figures).
            s.voice.rasp = 0.04f, s.voice.muffler = 0.65f, s.voice.mech = 0.45f, s.voice.lope = 0.1f, s.voice.exhaustHz = 75, s.voice.rumble = 0.95f, s.voice.roar = 0.95f;  // (a big, slow, deep roar and a chest-thumping rumble)
            engine(s, "454 BIG BLOCK V8",
                   {{0, 330}, {800, 410}, {1200, 450}, {1800, 490}, {2400, 505}, {2800, 500}, {3600, 470}, {4000, 440}, {4400, 395}, {4800, 340}}, 4800, 0);
            s.displacement = 7.4;
            s.engineMass += 70;
        }),
        part(3, 2, "HOT ROD V8", "BUILT 350 V8  355 HP  LOPEY IDLE", [](VehicleSpec& s) {
            s.voice.lope = 0.65f, s.voice.muffler = 0.7f, s.voice.crackle = 0.3f, s.voice.intake = 0.7f, s.voice.exhaustHz = 115, s.voice.rumble = 0.75f, s.voice.roar = 0.8f;
            engine(s, "ZZ4 350 CRATE V8",
                   {{0, 300}, {1000, 400}, {2000, 470}, {3000, 535}, {3500, 549}, {4500, 520}, {5250, 480}, {5800, 420}, {6000, 390}}, 6000, 0);
            s.engineMass -= 15;
            s.idleRpm = 800;
        }),
    };
    m[MOD_SUSPENSION] = {
        stock("LEAF SPRINGS"),
        part(1, 1, "LIFT KIT", "+100 MM  35 IN MUD TIRES", [](VehicleSpec& s) {
            springs(s, 0.10, 0.9, 0.8, 0.34);
            tires(s, 0.445, 0.318, 14, 0.92, 0.72, 0);
            s.tireType = TIRE_MUD_TERRAIN;
            s.bumpFront += 0.02, s.droopFront += 0.04, s.bumpRear += 0.02, s.droopRear += 0.05;
        }),
        part(2, 1, "SOFT SPRINGS", "+50 MM  SOFTER  MORE FLEX", [](VehicleSpec& s) {
            springs(s, 0.05, 0.7, 0.5, 0.30);
            s.droopFront += 0.03, s.droopRear += 0.04;
        }),
    };
    m[MOD_TRANSMISSION] = {
        stock("4-SPEED MANUAL  OPEN DIFFS"),
        part(1, 1, "AUTOMATIC", "3-SPEED AUTO", [](VehicleSpec& s) {
            gears(s, "TH350 3-SPEED AUTO", {2.52, 1.52, 1.00}, 2.07, 3.73, 0.4);
            s.stallRpm = 1900, s.stallRatio = 2.0, s.lockupGear = 9;  // (no lock-up: a 1970s converter)
        }),
        part(2, 2, "LOCKERS", "LOWER AXLE GEARS  LOCKING DIFFS FRONT AND REAR", [](VehicleSpec& s) {
            s.finalDrive = 4.56;
            s.lockerRear = true;
            s.lockerFront = true;
            s.frontLockerLowOnly = false;
        }),
    };
    return m;
}

Slots bajaMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("2.0 FLAT-4  115 HP"),
        part(1, 1, "QUIET ENGINE", "SMALLER, QUIETER  90 HP", [](VehicleSpec& s) {
            // A mild 1835 cc Type 1 on a single carb and a header (typical builder figures, estimate).
            s.voice.muffler = 0.6f, s.voice.rasp = 0.7f, s.voice.crackle = 0.15f, s.voice.intake = 0.45f, s.voice.exhaustHz = 205, s.voice.roar = 0.45f;
            engine(s, "1835 STREET FLAT-4",
                   {{0, 85}, {1000, 100}, {2000, 118}, {3000, 130}, {3500, 135}, {4200, 132}, {5000, 125}, {5500, 108}, {5800, 92}}, 5600, 0);
            s.displacement = 1.835;
            s.engineMass -= 5;
        }),
        part(2, 2, "BIG ENGINE", "2.3 FLAT-4  150 HP  LOUDER", [](VehicleSpec& s) {
            // A big-bore stroker Type 1 (2,332 cc; typical builder figures, estimate).
            s.voice.muffler = 0.95f, s.voice.rasp = 0.72f, s.voice.crackle = 0.45f, s.voice.lope = 0.25f, s.voice.intake = 0.8f, s.voice.exhaustHz = 190, s.voice.roar = 0.8f;
            engine(s, "2332 STROKER FLAT-4",
                   {{0, 110}, {1000, 135}, {2000, 165}, {3000, 188}, {4000, 200}, {4800, 196}, {5500, 185}, {6000, 165}, {6300, 140}}, 6200, 0);
            s.displacement = 2.33;
            s.engineMass += 6;
        }),
    };
    m[MOD_SUSPENSION] = {
        stock("LONG-TRAVEL SHOCKS  30 IN TIRES"),
        part(1, 1, "LIFT KIT", "+40 MM  LONGER TRAVEL  33 IN TIRES", [](VehicleSpec& s) {
            tires(s, 0.419, 0.267, 5, 0.94, 0.79, 0);
            springs(s, 0.04, 0.85, 0.8, 0.38);
            s.bumpFront += 0.03, s.droopFront += 0.04, s.bumpRear += 0.04, s.droopRear += 0.05;
        }),
        part(2, 1, "LOWERED", "-80 MM  ROAD TIRES", [](VehicleSpec& s) {
            tires(s, 0.317, 0.195, -7, 1.0, 0.86, 0);
            s.tireType = TIRE_STREET;
            springs(s, -0.08, 1.2, 1.3, 0.34);
            s.bumpFront = 0.07, s.droopFront = 0.08, s.bumpRear = 0.08, s.droopRear = 0.10;
        }),
    };
    m[MOD_TRANSMISSION] = {
        stock("4-SPEED  OPEN DIFF"),
        part(1, 1, "LIMITED SLIP", "BOTH REAR TIRES PULL", [](VehicleSpec& s) {
            s.diffRear = DIFF_LSD;
            s.lsdBias = 2.5;
        }),
        part(2, 2, "SAND GEARS", "LOWER GEARS: PULLS THROUGH SAND, LOWER TOP SPEED", [](VehicleSpec& s) {
            gears(s, "TYPE 1 SAND BOX", {3.09, 2.06, 1.26, 0.93}, 3.88, 4.86, 0.32);
        }),
    };
    return m;
}

const std::vector<Slots>& allMods() {
    static const std::vector<Slots> mods = [] {
        std::vector<Slots> m;
        for (const VehicleSpec& vs : vehicleCatalog()) {
            std::string id = vs.id;
            m.push_back(id == "civic" ? civicMods() : id == "jesko" ? jeskoMods() : id == "bronco" ? broncoMods() : id == "buggy" ? buggyMods() : id == "utv" ? utvMods() : id == "tacoma" ? tacomaMods() : id == "blazer" ? blazerMods() : id == "baja" ? bajaMods() : runnerMods());
        }
        return m;
    }();
    return mods;
}

// ---- Wheels and tires -------------------------------------------------------------
// Wheels: three sizes, each another vehicle's wheels and tires, put on whole (the car keeps its
// tread and grip). Tires: four treads, on the wheels it has.

void takeWheels(VehicleSpec& s, const VehicleSpec& from) {
    const double grip = s.tireGrip, lat = s.lateralGrip;
    // Tires taller than its wheel wells take a lift to clear them, as on a real build.
    const double wellR = s.wellTireRadius > 0 ? s.wellTireRadius : s.wheelRadius + 0.03;
    if (from.wheelRadius > wellR) raiseBody(s, from.wheelRadius - wellR);
    tires(s, from.wheelRadius, from.wheelWidth, from.wheelMass - s.wheelMass, grip, lat, 0);
    s.wheelWidthRear = from.wheelWidthRear;
    s.rimRadius = from.rimRadius;
    s.wheelStyle = from.wheelStyle;
    s.tirePsi = from.tirePsi;
    s.tireSoft = from.tireSoft;
}

struct WheelSize {
    const char* from;  // whose wheels
    const char* name;
    int tier;
};
const WheelSize kWheelSizes[] = {
    {"jesko", "STREET WHEELS", 1},  // 20 in rims, low tires
    {"4runner", "BIG WHEELS", 1},   // 35 in tires
    {"buggy", "HUGE WHEELS", 2},    // 40 in tires
};

struct Tread {
    int type;
    const char *name, *what;
    int tier;
};
const Tread kTreads[] = {
    {TIRE_STREET, "STREET TIRES", "BEST ON PAVEMENT, FAIR ELSEWHERE", 1},
    {TIRE_ALL_TERRAIN, "ALL-TERRAIN TIRES", "GOOD ON TRAILS, GRAVEL AND SNOW", 1},
    {TIRE_MUD_TERRAIN, "MUD TIRES", "BIG LUGS: BITE IN MUD AND LOOSE GROUND", 2},
    {TIRE_RACE_SLICK, "RACE TIRES", "HUGE GRIP ON DRY PAVEMENT, HOPELESS OFF IT", 3},
};
// The tread a factory tire counts as (a semi-slick is the Jesko's street tire).
int treadClass(int type) { return type == TIRE_SEMI_SLICK ? TIRE_STREET : type; }

// Each tread's own peak grip (dry pavement, then the cornering share of it), against which a
// change of tire scales the vehicle's calibrated figures.
const double kTypeGrip[TIRE_TYPES][2] = {
    {1.00, 0.86},  // street
    {1.10, 0.95},  // semi-slick
    {0.98, 0.77},  // all-terrain
    {0.92, 0.72},  // mud-terrain
    {1.32, 1.02},  // race slick
    {0.94, 0.80},  // snow
    {0.80, 0.70},  // (chains: fitted on foot, not here)
};

std::string shortName(const VehicleSpec& v) {
    std::string n = v.id;
    for (char& ch : n) ch = (char)std::toupper((unsigned char)ch);
    return n;
}

int vehicleById(const std::string& id) {
    const auto& cat = vehicleCatalog();
    for (int v = 0; v < (int)cat.size(); v++)
        if (id == cat[v].id) return v;
    return -1;
}

struct Universal {
    std::vector<std::vector<std::vector<ModOption>>> opts;  // [vehicle][slot]
    std::vector<std::vector<int>> own;
};

const Universal& universal() {
    static const Universal u = [] {
        Universal U;
        const auto& cat = vehicleCatalog();
        const auto& base = allMods();
        const int n = (int)cat.size();
        U.opts.assign(n, std::vector<std::vector<ModOption>>(MOD_SLOTS));
        U.own.assign(n, std::vector<int>(MOD_SLOTS, 1));
        for (int v = 0; v < n; v++) {
            for (int k = 0; k < MOD_SLOTS; k++) {
                auto& list = U.opts[v][k];
                if (k < (int)base[v].size()) list = base[v][k];
                if (list.empty()) {
                    char b[96];
                    if (k == MOD_WHEELS) std::snprintf(b, sizeof b, "%.0f MM TIRES  %.0f IN RIMS", cat[v].wheelRadius * 2000, cat[v].rimRadius * 2 / 0.0254);
                    else std::snprintf(b, sizeof b, "%s", k == MOD_TIRES ? tireTypeName(cat[v].tireType) : "");
                    list.push_back(stock(b));
                }
                U.own[v][k] = (int)list.size();
                for (ModOption& o : list) o.fromVehicle = v;  // (fromOption: its id, set in the catalog)
            }
            for (const WheelSize& ws : kWheelSizes) {
                const int w = vehicleById(ws.from);
                if (w < 0 || w == v) continue;
                const VehicleSpec* from = &cat[w];
                char b[96];
                std::snprintf(b, sizeof b, "%.0f MM TALL  %.0f MM WIDE  %.0f IN RIMS", from->wheelRadius * 2000, from->wheelWidth * 1000, from->rimRadius * 2 / 0.0254);
                ModOption o{ws.name, b, [from](VehicleSpec& s) { takeWheels(s, *from); }, w, 0};
                o.tier = ws.tier;
                U.opts[v][MOD_WHEELS].push_back(o);
            }
            for (const Tread& t : kTreads) {
                const int type = t.type;
                if (type == treadClass(cat[v].tireType)) continue;
                ModOption o{t.name, t.what, [type](VehicleSpec& s) {
                                const int was = std::min(std::max(s.tireType, 0), TIRE_TYPES - 1);
                                s.tireGrip *= kTypeGrip[type][0] / kTypeGrip[was][0];
                                s.lateralGrip *= kTypeGrip[type][1] / kTypeGrip[was][1];
                                s.tireType = type;
                            }, -1, type};
                o.tier = t.tier;
                U.opts[v][MOD_TIRES].push_back(o);
            }
        }
        return U;
    }();
    return u;
}

// ---- Saves from before the parts were cut -----------------------------------------
// Until parts list 2 every vehicle offered every other vehicle's parts too, so a slot had 31 or 32
// options (wheels 9, tires 6), in this order: its own parts, then each other vehicle's (catalog order;
// its stock part, then its own mods), and the treads in kOldTreads' order less the factory one.

// How many parts of its own each vehicle had (catalog order), engine, suspension, transmission.
const int kOldOwn[][3] = {{4, 4, 4}, {3, 4, 4}, {4, 4, 4}, {4, 4, 4}, {3, 3, 3}, {3, 3, 3}, {3, 4, 4}, {4, 3, 3}, {3, 3, 3}};
const int kOldTreads[] = {TIRE_STREET, TIRE_SEMI_SLICK, TIRE_RACE_SLICK, TIRE_ALL_TERRAIN, TIRE_MUD_TERRAIN, TIRE_SNOW};

// The parts that were cut, each to the nearest one left (an upgrade to an upgrade: what was bought
// isn't lost). Their ids are not reused.
struct Gone {
    const char* vehicle;
    int slot, id, now;
};
const Gone kGone[] = {
    {"civic", MOD_ENGINE, 1, 2},          // D16 CAM AND HEADER -> SPORT ENGINE
    {"civic", MOD_SUSPENSION, 1, 2},      // LOWERING SPRINGS -> LOWERED
    {"civic", MOD_TRANSMISSION, 1, 2},    // SHORT FINAL DRIVE -> SPORT GEARBOX
    {"jesko", MOD_TRANSMISSION, 3, 1},    // TRACK DIFF MAP -> SHORT GEARS
    {"bronco", MOD_ENGINE, 1, 0},         // 2.3 ECOBOOST I4 (less power) -> STOCK
    {"bronco", MOD_TRANSMISSION, 1, 2},   // 4.70 GEARS -> LOCKERS
    {"4runner", MOD_ENGINE, 1, 0},        // FACTORY 3.4 V6 (less power) -> AS BUILT
    {"4runner", MOD_SUSPENSION, 1, 3},    // FACTORY HEIGHT -> LOWERED
    {"4runner", MOD_TRANSMISSION, 1, 2},  // 4.88 REGEAR -> LOCKERS
    {"tacoma", MOD_TRANSMISSION, 1, 2},   // 4.88 REGEAR -> LOCKERS
    {"blazer", MOD_ENGINE, 1, 2},         // 350 4-BARREL -> BIG V8
};

int ownNow(int vehicle, int slot, int id) {
    for (const Gone& g : kGone)
        if (g.slot == slot && g.id == id && vehicleById(g.vehicle) == vehicle) return g.now;
    return id;
}

int treadNow(int type) { return type == TIRE_SEMI_SLICK ? TIRE_STREET : type == TIRE_SNOW ? TIRE_ALL_TERRAIN : type; }

int optionOf(int vehicle, int slot, int fromVehicle, int fromOption) {
    const auto& opts = modOptions(vehicle, slot);
    for (int o = 0; o < (int)opts.size(); o++)
        if (opts[o].fromVehicle == fromVehicle && opts[o].fromOption == fromOption) return o;
    return -1;
}

}  // namespace

const std::vector<ModOption>& modOptions(int vehicle, int slot) { return universal().opts[vehicle][slot]; }
int modOwnCount(int vehicle, int slot) { return universal().own[vehicle][slot]; }

std::string modPartKey(int vehicle, int slot, int option) {
    if (vehicle < 0 || vehicle >= (int)vehicleCatalog().size() || slot < 0 || slot >= MOD_SLOTS || option < 0 || option >= (int)modOptions(vehicle, slot).size()) return "";
    const ModOption& o = modOptions(vehicle, slot)[option];
    if (o.fromVehicle < 0) return std::string("TIRES:") + tireTypeName(o.fromOption);
    return std::string(modSlotName(slot)) + ":" + vehicleCatalog()[o.fromVehicle].id + ":" + std::to_string(o.fromOption);
}

bool modIsFactory(int vehicle, int slot, int option) {
    if (modPartKey(vehicle, slot, option).empty()) return false;
    const ModOption& o = modOptions(vehicle, slot)[option];
    return o.fromVehicle == vehicle && o.fromOption == 0;
}

std::string modPartTitle(const std::string& key) {
    const auto& cat = vehicleCatalog();
    for (int v = 0; v < (int)cat.size(); v++)
        for (int k = 0; k < MOD_SLOTS; k++)
            for (int o = 0; o < (int)modOptions(v, k).size(); o++) {
                if (modPartKey(v, k, o) != key) continue;
                const ModOption& mo = modOptions(v, k)[o];
                if (mo.fromVehicle != v) return mo.name;  // (a tread, or a wheel size: the same on every vehicle)
                if (o == 0) return shortName(cat[v]) + ": STOCK " + modSlotName(k);
                return shortName(cat[v]) + ": " + mo.name;
            }
    return "";
}

std::string modFindPart(const char* vehicleId, int slot, const char* name) {
    const int v = vehicleById(vehicleId);
    if (v < 0 || slot < 0 || slot >= MOD_SLOTS) return "";
    const auto& opts = modOptions(v, slot);
    auto findable = [&](int o) { return o < modOwnCount(v, slot) || opts[o].fromVehicle < 0; };
    for (int o = 0; o < (int)opts.size(); o++)  // (the exact name first: "LIFT KIT" before "BIG LIFT"...)
        if (findable(o) && (opts[o].name == name || (opts[o].fromVehicle < 0 && std::string(tireTypeName(opts[o].fromOption)) == name))) return modPartKey(v, slot, o);
    for (int o = 0; o < (int)opts.size(); o++)
        if (findable(o) && opts[o].name.find(name) != std::string::npos) return modPartKey(v, slot, o);
    return "";
}

std::string modLegacyKey(int vehicle, int slot, int oldPick) {
    const auto& cat = vehicleCatalog();
    const int n = std::min((int)cat.size(), (int)(sizeof kOldOwn / sizeof kOldOwn[0]));  // (the vehicles there were then)
    if (vehicle < 0 || vehicle >= n || slot < 0 || slot >= MOD_SLOTS || oldPick < 0) return "";
    const std::string slotName = modSlotName(slot);
    auto key = [&](int w, int o) { return slotName + ":" + cat[w].id + ":" + std::to_string(o); };
    if (oldPick == 0) return key(vehicle, 0);
    if (slot == MOD_TIRES) {
        int i = 0;
        for (int t : kOldTreads)
            if (t != cat[vehicle].tireType && ++i == oldPick) return std::string("TIRES:") + tireTypeName(t);
        return "";
    }
    auto own = [&](int w) { return slot == MOD_WHEELS ? 1 : kOldOwn[w][slot]; };
    if (oldPick < own(vehicle)) return key(vehicle, oldPick);
    int i = own(vehicle);
    for (int w = 0; w < n; w++) {
        if (w == vehicle) continue;
        if (oldPick < i + own(w)) return key(w, oldPick - i);
        i += own(w);
    }
    return "";
}

std::string modMigrateKey(const std::string& key) {
    if (key.rfind("TIRES:", 0) == 0) {
        for (int t = 0; t < TIRE_TYPES; t++)
            if (key == std::string("TIRES:") + tireTypeName(t)) return std::string("TIRES:") + tireTypeName(treadNow(t));
        // (else a vehicle's factory tires: "TIRES:blazer:0")
    }
    const size_t a = key.find(':'), b = key.rfind(':');
    if (a == std::string::npos || b == a) return key;
    int slot = -1;
    for (int k = 0; k < MOD_SLOTS; k++)
        if (key.compare(0, a, modSlotName(k)) == 0 && a == std::strlen(modSlotName(k))) slot = k;
    const int v = vehicleById(key.substr(a + 1, b - a - 1));
    if (slot < 0 || v < 0) return key;
    return key.substr(0, b + 1) + std::to_string(ownNow(v, slot, std::atoi(key.c_str() + b + 1)));
}

int modLegacyPick(int vehicle, int slot, int oldPick) {
    const std::string key = modMigrateKey(modLegacyKey(vehicle, slot, oldPick));
    if (key.empty()) return 0;
    const auto& cat = vehicleCatalog();
    const auto& opts = modOptions(vehicle, slot);
    for (int o = 0; o < (int)opts.size(); o++)
        if (modPartKey(vehicle, slot, o) == key) return o;
    if (slot == MOD_TIRES) return 0;  // (the factory tread, or one it counts as)
    // Another vehicle's part, as the garage had them on everything.
    const size_t a = key.find(':'), b = key.rfind(':');
    const int w = vehicleById(key.substr(a + 1, b - a - 1)), id = std::atoi(key.c_str() + b + 1);
    if (w < 0) return 0;
    int best = 0;
    if (slot == MOD_WHEELS) {  // the wheel size nearest its wheels'
        const double r = cat[w].wheelRadius;
        auto radius = [&](int o) { return opts[o].fromVehicle >= 0 ? cat[opts[o].fromVehicle].wheelRadius : cat[vehicle].wheelRadius; };
        for (int o = 1; o < (int)opts.size(); o++)
            if (std::fabs(radius(o) - r) < std::fabs(radius(best) - r)) best = o;
        return best;
    }
    if (slot == MOD_ENGINE) {  // its own engine nearest that one's power
        ModSet ms;
        ms.pick[MOD_ENGINE] = std::max(0, optionOf(w, MOD_ENGINE, w, id));
        const double hp = peakPower(tunedSpec(w, ms));
        double bestOff = 1e30;
        for (int o = 0; o < (int)opts.size(); o++) {
            ModSet mine;
            mine.pick[MOD_ENGINE] = o;
            const double off = std::fabs(peakPower(tunedSpec(vehicle, mine)) - hp);
            if (off < bestOff) bestOff = off, best = o;
        }
        return best;
    }
    return 0;  // (another vehicle's gearbox or suspension tuning: the car's own factory one)
}

ModSet modLegacyMods(int vehicle, const ModSet& old) {
    ModSet m;
    for (int k = 0; k < MOD_SLOTS; k++) m.pick[k] = modLegacyPick(vehicle, k, old.pick[k]);
    return m;
}

namespace {

double travelOf(const VehicleSpec& v, bool front, bool bump) {
    const double t = front ? (bump ? v.bumpFront : v.droopFront) : (bump ? v.bumpRear : v.droopRear);
    return t > 0 ? t : (bump ? 0.085 : 0.11) * v.wheelRadius / 0.32;  // (carbuild's size-scaled defaults)
}

}  // namespace

// Every part on, the body is made to take them, as a builder would: tires taller or wider than
// the factory's wheel wells, or a kit that swings them further up into the body, get the arches
// trimmed and flared round them (the tire-on-body contact and the drawn arches follow
// wellTire* and the kit's travel), and wheel spacers keep the inner sidewall where the factory
// tire's was, clear of the arms, springs and frame (the track widens; flares cover the tread).
void fitBody(VehicleSpec& s, const VehicleSpec& stock) {
    // No lower than the body can go: a floor ~9.5 cm up at most (the Jesko's splitter clears a
    // kerb at 7), or the factory's if that's lower. A kit or smaller wheels that would take it
    // further down have the spring perches set where it stops.
    const double minFloor = std::min(stock.layerY[0], 0.095);
    if (s.layerY[0] < minFloor) raiseBody(s, minFloor - s.layerY[0]);
    // How far the body sits lower over its wheels than the factory's: the wells are cut that
    // much deeper (the tire meets them sooner, at lock and over a bump).
    const double lowered = std::max(0.0, (stock.layerY[0] - stock.wheelRadius) - (s.layerY[0] - s.wheelRadius));
    const double wellR = std::max(stock.wheelRadius, stock.wellTireRadius);
    const double wF = s.wheelWidth, wR = s.wheelWidthRear > 0 ? s.wheelWidthRear : s.wheelWidth;
    const double sF = stock.wheelWidth, sR = stock.wheelWidthRear > 0 ? stock.wheelWidthRear : stock.wheelWidth;
    const double wellW = std::max({sF, sR, stock.wellTireWidth});
    bool longer = false;
    for (bool front : {true, false})
        for (bool bump : {true, false})
            longer |= travelOf(s, front, bump) > travelOf(stock, front, bump) + 0.005;
    // (With the room round them the factory left round its own tire.)
    const double needR = s.wheelRadius + (wellR - stock.wheelRadius) + lowered;
    const double needW = std::max(wF, wR) + (wellW - std::max(sF, sR));
    if (needR > wellR + 0.002 || needW > wellW + 0.002 || longer) {
        s.wellTireRadius = std::max({wellR, needR, s.wellTireRadius});
        s.wellTireWidth = std::max({wellW, needW, s.wellTireWidth});
        s.wellsTrimmed = true;
    }
    // Long travel on wishbones (or struts) takes long arms, or the wheel swings round a tight arc,
    // tucking in and leaning over at the ends of its travel and into the wheelhouse: the arms grow
    // to keep each end of the travel to the swing a long-travel side-by-side's arms see (droop about
    // half the arm, bump 0.4), as far as the space between the wheels allows; past that the kit's
    // bump stops and droop straps are set where the arms run out.
    {
        const double minTrack = std::min(s.trackFront > 0 ? s.trackFront : s.width - 0.2, s.trackRear > 0 ? s.trackRear : s.width - 0.2);
        const double cap = std::max(s.armLower, minTrack / 2 - 0.03 - 0.12);
        double need = s.armLower;
        for (bool front : {true, false})
            if ((front ? s.frontSusp : s.rearSusp) == WISHBONE || (front ? s.frontSusp : s.rearSusp) == STRUT)
                need = std::max({need, travelOf(s, front, false) / 0.52, travelOf(s, front, true) / 0.40});
        if (need > s.armLower + 0.005) {
            const double arm = std::min(need, cap);
            s.armUpper *= arm / s.armLower;
            s.armLower = arm;
            for (bool front : {true, false}) {
                if ((front ? s.frontSusp : s.rearSusp) != WISHBONE && (front ? s.frontSusp : s.rearSusp) != STRUT) continue;
                double& bump = front ? s.bumpFront : s.bumpRear;
                double& droop = front ? s.droopFront : s.droopRear;
                bump = std::min(travelOf(s, front, true), 0.40 * arm);
                droop = std::min(travelOf(s, front, false), 0.52 * arm);
            }
        }
    }
    // Spacers: the inner sidewall no further in than the factory tire's.
    auto spacer = [](double& track, double w, double stockTrack, double stockW) {
        if (track <= 0 || stockTrack <= 0) return;
        const double in = track / 2 - w / 2, stockIn = stockTrack / 2 - stockW / 2;
        if (in < stockIn - 0.002) track += 2 * (stockIn - in);
    };
    spacer(s.trackFront, wF, stock.trackFront, sF);
    spacer(s.trackRear, wR, stock.trackRear, sR);
}

const VehicleSpec& tunedSpec(int vehicle, const ModSet& mods) {
    if (mods.stock()) return vehicleCatalog()[vehicle];
    static std::mutex lock;
    static std::map<std::array<int, MOD_SLOTS + 1>, std::unique_ptr<VehicleSpec>> cache;
    std::lock_guard<std::mutex> g(lock);
    std::array<int, MOD_SLOTS + 1> key{};
    key[0] = vehicle;
    for (int k = 0; k < MOD_SLOTS; k++) key[k + 1] = mods.pick[k];
    auto& slot = cache[key];
    if (!slot) {
        slot = std::make_unique<VehicleSpec>(vehicleCatalog()[vehicle]);
        VehicleSpec& s = *slot;
        for (int k = 0; k < MOD_SLOTS; k++) {
            const auto& opts = modOptions(vehicle, k);
            int p = std::min(std::max(mods.pick[k], 0), (int)opts.size() - 1);
            s.mods[k] = p;
            if (p == 0) continue;
            opts[p].apply(s);
            s.modSummary += (s.modSummary.empty() ? "" : " / ") + opts[p].name;
        }
        fitBody(s, vehicleCatalog()[vehicle]);
    }
    return *slot;
}

Garage::Garage() : fitted(vehicleCatalog().size()), paint(vehicleCatalog().size()) {}

std::string Garage::defaultPath() {
    return configDir() + "/garage.txt";
}

bool Garage::load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char line[256];
    int parts = 1;  // (a file without a "parts N" line is from before the parts were cut: its picks are the old lists')
    while (std::fgets(line, sizeof line, f)) {  // "id engine suspension transmission [wheels tires]"
        char id[64];
        if (std::sscanf(line, "parts %d", &parts) == 1) continue;
        int p[MOD_SLOTS] = {0, 0, 0, 0, 0};
        {  // "paint <id> <body hex|-> <accent hex|-> <finish>": a line older builds skip
            char pid[64], body[16], accent[16];
            int finish = 0;
            if (std::sscanf(line, "paint %63s %15s %15s %d", pid, body, accent, &finish) >= 3) {
                const auto& cat = vehicleCatalog();
                for (size_t v = 0; v < cat.size(); v++)
                    if (std::string(cat[v].id) == pid) paintFromText(body, accent, finish, paint[v]);
                continue;
            }
        }
        if (std::sscanf(line, "%63s %d %d %d %d %d", id, &p[0], &p[1], &p[2], &p[3], &p[4]) < 4) continue;
        const auto& cat = vehicleCatalog();
        for (size_t v = 0; v < cat.size(); v++) {
            if (std::string(cat[v].id) != id) continue;
            ModSet m;
            for (int k = 0; k < MOD_SLOTS; k++) m.pick[k] = p[k];
            if (parts < kPartsVersion) m = modLegacyMods((int)v, m);
            for (int k = 0; k < MOD_SLOTS; k++)
                fitted[v].pick[k] = std::min(std::max(m.pick[k], 0), (int)modOptions((int)v, k).size() - 1);
        }
    }
    std::fclose(f);
    return true;
}

bool Garage::save(const std::string& path) const {
    // Make the folder (one level is enough for ~/.config/crumplezone).
    std::string dir = path.substr(0, path.find_last_of('/'));
    if (!dir.empty()) makeDir(dir);
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    const auto& cat = vehicleCatalog();
    std::fprintf(f, "parts %d\n", kPartsVersion);
    for (size_t v = 0; v < cat.size(); v++)
        std::fprintf(f, "%s %d %d %d %d %d\n", cat[v].id, fitted[v].pick[0], fitted[v].pick[1], fitted[v].pick[2], fitted[v].pick[3], fitted[v].pick[4]);
    for (size_t v = 0; v < cat.size(); v++)
        if (!paint[v].factory()) std::fprintf(f, "paint %s %s\n", cat[v].id, paintToText(paint[v]).c_str());
    std::fclose(f);
    return true;
}
