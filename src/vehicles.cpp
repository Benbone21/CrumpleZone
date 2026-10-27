#include "vehicles.h"

#include <algorithm>
#include <cmath>

std::vector<int> axleColumns(const VehicleSpec& vs) {
    std::vector<int> cols;
    const std::string& row = vs.profile[0];
    const int n = (int)row.size();
    for (int a = 0; a < n;) {
        if (row[a] != 'W') {
            a++;
            continue;
        }
        int b = a;
        while (b + 1 < n && row[b + 1] == 'W') b++;
        cols.push_back(a + (b - a + 1) / 2);
        a = b + 1;
    }
    return cols;
}

double latticeX(const VehicleSpec& vs, double i) {
    const int n = (int)vs.profile[0].size();
    std::vector<int> ax = vs.wheelbase > 0 ? axleColumns(vs) : std::vector<int>();
    if (ax.size() != 2) return vs.length / 2 - vs.length / n * i;
    // Piecewise linear: bumper -> front axle -> rear axle -> tail.
    const double xs[4] = {vs.length / 2, vs.length / 2 - vs.frontOverhang, vs.length / 2 - vs.frontOverhang - vs.wheelbase,
                          -vs.length / 2};
    const double is[4] = {0, (double)ax[0], (double)ax[1], (double)n};
    int seg = i < is[1] ? 0 : i < is[2] ? 1 : 2;
    double t = (i - is[seg]) / std::max(1e-9, is[seg + 1] - is[seg]);
    return xs[seg] + (xs[seg + 1] - xs[seg]) * t;
}

namespace {
void cabinRange(const VehicleSpec& vs, int& i0, int& i1) {
    i0 = (int)vs.profile[0].size();
    i1 = -1;
    for (const std::string& row : vs.profile)
        for (int i = 0; i < (int)row.size(); i++)
            if (row[i] == 'G') {
                i0 = std::min(i0, i);
                i1 = std::max(i1, i + 1);
            }
}
}  // namespace

int doorCells(const VehicleSpec& vs) {
    int i0, i1;
    cabinRange(vs, i0, i1);
    if (i1 < 0) return 2;
    double cell = (latticeX(vs, i0) - latticeX(vs, i1)) / (i1 - i0);
    return std::max(1, std::min(4, (int)std::lround(vs.doorLength / cell)));
}

bool hasRearDoors(const VehicleSpec& vs) {
    if (vs.doors) return vs.doors == 4;
    int i0, i1;
    cabinRange(vs, i0, i1);
    return i1 >= 0 && i1 - i0 >= 2 * doorCells(vs) + 1;
}

namespace {
double torqueAt(const VehicleSpec& vs, double r) {
    const auto& pts = vs.torque;
    if (r <= pts.front().rpm) return pts.front().nm;
    for (size_t i = 1; i < pts.size(); i++)
        if (r < pts[i].rpm) {
            double t = (r - pts[i - 1].rpm) / (pts[i].rpm - pts[i - 1].rpm);
            return pts[i - 1].nm + t * (pts[i].nm - pts[i - 1].nm);
        }
    return pts.back().nm;
}
}  // namespace

double peakPower(const VehicleSpec& vs, double* atRpm) {
    double best = 0, at = 0;
    for (double r = 1000; r <= vs.redline; r += 25) {
        double p = torqueAt(vs, r) * r * 2 * M_PI / 60;
        if (p > best) {
            best = p;
            at = r;
        }
    }
    if (atRpm) *atRpm = at;
    return best;
}

double peakTorque(const VehicleSpec& vs, double* atRpm) {
    double best = 0, at = 0;
    for (double r = 1000; r <= vs.redline; r += 25) {
        double t = torqueAt(vs, r);
        if (t > best + 1e-9) {
            best = t;
            at = r;
        }
    }
    if (atRpm) *atRpm = at;
    return best;
}

void raiseBody(VehicleSpec& vs, double dy) {
    for (double& y : vs.layerY) y += dy;
    for (double& y : vs.topLine)
        if (y > 0) y += dy;
    vs.engineY += dy;
    for (MassItem& it : vs.items) it.y += dy;
}

namespace {

const VehicleSpec* factoryOf(const VehicleSpec& vs) {
    for (const VehicleSpec& c : vehicleCatalog())
        if (vs.id && c.id && std::string(c.id) == vs.id) return &c;
    return nullptr;
}

// The smallest arch (centre cy, power p) that clears the tire's side profile, a hand's
// clearance round it, from its rest height up to full bump: what's above the sill.
double archNeed(const VehicleSpec& v, double cy, double p) {
    const double R = v.wheelRadius + 0.02;
    const double bump = (v.bumpFront > 0 ? std::max(v.bumpFront, v.bumpRear) : 0.085 * v.wheelRadius / 0.32);
    double need = 0;
    for (double u : {0.0, 0.5, 1.0})
        for (int k = 0; k < 72; k++) {
            const double a = k * M_PI / 36, x = std::fabs(R * std::cos(a)), y = v.wheelRadius + u * bump + R * std::sin(a);
            if (y < v.layerY[0]) continue;
            const double up = std::max(0.0, y - cy);
            need = std::max(need, std::pow(std::pow(x, p) + std::pow(up, p), 1 / p));
        }
    return need;
}

}  // namespace

double archGrowth(const VehicleSpec& vs, double cy, double r, double p) {
    (void)r;
    const VehicleSpec* f = factoryOf(vs);
    if (!f || f == &vs) return 0;
    const double cyStock = cy - (vs.layerY[0] - f->layerY[0]);
    return std::max(0.0, archNeed(vs, cy, p) - archNeed(*f, cyStock, p));
}

double tirePoke(const VehicleSpec& vs, bool front) {
    const VehicleSpec* f = factoryOf(vs);
    if (!f || f == &vs) return 0;
    auto outer = [&](const VehicleSpec& v) {
        const double t = front ? v.trackFront : v.trackRear, w = !front && v.wheelWidthRear > 0 ? v.wheelWidthRear : v.wheelWidth;
        return t > 0 ? t / 2 + w / 2 : v.width / 2 - 0.08;
    };
    return std::max(0.0, outer(vs) - outer(*f));
}

EngineTone voiceFor(const VehicleSpec& vs) {
    EngineTone e = vs.voice;
    if (!e.seed) {  // (from its name: two engines never sound quite alike)
        uint32_t h = 2166136261u;
        for (const char* c = vs.engineName; c && *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
        e.seed = h ? h : 1;
    }
    if (e.kind == EK_AUTO) {  // (what kind of engine: from its layout, induction and name)
        const std::string name = vs.engineName ? vs.engineName : "";
        auto has = [&](const char* s) { return name.find(s) != std::string::npos; };
        if (vs.cylinders == 4 && (has("VTEC") || has("TYPE R"))) {
            e.kind = EK_I4_VTEC;
            if (!e.vtecRpm) e.vtecRpm = has("B18C") ? 5700 : 5500;
        } else if (vs.cylinders == 4 && has("FLAT-4")) e.kind = EK_FLAT4_AIR;
        else if (vs.cylinders == 8 && !vs.flatPlane && has("LS")) e.kind = vs.induction == VehicleSpec::SUPERCHARGER ? EK_V8_LS_SUPERCHARGED : EK_V8_LS;
        else e.kind = (uint8_t)engineKindFor(vs.cylinders, (int)vs.induction, vs.flatPlane, (float)vs.displacement);
    }
    return e;
}

const char* tireTypeName(int t) {
    return t == TIRE_SEMI_SLICK ? "SEMI-SLICK" : t == TIRE_ALL_TERRAIN ? "ALL-TERRAIN" : t == TIRE_MUD_TERRAIN ? "MUD-TERRAIN"
           : t == TIRE_RACE_SLICK ? "RACE SLICK" : t == TIRE_SNOW ? "SNOW" : t == TIRE_CHAINS ? "CHAINED" : "STREET";
}

double tireSpeedRatingKmh(int t) {
    switch (t) {
        case TIRE_MUD_TERRAIN: case TIRE_SNOW: return 160;
        case TIRE_ALL_TERRAIN: return 170;
        case TIRE_SEMI_SLICK: case TIRE_RACE_SLICK: return 270;
        default: return 180;
    }
}

double VehicleSpec::tireRatingKmh() const {
    return stockTireRatingKmh > 0 && tireType == stockTireType ? stockTireRatingKmh : tireSpeedRatingKmh(tireType);
}

const std::vector<DriveMode>& driveModes(TransferCase t) {
    // Part-time (Bronco): 4A drives both axles with the center free, so it's fine on dry
    // pavement; 4H and 4L lock the shafts together for loose ground and rock.
    static const std::vector<DriveMode> partTime = {
        {"2H", false, false, false}, {"4A", true, false, false}, {"4H", true, true, false}, {"4L", true, true, true}};
    // Full-time multi-mode (3rd-gen 4Runner Limited): a center differential the driver
    // can lock; low range always locks it.
    static const std::vector<DriveMode> fullTime = {
        {"2H", false, false, false}, {"4H", true, false, false}, {"4H LOCK", true, true, false}, {"4L LOCK", true, true, true}};
    // Rock buggy (Atlas-style two-speed case): a lever with no center differential, so 4H
    // and 4L tie the shafts together like a part-time truck. It leaves in 4H: a crawler
    // wants all four driven (and the open center is only a bind in tight turns on pavement).
    static const std::vector<DriveMode> select = {
        {"2H", false, false, false}, {"4H", true, true, false}, {"4L", true, true, true}};
    // Side-by-side: a switch for 2WD or 4WD. In 4WD the front axle is driven through its
    // own differential (the front diff lock is the separate switch). There's no center
    // differential: the clutch that engages the front lets the shafts turn together, which
    // is what keeps a 1,800 lb machine's four tires pulling on loose ground.
    static const std::vector<DriveMode> onDemand = {{"2WD", false, false, false}, {"4WD", true, true, false}};
    // Plain part-time (Tacoma): no automatic mode, so it lives in 2H.
    static const std::vector<DriveMode> basic = {{"2H", false, false, false}, {"4H", true, true, false}, {"4L", true, true, true}};
    static const std::vector<DriveMode> none;
    return t == XFER_PART_TIME ? partTime : t == XFER_FULL_TIME ? fullTime : t == XFER_SELECT ? select : t == XFER_ONDEMAND ? onDemand : t == XFER_PART_TIME_BASIC ? basic : none;
}

int defaultDriveMode(TransferCase t) { return t == XFER_NONE || t == XFER_PART_TIME_BASIC ? 0 : 1; }

const std::vector<int>& listedVehicles() {
    static const std::vector<int> l = [] {
        std::vector<int> out;
        const auto& cat = vehicleCatalog();
        for (const char* id : {"blazer", "bronco", "4runner", "tacoma", "jesko"})  // (the Baja is built but hidden for now, like the Civic, UTV and buggy)
            for (int i = 0; i < (int)cat.size(); i++)
                if (std::string(cat[i].id) == id) out.push_back(i);
        return out;
    }();
    return l;
}

bool vehicleListed(int v) {
    for (int k : listedVehicles())
        if (k == v) return true;
    return false;
}

int nextListedVehicle(int v, int dir) {
    const std::vector<int>& l = listedVehicles();
    int at = -1;
    for (int i = 0; i < (int)l.size(); i++)
        if (l[i] == v) at = i;
    if (at < 0) return l[0];
    const int n = (int)l.size();
    return l[((at + dir) % n + n) % n];
}

const std::vector<VehicleSpec>& vehicleCatalog() {
    // Nine vehicles (two of them crawl specials), built from their published specs (dimensions, weights,
    // engine outputs, gear ratios, suspension layouts). Where a maker doesn't publish
    // a number (weight split of the Jesko, its final drive...), it's a careful estimate.
    // Lattice: x forward from the middle, y up from the ground; masses in kg.
    static const std::vector<VehicleSpec> catalog = [] {
        std::vector<VehicleSpec> v;

        // ---- Honda Civic Si (11th generation sedan, 2022-) ---------------------------
        // Front-drive fastback sedan, 1.5 L turbo four, 6-speed manual, helical LSD.
        VehicleSpec civic;
        civic.name = "HONDA CIVIC SI";
        civic.id = "civic";
        civic.blurb = "2022 SEDAN  1.5L TURBO I4  6-SPEED MANUAL  FWD";
        civic.profile = {
            "#WW######WW#",
            "############",
            "...GGGGGGGG.",
        };
        civic.layerY = {0.21, 0.58, 0.97, 1.41};
        civic.length = 4.674;
        civic.width = 1.801;
        civic.widthCells = 4;
        civic.wheelbase = 2.736;
        civic.frontOverhang = 0.98;
        civic.topLine = {0.66, 0.80, 0.90, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.98};  // low nose, short high deck
        civic.noseDrop = civic.tailDrop = 0;
        civic.bumperLift = 0.08;
        civic.cornerRound = 0.13;
        civic.roofTuck = 0.15;
        civic.rakeFront = 0.85;  // windshield ~62 degrees from vertical
        civic.rakeRear = 1.15;   // fastback roofline down to the short trunk
        // 1,318 kg, 60/40 (Honda): transverse engine hung out ahead of the front axle.
        civic.structureMass = 405;
        civic.panelMass = 150;
        civic.items = {
            {"radiator and intercooler", 16, 2.05, 0.45, 0}, {"battery", 15, 1.55, 0.75, -0.55},
            {"front crash beam", 9, 2.22, 0.45, 0},          {"air conditioning", 22, 1.70, 0.55, 0.30},
            {"front subframe and rack", 34, 1.30, 0.32, 0},  {"dashboard and heater", 38, 0.70, 0.85, 0},
            {"driver seat", 20, -0.05, 0.42, -0.38},         {"passenger seat", 20, -0.05, 0.42, 0.38},
            {"rear seat", 20, -0.95, 0.42, 0},               {"carpets, trim and wiring", 60, 0.0, 0.45, 0},
            {"fuel tank (47 L, full)", 44, -0.85, 0.32, 0},  {"exhaust", 18, -0.6, 0.20, 0.25},
            {"rear subframe and links", 32, -1.38, 0.30, 0}, {"rear crash beam", 7, -2.25, 0.42, 0},
            {"brakes and steering", 18, 1.3, 0.5, 0},        {"airbags and electronics", 16, 0.3, 0.7, 0},
            {"bumper, lamps and grille", 22, 2.15, 0.55, 0}, {"front rails and strut towers", 30, 1.55, 0.6, 0},
        };
        civic.engineMass = 108;
        civic.gearboxMass = 42;
        civic.transverse = true;
        civic.engineX = 1.50;
        civic.engineY = 0.56;
        civic.engineZ = 0.10;
        civic.engineLen = 0.50;
        civic.engineWid = 0.42;
        civic.engineHgt = 0.55;
        civic.gearboxLen = 0.30;
        civic.bodyStiffness = 1.15;
        civic.wheelRadius = 0.323;  // 235/40R18
        civic.wheelWidth = 0.235;
        civic.trackFront = 1.537;
        civic.trackRear = 1.567;
        civic.wheelMass = 20;
        civic.hubMass = 15;
        civic.frontSusp = STRUT;
        civic.rearSusp = WISHBONE;  // multi-link: works like short-long arms
        civic.rideFreqFront = 1.6;
        civic.rideFreqRear = 1.8;
        civic.dampingRatio = 0.42;
        civic.arbFront = 15000;  // 27 mm tubular bar
        civic.arbRear = 9000;    // 18 mm solid bar
        civic.tireGrip = 1.12;   // summer tires
        civic.lateralGrip = 0.90;
        civic.drive = VehicleSpec::FWD;
        civic.diffFront = DIFF_LSD;  // helical limited slip
        civic.lsdBias = 2.2;
        civic.brakeFront = 2300;  // 312 mm vented
        civic.brakeRear = 1100;   // 282 mm solid
        civic.dragArea = 0.62;    // Cd ~0.30 x 2.1 m^2
        // 200 hp at 6,000 rpm, 260 N*m flat from 1,800 to 5,000.
        civic.torque = {{0, 140}, {1000, 175}, {1800, 260}, {5000, 260}, {5500, 252}, {6000, 237}, {6600, 205}};
        civic.idleRpm = 750;
        civic.redline = 6600;
        civic.turboLag = 0.5;
        civic.gears = {3.643, 2.080, 1.361, 1.024, 0.830, 0.686};
        civic.reverseRatio = 3.673;
        civic.finalDrive = 4.35;
        civic.shiftTime = 0.30;
        civic.engineName = "1.5L TURBO I4";
        civic.gearboxName = "6-SPEED MANUAL";
        civic.lampStyle = LAMP_SLIM;
        civic.grilleStyle = GRILLE_WIDE;
        civic.extras = EXTRA_LIP_SPOILER;
        v.push_back(civic);

        // ---- Koenigsegg Jesko Attack -------------------------------------------------------
        // Mid-engine carbon hypercar: 5.0 L twin-turbo V8, 9-speed multi-clutch LST,
        // huge rear wing (1,000 kg of downforce at 290 km/h).
        VehicleSpec jesko = civic;
        jesko.name = "KOENIGSEGG JESKO";
        jesko.id = "jesko";
        jesko.blurb = "ATTACK  5.0L TWIN-TURBO V8  9-SPEED LST  RWD";
        jesko.profile = {
            "#WW######WW#",
            "############",
            "...GGGGG###.",
        };
        jesko.layerY = {0.11, 0.48, 0.80, 1.21};
        jesko.length = 4.626;
        jesko.width = 2.030;
        jesko.wheelbase = 2.700;
        jesko.frontOverhang = 0.90;
        // Low nose rising to the cowl; long engine cover falling to the tail.
        jesko.topLine = {0.50, 0.66, 0.77, 0, 0, 0, 0, 0, 0, 1.07, 1.01, 0.95, 0.86};
        jesko.bumperLift = 0.0;
        jesko.cornerRound = 0.16;
        jesko.roofTuck = 0.24;   // teardrop canopy
        jesko.rakeFront = 0.75;  // very raked windshield
        jesko.rakeRear = 0.15;   // canopy tapers into the engine cover
        // 1,420 kg curb (Koenigsegg); about 42/58 with the V8 behind the seats.
        jesko.structureMass = 385;  // carbon tub, subframes, crash structures
        jesko.panelMass = 95;       // carbon panels, glass (the roof is one thin carbon panel)
        jesko.items = {
            {"front radiators", 25, 2.05, 0.40, 0},       {"front splitter and canards", 15, 2.20, 0.18, 0},
            {"front crash structure", 10, 2.20, 0.35, 0}, {"front lift system", 10, 1.45, 0.40, 0},
            {"battery", 10, 1.75, 0.45, 0.4},             {"air conditioning", 15, 1.6, 0.36, -0.3},
            {"dashboard", 22, 0.95, 0.55, 0},             {"carbon seats", 24, 0.25, 0.25, 0},
            {"carpets, trim and wiring", 40, 0.4, 0.35, 0}, {"steering and pedals", 15, 1.1, 0.4, -0.3},
            {"fuel tank (72 L, full)", 60, -0.35, 0.30, 0}, {"intercoolers and turbos", 32, -0.95, 0.40, 0},  // slung low beside the block
            {"dry sump oil tank", 15, -0.55, 0.40, 0.45}, {"air tank and compressor", 10, -0.5, 0.3, -0.45},
            {"exhaust", 25, -1.75, 0.55, 0},              {"rear wing and pylons", 26, -1.95, 1.15, 0},
            {"rear crash structure", 8, -2.25, 0.35, 0},  {"brake hydraulics and electronics", 40, 0.0, 0.20, 0},
            {"carbon tub floor and sills", 91, 0.3, 0.16, 0},
        };
        jesko.engineMass = 189;  // Koenigsegg's figure
        jesko.gearboxMass = 90;  // LST, 90 kg
        jesko.transverse = false;
        jesko.midEngine = true;
        jesko.engineX = -0.77;
        jesko.engineY = 0.43;
        jesko.engineZ = 0;
        jesko.engineLen = 0.62;
        jesko.engineWid = 0.66;
        jesko.engineHgt = 0.46;
        jesko.gearboxLen = 0.42;
        jesko.bodyStiffness = 1.1;   // carbon monocoque
        jesko.carbonTub = true;
        jesko.wheelRadius = 0.355;    // 265/35R20 front, 325/30R21 rear
        jesko.wheelWidth = 0.265;
        jesko.wheelWidthRear = 0.325;
        jesko.trackFront = 1.70;
        jesko.trackRear = 1.66;
        jesko.wheelMass = 22;
        jesko.hubMass = 17;
        jesko.frontSusp = WISHBONE;
        jesko.rearSusp = WISHBONE;
        jesko.rideFreqFront = 2.1;  // stiff track springs (editor-ui's 2.3 / 2.45 / 0.50 made it scrape and wander at 250+ km/h: freeway/top-speed)
        jesko.rideFreqRear = 2.25;
        jesko.dampingRatio = 0.48;
        // Koenigsegg's Triplex: a third, horizontal damper and spring across each axle that
        // holds the car up against ~1,000 kg of downforce at speed (estimate: it picks up after
        // 8 mm and roughly triples each corner's rate).
        jesko.heaveRate = 150000;
        jesko.heaveGap = 0.008;
        jesko.heaveDamping = 22000;  // well damped: it mustn't bounce the car off a kerb
        jesko.arbFront = 18000;  // very stiff anti-roll bars for high-speed cornering
        jesko.arbRear = 20000;
        jesko.tireGrip = 1.32;  // Michelin Cup 2 R
        jesko.tireType = TIRE_SEMI_SLICK;
        jesko.stockTireType = TIRE_SEMI_SLICK, jesko.stockTireRatingKmh = 500;  // Michelin Cup 2 R: rated past 450
        jesko.lateralGrip = 1.0;  // semi-slick Cup 2 R: corners as hard as it brakes (~1.2 g at skidpad speed; editor-ui's 1.15 spun it off the freeway at 330 km/h)
        jesko.drive = VehicleSpec::RWD;
        jesko.diffFront = DIFF_OPEN;
        jesko.diffRear = DIFF_LSD;  // electronic differential
        jesko.lsdBias = 3.0;
        jesko.brakeFront = 4400;  // 410 mm carbon-ceramic, 6 pistons
        jesko.brakeRear = 3300;   // 395 mm, 4 pistons
        jesko.dragArea = 0.75;
        jesko.downforce = 1.48;       // 700 kg at 250 km/h, 1,000 kg at 290 km/h
        jesko.downforceMax = 13700;   // 1,400 kg
        jesko.aeroBalance = 0.40;
        // 1,280 hp at 7,800 rpm on petrol, over 1,000 N*m from 2,700 to 6,170.
        jesko.torque = {{0, 400}, {1500, 560}, {2700, 1000}, {4000, 1150}, {5100, 1250}, {6500, 1240}, {7800, 1168}, {8500, 1050}};
        jesko.idleRpm = 950;
        jesko.redline = 8500;
        jesko.turboLag = 0.25;  // the air tank spools the turbos
        jesko.gears = {4.720, 3.644, 2.874, 2.250, 1.737, 1.370, 1.078, 0.833, 0.657};
        jesko.reverseRatio = 5.0;
        jesko.finalDrive = 3.3;
        jesko.shiftTime = 0.03;  // multi-clutch: practically instant
        jesko.engineName = "5.0L TWIN-TURBO V8";
        jesko.gearboxName = "9-SPEED LST";
        jesko.doors = 2;
        jesko.doorLength = 1.2;
        jesko.cylinders = 8;
        jesko.flatPlane = true;
        jesko.displacement = 5.0;
        jesko.rimRadius = 0.254;
        jesko.wheelStyle = WHEEL_FORGED;
        jesko.paintColor[0] = 0.08f, jesko.paintColor[1] = 0.32f, jesko.paintColor[2] = 0.72f;
        // Plan-view taper: wide fenders, tucked doors, narrow rounded nose.
        jesko.bodyWidth = {0.84, 0.95, 1.0, 0.98, 0.93, 0.90, 0.90, 0.94, 1.0, 1.0, 1.0, 0.96, 0.90};
        jesko.lampStyle = LAMP_SLIM;
        jesko.grilleStyle = GRILLE_INTAKE;
        jesko.extras = EXTRA_WING | EXTRA_ROOF_SCOOP;
        jesko.coverColor[0] = 0.55f, jesko.coverColor[1] = 0.45f, jesko.coverColor[2] = 0.12f;
        // Ride height: ~100 mm under the splitter and floor at rest (it was drawn 15 mm
        // low, and scraped at 300+ km/h with the downforce on).
        raiseBody(jesko, 0.015);
        v.push_back(jesko);

        // ---- Ford Bronco 4-door ------------------------------------------------------------
        // Body-on-frame 4x4: 2.7 L twin-turbo V6, 10-speed auto, independent front,
        // five-link solid rear axle, 33 in tires, swing gate with the spare.
        VehicleSpec bronco = civic;
        bronco.name = "FORD BRONCO";
        bronco.id = "bronco";
        bronco.blurb = "4-DOOR  2.7L TWIN-TURBO V6  10-SPEED AUTO  4X4";
        bronco.profile = {
            "#WW######WW#",
            "############",
            "...GGGGGGGGG",
        };
        bronco.layerY = {0.44, 0.88, 1.25, 1.88};
        bronco.length = 4.53;  // body only: 4,811 mm with the spare wheel on the gate
        bronco.width = 1.928;
        bronco.wheelbase = 2.949;
        bronco.frontOverhang = 0.775;
        bronco.topLine = {1.13, 1.22, 1.25};  // flat clamshell hood
        bronco.bumperLift = 0.14;
        bronco.bumperLiftRear = 0.09;  // ~35 degree departure on 33s (ESTIMATE: 29.8 base, 37.0 Sasquatch)
        bronco.cornerRound = 0.06;  // boxy
        bronco.roofTuck = 0.05;
        bronco.rakeFront = 0.32;    // upright windshield
        bronco.rakeRear = 0.02;
        // ~2,214 kg (2.7 4-door), about 56/44.
        bronco.structureMass = 610;  // boxed ladder frame and body shell
        bronco.panelMass = 220;
        bronco.items = {
            {"radiator", 20, 2.15, 0.85, 0},               {"battery", 22, 1.85, 1.05, -0.6},
            {"steel front bumper", 35, 2.33, 0.62, 0},     {"front diff and IFS subframe", 60, 1.63, 0.50, 0},
            {"front prop shaft", 9, 1.0, 0.50, 0.1},       {"rear prop shaft", 14, -0.4, 0.50, 0},
            {"dashboard and heater", 45, 0.85, 1.15, 0},   {"front seats", 50, 0.05, 0.85, 0},
            {"rear seat", 35, -0.85, 0.85, 0},             {"carpets, trim and wiring", 70, 0.1, 0.70, 0},
            {"fuel tank (64 L, full)", 60, -0.55, 0.55, 0.45}, {"exhaust", 30, -0.3, 0.38, 0.35},
            {"spare wheel on the gate", 46, -2.32, 1.05, 0}, {"rear bumper", 18, -2.20, 0.62, 0},
            {"hard top", 40, -0.3, 1.86, 0},               {"steering and brakes", 20, 1.5, 0.7, 0},
            {"skid plates and bars", 30, 0.9, 0.42, 0},    {"electronics", 15, 0.6, 0.9, 0},
            {"front frame horns and shock towers", 80, 1.85, 0.75, 0},
        };
        bronco.engineMass = 200;
        bronco.gearboxMass = 130;  // 10-speed auto and transfer case
        bronco.transverse = false;
        // (Its block just ahead of the front axle line, as on the real truck: the front diff sits
        // under the sump's back half. Was 1.45, 4 cm behind the axle.)
        bronco.engineX = 1.53;
        bronco.engineY = 0.88;
        bronco.engineZ = 0;
        bronco.engineLen = 0.62;
        bronco.engineWid = 0.66;
        bronco.engineHgt = 0.62;
        bronco.gearboxLen = 0.93;  // (the long 10R80 and the transfer case: its tail where it was before the engine came forward)
        bronco.bodyStiffness = 1.2;
        bronco.frameStiffness = 0.30;  // ladder frame: twists more than a unibody
        bronco.bodyStrength = 1.0;     // high-strength steel body
        bronco.frameStrength = 3.0;    // beefed up for offroad abuse
        // Fully boxed ladder frame, seven crossmembers; rails up to 9.06 x 3.58 in (Ford
        // spec; 0.20 m is the average depth along it). Rail spacing and mass: estimates.
        bronco.railSpacing = 1.00;
        bronco.railHeight = 0.20;
        bronco.railWidth = 0.091;
        bronco.frameMass = 230;
        // Section modulus 4.95 in^3 at 49.3 ksi (Ford): each rail yields at ~28 kNm, so a
        // press on the middle of the frame (axles 2.55 m apart) sets it at ~85 kN, and bends
        // it ~1.9 mm per 20 kN (E = 200 GPa, I = 22.7 in^4) up to then.
        bronco.railYield = 0.0065;
        bronco.bodyMounts = 8;
        // Transfer case skid ~0.31 m up on 33s (ESTIMATE, from a ~24 degree breakover between
        // the base 4-door's ~21 and the 35 in Sasquatch's 26.3).
        bronco.skidDrop = 0.11;
        bronco.panelStrength = 1.6;
        bronco.pinStrength = 1.8;
        bronco.wheelRadius = 0.415;    // 285/70R17 (33 in)
        bronco.wellTireRadius = 0.445; // its wheel openings are cut for the Sasquatch package's 35s
        bronco.wellTireWidth = 0.318;  // (35x12.50R17)
        bronco.wheelWidth = 0.285;
        bronco.trackFront = bronco.trackRear = 1.651;
        bronco.wheelMass = 38;
        bronco.hubMass = 28;
        bronco.axleMass = 90;  // Dana 44 rear
        bronco.frontSusp = WISHBONE;
        bronco.rearSusp = SOLID_AXLE;
        bronco.rideFreqFront = 1.25;
        bronco.rideFreqRear = 1.45;
        bronco.dampingRatio = 0.42;
        bronco.arbFront = 15000;
        bronco.arbRear = 5000;
        // Wheel travel: ~22 cm front (IFS), ~27 cm rear (5-link Dana 44).
        bronco.bumpFront = 0.09, bronco.droopFront = 0.13;
        bronco.bumpRear = 0.11, bronco.droopRear = 0.16;
        bronco.swayDisconnect = 1;  // the dash button: front bar unhooks for crawling
        bronco.tirePsi = 35;
        // Wheels that come off and a spare to swap on (tires.h): the trucks have it so far.
        bronco.wheelLoss = true;
        bronco.lugShear = 120000;  // six 14 mm studs (above every hit in the suite: see docs/handoff.md)  // six 14 mm studs
        bronco.tireType = TIRE_ALL_TERRAIN;
        bronco.tireGrip = 0.98;  // all-terrain tires: brake well, but the tall tread squirms in corners
        bronco.lateralGrip = 0.76;
        bronco.drive = VehicleSpec::AWD;
        bronco.diffFront = bronco.diffRear = DIFF_OPEN;
        bronco.transfer = XFER_PART_TIME;  // advanced 4x4: 2H, 4A, 4H, 4L
        bronco.lockerRear = true;          // Dana 44 rear e-locker (front locker: Sasquatch, in the garage)
        bronco.frontLockerLowOnly = true;  // Ford only lets the front locker in with 4L
        // Dana 44 (M220) rear shafts, ~36 mm induction-hardened 4340: about 11 kN*m at
        // ~1,200 MPa ultimate shear (estimate: tau * pi d^3 / 16). The IFS CVs take ~7 kN*m,
        // less at full lock. In 4L first the engine alone can put ~33 kN*m into the axles, so
        // a spinning wheel landing, or a locker binding at full lock, can snap one.
        bronco.shaftLimit = 13000;    // beefier axles for offroad abuse
        bronco.lowRange = 2.72;
        bronco.brakeFront = 3800;  // 311 mm vented
        bronco.brakeRear = 2800;   // 308 mm vented
        bronco.dragArea = 1.70;    // Cd ~0.53 x 3.2 m^2
        // 330 hp at 5,250 rpm, 563 N*m at 3,100 (premium fuel).
        bronco.torque = {{0, 240}, {1000, 330}, {2000, 500}, {3100, 563}, {4500, 530}, {5250, 447}, {5800, 400}};
        bronco.idleRpm = 700;
        bronco.redline = 5800;
        bronco.turboLag = 0.45;
        bronco.gears = {4.714, 2.997, 2.149, 1.769, 1.521, 1.275, 1.000, 0.853, 0.689, 0.636};
        bronco.reverseRatio = 4.885;
        bronco.finalDrive = 4.27;
        bronco.shiftTime = 0.20;
        bronco.stallRpm = 2600;  // 10R80 converter behind the 2.7 (estimate: typical for the pair)
        bronco.stallRatio = 1.9;
        bronco.lockupGear = 3;
        bronco.engineName = "2.7L TWIN-TURBO V6";
        bronco.gearboxName = "10-SPEED AUTO";
        bronco.doors = 4;
        bronco.cylinders = 6;
        bronco.displacement = 2.7;
        bronco.rimRadius = 0.216;
        bronco.wheelStyle = WHEEL_OFFROAD_BLACK;
        bronco.paintColor[0] = 0.30f, bronco.paintColor[1] = 0.40f, bronco.paintColor[2] = 0.43f;
        bronco.lampStyle = LAMP_ROUND;
        bronco.grilleStyle = GRILLE_WIDE;
        bronco.extras = EXTRA_SPARE | EXTRA_FLARES | EXTRA_SKID_PLATE | EXTRA_SLIDERS | EXTRA_WINCH;
        bronco.coverColor[0] = 0.05f, bronco.coverColor[1] = 0.05f, bronco.coverColor[2] = 0.06f;
        v.push_back(bronco);

        // ---- Toyota 4Runner Limited 4x4 (3rd generation, 2002), as built ----------------
        // The Bring a Trailer truck (lot 61042, sold December 2021): a black 2002 Limited
        // with a Magnuson supercharger, Old Man Emu coils, Bilstein struts and SPC upper
        // arms, a Toytec 2 in body lift, 35x12.50R17 BFGoodrich KO2s on bronze Icon
        // Rebound wheels, Body Armor bumpers with a Smittybilt winch and a swing-out tire
        // carrier, rock sliders, a snorkel and a BajaRack roof basket.
        // Underneath it's the factory truck: body on a ladder frame, 3.4 L 5VZ-FE V6,
        // A340F 4-speed automatic with 4.10 axles, Multi-Mode 4WD with a 2.566:1 low range,
        // double wishbones in front and a four-link coil-sprung solid axle behind.
        VehicleSpec runner = bronco;
        runner.name = "TOYOTA 4RUNNER";
        runner.id = "4runner";
        runner.blurb = "2002 LIMITED  SUPERCHARGED 3.4L V6  LIFTED ON 35S";
        runner.profile = {
            "#WW######WW#",
            "############",
            "...GGGGGGGGG",
        };
        // Factory body first (Limited: 4,656 x 1,689 body / 1,801 over the flares x
        // 1,740 mm, 2,675 mm wheelbase)...
        runner.layerY = {0.40, 0.80, 1.09, 1.74};
        runner.length = 4.656;
        runner.width = 1.689;
        runner.wheelbase = 2.675;
        runner.frontOverhang = 0.85;
        runner.topLine = {0.99, 1.06, 1.08};  // long, nearly flat hood over a blunt nose
        runner.bumperLift = 0.10;
        runner.cornerRound = 0.07;  // square-shouldered
        runner.roofTuck = 0.08;
        runner.rakeFront = 0.50;
        runner.rakeRear = 0.05;     // upright tailgate
        runner.liftgate = true;     // third-generation 4Runner: top-hinged liftgate with power rear glass
        // Limited 4x4 1,867 kg; the build adds ~290 kg (bumpers and winch, carrier and
        // spare, sliders, basket, supercharger, heavier wheels): ~2,150 kg, about 55/45.
        runner.structureMass = 455;  // ladder frame and body shell
        runner.panelMass = 175;
        runner.items = {
            {"radiator", 18, 1.95, 0.80, 0},               {"battery", 20, 1.70, 0.95, -0.55},
            {"front diff and IFS crossmember", 50, 1.46, 0.46, 0},
            {"front prop shaft", 9, 0.85, 0.46, 0.1},      {"rear prop shaft", 13, -0.35, 0.46, 0},
            {"dashboard and heater", 40, 0.80, 1.02, 0},   {"leather power seats", 58, 0.05, 0.78, 0},
            {"rear seat", 35, -0.80, 0.78, 0},             {"carpets, trim and wiring", 75, 0.0, 0.66, 0},
            {"fuel tank (70 L, full)", 62, -0.55, 0.46, 0}, {"exhaust (Gibson cat-back)", 26, -0.3, 0.34, 0.33},
            {"sunroof and roof trim", 18, 0.1, 1.72, 0},  {"steering and brakes", 18, 1.4, 0.65, 0},
            {"front skid plate", 22, 1.3, 0.38, 0},        {"electronics and stereo", 18, 0.4, 0.85, 0},
            {"front frame horns and shock towers", 90, 1.75, 0.68, 0},
            {"Body Armor front bumper", 48, 2.36, 0.58, 0}, {"Smittybilt winch and rope", 34, 2.38, 0.58, 0},
            {"Body Armor rear bumper and carrier", 52, -2.38, 0.58, 0},
            {"spare 35 in wheel on the carrier", 44, -2.62, 1.0, -0.34},
            {"rock sliders", 36, 0.0, 0.36, 0},           {"roof basket and lights", 30, -0.25, 1.86, 0},
            {"snorkel", 4, 1.0, 1.5, 0.85},
        };
        runner.engineMass = 198;   // iron-block 5VZ-FE with the supercharger
        runner.gearboxMass = 100;  // 4-speed auto and transfer case
        // (Its block over the front axle, the IFS diff under the sump, as on the real 3rd-gen
        // 4Runner. Was 1.28, 20 cm behind it: the bay drawn ahead of it stood empty.)
        runner.engineX = 1.43;
        runner.engineY = 0.80;
        runner.engineLen = 0.58;
        runner.engineWid = 0.62;
        runner.engineHgt = 0.62;
        runner.gearboxLen = 0.85;
        runner.bodyStiffness = 1.2;
        runner.frameStiffness = 0.06;  // its thinner rails (below) bend more, and its open C-channel tail twists
        runner.bodyStrength = 1.0;   // thick 90s sheet metal
        runner.frameStrength = 3.0;  // beefed up for offroad abuse
        runner.panelStrength = 1.6;
        // Boxed ladder frame (C-channel at the tail), ~1/8 in wall, ~2.75 in wide rails;
        // 36.75 in outside at the hitch, so ~0.86 m between the rails' centres there (ESTIMATE:
        // 0.90 m along the cab). Depth and mass: estimates.
        runner.railSpacing = 0.90;
        runner.railHeight = 0.15;
        runner.railWidth = 0.07;
        runner.frameMass = 170;
        // A 2.75 x 5.9 in box of 1/8 in steel (~44 ksi, ESTIMATE): ~2.9 in^3, so each rail
        // yields at ~14 kNm; a press between the axles (2.675 m) sets it at ~43 kN and bends it
        // ~6 mm per 20 kN up to then: half the Bronco's frame. (Here ~53 kN and ~5 mm: the
        // crossmembers and brackets stiffen it a little.) Its open tail twists: ~3 kNm/deg.
        runner.railYield = 0.011;
        // Transfer case skid: a stock 3rd gen's ~0.30 m (ESTIMATE), plus the OME lift and the
        // 35s' extra radius (the body lift doesn't move the frame): ~0.40 m.
        runner.skidDrop = 0.15;
        runner.pinStrength = 1.8;
        // ...then the lift: ~65 mm of Old Man Emu springs, the 2 in (51 mm) body lift, and
        // 56 mm more tire radius (35x12.50R17 against the factory 265/70R16).
        runner.wheelRadius = 0.389;  // factory 265/70R16, 777 mm
        raiseBody(runner, 0.065 + 0.051 + (0.445 - 0.389));
        runner.wheelRadius = 0.445;  // 35x12.50R17
        runner.wheelWidth = 0.318;
        runner.trackFront = runner.trackRear = 1.56;  // factory 1,506 mm, wider with the Icon wheels
        runner.wheelMass = 45;       // KO2 and 17 in alloy (factory: ~30 kg)
        runner.hubMass = 25;
        runner.axleMass = 80;
        runner.rideFreqFront = 1.32;  // heavy-duty OME coils
        runner.rideFreqRear = 1.55;  // heavy-duty OME coils, for the steel bumper, carrier and 35 in spare out back
        runner.dampingRatio = 0.44;   // Bilsteins
        runner.arbFront = 14000;
        runner.arbRear = 4000;
        // Wheel travel: ~20 cm front (IFS with OME), ~25 cm rear (4-link coils).
        runner.bumpFront = 0.085, runner.droopFront = 0.12;
        runner.bumpRear = 0.11, runner.droopRear = 0.14;
        runner.swayDisconnect = 2;  // quick-disconnect end links: pull the pins, stopped
        runner.tirePsi = 35;
        runner.wheelLoss = true;
        runner.lugShear = 100000;      // heavy-duty studs on the 4Runner
        runner.beadlock = false;
        runner.tireType = TIRE_ALL_TERRAIN;
        runner.tireGrip = 0.95;       // KO2: good on the brakes, soft-blocked in corners
        runner.lateralGrip = 0.74;
        runner.diffFront = runner.diffRear = DIFF_OPEN;
        runner.transfer = XFER_FULL_TIME;  // multi-mode 4WD with the center diff lock on the dash
        runner.lockerRear = true;          // factory rear e-locker option
        runner.frontLockerLowOnly = false;
        runner.rearLockerLowOnly = true;   // Toyota's e-locker only engages in 4L
        // A shaft has to carry what its tire can put into it: tractive force x tire radius (a front tire on a
        // ledge face carries 10 to 22 kN and grips at mu ~0.8 to 1). The IFS CVs are 65% of this, less at lock.
        // At 10,000 the 4Runner's snapped at 14.6 kN of traction on its 35s (13.8 on the 37s), the Tacoma's
        // hold 18.4 on its 265s and the Bronco's 20.4: the same flat-out ledge crawl lost a 4Runner CV in 9 of
        // 24 runs against 2 and 3 for them. 13,000 (the Bronco's) holds 18 kN on the 37s, as the Bronco does.
        runner.shaftLimit = 13000;         // beefed up for offroad abuse
        runner.lowRange = 2.566;
        // 319 mm vented discs, 4-piston calipers: two ~43 mm pistons a side, pads (mu ~0.4) at
        // ~0.13 m, ~30 N*m per bar; a panic stop puts ~140 bar in (ESTIMATES): ~4.2 kN*m. That
        // takes the 35s to the ABS even with the nose loaded (~9 kN x 0.95 x 0.445 m = 3.8 kN*m).
        // (At 3,400 the fronts couldn't reach it: held at the same torque each side, they lose
        // the pull a front ABS gives on the more loaded side, which is what holds a car straight,
        // and the lifted panhard's sideways push and the offset spare steered the truck 6 deg.)
        runner.brakeFront = 4200;
        runner.brakeRear = 2000;   // 295 mm drums
        runner.dragArea = 1.50;    // Cd ~0.50 x 3.0 m^2 with the lift, basket and lights
        // Factory 183 hp / 294 N*m; the Magnuson blower at ~6 psi: ~256 hp at 5,200 rpm,
        // 366 N*m (270 lb-ft) at 3,600.
        runner.torque = {{0, 230}, {1000, 290}, {2000, 340}, {3000, 360}, {3600, 366}, {4200, 364}, {4800, 358}, {5200, 350}, {5500, 315}};
        runner.idleRpm = 700;
        runner.redline = 5500;
        runner.induction = VehicleSpec::SUPERCHARGER;
        runner.displacement = 3.4;
        runner.cylinders = 6;
        runner.turboLag = 0;  // a positive-displacement blower: boost from idle
        runner.gears = {2.804, 1.531, 1.000, 0.705};
        runner.reverseRatio = 2.393;
        runner.finalDrive = 4.10;   // never regeared: tall on 35s
        runner.shiftTime = 0.40;
        runner.stallRpm = 2200;  // A340F converter (estimate; the blower doesn't change it)
        runner.stallRatio = 2.0;
        runner.lockupGear = 3;   // locks up in 3rd and overdrive
        runner.engineName = "3.4L V6 5VZ-FE SUPERCHARGED";
        runner.gearboxName = "A340F 4-SPEED AUTO";
        runner.lampStyle = LAMP_RECT;
        runner.grilleStyle = GRILLE_WIDE;
        runner.extras = EXTRA_FLARES | EXTRA_SPARE | EXTRA_ARMOR | EXTRA_SNORKEL | EXTRA_ROOF_BASKET | EXTRA_SLIDERS | EXTRA_SKID_PLATE | EXTRA_WINCH;
        runner.paintColor[0] = 0.025f, runner.paintColor[1] = 0.026f, runner.paintColor[2] = 0.03f;  // Black
        runner.trimColor[0] = 0.47f, runner.trimColor[1] = 0.47f, runner.trimColor[2] = 0.46f;       // Limited two-tone (silver)
        runner.trimTop = 0.72f;
        runner.wheelStyle = WHEEL_OFFROAD_BRONZE;
        runner.coverColor[0] = 0.1f, runner.coverColor[1] = 0.1f, runner.coverColor[2] = 0.11f;
        v.push_back(runner);

        // ---- Ultra4-class rock buggy ---------------------------------------------------
        // A tube-chassis crawler/racer of the kind built for Ultra4 and King of the
        // Hammers: a chromoly tube frame and roll cage with a fiberglass hood and tail,
        // a front-mid 6.2 L LS-type V8 (Chevrolet's LS3 crate engine is rated 430 hp at
        // 5,900 rpm and 424 lb-ft at 4,600; these builds run ~450 hp), a GM 4L80E
        // four-speed automatic behind an Atlas-style two-speed transfer case, solid axles
        // front and rear on long four-link suspensions (Dana 60 front, 9 in rear; ~16 in of
        // wheel travel on 2.5 in coil-overs), 40 in tires on beadlock wheels run at
        // 12-18 psi, and selectable lockers in both axles. The numbers are class figures,
        // not one builder's car: weights and travel are estimates.
        VehicleSpec buggy = bronco;
        // (Not a Bronco underneath: a tube chassis with no ladder frame, body mounts, transfer
        // case skid or factory wheel wells, so none of the Bronco's frame settings carry over.)
        buggy.railSpacing = 0;
        buggy.wheelLoss = false;  // (the trucks' wheels-come-off and spare swaps; not the buggy yet)
        buggy.frameMass = 0;
        buggy.skidDrop = 0;
        buggy.wellTireRadius = buggy.wellTireWidth = 0;
        buggy.name = "ROCK BUGGY";
        buggy.id = "buggy";
        buggy.blurb = "ULTRA4 TUBE CHASSIS  6.2L V8  4-SPEED AUTO  40 IN TIRES";
        // 14 columns: 'p' is fiberglass (hood, tail), 'G' the cage (not drawn: tubes are), '#' the frame.
        buggy.profile = {
            "ppWW######WWpp",
            "ppp###GGGG##pp",
            "......GGGG....",
        };
        buggy.layerY = {0.62, 0.90, 1.22, 1.70};  // 24 in under the frame; 67 in to the cage top
        buggy.length = 4.70;
        buggy.width = 1.50;     // the chassis; the tires stand outside it
        buggy.widthCells = 4;
        buggy.wheelbase = 2.90;  // 114 in
        buggy.frontOverhang = 0.95;
        buggy.topLine = {0.96, 1.04, 1.12, 1.16, 1.16, 1.16, 0, 0, 0, 0, 0, 1.08, 1.04, 1.00, 0.96};
        // The tube frame necks in between the tires (the rails run ~0.34 m either side of the
        // middle there, as drawn): full width only at the cockpit. At 1.5 m all the way along, its
        // sides stood 2 cm inside the tires' sidewalls, and the rear tires rubbed them as the
        // car squatted under power. Narrowed to 0.9 there (~10 cm clear of the sidewalls): at
        // 0.62 the floor nodes over the rear axle bunched up so a hard landing drove the diff up
        // between them and trapped it inside the body, the housing rolled 40 degrees. (It still
        // lands ~5 cm deeper than the trucks: the diff's top sits level with the floor at rest.)
        buggy.bodyWidth = {1, 1, 0.9, 0.9, 0.9, 1, 1, 1, 1, 1, 0.9, 0.9, 0.9, 1, 1};
        buggy.openFrame = true;
        buggy.doors = 0;
        buggy.liftgate = false;
        buggy.noseDrop = buggy.tailDrop = 0;
        // The tube bumpers sit high, where buildOpenFrame draws them: the front one's wings at
        // ~0.84 m, the rear one at ~0.97 m (the lattice's nose and tail were 0.62 m up, under them).
        buggy.bumperLift = 0.22;
        buggy.bumperLiftRear = 0.33;
        buggy.cornerRound = 0.05;
        buggy.roofTuck = 0.08;
        buggy.rakeFront = 0.12;
        buggy.rakeRear = 0.10;
        // ~1,800 kg (estimate: Ultra4 cars race at ~3,800-4,500 lb), about 54/46 with the engine
        // just behind the front axle and the fuel cell at the tail.
        buggy.structureMass = 312;  // chromoly tube frame, cage, floor and skid plates
        buggy.panelMass = 40;       // fiberglass hood and tail, aluminum floor
        buggy.items = {
            {"radiator and fan", 24, 2.00, 0.95, 0},        {"front bumper hoop and winch", 52, 2.32, 0.80, 0},
            {"battery", 20, -0.95, 0.80, 0},                {"steering box and hydraulic ram", 22, 1.05, 0.78, 0},
            {"bucket seats", 36, -0.30, 0.82, 0},           {"dashboard, wiring and switches", 28, 0.15, 1.10, 0},
            {"fuel cell (95 L, full)", 71, -1.80, 0.85, 0}, {"exhaust (two side pipes)", 28, 0.25, 0.70, 0},
            {"prop shafts", 22, 0.1, 0.62, 0},              {"rear bumper and recovery gear", 36, -2.30, 0.80, 0},
            {"front coil-overs and bump stops", 26, 1.28, 1.05, 0}, {"rear coil-overs and bump stops", 26, -1.55, 1.05, 0},
            {"skid plates", 30, 0.5, 0.60, 0},              {"jack and recovery straps", 12, -2.0, 1.0, 0},
            {"upper cage tubing", 30, -0.35, 1.55, 0},      {"brake lines, cooler and electronics", 24, 0.3, 0.7, 0},
        };
        buggy.engineMass = 215;  // 6.2 L aluminum LS with its accessories
        buggy.gearboxMass = 190;  // 4L80E and the transfer case
        buggy.transverse = false;
        buggy.midEngine = false;
        buggy.engineX = 0.92;
        buggy.engineY = 0.98;
        buggy.engineZ = 0;
        buggy.engineLen = 0.70;
        buggy.engineWid = 0.66;
        buggy.engineHgt = 0.60;
        buggy.gearboxLen = 0.95;
        buggy.bodyStiffness = 1.3;
        buggy.bodyStrength = 3.0;   // 1.75 in chromoly tube: it takes a hit that would fold sheet steel
        buggy.frameStiffness = 1.0;
        buggy.frameStrength = 3.0;
        buggy.panelStrength = 0.6;  // fiberglass cracks and falls off
        buggy.pinStrength = 0.6;
        buggy.wheelRadius = 0.508;  // 40x13.5-17 (a 40 in tire)
        buggy.wheelWidth = 0.343;
        buggy.wheelWidthRear = 0;
        buggy.rimRadius = 0.2159;   // 17 in beadlock
        buggy.trackFront = buggy.trackRear = 1.88;
        buggy.wheelMass = 58;       // ~42 kg of tire and ~16 kg of beadlock wheel
        buggy.hubMass = 30;
        buggy.axleMass = 118;       // Dana 60 / 9 in housings with 40-spline shafts and big discs
        buggy.frontSusp = SOLID_AXLE;
        buggy.rearSusp = SOLID_AXLE;
        buggy.linkLength = 1.05;    // long four-link arms
        buggy.upperLink = 0.714;    // the upper link (0.75 m) is a bit shorter, as on the trucks
        buggy.coilTop = 0.62;
        buggy.rideFreqFront = 1.2;
        buggy.rideFreqRear = 1.3;
        buggy.dampingRatio = 0.45;
        buggy.arbFront = buggy.arbRear = 0;  // no sway bars
        buggy.swayDisconnect = 0;
        // ~16 in (0.41 m) front and ~17.5 in (0.44 m) rear wheel travel (estimate for a long-travel buggy).
        buggy.bumpFront = 0.17, buggy.droopFront = 0.23;
        buggy.bumpRear = 0.19, buggy.droopRear = 0.25;
        buggy.tirePsi = 30;         // desert and road pressure; the rocks want 12-15 (the game airs between 12 and 40)
        buggy.tireSoft = 0.55;      // a 40 in sidewall at that pressure is soft
        buggy.looseGrip = 1.18;     // a soft, deep-lugged crawler compound
        buggy.ledgeFactor = 1.2;    // measured: it climbs 0.7 m square ledges at 12 psi in 4L with lockers (the Bronco: 0.45)
        buggy.tireType = TIRE_MUD_TERRAIN;
        buggy.tireGrip = 0.95;
        buggy.lateralGrip = 0.70;
        buggy.drive = VehicleSpec::AWD;
        buggy.diffFront = buggy.diffRear = DIFF_OPEN;
        buggy.transfer = XFER_SELECT;
        buggy.lockerFront = buggy.lockerRear = true;  // air lockers in both axles (a switch each)
        buggy.frontLockerLowOnly = buggy.rearLockerLowOnly = false;
        // 40-spline 4340 chromoly shafts, ~41 mm at the spline root: ~16.5 kN*m at ~1,200 MPa
        // ultimate shear (estimate, tau * pi d^3 / 16, as for the Bronco's).
        buggy.shaftLimit = 16500;
        buggy.lowRange = 4.3;       // Atlas-style low range (they come 2.0 to 5.0)
        buggy.brakeFront = 4200;    // 13 in discs, 4-piston calipers (estimate)
        buggy.brakeRear = 3600;
        buggy.dragArea = 1.95;      // Cd ~0.85 x 2.3 m^2: a box of tubes and tires
        // 450 hp at 6,000 rpm, ~565 N*m at 4,600 (LS3 crate: 430 hp, 575 N*m).
        buggy.torque = {{0, 300}, {1000, 360}, {2000, 470}, {3000, 520}, {4000, 555}, {4600, 565}, {5200, 560}, {6000, 534}, {6500, 485}};
        buggy.idleRpm = 800;
        buggy.redline = 6500;
        buggy.induction = VehicleSpec::NATURAL;
        buggy.turboLag = 0;
        buggy.cylinders = 8;
        buggy.flatPlane = false;
        buggy.displacement = 6.2;
        buggy.gears = {2.48, 1.48, 1.00, 0.75};  // 4L80E
        buggy.reverseRatio = 2.08;  // (GM: 2.48 / 1.48 / 1.00 / 0.75, reverse 2.08)
        buggy.finalDrive = 5.38;    // typical for 40s
        buggy.shiftTime = 0.30;
        // 4L80E converter: these crawl/race builds run ~2,000-2,400 rpm stalls (estimate: low
        // enough to creep up a ledge on the brake, high enough to launch); ~2.0 torque ratio at
        // stall (GM's 298 mm 4L80E converter); the lock-up clutch applies in 3rd and 4th.
        buggy.stallRpm = 2200;
        buggy.stallRatio = 2.0;
        buggy.lockupGear = 3;
        // A race car: no ABS, so no stability control or brake traction control either (the
        // lockers do that job). Its standalone engine ECU (Holley Dominator / MoTeC class, as these
        // LS builds run) has a throttle-side traction control, which stays.
        buggy.absEnabled = false;
        buggy.tractionFitted = true;
        buggy.engineName = "6.2L LS V8";
        buggy.gearboxName = "4L80E 4-SPEED AUTO + ATLAS 2-SPEED";
        buggy.lampStyle = LAMP_RECT;
        buggy.grilleStyle = GRILLE_INTAKE;
        buggy.extras = EXTRA_TUBE_CAGE | EXTRA_WINCH | EXTRA_SKID_PLATE;
        buggy.wheelStyle = WHEEL_OFFROAD_BLACK;
        buggy.trimColor[0] = buggy.trimColor[1] = buggy.trimColor[2] = 0;
        buggy.trimTop = 0;
        buggy.paintColor[0] = 0.95f, buggy.paintColor[1] = 0.55f, buggy.paintColor[2] = 0.06f;  // orange
        buggy.coverColor[0] = 0.55f, buggy.coverColor[1] = 0.1f, buggy.coverColor[2] = 0.08f;
        v.push_back(buggy);

        // ---- Turbo side-by-side (RZR / Maverick class) ---------------------------------
        // A ~200 hp long-travel sport UTV like the Can-Am Maverick X3 Turbo RR (a 900 cc
        // Rotax three-cylinder turbo, 195 hp, rated up to ~85 mph) or Polaris's RZR Turbo S:
        // a tube chassis and cage with plastic bodywork, the engine behind the seats driving
        // all four wheels through a CVT (approximated with five close ratios), double
        // wishbones all round with ~16-18 in of travel, 32 in tires on 15 in wheels. Class
        // figures: the 1.7 m width and ~800 kg are what the class is sold at; the wheelbase
        // (~102 in), lengths and weight split are estimates.
        VehicleSpec utv = jesko;
        utv.name = "TURBO UTV";
        utv.id = "utv";
        utv.blurb = "SIDE-BY-SIDE  900CC TURBO TRIPLE  CVT  4X4  LONG TRAVEL";
        utv.profile = {
            "pWWppWWp",
            "pwwGGwwp",
            "...GG...",
        };
        utv.layerY = {0.40, 0.72, 1.12, 1.78};
        utv.length = 3.35;
        utv.width = 1.40;
        utv.widthCells = 4;
        utv.wheelbase = 2.60;
        utv.frontOverhang = 0.42;
        utv.topLine = {0.84, 0.92, 0.98, 0, 0, 0, 0.98, 0.95, 0.90};
        utv.bodyWidth.clear();
        utv.openFrame = true;
        utv.doors = 0;
        utv.noseDrop = utv.tailDrop = 0;
        utv.bumperLift = 0.0;
        utv.cornerRound = 0.06;
        utv.roofTuck = 0.06;
        utv.rakeFront = 0.05;
        utv.rakeRear = 0.05;
        // ~800 kg with fluids (class figure), about 47/53 with the engine behind the seats.
        utv.structureMass = 165;  // chromoly tube frame and cage
        utv.panelMass = 55;       // plastic hood, fenders and tail
        utv.items = {
            {"radiator and intercooler", 14, 1.55, 0.62, 0},  {"front bumper and skid plate", 22, 1.60, 0.46, 0},
            {"front differential", 26, 1.26, 0.45, 0},         {"front prop shaft", 8, 0.1, 0.45, 0},
            {"battery", 11, 0.95, 0.65, 0},                   {"dashboard, steering and pedals", 20, 0.72, 0.85, 0},
            {"seats and belts", 30, -0.05, 0.62, 0},            {"fuel tank (40 L, full)", 30, -0.30, 0.50, 0},
            {"exhaust and muffler", 12, -1.35, 0.62, 0},     {"rear bumper and tow hitch", 10, -1.70, 0.55, 0},
            {"coil-overs", 28, 0.0, 0.8, 0},                    {"skid plates", 14, 0.2, 0.40, 0},
            {"cargo box and tail", 10, -1.40, 0.9, 0},          {"electronics, lamps and wiring", 18, 0.5, 0.9, 0},
        };
        utv.engineMass = 85;      // 900 cc triple with the turbo, intercooler plumbing and manifold
        utv.gearboxMass = 55;     // CVT housing, gearbox and the front-drive output
        utv.transverse = false;
        utv.midEngine = true;
        utv.engineX = -0.80;
        utv.engineY = 0.60;
        utv.engineZ = 0;
        utv.engineLen = 0.46;
        utv.engineWid = 0.45;
        utv.engineHgt = 0.46;
        utv.gearboxLen = 0.32;
        utv.bodyStiffness = 0.8;
        utv.bodyStrength = 3.5;
        utv.frameStiffness = 0.7;
        utv.frameStrength = 3.0;
        utv.panelStrength = 0.5;
        utv.pinStrength = 0.5;
        utv.wheelRadius = 0.406;   // 32x10R15
        utv.wheelWidth = 0.254;
        utv.wheelWidthRear = 0;
        utv.rimRadius = 0.1905;
        utv.trackFront = utv.trackRear = 1.46;  // 1.7 m over the tires
        utv.wheelMass = 25;
        utv.hubMass = 16;
        utv.axleMass = 60;
        utv.frontSusp = WISHBONE;
        utv.rearSusp = WISHBONE;
        utv.armLower = 0.46, utv.armUpper = 0.38;
        utv.rideFreqFront = 1.35;
        utv.rideFreqRear = 1.45;
        utv.dampingRatio = 0.38;
        utv.arbFront = 2500;
        utv.arbRear = 1500;
        utv.swayDisconnect = 0;
        utv.heaveRate = utv.heaveGap = utv.heaveDamping = 0;
        // ~16 in (0.41 m) front and ~18 in (0.46 m) rear travel.
        utv.bumpFront = 0.17, utv.droopFront = 0.24;
        utv.bumpRear = 0.19, utv.droopRear = 0.27;
        utv.tirePsi = 14;
        utv.tireSoft = 0.8;
        utv.tireType = TIRE_ALL_TERRAIN;
        utv.tireGrip = 1.00;
        utv.lateralGrip = 0.82;
        utv.drive = VehicleSpec::AWD;
        utv.diffFront = DIFF_OPEN;
        utv.diffRear = DIFF_LSD;  // (estimate: UTV rears are open or limited-slip)
        utv.lsdBias = 2.0;
        utv.transfer = XFER_ONDEMAND;
        utv.lockerFront = true;   // the front diff lock
        utv.lockerRear = false;
        utv.frontLockerLowOnly = utv.rearLockerLowOnly = false;
        utv.shaftLimit = 7500;    // CV axles (estimate)
        utv.lowRange = 0;
        utv.brakeFront = 1500;
        utv.brakeRear = 1300;
        utv.dragArea = 1.10;
        // (Built from the Jesko's spec: none of its wing's downforce carries over.)
        utv.downforce = utv.downforceMax = 0;
        utv.aeroBalance = 0.45;
        // Like the Maverick X3 and RZR: no ABS, no traction or stability control (Can-Am and
        // Polaris spec sheets list neither); the front diff lock is the traction aid.
        utv.absEnabled = false;
        utv.tractionFitted = false;
        // 195 hp at 7,500 rpm (Can-Am's rating for the Turbo RR), ~200 N*m through the mid-range.
        utv.torque = {{0, 90}, {1500, 110}, {3000, 160}, {4500, 195}, {5500, 205}, {6500, 200}, {7500, 185}, {8000, 165}, {8400, 130}};
        utv.idleRpm = 1400;
        utv.redline = 8300;
        utv.induction = VehicleSpec::TURBO;
        utv.turboLag = 0.4;
        utv.cylinders = 3;
        utv.flatPlane = false;
        utv.displacement = 0.9;
        utv.gears = {3.4, 2.4, 1.75, 1.28, 0.95};  // a CVT sweeps this range; five steps stand in for it
        utv.reverseRatio = 3.4;
        utv.finalDrive = 9.3;
        utv.shiftTime = 0.06;
        utv.engineName = "900CC TURBO I3";
        utv.gearboxName = "CVT (5 STEPS)";
        utv.lampStyle = LAMP_SLIM;
        utv.grilleStyle = GRILLE_INTAKE;
        // (The Maverick X3 X mr comes with a 4,500 lb winch from the factory.)
        utv.extras = EXTRA_TUBE_CAGE | EXTRA_SKID_PLATE | EXTRA_WINCH;
        utv.winchPull = 20000;
        utv.wheelStyle = WHEEL_OFFROAD_BLACK;
        utv.paintColor[0] = 0.10f, utv.paintColor[1] = 0.60f, utv.paintColor[2] = 0.85f;
        utv.coverColor[0] = 0.08f, utv.coverColor[1] = 0.08f, utv.coverColor[2] = 0.09f;
        utv.finish = VehicleSpec::PAINT;
        v.push_back(utv);
        // ---- Toyota Tacoma TRD Off-Road Double Cab 4x4, short bed (3rd gen, 2016-2023) ----
        // A mid-size pickup on a hybrid ladder frame (boxed up front, C-channel under the
        // bed): 3.5 L 2GR-FKS V6, 6-speed automatic, 3.909 axles, a 2.566:1 low range and
        // Toyota's rear e-locker. Double wishbones in front; behind, a solid 8.75 in axle on
        // multi-leaf springs: the leaves both hold the axle and spring it. P265/70R16s.
        // (Toyota pressroom, KBB, Edmunds tests; the leaf geometry, frame section and the
        // masses inside are estimates, marked where they're made.)
        VehicleSpec tacoma = runner;
        tacoma.name = "TOYOTA TACOMA";
        tacoma.id = "tacoma";
        tacoma.blurb = "2019 TRD OFF-ROAD DOUBLE CAB  3.5L V6  6-SPEED AUTO  4X4";
        // Nose and front wheels, the cab (four doors), then the bed over the rear wheels:
        // bed walls only, open above, the tailgate at the back.
        tacoma.profile = {
            "#WW#######WW##",
            "#########BBBBB",
            "....GGGGG.....",
        };
        // 5,392 x 1,890 x 1,795 mm, 3,236 mm wheelbase; bed floor ~0.95 m up, rails ~1.30.
        tacoma.layerY = {0.50, 0.97, 1.30, 1.795};
        tacoma.length = 5.392;
        tacoma.width = 1.890;
        tacoma.wheelbase = 3.236;
        tacoma.frontOverhang = 0.88;  // 32 degree approach angle
        tacoma.outerCellWidth = 0.35;  // the bed's walls and the wheel wells (1.19 m between the walls: 1.05 real between the wheel tubs, 1.44 at the top)
        tacoma.topLine = {1.07, 1.13, 1.17, 1.21};  // the hood rising to the cowl
        tacoma.bumperLift = 0.04;      // 32 degree approach: the air dam is low
        tacoma.bumperLiftRear = 0.10;  // 23.5 degree departure (the step bumper and hitch)
        tacoma.cornerRound = 0.07;
        tacoma.roofTuck = 0.08;
        tacoma.rakeFront = 0.55;  // a raked windshield
        tacoma.rakeRear = 0.04;   // upright back glass
        tacoma.liftgate = false;  // a tailgate
        tacoma.doors = 4;
        tacoma.doorLength = 1.05;
        tacoma.bodyWidth.clear();
        // 4,445-4,540 lb (2,016-2,059 kg) by the listings: ~2,040 kg, about 55/45 empty.
        // Cab and bed shells (the frame is its own, below). Spread over the lattice by height, the
        // shell put ~180 kg in the bed, half again what a composite bed weighs: 40 kg of it is the
        // cab's floor and firewall instead (the item below).
        tacoma.structureMass = 410;
        tacoma.panelMass = 165;
        tacoma.items = {
            {"radiator", 18, 2.30, 0.85, 0},                {"battery", 20, 2.05, 1.00, -0.55},
            {"front diff and IFS crossmember", 52, 1.82, 0.52, 0},
            {"front prop shaft", 9, 1.25, 0.52, 0.1},       {"two-piece rear prop shaft", 20, -0.20, 0.52, 0},
            {"dashboard and heater", 42, 0.95, 1.10, 0},    {"front seats", 48, 0.30, 0.90, 0},
            {"rear seat", 32, -0.45, 0.90, 0},              {"carpets, trim and wiring", 62, 0.25, 0.75, 0},
            {"fuel tank (80 L, full)", 70, -0.95, 0.55, -0.45}, {"exhaust", 26, -0.6, 0.40, 0.35},
            {"steering and brakes", 18, 1.8, 0.70, 0},      {"front skid plate", 14, 1.7, 0.42, 0},
            {"electronics", 15, 0.6, 0.95, 0},               {"front frame horns and shock towers", 70, 2.10, 0.74, 0},
            {"front bumper", 20, 2.62, 0.68, 0},             {"rear step bumper and hitch", 30, -2.62, 0.62, 0},
            {"spare wheel under the bed", 34, -2.25, 0.50, 0}, {"composite bed liner and floor", 25, -1.75, 0.98, 0},
            // (The cab is the dense steel; the bed is a light box.)
            {"cab floor, firewall and pillars", 140, 0.90, 0.75, 0}, {"core support and inner fenders", 65, 2.20, 0.85, 0},
            {"A/C, alternator and power steering", 28, 2.15, 0.85, 0.1},  // (on the block's front: they move with it)
        };
        tacoma.engineMass = 167;   // aluminum 2GR-FKS
        tacoma.gearboxMass = 110;  // 6-speed auto and transfer case
        // (Its block over the front axle, the diff under the sump, as on the real truck: 3rd-gen
        // Tacomas weigh ~56% on the front. Was 1.58, 24 cm behind the axle, and it came out 52%.)
        tacoma.engineX = 1.78;
        tacoma.engineY = 0.92;
        tacoma.engineZ = 0;
        tacoma.engineLen = 0.60;
        tacoma.engineWid = 0.66;
        tacoma.engineHgt = 0.62;
        tacoma.gearboxLen = 1.00;  // (AC60F ~0.70 m and the transfer case ~0.30: its tail barely moved with the engine, the rear shaft the length it was)
        tacoma.induction = VehicleSpec::NATURAL;
        tacoma.displacement = 3.5;
        tacoma.cylinders = 6;
        tacoma.turboLag = 0;
        tacoma.bodyStiffness = 1.15;
        tacoma.bodyStrength = 1.0;
        tacoma.frameStrength = 3.0;    // beefed up for offroad abuse
        tacoma.panelStrength = 1.4;
        tacoma.pinStrength = 1.8;
        // Hybrid frame: boxed ahead of the cab, C-channel under the bed, ~1/8 in wall,
        // ~2.75 in wide rails 37 in apart outside (2nd-gen measurement; ESTIMATE ~0.87 m
        // between centres), ~6.5 in deep (ESTIMATE). A 6.5 x 2.75 x 1/8 in C at ~50 ksi:
        // ~2.5 in^3 and ~10 in^4 per rail (boxed: ~3.7 in^3, ~12 in^4), so a press between
        // the axles (3.236 m apart, under the boxed cab section) sets it at ~35-50 kN and
        // bends it ~9 mm per 20 kN: the longest and softest frame here.
        tacoma.railSpacing = 0.87;
        tacoma.railHeight = 0.165;
        tacoma.railWidth = 0.07;
        tacoma.frameMass = 100;
        tacoma.frameStiffness = 0.05;
        tacoma.railYield = 0.0155;
        tacoma.bodyMounts = 10;    // cab and bed
        // Its 21 degree breakover (dealer spec sheets; the long wheelbase) puts the lowest thing
        // between the axles ~0.30 m up: the transfer case skid (ESTIMATE).
        tacoma.skidDrop = 0.19;
        tacoma.wheelRadius = 0.389;  // P265/70R16, 777 mm
        tacoma.wheelWidth = 0.265;
        tacoma.wellTireRadius = 0.40;  // fits ~32s before it rubs
        tacoma.wellTireWidth = 0.285;
        tacoma.trackFront = 1.600;
        tacoma.trackRear = 1.605;
        tacoma.wheelMass = 30;     // 42 lb Wrangler on a ~25 lb 16 in alloy
        tacoma.hubMass = 25;
        tacoma.axleMass = 100;     // 8.75 in rear with drums, and half the leaf packs
        tacoma.rimRadius = 0.203;
        tacoma.frontSusp = WISHBONE;
        tacoma.rearSusp = SOLID_AXLE;
        tacoma.leafSprings = true;
        tacoma.leafHalf = 0.70;    // 54-3/8 in eye to eye, the seat in the middle (aftermarket packs)
        tacoma.rideFreqFront = 1.30;
        tacoma.rideFreqRear = 1.55;  // springs for a payload: firm when it's empty
        tacoma.dampingRatio = 0.40;  // TRD Off-Road Bilsteins
        tacoma.arbFront = 13000;     // ~28-30 mm bar
        tacoma.arbRear = 0;
        // Wheel travel (ESTIMATE): ~20 cm front, ~24 cm at the rear axle.
        tacoma.bumpFront = 0.08, tacoma.droopFront = 0.115;
        tacoma.bumpRear = 0.10, tacoma.droopRear = 0.14;
        tacoma.swayDisconnect = 0;
        tacoma.tirePsi = 30;
        tacoma.wheelLoss = true;
        tacoma.spareFlat = true;   // full-size, winched up under the bed
        tacoma.lugShear = 110000;
        tacoma.tireType = TIRE_ALL_TERRAIN;
        tacoma.tireGrip = 0.95;
        tacoma.lateralGrip = 0.78;
        tacoma.drive = VehicleSpec::AWD;
        tacoma.diffFront = tacoma.diffRear = DIFF_OPEN;
        tacoma.transfer = XFER_PART_TIME_BASIC;  // 2H, 4H, 4L
        tacoma.lockerRear = true;          // electric rear locker (TRD Off-Road)
        tacoma.frontLockerLowOnly = false;
        tacoma.rearLockerLowOnly = true;   // Toyota's e-locker only engages in 4L
        tacoma.shaftLimit = 11000;         // beefed up for offroad abuse
        tacoma.lowRange = 2.566;
        tacoma.brakeFront = 3500;  // 319 mm vented discs
        tacoma.brakeRear = 1800;   // 10 in drums
        tacoma.dragArea = 1.30;    // Cd ~0.43 x ~3.0 m^2
        // 278 hp at 6,000 rpm, 359 N*m (265 lb-ft) at 4,600 (SAE J1349); the shape between
        // is an estimate (a flat 2GR curve).
        tacoma.torque = {{0, 200}, {1000, 262}, {1600, 292}, {2000, 305}, {3000, 336}, {4000, 355}, {4600, 359}, {5000, 355}, {5500, 342}, {6000, 330}, {6400, 305}};
        tacoma.idleRpm = 650;
        tacoma.redline = 6400;
        tacoma.gears = {3.600, 2.090, 1.490, 1.000, 0.690, 0.580};
        tacoma.reverseRatio = 3.73;
        tacoma.finalDrive = 3.909;
        tacoma.shiftTime = 0.35;
        tacoma.stallRpm = 2300;  // (estimate)
        tacoma.stallRatio = 2.0;
        tacoma.lockupGear = 3;
        tacoma.engineName = "3.5L V6 2GR-FKS";
        tacoma.gearboxName = "6-SPEED AUTO";
        tacoma.lampStyle = LAMP_RECT;
        tacoma.grilleStyle = GRILLE_WIDE;
        tacoma.extras = EXTRA_SKID_PLATE | EXTRA_FLARES;  // (the TRD Pro's black flare moldings: drawn only)
        tacoma.paintColor[0] = 0.50f, tacoma.paintColor[1] = 0.51f, tacoma.paintColor[2] = 0.49f;  // Cement
        tacoma.trimColor[0] = tacoma.trimColor[1] = tacoma.trimColor[2] = 0;
        tacoma.trimTop = 0;
        tacoma.wheelStyle = WHEEL_OFFROAD_BLACK;
        tacoma.coverColor[0] = 0.08f, tacoma.coverColor[1] = 0.08f, tacoma.coverColor[2] = 0.09f;
        v.push_back(tacoma);

        // ---- 1972 Chevrolet K5 Blazer 4x4 (first generation, 1969-72) ----------------------
        // The short, square two-door: a full-length box on a ladder frame with a removable
        // fiberglass/steel hardtop over the back, a 350 small-block V8, the SM465 four-speed
        // ("granny" first), an NP205 gear-drive transfer case, a Dana 44 front and a GM 12-bolt
        // rear, both on multi-leaf springs, the front one steering on kingpins. 104 in
        // wheelbase, 177.5 x 79 in (GM specs for 1969-72). 1972 SAE net ratings for the
        // two-barrel 350: 165 hp at 4,000 rpm, 280 lb-ft at 2,400. ~4,000 lb with the top on.
        VehicleSpec blazer = bronco;
        blazer.name = "CHEVY BLAZER";
        blazer.id = "blazer";
        blazer.blurb = "1972 K5 4X4  350 V8  SM465 4-SPEED  NP205";
        // Nose and front wheels, the cab (two long doors), the back under the hardtop.
        blazer.profile = {
            "#WW#####WW#",
            "###########",
            "...GGGGGGGG",
        };
        // 4,509 x 2,007 mm (with the mirrors folded: the body ~1,980), ~1,770 mm tall with the top.
        blazer.layerY = {0.53, 0.99, 1.32, 1.80};  // (the 4x4's floor sits high over the frame and the leaf packs)
        blazer.length = 4.509;
        blazer.width = 1.98;
        blazer.wheelbase = 2.642;  // 104 in
        blazer.frontOverhang = 0.80;
        blazer.outerCellWidth = 0;
        blazer.topLine = {1.18, 1.25, 1.30};  // a long, flat hood, nearly level to the cowl
        blazer.bumperLift = 0.10;
        blazer.bumperLiftRear = 0.10;
        blazer.cornerRound = 0.04;  // a box
        blazer.roofTuck = 0.06;
        blazer.rakeFront = 0.26;    // a near-vertical windshield
        blazer.rakeRear = 0.02;     // the tailgate glass, upright
        blazer.liftgate = false;    // a drop-down tailgate with a roll-down window
        blazer.doors = 2;
        blazer.doorLength = 1.25;
        blazer.bodyWidth.clear();
        // ~4,000 lb (1,815 kg) with the hardtop and a full tank, about 55/45 (ESTIMATE from
        // period road tests: the iron V8 and the front axle over the front wheels).
        blazer.structureMass = 380;  // the steel body tub (the frame is its own, below)
        blazer.panelMass = 150;      // thick 1970s sheet metal
        blazer.items = {
            {"radiator and core support", 30, 2.02, 0.88, 0},   {"battery", 25, 1.55, 1.00, -0.55},
            {"front bumper (chrome)", 30, 2.20, 0.62, 0},       {"steering box and drag link", 22, 1.75, 0.70, -0.45},
            {"front prop shaft", 10, 0.90, 0.52, 0.1},          {"rear prop shaft", 15, -0.45, 0.52, 0},
            {"dashboard and heater", 32, 0.80, 1.10, 0},        {"front bucket seats", 44, 0.05, 0.98, 0},
            {"rear bench seat", 32, -0.95, 0.96, 0},            {"floor mats, trim, wiring", 30, -0.10, 0.70, 0},
            {"fuel tank (117 L, full)", 85, -1.20, 0.58, 0.40}, {"exhaust", 28, -0.30, 0.40, 0.35},
            {"spare wheel on the swing-away carrier", 38, -2.42, 1.11, 0},  // (a big tire doesn't fit inside, on the side wall where the factory put it)
            {"rear bumper (chrome)", 22, -2.22, 0.62, 0},        {"hardtop", 45, -0.70, 1.72, 0},
            {"brakes", 22, 0.0, 0.50, 0},                       {"tailgate", 25, -2.20, 0.95, 0},
        };
        blazer.engineMass = 240;   // cast-iron small block with its accessories
        blazer.gearboxMass = 110;  // SM465 (~70 kg, cast iron) and the NP205 (~40 kg)
        blazer.transverse = false;
        // (The small block's front over the front axle and its water pump just behind the radiator, as
        // on the real K5: 12 cm forward. Was 1.35, the radiator 20 cm further back than it's drawn.)
        blazer.engineX = 1.47;
        blazer.engineY = 0.97;  // (moved forward under the falling hood line, it sits 5 cm lower: the air cleaner under the hood)
        blazer.engineZ = 0;
        blazer.engineLen = 0.68;
        blazer.engineWid = 0.66;
        blazer.engineHgt = 0.64;
        blazer.gearboxLen = 0.93;  // (SM465 ~0.6 m and the NP205)
        blazer.induction = VehicleSpec::NATURAL;
        blazer.turboLag = 0;
        blazer.displacement = 5.7;
        blazer.cylinders = 8;
        blazer.bodyStiffness = 0.95;   // an open-backed tub (the top is bolted on)
        blazer.bodyStrength = 0.85;    // mild steel, thick
        blazer.frameStrength = 2.6;
        blazer.noseStrength = 1.5;     // the grille, core support and fenders boxed on the frame horns
        blazer.panelStrength = 1.5;
        blazer.pinStrength = 1.8;
        // Ladder frame: C-channel rails, riveted crossmembers (ESTIMATE: ~7 x 2.5 in, 3/16 in).
        blazer.railSpacing = 0.86;
        blazer.railHeight = 0.18;
        blazer.railWidth = 0.065;
        blazer.frameMass = 170;
        blazer.frameStiffness = 0.12;  // an open C: it twists
        blazer.railYield = 0.010;
        blazer.bodyMounts = 8;
        blazer.skidDrop = 0.20;        // the NP205 and its crossmember hang low
        // 10-15LT on 15 x 7 steel wheels: ~30.5 x 10 in (ESTIMATE between the G78-15 base tire
        // and the optional 10-15 the off-road package listed).
        blazer.wheelRadius = 0.385;
        blazer.wheelWidth = 0.255;
        blazer.wellTireRadius = 0.42;  // ~33s before they rub
        blazer.wellTireWidth = 0.29;
        blazer.rimRadius = 0.191;      // 15 in
        blazer.trackFront = 1.638;     // 64.5 in
        blazer.trackRear = 1.600;      // 63 in
        blazer.wheelMass = 27;
        blazer.hubMass = 25;           // locking hubs and the brake
        blazer.axleMass = 110;         // Dana 44 front / 12-bolt rear, and half the leaf packs
        blazer.frontSusp = SOLID_AXLE;
        blazer.rearSusp = SOLID_AXLE;
        blazer.leafFront = true;       // multi-leaf packs at both ends, the front axle steering
        blazer.leafSprings = true;
        blazer.leafHalfFront = 0.58;   // 46 in front springs (ESTIMATE)
        blazer.leafHalf = 0.70;        // 52 in rear springs (ESTIMATE)
        blazer.linkLength = 0.70;
        blazer.upperLink = 0;
        blazer.rideFreqFront = 1.45;   // stiff front leaves: the V8 sits on them
        blazer.rideFreqRear = 1.45;
        blazer.dampingRatio = 0.30;    // 1970s shocks
        blazer.arbFront = 6000;        // a thin front bar
        blazer.arbRear = 0;
        // Wheel travel (ESTIMATE): leaf packs, ~20 cm front, ~22 cm rear.
        blazer.bumpFront = 0.08, blazer.droopFront = 0.11;
        blazer.bumpRear = 0.09, blazer.droopRear = 0.13;
        blazer.swayDisconnect = 0;
        blazer.tirePsi = 32;
        blazer.wheelLoss = true;
        blazer.spareFlat = false;
        blazer.lugShear = 105000;      // eight 1/2 in studs
        blazer.tireType = TIRE_ALL_TERRAIN;
        blazer.tireGrip = 0.90;        // bias-ply-era tread
        blazer.lateralGrip = 0.74;     // ~0.63 g on the skidpad: 1970s light-truck tires
        blazer.drive = VehicleSpec::AWD;
        blazer.diffFront = blazer.diffRear = DIFF_OPEN;
        blazer.transfer = XFER_PART_TIME_BASIC;  // NP205: 2H, 4H, 4L (locking hubs in front)
        blazer.lockerRear = false;
        blazer.lockerFront = false;
        blazer.frontLockerLowOnly = false;
        blazer.rearLockerLowOnly = false;
        blazer.shaftLimit = 9500;      // Dana 44 front shafts and the 12-bolt's C-clip rears
        blazer.lowRange = 1.96;        // NP205
        blazer.brakeFront = 3300;      // 11.86 in discs (power front discs from 1971)
        blazer.brakeRear = 1300;       // 11 in drums (behind a proportioning valve: the fronts lock first)
        blazer.absEnabled = false;
        blazer.tractionFitted = false;
        blazer.dragArea = 1.75;        // Cd ~0.55 x 3.2 m^2 (a brick)
        // 165 hp at 4,000 rpm, 280 lb-ft (380 N*m) at 2,400 (SAE net, 1972 2-bbl 350); the curve
        // between is the small block's (estimate).
        blazer.torque = {{0, 250}, {800, 300}, {1200, 335}, {1800, 365}, {2400, 380}, {3000, 370}, {3600, 345}, {4000, 320}, {4400, 285}, {4800, 240}};
        blazer.idleRpm = 650;
        blazer.redline = 4800;
        blazer.gears = {6.55, 3.58, 1.70, 1.00};  // SM465
        blazer.reverseRatio = 6.09;
        blazer.finalDrive = 3.73;
        blazer.shiftTime = 0.45;       // a long-throw truck box, double-clutched if you like
        blazer.stallRpm = 0;           // a clutch, not a converter
        blazer.lockupGear = 0;
        blazer.engineName = "350 V8 2-BARREL";
        blazer.gearboxName = "SM465 4-SPEED MANUAL";
        blazer.lampStyle = LAMP_ROUND;
        blazer.grilleStyle = GRILLE_WIDE;
        blazer.extras = EXTRA_SPARE;  // the spare on a swing-away carrier behind the tailgate
        // Ochre with the white hardtop, the classic two-tone.
        blazer.paintColor[0] = 0.66f, blazer.paintColor[1] = 0.44f, blazer.paintColor[2] = 0.12f;
        blazer.trimColor[0] = 0.93f, blazer.trimColor[1] = 0.93f, blazer.trimColor[2] = 0.90f;  // the white hardtop (its shell paints it)
        blazer.trimTop = 0;
        blazer.wheelStyle = WHEEL_STEEL;
        blazer.coverColor[0] = 0.92f, blazer.coverColor[1] = 0.92f, blazer.coverColor[2] = 0.90f;  // the white top
        v.push_back(blazer);

        // EF Civic Si: the Rio Red hatch in bringatrailer.com/listing/1990-honda-civic-si-5/.
        // Honda's D16A6 rating: 108 hp / 100 lb-ft; 185/60R14 tires, discs/drums,
        // double wishbones and a five-speed. Internal mass placement and CG are estimates.
        VehicleSpec& ef = v[0];
        ef.blurb = "1990 EF HATCH  1.6L D16A6  5-SPEED MANUAL  FWD";
        ef.profile = {"#WW######WW#", "############", "...GGGGGGGGG"};
        ef.layerY = {0.19, 0.53, 0.89, 1.335};
        ef.length = 3.965;
        ef.width = 1.666;
        ef.wheelbase = 2.500;
        ef.frontOverhang = 0.78;
        ef.topLine = {0.64, 0.73, 0.83};
        ef.rakeFront = 0.55;
        ef.rakeRear = 0.35;
        ef.roofTuck = 0.11;
        ef.cornerRound = 0.055;
        ef.doors = 2;
        ef.doorLength = 1.2;
        ef.liftgate = true;
        ef.structureMass = 315;
        ef.panelMass = 120;
        ef.items = {
            {"radiator", 12, 1.68, 0.4, 0}, {"battery", 12, 1.32, 0.62, -0.4},
            {"front subframe and rack", 30, 1.15, 0.29, 0}, {"dashboard and heater", 30, 0.55, 0.68, 0},
            {"front seats", 32, 0.05, 0.38, 0}, {"rear seat", 18, -0.8, 0.38, 0},
            {"interior, wiring and glass hardware", 50, -0.1, 0.47, 0},
            {"fuel tank (45 L)", 39, -0.8, 0.29, 0}, {"exhaust", 15, -0.4, 0.20, 0.22},
            {"rear suspension crossmember", 25, -1.27, 0.3, 0}, {"front bumper and lamps", 20, 1.8, 0.40, 0},
            {"rear bumper", 12, -1.83, 0.4, 0}, {"air conditioning", 16, 1.3, 0.48, 0.25},
            {"spare wheel and tools", 15, -1.55, 0.26, 0}, {"strut towers and front rails", 24, 1.4, 0.5, 0},
        };
        ef.engineMass = 100;
        ef.gearboxMass = 35;
        ef.engineX = 1.42;
        ef.engineY = 0.48;
        ef.engineLen = 0.49;
        ef.engineWid = 0.41;
        ef.engineHgt = 0.48;
        ef.bodyStiffness = 0.87;
        ef.bodyStrength = 0.67;  // 1990 unibody: thinner, mild steel; it folds up much more than a modern car
        ef.frameStiffness = 0.55;
        ef.wheelRadius = 0.289; // 14 in rim + 185 mm * 0.60 sidewall
        ef.rimRadius = 0.1778;
        ef.wheelWidth = 0.185;
        ef.trackFront = 1.450;
        ef.trackRear = 1.455;
        ef.wheelMass = 13;
        ef.hubMass = 11;
        ef.frontSusp = ef.rearSusp = WISHBONE;
        ef.rideFreqFront = 1.45;
        ef.rideFreqRear = 1.6;
        ef.arbFront = 10000;
        ef.arbRear = 4000;
        ef.tireGrip = 1.04;
        ef.lateralGrip = 0.87;
        ef.diffFront = DIFF_OPEN;
        ef.brakeFront = 1550;
        ef.brakeRear = 750;
        ef.absEnabled = false;
        ef.tractionFitted = false;  // no traction control on a 1990 Civic either
        ef.dragArea = 0.61;
        ef.torque = {{0, 65}, {1000, 91}, {2000, 113}, {3000, 127}, {4000, 135.6}, {5000, 134}, {6000, 128.2}, {6500, 112}};
        ef.idleRpm = 750;
        ef.redline = 6500;
        ef.turboLag = 0;
        ef.induction = VehicleSpec::NATURAL;
        ef.displacement = 1.6;
        ef.gears = {3.250, 1.894, 1.259, 0.937, 0.771};
        ef.reverseRatio = 3.153;
        ef.finalDrive = 4.250;
        ef.engineName = "1.6L SOHC D16A6";
        ef.gearboxName = "5-SPEED MANUAL";
        ef.paintColor[0] = 0.72f, ef.paintColor[1] = 0.045f, ef.paintColor[2] = 0.035f;
        ef.lampStyle = LAMP_RECT;
        ef.grilleStyle = GRILLE_CENTER;
        ef.wheelStyle = WHEEL_HONDA_COVER;
        ef.extras = EXTRA_LIP_SPOILER;

        // ---- VW Baja Bug (a 1969 Type 1 Beetle built for the desert) ----------------------------
        // The classic air-cooled Beetle cut down for Baja: the fenders bobbed front and rear, a
        // short fiberglass nose, the rear apron and engine lid gone so the motor sits out in the
        // air under a tube exo-cage, the front torsion beam's trailing arms and the rear IRS
        // trailing arms lifted and on long-travel shocks, 30 in all-terrains on 15 in wheels, a
        // tube push bar and a light bar. Platform chassis with the body bolted on (unibody here),
        // 2,400 mm (94.5 in) wheelbase; stock 1969 body 4,031 x 1,549 mm, tracks 1,311 / 1,354 mm
        // (carsales.com.au spec listing); the Baja's cut nose and tail and wider wheels are
        // estimates. Engine: a built 2.0 L Type 1 (2007 cc, dual carbs; ~115 hp at 5,200, ~160 N*m
        // at 3,800, typical builder figures, estimate). Type 1 four-speed: 3.80 / 2.06 / 1.26 /
        // 0.93, 4.375 final (VW). ~870 kg with the cage and a tank of fuel (stock Beetle ~820 kg;
        // estimate), about 40/60 with the engine hung out behind the rear axle. (The trailing
        // arms at both ends are drawn and built as short-long arms: the same wheel paths for
        // the travel the game uses.)
        VehicleSpec baja = v[0];
        baja.name = "VW BAJA BUG";
        baja.id = "baja";
        baja.blurb = "1969 BEETLE  2.0L AIR-COOLED FLAT-4  4-SPEED  RWD  LONG TRAVEL";
        // Nose (the cut fiberglass hood), front wheels, the cabin under the round roof, rear
        // wheels, the bare engine out the back.
        baja.profile = {
            "#WW#####WW#",
            "###########",
            "...GGGGG...",
        };
        baja.layerY = {0.42, 0.78, 1.05, 1.60};  // (lifted: ~0.36 m under the pan)
        baja.length = 3.85;
        baja.width = 1.60;
        baja.widthCells = 4;
        baja.wheelbase = 2.40;
        baja.frontOverhang = 0.62;
        // The short hood falling to the push bar; behind the cabin, the stub of the deck over
        // the engine, cut off low (the engine stands up out of it).
        baja.topLine = {0.80, 0.93, 1.01, 0, 0, 0, 0, 0, 0, 0.94, 0.80, 0.72};
        baja.bodyWidth = {0.74, 0.94, 1.0, 0.95, 0.92, 0.92, 0.92, 0.93, 0.97, 1.0, 0.92, 0.82};
        baja.outerCellWidth = 0;
        baja.noseDrop = baja.tailDrop = 0;
        baja.bumperLift = 0.10;
        baja.bumperLiftRear = 0.16;
        baja.cornerRound = 0.12;
        baja.roofTuck = 0.10;   // the Beetle's strong tumblehome (the drawn shell leans in more)
        baja.rakeFront = 0.38;
        baja.rakeRear = 0.78;   // the long, round back of the roof
        baja.doors = 2;
        baja.doorLength = 1.05;
        baja.liftgate = false;
        baja.structureMass = 185;  // the platform pan, its backbone tunnel and the steel body tub (the rear torsion housing is an item)
        baja.panelMass = 70;       // doors (steel), fiberglass nose and the cut fenders, glass
        baja.items = {
            {"tube push bar", 9, 1.88, 0.58, 0},              {"fuel cell behind the seats (40 L, full)", 38, -0.45, 0.62, 0},
            {"spare wheel strapped in where the back seat was", 22, -0.65, 0.80, 0},    {"front beam and torsion bars", 22, 1.30, 0.50, 0},
            {"steering box", 6, 1.10, 0.68, -0.30},            {"dashboard", 8, 0.85, 0.95, 0},
            {"race buckets and belts", 24, 0.15, 0.60, 0},     {"tools and recovery straps", 10, -0.50, 0.58, 0},
            {"roll cage", 45, -0.15, 1.20, 0},                 {"battery", 15, -1.0, 0.60, 0.35},
            {"carpets, trim and wiring", 20, 0.0, 0.50, 0},    {"rear exo-cage and bumper", 18, -1.80, 0.85, 0},
            {"stinger exhaust", 10, -1.75, 0.75, 0},           {"oil cooler", 6, -1.45, 1.00, 0.25},
            {"light bar", 8, 0.20, 1.68, 0},                   {"brakes", 16, 0.0, 0.45, 0},
            {"rear torsion housing and spring plates", 40, -1.10, 0.45, 0},
        };
        baja.engineMass = 125;   // a built Type 1 with its fan shroud, tin, heater boxes and dual carbs
        baja.gearboxMass = 40;   // the transaxle
        baja.transverse = false;
        baja.midEngine = true;   // (a transaxle: the differential is in the gearbox)
        baja.rearEngine = true;
        baja.airCooled = true;
        baja.engineX = -1.60;
        baja.engineY = 0.72;
        baja.engineZ = 0;
        baja.engineLen = 0.44;
        baja.engineWid = 0.78;   // a flat four is wide and short
        baja.engineHgt = 0.56;
        baja.gearboxLen = 0.40;
        baja.induction = VehicleSpec::NATURAL;
        baja.turboLag = 0;
        baja.cylinders = 4;
        baja.flatPlane = false;
        baja.displacement = 2.0;
        baja.bodyStiffness = 1.25;    // the cage ties the tub together
        baja.bodyStrength = 1.0;      // 1969 sheet steel, the cage and the seam welding a Baja gets
        baja.frameStrength = 2.0;     // plated shock towers and beefed trailing arms, as a desert Bug gets
        baja.noseStrength = 0.7;      // a fiberglass nose ahead of the beam: it crushes
        baja.panelStrength = 0.6;
        baja.pinStrength = 0.8;
        baja.wheelRadius = 0.381;     // 30x9.50R15
        baja.wheelWidth = 0.24;
        baja.wheelWidthRear = 0;
        baja.wellTireRadius = 0.42;   // the bobbed fenders clear ~33s
        baja.wellTireWidth = 0.29;
        baja.rimRadius = 0.1905;
        baja.trackFront = 1.40;       // (stock 1,311 / 1,354 mm; wider-offset wheels)
        baja.trackRear = 1.40;
        baja.wheelMass = 22;
        baja.hubMass = 14;
        baja.frontSusp = WISHBONE;    // torsion-beam trailing arms (see above)
        baja.rearSusp = WISHBONE;     // IRS trailing arms on long-travel shocks
        // (Long-travel arms, as the kits use. On the stock-length 34 / 28 cm ones the tire was
        // dragged ~6 m/s sideways across the ground at full bump, and that scrub jacked a front
        // corner onto its rim landing the kicker level: a pinched tire at 76 km/h.)
        baja.armLower = 0.46, baja.armUpper = 0.38;
        baja.rideFreqFront = 1.30;
        baja.rideFreqRear = 1.40;
        baja.dampingRatio = 0.40;     // race shocks, two per wheel at the back
        baja.arbFront = 3500;
        baja.arbRear = 0;
        // Wheel travel (ESTIMATE, a lifted desert Bug): ~21 cm front, ~28 cm rear.
        baja.bumpFront = 0.09, baja.droopFront = 0.12;
        baja.bumpRear = 0.12, baja.droopRear = 0.16;
        baja.swayDisconnect = 0;
        baja.heaveRate = baja.heaveGap = baja.heaveDamping = 0;
        baja.tirePsi = 22;
        baja.tireType = TIRE_ALL_TERRAIN;   // BFG-style all-terrains
        baja.stockTireType = TIRE_ALL_TERRAIN, baja.stockTireRatingKmh = 180;  // (S-rated 30x9.50R15s: flat out it's ~176 km/h)
        baja.tireGrip = 0.95;
        baja.lateralGrip = 0.80;
        baja.looseGrip = 1.05;    // light, on wide aired-down tires: it floats on sand and bites in dirt
        baja.drive = VehicleSpec::RWD;
        baja.diffFront = DIFF_OPEN;
        baja.diffRear = DIFF_OPEN;  // the stock Type 1 differential (the engine's weight is the traction aid)
        baja.transfer = XFER_NONE;
        baja.lockerFront = baja.lockerRear = false;
        baja.shaftLimit = 7000;     // 930-size CV axles (estimate)
        baja.lowRange = 0;
        baja.brakeFront = 1500;     // drums all round
        baja.brakeRear = 1150;
        baja.absEnabled = false;
        baja.tractionFitted = false;
        baja.dragArea = 1.10;       // Cd ~0.55 x 2.0 m^2 with the cage, the light bar, the spare on the roof and the big bare tires
        baja.downforce = baja.downforceMax = 0;
        // 2.0 L Type 1 build (estimate): ~115 hp at 5,200, ~160 N*m at 3,800.
        baja.torque = {{0, 95}, {1000, 112}, {2000, 135}, {3000, 152}, {3800, 160}, {4500, 157}, {5200, 150}, {5800, 132}, {6200, 112}};
        baja.idleRpm = 900;
        baja.redline = 5800;
        baja.gears = {3.80, 2.06, 1.26, 0.93};
        baja.reverseRatio = 3.88;
        baja.finalDrive = 4.375;
        baja.shiftTime = 0.32;
        baja.stallRpm = 0;
        baja.lockupGear = 0;
        baja.engineName = "2.0L AIR-COOLED FLAT-4";
        baja.gearboxName = "TYPE 1 4-SPEED TRANSAXLE";
        baja.lampStyle = LAMP_ROUND;
        baja.grilleStyle = GRILLE_CENTER;
        baja.extras = EXTRA_BAJA | EXTRA_SKID_PLATE;
        baja.wheelStyle = WHEEL_OFFROAD_BLACK;
        baja.paintColor[0] = 0.92f, baja.paintColor[1] = 0.70f, baja.paintColor[2] = 0.12f;  // Saturn yellow
        baja.trimColor[0] = baja.trimColor[1] = baja.trimColor[2] = 0;
        baja.trimTop = 0;
        baja.coverColor[0] = 0.06f, baja.coverColor[1] = 0.06f, baja.coverColor[2] = 0.065f;
        v.push_back(baja);

        // Each engine's sound (engineaudio.h): the exhaust pipe's note (Hz), how open it is, cam,
        // intake, valvetrain, timbre, pops, the burble of big uneven pulses, and the roar under load.
        auto voice = [&](const char* id, float pipeHz, float muffler, float lope, float intake, float mech, float rasp, float crackle, float rumble, float roar) {
            for (VehicleSpec& s : v)
                if (std::string(s.id) == id) s.voice = EngineTone{pipeHz, muffler, lope, intake, mech, rasp, crackle, 0, rumble, roar};
        };
        voice("civic", 260, 0.30f, 0.0f, 0.55f, 0.35f, 0.60f, 0.0f, 0.0f, 0.20f);    // a little SOHC four: buzzy, intake-y, quiet pipe
        voice("jesko", 340, 0.75f, 0.0f, 0.50f, 0.20f, 0.70f, 0.35f, 0.0f, 0.60f);   // flat-plane V8 through a short race exhaust: a howl
        voice("bronco", 150, 0.30f, 0.0f, 0.30f, 0.20f, 0.35f, 0.0f, 0.05f, 0.30f);  // twin-turbo V6: muted, turbo hiss, a modern box
        voice("4runner", 165, 0.40f, 0.0f, 0.45f, 0.35f, 0.40f, 0.0f, 0.05f, 0.30f); // 5VZ V6 with the blower whine
        voice("buggy", 95, 0.92f, 0.35f, 0.55f, 0.30f, 0.30f, 0.50f, 0.60f, 0.85f);  // LS V8 out of short open headers: loud, lumpy, poppy
        voice("utv", 300, 0.62f, 0.0f, 0.60f, 0.55f, 0.85f, 0.30f, 0.15f, 0.50f);    // Rotax turbo triple: raspy, clattery, braps
        voice("tacoma", 175, 0.25f, 0.0f, 0.35f, 0.25f, 0.40f, 0.0f, 0.0f, 0.20f);   // 2GR V6: smooth and quiet
        voice("blazer", 105, 0.38f, 0.15f, 0.60f, 0.40f, 0.12f, 0.15f, 0.55f, 0.55f); // carbureted small block on dual exhaust: deep, a little lope, a rumble
        voice("baja", 215, 0.88f, 0.10f, 0.65f, 0.70f, 0.80f, 0.30f, 0.12f, 0.65f);   // air-cooled flat four out a stinger: loud, raspy, valvetrain and fan clatter, dual-carb honk
        for (size_t i = 0; i < v.size(); i++) v[i].model = (int)i;
        // Engines tuned ~15% over their factory ratings: rated torque is the crank's, and
        // with the drivetrain's spinning mass counted the stock figures felt flat in the game.
        for (VehicleSpec& s : v)
            for (TorquePoint& tp : s.torque) tp.nm *= 1.15;
        // Tanks (and the trucks' spare cans).
        v[0].fuelCapacity = 45;                         // EF Civic
        v[1].fuelCapacity = 72;                         // Jesko
        v[2].fuelCapacity = 79, v[2].jerryCan = 20;     // Bronco: 20.8 gal
        v[3].fuelCapacity = 70, v[3].jerryCan = 20;     // 4Runner: 18.5 gal
        v[4].fuelCapacity = 95;                         // rock buggy: a 25 gal fuel cell
        v[5].fuelCapacity = 40;                         // UTV: 10.6 gal (Maverick X3)
        v[6].fuelCapacity = 80, v[6].jerryCan = 20;     // Tacoma: 21.1 gal
        v[7].fuelCapacity = 117;                        // Blazer: 31 gal
        v[8].fuelCapacity = 40, v[8].jerryCan = 10;     // Baja Bug: the Beetle's 10.6 gal tank in the nose
        // A set of snow chains in the back of each off-roader (a pair per axle).
        for (const char* id : {"bronco", "4runner", "buggy", "utv", "tacoma", "blazer", "baja"})
            for (VehicleSpec& s : v)
                if (std::string(s.id) == id) s.snowChains = 4;
        return v;
    }();
    return catalog;
}

namespace {
VehicleSpec propBase() {
    VehicleSpec p = vehicleCatalog()[0];
    p.widthCells = 1;
    p.noseDrop = p.tailDrop = p.bumperLift = p.cornerRound = p.roofTuck = 0;
    p.rakeFront = p.rakeRear = 0;
    p.engineMass = p.gearboxMass = 0;
    p.wheelbase = p.frontOverhang = 0;
    p.topLine.clear();
    p.bodyWidth.clear();
    p.doors = 0;
    p.downforce = 0;
    p.extras = 0;
    p.items.clear();
    p.panelMass = 0;
    p.finish = VehicleSpec::MATTE;
    return p;
}
}  // namespace

const VehicleSpec& crashBoxSpec() {
    static const VehicleSpec spec = [] {
        VehicleSpec p = propBase();
        p.name = "CRASH BOX";
        p.profile = {"#"};
        p.layerY = {0.0, 1.0};
        p.length = p.width = 1.0;
        p.structureMass = 40;
        p.bodyStiffness = 0.08;  // soft foam: dents and splits easily
        p.contactScale = 0.12;
        return p;
    }();
    return spec;
}

const VehicleSpec& barrierSpec() {
    static const VehicleSpec spec = [] {
        VehicleSpec p = propBase();
        p.name = "BARRIER";
        p.profile = {"##"};
        p.layerY = {0.0, 0.8};
        p.length = 2.0;
        p.width = 0.6;
        p.structureMass = 1200;
        p.bodyStiffness = 6.0;
        p.finish = VehicleSpec::CONCRETE;
        return p;
    }();
    return spec;
}
