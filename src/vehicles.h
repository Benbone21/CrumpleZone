// Vehicle definitions. Each vehicle's body is described by a side-profile map
// that the soft-body builder turns into a node/beam lattice.
#pragma once
#include <string>
#include <vector>

struct TorquePoint {
    double rpm, nm;
};

// A lump of mass that rides on the body (seats, fuel tank, battery...). Position in
// the car's frame: x forward from the middle, y up from the ground, z to the right.
struct MassItem {
    const char* name;
    double kg, x, y, z;
};

enum SuspensionType {
    WISHBONE,    // double wishbone: upper and lower A-arms, coil-over, tie rod or toe link
    TWIST_BEAM,  // trailing arms joined by a torsion beam (light front-drive cars)
    SOLID_AXLE,  // one rigid axle housing on trailing links and a panhard rod (trucks, vans)
    STRUT,       // MacPherson strut: lower A-arm, the knuckle slides on a strut up to the tower
};

enum DiffType {
    DIFF_OPEN,    // equal torque both sides: a wheel in the air spins and the other gets little
    DIFF_LSD,     // limited slip: sends torque to the slower wheel, up to a bias ratio
    DIFF_LOCKED,  // both wheels turn together (off-road lockers)
};

// Transfer case: how a 4x4 drives its front axle.
enum TransferCase {
    XFER_NONE,       // the drive layout is fixed
    XFER_PART_TIME,  // 2H; 4A (front engaged, center free); 4H and 4L tie the front and rear shafts together
    XFER_FULL_TIME,  // center differential: 2H; 4H (center open); 4H LOCK and 4L LOCK
    XFER_SELECT,     // plain two-speed lever (rock buggy): 2H; 4H and 4L tie the front and rear shafts together
    XFER_ONDEMAND,   // side-by-side: 2WD; 4WD (the front axle comes in through an open center, no low range)
    XFER_PART_TIME_BASIC,  // plain part-time: 2H (where it's driven day to day), 4H and 4L shafts locked
};

// One position of the transfer-case lever.
// Tire tread: how it gets on with each surface (on top of the spec's pavement grip).
enum TireType { TIRE_STREET, TIRE_SEMI_SLICK, TIRE_ALL_TERRAIN, TIRE_MUD_TERRAIN,
                TIRE_RACE_SLICK,  // no tread at all: the most rubber on dry pavement, nothing to bite with off it
                TIRE_SNOW,        // winter tire: soft compound, siped blocks: grips snow and ice
                TIRE_CHAINS,      // snow chains over the tire: they cut into ice, snow and mud, skate on pavement
                TIRE_TYPES };
const char* tireTypeName(int t);

struct DriveMode {
    const char* name;
    bool front;       // front axle driven
    bool centerLock;  // front and rear prop shafts turn together
    bool low;         // low range
};
const std::vector<DriveMode>& driveModes(TransferCase t);  // empty for XFER_NONE
int defaultDriveMode(TransferCase t);                      // what the truck leaves the factory in

// Bolt-on visual extras (bit flags).
enum BodyExtra : unsigned {
    EXTRA_WING = 1,         // big rear wing on pylons
    EXTRA_SPARE = 2,        // spare wheel on the tailgate
    EXTRA_ROOF_RACK = 4,
    EXTRA_HOOD_SCOOP = 8,
    EXTRA_LIP_SPOILER = 16, // small spoiler on the trunk lid
    EXTRA_FLARES = 32,      // black fender flares
    EXTRA_ROOF_SCOOP = 64,  // engine air intake on the roof (mid-engine)
    EXTRA_SKID_PLATE = 128,
    EXTRA_ARMOR = 256,        // steel bumpers on the frame: winch up front, swing-out tire carrier behind
    EXTRA_SNORKEL = 512,      // raised air intake up the right A-pillar
    EXTRA_ROOF_BASKET = 1024, // full-length roof basket with a light bar
    EXTRA_SLIDERS = 2048,     // steel rock sliders along the sills
    EXTRA_WINCH = 4096,       // electric winch on the front bumper (V hooks it, Z reels in)
    EXTRA_TUBE_CAGE = 8192,   // tube-frame vehicles: the roll cage, tube chassis and bumpers are drawn from tubes (carvisual.cpp)
};

// Wheel and tire look.
enum WheelStyle { WHEEL_ALLOY = 0, WHEEL_OFFROAD_BRONZE = 1, WHEEL_OFFROAD_BLACK = 2, WHEEL_HONDA_COVER = 3, WHEEL_FORGED = 4,
                  WHEEL_STEEL = 5,  // 1970s painted steel wheel, chrome dog-dish cap, on a tall tire
                  WHEEL_STYLES };

// Headlamp and grille shapes (body shader).
enum LampStyle { LAMP_RECT = 0, LAMP_ROUND = 1, LAMP_SLIM = 2 };
enum GrilleStyle { GRILLE_CENTER = 0, GRILLE_WIDE = 1, GRILLE_INTAKE = 2 };

struct VehicleSpec {
    const char* name;        // shown in the HUD
    const char* id = "";     // short lowercase name (test names, save file)
    const char* blurb = "";  // one line about the real car this models
    // Side profile: one string per cell layer (bottom layer first), one character
    // per cell from the front bumper back. Every layer must be the same length.
    //   '#' solid body          'G' cabin (glass sides, painted roof)
    //   'W' wheel well (solid only in the inner columns; a run of W's is an axle)
    //   'B' bed wall (solid only in the outer columns)      '.' empty
    std::vector<std::string> profile;
    std::vector<double> layerY;  // node layer heights, profile.size() + 1 entries
    double length, width;
    int widthCells;
    // Real wheelbase and front overhang (m). The lattice columns are stretched so the
    // axles land exactly there; 0 = evenly spaced columns.
    double wheelbase = 0, frontOverhang = 0;
    // Height of the top surface at each column of nodes (front first, profile[0].size() + 1
    // entries; 0 = leave it at its layer). Shapes hoods, decks and engine covers.
    std::vector<double> topLine;
    std::vector<double> bodyWidth;  // half-width multiplier per longitudinal node column
    double outerCellWidth = 0;      // width of the outermost cell column each side (m; 0 = even): a
                                    // pickup's bed walls and the fenders over its wheels
    int doors = 0;                 // 0 = infer from cabin; 2/4 = explicit body style
    // Tube-frame vehicle (rock buggy, side-by-side): the lattice is the chassis and cage and
    // is not drawn; only the cells marked 'p' (hood, tail and body panels) carry a skin.
    // It has no doors, and its panels are plain bolt-ons.
    bool openFrame = false;
    double doorLength = 1.0;        // physical front door length (m)
    double rimRadius = 0;           // metal rim radius; 0 = inferred from wheel style
    bool absEnabled = true;
    bool tractionFitted = true;  // has traction control at all (the driver can still switch it off)
    int cylinders = 4;
    bool flatPlane = false;        // flat-plane crank V8: even firing, a high-pitched howl
    enum Induction { NATURAL, TURBO, SUPERCHARGER } induction = TURBO;
    double displacement = 1.5;     // liters, also sets the procedural engine voice

    // Shaping applied to the lattice.
    double noseDrop, tailDrop;     // lower the top of the nose / tail
    double bumperLift;             // raise the bottom corners (approach angle)
    double bumperLiftRear = -1;    // ...at the tail (departure angle); < 0 = the same
    double cornerRound;            // chamfer the four vertical corners
    double roofTuck;               // pull the roof sides inward
    double rakeFront, rakeRear;    // windshield / rear window slope

    // Weight (kg). Everything is placed where it really sits, so the center of
    // gravity and the front/rear split come out of the build.
    double structureMass;          // welded body structure: floor, rails, pillars (mostly low down)
    double panelMass;              // outer panels and glass, spread over the skin
    std::vector<MassItem> items;   // interior, fuel, battery, radiator, exhaust...
    double bodyStiffness;          // multiplier on beam stiffness
    double bodyStrength = 1.0;     // multiplier on how far the body steel bends before it dents (thinner, older steel < 1)
    double frameStiffness = 1.0;   // multiplier on the chassis frame's rigidity (body-on-frame trucks: less)
    double frameStrength = 1.0;    // multiplier on how far frame rails bend before they take a set (ladder frames: more)
    double panelStrength = 1.0;    // multiplier on panel sheet metal (thicker truck doors and hoods: more)
    double pinStrength = 1.0;      // multiplier on the force hinges, latches and clips hold
    // Body on frame: a separate steel ladder frame carries the suspension, the engine and the
    // recovery points, and the body tub sits on it on rubber mounts (0 rail spacing = unibody).
    double railSpacing = 0;        // between the two rails' centres (m)
    double railHeight = 0.20;      // boxed rail section, tall x wide (m)
    double railWidth = 0.09;
    double frameMass = 0;          // kg (part of structureMass)
    double railYield = 0.01;       // how far (m) a rail's shape is pushed before the steel yields
                                   // (sets its strength: the shape-holding pull at that point)
    int bodyMounts = 8;            // rubber mounts between the body and the frame
    // The transfer case's skid plate, bolted under the frame behind the gearbox: the lowest
    // thing between the axles on a 4x4 (what the breakover angle is measured to, and what
    // high-centres it on a ridge). How far it hangs below the rails (m); 0 = none.
    double skidDrop = 0;

    // Powertrain: engine and gearbox as one block on rubber mounts.
    double engineMass = 0, gearboxMass = 0;  // 0 = no powertrain (props)
    bool transverse = false;                  // crank across the car (front-drive) or along it
    double engineX = 0, engineY = 0.5, engineZ = 0;  // block center
    double engineLen = 0.6, engineWid = 0.5, engineHgt = 0.5;  // along the crank, across, up
    double gearboxLen = 0.5;                  // behind the block (beside it, if transverse)

    // Running gear.
    double wheelRadius, wheelWidth;
    double wellTireRadius = 0;     // the biggest tire the wheel wells were cut for (m; 0 = this one): bigger rubs
    double wellTireWidth = 0;      // ...and its width
    double wheelWidthRear = 0;                // 0 = same as the front (staggered tires otherwise)
    double trackFront = 0, trackRear = 0;     // tire center to tire center (m); 0 = from the body width
    double wheelMass = 20, hubMass = 16;      // per corner: wheel and tire; knuckle, hub and brake
    double axleMass = 60;                     // solid axle housing and differential
    SuspensionType frontSusp = WISHBONE, rearSusp = WISHBONE;
    // Linkage sizes (m). Solid axle: the length of the lower trailing links (the upper link
    // is `upperLink` times it, or 0.55 m if that is 0; equal lengths keep the axle parallel to the frame through its travel); a front solid axle steers on kingpins and trails them back toward the
    // middle of the car), and how high above the axle the coil-over's top mount sits (0 =
    // the size-scaled default). Wishbones: how far the inner pivots are from the ball joints.
    double linkLength = 0.77, upperLink = 0, coilTop = 0, axleSeat = 0.10;  // (axleSeat: the coil-over's seat above the axle's centre line)
    double armLower = 0.36, armUpper = 0.27;
    // A live rear axle on leaf springs (pickups): each leaf locates the axle and is its spring;
    // the front half pivots on a hanger, the rear swings on a shackle. No links, no panhard.
    bool leafSprings = false;
    double leafHalf = 0.70;        // spring eye to the axle seat (m), each side
    // The front axle on leaves too (a steered solid axle: the 1970s 4x4s): fixed eye ahead,
    // shackle behind, the same way round as the rear.
    bool leafFront = false;
    double leafHalfFront = 0.60;
    double rideFreqFront = 1.5, rideFreqRear = 1.6;  // body bounce frequency on its springs (Hz)
    double dampingRatio = 0.35;
    double arbFront = 0, arbRear = 0;
    // A third spring per axle that only resists both wheels going up together (heave):
    // downforce squats the car without the springs going hard in bumps and roll. Rate at
    // each wheel (N/m) and the wheel travel it picks up after (m). 0 = none.
    double heaveRate = 0, heaveGap = 0, heaveDamping = 0;  // (damping: N*s/m at each wheel)         // anti-roll bar rate at the wheels (N/m, left vs right)
    // Wheel travel from ride height (m at the wheel): up to the bump stop, down to full droop.
    // 0 = the old defaults scaled by tire size.
    double bumpFront = 0, droopFront = 0, bumpRear = 0, droopRear = 0;
    // Front anti-roll bar disconnect: 0 none, 1 electronic (a dash button, under 30 km/h),
    // 2 quick-disconnect pins (stopped only).
    int swayDisconnect = 0;
    double tirePsi = 32;          // street pressure; air down for rock (the game airs between 12 and 40 psi)
    // Wheels coming off (tires.h), on vehicles with wheelLoss (the trucks, so far): the
    // studs holding a wheel shear at about this sideways force at the tread (N); beadlock
    // rims clamp the tire's bead, so it can't roll off however low it's aired.
    bool wheelLoss = false;
    double lugShear = 80000;
    bool beadlock = false;
    // The spare: the mass item named "spare ... wheel" is it. Its size (0 = the road
    // wheels'), and whether it lies flat (under a bed, in a trunk) or stands on a carrier.
    double spareR = 0, spareRimR = 0, spareWidth = 0, spareMass = 0;
    bool spareFlat = false;
    double ledgeFactor = 1.0;     // how much further than the tire-radius rule it climbs a square ledge (autopilot's step table: travel and soft sidewalls help)
    double looseGrip = 1.0;       // grip multiplier off the pavement (a soft crawler compound sticks to rock and dirt)
    double tireSoft = 1.0;        // radial stiffness at that pressure, against the 33 in truck tire (tall, low-pressure tires are softer)
    double fuelCapacity = 50;     // L in a full tank
    double jerryCan = 0;          // L in the spare can it carries (0: none)
    int tireType = TIRE_STREET;
    int snowChains = 0;           // chains carried, one per tire (fitted on foot: interact.h)
    double tireGrip = 1.1;    // peak friction coefficient on pavement
    double lateralGrip = 1.0; // cornering grip as a share of braking/traction grip (blocky off-road treads squirm)
    enum Drive { RWD, FWD, AWD } drive;
    bool midEngine = false;   // engine ahead of the rear axle, gearbox and diff in one transaxle
    bool liftgate = false;    // tall back opens upward (hinged at the top) rather than to the side
    DiffType diffFront = DIFF_OPEN, diffRear = DIFF_OPEN;
    double lsdBias = 2.5;     // limited slip: most torque one side can get vs the other
    TransferCase transfer = XFER_NONE;
    bool lockerFront = false, lockerRear = false;  // selectable lockers (a switch on the dash)
    bool frontLockerLowOnly = false, rearLockerLowOnly = false;  // the switch only works in low range
    double speedLimit = 0;    // km/h the ECU won't go past (0 = none): trucks are governed to their tires' rating
    double shaftLimit = 0;    // N*m an axle shaft takes before it snaps (0 = never); IFS CVs take 65% of it
    double brakeFront, brakeRear;  // N*m per wheel

    // Aerodynamics.
    double dragArea = 0.70;    // drag coefficient times frontal area (m^2)
    double downforce = 0;      // N per (m/s)^2
    double downforceMax = 0;   // N; the wing flattens out past this (0 = no limit)
    double aeroBalance = 0.45; // share of the downforce on the front axle

    // Engine and gearbox.
    std::vector<TorquePoint> torque;
    double idleRpm, redline;
    std::vector<double> gears;  // forward ratios, 1st first
    double reverseRatio, finalDrive;
    double shiftTime = 0.18;    // s with no drive during a shift
    double lowRange = 0;        // transfer case low ratio (4LO); 0 = none
    double turboLag = 0;        // s for boost to build (0 = naturally aspirated)
    // Automatic gearbox: a torque converter between the engine and the gears (0 = a clutch).
    // The stall speed (rpm the engine reaches at full throttle against a held turbine) sets
    // how hard the converter grips; it multiplies torque by stallRatio there. Above
    // lockupGear (and once moving) its clutch locks it solid.
    double stallRpm = 0;
    double stallRatio = 2.0;
    int lockupGear = 3;
    const char* engineName = "";
    const char* gearboxName = "";

    // Looks.
    int lampStyle = LAMP_RECT, grilleStyle = GRILLE_CENTER;
    unsigned extras = 0;
    double winchPull = 44000;      // N the winch stalls at (EXTRA_WINCH): a 9,500 lb winch on its first layer
    float coverColor[3] = {0.05f, 0.05f, 0.05f};  // engine cover
    float paintColor[3] = {-1, -1, -1};  // factory color (< 0: picked from the palette)
    float trimColor[3] = {0, 0, 0};      // two-tone lower body and flares...
    float trimTop = 0;                   // ...up to this lattice height (layers; 0 = no two-tone)
    int wheelStyle = WHEEL_ALLOY;
    // Set on tuned copies (see mods.h): which catalog vehicle, and the mods fitted.
    int model = -1;
    int mods[5] = {0, 0, 0, 0, 0};  // the garage's pick in each slot (mods.h ModSlot)
    int engineOrigin = -1;          // the catalog vehicle whose engine is fitted (-1: its own): picks its recorded sound
    std::string modSummary;  // "STAGE 1 / COILOVERS / STOCK" (empty when stock)

    enum Finish { PAINT, MATTE, CONCRETE } finish = PAINT;
    double contactScale = 1.0;  // < 1 for soft things (foam) that cushion impacts
};

// Drivable vehicles (keys 1-6).
const std::vector<VehicleSpec>& vehicleCatalog();
// Body lattice: x (m, + forward) of node column i (fractional i interpolates).
double latticeX(const VehicleSpec& vs, double i);
// Node columns the axles sit on (from the runs of 'W' in the bottom layer).
std::vector<int> axleColumns(const VehicleSpec& vs);
// Doors: how many lattice columns each door spans (about a meter), and whether the
// cabin is long enough for rear doors too.
int doorCells(const VehicleSpec& vs);
bool hasRearDoors(const VehicleSpec& vs);
// Peak power (W) and the rpm it's made at.
double peakPower(const VehicleSpec& vs, double* atRpm = nullptr);
double peakTorque(const VehicleSpec& vs, double* atRpm = nullptr);
// Moves the body (and everything on it) up by dy relative to the wheels: lift kits, lowering.
void raiseBody(VehicleSpec& vs, double dy);
// Wheel-less soft bodies placed around the map: foam crash boxes, concrete barriers.
const VehicleSpec& crashBoxSpec();
const VehicleSpec& barrierSpec();
