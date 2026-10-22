#include "mods.h"

#include "platform.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

ModOption stock(const char* detail) { return {"STOCK", detail, [](VehicleSpec&) {}}; }

// ---- The catalog ----------------------------------------------------------------

using Slots = std::vector<std::vector<ModOption>>;

Slots civicMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("1.6L D16A6  108 HP  136 NM"),
        {"D16 CAM AND HEADER", "MILD SOHC BUILD  ABOUT 125 HP  145 NM", [](VehicleSpec& s) {
             // Estimated street cam/header curve, not a factory engine rating.
             s.voice.muffler = 0.5f, s.voice.intake = 0.65f, s.voice.exhaustHz = 230, s.voice.roar = 0.45f;
             engine(s, "1.6L D16A6 CAM AND HEADER",
                    {{0, 65}, {1000, 89}, {2500, 122}, {4000, 141}, {5000, 145}, {6500, 137}, {7000, 119}}, 7000, 0);
         }},
        {"B16A VTEC SWAP", "1.6L DOHC  160 HP  150 NM  +20 KG", [](VehicleSpec& s) {
             s.voice.intake = 0.9f, s.voice.rasp = 0.75f, s.voice.muffler = 0.45f, s.voice.exhaustHz = 320, s.voice.roar = 0.35f;
             engine(s, "1.6L DOHC B16A VTEC",
                    {{0, 65}, {1000, 86}, {3000, 120}, {5000, 137}, {6000, 145}, {7000, 150}, {7600, 150}, {8200, 130}}, 8200, 0);
             s.engineMass += 20;
             s.engineLen = 0.54;
         }},
        {"B18C TYPE R SWAP", "1.8L DOHC  195 HP  176 NM  +25 KG", [](VehicleSpec& s) {
             s.voice.intake = 0.95f, s.voice.rasp = 0.8f, s.voice.muffler = 0.55f, s.voice.exhaustHz = 290, s.voice.roar = 0.5f;
             engine(s, "1.8L DOHC B18C TYPE R",
                    {{0, 85}, {1000, 105}, {3000, 146}, {5000, 160}, {6200, 176}, {7300, 175}, {8000, 174}, {8400, 155}}, 8400, 0);
             s.displacement = 1.8;
             s.engineMass += 25;
             s.engineLen = 0.55;
         }},
    };
    m[MOD_SUSPENSION] = {
        stock("DOUBLE WISHBONES  185/60R14"),
        {"LOWERING SPRINGS", "-25 MM  SPRINGS +25%", [](VehicleSpec& s) { springs(s, -0.025, 1.25, 1.0, 0.44); }},
        {"COILOVERS", "-35 MM  SPRINGS +65%  BARS +25%", [](VehicleSpec& s) {
             springs(s, -0.035, 1.65, 1.25, 0.52);
             s.lateralGrip += 0.03;
         }},
        {"RALLY KIT", "+30 MM  SOFTER SPRINGS  GRAVEL TIRES", [](VehicleSpec& s) {
             springs(s, 0.03, 0.8, 0.7, 0.45);
             tires(s, 0.302, 0.185, 2, 0.96, 0.84, 0);
             s.tireType = TIRE_ALL_TERRAIN;  // gravel rally tires: knobbly, for loose stuff
         }},
    };
    m[MOD_TRANSMISSION] = {
        stock("5-SPEED MANUAL  4.25 FINAL  OPEN DIFF"),
        {"SHORT FINAL DRIVE", "4.70 FINAL DRIVE", [](VehicleSpec& s) { s.finalDrive = 4.70; }},
        {"CLOSE RATIO AND LSD", "5-SPEED  4.40 FINAL  HELICAL LSD", [](VehicleSpec& s) {
             gears(s, "5-SPEED CLOSE RATIO", {3.230, 2.105, 1.458, 1.107, 0.848}, 3.153, 4.4, 0.25);
             s.diffFront = DIFF_LSD;
         }},
        {"SEQUENTIAL RACE BOX", "5-SPEED DOG BOX  70 MS SHIFTS  LSD", [](VehicleSpec& s) {
             gears(s, "5-SPEED SEQUENTIAL", {3.0, 2.0, 1.50, 1.15, 0.90}, 3.153, 4.6, 0.07);
             s.diffFront = DIFF_LSD;
             s.lsdBias = 4.0;
         }},
    };
    return m;
}

Slots jeskoMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("5.0L TWIN-TURBO V8 ON PETROL  1280 HP"),
        {"E85 FLEX FUEL", "FACTORY E85 MAP  1600 HP  1500 NM", [](VehicleSpec& s) {
             s.voice.crackle = 0.45f, s.voice.exhaustHz = 300, s.voice.roar = 0.7f;
             engine(s, "5.0L TWIN-TURBO V8 E85",
                    {{0, 500}, {1500, 700}, {2700, 1050}, {4000, 1350}, {5100, 1500}, {6500, 1500}, {7800, 1460}, {8500, 1300}}, 8500, 0.25);
         }},
        {"RACE FUEL AND BOOST", "MORE BOOST ON RACE FUEL  ABOUT 1750 HP  1600 NM", [](VehicleSpec& s) {
             s.voice.crackle = 0.6f, s.voice.muffler = 0.85f, s.voice.exhaustHz = 390, s.voice.roar = 0.9f, s.voice.rasp = 0.85f;
             engine(s, "5.0L TWIN-TURBO V8 RACE",
                    {{0, 520}, {1500, 720}, {2700, 1100}, {4000, 1450}, {5100, 1600}, {6500, 1600}, {7800, 1600}, {8500, 1420}}, 8500, 0.28);
         }},
    };
    m[MOD_SUSPENSION] = {
        stock("TRIPLEX DAMPERS  ROAD SETTING"),
        {"TRACK SETUP", "-20 MM  SPRINGS +50%  BARS +40%", [](VehicleSpec& s) { springs(s, -0.02, 1.5, 1.4, 0.55); }},
        {"COMFORT AND LIFT", "+40 MM NOSE AND TAIL  SOFTER", [](VehicleSpec& s) { springs(s, 0.04, 0.75, 0.8, 0.45); }},
        {"ABSOLUT LOW-DRAG BODY", "NO WING: CD 0.278, ALMOST NO DOWNFORCE", [](VehicleSpec& s) {
             s.dragArea = 0.53;
             s.downforce = 0.1;
             s.downforceMax = 1470;
             s.extras &= ~(unsigned)EXTRA_WING;
         }},
    };
    m[MOD_TRANSMISSION] = {
        stock("9-SPEED LST  3.3 FINAL  E-DIFF"),
        {"SHORT FINAL DRIVE", "3.6 FINAL DRIVE: HARDER PULL", [](VehicleSpec& s) { s.finalDrive = 3.6; }},
        {"TOP SPEED GEARING", "3.0 FINAL DRIVE: LONGER LEGS", [](VehicleSpec& s) { s.finalDrive = 3.0; }},
        {"TRACK DIFF MAP", "E-DIFF LOCKS HARDER  BIAS 5:1", [](VehicleSpec& s) { s.lsdBias = 5.0; }},
    };
    return m;
}

Slots broncoMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("2.7L TWIN-TURBO V6  330 HP  563 NM"),
        {"2.3 ECOBOOST I4", "THE BASE ENGINE  300 HP  441 NM  -50 KG", [](VehicleSpec& s) {
             s.voice.rasp = 0.55f, s.voice.muffler = 0.32f, s.voice.exhaustHz = 220;
             engine(s, "2.3L TURBO I4",
                    {{0, 200}, {1000, 280}, {2000, 400}, {3400, 441}, {4500, 420}, {5700, 375}, {6250, 330}}, 6250, 0.5);
             s.cylinders = 4;
             s.displacement = 2.3;
             s.engineMass -= 50;
             s.engineWid = 0.48;
             s.engineLen = 0.60;
         }},
        {"2.7 STAGE 1 TUNE", "93 OCTANE TUNE  390 HP  690 NM", [](VehicleSpec& s) {
             s.voice.muffler = 0.45f, s.voice.crackle = 0.15f, s.voice.exhaustHz = 135, s.voice.roar = 0.45f;
             engine(s, "2.7L TWIN-TURBO V6 STAGE 1",
                    {{0, 260}, {1000, 360}, {2000, 600}, {3000, 690}, {4500, 610}, {5250, 529}, {5800, 470}}, 5800, 0.45);
         }},
        {"RAPTOR 3.0 V6 SWAP", "3.0L TWIN-TURBO V6  418 HP  597 NM  +15 KG", [](VehicleSpec& s) {
             s.voice.muffler = 0.6f, s.voice.crackle = 0.25f, s.voice.rasp = 0.45f, s.voice.exhaustHz = 125, s.voice.roar = 0.6f;
             engine(s, "3.0L TWIN-TURBO V6 RAPTOR",
                    {{0, 280}, {1000, 380}, {2000, 560}, {3250, 597}, {4500, 580}, {5600, 532}, {6000, 490}}, 6000, 0.45);
             s.displacement = 3.0;
             s.engineMass += 15;
         }},
    };
    m[MOD_SUSPENSION] = {
        stock("IFS AND 5-LINK REAR  33 IN TIRES"),
        {"SASQUATCH PACKAGE", "35 IN TIRES  +25 MM  BILSTEIN DAMPERS  WIDER TRACK", [](VehicleSpec& s) {
             tires(s, 0.445, 0.315, 8, 0.96, 0.74, 1.699);
             springs(s, 0.025, 0.95, 1.0, 0.38);
         }},
        {"3 IN LIFT AND 37S", "37 IN MUD TIRES  +75 MM  SOFTER  LONG TRAVEL", [](VehicleSpec& s) {
             tires(s, 0.47, 0.32, 16, 0.95, 0.72, 1.74);
             s.tireType = TIRE_MUD_TERRAIN;
             springs(s, 0.075, 0.85, 0.8, 0.36);
             s.bumpFront = 0.11, s.droopFront = 0.16, s.bumpRear = 0.13, s.droopRear = 0.20;
         }},
        {"STREET LOWERED", "-35 MM  ROAD TIRES  STIFFER", [](VehicleSpec& s) {
             tires(s, 0.40, 0.265, -6, 1.05, 0.90, 0);
             s.tireType = TIRE_STREET;
             springs(s, -0.035, 1.3, 1.5, 0.45);
             s.bumpFront = 0.065, s.bumpRear = 0.08;
         }},
    };
    m[MOD_TRANSMISSION] = {
        stock("10-SPEED AUTO  4.27 AXLES  OPEN DIFFS"),
        {"4.70 GEARS", "SHORTER AXLE GEARS FOR BIG TIRES", [](VehicleSpec& s) { s.finalDrive = 4.70; }},
        {"4.70 AND LOCKERS", "4.70 AXLES  FRONT AND REAR E-LOCKERS (SASQUATCH)", [](VehicleSpec& s) {
             s.finalDrive = 4.70;
             s.lockerFront = s.lockerRear = true;
         }},
        {"7-SPEED MANUAL", "CRAWLER GEAR 6.59  4.70 AXLES", [](VehicleSpec& s) {
             gears(s, "7-SPEED MANUAL", {6.588, 4.283, 2.365, 1.453, 1.000, 0.776, 0.646}, 5.625, 4.70, 0.35);
         }},
    };
    return m;
}

Slots runnerMods() {
    // The 4Runner is modelled as one particular built truck, so option 0 is the truck as
    // it was sold; the other options put parts back to factory or take it further.
    auto asBuilt = [](const char* detail) { return ModOption{"AS BUILT", detail, [](VehicleSpec&) {}}; };
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        asBuilt("MAGNUSON SUPERCHARGED 3.4L V6  256 HP  366 NM"),
        {"FACTORY 3.4 V6", "SUPERCHARGER OFF  183 HP AT 4800  294 NM", [](VehicleSpec& s) {
             s.voice.rasp = 0.2f, s.voice.muffler = 0.25f, s.voice.intake = 0.25f, s.voice.mech = 0.5f, s.voice.exhaustHz = 150;  // (an older 24-valve V6: deeper, clattery)
             engine(s, "3.4L V6 5VZ-FE",
                    {{0, 190}, {1000, 235}, {2000, 270}, {3000, 288}, {3600, 294}, {4200, 290}, {4800, 272}, {5400, 235}}, 5400, 0);
             s.induction = VehicleSpec::NATURAL;
             s.engineMass -= 18;
         }},
        {"4.0 V6 1GR SWAP", "LATER 4RUNNER ENGINE  270 HP  377 NM", [](VehicleSpec& s) {
             s.voice.rasp = 0.55f, s.voice.muffler = 0.42f, s.voice.intake = 0.6f, s.voice.mech = 0.15f, s.voice.exhaustHz = 190, s.voice.roar = 0.4f;  // (crisper, an intake roar)
             engine(s, "4.0L V6 1GR-FE",
                    {{0, 220}, {1000, 280}, {2000, 345}, {3000, 362}, {4400, 377}, {5000, 368}, {5600, 343}, {6100, 310}}, 6100, 0);
             s.displacement = 4.0;
             s.induction = VehicleSpec::NATURAL;
             s.engineMass -= 33;
         }},
        {"1GR SUPERCHARGED", "4.0 V6 WITH A TRD BLOWER  340 HP  470 NM", [](VehicleSpec& s) {
             s.voice.rasp = 0.4f, s.voice.muffler = 0.5f, s.voice.exhaustHz = 205, s.voice.roar = 0.45f;
             engine(s, "4.0L V6 1GR-FE SUPERCHARGED",
                    {{0, 290}, {1000, 370}, {2000, 440}, {3000, 465}, {4000, 470}, {5000, 455}, {5600, 425}, {6100, 380}}, 6100, 0);
             s.displacement = 4.0;
             s.engineMass -= 15;
         }},
    };
    m[MOD_SUSPENSION] = {
        asBuilt("OME COILS  BILSTEINS  2 IN BODY LIFT  35X12.50R17 KO2"),
        {"FACTORY HEIGHT", "STOCK SPRINGS  NO BODY LIFT  265/70R16 TIRES", [](VehicleSpec& s) {
             tires(s, 0.389, 0.265, -15, 0.95, 0.76, 1.506);
             springs(s, -0.065 - 0.051, 0.83, 1.0, 0.36);
         }},
        {"LONG TRAVEL AND 37S", "+50 MM  LONG-TRAVEL ARMS  37X12.50R17 MUD TIRES", [](VehicleSpec& s) {
             tires(s, 0.47, 0.318, 8, 0.93, 0.70, 1.66);
             s.tireType = TIRE_MUD_TERRAIN;
             springs(s, 0.05, 0.85, 0.75, 0.38);
             s.bumpFront = 0.11, s.droopFront = 0.16, s.bumpRear = 0.13, s.droopRear = 0.18;
         }},
        {"STREET SPORT", "FACTORY HEIGHT -25 MM  275/60R17 ROAD TIRES  STIFFER", [](VehicleSpec& s) {
             tires(s, 0.381, 0.275, -10, 1.02, 0.88, 1.53);
             s.tireType = TIRE_STREET;
             springs(s, -0.065 - 0.051 - 0.025, 1.15, 1.5, 0.45);
         }},
    };
    m[MOD_TRANSMISSION] = {
        asBuilt("A340F 4-SPEED AUTO  4.10 AXLES  OPEN DIFFS"),
        {"4.88 REGEAR", "THE RIGHT AXLE GEARS FOR 35S", [](VehicleSpec& s) { s.finalDrive = 4.88; }},
        {"4.88 AND LOCKERS", "4.88 AXLES  ARB AIR LOCKERS FRONT AND REAR", [](VehicleSpec& s) {
             s.finalDrive = 4.88;
             s.lockerFront = s.lockerRear = true;
             s.rearLockerLowOnly = false;  // ARBs lock on air, in any range
         }},
        {"CRAWL BOX", "4.88  ARB LOCKERS  4.7:1 LOW RANGE", [](VehicleSpec& s) {
             s.finalDrive = 4.88;
             s.lockerFront = s.lockerRear = true;
             s.rearLockerLowOnly = false;
             s.lowRange = 4.7;
             s.gearboxName = "A340F + 4.7 LOW RANGE";
         }},
    };
    return m;
}

Slots buggyMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("6.2L LS V8  450 HP  565 NM"),
        {"7.0 LS7", "7.0L DRY-SUMP V8  ABOUT 505 HP  650 NM  +8 KG", [](VehicleSpec& s) {
             // Estimated: LS7 crate figures (505 hp, 470 lb-ft) with a truck-friendly curve.
             s.voice.lope = 0.45f, s.voice.rasp = 0.25f, s.voice.exhaustHz = 85, s.voice.rumble = 0.75f, s.voice.roar = 0.95f;
             engine(s, "7.0L LS7 V8",
                    {{0, 340}, {1000, 410}, {2000, 520}, {3000, 590}, {4000, 630}, {4800, 650}, {5600, 620}, {6300, 574}, {6800, 520}}, 6800, 0);
             s.displacement = 7.0;
             s.engineMass += 8;
         }},
        {"BLOWN 6.2", "ROOTS-BLOWN LS3  ABOUT 650 HP  820 NM  +30 KG", [](VehicleSpec& s) {
             s.voice.lope = 0.25f, s.voice.intake = 0.4f, s.voice.exhaustHz = 112, s.voice.rumble = 0.5f, s.voice.roar = 0.8f;
             engine(s, "6.2L LS V8 SUPERCHARGED",
                    {{0, 420}, {1000, 520}, {2000, 690}, {3000, 780}, {4000, 820}, {5000, 810}, {5800, 740}, {6400, 640}}, 6400, 0);
             s.induction = VehicleSpec::SUPERCHARGER;
             s.engineMass += 30;
         }},
    };
    m[MOD_SUSPENSION] = {
        stock("4-LINK SOLID AXLES  16 IN TRAVEL  40 IN TIRES"),
        {"42 IN TIRES", "42X14.5 TIRES  +25 MM  LONGER SHOCKS  WIDER TRACK", [](VehicleSpec& s) {
             tires(s, 0.533, 0.368, 9, 0.95, 0.69, 1.92);
             s.bumpFront += 0.02, s.droopFront += 0.02, s.bumpRear += 0.02, s.droopRear += 0.02;
             s.coilTop += 0.03;
         }},
        {"DESERT SETUP", "FIRMER SPRINGS  RACE SHOCKS  36 IN TIRES", [](VehicleSpec& s) {
             tires(s, 0.457, 0.30, -14, 0.98, 0.76, 1.84);
             springs(s, 0.0, 1.5, 1.0, 0.55);
             s.arbFront = s.arbRear = 6000;
         }},
    };
    m[MOD_TRANSMISSION] = {
        stock("4L80E + ATLAS 4.3 LOW  5.38 AXLES  OPEN DIFFS"),
        {"TH400 3-SPEED", "THE CLASSIC BUGGY BOX: THREE GEARS, FEWER SHIFTS", [](VehicleSpec& s) {
             gears(s, "TH400 3-SPEED + ATLAS 4.3 LOW", {2.48, 1.48, 1.00}, 2.07, 5.38, 0.35);
         }},
        {"ATLAS 5.0 LOW", "DEEPER LOW RANGE: ABOUT 20% LOWER CRAWL RATIO", [](VehicleSpec& s) {
             s.lowRange = 5.0;
             s.gearboxName = "4L80E + ATLAS 5.0 LOW";
         }},
    };
    return m;
}

Slots tacomaMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("3.5L V6 2GR-FKS  278 HP AT 6000  359 NM"),
        {"INTAKE AND TUNE", "COLD AIR INTAKE  91 OCTANE TUNE  ~295 HP  372 NM", [](VehicleSpec& s) {
             s.voice.muffler = 0.45f, s.voice.intake = 0.55f, s.voice.exhaustHz = 190, s.voice.roar = 0.3f;
             engine(s, "3.5L V6 2GR-FKS TUNED",
                    {{0, 205}, {1000, 270}, {1600, 300}, {2000, 314}, {3000, 347}, {4000, 367}, {4600, 372}, {5000, 368}, {5500, 356}, {6000, 344}, {6400, 320}}, 6400, 0);
         }},
        {"MAGNUSON SUPERCHARGER", "TVS1900 AT 6 PSI  ~350 HP  450 NM", [](VehicleSpec& s) {
             s.voice.muffler = 0.5f, s.voice.exhaustHz = 160, s.voice.roar = 0.35f;
             engine(s, "3.5L V6 2GR-FKS SUPERCHARGED",
                    {{0, 260}, {1000, 330}, {1600, 380}, {2000, 405}, {3000, 435}, {4000, 450}, {4600, 448}, {5000, 440}, {5500, 425}, {6000, 405}, {6400, 375}}, 6400, 0);
             s.induction = VehicleSpec::SUPERCHARGER;
             s.engineMass += 14;
         }},
    };
    m[MOD_SUSPENSION] = {
        stock("TRD BILSTEINS  LEAF PACK  265/70R16 ALL-TERRAINS"),
        {"3 IN LIFT AND 33S", "COILOVERS  ADD-A-LEAF  +75 MM  285/75R16 ALL-TERRAINS", [](VehicleSpec& s) {
             tires(s, 0.42, 0.285, 6, 0.95, 0.76, 1.63);
             springs(s, 0.075, 1.05, 0.9, 0.40);
             s.bumpFront = 0.09, s.droopFront = 0.13, s.droopRear = 0.16;
         }},
        {"LONG TRAVEL AND 35S", "+50 MM  LONG-TRAVEL ARMS  NEW LEAF PACK  35X12.50R17  FENDERS TRIMMED", [](VehicleSpec& s) {
             tires(s, 0.445, 0.318, 14, 0.93, 0.72, 1.70);
             s.tireType = TIRE_MUD_TERRAIN;
             springs(s, 0.05, 0.9, 0.7, 0.38);
             s.bumpFront = 0.12, s.droopFront = 0.17, s.bumpRear = 0.13, s.droopRear = 0.19;
             s.wellTireRadius = 0.46, s.wellTireWidth = 0.32;  // (cut for them)
         }},
        {"STREET LOWERED", "-25 MM  265/65R17 ROAD TIRES  STIFFER", [](VehicleSpec& s) {
             tires(s, 0.385, 0.265, -4, 1.02, 0.88, 1.60);
             s.tireType = TIRE_STREET;
             springs(s, -0.025, 1.25, 1.4, 0.45);
             s.bumpFront = 0.065, s.bumpRear = 0.08;
         }},
    };
    m[MOD_TRANSMISSION] = {
        stock("6-SPEED AUTO  3.909 AXLES  REAR E-LOCKER IN 4L"),
        {"4.88 REGEAR", "SHORTER AXLE GEARS FOR BIGGER TIRES", [](VehicleSpec& s) { s.finalDrive = 4.88; }},
        {"4.88 AND FRONT LOCKER", "4.88 AXLES  ARB AIR LOCKER IN FRONT  E-LOCKER BEHIND", [](VehicleSpec& s) {
             s.finalDrive = 4.88;
             s.lockerFront = true;
         }},
        {"CRAWL BOX", "4.88  FRONT LOCKER  4.3:1 LOW RANGE", [](VehicleSpec& s) {
             s.finalDrive = 4.88;
             s.lockerFront = true;
             s.lowRange = 4.3;
             s.gearboxName = "6-SPEED AUTO + 4.3 LOW RANGE";
         }},
    };
    return m;
}

Slots utvMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("900CC TURBO TRIPLE  195 HP  205 NM"),
        {"STAGE 2 TUNE", "BIGGER TURBO AND INTERCOOLER  ABOUT 230 HP  240 NM", [](VehicleSpec& s) {
             // Estimated bolt-on figures for the Rotax 900 turbo.
             s.voice.muffler = 0.8f, s.voice.crackle = 0.5f, s.voice.exhaustHz = 335, s.voice.roar = 0.75f;
             engine(s, "900CC TURBO I3 STAGE 2",
                    {{0, 95}, {1500, 120}, {3000, 190}, {4500, 232}, {5500, 242}, {6500, 238}, {7500, 220}, {8000, 195}, {8400, 150}}, 8300, 0.5);
             s.engineMass += 4;
         }},
        {"ECO TUNE", "SOFTER MAP FOR TRAIL RIDES  ABOUT 150 HP  170 NM", [](VehicleSpec& s) {
             s.voice.muffler = 0.45f, s.voice.crackle = 0.0f, s.voice.exhaustHz = 270, s.voice.roar = 0.3f;
             engine(s, "900CC TURBO I3 ECO",
                    {{0, 85}, {1500, 100}, {3000, 140}, {4500, 165}, {5500, 172}, {6500, 168}, {7500, 150}, {8000, 130}, {8400, 100}}, 8000, 0.3);
         }},
    };
    m[MOD_SUSPENSION] = {
        stock("LONG-TRAVEL A-ARMS  16 IN TRAVEL  32 IN TIRES"),
        {"RACE SHOCKS", "FIRMER SPRINGS  BIGGER BARS  FIRMER DAMPING", [](VehicleSpec& s) { springs(s, 0.0, 1.35, 1.4, 0.5); }},
        {"SOFT TRAIL SETUP", "+25 MM  SOFTER  LONGER DROOP", [](VehicleSpec& s) {
             springs(s, 0.025, 0.8, 0.7, 0.36);
             s.droopFront += 0.03, s.droopRear += 0.03;
         }},
    };
    m[MOD_TRANSMISSION] = {
        stock("CVT  4WD  FRONT DIFF LOCK"),
        {"SHORT GEARING", "10.2 FINAL: HARDER PULL, LOWER TOP SPEED", [](VehicleSpec& s) { s.finalDrive = 10.2; }},
        {"REAR LOCKER", "FRONT AND REAR DIFF LOCKS", [](VehicleSpec& s) { s.lockerRear = true; }},
    };
    return m;
}

Slots blazerMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("350 V8 2-BARREL  165 HP  280 LB-FT"),
        {"350 4-BARREL", "THE QUADRAJET 350  175 HP  290 LB-FT", [](VehicleSpec& s) {
             // 1972 SAE net, L48 four-barrel (GM).
             s.voice.intake = 0.75f, s.voice.muffler = 0.42f, s.voice.exhaustHz = 98, s.voice.rumble = 0.6f, s.voice.roar = 0.7f;
             engine(s, "350 V8 4-BARREL",
                    {{0, 255}, {800, 310}, {1200, 345}, {1800, 380}, {2400, 393}, {3000, 385}, {3600, 365}, {4000, 345}, {4800, 280}, {5200, 230}}, 5200, 0);
         }},
        {"454 BIG BLOCK", "THE PICKUPS' 7.4 L  ABOUT 240 HP  370 LB-FT  +70 KG", [](VehicleSpec& s) {
             // 1972 LS5 truck rating ~230-240 hp net, 360-375 lb-ft (estimate from GM's 1972 figures).
             s.voice.rasp = 0.04f, s.voice.muffler = 0.65f, s.voice.mech = 0.45f, s.voice.lope = 0.1f, s.voice.exhaustHz = 75, s.voice.rumble = 0.95f, s.voice.roar = 0.95f;  // (a big, slow, deep roar and a chest-thumping rumble)
             engine(s, "454 BIG BLOCK V8",
                    {{0, 330}, {800, 410}, {1200, 450}, {1800, 490}, {2400, 505}, {2800, 500}, {3600, 470}, {4000, 440}, {4400, 395}, {4800, 340}}, 4800, 0);
             s.displacement = 7.4;
             s.engineMass += 70;
         }},
        {"ZZ4 CRATE 350", "ALUMINUM HEADS, ROLLER CAM  355 HP  405 LB-FT  LOPEY IDLE", [](VehicleSpec& s) {
             s.voice.lope = 0.65f, s.voice.muffler = 0.7f, s.voice.crackle = 0.3f, s.voice.intake = 0.7f, s.voice.exhaustHz = 115, s.voice.rumble = 0.75f, s.voice.roar = 0.8f;
             engine(s, "ZZ4 350 CRATE V8",
                    {{0, 300}, {1000, 400}, {2000, 470}, {3000, 535}, {3500, 549}, {4500, 520}, {5250, 480}, {5800, 420}, {6000, 390}}, 6000, 0);
             s.engineMass -= 15;
             s.idleRpm = 800;
         }},
    };
    m[MOD_SUSPENSION] = {
        stock("LEAF SPRINGS BOTH ENDS  10-15 TIRES"),
        {"4 IN LIFT AND 35S", "+100 MM ON TALLER LEAVES  35X12.50 MUD TIRES", [](VehicleSpec& s) {
             springs(s, 0.10, 0.9, 0.8, 0.34);
             tires(s, 0.445, 0.318, 14, 0.92, 0.72, 0);
             s.tireType = TIRE_MUD_TERRAIN;
             s.bumpFront += 0.02, s.droopFront += 0.04, s.bumpRear += 0.02, s.droopRear += 0.05;
         }},
        {"SOFT TRAIL LEAVES", "+50 MM  SOFTER PACKS  MORE FLEX", [](VehicleSpec& s) {
             springs(s, 0.05, 0.7, 0.5, 0.30);
             s.droopFront += 0.03, s.droopRear += 0.04;
         }},
    };
    m[MOD_TRANSMISSION] = {
        stock("SM465 4-SPEED  NP205  3.73 AXLES  OPEN DIFFS"),
        {"TH350 AUTOMATIC", "THREE-SPEED AUTO WITH A TORQUE CONVERTER", [](VehicleSpec& s) {
             gears(s, "TH350 3-SPEED AUTO", {2.52, 1.52, 1.00}, 2.07, 3.73, 0.4);
             s.stallRpm = 1900, s.stallRatio = 2.0, s.lockupGear = 9;  // (no lock-up: a 1970s converter)
         }},
        {"4.56 GEARS AND LOCKERS", "4.56 AXLES  DETROIT REAR  ARB FRONT", [](VehicleSpec& s) {
             s.finalDrive = 4.56;
             s.lockerRear = true;
             s.lockerFront = true;
             s.frontLockerLowOnly = false;
         }},
    };
    return m;
}

Slots bajaMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("2.0L TYPE 1 DUAL CARBS  115 HP  160 NM"),
        {"1835 STREET MOTOR", "92 MM BORE  SINGLE 34 CARB  ~90 HP  ~135 NM  QUIETER", [](VehicleSpec& s) {
             // A mild 1835 cc Type 1 on a single carb and a header (typical builder figures, estimate).
             s.voice.muffler = 0.6f, s.voice.rasp = 0.7f, s.voice.crackle = 0.15f, s.voice.intake = 0.45f, s.voice.exhaustHz = 205, s.voice.roar = 0.45f;
             engine(s, "1835 STREET FLAT-4",
                    {{0, 85}, {1000, 100}, {2000, 118}, {3000, 130}, {3500, 135}, {4200, 132}, {5000, 125}, {5500, 108}, {5800, 92}}, 5600, 0);
             s.displacement = 1.835;
             s.engineMass -= 5;
         }},
        {"2332 STROKER", "94 MM BORE  84 STROKE  DUAL 48S  ~150 HP  ~200 NM  LOUDER", [](VehicleSpec& s) {
             // A big-bore stroker Type 1 (2,332 cc; typical builder figures, estimate).
             s.voice.muffler = 0.95f, s.voice.rasp = 0.72f, s.voice.crackle = 0.45f, s.voice.lope = 0.25f, s.voice.intake = 0.8f, s.voice.exhaustHz = 190, s.voice.roar = 0.8f;
             engine(s, "2332 STROKER FLAT-4",
                    {{0, 110}, {1000, 135}, {2000, 165}, {3000, 188}, {4000, 200}, {4800, 196}, {5500, 185}, {6000, 165}, {6300, 140}}, 6200, 0);
             s.displacement = 2.33;
             s.engineMass += 6;
         }},
    };
    m[MOD_SUSPENSION] = {
        stock("LIFTED BEAM AND IRS  LONG-TRAVEL SHOCKS  30X9.50R15"),
        {"DESERT RACE AND 33S", "+40 MM  LONGER TRAILING ARMS  SOFTER  33X10.50R15 ALL-TERRAINS", [](VehicleSpec& s) {
             tires(s, 0.419, 0.267, 5, 0.94, 0.79, 0);
             springs(s, 0.04, 0.85, 0.8, 0.38);
             s.bumpFront += 0.03, s.droopFront += 0.04, s.bumpRear += 0.04, s.droopRear += 0.05;
         }},
        {"STREET BUG", "-80 MM  STOCK TORSION BARS  195/65R15 ROAD TIRES", [](VehicleSpec& s) {
             tires(s, 0.317, 0.195, -7, 1.0, 0.86, 0);
             s.tireType = TIRE_STREET;
             springs(s, -0.08, 1.2, 1.3, 0.34);
             s.bumpFront = 0.07, s.droopFront = 0.08, s.bumpRear = 0.08, s.droopRear = 0.10;
         }},
    };
    m[MOD_TRANSMISSION] = {
        stock("TYPE 1 4-SPEED  4.375 FINAL  OPEN DIFF"),
        {"WEDDLE LSD", "A LIMITED-SLIP IN THE TRANSAXLE: BOTH REAR TIRES PULL", [](VehicleSpec& s) {
             s.diffRear = DIFF_LSD;
             s.lsdBias = 2.5;
         }},
        {"SAND BOX", "4.86 FINAL  TALLER 3.09 FIRST: PULLS THROUGH SAND WITHOUT DIGGING IN, LOWER TOP SPEED", [](VehicleSpec& s) {
             gears(s, "TYPE 1 SAND BOX", {3.09, 2.06, 1.26, 0.93}, 3.88, 4.86, 0.32);
         }},
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

// ---- Everyone's parts on everything ---------------------------------------------
// Another vehicle's part goes on whole: its stock engine (or gearbox, or suspension, or
// wheels) first, then that vehicle's own mod on top of it, as on its own car. (A mod alone
// only touches what it changes: the Civic's VTEC swap on the Bronco would otherwise keep the
// Bronco's six cylinders and twin turbos.)

void takeEngine(VehicleSpec& s, const VehicleSpec& from) {
    s.torque = from.torque;  // (already the catalog's, ~15% over the rating)
    s.idleRpm = from.idleRpm;
    s.redline = from.redline;
    s.turboLag = from.turboLag;
    s.induction = from.induction;
    s.displacement = from.displacement;
    s.cylinders = from.cylinders;
    s.flatPlane = from.flatPlane;
    s.airCooled = from.airCooled;
    s.engineName = from.engineName;
    s.engineMass = from.engineMass;
    s.engineLen = from.engineLen, s.engineWid = from.engineWid, s.engineHgt = from.engineHgt;
    s.voice = from.voice;
    s.engineOrigin = from.model;
}

void takeGearbox(VehicleSpec& s, const VehicleSpec& from) {
    s.gears = from.gears;
    s.reverseRatio = from.reverseRatio;
    s.finalDrive = from.finalDrive;
    s.shiftTime = from.shiftTime;
    s.stallRpm = from.stallRpm, s.stallRatio = from.stallRatio, s.lockupGear = from.lockupGear;
    s.gearboxName = from.gearboxName;
    s.gearboxMass = from.gearboxMass;
}

// The suspension's tuning: springs, dampers, bars and travel. (The layout stays the car's:
// wishbones on a car's subframe, leaves on a truck's frame. Bolting a truck's solid axles
// under a unibody that has nothing to hang them from tore the cars apart.)
void takeSuspension(VehicleSpec& s, const VehicleSpec& from) {
    s.rideFreqFront = from.rideFreqFront, s.rideFreqRear = from.rideFreqRear;
    s.dampingRatio = from.dampingRatio;
    s.arbFront = from.arbFront, s.arbRear = from.arbRear;
    s.bumpFront = from.bumpFront, s.droopFront = from.droopFront, s.bumpRear = from.bumpRear, s.droopRear = from.droopRear;
    s.swayDisconnect = from.swayDisconnect;
}

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

std::string powerLine(const VehicleSpec& v) {
    char b[96];
    std::snprintf(b, sizeof b, "%.0f HP  %.0f NM  %d CYL", peakPower(v) / 745.7, peakTorque(v), v.cylinders);
    return b;
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
                    list.push_back(stock(""));
                    list.back().detail = b;
                }
                U.own[v][k] = (int)list.size();
                for (int o = 0; o < (int)list.size(); o++) list[o].fromVehicle = v, list[o].fromOption = o;
            }
            // Every other vehicle's parts.
            for (int w = 0; w < n; w++) {
                if (w == v) continue;
                const VehicleSpec* from = &cat[w];
                const std::string who = vehicleListed(w) ? shortName(cat[w]) + ": " : std::string("SWAP: ");  // (the Civic, UTV and buggy aren't offered: their parts are just swaps)
                U.opts[v][MOD_ENGINE].push_back({who + cat[w].engineName, "STOCK  " + powerLine(cat[w]), [from](VehicleSpec& s) { takeEngine(s, *from); }, w, 0});
                for (size_t o = 1; o < base[w][MOD_ENGINE].size(); o++) {
                    const ModOption& mo = base[w][MOD_ENGINE][o];
                    auto fn = mo.apply;
                    U.opts[v][MOD_ENGINE].push_back({who + mo.name, mo.detail, [from, fn](VehicleSpec& s) {
                                                         takeEngine(s, *from);
                                                         fn(s);
                                                     }, w, (int)o});
                }
                U.opts[v][MOD_TRANSMISSION].push_back({who + cat[w].gearboxName, "STOCK GEARBOX AND AXLE RATIO", [from](VehicleSpec& s) { takeGearbox(s, *from); }, w, 0});
                for (size_t o = 1; o < base[w][MOD_TRANSMISSION].size(); o++) {
                    const ModOption& mo = base[w][MOD_TRANSMISSION][o];
                    auto fn = mo.apply;
                    U.opts[v][MOD_TRANSMISSION].push_back({who + mo.name, mo.detail, [from, fn](VehicleSpec& s) {
                                                               takeGearbox(s, *from);
                                                               fn(s);
                                                           }, w, (int)o});
                }
                U.opts[v][MOD_SUSPENSION].push_back({who + "STOCK SUSPENSION", base[w][MOD_SUSPENSION][0].detail, [from](VehicleSpec& s) { takeSuspension(s, *from); }, w, 0});
                for (size_t o = 1; o < base[w][MOD_SUSPENSION].size(); o++) {
                    const ModOption& mo = base[w][MOD_SUSPENSION][o];
                    auto fn = mo.apply;
                    // (Fitted to its own vehicle first: then its tuning, its tires if it brings
                    // any, and what it does to the ride height, within reason, come across.)
                    U.opts[v][MOD_SUSPENSION].push_back({who + mo.name, mo.detail, [from, fn](VehicleSpec& s) {
                                                             VehicleSpec m = *from;
                                                             fn(m);
                                                             takeSuspension(s, m);
                                                             const double lift = (m.layerY[0] - from->layerY[0]) - (m.wheelRadius - from->wheelRadius);
                                                             raiseBody(s, std::min(std::max(lift, -0.04), 0.15));
                                                             if (std::fabs(m.wheelRadius - from->wheelRadius) > 0.005 || m.tireType != from->tireType) {
                                                                 takeWheels(s, m);
                                                                 s.tireGrip = m.tireGrip, s.lateralGrip = m.lateralGrip, s.tireType = m.tireType;
                                                             }
                                                         }, w, (int)o});
                }
                char b[96];
                std::snprintf(b, sizeof b, "%.0f MM TALL  %.0f MM WIDE  %.0f IN RIMS", from->wheelRadius * 2000, from->wheelWidth * 1000, from->rimRadius * 2 / 0.0254);
                U.opts[v][MOD_WHEELS].push_back({who + "WHEELS AND TIRES", b, [from](VehicleSpec& s) { takeWheels(s, *from); }, w, 0});
            }
            // Tires: every tread, on the wheels it has.
            static const int kOffer[] = {TIRE_STREET, TIRE_SEMI_SLICK, TIRE_RACE_SLICK, TIRE_ALL_TERRAIN, TIRE_MUD_TERRAIN, TIRE_SNOW};
            static const char* kWhat[] = {"ROAD TREAD: GOOD ON PAVEMENT, FAIR ELSEWHERE", "TRACK-DAY TREAD: STICKY, POOR IN MUD AND SNOW",
                                          "NO TREAD AT ALL: HUGE DRY GRIP, HOPELESS OFF IT", "KNOBBY ALL-ROUNDER FOR TRAILS AND GRAVEL",
                                          "BIG LUGS: CLEAR MUD, BITE LOOSE GROUND", "SOFT SIPED WINTER TREAD: SNOW AND ICE"};
            for (int t = 0; t < 6; t++) {
                const int type = kOffer[t];
                if (type == cat[v].tireType) continue;
                std::string name = std::string(tireTypeName(type)) + (type == TIRE_RACE_SLICK || type == TIRE_SEMI_SLICK ? "S" : " TIRES");
                U.opts[v][MOD_TIRES].push_back({name, kWhat[t], [type](VehicleSpec& s) {
                                                    const int was = std::min(std::max(s.tireType, 0), TIRE_TYPES - 1);
                                                    s.tireGrip *= kTypeGrip[type][0] / kTypeGrip[was][0];
                                                    s.lateralGrip *= kTypeGrip[type][1] / kTypeGrip[was][1];
                                                    s.tireType = type;
                                                }, -1, type});
            }
        }
        return U;
    }();
    return u;
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
                if (mo.fromVehicle < 0) return mo.name;
                if (mo.fromVehicle != v) return mo.name;  // (offered on another vehicle: already "WHO: NAME")
                if (o == 0) {
                    const VehicleSpec& s = cat[v];
                    return shortName(s) + ": " + (k == MOD_ENGINE ? s.engineName : k == MOD_TRANSMISSION ? s.gearboxName : k == MOD_WHEELS ? "WHEELS AND TIRES" : "STOCK SUSPENSION");
                }
                return shortName(cat[v]) + ": " + mo.name;
            }
    return "";
}

std::string modFindPart(const char* vehicleId, int slot, const char* name) {
    const auto& cat = vehicleCatalog();
    for (int v = 0; v < (int)cat.size(); v++) {
        if (std::string(cat[v].id) != vehicleId) continue;
        const auto& opts = modOptions(v, slot);
        for (int o = 0; o < (int)opts.size(); o++)
            if ((o < modOwnCount(v, slot) || opts[o].fromVehicle < 0) && opts[o].name.find(name) != std::string::npos) return modPartKey(v, slot, o);
    }
    return "";
}

void applyMods(int vehicle, const ModSet& mods, VehicleSpec& s) {
    s = vehicleCatalog()[vehicle];
    for (int k = 0; k < MOD_SLOTS; k++) {
        const auto& opts = modOptions(vehicle, k);
        int p = std::min(std::max(mods.pick[k], 0), (int)opts.size() - 1);
        s.mods[k] = p;
        if (p == 0) continue;
        opts[p].apply(s);
        s.modSummary += (s.modSummary.empty() ? "" : " / ") + opts[p].name;
    }
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
        slot = std::make_unique<VehicleSpec>();
        applyMods(vehicle, mods, *slot);
    }
    return *slot;
}

Garage::Garage() : fitted(vehicleCatalog().size()) {}

std::string Garage::defaultPath() {
    return configDir() + "/garage.txt";
}

bool Garage::load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char line[256];
    while (std::fgets(line, sizeof line, f)) {  // "id engine suspension transmission [wheels tires]"
        char id[64];
        int p[MOD_SLOTS] = {0, 0, 0, 0, 0};
        if (std::sscanf(line, "%63s %d %d %d %d %d", id, &p[0], &p[1], &p[2], &p[3], &p[4]) < 4) continue;
        const auto& cat = vehicleCatalog();
        for (size_t v = 0; v < cat.size(); v++) {
            if (std::string(cat[v].id) != id) continue;
            for (int k = 0; k < MOD_SLOTS; k++)
                fitted[v].pick[k] = std::min(std::max(p[k], 0), (int)modOptions((int)v, k).size() - 1);
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
    for (size_t v = 0; v < cat.size(); v++)
        std::fprintf(f, "%s %d %d %d %d %d\n", cat[v].id, fitted[v].pick[0], fitted[v].pick[1], fitted[v].pick[2], fitted[v].pick[3], fitted[v].pick[4]);
    std::fclose(f);
    return true;
}
