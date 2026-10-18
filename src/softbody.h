// Node/beam soft-body vehicle simulation.
//
// A car is a lattice of point masses (nodes) joined by damped springs (beams).
// Beams yield plastically when strained too far (permanent dents) and snap
// when stretched past their limit. Suspension is made of the same beams:
// stiff "arms" locate each wheel hub and soft "springs" carry the load.
#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "autopilot.h"
#include "mathx.h"
#include "rope.h"
#include "tires.h"
#include "vehicles.h"
#include "world.h"
#include "physics_workers.h"

struct Node {
    V3 p, v, f;
    double m = 1, invm = 1;
    bool collide = true;
    float scrape = 0;  // sliding speed while touching the ground (for sparks)
};

enum BeamType : uint8_t {
    BEAM_BODY,     // body lattice
    BEAM_CABIN,    // glass cabin lattice (softer: roofs crush)
    BEAM_ARM,      // suspension link: control arm, trailing link, panhard rod
    BEAM_SPRING,   // coil spring and damper
    BEAM_KNUCKLE,  // knuckle / hub carrier / axle housing (one rigid piece)
    BEAM_FRAME,    // frame rails, crossmembers, subframe pick-up points
    BEAM_MOUNT,    // rubber engine mounts
    BEAM_ENGINE,   // engine and gearbox block
    BEAM_TIEROD,   // tie rod (length set by the steering rack) or rear toe link
    BEAM_SHAFT,    // drive shaft: carries no load here (it slides and flexes), but it can be torn off
    BEAM_PANEL,    // outer sheet-metal panel (hood, door, bumper...): thin, dents easily
    BEAM_BUMPSTOP, // jounce bumper from the frame onto a live axle's tube: pushes only once the axle rises onto it
    BEAM_TYPE_COUNT
};

struct Beam {
    int a, b;
    double hardening = 5;  // how quickly the yield point rises as the beam is deformed
    float relaxTime = 0;   // > 0 shortly after yielding: elastic strain bleeds into permanent set
    double L0;          // current rest length (changes when the beam yields)
    double Lorig;       // factory rest length
    double k, c;        // stiffness (N/m), damping (N*s/m)
    double yieldStrain; // strain beyond which the beam deforms permanently
    double breakStrain; // stretch beyond factory length (fraction) that snaps it
    double minL = 0, maxL = 0;  // bump / droop stops for springs (0 = none)
    uint8_t type;
    bool broken = false;
    bool snub = false;  // a rubber mount with a snubber and rebound washer (hard stops both ways)
    float friction = 0; // springs: Coulomb friction (N), the leaves of a leaf spring sliding on each other
};

enum SkinMat : uint8_t { MAT_PAINT, MAT_GLASS, MAT_HEADLIGHT, MAT_TAILLIGHT, MAT_TRIM, MAT_UNDER, MAT_WELL };

struct Tri {
    int v[3];
    uint8_t mat;
    double rest[3];  // factory edge lengths, used to detect torn panels
    V3 nrm;          // outward normal (refreshed when needed for collisions)
    bool torn = false;
    bool shattered = false;  // glass that has broken out
    // Collision geometry shared by every point query until the nodes move again.
    V3 e0, e1;
    double d00 = 0, d01 = 0, d11 = 0, den = 0;
};

// One quad of the outer skin (two Tris), with where it sits in the body lattice.
// The renderer builds the smooth visual body from these.
struct SkinQuad {
    int v[4];          // node ids, wound outward
    float lat[4][3];   // lattice coordinates (i, j, k) of each corner
    int cell[3];       // the solid cell this face belongs to
    uint8_t mat, axis, side;
    char cellChar;     // profile character of that cell ('G' = cabin)
    int tri;           // index of its first Tri (the second is tri + 1)
};

// A suspension link, kept for drawing the running gear.
enum LinkKind : uint8_t {
    LINK_LOWER_ARM, LINK_UPPER_ARM, LINK_TIE_ROD, LINK_TOE_LINK, LINK_TRAILING, LINK_PANHARD,
    LINK_BUSHING,  // trailing-arm pivot bushing (not drawn)
    LINK_STRUT_GUIDE,  // holds a strut's top to its mount (part of the strut; not drawn)
    LINK_LEAF,     // a leaf spring's front half, hanger to axle seat (drawn with its rear half and shackle)
};
struct SuspLink {
    int beam;
    uint8_t kind;
    int8_t wheel;  // -1 = belongs to an axle, not one wheel
};

// Diff side gear plus its share of the ring gear, carrier and prop shaft, as seen at one
// half shaft (kg*m^2).
constexpr double kSideGearI = 0.15;
// A solid axle's tube and diff housing (pumpkin) radii: drawn at this size, and what hits
// the ground.
constexpr double kAxleTubeR = 0.04, kAxleDiffR = 0.125;

struct Wheel {
    int inner, outer;       // hub nodes: the axle runs inner -> outer
    int top = -1;           // upper ball joint (or spring seat on a solid axle)
    int lower = -1;         // lower ball joint (or lower link mount)
    int arm = -1;           // steering arm / toe link end
    int spring = -1;        // coil-over beam
    int tieRod = -1;        // tie rod beam (front) or toe link (rear), -1 = none
    int bumpStop = -1;      // jounce bumper over a live axle's tube (-1 = the coil-over's own stop)
    int shackle = -1;       // leaf spring: the rear hanger its shackle swings from (frame node; drawn)
    std::vector<int> assembly;  // nodes moving with the hub (brake torque reacts here)
    std::vector<int> shafts;    // drive shafts that must be intact for this wheel to be driven
    double springRest = 0;      // coil-over length at ride height (anti-roll bars work from it)
    double motionRatio = 1;     // coil-over travel per unit of wheel travel
    double tieOffset = 0;       // permanent bend in the tie rod (m): the wheel is knocked out of line
    double brakeReaction = 0;   // brake torque passed to the knuckle (smoothed by pad/caliper give)
    double abs = 1;             // ABS: share of brake pressure let through to this wheel
    double tcBrake = 0;         // brake traction control: share of this brake applied to stop it spinning
    double escBrake = 0;        // stability control: share of this brake applied to stop a spin
    double escCut = 0;          // ...and how much of the driver's braking it lets off here (inside wheels)
    // Steering geometry in the car's rest frame: lower and upper ball joints (kingpin
    // axis), steering arm end, and rack end. The tie rod's length follows the steer angle.
    V3 kpLow, kpUp, armRest, rackRest;
    bool left, front, driven;
    double R = 0.32, rimR = 0.21, width = 0.24, I = 1.2;
    double omega = 0, angle = 0, brakeMax = 2000;
    double sx = 0;          // last longitudinal slip (for ABS)
    float driveActual = 0;  // drive torque the gears put through its hub last step (N*m)
    double groundSpeed = 0; // road speed along the tire's heading (last step)
    double stickX = 0, stickY = 0;  // contact-patch deflection while static friction holds (m)
    float slip = 0, load = 0;
    V3 contact;             // last ground contact point (valid when load > 0)
    V3 contactNormal;
    Surface surface = SURF_PAVED;
    bool detached = false;
    bool popped = false;    // blown tire: runs on the rim
    bool chained = false;   // snow chains fitted over the tire (on foot: interact.h)
    float chainStrain = 0;  // s driven too fast on them: past ~2 s they snap
    double poppedAnim = 0;  // deflation animation: 0 = inflated, 1 = fully flat
    bool shaftBroken = false;  // its axle shaft / CV snapped: the hub freewheels
    // The wheel bolted on (tires.h): what it is, and whether it's there at all. Off (its
    // studs sheared, or taken off on the trail), the corner rolls on its bare hub; with
    // the tire rolled off the rim (debeaded), on the rim.
    WheelItem item;
    bool mounted = true;
    bool debeaded = false;
    float studLoad = 0;        // load trying to shear the wheel off its studs (N, smoothed over ~5 ms)
    float beadLoad = 0;        // side load over the tire's load, trying to roll it off the rim (smoothed)
    uint8_t shed = 0;          // set by the tire model: 1 the wheel came off, 2 the tire did (Sim spawns it)
    float sink = 0;            // how far it has sunk into sand, mud or snow (m)
    float clog = 0;            // mud packed into the tread, 0..1 (spinning flings it out)
    float bite = 1;            // how fast its grip builds with slip here (tread and surface): ABS and traction control aim for the peak
    float shaftLoad = 0;       // torque through the axle shaft (N*m, smoothed over ~10 ms)
    double xferTorque = 0;     // torque a locker or the locked center put through it this step
    // The half shaft (or CV shaft) is a torsion spring between the diff's side gear and
    // the hub: the gears and lockers turn the side gear, the shaft winds up and drives
    // the wheel. Its wind-up is what keeps a tire catching an edge from stopping the whole
    // geared-down engine through the shaft in one step.
    double sideOmega = 0;      // diff side gear (inner end of the shaft), rad/s
    double twist = 0;          // shaft wind-up, rad (+ = driving forward)
    double shaftTorque = 0;    // torque in the shaft now (N*m)
    double omegaWas = 0;       // omega at the end of the last step (follows outside changes)
    // The tire against its own car: wheel-well liners, fenders, bumpers, sills and the frame
    // rails. Built once: the faces it could reach (skin triangles, and the rails' sides as
    // triangles); at run time, how hard it's rubbing.
    std::vector<std::array<int, 3>> rubFaces;
    std::vector<float> rubAllow;  // per rub face, for each of its 7 sample points: how deep the stock tire
                                  // already reaches there over its travel and steering lock (the coarse
                                  // lattice cutting a corner)
    double rubTorque = 0;      // the rubbing's drag on its spin this step (N*m)
    float rub = 0;             // how hard it's pressing on the body (N)
    std::vector<int> mounts;  // beams that attach this wheel to the body
    // Radius it rolls on (a blown tire runs on its rim).
    double rollR() const { return !mounted ? kBareHubR : debeaded ? rimR + 0.005 : popped ? rimR + 0.015 : R; }
    static constexpr double radialStiffness = 2.2e5;
    double radialK(double psi, double stockPsi, double softness = 1) const {
        return radialStiffness * softness * (psi + 10) / (stockPsi + 10);
    }
    // Pressure already changes spring compliance in wheelStep. Do not shrink
    // collision geometry as well: that double-counts sag and hurts ledge climbing.
    double loadedRadius(double psi, double stockPsi, double softness = 1) const {
        if (!mounted || debeaded || popped || !std::isfinite(psi) || !std::isfinite(stockPsi) ||
            !std::isfinite(softness) || psi < 0 || stockPsi < 0 || softness <= 0) return rollR();
        return std::max(rimR + 0.015, R - std::max(0.0, (double)load) / radialK(psi, stockPsi, softness));
    }
};

// A pin holds a panel node on the body node under it (a spring with no rest length).
// Hinges are strong; latches and clips let go in a hard hit.
// A seal is the rubber a shut door's window frame rests against: soft, it only keeps
// the frame from rattling, and lets go as soon as the door is open or off.
// A check is the door check strap (or a hood's prop, a gate's cable): slack until the
// panel is open to its stop, then it holds it there; flung open hard, it tears.
enum PinKind : uint8_t { PIN_HINGE, PIN_LATCH, PIN_CLIP, PIN_SEAL, PIN_CHECK };
struct Pin {
    int body, panel;  // node on the structure, node on the panel
    double k, c, breakDist;
    double slack = 0;  // pulls only once the nodes are this far apart
    uint8_t kind;
    bool broken = false;
};

// A bolt-on body panel: its own sheet of nodes over some of the skin, pinned to the
// structure. A door whose latch breaks swings on its hinges; a bumper whose clips
// break falls off.
struct Panel {
    const char* name;
    std::vector<int> quads;          // skin quads it covers
    std::vector<int> bodyNodes;      // the structure nodes under it...
    std::vector<int> panelNodes;     // ...and its own nodes over them (same order)
    std::vector<int> pins;
    // Per panel node: the panel's skin quads it sat on. Off its pin, it's still a sheet of
    // steel against the body: it can swing out, but not in through the quads under it.
    std::vector<std::vector<int>> nodeQuads;
    std::vector<int8_t> outside;     // per panel node: came to the body from outside (only those are held out)
    std::vector<int> checks;         // its opening stops (PIN_CHECK pins, not in `pins`)
    int state = 0;                   // 0 shut, 1 hanging open on its hinges, 2 torn off
    // By hand (Car::setGate): its latches off, and the sheet driven round its hinges.
    bool hand = false;
    double handSign = 0;             // which way round the hinge line it opens (+-1)
};

// One axle: its two wheels, anti-roll bar, and where its differential pushes back.
struct Axle {
    bool front = false;
    int wheel[2] = {-1, -1};      // left, right
    double arbK = 0;              // anti-roll bar rate, per meter of coil-over difference
    // The bar is spring steel: twisted past what the suspension's travel asks of it (a big
    // hit on one wheel), it takes a set and holds that corner up (the truck leans).
    double arbYield = 1e9;        // elastic limit, in coil-over difference (m)
    double arbBend = 0;           // permanent set (m of coil-over difference it now holds)
    double heaveK = 0, heaveGap = 0, heaveC = 0;  // third (heave) spring and damper: per meter (per m/s) of both coil-overs squeezing together, past heaveGap
    std::vector<int> diffNodes;   // differential housing (drive torque reacts here)
    int propShaft = -1;           // prop shaft from the gearbox (-1 = none)
    int diffNode = -1;            // where the shafts meet (for drawing)
    std::vector<int> housing;     // solid axle: the housing nodes (empty for independent)
};

struct Frame {
    V3 fwd, up, left, com, vel;
};

// The chassis as a whole. A coarse node lattice can't be as stiff as a real welded
// body without a tiny time step, so the body's structural nodes are also pulled
// toward a best-fit rigid copy of their own rest shape ("shape matching"). That
// gives the frame its real torsional and bending stiffness while staying stable.
// Each node's pull is capped, and when its beams bend for good (a dent) the node is
// let go briefly and its rest position re-taken, so crashes still crumple the body.
struct ChassisFrame {
    std::vector<int> nodes;     // structural nodes taking part
    std::vector<V3> rest;       // their rest positions relative to the rest center of mass
    std::vector<char> attached; // 0 = torn away from the main structure
    std::vector<float> release; // > 0: node is part of a fresh dent (s left), the frame lets it move
    std::vector<int> slot;      // node id -> index in `nodes` (-1 = not part of the frame)
    Quat rot;                   // current best-fit rotation (warm start)
    double kappa = 0;           // stiffness per kg (N/m/kg): sqrt(kappa) is the frame's frequency
    double damping = 0;         // damping per kg (N*s/m/kg) on non-rigid motion
    double releaseDist = 0.02;  // a node pushed this far out of shape is let go (it's being dented)
    double maxPull = 2500;      // most it pulls on one node (N): about what the body around it can carry,
                                // so in a crash the body itself gives way (and the frame lets go)
    // Steel that bends and stays bent (a frame rail): > 0, a node held further than this out
    // of shape doesn't let go, it flows (its rest position moves), and the beams between its
    // nodes take the same set. 0 = sheet metal: let go when shoved out of shape.
    double yieldDist = 0;
    std::vector<float> weight;  // per member: how hard it's held (x its mass), e.g. a welded-on bracket
                                // is stouter than the bare rail; empty = all 1
    std::vector<int> section;   // per member: which cross-section it's part of (a rail station's corners
                                // bend together: the section itself only crushes under a far bigger
                                // local shove); empty = each on its own
    std::vector<std::vector<int>> beamsAt;  // per member: its beams to other members (built on the first frame)
};

// Spatial hash of a car's skin triangles, rebuilt on demand for car-vs-car contact.
struct TriGrid {
    static constexpr int kBuckets = 4096;
    static constexpr double kCell = 0.5;
    // Buckets are chains through one pooled array (in insertion order), so a rebuild reuses its storage instead of regrowing 4096 vectors.
    int head[kBuckets], tail[kBuckets];
    std::vector<int> val, nxt;
    std::vector<int> used;
    long stamp = -1;
    TriGrid() { std::fill(head, head + kBuckets, -1); }
    struct Chain {
        const TriGrid* g;
        int first;
        struct It {
            const TriGrid* g;
            int i;
            int operator*() const { return g->val[i]; }
            It& operator++() { i = g->nxt[i]; return *this; }
            bool operator!=(const It& o) const { return i != o.i; }
        };
        It begin() const { return {g, first}; }
        It end() const { return {g, -1}; }
    };
    void clear();
    void insert(int tri, const V3& mn, const V3& mx);
    Chain query(const V3& p) const;
    static int key(int x, int y, int z);
};

class Car {
public:
    const VehicleSpec* spec = nullptr;
    std::vector<Node> nodes;  // body nodes first, then wheel nodes
    std::vector<Beam> beams;
    std::vector<Tri> tris;
    std::vector<Wheel> wheels;
    std::vector<int> engineNodes;   // engine and gearbox block (8 engine corners, then 4 gearbox)
    std::vector<int> engineMounts;  // rubber mount beams
    std::vector<int> bayBeams;      // engine-bay structure (crushing it damages the engine)
    std::vector<Axle> axles;
    std::vector<SuspLink> links;
    std::vector<int> frameBeams;    // frame rails and crossmembers under the floor (for drawing)
    std::vector<Pin> pins;
    std::vector<Panel> panels;
    std::vector<int> quadPanel;     // per skin quad: the panel covering it, or -1
    int gearboxOut = -1;            // node at the gearbox output
    // Body nodes that the engine block pushes against, each with the block face
    // (axis * 2 + positive side) it stays outside of.
    std::vector<std::pair<int, int8_t>> blockContacts;
    int frameNodes = 0;             // nodes [0, frameNodes) are rigidly part of the body
    std::vector<std::vector<int>> nodeTris;
    // Up a side wall above the sill: the sill node at the foot of it (a push on the door goes
    // there through the door's side-impact beam and the pillars), else -1.
    std::vector<int> sideSill;  // skin triangles touching each node (for smooth shading)
    std::vector<SkinQuad> quads;               // skin as quads (visual body is built from these)
    std::vector<V3> restLocal;                 // every node's factory position in the car's own frame
    std::vector<std::array<int, 8>> cells;     // solid cells' corner node ids (bit 0 = +i, 1 = +j, 2 = +k)
    std::vector<std::array<int, 3>> cellIJK;   // lattice coordinates of each solid cell
    std::vector<std::pair<int, int>> skinEdges;  // unique panel edges (for pole collisions)
    void buildWheelRub();      // each tire's nearby skin triangles (after the tris and rest shape exist)
    std::vector<V3> extForce;  // optional extra force per node (test rigs); empty = none
    ChassisFrame chassis;
    std::vector<V3> chassisRest0;  // its factory rest shape (what dents are measured against)
    // A body-on-frame truck's ladder frame: each rail (and everything welded to it) holds its
    // own shape, stiff in bending; the crossmembers tie the two, and give in twist as a real
    // ladder frame does. The body tub rides on it on its rubber mounts. Empty on a unibody.
    ChassisFrame railFrame[2];
    int bodyNodes = 0;
    // Ladder frame (body-on-frame trucks): its nodes, station by station (left rail's four
    // section corners, then the right rail's), and the rubber body mounts.
    std::vector<int> ladder;
    int ladderStations = 0;
    std::vector<int> bodyMountBeams;
    std::vector<int> crossStations;  // the stations (ladder index / 8) a crossmember ties the rails at, front to back
    std::vector<int> skidNodes;      // the transfer case's skid plate: three frame nodes across the frame (empty without one)
    double mass = 0;
    double wheelbase = 2.5;
    double frontShare = 0.5;  // share of the weight on the front axle (at rest, from the build)
    std::vector<double> aeroShare;  // per body node: its share of the downforce (empty = none)
    double aeroSpeed = 0;           // airspeed along the car (m/s), as the aero last saw it

    V3 color;
    V3 spawnPos;
    double spawnYaw = 0;

    // Controls.
    double throttle = 0, brake = 0, handbrake = 0;
    double brakePressure = 0;  // follows `brake`, but takes ~0.15 s to build (hydraulics)
    double steerInput = 0;  // -1..1 (smoothed key input)
    double steer = 0;       // road-wheel angle, radians (+ = left)
    double rackSteer = 0;   // where the rack actually is: it follows `steer` at a finite speed

    // Drivetrain.
    int gear = 1;  // -1 reverse, 0 neutral, 1..N
    bool towRolling = false;   // unattended recovery: temporarily in neutral
    int towGear = 1;           // restore the driver's selection when taking the wheel
    double rpm = 850, shiftTimer = 0;
    uint8_t lampMode = 0;  // LampMode (lights.h); carried reliably to other players
    bool manualShift = false;   // the driver picks gears (otherwise it shifts itself)
    bool lowRange = false;      // transfer case in low range (follows driveMode on a 4x4)
    int driveMode = -1;         // transfer-case position (index into driveModes(spec->transfer))
    TransferCase modeTransfer = XFER_NONE;  // which transfer case driveMode refers to
    bool lockFront = false, lockRear = false;  // selectable lockers engaged
    double boost = 0;           // turbo boost built up, 0..1
    bool tractionControl = true;
    double tcCut = 1;           // traction-control throttle multiplier
    // Stability control: the yaw rate it measures (rad/s, + = turning left) and how hard it's
    // working (0..1: braking the outside front, easing the throttle).
    double escYaw = 0, escLevel = 0;
    double govKmh = 0;          // the speed limiter's filtered road speed
    V3 escFwd;
    double dragAssist = 0;      // throttle opened to stop engine braking locking the driven wheels
    double throttleFill = 0;    // the throttle the engine has built up to (see wheelStep)
    double engineHealth = 1;    // drops as the engine bay is crushed
    bool engineDead = false;
    double fuel = -1;           // L in the tank (-1: full; filled when first driven)
    double jerry = -1;          // L in its spare can (-1: as it came)
    double fuelUse = 0;         // how fast it burns: 0 none (tests), 1 real, more for the game
    bool outOfFuel() const { return fuelUse > 0 && fuel == 0; }
    bool hydrolocked = false;   // sucked water into the intake (dead until repaired)
    // Trail setup.
    double tirePsi = 32;        // tire pressure (starts at the spec's; air down for rock)
    // The spare (tires.h): on its carrier (or under the bed, in the trunk) if hasSpare.
    // Where it sits follows the body: a weighted mix of the lattice nodes around it, which
    // also carry its weight (taken off with it, put back when one is stowed).
    WheelItem spare;
    bool hasSpare = false;
    std::vector<int> spareNodes;
    std::vector<double> spareWeights;
    std::vector<double> spareShift;  // kg taken off each node with the spare (put back when one's stowed)
    V3 spareAt() const;          // where it is (world)
    V3 spareAxis() const;        // its axle (world)
    // Where the snow chains (STOW_CHAINS) and the jack (STOW_JACK) ride: in the back by the
    // tailgate, either side, apart from the spare so each is its own thing to reach for.
    enum Stow { STOW_CHAINS, STOW_JACK };
    V3 stowAt(Stow what) const;
    // The back gate (trunk.h): a tailgate or liftgate panel opened by hand on its real hinges (its
    // latches let go and the sheet is driven round the hinge line, held there like a gas strut, so
    // a bump rattles it), or, on a pickup, a drawn gate with no panel under it.
    struct Gate {
        double share = 0;  // 0 shut .. 1 open
        int dir = 0;       // +1 opening, -1 closing
        double jam = 0;    // s spent closing against something
    } gate;
    static constexpr double kGateTime = 1.1;    // s to swing it
    static constexpr double kGateAngle = 1.40;  // rad open (80 degrees: short of the check strap)
    int gatePanel() const;     // the LIFTGATE / TAILGATE panel (-1: none)
    bool bedGate() const;      // a pickup: the gate is drawn (trunk.h), not a panel
    bool hasGate() const { return gatePanel() >= 0 || bedGate(); }
    bool gateOpen() const;     // open enough to reach in (or torn off)
    bool gateShut() const;
    void setGate(bool open);   // starts it swinging
    bool gateCarried() const;  // the spare rides the gate's carrier: it swings away with it
    V3 gateSwung(const V3& p, bool point) const;  // a point (or direction) on the gate, turned as far as it's open
    void gateStep(double dt);  // per frame: the swing, and latching it again when it's home
    double winchSpeed = 1.0;    // multiplier of the rated line speed (0.1-20); game preference starts at 4x
    bool swayDisconnected = false;  // front anti-roll bar unhooked
    int shaftsBroken = 0;       // axle shafts snapped since last read (for the HUD)
    int airing = 0;             // tire valves: -1 letting air out, +1 pumping up (held key)
    int station = -1;           // repair station it's parked at (-1 = none)
    double repairTime = 0;      // s spent waiting on the pad
    // The jack: a tool the player can place under a wheel to lift it off the ground.
    int chains = 0;             // snow chains in the back (VehicleSpec::snowChains, back after a rebuild)
    int chainsSnapped = 0;      // chains that broke from too much speed (the HUD counts these)
    bool hasJack = false;       // the "jack and recovery straps" mod
    bool jackPlaced = false;    // the jack is currently under the car
    int jackWheel = -1;         // which wheel the jack is under
    double jackHeight = 0;      // how far up the jack has lifted (m)
    bool jackWorked = false;    // it's been raised since it went under (so once it's down again, it comes out)
    double jackBase = 0;        // where its saddle met the frame's jack pad when it went under (m over the ground)
    V3 jackFoot;                // where the jack's foot stands (world, set when it goes under)
    void setJack(bool has);     // called when the jack mod is fitted
    // A point on a ladder rail now (world): side -1 / +1 is the rail on the car's -z / +z (in its own build frame), corner 0..3 the rail
    // section's inner bottom, outer bottom, inner top, outer top, xRest a place along the car (rest frame x), the stations either
    // side blended. (No ladder: the car's centre.)
    V3 railPoint(int side, double xRest, int corner) const;
    // Where a jack goes under a corner: the frame's jack pad (carframe.h), a plate welded under the rail's outside edge
    // just inboard of the wheel well (behind a front wheel, ahead of a rear one), not in the well and not under the axle.
    // Where the saddle touches it (world), the pad's underside.
    V3 jackPoint(int wheel) const;
    double jackPadX(int wheel) const;   // where along the car the pad is (rest x): clear of the tire's footprint, inboard of the well
    double jackDrop(int wheel) const;   // how far that point is under the hub (m)
    V3 jackOut(int wheel) const;        // level, outward from the car at that corner: the way the jack's handle points
    double jackReach(int wheel) const;  // how far out from the pad the jack stands (m): past the body's side, its arm reaching in
    // The rail nodes the saddle lifts, and their shares (the two stations round the pad, bottom corners).
    struct JackNodes {
        int n[4] = {-1, -1, -1, -1};
        double w[4] = {0, 0, 0, 0};
    };
    JackNodes jackNodes(int wheel) const;
    void placeJack(const World& world, int wheel);  // the jack goes under this corner, saddle just touching
    // Multiplayer: a remote car is another player's, moved by the network (its owner bends
    // it; here it only pushes back). netId is the same on every machine.
    bool remote = false;
    uint32_t netId = 0;
    // A remote car one of ours crashes into is simulated here, as a whole soft body, through
    // the hit and a little after: here it was a wall (moved by its states, it couldn't be
    // pushed or bent, so whoever hit it took the whole crash, and their states showed them
    // stopping dead, so it was hardly touched on its owner's machine). Its owner still bends it
    // for real (that machine runs the same crash) and its damage still comes from there.
    // Handed back, it's eased onto its states: shoveP / shoveV are added to where they put it
    // until they catch up (the network playback calls settleShove).
    double heldHere = 0;     // s left on our physics
    bool handBack = false;   // just let go: the playback works out the ease from here
    V3 shoveV, shoveP;
    V3 shoveNetVel;
    void holdHere();         // takes it on: the shape it's in is its rest shape here
    void handBackTo(const std::vector<V3>& statePos, const V3& stateVel);  // where its states put it now
    void settleShove(const V3& netVel, double dt);
    int buildCount = 0;         // bumped by every build() (a respawn or repair)
    // Recovery points, front and rear: the frame nodes a line pulls on, and where the
    // shackle sits (rest position in the car's frame).
    std::vector<int> hooks[2];
    V3 hookRest[2];

    Autopilot pilot;  // drives to a destination (autopilot.h)
    // ---- vehicle tools (vtools.h): this vehicle's own name and autopilot preferences ----
    uint32_t uid = 0;         // stays with the vehicle for the whole session (the list and popup find it by this)
    int labelNo = 0;          // "BRONCO 2": its number among the vehicles of that model (kept until the model changes)
    int labelModel = -2;      // ... the model (and owner) the number was given for
    double wantKmh = 0;       // speed it is asked to drive at (km/h; 0 = the settings' default)
    int wantRoute = -1;       // route strictness (PilotRoute; -1 = the settings' default)
    double followGap = 0;     // when following: how far behind the leader to aim, on top of the autopilot's own 12 m

    // AI driver (follows the loop road in its lane).
    bool ai = false;
    int aiIndex = -1;  // nearest road sample
    int aiDir = 1;     // direction of travel around the loop
    double aiCruise = 24.0;  // preferred speed on straights (m/s)
    double aiSteerTrim = 0;  // integral steering correction (holds the lane through long bends)
    double aiGap = 1e9;      // distance to the vehicle ahead (for HUD / tests)
    double aiStuckTime = 0, aiReverseTime = 0, aiSteerAtStuck = 0;
    double aiLane = 2.6;        // target offset right of the road center (negative while passing)
    bool aiPassing = false;     // overtaking a stopped obstacle in the oncoming lane
    double aiPassLeft = 0;      // road distance still to cover before pulling back in
    double aiBlockedTime = 0;   // how long a stopped obstacle has blocked our lane
    double aiLastAlong = -1;

    // State.
    bool sleeping = false;
    double stillTime = 0;
    double sleptControls[4] = {};  // throttle, brake, handbrake and steer when it fell asleep
    V3 bbMin, bbMax;
    double impactEnergy = 0;  // joules absorbed by deformation since last read
    double damage = 0;        // 0..1
    int brokenBeams = 0;
    std::vector<V3> glassBursts;  // where windows shattered since last read
    int tirePops = 0;             // tires blown since last read
    std::unique_ptr<TriGrid> grid;
    std::unique_ptr<TriGrid> nodeGrid;  // body nodes, for node-node contact
    long nodeGridStamp = -1;

    void build(const VehicleSpec& vs, const V3& pos, double yaw, const V3& color);
    void translate(const V3& d);  // moves the whole vehicle (and its autopilot's route) by d
    // A respawn is a fresh vehicle: repaired, a full tank and its spare can (switching to
    // another vehicle used to inherit the last one's tank, empty if it had run dry).
    void respawn(const V3& pos, double yaw) {
        build(*spec, pos, yaw, color);
        fuel = jerry = -1;
    }
    // Driver gear change (+1 up, -1 down); returns false if refused (would over-rev).
    bool shift(int dir);
    Frame frame() const;
    V3 engineBay() const;
    void computeForces(double h, const World& w);
    void integrate(double h);
    void updateBounds();
    void updateTris();
    void prepareContact(long step);
    void updateStats();
    bool finite() const;
    void wake() {
        sleeping = false;
        stillTime = 0;
    }
    bool isProp() const { return wheels.empty(); }
    // Recovery gear (recovery.cpp).
    V3 hookPoint(int e) const;            // shackle position (world)
    V3 hookVel(int e) const;
    void pullHook(int e, const V3& F, const V3& at);  // a line's pull F at the shackle (at)
    // Turns the truck as it is (damage and all) upright at (x, z) facing `yaw`, set down on
    // whatever is there: a wrecker righting it, not a repair.
    void placeUpright(const World& w, double x, double z, double yaw);
    // Turns the whole vehicle rigidly by `ang` about `axis` through its center of mass
    // (and the chassis frame with it), at rest.
    void rotateRigid(const V3& axis, double ang);
    // Tilts the vehicle to the ground under its wheels and sets it down with nothing
    // buried (after a rebuild in place on a slope).
    void setDown(const World& w);
    bool needsRepair() const;
    double repairSeconds() const;         // how long a repair station takes to fix it
    // Trail repairs (from another truck's kit, parked alongside): tires, axle shafts, bent
    // steering and suspension links, a flooded engine. Bodywork, glass, lost wheels and a
    // wrecked engine need a station.
    bool needsFieldRepair() const;
    double fieldRepairSeconds() const;
    // A hands-on repair under way (fixes.h): which one (Fix::Kind and what it's on) and how many of its work
    // steps are done. Kept while you walk off and come back; dropped once it's finished (or fixed some other way).
    struct FieldJob {
        int kind = 0, wheel = -1, axle = -1, beam = -1;
        int done = 0;        // work steps done
        bool fixed = false;  // the part's whole again (what's left: the wheel back on, the jack down)
    };
    std::vector<FieldJob> fieldJobs;
    void fieldRepair();

    // Repair kit: parts carried for trail-side field repairs. Consumed when fixing damage.
    struct RepairKit {
        int spareTires = 1;       // spare tire and wheel assemblies
        int cvJoints = 1;         // axle shaft / CV joint
        int tieRods = 1;          // tie rod or steering link
        int suspensionLinks = 1;  // control arm or suspension link
        int oilQuarts = 4;        // engine oil (quarts)
        int brakeFluid = 1;       // brake fluid (quarts)
        int tireValves = 4;       // tire valve cores
        int towStraps = 1;        // tow/recovery straps
        int fuelFilter = 1;       // fuel filter
        bool hasSpareParts() const { return spareTires > 0 || cvJoints > 0 || tieRods > 0 || suspensionLinks > 0; }
        bool isEmpty() const { return !hasSpareParts() && oilQuarts == 0 && brakeFluid == 0 && tireValves == 0 && towStraps == 0 && fuelFilter == 0; }
    };
    RepairKit repairKit;

private:
    std::vector<int> grpFront, grpRear, grpLeft, grpRight;
    std::vector<int> nearPoles;
    void poleEdgeContacts(double h, const World& w);
    void engineBlockContact(double h);
    void panelBodyContact(double h);
    void gateServo(double h);  // the back gate driven round its hinges (trunk.cpp)
public:
    bool wheelPowered(const Wheel& wh) const;  // driven, attached, and its shafts are intact
    // 4x4 controls. setDriveMode/setLocker only change state; the caller decides when the
    // driver may (speed limits). Lockers drop out when their axle stops being driven.
    const DriveMode* mode() const;
    bool frontEngaged() const;   // front axle receiving drive
    bool centerLocked() const;   // front and rear shafts tied together
    DiffType diffType(bool front) const;  // the axle's diff as it's working right now
    void setDriveMode(int m);
    void setLocker(bool front, bool on);
    // Whether that locker can go in right now (fitted, axle driven, and in low range
    // if it's wired to it).
    bool lockerAllowed(bool front) const;
private:
    void applyTorque(const std::vector<int>& set, const V3& torque, bool byMass = false);
    void chassisForces(double h);
    void chassisGroupForces(ChassisFrame& cf, double h);
    void releaseFromFrame(const Beam& b);
    void updateChassisMembership();
    void wheelStep(double h, const World& w);
    V3 groupAvg(const std::vector<int>& g) const;
    double engineTorque(double rpm) const;
    double shaftStiffness() const;
    void wheelBodyContact(double h);
    double gearRatio(int g) const;
};

class Sim {
public:
    // 1 = serial. The game opts in; scenario runners stay serial to avoid nested pools.
    void setPhysicsThreads(int threads);
    int physicsThreads() const { return physicsThreadCount; }
    World world;
    std::vector<Car> cars;
    int player = 0;
    // The player is out on foot: their vehicle, left standing, sleeps like any parked one (it wakes the same ways:
    // something touching it, its pedals or wheel worked, a jack wound up under it).
    bool playerAway = false;
    double h = 1.0 / 2000.0;  // physics step: 2 kHz
    double timeScale = 1.0;
    bool paused = false;
    static constexpr int kMaxVehicles = 12;
    static constexpr int kMaxBodies = 48;  // vehicles + props

    // Wheels off their vehicles (tires.cpp): lying about, rolling, bouncing.
    std::vector<LooseWheel> looseWheels;
    uint32_t nextLooseId = 1;
    WheelItem wheelItem(const Car& c, const Wheel& wh) const;  // what's bolted to that corner
    // The wheel (or with tireOnly, just the tire) comes off that corner and goes its own way.
    int shedWheel(int car, int wheel, bool tireOnly);
    int addLooseWheel(const WheelItem& it, const V3& p, const Quat& q, const V3& v, const V3& w);
    // On foot (interact.h): each needs the vehicle stopped, and returns false if it can't.
    bool takeSpare(int car, WheelItem& out);
    bool stowSpare(int car, const WheelItem& it);
    bool removeWheel(int car, int wheel, WheelItem& out);
    bool fitWheel(int car, int wheel, const WheelItem& it);  // a wheel on a bare hub, or a tire back on its rim
    bool pickUpWheel(int loose, WheelItem& out);
    int dropWheel(const WheelItem& it, const V3& at, const V3& facing);
    void looseWheelForces();  // ground, rocks and vehicles (run by substep)
    void looseWheelStep();

    // Winch lines and tow straps (recovery.cpp).
    std::vector<Rope> ropes;
    bool realRecovery = true;          // damage stays until a repair station fixes it
    bool steeringAssist = true;        // limit steering angle at high speeds (false = simulation mode)
    std::vector<std::string> events;   // things the HUD should announce (it clears them)
    std::string hookWinch(int car);    // hooks the car's winch to the best anchor ahead, or unhooks it
    // Everything within the winch line's reach, nearest first.
    // Every anchor the winch reaches. `look` (optional): where the driver is looking or
    // standing, offered as a ground anchor too (buried in loose ground, a bolt in rock).
    std::vector<WinchTarget> winchTargets(int car, const V3* look = nullptr) const;
    std::string hookWinchTo(int car, const RopeEnd& anchor);
    bool dropWinch(int car, bool say = true);  // unhooks the car's winch line (false if none); say: announce the strap or sling coming back
    // Anchor gear (anchors.h): what you carry, and the stakes you've left in the ground.
    // Hooking to a stake, bolt or picket uses one up; a strap or sling comes back when the line comes off.
    AnchorStock anchorStock;
    std::vector<SetAnchor> setAnchors;
    struct AnchorUse {
        AnchorGear gear = GEAR_COUNT;  // the gear it takes (GEAR_COUNT: a vehicle needs none)
        bool ok = true;
        const char* why = "";          // not ok: why
        int reuse = -1;                // a stake already set there (setAnchors index)
    };
    AnchorUse anchorUse(const RopeEnd& e, int exceptCar = -1) const;  // what hooking a line to `e` takes (ignoring car `exceptCar`'s own line)
    int anchorsFree(AnchorGear g, int exceptCar = -1) const;           // carried and not in use on a line
    void refillAnchors();                                              // sandbox: a full set again (a repair station, a respawn)
    std::string hookStrap(int car);    // straps the car to the nearest other vehicle, or unhooks it
    bool dropStrap(int car);
    std::vector<WinchTarget> strapTargets(int car, int hook) const;
    std::string hookStrapTo(int car, int hook, const RopeEnd& target);
    int pickRecoveryTarget(const std::vector<WinchTarget>& targets, const V3& eye, const V3& ray, double cone) const;
    void setWinch(int car, int dir);   // +1 reel in, -1 pay out, 0 hold
    int ropeOn(int car, int kind = -1) const;  // index of a line on that car (-1 none)
    // Where a line lies, end a to end b: sagging with its slack, and along the ground where it gets
    // there (`lift`: how far above it), so paid-out line lies on the ground rather than through it.
    std::vector<V3> ropePath(const Rope& r, double lift = 0) const;
    void dropRopes(int car);
    void carRemoved(int car);          // call BEFORE erasing cars[car]: drops its lines, shifts the line ends and every autopilot's followCar
    // Takes cars[i] out (a vehicle, not a prop, and not another player's) and fixes the player index. False if refused.
    // Removing the car you are in switches to another one first.
    bool removeCar(int i);
    // Per frame: tire pressure, sway bars, repair stations. Run by advance().
    void updateTrail(double dt);
    void repairCar(int car);
    int stationAt(const V3& p) const;  // the repair station pad p is on (-1 none)
    int nearestStation(const V3& p, double* dist = nullptr) const;

    // The freeway: total distance everything's been moved back along x so far (add it to a
    // position to get the distance really covered), and the move itself (run by advance).
    double wrapShift = 0;
    void wrapAround();

    void setup();
    // The vehicles setup() puts out, you in the first (empty: the whole catalog in order, you
    // in the Civic: what the tests expect). The game sets listedVehicles().
    std::vector<int> lineup;
    void setupEmpty();  // just the world (for tests)
    // Runs every AI driver (and the player's autopilot) for one frame.
    void updateAI(double dt);
    // Advances the simulation by realDt * timeScale; returns substeps taken.
    int advance(double realDt, int maxSubsteps);
    void substep();
    void drive(bool up, bool down, bool left, bool right, bool handbrake, double dt);
    void pilotStep(int car, double dt);  // one frame of a car's autopilot
    void respawnPlayer();
    void recoverPlayer();   // rebuilt like new where it stands (arcade)
    void rightPlayer();     // put back on its wheels where it is, damage and all
    void towPlayerHome();   // carried back to its spawn point, damage and all
    void teleportPlayer(double x, double z, double yaw);  // set down upright at x, z facing yaw (the dev menu), damage and all
    // Replaces the player's car with another vehicle type, in place.
    void swapPlayerVehicle(const VehicleSpec& vs);
    // Adds a parked vehicle a few meters ahead of the player; returns false when full.
    bool spawnAhead(const VehicleSpec& vs);
    // Removes the player's car (if it isn't the last one) and switches to another.
    void removePlayerCar();
    // Puts an AI-driven vehicle on the loop road ahead of the player; false when full.
    bool spawnTraffic(const VehicleSpec& vs);
    V3 nextColor();
    int vehicleCount() const;
    // Next drivable vehicle after index `from` (wraps around).
    int nextVehicle(int from) const;
    void addCar(const VehicleSpec& vs, const V3& pos, double yaw, const V3& color);
    // Nearest loop-road sample to p; searches around `hint` when it is >= 0.
    int nearestRoadIndex(const V3& p, int hint) const;

private:
    double accum = 0;
    std::vector<std::pair<int, int>> contactPairs;
    std::vector<int> contactOrder;
    std::unique_ptr<PhysicsWorkers> physicsWorkers;
    int physicsThreadCount = 1;
    std::vector<int> forceCars;
    std::vector<uint8_t> needsContact;
    long stepCount = 0;
    int colorIndex = 0;
    void placeProps();
    void aiDrive(int self, double dt);
    std::vector<Frame> aiFrames;
    std::vector<double> aiAlong, aiLat;
    int trafficDir = 1;
    void collide(Car& A, Car& B);
    void ropeForces();
    void jackForces();
    void pointsVsSkin(Car& A, Car& B);
};
