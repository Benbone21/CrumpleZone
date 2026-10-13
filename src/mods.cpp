#include "mods.h"

#include "platform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>

const char* modSlotName(int slot) {
    static const char* names[MOD_SLOTS] = {"ENGINE", "SUSPENSION", "TRANSMISSION"};
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
             engine(s, "1.6L D16A6 CAM AND HEADER",
                    {{0, 65}, {1000, 89}, {2500, 122}, {4000, 141}, {5000, 145}, {6500, 137}, {7000, 119}}, 7000, 0);
         }},
        {"B16A VTEC SWAP", "1.6L DOHC  160 HP  150 NM  +20 KG", [](VehicleSpec& s) {
             engine(s, "1.6L DOHC B16A VTEC",
                    {{0, 65}, {1000, 86}, {3000, 120}, {5000, 137}, {6000, 145}, {7000, 150}, {7600, 150}, {8200, 130}}, 8200, 0);
             s.engineMass += 20;
             s.engineLen = 0.54;
         }},
        {"B18C TYPE R SWAP", "1.8L DOHC  195 HP  176 NM  +25 KG", [](VehicleSpec& s) {
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
             engine(s, "5.0L TWIN-TURBO V8 E85",
                    {{0, 500}, {1500, 700}, {2700, 1050}, {4000, 1350}, {5100, 1500}, {6500, 1500}, {7800, 1460}, {8500, 1300}}, 8500, 0.25);
         }},
        {"RACE FUEL AND BOOST", "MORE BOOST ON RACE FUEL  ABOUT 1750 HP  1600 NM", [](VehicleSpec& s) {
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
             engine(s, "2.3L TURBO I4",
                    {{0, 200}, {1000, 280}, {2000, 400}, {3400, 441}, {4500, 420}, {5700, 375}, {6250, 330}}, 6250, 0.5);
             s.cylinders = 4;
             s.displacement = 2.3;
             s.engineMass -= 50;
             s.engineWid = 0.48;
             s.engineLen = 0.60;
         }},
        {"2.7 STAGE 1 TUNE", "93 OCTANE TUNE  390 HP  690 NM", [](VehicleSpec& s) {
             engine(s, "2.7L TWIN-TURBO V6 STAGE 1",
                    {{0, 260}, {1000, 360}, {2000, 600}, {3000, 690}, {4500, 610}, {5250, 529}, {5800, 470}}, 5800, 0.45);
         }},
        {"RAPTOR 3.0 V6 SWAP", "3.0L TWIN-TURBO V6  418 HP  597 NM  +15 KG", [](VehicleSpec& s) {
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
             engine(s, "3.4L V6 5VZ-FE",
                    {{0, 190}, {1000, 235}, {2000, 270}, {3000, 288}, {3600, 294}, {4200, 290}, {4800, 272}, {5400, 235}}, 5400, 0);
             s.induction = VehicleSpec::NATURAL;
             s.engineMass -= 18;
         }},
        {"4.0 V6 1GR SWAP", "LATER 4RUNNER ENGINE  270 HP  377 NM", [](VehicleSpec& s) {
             engine(s, "4.0L V6 1GR-FE",
                    {{0, 220}, {1000, 280}, {2000, 345}, {3000, 362}, {4400, 377}, {5000, 368}, {5600, 343}, {6100, 310}}, 6100, 0);
             s.displacement = 4.0;
             s.induction = VehicleSpec::NATURAL;
             s.engineMass -= 33;
         }},
        {"1GR SUPERCHARGED", "4.0 V6 WITH A TRD BLOWER  340 HP  470 NM", [](VehicleSpec& s) {
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
             engine(s, "7.0L LS7 V8",
                    {{0, 340}, {1000, 410}, {2000, 520}, {3000, 590}, {4000, 630}, {4800, 650}, {5600, 620}, {6300, 574}, {6800, 520}}, 6800, 0);
             s.displacement = 7.0;
             s.engineMass += 8;
         }},
        {"BLOWN 6.2", "ROOTS-BLOWN LS3  ABOUT 650 HP  820 NM  +30 KG", [](VehicleSpec& s) {
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

Slots utvMods() {
    Slots m(MOD_SLOTS);
    m[MOD_ENGINE] = {
        stock("900CC TURBO TRIPLE  195 HP  205 NM"),
        {"STAGE 2 TUNE", "BIGGER TURBO AND INTERCOOLER  ABOUT 230 HP  240 NM", [](VehicleSpec& s) {
             // Estimated bolt-on figures for the Rotax 900 turbo.
             engine(s, "900CC TURBO I3 STAGE 2",
                    {{0, 95}, {1500, 120}, {3000, 190}, {4500, 232}, {5500, 242}, {6500, 238}, {7500, 220}, {8000, 195}, {8400, 150}}, 8300, 0.5);
             s.engineMass += 4;
         }},
        {"ECO TUNE", "SOFTER MAP FOR TRAIL RIDES  ABOUT 150 HP  170 NM", [](VehicleSpec& s) {
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

const std::vector<Slots>& allMods() {
    static const std::vector<Slots> mods = [] {
        std::vector<Slots> m;
        for (const VehicleSpec& vs : vehicleCatalog()) {
            std::string id = vs.id;
            m.push_back(id == "civic" ? civicMods() : id == "jesko" ? jeskoMods() : id == "bronco" ? broncoMods() : id == "buggy" ? buggyMods() : id == "utv" ? utvMods() : runnerMods());
        }
        return m;
    }();
    return mods;
}

}  // namespace

const std::vector<ModOption>& modOptions(int vehicle, int slot) { return allMods()[vehicle][slot]; }

const VehicleSpec& tunedSpec(int vehicle, const ModSet& mods) {
    if (mods.stock()) return vehicleCatalog()[vehicle];
    static std::mutex lock;
    static std::map<std::array<int, 4>, std::unique_ptr<VehicleSpec>> cache;
    std::lock_guard<std::mutex> g(lock);
    auto& slot = cache[{vehicle, mods.pick[0], mods.pick[1], mods.pick[2]}];
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
    char id[64];
    int e, s, t;
    while (std::fscanf(f, "%63s %d %d %d", id, &e, &s, &t) == 4) {
        const auto& cat = vehicleCatalog();
        for (size_t v = 0; v < cat.size(); v++) {
            if (std::string(cat[v].id) != id) continue;
            int pick[3] = {e, s, t};
            for (int k = 0; k < MOD_SLOTS; k++)
                fitted[v].pick[k] = std::min(std::max(pick[k], 0), (int)modOptions((int)v, k).size() - 1);
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
        std::fprintf(f, "%s %d %d %d\n", cat[v].id, fitted[v].pick[0], fitted[v].pick[1], fitted[v].pick[2]);
    std::fclose(f);
    return true;
}
