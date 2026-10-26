// Visual models for vehicles, driven by the soft-body physics.
//
// Body: the physics skin is a coarse cage of quads. Two rounds of Catmull-Clark
// subdivision turn it into a smooth shell; because subdivision is linear, every
// visual vertex is a fixed weighted sum of physics nodes, so the shell follows
// every dent and tear exactly. Fine detail (windows, lamps, grille, seams,
// plates) is drawn per pixel from lattice coordinates carried by each vertex.
// The Jesko instead uses dimensioned, curved surface patches bound to this same
// cage and its moving panels. Separate seam vertices let doors and covers detach.
//
// Parts (engine, interior, exhaust, mirrors...) are small meshes attached to the
// lattice cell around them with trilinear weights, so they deform with the car.
//
// Running gear (arms, coil-overs, drive shafts) is rebuilt every frame from the
// actual suspension beams, so what you see is what the physics is doing.
#pragma once
#include <vector>

#include "render.h"
#include "softbody.h"

// Body vertex data that never changes for a vehicle type.
struct BodyStatic {
    float lat[3];  // lattice coordinates (i along, j across, k up)
    float region;  // which part of the body this is (see BodyRegion)
};

// Body vertex data recomputed every frame.
struct BodyDynamic {
    float p[3], n[3];
    float broken;  // 1 = this window has shattered
};

enum BodyRegion {
    REG_SIDE = 0,         // body side below the windows
    REG_CABIN_SIDE = 1,   // side windows and pillars
    REG_WINDSHIELD = 2,
    REG_REAR_WINDOW = 3,
    REG_ROOF = 4,
    REG_FRONT = 5,        // nose: lamps, grille, bumper, plate
    REG_REAR = 6,         // tail: lamps, bumper, plate
    REG_UNDER = 7,
    REG_WELL = 8,
    REG_BED = 9,          // pickup bed floor
    REG_TOP = 10,         // hood, trunk lid, bed rails
    REG_STEP = 11,        // other vertical faces (cab back wall...)
};

struct VisualTemplate {
    // Smooth body: each topological vertex is a sparse weighted sum of nodes (CSR).
    std::vector<int> wStart, wNode;
    std::vector<float> wVal;
    int topoCount = 0;
    struct Face {
        int topo[4];  // topological vertices (for positions and normals)
        int rv[4];    // render vertices (topological vertex split by region)
        int parent;   // skin quad this face came from
        bool glass;   // part of a window region (drawn again in the glass pass)
    };
    std::vector<Face> faces;
    std::vector<int> rvTopo;
    std::vector<BodyStatic> rvStatic;

    // Decal layout in lattice units, passed to the body shader.
    float cabin[4];  // i start, i end, k bottom, k top of the glass cabin
    float body[4];   // length cells, width cells, front lamp row k, rear lamp row k
    float style[4];  // lamp style, grille style, lattice j/k aspect at the front lamps (m per unit), unused

    // Sculpted shell (see bodyshell.h) instead of the smoothed cage: the render vertices
    // then carry rest positions in meters, and these are the lines the shader draws from.
    int shell = 0;
    float shellParam[20] = {};

    // Parts skinned to the lattice.
    struct Bind {
        int n[8];
        float w[8];
        float normalOffset = 0; // panel detail offset, follows its deformed surface normal
    };
    std::vector<Bind> sculptBind; // authored body mesh, bound to cage / moving panels
    std::vector<Bind> partBind;  // one per part triangle corner
    std::vector<float> partColor;  // r, g, b, material per triangle
    float bakedPaint[3] = {};      // the body colour the painted parts were made in (a car in another colour: the renderer swaps it)

    // The driver's seat (people.h draws the steering wheel, which turns, there; the cockpit view is from where a driver's
    // eyes would be): the hip point on the seat and the wheel's middle, each bound to the body like a part (it rides its
    // dents and flex). Rest positions in the car's frame (x ahead, y up, z to the right).
    struct Cockpit {
        bool on = false;
        Bind hip{}, wheel{};
        V3 hipL, wheelL;
        float wheelR = 0.17f, tilt = 0.45f;  // the wheel's radius (m) and lean back from upright (rad)
    } cockpit;
};
// Where a bound point is now (on the car's live nodes, world space).
V3 boundPoint(const Car& car, const VisualTemplate::Bind& b);

// Built once per vehicle type (cached).
const VisualTemplate& visualTemplate(const Car& car);
// For the render loop, which must not stall on a first sight: the template if it is built; otherwise null (the car is left out of
// the frame) while a worker thread builds it from a copy of the car. Set blockOnTemplates (screenshots, tests) to build it right here.
const VisualTemplate* tryVisualTemplate(const Car& car);
inline bool blockOnTemplates = false;
// Starts building a spec's template on a worker (a vehicle about to spawn), and asks whether it is done.
void prebuildVisual(const VehicleSpec& spec, const V3& color);
bool visualReady(const VehicleSpec& spec);

// Evaluation from node positions P (the car's nodes, in whatever frame the result is wanted in). Indices are
// appended for visible faces; glass faces go in both lists.
void evalBody(const Car& car, const std::vector<V3>& P, const VisualTemplate& t, std::vector<BodyDynamic>& dyn,
              std::vector<unsigned>& opaque, std::vector<unsigned>& glass);
void evalParts(const std::vector<V3>& P, const VisualTemplate& t, std::vector<Vtx>& out);
// Suspension arms, coil-overs, drive shafts and differentials from the live physics.
void buildRunningGear(const Car& car, std::vector<Vtx>& out);
// The frame rails and crossmembers under the floor (part of the running gear).
void buildFrameRails(const Car& car, std::vector<Vtx>& out);

// (A solid axle's tube and diff radii, kAxleTubeR and kAxleDiffR, are in softbody.h: the
// physics collides at the size it's drawn.)
// How far a hard landing drives a solid axle past its spec'd bump travel, squashing the
// bump stop. The frame, exhaust and tank over and around the axle are laid out to clear
// it, and tests/components.cpp holds the physics to it.
constexpr double kAxleCrush = 0.08;
// Highest point (body-local y) the centre line of a solid axle reaches.
inline double axleReach(const VehicleSpec& vs, bool front) {
    const double bump = front ? vs.bumpFront : vs.bumpRear;
    return vs.wheelRadius + (bump > 0 ? bump : 0.085 * vs.wheelRadius / 0.32) + kAxleCrush;  // (carbuild's default stop)
}
// Winch lines and tow straps in use.
void buildRopes(const Sim& sim, std::vector<Vtx>& out);
