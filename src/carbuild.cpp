// Vehicle construction: turns a VehicleSpec into nodes and beams.
//
//   1. Body lattice  Cells from the side-profile map. Every solid cell is a box of
//                    8 nodes and 28 beams, and its outer faces are the skin.
//   2. Frame         Floor rails, sills and crossmembers are stronger members, and
//                    the whole structure is held to its shape by the chassis frame
//                    (see ChassisFrame) so it doesn't wobble.
//   3. Weight        Structure, panels and every listed item go where they sit in a
//                    real car, so the balance and center of gravity come out right.
//   4. Powertrain    Engine and gearbox as one heavy block on rubber mounts.
//   5. Suspension    Knuckles on double wishbones, trailing arms or a solid axle;
//                    coil-overs, tie rods on a steering rack, anti-roll bars. Spring
//                    and damper rates come from the ride frequency and the actual
//                    linkage geometry.
//   6. Driveline     Prop shaft and half shafts. If one is torn off, the wheels it
//                    drives lose power.
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <utility>

#include "softbody.h"

namespace {

struct BeamSpec {
    double k, c, yield, brk;
};
// (Yield and break strains run ~40% over what the steel alone would give: the game's
// cars felt made of tin; this is about where a hard knock leaves a dent, not a wreck.)
// A body-on-frame truck's body tub (with its roof and pillars) holds its shape better than
// the ladder frame under it, which twists on the trail.
constexpr double kLadderTubStiffness = 0.6;
// A ladder frame's rails: each is shape-held this much stiffer than vs.frameStiffness (they
// carry the truck's bending; the twist is in the crossmembers).
constexpr double kRailStiffness = 3.5;
// What's welded to them (suspension brackets, coil towers, the engine's pads) is built for
// the suspension's hits, whatever the rails: about this much force before it bends (N).
constexpr double kBracketYield = 70000;
// The doors and sills crushed across, and the tail lengthwise: their share of what the
// lattice's members take before they set.
constexpr double kSideGive = 0.35;
constexpr double kSillGive = 0.45;
constexpr double kTailGive = 0.45;

const BeamSpec kSpec[BEAM_TYPE_COUNT] = {
    {1.6e5, 350, 0.056, 0.65},   // body
    {1.0e5, 240, 0.042, 0.65},   // cabin (softer: roofs crush)
    {3.0e6, 900, 0.060, 0.45},   // suspension link
    {3.0e4, 2000, 1e9, 1.00},    // coil-over (k, c come from the vehicle)
    {5.0e6, 900, 0.085, 0.80},   // knuckle / axle housing
    {3.2e5, 450, 0.045, 0.65},   // frame member
    {2.5e5, 1800, 0.08, 0.85},   // rubber mount: bottoms out on its snubber, then tears
    {3.0e6, 1200, 0.12, 1.00},   // engine block
    {6.0e6, 1200, 0.070, 0.50},  // tie rod / toe link
    {0, 0, 1e9, 0.40},           // drive shaft (no force; tears off past 40% stretch)
    {1.2e5, 170, 0.032, 0.80},   // panel sheet metal (0.8-1 mm steel over its inner frame)
    {0, 0, 1e9, 1e9},            // jounce bumper (k, c come from the vehicle)
};

// ---- Small linear algebra for the suspension kinematics -----------------------

using Row = std::array<double, 6>;

// Basis of the null space of J (rows of 6): the ways a rigid assembly can move
// without stretching any of its links.
std::vector<Row> nullSpace(std::vector<Row> J) {
    const int m = (int)J.size();
    int pivotCol[6], rank = 0;
    std::vector<bool> isPivot(6, false);
    for (int col = 0; col < 6 && rank < m; col++) {
        int best = rank;
        for (int r = rank + 1; r < m; r++)
            if (std::fabs(J[r][col]) > std::fabs(J[best][col])) best = r;
        if (std::fabs(J[best][col]) < 1e-9) continue;
        std::swap(J[rank], J[best]);
        double p = J[rank][col];
        for (double& v : J[rank]) v /= p;
        for (int r = 0; r < m; r++) {
            if (r == rank || J[r][col] == 0) continue;
            double f = J[r][col];
            for (int c = 0; c < 6; c++) J[r][c] -= f * J[rank][c];
        }
        pivotCol[rank++] = col;
        isPivot[col] = true;
    }
    std::vector<Row> basis;
    for (int free = 0; free < 6; free++) {
        if (isPivot[free]) continue;
        Row n{};
        n[free] = 1;
        for (int r = 0; r < rank; r++) n[pivotCol[r]] = -J[r][free];
        basis.push_back(n);
    }
    return basis;
}

// Velocity of point p on a rigid body moving with (v, omega) about origin o.
V3 pointVel(const Row& q, const V3& o, const V3& p) {
    V3 v(q[0], q[1], q[2]), w(q[3], q[4], q[5]);
    return v + cross(w, p - o);
}

}  // namespace

void Car::build(const VehicleSpec& vs, const V3& pos, double yaw, const V3& col) {
    spec = &vs;
    chains = vs.snowChains;
    gate = Gate();  // (a rebuilt body has its gate shut and latched: trunk.h)
    buildCount++;
    nodes.clear();
    beams.clear();
    hasSpare = false;
    spareNodes.clear();
    spareWeights.clear();
    tris.clear();
    quads.clear();
    cells.clear();
    cellIJK.clear();
    wheels.clear();
    axles.clear();
    pins.clear();
    panels.clear();
    quadPanel.clear();
    links.clear();
    frameBeams.clear();
    ladder.clear();
    ladderStations = 0;
    bodyMountBeams.clear();
    crossStations.clear();
    skidNodes.clear();
    engineNodes.clear();
    engineMounts.clear();
    bayBeams.clear();
    blockContacts.clear();
    grpFront.clear();
    grpRear.clear();
    grpLeft.clear();
    grpRight.clear();
    glassBursts.clear();
    extForce.clear();
    gearboxOut = -1;
    color = col;
    spawnPos = pos;
    spawnYaw = yaw;
    throttle = brake = handbrake = steerInput = steer = rackSteer = brakePressure = 0;
    gear = 1;
    rpm = vs.idleRpm;
    shiftTimer = 0;
    // The transfer case and lockers stay where the driver left them (a reset is a tow,
    // not a new truck), unless this is a different drivetrain.
    if (modeTransfer != vs.transfer || driveMode < 0 || driveMode >= (int)driveModes(vs.transfer).size()) {
        driveMode = defaultDriveMode(vs.transfer);
        lockFront = lockRear = false;
    }
    modeTransfer = vs.transfer;
    lockFront = lockFront && vs.lockerFront;
    lockRear = lockRear && vs.lockerRear;
    lowRange = !driveModes(vs.transfer).empty() && driveModes(vs.transfer)[driveMode].low;
    boost = 0;
    tcCut = 1;
    engineHealth = 1;
    engineDead = false;
    hydrolocked = false;
    tirePsi = vs.tirePsi;
    swayDisconnected = false;
    shaftsBroken = 0;
    sleeping = false;
    stillTime = 0;
    impactEnergy = damage = 0;
    shoveV = shoveP = V3();
    heldHere = 0;
    handBack = false;
    brokenBeams = 0;
    tirePops = 0;
    if (!grid) grid = std::make_unique<TriGrid>();
    if (!nodeGrid) nodeGrid = std::make_unique<TriGrid>();
    grid->stamp = -1;
    nodeGridStamp = -1;

    // =========================================================================
    // 1. Body lattice

    const int ncell = (int)vs.profile[0].size();
    const int NI = ncell + 1, NJ = vs.widthCells + 1, NK = (int)vs.profile.size() + 1;
    const double dz = vs.width / vs.widthCells;
    auto cellChar = [&](int i, int j, int k) -> char {
        if (i < 0 || i >= ncell || j < 0 || j >= NJ - 1 || k < 0 || k >= NK - 1) return '.';
        return vs.profile[k][i];
    };
    auto outerCol = [&](int j) { return j == 0 || j == NJ - 2; };
    auto solid = [&](int i, int j, int k) {
        char c = cellChar(i, j, k);
        if (c == '#' || c == 'G' || c == 'p') return true;  // ('p': a panel cell of a tube-frame vehicle: same structure, drawn skin)
        if (c == 'W' || c == 'w') return !outerCol(j);  // ('w': a wheel-well cell whose skin is drawn on a tube-frame vehicle)
        if (c == 'B') return outerCol(j);
        return false;
    };
    auto well = [&](int i, int j, int k) { return (cellChar(i, j, k) == 'W' || cellChar(i, j, k) == 'w') && outerCol(j); };

    std::vector<int> idx(NI * NJ * NK, -1);
    auto id = [&](int i, int j, int k) -> int& { return idx[(i * NJ + j) * NK + k]; };
    auto cornerIJK = [](int i, int j, int k, int bit, int& ci, int& cj, int& ck) {
        ci = i + (bit & 1);
        cj = j + ((bit >> 1) & 1);
        ck = k + ((bit >> 2) & 1);
    };

    std::vector<char> exists(NI * NJ * NK, 0);
    std::vector<int> colTop(NI, 0);
    for (int k = 0; k < NK - 1; k++)
        for (int i = 0; i < ncell; i++)
            for (int j = 0; j < NJ - 1; j++) {
                if (!solid(i, j, k)) continue;
                for (int bit = 0; bit < 8; bit++) {
                    int ci, cj, ck;
                    cornerIJK(i, j, k, bit, ci, cj, ck);
                    exists[(ci * NJ + cj) * NK + ck] = 1;
                    colTop[ci] = std::max(colTop[ci], ck);
                }
            }

    // Roof shape: the top layer of each glass cabin is raked front and rear. A short rake
    // just pulls the roof's edge in; a long one (a windshield or fastback spanning several
    // columns) brings the roof down along the glass line, column by column, so the cells
    // stay a sensible size.
    int roofCellK = -1;
    for (int k = 0; k < NK - 1; k++)
        if (vs.profile[k].find('G') != std::string::npos) roofCellK = k;
    std::vector<double> roofX(NI, NAN), roofY(NI, NAN);
    auto xAt = [&](int i) { return latticeX(vs, i); };
    if (roofCellK >= 0) {
        const std::string& row = vs.profile[roofCellK];
        const double belt = vs.layerY[roofCellK], top = vs.layerY[roofCellK + 1];
        for (int a = 0; a < ncell;) {
            if (row[a] != 'G') {
                a++;
                continue;
            }
            int b = a;
            while (b + 1 < ncell && row[b + 1] == 'G') b++;
            const double xf = xAt(a), xr = xAt(b + 1);
            const bool slopeF = vs.rakeFront > 0.6 * (xAt(a) - xAt(a + 1));
            const bool slopeR = vs.rakeRear > 0.6 * (xAt(b) - xAt(b + 1));
            for (int n = a; n <= b + 1; n++) {
                double x = xAt(n), h = 1;
                if (slopeF) h = std::min(h, (xf - x) / vs.rakeFront);
                if (slopeR) h = std::min(h, (x - xr) / vs.rakeRear);
                if (n == a && !slopeF) x -= vs.rakeFront;
                if (n == b + 1 && !slopeR) x += vs.rakeRear;
                roofX[n] = x;
                roofY[n] = belt + (top - belt) * clampd(h, 0.25, 1.0);
            }
            a = b + 1;
        }
    }

    auto nodePos = [&](int i, int j, int k) {
        double x = xAt(i), y = vs.layerY[k], z = -vs.width / 2 + dz * j;
        if (vs.outerCellWidth > 0 && NJ > 3) {
            // (Outer columns narrower, the rest evenly between.)
            const double inner = vs.width - 2 * vs.outerCellWidth;
            z = j == 0 ? -vs.width / 2 : j == NJ - 1 ? vs.width / 2 : -inner / 2 + inner * (j - 1) / (NJ - 3);
        }
        bool roof = (k == roofCellK + 1) && !std::isnan(roofX[i]);
        if (roof) {
            x = roofX[i];
            y = roofY[i];
            if (j == 0) z += vs.roofTuck;
            if (j == NJ - 1) z -= vs.roofTuck;
        } else if (k == colTop[i] && i < (int)vs.topLine.size() && vs.topLine[i] > 0) {
            y = vs.topLine[i];
        } else if (k == colTop[i]) {
            if (i == 0) y -= vs.noseDrop;
            if (i == 1) y -= vs.noseDrop * 0.35;
            if (i == NI - 1) y -= vs.tailDrop;
            if (i == NI - 2) y -= vs.tailDrop * 0.35;
        }
        if (k == 0 && (i == 0 || i == NI - 1)) y += i == 0 || vs.bumperLiftRear < 0 ? vs.bumperLift : vs.bumperLiftRear;
        if ((i == 0 || i == NI - 1) && (j == 0 || j == NJ - 1)) {
            z -= (z > 0 ? 1 : -1) * vs.cornerRound;
            x -= (x > 0 ? 1 : -1) * vs.cornerRound * 0.5;
        }
        if (i < (int)vs.bodyWidth.size()) z *= vs.bodyWidth[i];
        return V3(x, y, z);
    };

    const double cy = std::cos(yaw), sy = std::sin(yaw);
    auto toWorld = [&](const V3& l) { return V3(pos.x + l.x * cy + l.z * sy, pos.y + l.y, pos.z - l.x * sy + l.z * cy); };
    std::vector<V3> local;
    auto addNode = [&](const V3& lp, double m, bool collide) {
        Node n;
        n.p = toWorld(lp);
        n.m = m;
        n.collide = collide;
        nodes.push_back(n);
        local.push_back(lp);
        return (int)nodes.size() - 1;
    };

    for (int i = 0; i < NI; i++)
        for (int j = 0; j < NJ; j++)
            for (int k = 0; k < NK; k++)
                if (exists[(i * NJ + j) * NK + k]) id(i, j, k) = addNode(nodePos(i, j, k), 0, true);
    bodyNodes = (int)nodes.size();

    std::set<std::pair<int, int>> seen;
    double panelStrength = 1.0;  // the sheet being built (bumper covers stay stock)
    auto addBeam = [&](int a, int b, BeamType t) -> int {
        if (a < 0 || b < 0 || a == b) return -1;
        if (a > b) std::swap(a, b);
        if (!seen.insert({a, b}).second) return -1;
        Beam bm;
        bm.a = a;
        bm.b = b;
        bm.type = t;
        bool shell = t == BEAM_BODY || t == BEAM_CABIN || t == BEAM_FRAME;
        double stiff = shell ? vs.bodyStiffness : 1.0;
        bm.k = kSpec[t].k * stiff;
        bm.c = kSpec[t].c * std::sqrt(stiff);
        bm.yieldStrain = kSpec[t].yield * (shell ? vs.bodyStrength : 1.0);
        bm.breakStrain = kSpec[t].brk;
        if (t == BEAM_PANEL) {
            bm.k *= std::sqrt(panelStrength);
            bm.yieldStrain *= panelStrength;
        }
        bm.Lorig = bm.L0 = len(local[a] - local[b]);
        // A beam's stiffness here doesn't depend on its length, so a short one (where the
        // body shape pulls two nodes close, as noses and hoods do) would give way at a
        // fraction of the force a full-length member takes. Short beams yield and break
        // at the same movement, so the same force, as one of the lattice's ~30 cm members.
        if (shell && bm.Lorig < 0.3) {
            const double f = 0.3 / std::max(bm.Lorig, 0.005);
            bm.yieldStrain *= f;
            bm.breakStrain *= f;
        }
        beams.push_back(bm);
        return (int)beams.size() - 1;
    };
    auto corner = [&](int i, int j, int k, int bit) {
        int ci, cj, ck;
        cornerIJK(i, j, k, bit, ci, cj, ck);
        return id(ci, cj, ck);
    };
    for (int k = 0; k < NK - 1; k++)
        for (int i = 0; i < ncell; i++)
            for (int j = 0; j < NJ - 1; j++) {
                if (!solid(i, j, k)) continue;
                std::array<int, 8> cn;
                for (int bit = 0; bit < 8; bit++) cn[bit] = corner(i, j, k, bit);
                cells.push_back(cn);
                cellIJK.push_back({i, j, k});
            }

    // Axle stations: each run of 'W' cells in the bottom layer is an axle centered on its middle node.
    const std::vector<int> axleI = axleColumns(vs);

    // ---- 2. Frame members ----------------------------------------------------
    // Along the floor: two main rails (inner columns) and the sills (outer columns),
    // tied together by crossmembers at the axles and at both ends of the cabin.
    int cabI0 = ncell, cabI1 = -1;
    for (int k = 0; k < NK - 1; k++)
        for (int i = 0; i < ncell; i++)
            if (vs.profile[k][i] == 'G') {
                cabI0 = std::min(cabI0, i);
                cabI1 = std::max(cabI1, i + 1);
            }
    const bool ladderFrame = vs.railSpacing > 0 && vs.engineMass > 0;
    auto frameEdge = [&](int a, int b) {
        if (a < 0 || b < 0) return false;
        if (ladderFrame) return false;  // the floor is the body tub's sheet metal: the frame is its own
        const V3 &pa = local[a], &pb = local[b];
        // Both ends on the floor layer.
        if (std::fabs(pa.y - vs.layerY[0]) > 0.15 || std::fabs(pb.y - vs.layerY[0]) > 0.15) return false;
        bool along = std::fabs(pa.z - pb.z) < 1e-6 && std::fabs(pa.x - pb.x) > 1e-3;
        bool across = std::fabs(pa.x - pb.x) < 1e-6 && std::fabs(pa.z - pb.z) > 1e-3;
        if (along) return true;  // rails and sills
        if (across) {
            for (int ai : axleI)
                if (std::fabs(pa.x - xAt(ai)) < 1e-6) return true;
            if (cabI1 >= 0 && (std::fabs(pa.x - xAt(cabI0)) < 1e-6 || std::fabs(pa.x - xAt(cabI1)) < 1e-6)) return true;
            if (std::fabs(pa.x - xAt(0)) < 0.3 || std::fabs(pa.x - xAt(ncell)) < 0.3) return true;  // bumper beams
        }
        return false;
    };
    const bool isProp = vs.engineMass <= 0;
    for (int k = 0; k < NK - 1; k++)
        for (int i = 0; i < ncell; i++)
            for (int j = 0; j < NJ - 1; j++) {
                if (!solid(i, j, k)) continue;
                BeamType t = cellChar(i, j, k) == 'G' ? BEAM_CABIN : BEAM_BODY;
                // On a body-on-frame truck the cells round a wheel well are the fender and
                // the inner wheel-well liner: sheet metal hung off the tub (the frame under it
                // carries the suspension), so a tire forced up into it dents it.
                bool fender = false;
                if (ladderFrame)
                    for (int d = 0; d < 6; d++) {
                        const int di = d == 0 ? -1 : d == 1 ? 1 : 0, dj = d == 2 ? -1 : d == 3 ? 1 : 0, dk = d == 4 ? -1 : d == 5 ? 1 : 0;
                        fender |= well(i + di, j + dj, k + dk);
                    }
                for (int p = 0; p < 8; p++)
                    for (int q = p + 1; q < 8; q++) {
                        int a = corner(i, j, k, p), b = corner(i, j, k, q);
                        bool frame = !isProp && frameEdge(a, b);
                        int bi = addBeam(a, b, frame ? BEAM_FRAME : t);
                        if (bi >= 0 && fender && beams[bi].type == BEAM_BODY) {
                            beams[bi].yieldStrain *= 0.45;
                            beams[bi].breakStrain *= 0.7;
                        }
                        // Drawn under the floor: the main rails and the crossmembers (the
                        // outer sills are part of the body sides).
                        bool sill = std::fabs(std::fabs(local[a].z) - vs.width / 2) < 0.05 && std::fabs(local[a].z - local[b].z) < 1e-6;
                        if (bi >= 0 && frame && !sill) frameBeams.push_back(bi);
                    }
            }

    // Skin: every face of a solid cell that borders an empty cell.
    auto topCellLayer = [&](int i) {
        int t = 0;
        for (int k = 0; k < NK - 1; k++)
            for (int j = 0; j < NJ - 1; j++)
                if (solid(i, j, k)) t = k;
        return t;
    };
    const int lightLayerFront = topCellLayer(0), lightLayerRear = topCellLayer(ncell - 1);
    for (int k = 0; k < NK - 1; k++)
        for (int i = 0; i < ncell; i++)
            for (int j = 0; j < NJ - 1; j++) {
                if (!solid(i, j, k)) continue;
                V3 cellCenter;
                for (int bit = 0; bit < 8; bit++) cellCenter += local[corner(i, j, k, bit)] * 0.125;
                for (int axis = 0; axis < 3; axis++)
                    for (int side = 0; side < 2; side++) {
                        int d[3] = {0, 0, 0};
                        d[axis] = side ? 1 : -1;
                        int ni = i + d[0], nj = j + d[1], nk = k + d[2];
                        if (solid(ni, nj, nk)) continue;

                        int u = (axis + 1) % 3, v = (axis + 2) % 3;
                        const int loop[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                        int q[4];
                        float latc[4][3];
                        V3 faceCenter;
                        for (int c = 0; c < 4; c++) {
                            int bitv[3];
                            bitv[axis] = side;
                            bitv[u] = loop[c][0];
                            bitv[v] = loop[c][1];
                            q[c] = corner(i, j, k, bitv[0] | (bitv[1] << 1) | (bitv[2] << 2));
                            latc[c][0] = (float)(i + bitv[0]);
                            latc[c][1] = (float)(j + bitv[1]);
                            latc[c][2] = (float)(k + bitv[2]);
                            faceCenter += local[q[c]] * 0.25;
                        }
                        V3 n = cross(local[q[1]] - local[q[0]], local[q[2]] - local[q[0]]);
                        if (dot(n, faceCenter - cellCenter) < 0) {
                            std::swap(q[1], q[3]);
                            for (int a = 0; a < 3; a++) std::swap(latc[1][a], latc[3][a]);
                        }

                        uint8_t mat = MAT_PAINT;
                        bool outerJ = (j == 0 || j == NJ - 2);
                        if (axis == 2 && side == 0) mat = MAT_UNDER;
                        else if (well(ni, nj, nk)) mat = MAT_WELL;
                        else if (cellChar(i, j, k) == 'G') {
                            mat = axis == 2 ? MAT_PAINT : MAT_GLASS;
                            // Sloping top faces form the windshield and hatch glass too.
                            if (axis == 2 && side == 1 && faceCenter.y < vs.layerY.back() - 0.015 &&
                                (faceCenter.x > xAt(cabI0) - vs.rakeFront || faceCenter.x < xAt(cabI1) + vs.rakeRear))
                                mat = MAT_GLASS;
                        }
                        else if (axis == 2 && side == 1 && k == 0) mat = MAT_TRIM;  // pickup bed floor
                        else if (axis == 0 && side == 0 && i == 0 && k == lightLayerFront)
                            mat = outerJ ? MAT_HEADLIGHT : MAT_TRIM;
                        else if (axis == 0 && side == 1 && i == ncell - 1 && k == lightLayerRear && outerJ)
                            mat = MAT_TAILLIGHT;

                        SkinQuad sq;
                        for (int c = 0; c < 4; c++) {
                            sq.v[c] = q[c];
                            for (int a = 0; a < 3; a++) sq.lat[c][a] = latc[c][a];
                        }
                        sq.cell[0] = i;
                        sq.cell[1] = j;
                        sq.cell[2] = k;
                        sq.mat = mat;
                        sq.axis = (uint8_t)axis;
                        sq.side = (uint8_t)side;
                        sq.cellChar = cellChar(i, j, k);
                        sq.tri = (int)tris.size();
                        quads.push_back(sq);

                        const int split[2][3] = {{0, 1, 2}, {0, 2, 3}};
                        for (auto& s : split) {
                            Tri t;
                            for (int c = 0; c < 3; c++) t.v[c] = q[s[c]];
                            t.mat = mat;
                            for (int e = 0; e < 3; e++) t.rest[e] = len(local[t.v[(e + 1) % 3]] - local[t.v[e]]);
                            tris.push_back(t);
                        }
                    }
            }
    std::vector<char> onSkin(bodyNodes, 0);
    for (const Tri& t : tris)
        for (int c = 0; c < 3; c++) onSkin[t.v[c]] = 1;

    // ---- Lattice lookups used by everything bolted to the body -----------------

    // The solid cell containing p (or the nearest one), with p's position inside it (0..1 per axis).
    auto cellNear = [&](const V3& p, V3* frac) {
        int best = 0;
        double bestOut = 1e18;
        V3 bestF;
        for (size_t ci = 0; ci < cells.size(); ci++) {
            V3 mn = local[cells[ci][0]], mx = mn;
            for (int b = 1; b < 8; b++) {
                const V3& q = local[cells[ci][b]];
                mn = V3(std::min(mn.x, q.x), std::min(mn.y, q.y), std::min(mn.z, q.z));
                mx = V3(std::max(mx.x, q.x), std::max(mx.y, q.y), std::max(mx.z, q.z));
            }
            double out = 0;
            out += std::max(0.0, std::max(mn.x - p.x, p.x - mx.x));
            out += std::max(0.0, std::max(mn.y - p.y, p.y - mx.y));
            out += std::max(0.0, std::max(mn.z - p.z, p.z - mx.z));
            if (out < bestOut - 1e-9) {
                bestOut = out;
                best = (int)ci;
                // Cell corners: bit 0 = +i (toward the rear, -x), bit 1 = +j (+z), bit 2 = +k (+y).
                bestF = V3(clampd((mx.x - p.x) / std::max(1e-6, mx.x - mn.x), 0, 1),
                           clampd((p.z - mn.z) / std::max(1e-6, mx.z - mn.z), 0, 1),
                           clampd((p.y - mn.y) / std::max(1e-6, mx.y - mn.y), 0, 1));
            }
        }
        if (frac) *frac = bestF;
        return best;
    };
    auto addMassAt = [&](const V3& p, double kg) {
        V3 f;
        int ci = cellNear(p, &f);
        for (int b = 0; b < 8; b++) {
            double w = ((b & 1) ? f.x : 1 - f.x) * ((b & 2) ? f.y : 1 - f.y) * ((b & 4) ? f.z : 1 - f.z);
            nodes[cells[ci][b]].m += kg * w;
        }
    };
    // Bolts a node to the body: beams to the corners of the lattice cell around it.
    // That cell becomes part of the frame (a strut tower or subframe mount): it carries
    // spring and engine loads, so it's made several times stiffer and stronger.
    std::set<int> mountCells;
    auto anchor = [&](int node, BeamType t, double kScale = 1.0) {
        int ci = cellNear(local[node], nullptr);
        mountCells.insert(ci);
        for (int b = 0; b < 8; b++) {
            int c = cells[ci][b];
            if (len(local[c] - local[node]) < 0.04) continue;
            int bi = addBeam(node, c, t);
            if (bi < 0) continue;
            beams[bi].k *= kScale;
            beams[bi].yieldStrain = 0.15;  // welded bracket: the body around it gives way first
            beams[bi].breakStrain = 0.8;
        }
        return ci;
    };

    // ---- 3. Weight -------------------------------------------------------------
    // The welded structure is heaviest at the floor (rails, sills, tunnel) and
    // lightest at the roof; panels and glass are spread over the skin; every listed
    // item goes where it really sits.
    {
        std::vector<double> share(bodyNodes, 0);
        double sum = 0;
        for (int n = 0; n < bodyNodes; n++) {
            double y = local[n].y, top = vs.layerY.back(), bottom = vs.layerY[0];
            double t = clampd((y - bottom) / std::max(0.1, top - bottom), 0, 1);
            share[n] = isProp ? 1.0 : 3.0 - 2.0 * t;
            sum += share[n];
        }
        const double tub = ladderFrame ? std::max(0.0, vs.structureMass - vs.frameMass) : vs.structureMass;
        for (int n = 0; n < bodyNodes; n++) nodes[n].m = tub * share[n] / sum;
        int skinCount = 0;
        for (int n = 0; n < bodyNodes; n++) skinCount += onSkin[n];
        if (vs.panelMass > 0)
            for (int n = 0; n < bodyNodes; n++)
                if (onSkin[n]) nodes[n].m += vs.panelMass / skinCount;
        for (const MassItem& it : vs.items) {
            addMassAt(V3(it.x, it.y, it.z), it.kg);
            // The spare (tires.h): remember the nodes carrying it, so it can be drawn where it
            // sits and its weight taken off with it.
            const std::string nm = it.name;
            if (vs.wheelLoss && nm.find("spare") != std::string::npos && nm.find("wheel") != std::string::npos) {
                V3 f;
                const int ci = cellNear(V3(it.x, it.y, it.z), &f);
                spareNodes.clear();
                spareWeights.clear();
                for (int b = 0; b < 8; b++) {
                    spareNodes.push_back(cells[ci][b]);
                    spareWeights.push_back(((b & 1) ? f.x : 1 - f.x) * ((b & 2) ? f.y : 1 - f.y) * ((b & 4) ? f.z : 1 - f.z));
                }
                hasSpare = true;
                spare.mass = it.kg;
            }
            // The jack: a tool the player can use to lift a corner off the ground.
            if (nm.find("jack") != std::string::npos) {
                setJack(true);
            }
        }
    }

    // ---- 3b. Ladder frame (body-on-frame trucks) --------------------------------
    // Two boxed steel rails, each a box section (four corner nodes per station, braced
    // across every face and through the middle), tied by crossmembers. Between the axles
    // the rails run along the body's floor; over a live rear axle they kick up so it can
    // reach full bump under them. Everything that carries the truck's weight and the
    // road's loads bolts to the frame; the body tub sits on it on rubber mounts.
    std::vector<double> stationX;
    if (ladderFrame) {
        const double xNose = xAt(0) - 0.12, xTail = xAt(ncell) + 0.12;
        const int nst = std::max(4, (int)std::lround((xNose - xTail) / 0.42));
        for (int st = 0; st <= nst; st++) stationX.push_back(xNose + (xTail - xNose) * st / nst);
        ladderStations = (int)stationX.size();
        const double xr = axleI.size() >= 2 ? xAt(axleI.back()) : -1e9;
        const bool liveRear = vs.rearSusp == SOLID_AXLE && axleI.size() >= 2;
        // Rail bottom at the floor; over a live axle, high enough for the tube at full bump
        // (plus the bumper's squash) to pass under.
        const double base = vs.layerY[0];
        const double reach = vs.wheelRadius + (vs.bumpRear > 0 ? vs.bumpRear : 0.085) + 0.08 + 0.05;
        const double kick = liveRear ? std::max(0.0, reach - base) : 0.0;
        const double xf0 = axleI.size() >= 2 ? xAt(axleI.front()) : 1e9;
        auto bottomAt = [&](double x) {
            // The horns rise ahead of the front axle to the bumper's line (and the tail to
            // the rear one), so the frame never hangs below the approach and departure angles.
            const double liftRear = vs.bumperLiftRear < 0 ? vs.bumperLift : vs.bumperLiftRear;
            double y = base + std::max(vs.bumperLift * smoothstep(xf0 + 0.3, xNose, x), liftRear * smoothstep(xr - 0.5, xTail, x));
            if (!liveRear) return y;
            // Over a live axle: up 0.35 m ahead of its front clearance, and up from there on back.
            return std::max(y, base + kick * smoothstep(xr + 0.65, xr + 0.30, x));
        };
        const double hz = vs.railSpacing / 2, hw = vs.railWidth / 2, hh = vs.railHeight;
        // The frame's weight along it: heavier where the horns, the engine crossmember and
        // the suspension brackets are, so its center of mass falls where the welded
        // structure's would (the body tub's spread, which it replaces).
        double xm = 0, wsum = 0, xs = 0, ssum = 0;
        for (double x : stationX) xm += x / ladderStations;
        for (int n = 0; n < bodyNodes; n++) {
            const double t = clampd((local[n].y - vs.layerY[0]) / std::max(0.1, vs.layerY.back() - vs.layerY[0]), 0, 1);
            xs += (3.0 - 2.0 * t) * local[n].x;
            ssum += 3.0 - 2.0 * t;
        }
        xs /= std::max(ssum, 1e-9);
        double var = 0;
        for (double x : stationX) var += (x - xm) * (x - xm) / ladderStations;
        const double beta = (xs - xm) / std::max(var, 1e-6);
        std::vector<double> stW(ladderStations);
        for (int st = 0; st < ladderStations; st++) wsum += stW[st] = std::max(0.2, 1 + beta * (stationX[st] - xm));
        for (int st = 0; st < ladderStations; st++) {
            const double x = stationX[st], yb = bottomAt(x), m = vs.frameMass * stW[st] / wsum / 8;
            for (int side = -1; side <= 1; side += 2)
                for (int c = 0; c < 4; c++) {
                    // Corners: 0 inner bottom, 1 outer bottom, 2 inner top, 3 outer top.
                    const double z = side * (hz + ((c & 1) ? hw : -hw));
                    ladder.push_back(addNode(V3(x, yb + ((c & 2) ? hh : 0.0), z), m, true));
                }
        }
        auto L = [&](int st, int side, int c) { return ladder[st * 8 + (side > 0 ? 4 : 0) + c]; };
        auto railBeam = [&](int a, int b) {
            const int bi = addBeam(a, b, BEAM_FRAME);
            if (bi < 0) return bi;
            beams[bi].k = kSpec[BEAM_FRAME].k;  // boxed steel (the chassis frame does the rest)
            beams[bi].c = kSpec[BEAM_FRAME].c;
            // (As strong as a full station-length member however short it is: a beam's
            // stiffness here doesn't scale with length, and the section's own 7-20 cm sides
            // would otherwise fold at a couple of kN.)
            const double f = std::max(1.0, 0.42 / std::max(beams[bi].Lorig, 0.01));
            beams[bi].yieldStrain = kSpec[BEAM_FRAME].yield * f;
            beams[bi].breakStrain = kSpec[BEAM_FRAME].brk * f;
            return bi;
        };
        for (int side = -1; side <= 1; side += 2)
            for (int st = 0; st < ladderStations; st++) {
                // The section itself: its four sides and both diagonals.
                for (auto e : {std::make_pair(0, 1), std::make_pair(2, 3), std::make_pair(0, 2), std::make_pair(1, 3),
                               std::make_pair(0, 3), std::make_pair(1, 2)})
                    railBeam(L(st, side, e.first), L(st, side, e.second));
                if (st + 1 >= ladderStations) continue;
                // On to the next station: the four edges, every face's diagonals.
                for (int c = 0; c < 4; c++) {
                    const int bi = railBeam(L(st, side, c), L(st + 1, side, c));
                    if (bi >= 0) frameBeams.push_back(bi);
                }
                for (auto e : {std::make_pair(0, 1), std::make_pair(2, 3), std::make_pair(0, 2), std::make_pair(1, 3)}) {
                    railBeam(L(st, side, e.first), L(st + 1, side, e.second));
                    railBeam(L(st, side, e.second), L(st + 1, side, e.first));
                }
            }
        // Crossmembers: front bumper, front axle, gearbox, mid cab, ahead of and behind the
        // rear axle, rear bumper (a real ladder frame has about seven).
        std::vector<double> crossAt = {stationX.front(), stationX.back()};
        if (axleI.size() >= 2) {
            const double xf = xAt(axleI.front());
            crossAt.insert(crossAt.end(), {xf, vs.engineX - vs.engineLen / 2 - vs.gearboxLen, 0.5 * (xf + xr), xr + 0.7, xr - 0.45});
        }
        std::set<int> crossSt;
        for (double x : crossAt) {
            int best = 0;
            for (int st = 1; st < ladderStations; st++)
                if (std::fabs(stationX[st] - x) < std::fabs(stationX[best] - x)) best = st;
            crossSt.insert(best);
        }
        crossStations.assign(crossSt.begin(), crossSt.end());
        for (int st : crossSt) {
            for (auto e : {std::make_pair(0, 0), std::make_pair(2, 2), std::make_pair(0, 2), std::make_pair(2, 0)}) {
                const int bi = railBeam(L(st, -1, e.first), L(st, 1, e.second));
                if (bi >= 0 && e.first == e.second) frameBeams.push_back(bi);
            }
            // The crossmember's gusseted ends keep the rails square to each other (in plan
            // view): without them one rail could slide fore and aft against the other
            // ("matchboxing"), and the front axle with it, which steers the truck.
            const int to = st + 1 < ladderStations ? st + 1 : st - 1;
            for (int c : {0, 2})
                for (int side = -1; side <= 1; side += 2) {
                    const int bi = railBeam(L(st, side, c), L(to, -side, c));
                    if (bi >= 0) beams[bi].k *= 3;
                }
        }
    }
    [[maybe_unused]] auto nearestFrame = [&](const V3& p, int count) {
        std::vector<std::pair<double, int>> c;
        for (int n : ladder) c.push_back({len(local[n] - p), n});
        std::sort(c.begin(), c.end());
        std::vector<int> out;
        for (int k = 0; k < count && k < (int)c.size(); k++) out.push_back(c[k].second);
        return out;
    };

    // Frame-mounted nodes (suspension pick-ups, rack ends, spring towers) come next:
    // they're bolted rigidly to the body and move with the chassis frame.
    // Their mass comes out of the body cell they're bolted to (it's part of the same steel).
    std::set<int> bracketed;  // rail nodes something is welded to (that stretch is boxed in and gusseted)
    auto frameNode = [&](const V3& p, double kg) {
        int n = addNode(p, 0, false);
        if (ladderFrame) {
            // A bracket (or tower) welded to the nearest stretch of rail.
            kg = std::max(kg, 4.0);
            double avail = 0;
            // Welded to the near rail's section at the two nearest stations (it's held in shape
            // with that rail, so it can't pivot on its welds like a lever). One on the
            // centreline (a diff's mount) bridges both rails.
            std::vector<int> to;
            {
                std::vector<std::pair<double, int>> st;
                for (int k = 0; k < ladderStations; k++) st.push_back({std::fabs(stationX[k] - p.x), k});
                std::sort(st.begin(), st.end());
                const bool centre = std::fabs(p.z) < 0.05;
                for (int side : {0, 4}) {
                    if (!centre && side != (p.z > 0 ? 4 : 0)) continue;
                    for (int a = 0; a < 2 && a < (int)st.size(); a++)
                        for (int c = 0; c < 4; c++) to.push_back(ladder[st[a].second * 8 + side + c]);
                }
            }
            bracketed.insert(to.begin(), to.end());
            for (int f : to) {
                const int bi = addBeam(n, f, BEAM_FRAME);
                if (bi < 0) continue;
                beams[bi].k = kSpec[BEAM_FRAME].k * 2;
                beams[bi].yieldStrain = 0.15;  // the rail around it gives way first
                beams[bi].breakStrain = 0.8;
            }
            // (Its mass comes out of the structure around it, as on a unibody: the weight
            // the factory spec puts at each end stays there.)
            const int ci = cellNear(p, nullptr);
            for (int b = 0; b < 8; b++) avail += nodes[cells[ci][b]].m;
            const double take = std::min(kg, avail * 0.3);
            for (int b = 0; b < 8; b++) nodes[cells[ci][b]].m -= take * nodes[cells[ci][b]].m / avail;
            nodes[n].m = take;
            return n;
        }
        int ci = anchor(n, BEAM_FRAME, 4.0);
        kg = std::max(kg, 6.0);
        double avail = 0;
        for (int b = 0; b < 8; b++) avail += nodes[cells[ci][b]].m;
        double take = std::min(kg, avail * 0.3);
        for (int b = 0; b < 8; b++) nodes[cells[ci][b]].m -= take * nodes[cells[ci][b]].m / avail;
        nodes[n].m = take;
        return n;
    };

    const double R = vs.wheelRadius, hw = vs.width / 2;
    const double sizeK = R / 0.32;
    struct WheelPlan {
        double x, zc;
        int side;  // -1 left, +1 right
        bool front;
    };

    // Suspension pick-ups, built before the moving parts so they stay in the frame range.
    struct Pickups {
        int lf = -1, lr = -1, uf = -1, ur = -1, rack = -1, coil = -1, toe = -1;
        int trail = -1, panhard = -1, stop = -1, shackle = -1;
    };
    std::vector<std::array<Pickups, 2>> pick(axleI.size());
    std::vector<int> axleUpperPick(axleI.size(), -1), diffFrame(axleI.size(), -1), panhardFrame(axleI.size(), -1);
    auto suspType = [&](size_t ax) { return ax == 0 ? vs.frontSusp : vs.rearSusp; };
    auto tireWidth = [&](size_t ax) { return ax > 0 && vs.wheelWidthRear > 0 ? vs.wheelWidthRear : vs.wheelWidth; };
    auto wheelZ = [&](int side, size_t ax) {
        double track = ax == 0 ? vs.trackFront : vs.trackRear;
        return side * (track > 0 ? track / 2 : hw - 0.08 - tireWidth(ax) / 2);
    };
    // Double-wishbone layout for one corner (short-long arms). A tall knuckle and arms
    // that are nearly level at ride height keep the linkage far from locking straight at
    // full droop or bump, and give a modest camber gain.
    struct WishboneGeom {
        V3 lowerBJ, upperBJ, arm, lf, lr, uf, ur, rack, coil, seat;
    };
    // A MacPherson strut is the same linkage with the upper arm made infinitely long:
    // the knuckle slides along the strut, which pivots at the top mount. Here the "upper
    // ball joint" is the point of the strut at the top mount, held by two long links at
    // right angles to the strut, so it can only slide along it (and swivel).
    auto wishbone = [&](double xw, int s, double zc, double ww, bool strut, bool frontAxle) {
        WishboneGeom g;
        const double az = std::fabs(zc);
        const double yl = R - 0.17 * sizeK, yu = strut ? R + 0.40 * sizeK : R + 0.23 * sizeK;  // ball joint heights
        const double zl = az - 0.03, zu = strut ? az - 0.17 : az - 0.11;  // ball joints, just inboard of the wheel center
        const double zlp = zl - vs.armLower, zup = zu - vs.armUpper;  // inner pivots: lower arm 36 cm, upper 27 cm (long-travel cars: longer)
        g.lowerBJ = V3(xw, yl, s * zl);
        g.upperBJ = V3(xw - (strut ? 0.05 : 0.02), yu, s * zu);  // top joint a little behind: caster
        // Pivot axes tilted in side view so the arms' instant centre is on the car's side of
        // the wheel, above the ground: braking (and, on a driven rear, acceleration) forces then
        // partly hold the body up. Anti-dive at the front (~40% on a truck), anti-lift and
        // anti-squat behind. (Tilted the same way at both ends, the front's centre was ahead of
        // the wheel: ~60% pro-dive, and a hard stop pitched the nose down until the rear tires
        // went light.)
        const double tilt = frontAxle ? -1.0 : 1.0;
        g.lf = V3(xw + 0.24, yl + 0.01 + 0.02 * tilt, s * zlp);
        g.lr = V3(xw - 0.24, yl + 0.01 - 0.02 * tilt, s * zlp);
        g.uf = V3(xw + 0.14, yu + 0.03 - 0.025 * tilt, s * zup);
        g.ur = V3(xw - 0.14, yu + 0.03 + 0.025 * tilt, s * zup);
        g.seat = g.lowerBJ;
        if (strut) {
            const V3 a = norm(g.upperBJ - g.lowerBJ), in(0, 0, -s);
            const V3 p = norm(in - a * dot(in, a)), q = norm(cross(a, p));
            g.uf = g.upperBJ + p * 0.55 + q * 0.12;
            g.ur = g.upperBJ + p * 0.55 - q * 0.12;
            // Spring seat on the strut tube; the coil runs up the strut to the tower.
            g.seat = g.lowerBJ + (g.upperBJ - g.lowerBJ) * 0.42;
        }
        // Tie rod: outer end on the line through the ball joints, inner end on the line
        // through the inner pivot axes (seen from the front, at the steering arm's position
        // along the car, since the axes are tilted): then it swings with the arms and
        // bumps barely steer the wheel.
        const double ya = R - 0.06 * sizeK, xa = xw - 0.14;
        auto axisY = [&](const V3& f, const V3& r) { return f.y + (r.y - f.y) * (f.x - xa) / (f.x - r.x); };
        const double lowY = axisY(g.lf, g.lr), upY = axisY(g.uf, g.ur);
        const double fo = (ya - yl) / (yu - yl), fi = (ya - lowY) / (upY - lowY);
        g.arm = V3(xa, ya, s * (zl + fo * (zu - zl)));
        g.rack = V3(xa, ya, s * (zlp + fi * (zup - zlp)));
        g.coil = strut ? g.upperBJ + norm(g.upperBJ - g.lowerBJ) * 0.06 : V3(xw, R + 0.46 * sizeK, s * (zl - 0.20));
        // The Jesko's low clamshell needs an inboard spring package. This is an
        // equivalent inclined coil (the real car uses pushrods and rockers); the
        // linkage Jacobian below still sets its wheel rate and damping correctly.
        if (vs.model == 1 && !strut)
            g.coil = V3(xw - 0.12, R + 0.24 * sizeK, s * (zl - 0.40));
        // That rule is exact only for level pivot axes. Fine-tune the rack's height (the tie
        // rod's angle) against the actual linkage so the wheel doesn't steer as it moves.
        const V3 hubIn(xw, R, zc - s * ww / 2), hubOut(xw, R, zc + s * ww / 2);
        auto steerRate = [&](double rackY) {
            V3 rack(g.rack.x, rackY, g.rack.z);
            const V3 asmPts[5] = {hubIn, hubOut, g.lowerBJ, g.upperBJ, g.arm};
            V3 o;
            for (const V3& p : asmPts) o += p * 0.2;
            const std::pair<V3, V3> lk[5] = {{g.lowerBJ, g.lf}, {g.lowerBJ, g.lr}, {g.upperBJ, g.uf}, {g.upperBJ, g.ur}, {g.arm, rack}};
            std::vector<Row> J;
            for (const auto& l : lk) {
                V3 u = norm(l.first - l.second), ru = cross(l.first - o, u);
                J.push_back({u.x, u.y, u.z, ru.x, ru.y, ru.z});
            }
            std::vector<Row> ns = nullSpace(J);
            if (ns.size() != 1) return 0.0;
            double up = pointVel(ns[0], o, (hubIn + hubOut) * 0.5 - V3(0, R, 0)).y;
            return std::fabs(up) > 1e-9 ? ns[0][4] / up : 0.0;  // steer rate per meter of travel
        };
        double y0 = g.rack.y, y1 = y0 + 0.01, f0 = steerRate(y0), f1 = steerRate(y1);
        for (int it = 0; it < 12 && std::fabs(f1) > 1e-5 && std::fabs(f1 - f0) > 1e-12; it++) {
            double y2 = clampd(y1 - f1 * (y1 - y0) / (f1 - f0), yl - 0.05, yu);
            y0 = y1;
            f0 = f1;
            y1 = y2;
            f1 = steerRate(y1);
        }
        if (std::fabs(f1) < std::fabs(steerRate(g.rack.y))) g.rack.y = y1;
        return g;
    };
    if (!isProp) {
        for (size_t ax = 0; ax < axleI.size(); ax++) {
            const double xw = xAt(axleI[ax]);
            const SuspensionType st = suspType(ax);
            const bool front = ax == 0;
            const double ww = tireWidth(ax);
            for (int sIdx = 0; sIdx < 2; sIdx++) {
                const int s = sIdx == 0 ? -1 : 1;
                const double zc = wheelZ(s, ax), az = std::fabs(zc);
                Pickups& P = pick[ax][sIdx];
                if (st == WISHBONE || st == STRUT) {
                    WishboneGeom G = wishbone(xw, s, zc, ww, st == STRUT, front);
                    P.lf = frameNode(G.lf, 2.5);
                    P.lr = frameNode(G.lr, 2.5);
                    P.uf = frameNode(G.uf, 2.0);
                    P.ur = frameNode(G.ur, 2.0);
                    P.rack = frameNode(G.rack, 2.0);
                    P.coil = frameNode(G.coil, 2.0);
                } else if (st == TWIST_BEAM) {
                    P.coil = frameNode(V3(xw + 0.08, R * 0.75 + 0.05 + 0.32 * sizeK, s * (az - 0.16)), 2.0);
                } else if (front ? vs.leafFront : vs.leafSprings) {  // SOLID_AXLE on leaf springs
                    // The leaf rides on the tube (spring over axle), its eyes a little above the
                    // seat: the front one on a hanger, the rear on a shackle hanging from the frame.
                    // (The coil-over's top here is where the leaf's own rate pushes from: straight
                    // up over the seat.)
                    // (A steered axle's packs ride further in, under the frame rails, clear of the
                    // tires as they swing to full lock.)
                    const double zl = s * (front ? std::min(az - 0.20, std::max(vs.railSpacing * 0.5, az - 0.40)) : az - 0.20), ys = R + 0.10;
                    const double lh = front ? vs.leafHalfFront : vs.leafHalf;
                    // (A steered front axle: the fixed eye a little below the seat. Then as the body
                    // rolls the axle swings to steer out of the turn just enough to cancel the steering
                    // linkage's own bump steer; with the eye above it the truck wandered on the freeway.)
                    P.trail = frameNode(V3(xw + lh, ys + (front ? -0.025 : 0.05), zl), 2.5);
                    P.shackle = frameNode(V3(xw - lh - 0.03, ys + 0.05 + 0.10, zl), 2.0);
                    P.coil = frameNode(V3(xw - 0.06, ys + 0.30, zl), 2.0);
                    P.stop = frameNode(V3(xw, R + 0.31 * sizeK, s * (az - tireWidth(ax) / 2)), 2.0);
                } else {  // SOLID_AXLE (links run toward the middle of the car)
                    const double dir = front ? -1.0 : 1.0;
                    // Lower links level at ride height (their frame ends as high as the axle
                    // brackets): a level link barely moves the axle fore and aft as the body rolls,
                    // so the axle doesn't steer itself (sloped up to the frame, the outside end swung
                    // back in a turn or a lopsided stop and the axle steered the truck further in).
                    P.trail = frameNode(V3(xw + dir * vs.linkLength, R - 0.12, s * (az - 0.30)), 2.5);
                    P.coil = frameNode(V3(xw - dir * 0.06, R + (vs.coilTop > 0 ? vs.coilTop : 0.48 * sizeK), s * (az - 0.20)), 2.0);
                    // Jounce bumper on a bracket off the rail, straight over the tube's end
                    // (where the hub's inner node is: the stop lands on the tube itself).
                    P.stop = frameNode(V3(xw, R + 0.31 * sizeK, s * (az - tireWidth(ax) / 2)), 2.0);
                }
            }
            if (st == SOLID_AXLE) {
                const bool leaf = front ? vs.leafFront : vs.leafSprings;
                const double dir = front ? -1.0 : 1.0;
                // (On leaves, the stand-in for their resistance to wind-up pulls from under the
                // housing: the leaves themselves sit on top of it.)
                // (The upper link runs down toward the frame: with the level lower links its
                // instant centre is ahead of the axle, ~70% anti-squat and anti-lift.)
                axleUpperPick[ax] = frameNode(V3(xw + dir * (vs.upperLink > 0 ? vs.linkLength * vs.upperLink : 0.55), leaf ? R - 0.06 : R + 0.05, 0), 2.5);
                // (Leaf springs hold the axle sideways themselves, at the seats: the panhard's
                // place is taken by a level tie at seat height, standing in for the leaves' own
                // sideways stiffness, and not drawn.)
                panhardFrame[ax] = leaf ? frameNode(V3(xw - 0.06, R + 0.10, std::fabs(wheelZ(1, ax)) - 0.20 + 0.06), 2.5)
                                        : frameNode(V3(xw - 0.10, R + 0.16, hw - 0.40), 2.5);
                // The link brackets, coil towers and bump stops are welded to the same rails
                // and crossmember: tie them together so they can't shift against each other.
                // (Held only by the body lattice they moved 2-4 cm apart in a hard landing,
                // enough to let the housing roll over on its links.)
                std::vector<int> brackets = {axleUpperPick[ax], panhardFrame[ax]};
                for (int sIdx = 0; sIdx < 2; sIdx++)
                    for (int n : {pick[ax][sIdx].trail, pick[ax][sIdx].coil, pick[ax][sIdx].stop, pick[ax][sIdx].shackle})
                        if (n >= 0) brackets.push_back(n);
                for (size_t i = 0; i < brackets.size(); i++)
                    for (size_t j = i + 1; j < brackets.size(); j++) {
                        int bi = addBeam(brackets[i], brackets[j], BEAM_FRAME);
                        if (bi < 0) continue;
                        beams[bi].k = kSpec[BEAM_FRAME].k * 10;
                        beams[bi].c = kSpec[BEAM_FRAME].c * 3;
                        beams[bi].yieldStrain = 0.06;
                    }
            }
            // Differential on the body (independent suspension, driven axle).
            bool driven = vs.drive == VehicleSpec::AWD || (vs.drive == VehicleSpec::FWD) == front;
            bool transaxle = (front && vs.drive != VehicleSpec::RWD && vs.transverse) || (!front && vs.midEngine);
            bool onBody = st != SOLID_AXLE && driven && !transaxle;
            if (onBody) diffFrame[ax] = frameNode(V3(xw, R + 0.02, 0), 6.0);
        }
    }
    // On a ladder frame the engine and gearbox sit on brackets that reach in from the
    // rails to just under the block's mounts (as on the real truck): pads welded to the
    // frame, braced to both rails.
    std::vector<int> enginePads;
    if (ladderFrame && !isProp && !vs.transverse) {
        const V3 ca(1, 0, 0), up(0, 1, 0), across(0, 0, 1), c0(vs.engineX, vs.engineY, vs.engineZ);
        const double hl = vs.engineLen / 2, hwid = vs.engineWid / 2, hh = vs.engineHgt / 2;
        const V3 at[6] = {c0 + ca * -hl - up * hh - across * hwid, c0 + ca * hl - up * hh - across * hwid,
                          c0 + ca * -hl - up * hh + across * hwid, c0 + ca * hl - up * hh + across * hwid,
                          c0 - ca * (hl + vs.gearboxLen) - up * (hh * 0.75) - across * (hwid * 0.5),
                          c0 - ca * (hl + vs.gearboxLen) - up * (hh * 0.75) + across * (hwid * 0.5)};
        for (const V3& p : at) enginePads.push_back(frameNode(p - up * 0.06, 4.0));
    }
    frameNodes = (int)nodes.size();
    // =========================================================================
    // 4. Powertrain: engine (8 corners) and gearbox (4 tail corners) as one block.

    std::vector<int> gearboxTail;
    if (!isProp) {
        const V3 ca = vs.transverse ? V3(0, 0, 1) : V3(1, 0, 0);  // crank axis (block "front" end)
        const V3 up(0, 1, 0), across = cross(ca, up);
        const V3 c0(vs.engineX, vs.engineY, vs.engineZ);
        const double hl = vs.engineLen / 2, hwid = vs.engineWid / 2, hh = vs.engineHgt / 2;
        for (int b = 0; b < 8; b++) {
            V3 p = c0 + ca * ((b & 1) ? hl : -hl) + up * ((b & 2) ? hh : -hh) + across * ((b & 4) ? hwid : -hwid);
            engineNodes.push_back(addNode(p, vs.engineMass / 8, true));
        }
        // Gearbox: a narrower box behind the block (beside it, if transverse).
        for (int b = 0; b < 4; b++) {
            V3 p = c0 - ca * (hl + vs.gearboxLen) + up * ((b & 1) ? hh * 0.45 : -hh * 0.75) +
                   across * ((b & 2) ? hwid * 0.5 : -hwid * 0.5);
            int n = addNode(p, vs.gearboxMass / 4, true);
            engineNodes.push_back(n);
            gearboxTail.push_back(n);
        }
        for (size_t a = 0; a < engineNodes.size(); a++)
            for (size_t b = a + 1; b < engineNodes.size(); b++) {
                bool bothTail = a >= 8 && b >= 8;
                bool tailToRear = a < 8 && b >= 8 && !(a & 1);  // gearbox bolts to the block's rear face
                if (a < 8 || bothTail || tailToRear) addBeam(engineNodes[a], engineNodes[b], BEAM_ENGINE);
            }
        // Output: behind the gearbox tail (prop shaft) or at the final drive (transaxle).
        V3 tail;
        for (int n : gearboxTail) tail += local[n] * 0.25;
        V3 outPos = vs.transverse   ? V3(xAt(axleI[0]), R + 0.02, vs.engineZ - hl - vs.gearboxLen * 0.5)
                    : vs.midEngine ? V3(xAt(axleI.back()), R + 0.02, 0)  // transaxle: the diff is in the gearbox
                                   : tail - ca * 0.10 - up * 0.05;
        gearboxOut = addNode(outPos, 3.0, false);
        for (int n : gearboxTail) addBeam(gearboxOut, n, BEAM_ENGINE);
        // The transfer case's skid plate: across the frame under the back of the gearbox, three
        // points (nodes collide as 4 cm spheres) welded to the rails like any bracket, so a
        // ridge under the belly lifts the frame, not the engine on its mounts.
        if (ladderFrame && vs.skidDrop > 0 && vs.transfer != XFER_NONE) {
            const double xs = tail.x - 0.15;
            for (double z : {-0.18, 0.0, 0.18}) {
                const int n = frameNode(V3(xs, vs.layerY[0] - vs.skidDrop + 0.04, z), 4.0);
                nodes[n].collide = true;
                skidNodes.push_back(n);
            }
        }
        if (vs.transverse)
            for (int b = 0; b < 8; b++)
                if (!(b & 1)) addBeam(gearboxOut, engineNodes[b], BEAM_ENGINE);

        // Rubber mounts: each lower corner of the block (and the gearbox tail) to the
        // three nearest body nodes outside the block.
        auto insideBlock = [&](const V3& p, double margin) {
            V3 d = p - c0;
            return std::fabs(dot(d, ca)) < hl + vs.gearboxLen + margin && std::fabs(dot(d, up)) < hh + margin &&
                   std::fabs(dot(d, across)) < hwid + margin;
        };
        std::vector<int> mountFrom = {engineNodes[0], engineNodes[1], engineNodes[4], engineNodes[5],
                                      gearboxTail[0], gearboxTail[2]};
        for (size_t mi = 0; mi < mountFrom.size(); mi++) {
            const int m = mountFrom[mi];
            std::vector<std::pair<double, int>> cand;
            // (On a ladder frame, each mount sits on its bracket pad under the block, plus
            // the nearest rail to keep the block from shuffling sideways.)
            if (ladderFrame && mi < enginePads.size()) {
                cand.push_back({0.0, enginePads[mi]});
                // ...and on to the neighbouring pad (fore and aft for the engine's, across at
                // the gearbox): rubber blocks take shear too, so a knock can't surge the block
                // off its pads.
                static const int other[6] = {1, 0, 3, 2, 5, 4};
                cand.push_back({0.0, enginePads[other[mi]]});
                double best = 1e9;
                int near = -1;
                for (int n : ladder)
                    if (!insideBlock(local[n], 0.02) && len(local[n] - local[m]) < best) best = len(local[n] - local[m]), near = n;
                if (near >= 0) cand.push_back({best, near});
            } else if (ladderFrame)
                for (int n : ladder) {
                    if (insideBlock(local[n], 0.02)) continue;
                    cand.push_back({len(local[n] - local[m]), n});
                }
            else
                for (int n = 0; n < bodyNodes; n++) {
                    if (insideBlock(local[n], 0.02)) continue;
                    cand.push_back({len(local[n] - local[m]), n});
                }
            std::sort(cand.begin(), cand.end());
            for (int k = 0; k < 3 && k < (int)cand.size(); k++) {
                int bi = addBeam(m, cand[k].second, BEAM_MOUNT);
                if (bi < 0) continue;
                // A mount's rubber gives ~2.5 cm before its snubber bottoms, and tears at ~8,
                // however long the bracket: short ones (on a frame rail right under the
                // block) mustn't tear in a hard landing.
                beams[bi].yieldStrain = std::max(beams[bi].yieldStrain, 0.025 / beams[bi].Lorig);
                beams[bi].breakStrain = std::max(beams[bi].breakStrain, 0.08 / beams[bi].Lorig);
                // (On a frame, the mounts' snubbers carry the block through a landing. A unibody
                // car's subframe mounts give, and soak up some of a crash, as built.)
                beams[bi].snub = ladderFrame;
                engineMounts.push_back(bi);
            }
        }
        // Torque rods ("dog bones"): a transverse engine's block takes the whole drive torque
        // (it holds the final drive), which would twist it off soft rubber mounts. Stiff links
        // from each corner fore and aft to the body hold it, as on real front-drive cars.
        if (vs.transverse) {
            for (int b = 0; b < 8; b++) {
                const V3 p = local[engineNodes[b]];
                const double side = (p.x - c0.x) > 0 ? 1 : -1;  // ahead of or behind the crank
                int best = -1;
                double bestD = 1e9;
                for (int n = 0; n < bodyNodes; n++) {
                    if (insideBlock(local[n], 0.02) || (local[n].x - p.x) * side < 0.05) continue;
                    double d = len(local[n] - p);
                    if (d < bestD) {
                        bestD = d;
                        best = n;
                    }
                }
                int bi = addBeam(engineNodes[b], best, BEAM_MOUNT);
                if (bi < 0) continue;
                beams[bi].k *= 5;
                beams[bi].c *= 2;
                beams[bi].yieldStrain = 0.2;
                beams[bi].breakStrain = 0.7;
                engineMounts.push_back(bi);
            }
        }
        // Body nodes around the block push it (and get pushed) when the bay crumples.
        // Each one is kept out through the face it sits outside of; nodes that sit
        // inside the block's volume at rest (interior lattice) are ignored.
        for (int n = 0; n < bodyNodes; n++) {
            V3 d = local[n] - c0;
            double e[3] = {dot(d, ca), dot(d, up), dot(d, across)};
            double half[3] = {hl, hh, hwid};
            if (e[0] < 0) half[0] = hl + vs.gearboxLen;
            int face = -1;
            double worst = 0;
            for (int a = 0; a < 3; a++) {
                double out = std::fabs(e[a]) - half[a];
                if (out > worst) {
                    worst = out;
                    face = a * 2 + (e[a] > 0 ? 1 : 0);
                }
            }
            // Only the outer skin, and only if there's real clearance at rest (interior lattice
            // nodes are bookkeeping, not structure the engine could hit).
            if (face >= 0 && worst > 0.04 && worst < 0.7 && onSkin[n]) blockContacts.push_back({n, (int8_t)face});
        }
        // Engine bay: the body structure around the block. Crushing it hurts the engine.
        for (size_t bi = 0; bi < beams.size(); bi++) {
            const Beam& b = beams[bi];
            if (b.a >= bodyNodes || b.b >= bodyNodes) continue;
            if (insideBlock(local[b.a], 0.35) && insideBlock(local[b.b], 0.35)) bayBeams.push_back((int)bi);
        }
    }

    // =========================================================================
    // 5. Suspension

    const double g = 9.81;
    // Sprung mass and its center (everything so far rides on the springs).
    double sprungM = 0, sprungX = 0;
    for (const Node& n : nodes) sprungM += n.m;
    for (size_t n = 0; n < nodes.size(); n++) sprungX += local[n].x * nodes[n].m;
    sprungX /= std::max(sprungM, 1e-9);

    struct Assembly {
        std::vector<int> nodes;
        std::vector<int> wheelIdx;   // wheels on it
        std::vector<int> links;      // beams tying it to the body
        std::vector<int> springs;    // coil-over per wheel
    };
    std::vector<Assembly> assemblies;

    for (size_t ax = 0; ax < axleI.size() && !isProp; ax++) {
        const double xw = xAt(axleI[ax]);
        const SuspensionType st = suspType(ax);
        const bool front = ax == 0;
        const bool driven = vs.drive == VehicleSpec::AWD || (vs.drive == VehicleSpec::FWD) == front;
        const double ww = tireWidth(ax);
        Axle axle;
        axle.front = front;
        axle.diffNode = diffFrame[ax];

        auto newWheel = [&](int s, double zc) {
            Wheel wh;
            wh.front = front;
            wh.left = s < 0;
            wh.driven = driven;
            wh.R = R;
            wh.rimR = vs.rimRadius > 0 ? vs.rimRadius : R * 0.65;
            wh.width = ww;
            // Tire mass is concentrated near its tread; the rim is closer to the axle.
            // A 0.65 inertia factor approximates that distribution, in kg m^2.
            wh.I = 0.65 * vs.wheelMass * R * R;
            wh.item.R = wh.R, wh.item.rimR = wh.rimR, wh.item.width = ww;
            wh.item.mass = vs.wheelMass, wh.item.I = wh.I;
            wh.item.style = vs.wheelStyle, wh.item.from = vs.id;
            wh.brakeMax = front ? vs.brakeFront : vs.brakeRear;
            double wm = vs.wheelMass / 2 + vs.hubMass * 0.25;
            wh.inner = addNode(V3(xw, R, zc - s * ww / 2), wm, false);
            wh.outer = addNode(V3(xw, R, zc + s * ww / 2), wm, false);
            return wh;
        };
        auto link = [&](Wheel* wh, int a, int b, BeamType t, LinkKind kind, int wheelIndex) {
            int bi = addBeam(a, b, t);
            if (bi < 0) return -1;
            links.push_back({bi, (uint8_t)kind, (int8_t)wheelIndex});
            if (wh) wh->mounts.push_back(bi);
            return bi;
        };
        auto rigid = [&](const std::vector<int>& set, BeamType t) {
            for (size_t a = 0; a < set.size(); a++)
                for (size_t b = a + 1; b < set.size(); b++) addBeam(set[a], set[b], t);
        };

        if (st == WISHBONE || st == STRUT || st == TWIST_BEAM) {
            for (int sIdx = 0; sIdx < 2; sIdx++) {
                const int s = sIdx == 0 ? -1 : 1;
                const double zc = wheelZ(s, ax), az = std::fabs(zc);
                const Pickups& P = pick[ax][sIdx];
                Wheel wh = newWheel(s, zc);
                const int wi = (int)wheels.size();
                Assembly as;
                if (st == WISHBONE || st == STRUT) {
                    const bool strut = st == STRUT;
                    WishboneGeom G = wishbone(xw, s, zc, ww, strut, front);
                    wh.lower = addNode(G.lowerBJ, vs.hubMass * 0.2 + 2.0, false);
                    wh.top = addNode(G.upperBJ, vs.hubMass * 0.15 + 1.0, false);
                    wh.arm = addNode(G.arm, vs.hubMass * 0.15 + 0.5, false);
                    as.nodes = {wh.inner, wh.outer, wh.lower, wh.top, wh.arm};
                    int seat = wh.lower;
                    if (strut) {
                        seat = addNode(G.seat, vs.hubMass * 0.1 + 1.0, false);
                        as.nodes.push_back(seat);
                    }
                    rigid(as.nodes, BEAM_KNUCKLE);
                    const LinkKind upper = strut ? LINK_STRUT_GUIDE : LINK_UPPER_ARM;
                    for (int bi : {link(&wh, wh.lower, P.lf, BEAM_ARM, LINK_LOWER_ARM, wi),
                                   link(&wh, wh.lower, P.lr, BEAM_ARM, LINK_LOWER_ARM, wi),
                                   link(&wh, wh.top, P.uf, BEAM_ARM, upper, wi),
                                   link(&wh, wh.top, P.ur, BEAM_ARM, upper, wi)})
                        as.links.push_back(bi);
                    wh.tieRod = link(&wh, wh.arm, P.rack, BEAM_TIEROD, front ? LINK_TIE_ROD : LINK_TOE_LINK, wi);
                    as.links.push_back(wh.tieRod);
                    wh.kpLow = local[wh.lower];
                    wh.kpUp = local[wh.top];
                    wh.armRest = local[wh.arm];
                    wh.rackRest = local[P.rack];
                    wh.spring = addBeam(seat, P.coil, BEAM_SPRING);
                } else {
                    // Trailing arm: pivots on two bushings ahead of the wheel; the hub is welded to it.
                    // A boxed arm: nodes above and below its line so it resists twisting.
                    wh.top = addNode(V3(xw, R + 0.17, zc - s * 0.10), vs.hubMass * 0.25 + 1.0, false);
                    int pivOut = addNode(V3(xw + 0.45, R * 0.75 + 0.05, s * (az - 0.12)), 2.5, false);
                    int pivIn = addNode(V3(xw + 0.45, R * 0.75 + 0.05, s * (az - 0.34)), 2.5, false);
                    int armLow = addNode(V3(xw + 0.05, R - 0.13, zc - s * 0.12), 2.0, false);
                    int armUp = addNode(V3(xw + 0.30, R * 0.75 + 0.17, s * (az - 0.22)), 1.5, false);
                    // Spring seat level with the pivots, so it moves straight up and down and
                    // the spring stays upright through the whole travel.
                    int seat = addNode(V3(xw + 0.08, R * 0.75 + 0.05, s * (az - 0.16)), 1.5, false);
                    wh.lower = pivOut;
                    wh.arm = pivIn;
                    as.nodes = {wh.inner, wh.outer, wh.top, pivOut, pivIn, armLow, armUp, seat};
                    rigid(as.nodes, BEAM_KNUCKLE);
                    // Bushings: each pivot is held to the body in every direction.
                    // (All eight corners of its cell, so it's pinned in every direction.)
                    for (int piv : {pivOut, pivIn}) {
                        int ci = cellNear(local[piv], nullptr);
                        mountCells.insert(ci);
                        for (int b = 0; b < 8; b++)
                            if (len(local[cells[ci][b]] - local[piv]) > 0.04)
                                as.links.push_back(link(&wh, piv, cells[ci][b], BEAM_ARM, LINK_BUSHING, wi));
                    }
                    wh.spring = addBeam(seat, P.coil, BEAM_SPRING);
                }
                wh.assembly = as.nodes;
                wh.mounts.push_back(wh.spring);
                as.wheelIdx = {wi};
                as.springs = {wh.spring};
                axle.wheel[sIdx] = wi;
                wheels.push_back(wh);
                assemblies.push_back(as);
            }
        } else {  // SOLID_AXLE
            Assembly as;
            const double am = vs.axleMass;
            // Links run toward the middle of the car; the pinion points that way too. A front
            // solid axle steers: the hubs sit on knuckles that turn on kingpins at the tube
            // ends (two shared nodes make the hinge), pushed by tie rods from the axle.
            const double dir = front ? -1.0 : 1.0;
            const bool steered = front;
            int diff = addNode(V3(xw, R, 0), am * 0.30, false);
            int pinion = addNode(V3(xw + dir * 0.20, R + 0.02, 0), am * 0.10, false);
            int dtop = addNode(V3(xw, R + 0.14, 0), am * 0.06, false);
            axle.housing = {diff, pinion, dtop};
            const bool leafAxle = front ? vs.leafFront : vs.leafSprings;
            int dbot = -1;  // (leaf springs: under the diff, where the wind-up stand-in pulls)
            if (leafAxle) {
                dbot = addNode(V3(xw, R - 0.12, 0), am * 0.04, false);
                axle.housing.push_back(dbot);
            }
            axle.diffNode = diff;
            as.nodes = axle.housing;
            int seat[2], lowMount[2];
            std::vector<std::vector<int>> knuckle(2);
            std::vector<std::array<int, 4>> steerNodes(2);  // kpLow, kpUp, arm, rack
            for (int sIdx = 0; sIdx < 2; sIdx++) {
                const int s = sIdx == 0 ? -1 : 1;
                const double zc = wheelZ(s, ax), az = std::fabs(zc);
                Wheel wh = newWheel(s, zc);
                nodes[wh.inner].m += am * 0.07;
                nodes[wh.outer].m += am * 0.03;
                // Spring seat behind and link bracket ahead of the tube: the housing is a
                // solid 3D piece, so the wheels can't toe in or out.
                const double seatZ = steered && vs.leafFront ? std::min(az - 0.20, std::max(vs.railSpacing * 0.5, az - 0.40)) : az - 0.20;  // (under the leaf: see the pick-ups)
                seat[sIdx] = addNode(V3(xw - dir * 0.06, R + vs.axleSeat, s * seatZ), am * 0.07, false);
                lowMount[sIdx] = addNode(V3(xw + dir * 0.12, R - 0.12, s * (az - 0.30)), am * 0.07, false);
                wh.top = seat[sIdx];
                wh.lower = lowMount[sIdx];
                std::vector<int> mine;
                if (!steered) {
                    // Hub carrier: braces the hub above and below, right at the wheel, so the
                    // hub is held in every direction and not just along the axle tube.
                    int carUp = addNode(V3(xw - dir * 0.04, R + 0.13, s * (az - 0.06)), am * 0.03, false);
                    int carLow = addNode(V3(xw + dir * 0.06, R - 0.12, s * (az - 0.06)), am * 0.03, false);
                    mine = {wh.inner, wh.outer, seat[sIdx], lowMount[sIdx], carUp, carLow};
                } else {
                    // Kingpin (a little caster: the top leans back), steering arm behind the
                    // axle, and the tie rod's fixed end on the housing.
                    int kpLow = addNode(V3(xw, R - 0.13, s * (az - 0.06)), am * 0.03 + 3.0, false);
                    int kpUp = addNode(V3(xw - 0.035, R + 0.17, s * (az - 0.09)), am * 0.03 + 3.0, false);
                    int arm = addNode(V3(xw + dir * 0.17, R - 0.05, s * (az - 0.26)), am * 0.02 + 1.5, false);
                    // The tie rod's fixed end: square to the steering arm at straight ahead (forward
                    // and inboard of the arm's end), so the arm can swing well past full lock before
                    // it lines up with the rod. (Run straight across to the middle of the axle, the rod
                    // started 40 degrees off the arm: the inside wheel reached that fold near full lock
                    // and could snap over it and jam a few degrees off straight.)
                    const V3 armEnd(xw + dir * 0.17, R - 0.05, s * (az - 0.26)), kpMid(xw, R - 0.05, s * (az - 0.06));
                    const V3 armDir = norm(armEnd - kpMid), perp = norm(V3(-armDir.z, 0, armDir.x));
                    V3 rackAt = armEnd + perp * 0.28;
                    if (std::fabs(rackAt.z) > std::fabs(armEnd.z)) rackAt = armEnd - perp * 0.28;  // (the inboard side)
                    int rack = addNode(rackAt, am * 0.02 + 1.5, false);
                    knuckle[sIdx] = {wh.inner, wh.outer, kpLow, kpUp, arm};
                    steerNodes[sIdx] = {kpLow, kpUp, arm, rack};
                    mine = {seat[sIdx], lowMount[sIdx], kpLow, kpUp};
                    wh.lower = kpLow;
                    wh.top = kpUp;
                    wh.arm = arm;
                }
                for (int n : mine) {
                    axle.housing.push_back(n);
                    as.nodes.push_back(n);
                }
                axle.wheel[sIdx] = (int)wheels.size();
                as.wheelIdx.push_back((int)wheels.size());
                wheels.push_back(wh);
            }
            rigid(as.nodes, BEAM_KNUCKLE);
            // (The tie rod's fixed end is a bracket on the tube, braced to the kingpin and the
            // housing beside it, not welded to every point of the axle: that many stiff beams on
            // one light node needed more weight than a tube chassis has to lend.)
            if (steered)
                for (int sIdx = 0; sIdx < 2; sIdx++) {
                    const int rack = steerNodes[sIdx][3];
                    for (int o : {steerNodes[sIdx][0], steerNodes[sIdx][1], lowMount[sIdx], seat[sIdx], diff, pinion}) addBeam(rack, o, BEAM_KNUCKLE);
                    axle.housing.push_back(rack);
                    as.nodes.push_back(rack);
                }
            // What a solid axle drags over the rocks: the diff's pumpkin, the lowest thing on the
            // truck (ground clearance is measured under it), and the tubes either side. Nodes
            // collide as 4 cm spheres: the pumpkin node sits that far above its bottom, ~12.5 cm
            // below the axle's centre (an 8-8.8 in ring gear's housing: Toyota quotes 9.4 in under
            // the Tacoma's on its loaded 265/70R16s), and a tube node halfway out to each hub.
            // (Each is braced to a few of the housing's points, not welded to all of them: a
            // light node on that many stiff beams would have to borrow weight off the body to
            // stay stable, and the axle would get heavier.)
            {
                const double nodeR = 0.04;
                auto brace = [&](int n, std::initializer_list<int> to) {
                    for (int o : to) {
                        const int bi = addBeam(n, o, BEAM_KNUCKLE);
                        if (bi >= 0) beams[bi].k *= 0.1, beams[bi].c *= 0.3;
                    }
                };
                // (The tube ends: the hubs on a rigid axle; the lower kingpins on a steered one,
                // whose hubs turn with the knuckles.)
                const int hubL = steered ? steerNodes[0][0] : wheels[axle.wheel[0]].inner, hubR = steered ? steerNodes[1][0] : wheels[axle.wheel[1]].inner;
                const int pump = addNode(V3(xw, R - kAxleDiffR + nodeR, 0), am * 0.05, true);
                brace(pump, {diff, pinion, dtop, hubL, hubR});
                nodes[diff].m -= am * 0.11;
                axle.housing.push_back(pump);
                as.nodes.push_back(pump);
                for (int sIdx = 0; sIdx < 2; sIdx++) {
                    const int s = sIdx == 0 ? -1 : 1;
                    const int hub = sIdx == 0 ? hubL : hubR;
                    const int tube = addNode(V3(xw, R - (kAxleTubeR - nodeR), s * std::fabs(wheelZ(s, ax)) * 0.5), am * 0.03, true);
                    // (And to the pinion, ahead of the tube: every other brace lies close to one
                    // plane through the tube's line, so a node held only by them could swing across
                    // it with almost nothing pushing back or damping it. The rock buggy's tube nodes
                    // rang there at 5 Hz, ~1 mm, for good after it spawned.)
                    brace(tube, {diff, hub, seat[sIdx], lowMount[sIdx], pump, pinion});
                    axle.housing.push_back(tube);
                    as.nodes.push_back(tube);
                }
            }
            const bool leaf = front ? vs.leafFront : vs.leafSprings;
            for (int sIdx = 0; sIdx < 2; sIdx++) {
                Wheel& wh = wheels[axle.wheel[sIdx]];
                wh.assembly = as.nodes;
                if (steered) {
                    rigid(knuckle[sIdx], BEAM_KNUCKLE);
                    wh.assembly.insert(wh.assembly.end(), {wh.inner, wh.outer, steerNodes[sIdx][2]});
                    const int kpLow = steerNodes[sIdx][0], kpUp = steerNodes[sIdx][1], arm = steerNodes[sIdx][2], rack = steerNodes[sIdx][3];
                    wh.tieRod = link(&wh, arm, rack, BEAM_TIEROD, LINK_TIE_ROD, axle.wheel[sIdx]);
                    as.links.push_back(wh.tieRod);
                    wh.kpLow = local[kpLow];
                    wh.kpUp = local[kpUp];
                    wh.armRest = local[arm];
                    wh.rackRest = local[rack];
                    // The tube from the diff out to the kingpin: if it snaps, the wheel is off.
                    for (const auto& bi : {std::make_pair(diff, kpLow), std::make_pair(seat[sIdx], kpUp), std::make_pair(lowMount[sIdx], kpLow)}) {
                        int a2 = std::min(bi.first, bi.second), b2 = std::max(bi.first, bi.second);
                        for (size_t k = 0; k < beams.size(); k++)
                            if (beams[k].a == a2 && beams[k].b == b2) wh.mounts.push_back((int)k);
                    }
                }
                if (leaf) {
                    // The leaf's front half: from its hanger to the seat, it holds the axle fore
                    // and aft (and with the other side, square to the truck).
                    const int lf = link(&wh, seat[sIdx], pick[ax][sIdx].trail, BEAM_ARM, LINK_LEAF, axle.wheel[sIdx]);
                    as.links.push_back(lf);
                    wh.shackle = pick[ax][sIdx].shackle;
                } else {
                    as.links.push_back(link(&wh, lowMount[sIdx], pick[ax][sIdx].trail, BEAM_ARM, LINK_TRAILING, axle.wheel[sIdx]));
                }
                wh.spring = addBeam(seat[sIdx], pick[ax][sIdx].coil, BEAM_SPRING);
                // The bumper lands on the tube's centre line: it pushes straight down through
                // the tube and can't roll the housing over on its links (a stop pushing on the
                // coil seat behind the tube did).
                wh.bumpStop = addBeam(wh.inner, pick[ax][sIdx].stop, BEAM_BUMPSTOP);
                wh.mounts.push_back(wh.spring);
                as.springs.push_back(wh.spring);
                if (!steered) {
                    // The tube from the diff out to this hub: if it snaps, the wheel is off.
                    for (const auto& bi : {std::make_pair(diff, wh.inner), std::make_pair(seat[sIdx], wh.inner),
                                           std::make_pair(lowMount[sIdx], wh.inner)}) {
                        int a2 = std::min(bi.first, bi.second), b2 = std::max(bi.first, bi.second);
                        for (size_t k = 0; k < beams.size(); k++)
                            if (beams[k].a == a2 && beams[k].b == b2) wh.mounts.push_back((int)k);
                    }
                }
            }
            // Upper link (holds the axle against wind-up) and panhard rod (holds it sideways).
            // On leaf springs both are the leaves themselves: the packs clamped to the tube
            // resist its wind-up (softly: under a hard launch the axle wraps), and their width
            // holds it sideways. Neither is drawn.
            if (leaf) {
                // (Leaves clamped along the seat: stiff against wind-up, though they do wrap a
                // few degrees under a hard launch, and they spring back rather than bend.)
                const int wrap = link(nullptr, dbot, axleUpperPick[ax], BEAM_ARM, LINK_BUSHING, -1);
                if (wrap >= 0) beams[wrap].yieldStrain = 0.25, beams[wrap].c *= 6;  // (interleaf friction damps the wrap)
                as.links.push_back(wrap);
                as.links.push_back(link(nullptr, seat[0], panhardFrame[ax], BEAM_ARM, LINK_BUSHING, -1));
            } else {
                as.links.push_back(link(nullptr, dtop, axleUpperPick[ax], BEAM_ARM, LINK_TRAILING, -1));
                as.links.push_back(link(nullptr, seat[0], panhardFrame[ax], BEAM_ARM, LINK_PANHARD, -1));
            }
            axle.diffNodes = axle.housing;
            assemblies.push_back(as);
        }
        axles.push_back(axle);
    }

    // Every body cell something is bolted to (spring towers, arm and pivot mounts) is
    // part of the frame: several times stiffer and stronger than plain body structure.
    {
        std::set<std::pair<int, int>> towerEdges;
        for (int ci : mountCells)
            for (int p = 0; p < 8; p++)
                for (int q = p + 1; q < 8; q++)
                    towerEdges.insert({std::min(cells[ci][p], cells[ci][q]), std::max(cells[ci][p], cells[ci][q])});
        for (size_t bi = 0; bi < beams.size(); bi++) {
            Beam& b = beams[bi];
            if (b.a >= bodyNodes || b.b >= bodyNodes || !towerEdges.count({b.a, b.b})) continue;
            b.type = BEAM_FRAME;
            b.k = kSpec[BEAM_FRAME].k * vs.bodyStiffness * 3;
            b.c = kSpec[BEAM_FRAME].c * std::sqrt(vs.bodyStiffness * 3);
            b.yieldStrain = kSpec[BEAM_FRAME].yield * 1.3;
        }
    }


    // ---- Spring and damper rates from the linkage geometry ------------------------
    // For each assembly, find how it can move (null space of its links), then how
    // fast each coil-over shortens per meter of wheel travel: the motion ratio.
    if (!isProp && axles.size() >= 2) {
        const double xf = xAt(axleI.front()), xr = xAt(axleI.back());
        const double frontShare = clampd((sprungX - xr) / (xf - xr), 0.2, 0.8);
        const double kTire = 2.2e5 * vs.tireSoft;  // (softbody.cpp's kTireK, scaled: the springs are sized for the tire it really has)
        for (const Assembly& as : assemblies) {
            std::vector<Row> J;
            V3 o;
            for (int n : as.nodes) o += local[n];
            o = o / (double)as.nodes.size();
            for (int bi : as.links) {
                const Beam& b = beams[bi];
                bool aIn = std::find(as.nodes.begin(), as.nodes.end(), b.a) != as.nodes.end();
                int mine = aIn ? b.a : b.b, other = aIn ? b.b : b.a;
                V3 u = norm(local[mine] - local[other]), r = local[mine] - o, ru = cross(r, u);
                J.push_back({u.x, u.y, u.z, ru.x, ru.y, ru.z});
            }
            std::vector<Row> basis = nullSpace(J);
            const int nw = (int)as.wheelIdx.size();
            // Combination of free motions that lifts every contact patch at 1 m/s.
            Row q{};
            if ((int)basis.size() >= nw) {
                double A[2][2] = {{0, 0}, {0, 0}};
                for (int w = 0; w < nw; w++) {
                    const Wheel& wh = wheels[as.wheelIdx[w]];
                    V3 contact = (local[wh.inner] + local[wh.outer]) * 0.5 - V3(0, R, 0);
                    for (int bv = 0; bv < nw; bv++) A[w][bv] = pointVel(basis[bv], o, contact).y;
                }
                double cmb[2] = {0, 0};
                if (nw == 1) {
                    cmb[0] = std::fabs(A[0][0]) > 1e-9 ? 1.0 / A[0][0] : 0;
                } else {
                    double det = A[0][0] * A[1][1] - A[0][1] * A[1][0];
                    if (std::fabs(det) > 1e-12) {
                        cmb[0] = (A[1][1] - A[0][1]) / det;
                        cmb[1] = (A[0][0] - A[1][0]) / det;
                    }
                }
                for (int bv = 0; bv < nw; bv++)
                    for (int c = 0; c < 6; c++) q[c] += basis[bv][c] * cmb[bv];
            }
            for (int w = 0; w < nw; w++) {
                Wheel& wh = wheels[as.wheelIdx[w]];
                Beam& sp = beams[wh.spring];
                bool aIn = std::find(as.nodes.begin(), as.nodes.end(), sp.a) != as.nodes.end();
                int mine = aIn ? sp.a : sp.b, other = aIn ? sp.b : sp.a;
                V3 u = norm(local[mine] - local[other]);
                double mr = std::fabs(dot(u, pointVel(q, o, local[mine])));
                if (!(mr > 0.2 && mr < 2.0)) mr = 0.85;  // geometry problem: fall back to a typical ratio
                double share = wh.front ? frontShare : 1 - frontShare;
                double mCorner = sprungM * share / 2;
                double f = wh.front ? vs.rideFreqFront : vs.rideFreqRear;
                double kRide = std::pow(2 * M_PI * f, 2) * mCorner;
                double kWheel = kRide * kTire / std::max(kTire - kRide, kTire * 0.2);
                double cWheel = 2 * vs.dampingRatio * std::sqrt(kWheel * mCorner);
                sp.k = kWheel / (mr * mr);
                sp.c = cWheel / (mr * mr);
                double preload = mCorner * g / mr / sp.k;
                // (A multi-leaf spring: interleaf friction ~8% of the corner's weight.)
                if (wh.front ? vs.leafFront : vs.leafSprings) sp.friction = (float)(0.08 * mCorner * g / mr);
                double L = sp.L0;
                wh.motionRatio = mr;
                wh.springRest = L;
                sp.L0 = sp.Lorig = L + preload;  // so the car sits at design height
                // Bump stop and droop limit: the spec's wheel travel through the motion ratio
                // (coil travel = wheel travel * mr), else the old size-scaled defaults.
                const double bump = wh.front ? vs.bumpFront : vs.bumpRear, droop = wh.front ? vs.droopFront : vs.droopRear;
                sp.minL = bump > 0 ? L - bump * mr : L - 0.085 * sizeK;
                sp.maxL = droop > 0 ? L + droop * mr : L + 0.11 * sizeK;
                if (wh.bumpStop >= 0) {
                    // A live axle lands on its jounce bumpers (tube travel ~ wheel travel).
                    // The coil-over gets no stop of its own: pushing on the seat behind the
                    // tube, one rolled the housing over on its links in hard landings.
                    Beam& st = beams[wh.bumpStop];
                    st.k = kWheel;
                    st.c = cWheel * 2;  // rubber (or hydraulic) bumpers soak up the hit
                    st.minL = st.L0 - (bump > 0 ? bump : 0.085 * sizeK);
                    st.maxL = 1e9;
                    sp.minL = 0;
                }
                if (getenv("CZ_DEBUG"))
                    std::printf("%s %s%s: bump steer %.3f deg/cm, camber %.3f deg/cm\n", vs.name, wh.front ? "front " : "rear ", wh.left ? "left" : "right",
                                q[4] * 180 / M_PI / 100, q[3] * 180 / M_PI / 100);
                if (getenv("CZ_DEBUG"))
                    std::printf("%s %s%s: null dim %zu, motion ratio %.3f, corner %.0f kg, wheel rate %.0f N/m, coil %.0f N/m, damper %.0f, preload %.3f m\n",
                                vs.name, wh.front ? "front " : "rear ", wh.left ? "left" : "right", basis.size(), mr, mCorner,
                                kWheel, sp.k, sp.c, preload);
            }
        }
        for (Axle& a : axles) {
            if (a.wheel[0] < 0 || a.wheel[1] < 0) continue;
            // Bar rate at the wheel -> per meter of coil-over travel.
            double mr = wheels[a.wheel[0]].motionRatio;
            a.arbK = (a.front ? vs.arbFront : vs.arbRear) / (mr * mr);
            // Sized to twist through one wheel at full bump and the other at full droop, with
            // ~15% to spare; past that it yields.
            {
                const double bump = a.front ? vs.bumpFront : vs.bumpRear, droop = a.front ? vs.droopFront : vs.droopRear;
                a.arbYield = ((bump > 0 ? bump : 0.085 * sizeK) + (droop > 0 ? droop : 0.11 * sizeK)) * mr * 1.15;
                a.arbBend = 0;
            }
            a.heaveK = vs.heaveRate / (mr * mr);
            a.heaveGap = vs.heaveGap * mr;
            a.heaveC = vs.heaveDamping / (mr * mr);
        }
    }

    // =========================================================================
    // 6. Driveline: prop shafts from the gearbox, half shafts to independent wheels.

    for (size_t ax = 0; ax < axles.size() && !isProp; ax++) {
        Axle& a = axles[ax];
        const Wheel& w0 = wheels[a.wheel[0]];
        if (!w0.driven) continue;
        bool transaxle = (a.front && vs.transverse) || (!a.front && vs.midEngine);
        int diff = transaxle ? gearboxOut : a.diffNode;
        if (diff < 0) continue;
        if (!transaxle) {
            int from = gearboxOut;
            int target = a.housing.empty() ? diff : a.housing[1];  // solid axle: the pinion
            if (a.front && vs.drive == VehicleSpec::AWD) {
                // Transfer case front output, beside the gearbox.
                V3 p = (local[gearboxTail[0]] + local[gearboxTail[2]]) * 0.5 + V3(0.25, -0.02, -0.18);
                from = addNode(p, 2.0, false);
                for (int n : gearboxTail) addBeam(from, n, BEAM_ENGINE);
                for (int b = 0; b < 8; b += 2) addBeam(from, engineNodes[b], BEAM_ENGINE);
            }
            a.propShaft = addBeam(from, target, BEAM_SHAFT);
        }
        a.diffNode = diff;
        if (transaxle) {
            a.diffNodes = engineNodes;
        } else if (a.housing.empty()) {
            int ci = cellNear(local[diff], nullptr);
            a.diffNodes.assign(cells[ci].begin(), cells[ci].end());
            a.diffNodes.push_back(diff);
        }
        for (int sIdx = 0; sIdx < 2; sIdx++) {
            Wheel& wh = wheels[a.wheel[sIdx]];
            if (a.propShaft >= 0) wh.shafts.push_back(a.propShaft);
            if (a.housing.empty()) wh.shafts.push_back(addBeam(diff, wh.inner, BEAM_SHAFT));
        }
    }

    // Steel armor bumpers: the lowest row of the nose and tail is plate steel on the
    // frame horns. It shrugs off knocks that would crumple a bumper cover.
    if (!isProp && (vs.extras & EXTRA_ARMOR)) {
        const double xFront = xAt(1) - 1e-6, xRear = xAt(ncell - 1) + 1e-6, yTop = vs.layerY[1] + 1e-6;
        for (int bi = 0; bi < (int)beams.size(); bi++) {
            Beam& b = beams[bi];
            if (b.type != BEAM_BODY || b.a >= bodyNodes || b.b >= bodyNodes) continue;
            const V3 &pa = local[b.a], &pb = local[b.b];
            if (pa.y > yTop || pb.y > yTop) continue;
            if ((pa.x >= xFront && pb.x >= xFront) || (pa.x <= xRear && pb.x <= xRear)) b.yieldStrain *= 1.6;
        }
    }

    // Ladder frames, skid plates and rock sliders. All of it makes the truck tough where
    // the trail hits it (bending between the axles, rocks under the belly and sills),
    // but none of it reaches the frame horns and bumpers ahead of and behind the axles:
    // those still crush in a crash the way they're designed to.
    if (!isProp && axleI.size() >= 2) {
        const double xf = xAt(axleI.front()), xr = xAt(axleI.back()), yTop = vs.layerY[1] + 1e-6;
        const double sill = vs.width / 2 - vs.width / vs.widthCells * 0.6;
        for (Beam& b : beams) {
            if ((b.type != BEAM_BODY && b.type != BEAM_FRAME) || b.a >= bodyNodes || b.b >= bodyNodes) continue;
            const V3 &pa = local[b.a], &pb = local[b.b];
            // Crumple zones: the frame horns up front, the last of the tail (the rails
            // over and just behind the rear axle carry the hitch and spare, so they count).
            if (std::max(pa.x, pb.x) > xf + 0.2 || std::min(pa.x, pb.x) < xr - 0.8) continue;
            if (b.type == BEAM_FRAME) { b.yieldStrain *= vs.frameStrength; b.breakStrain *= vs.frameStrength; }
            if (pa.y > yTop || pb.y > yTop) continue;
            // Plates and sliders take hits from below and the side (vertical and crosswise
            // members), not the lengthwise crush of a frontal impact.
            if (std::fabs(pa.x - pb.x) > 0.5 * len(pa - pb)) continue;
            const bool skid = (vs.extras & EXTRA_SKID_PLATE) && pa.x > xf - 0.9 && pb.x > xf - 0.9;
            const bool slider = (vs.extras & EXTRA_SLIDERS) && std::min(pa.x, pb.x) > xr - 0.3 && std::max(pa.x, pb.x) < xf + 0.3 &&
                                ((pa.z > sill && pb.z > sill) || (pa.z < -sill && pb.z < -sill));
            // (A slider is a tube under the sill, for rocks from below: a bumper in the door
            // squeezes the sill across, which the slider doesn't stop.)
            if (skid || (slider && std::fabs(pa.y - pb.y) > 0.5 * len(pa - pb))) b.yieldStrain *= 2.5;
        }
    }

    // Where a crash is taken. A nose is built to soak up a frontal hit; the doors, sills and
    // the tail are not: a door is two skins and a bar, a tail a floor pan and a bumper. Crushed
    // across (a T-bone) or lengthwise from behind they give at a fraction of what the nose
    // takes, so the one hit dents instead of being shoved over whole. (Only the members a hit
    // squeezes: the uprights a rock or a landing loads keep their strength.)
    if (!isProp && axleI.size() >= 2) {
        const double xf = xAt(axleI.front()), xr = xAt(axleI.back());
        const double sill = vs.width / 2 - vs.width / vs.widthCells * 1.2;
        for (Beam& b : beams) {
            if ((b.type != BEAM_BODY && b.type != BEAM_CABIN) || b.a >= bodyNodes || b.b >= bodyNodes) continue;
            const V3 &pa = local[b.a], &pb = local[b.b], d = pb - pa;
            const double L = len(d);
            if (L < 1e-9) continue;
            if (pa.x > xf && pb.x > xf) {
                b.yieldStrain *= vs.noseStrength;
            } else if (vs.carbonTub || (vs.extras & EXTRA_TUBE_CAGE)) {
                continue;  // (a tub or a cage is the crash structure all round)
            } else if (pa.x < xr && pb.x < xr) {
                if (std::fabs(d.x) > 0.5 * L) b.yieldStrain *= kTailGive;  // (lengthwise)
            } else if (std::fabs(d.z) > 0.5 * L && std::fabs(pa.z) > sill && std::fabs(pb.z) > sill) {
                // (crosswise, in the door and sill cells: the sill, low down, is a box section
                // and gives less; it's what shoves the struck car along, under its weight)
                const bool low = pa.y < vs.layerY[1] - 1e-6 || pb.y < vs.layerY[1] - 1e-6;
                b.yieldStrain *= low ? kSillGive : kSideGive;
            }
        }
    }

    // The ladder frame's strength: the rails between the axles bend only under a heavy
    // hit (frameStrength); the horns ahead of the front axle and the tail behind the rear
    // one stay crumple zones. Where a shock tower or a link bracket is welded on, the rail
    // is boxed in and gusseted (it carries the suspension's whole load). And the body tub
    // sits on the frame on rubber mounts.
    if (ladderFrame && axleI.size() >= 2) {
        const double xf = xAt(axleI.front()), xr = xAt(axleI.back());
        std::set<int> rail(ladder.begin(), ladder.end());
        for (Beam& b : beams) {
            if (!rail.count(b.a) || !rail.count(b.b)) continue;
            if (bracketed.count(b.a) && bracketed.count(b.b)) {
                b.k *= 2;
                b.yieldStrain *= 3;
                b.breakStrain *= 2;
            }
            const V3 &pa = local[b.a], &pb = local[b.b];
            // The horns ahead of the front axle (and the last of the tail) have crush
            // initiators pressed in: in a crash they fold up, absorbing it, before the cab.
            if (std::min(pa.x, pb.x) > xf + 0.25 || std::max(pa.x, pb.x) < xr - 0.9) {
                b.yieldStrain *= 0.75;
                continue;
            }
            if (std::max(pa.x, pb.x) > xf + 0.2 || std::min(pa.x, pb.x) < xr - 0.8) continue;
            b.yieldStrain *= vs.frameStrength;
            b.breakStrain *= vs.frameStrength;
        }
        // Mounts: pairs along the rails, from the cowl (just behind the front tires: not in
        // the wheel well, where a tire forced up would find a mount) back past the rear axle. Each
        // ties the body floor nodes over the rail to its section there: the bushing sits on an
        // outrigger welded across the rail's side, which spreads its load into the whole
        // section (on one corner alone, a landing would crush the rail locally).
        const int perSide = std::max(2, vs.bodyMounts / 2);
        for (int m = 0; m < perSide; m++) {
            const double x0 = xf - vs.wheelRadius - 0.12, x1 = xr - 0.55;
            const double x = x0 + (x1 - x0) * m / (perSide - 1);
            int st = 0;
            for (int k = 1; k < ladderStations; k++)
                if (std::fabs(stationX[k] - x) < std::fabs(stationX[st] - x)) st = k;
            for (int side = 0; side < 2; side++) {
                const int top = ladder[st * 8 + side * 4 + 3];
                std::vector<std::pair<double, int>> cand;
                for (int n = 0; n < bodyNodes; n++)
                    if (local[n].y < vs.layerY[0] + 0.5 * (vs.layerY[1] - vs.layerY[0])) cand.push_back({len(local[n] - local[top]), n});
                std::sort(cand.begin(), cand.end());
                for (int k = 0; k < 4 && k < (int)cand.size(); k++)  // (a pyramid: it holds every way)
                    for (int corner = 0; corner < 4; corner++) {
                        const int bi = addBeam(ladder[st * 8 + side * 4 + corner], cand[k].second, BEAM_MOUNT);
                        if (bi < 0) continue;
                        beams[bi].k *= 6.0 / 4;  // body mount bushings: stiff rubber (all four together)
                        beams[bi].c *= 2.0 / 4;
                        beams[bi].yieldStrain = 0.12;
                        beams[bi].breakStrain = 0.6;  // a big enough hit shears the body off its mounts
                        beams[bi].snub = true;
                        bodyMountBeams.push_back(bi);
                    }
            }
        }
    }
    // Offroad vehicles have beefier suspension arms that resist bending and breaking.
    if (vs.frameStrength > 1.0) {
        for (Beam& b : beams) {
            if (b.type != BEAM_ARM) continue;
            b.yieldStrain *= vs.frameStrength;
            b.breakStrain *= vs.frameStrength;
        }
    }

    // =========================================================================
    // 7. Bolt-on panels: hood, doors, bumpers, trunk lid (or tailgate / rear doors).

    quadPanel.assign(quads.size(), -1);
    if (!isProp) {
        int gk0 = 99;
        for (int k = 0; k < NK - 1; k++)
            if (vs.profile[k].find('G') != std::string::npos) gk0 = std::min(gk0, k);
        bool hasBed = false;
        for (const std::string& row : vs.profile) hasBed |= row.find('B') != std::string::npos;
        const int last = ncell - 1;
        const int dc = doorCells(vs);  // lattice columns per door (about a meter)
        auto outerSide = [&](const SkinQuad& q, int sideSign) {  // -1 left, +1 right
            return q.axis == 1 && (sideSign < 0 ? (q.side == 0 && q.cell[1] == 0) : (q.side == 1 && q.cell[1] == NJ - 2));
        };
        struct Def {
            const char* name;
            std::function<bool(const SkinQuad&)> pick;
            int hingeAxis;  // lattice axis whose min (or max) edge carries the hinges; -1 = clips only
            bool hingeAtMax;
        };
        std::vector<Def> defs;
        defs.push_back({"HOOD", [&](const SkinQuad& q) {
                            return q.axis == 2 && q.side == 1 && q.cell[0] < cabI0 && q.cellChar != 'G' &&
                                   (!vs.openFrame || q.cellChar == 'p' || q.cellChar == 'w');
                        }, 0, true});
        for (int sd = -1; sd <= 1 && !vs.openFrame; sd += 2) {  // (tube-frame vehicles have no doors)
            const char* nm = sd < 0 ? "LEFT DOOR" : "RIGHT DOOR";
            defs.push_back({nm, [&, sd](const SkinQuad& q) {
                                return outerSide(q, sd) && q.cell[0] >= cabI0 && q.cell[0] < cabI0 + dc && q.cell[2] <= gk0;
                            },
                            0, false});
            if (hasRearDoors(vs)) {
                const char* rn = sd < 0 ? "LEFT REAR DOOR" : "RIGHT REAR DOOR";
                defs.push_back({rn, [&, sd](const SkinQuad& q) {
                                    return outerSide(q, sd) && q.cell[0] >= cabI0 + dc && q.cell[0] < cabI0 + 2 * dc && q.cell[2] <= gk0;
                                },
                                0, false});
            }
        }
        // Steel armor bumpers bolt to the frame: no plastic bumper covers to knock off.
        const bool armor = (vs.extras & EXTRA_ARMOR) || vs.openFrame;  // (tube bumpers are part of the frame)
        if (!armor) defs.push_back({"FRONT BUMPER", [&](const SkinQuad& q) {
                            return q.cell[0] == 0 && q.cell[2] == 0 && ((q.axis == 0 && q.side == 0) || outerSide(q, -1) || outerSide(q, 1));
                        },
                        -1, false});
        if (!armor) defs.push_back({"REAR BUMPER", [&](const SkinQuad& q) {
                            return q.cell[0] == last && q.cell[2] == 0 && ((q.axis == 0 && q.side == 1) || outerSide(q, -1) || outerSide(q, 1));
                        },
                        -1, false});
        int topLast = 0;
        for (int k = 0; k < NK - 1; k++)
            for (int j = 0; j < NJ - 1; j++)
                if (solid(last, j, k)) topLast = k;
        if (hasBed || vs.openFrame) {
            // Pickup: an open bed, no lid. (Tube-frame vehicles: nothing to open at the back.)
        } else if (topLast >= gk0) {
            // Tall back (SUV): a swing gate hinged at the side, or a liftgate hinged at the top.
            defs.push_back({vs.liftgate ? "LIFTGATE" : "TAILGATE", [&](const SkinQuad& q) {
                                return q.axis == 0 && q.side == 1 && q.cell[0] == last && q.cell[2] >= 1;
                            },
                            vs.liftgate ? 2 : 1, vs.liftgate});
        } else if (vs.midEngine) {
            // Engine cover behind the cabin, hinged at its front edge.
            defs.push_back({"ENGINE COVER", [&](const SkinQuad& q) {
                                return q.axis == 2 && q.side == 1 && q.cell[0] >= cabI1 && q.cellChar != 'G';
                            },
                            0, false});
        } else {
            // Trunk lid (or a small hatch lid) behind the cabin, hinged at its front edge.
            defs.push_back({"TRUNK LID", [&](const SkinQuad& q) {
                                return q.axis == 2 && q.side == 1 && q.cell[0] >= cabI1 && q.cell[2] < gk0 && q.cellChar != 'G';
                            },
                            0, false});
        }

        // How many skin quads touch each node (for splitting the panel mass off it).
        std::vector<int> quadCount(bodyNodes, 0);
        for (const SkinQuad& q : quads)
            for (int c = 0; c < 4; c++) quadCount[q.v[c]]++;
        const double panelShare = vs.panelMass > 0 ? vs.panelMass / std::max(1, (int)std::count(onSkin.begin(), onSkin.end(), 1)) : 0;

        for (const Def& d : defs) {
            Panel pnl;
            pnl.name = d.name;
            const bool bumper = std::string(d.name).find("BUMPER") != std::string::npos;
            panelStrength = bumper ? 1.0 : vs.panelStrength;
            for (size_t qi = 0; qi < quads.size(); qi++)
                if (quadPanel[qi] < 0 && quads[qi].mat != MAT_WELL && quads[qi].mat != MAT_UNDER &&
                    d.pick(quads[qi]))
                    pnl.quads.push_back((int)qi);
            if (pnl.quads.size() < 2) continue;
            const int pi = (int)panels.size();
            std::map<int, int> copy;
            std::map<int, int> inPanel;  // body node -> quads of this panel touching it
            for (int qi : pnl.quads) {
                quadPanel[qi] = pi;
                for (int c = 0; c < 4; c++) inPanel[quads[qi].v[c]]++;
            }
            // Lattice coordinate range along the hinge axis.
            double lo = 1e9, hi = -1e9;
            for (int qi : pnl.quads)
                for (int c = 0; c < 4; c++)
                    if (d.hingeAxis >= 0) {
                        lo = std::min(lo, (double)quads[qi].lat[c][d.hingeAxis]);
                        hi = std::max(hi, (double)quads[qi].lat[c][d.hingeAxis]);
                    }
            for (const auto& e : inPanel) {
                int bn = e.first;
                double share = panelShare * e.second / std::max(1, quadCount[bn]);
                double m = std::min(std::max(share, 0.8), nodes[bn].m * 0.45);
                nodes[bn].m -= m;
                int pn = addNode(local[bn], m, true);
                copy[bn] = pn;
                pnl.bodyNodes.push_back(bn);
                pnl.panelNodes.push_back(pn);
            }
            // Which edge carries the hinges.
            auto latOf = [&](int bn) -> const float* {
                for (int qi : pnl.quads)
                    for (int c = 0; c < 4; c++)
                        if (quads[qi].v[c] == bn) return quads[qi].lat[c];
                return nullptr;
            };
            for (size_t k = 0; k < pnl.bodyNodes.size(); k++) {
                int bn = pnl.bodyNodes[k];
                Pin pin;
                pin.body = bn;
                pin.panel = pnl.panelNodes[k];
                pin.c = 300;
                const float* lt = latOf(bn);
                // Door window frames move with the door; they aren't bolted to the roof,
                // just pressed against its weather seal while the door is shut.
                const bool frame = std::string(d.name).find("DOOR") != std::string::npos && lt && lt[2] > gk0;
                if (frame) {
                    pin.kind = PIN_SEAL, pin.k = 4e4, pin.c = 120, pin.breakDist = 0.08;
                    pnl.pins.push_back((int)pins.size());
                    pins.push_back(pin);
                    continue;
                }
                bool hinge = d.hingeAxis >= 0 && lt && std::fabs(lt[d.hingeAxis] - (d.hingeAtMax ? hi : lo)) < 1e-3;
                bool latch = d.hingeAxis >= 0 && lt && std::fabs(lt[d.hingeAxis] - (d.hingeAtMax ? lo : hi)) < 1e-3;
                // Break at a force: hinges ~15 kN, latches ~9 kN (door latches are rated
                // for ~11 kN), bolts and clips ~4 kN.
                if (hinge) pin.kind = PIN_HINGE, pin.k = 3e5, pin.breakDist = 15000 / 3e5;
                else if (latch) pin.kind = PIN_LATCH, pin.k = 2e5, pin.breakDist = 9000 / 2e5;
                else pin.kind = PIN_CLIP, pin.k = 2e5, pin.breakDist = 4000 / 2e5;
                if (!bumper) pin.breakDist *= vs.pinStrength;
                pin.breakDist *= 1.5;  // (hinges, latches and clips: hold through knocks, as the beams above)
                pnl.pins.push_back((int)pins.size());
                pins.push_back(pin);
            }
            // The sheet itself: edges and diagonals of each face, plus beams across each
            // fold so it keeps its shape once it's off.
            std::map<std::pair<int, int>, std::vector<int>> edgeQuads;
            for (int qi : pnl.quads) {
                const SkinQuad& q = quads[qi];
                for (int a = 0; a < 4; a++)
                    for (int b = a + 1; b < 4; b++) addBeam(copy[q.v[a]], copy[q.v[b]], BEAM_PANEL);
                for (int a = 0; a < 4; a++) {
                    int u = q.v[a], v = q.v[(a + 1) % 4];
                    edgeQuads[{std::min(u, v), std::max(u, v)}].push_back(qi);
                }
            }
            for (const auto& eq : edgeQuads) {
                if (eq.second.size() != 2) continue;
                std::vector<int> far[2];
                for (int s2 = 0; s2 < 2; s2++)
                    for (int c = 0; c < 4; c++) {
                        int v = quads[eq.second[s2]].v[c];
                        if (v != eq.first.first && v != eq.first.second) far[s2].push_back(v);
                    }
                for (int a : far[0])
                    for (int b : far[1]) addBeam(copy[a], copy[b], BEAM_PANEL);
            }
            // Opening stops: a door opens to ~70 degrees on its check strap, a hood to ~80 on its
            // prop, a lid or gate to ~85-90. The strap is a slack pin from each node one cell off
            // the hinge line to the body node it sat on: that gap grows as 2 r sin(angle / 2).
            if (d.hingeAxis >= 0) {
                const std::string nm = d.name;
                const double open = (nm.find("DOOR") != std::string::npos ? 70 : nm == "HOOD" ? 80 : nm == "TAILGATE" ? 90 : 85) * M_PI / 180;
                const double hv = d.hingeAtMax ? hi : lo, step = d.hingeAtMax ? -1 : 1;
                std::vector<int> hingeNodes, next;
                const bool door = nm.find("DOOR") != std::string::npos;
                for (size_t k = 0; k < pnl.bodyNodes.size(); k++) {
                    const float* lt = latOf(pnl.bodyNodes[k]);
                    if (!lt) continue;
                    if (std::fabs(lt[d.hingeAxis] - hv) < 1e-3) hingeNodes.push_back((int)k);
                    // (A door's strap is in the door itself, below the glass: not on its window frame.)
                    else if (std::fabs(lt[d.hingeAxis] - (hv + step)) < 1e-3 && !(door && lt[2] > gk0)) next.push_back((int)k);
                }
                for (int k : next) {
                    double r = 1e9;
                    for (int hk : hingeNodes) r = std::min(r, len(local[pnl.bodyNodes[k]] - local[pnl.bodyNodes[hk]]));
                    if (r > 2 || next.empty()) continue;
                    Pin pin;
                    pin.body = pnl.bodyNodes[k];
                    pin.panel = pnl.panelNodes[k];
                    pin.kind = PIN_CHECK;
                    // (A steel stop: a door hanging open through a hard corner barely moves on it.
                    // Each tears at ~3 kN (a door twisting on its stop loads one more than the
                    // rest): a door flung open in a crash or a roll goes past.)
                    pin.k = 4e5 / next.size();
                    pin.c = 600.0 / next.size();
                    pin.slack = 2 * r * std::sin(open / 2);
                    pin.breakDist = pin.slack + 3000.0 / pin.k * (bumper ? 1.0 : vs.pinStrength);
                    pnl.checks.push_back((int)pins.size());
                    pins.push_back(pin);
                }
            }
            pnl.nodeQuads.resize(pnl.bodyNodes.size());
            for (size_t k = 0; k < pnl.bodyNodes.size(); k++)
                for (int qi : pnl.quads)
                    for (int c = 0; c < 4; c++)
                        if (quads[qi].v[c] == pnl.bodyNodes[k]) pnl.nodeQuads[k].push_back(qi);
            // Collision and glass use the same moving nodes as the visible panel.
            // Leaving these on the welded body creates invisible doors after detachment.
            for (int qi : pnl.quads)
                for (int t = 0; t < 2; t++)
                    for (int k = 0; k < 3; k++) {
                        int& n = tris[quads[qi].tri + t].v[k];
                        n = copy[n];
                    }
            panels.push_back(pnl);
        }
    }

    // =========================================================================
    // Finish

    // Stability: an explicit integrator can't step a light node that hangs on stiff,
    // damped beams. For each node, take its stiffest direction (counting bump stops and
    // rebound damping) and check h^2*k/m + 2h*c/m against a safe margin (the hard limit
    // is 4). A node over the margin borrows mass from its neighbors; total mass and
    // balance stay the same.
    // (Where the mass sits before: the pass moves some about, and the balance is put back after.)
    V3 cgBefore;
    double mBefore = 0;
    for (size_t i = 0; i < nodes.size(); i++) cgBefore += local[i] * nodes[i].m, mBefore += nodes[i].m;
    cgBefore = cgBefore / mBefore;
    {
        // The chassis frame's own pull and damping (see below) use up part of the margin,
        // so a very rigid frame leaves less for the beams.
        const double h = 1.0 / 2000.0, frameK = 300000 * (ladderFrame ? std::max(vs.frameStiffness * kRailStiffness, kLadderTubStiffness) : vs.frameStiffness);
        const double frameLoad = isProp ? 0.0 : h * h * frameK + 2 * h * 2 * 0.85 * std::sqrt(frameK);
        const double target = std::min(1.6, (3.9 - frameLoad) / 1.85);
        std::vector<std::vector<int>> nb(nodes.size());
        for (size_t bi = 0; bi < beams.size(); bi++) {
            nb[beams[bi].a].push_back((int)bi);
            nb[beams[bi].b].push_back((int)bi);
        }
        // (Panel pins pull a panel node toward the body node under it in every direction.)
        std::vector<std::vector<int>> pinsAt(nodes.size());
        for (size_t pi = 0; pi < pins.size(); pi++) {
            pinsAt[pins[pi].body].push_back((int)pi);
            pinsAt[pins[pi].panel].push_back((int)pi);
        }
        auto load = [&](size_t i) {  // worst-direction h^2*k + 2h*c
            double pk = 0, pc = 0;
            for (int pi : pinsAt[i]) pk += pins[pi].k, pc += pins[pi].c;
            double worst = h * h * pk + 2 * h * pc;
            for (int bj : nb[i]) {
                const Beam& d = beams[bj];
                V3 u = norm(local[d.b] - local[d.a]);
                double K = 0, C = 0;
                for (int bk : nb[i]) {
                    const Beam& e = beams[bk];
                    double c2 = dot(u, norm(local[e.b] - local[e.a]));
                    c2 *= c2;
                    bool spring = e.type == BEAM_SPRING || e.type == BEAM_BUMPSTOP;
                    K += c2 * e.k * (e.type == BEAM_BUMPSTOP ? 125 : spring ? 26 : 1);
                    C += c2 * e.c * (spring ? 1.35 : 1);
                }
                worst = std::max(worst, h * h * (K + pk) + 2 * h * (C + pc));
            }
            return worst;
        };
        std::vector<double> L(nodes.size());
        for (size_t i = 0; i < nodes.size(); i++) L[i] = load(i);
        for (int pass = 0; pass < 4; pass++)
            for (size_t i = 0; i < nodes.size(); i++) {
                if (L[i] / nodes[i].m <= target) continue;
                double need = L[i] / target - nodes[i].m;
                std::vector<int> donors;
                double avail = 0;
                for (int bj : nb[i]) {
                    // (Only within one solid assembly: across a rubber mount or a spring is a
                    // different part, and borrowing there would move real weight, the engine's,
                    // say, to where it isn't.)
                    const uint8_t t = beams[bj].type;
                    if (t == BEAM_MOUNT || t == BEAM_SPRING || t == BEAM_BUMPSTOP) continue;
                    int o = beams[bj].a == (int)i ? beams[bj].b : beams[bj].a;
                    double spare = nodes[o].m - L[o] / (target * 0.8);
                    if (spare > 0) {
                        donors.push_back(o);
                        avail += spare;
                    }
                }
                if (avail < need) {
                    // Not enough nearby: the rest comes evenly from the body's heaviest nodes.
                    double pool = 0;
                    for (int o = 0; o < bodyNodes; o++) pool += std::max(0.0, nodes[o].m - L[o] / (target * 0.8));
                    double extra = std::min(need - avail * 0.5, pool * 0.2);
                    if (extra > 0) {
                        for (int o = 0; o < bodyNodes; o++) {
                            double spare = std::max(0.0, nodes[o].m - L[o] / (target * 0.8));
                            nodes[o].m -= extra * spare / pool;
                        }
                        nodes[i].m += extra;
                        need -= extra;
                    }
                }
                if (avail <= 0) continue;
                double take = std::min(need, avail * 0.5);
                for (int o : donors) {
                    double spare = nodes[o].m - L[o] / (target * 0.8);
                    nodes[o].m -= take * spare / avail;
                }
                nodes[i].m += take;
            }
        // Still over the margin where nothing was left to lend (a light tube chassis): soften
        // that node's stiff beams until it steps stably. A slightly springier joint is better
        // than a node that runs away at the first step.
        for (int it = 0; it < 4; it++) {
            bool changed = false;
            for (size_t i = 0; i < nodes.size(); i++) {
                const double lim = target * 1.25 * nodes[i].m;
                if (L[i] <= lim) continue;
                const double f = lim / L[i];
                for (int bj : nb[i]) {
                    Beam& b = beams[bj];
                    if (b.type == BEAM_SPRING || b.type == BEAM_BUMPSTOP) continue;
                    b.k *= f;
                    b.c *= f;
                }
                for (int pi : pinsAt[i]) {  // (a softer pin still lets go at the same force)
                    Pin& pn = pins[pi];
                    pn.breakDist = pn.slack + (pn.breakDist - pn.slack) / f;
                    pn.k *= f;
                    pn.c *= f;
                }
                changed = true;
            }
            if (!changed) break;
            for (size_t i = 0; i < nodes.size(); i++) L[i] = load(i);
        }
        if (getenv("CZ_STAB"))
            for (size_t i = 0; i < nodes.size(); i++)
                if (L[i] / nodes[i].m > target * 1.3) std::printf("STAB node %zu L %.2f m %.2f L/m %.2f target %.2f\n", i, L[i], nodes[i].m, L[i] / nodes[i].m, target);
        // Balance: what the body lent shifted the center of mass (toward the stiff nodes
        // that borrowed: a few cm). Tilt the mass of the body nodes with plenty to spare
        // fore-aft and side to side, just enough to put it back, so the axle loads stay the
        // factory's. (Nodes near their limit are left alone.)
        for (int it = 0; it < 6 && !isProp; it++) {
            auto roomy = [&](int i) { return nodes[i].m > 2 * L[i] / target; };
            V3 cg, xb;
            double M = 0, Mb = 0, vx = 0, vz = 0;
            for (size_t i = 0; i < nodes.size(); i++) cg += local[i] * nodes[i].m, M += nodes[i].m;
            cg = cg / M;
            for (int i = 0; i < bodyNodes; i++)
                if (roomy(i)) xb += local[i] * nodes[i].m, Mb += nodes[i].m;
            if (Mb <= 0) break;
            xb = xb / Mb;
            for (int i = 0; i < bodyNodes; i++)
                if (roomy(i)) {
                    vx += nodes[i].m * (local[i].x - xb.x) * (local[i].x - xb.x);
                    vz += nodes[i].m * (local[i].z - xb.z) * (local[i].z - xb.z);
                }
            const double bx = -(cg.x - cgBefore.x) * M / std::max(vx, 1e-9), bz = -(cg.z - cgBefore.z) * M / std::max(vz, 1e-9);
            double before = 0, after = 0;
            for (int i = 0; i < bodyNodes; i++)
                if (roomy(i)) {
                    before += nodes[i].m;
                    nodes[i].m *= clampd(1 + bx * (local[i].x - xb.x) + bz * (local[i].z - xb.z), 0.6, 1.6);
                    after += nodes[i].m;
                }
            for (int i = 0; i < bodyNodes; i++)
                if (roomy(i)) nodes[i].m *= before / after;  // (same total)
        }
        if (getenv("CZ_DEBUG"))
            for (size_t i = 0; i < nodes.size(); i++)
                if (L[i] / nodes[i].m > target) std::printf("%s: node %zu (%.2f, %.2f, %.2f; %.1f kg; %d of %d body) still stiff for its mass (%.2f)\n", vs.name, i, local[i].x, local[i].y, local[i].z, nodes[i].m, (int)(i < (size_t)bodyNodes), bodyNodes, L[i] / nodes[i].m);
    }
    // Spring preloads again, now that the weight has settled where it ends up. The pass
    // above lends body weight to the suspension's stiff little nodes (a live axle's take
    // ~60 kg on the Bronco) and then shifts the body's weight forward to put the balance
    // back, so the springs carried less than the preload set above was sized for and a live
    // rear rode ~1 cm high. Same coils (same rate); only their free length changes, so they
    // sit at design height under the weight they really carry.
    if (!isProp && axles.size() >= 2) {
        const double xf = xAt(axleI.front()), xr = xAt(axleI.back());
        std::vector<char> unsprung(nodes.size(), 0);
        for (const Wheel& wh : wheels)
            for (int n : wh.assembly) unsprung[n] = 1;
        double ms = 0, xs = 0;
        for (size_t i = 0; i < nodes.size(); i++)
            if (!unsprung[i]) ms += nodes[i].m, xs += local[i].x * nodes[i].m;
        xs /= std::max(ms, 1e-9);
        const double frontShare = clampd((xs - xr) / (xf - xr), 0.2, 0.8);
        for (Wheel& wh : wheels) {
            if (wh.spring < 0 || wh.motionRatio <= 0) continue;
            Beam& sp = beams[wh.spring];
            const double cornerW = ms * (wh.front ? frontShare : 1 - frontShare) / 2 * g;
            sp.L0 = sp.Lorig = wh.springRest + cornerW / wh.motionRatio / sp.k;
        }
    }
    // The spare's size: a road wheel, unless the spec carries a smaller one (a space saver).
    if (hasSpare && !wheels.empty()) {
        const double kg = spare.mass;
        spare = wheels.back().item;
        spare.mass = kg;
        if (vs.spareR > 0) {
            spare.R = vs.spareR;
            spare.rimR = vs.spareRimR > 0 ? vs.spareRimR : vs.spareR * 0.62;
            spare.width = vs.spareWidth > 0 ? vs.spareWidth : spare.width;
            if (vs.spareMass > 0) spare.mass = vs.spareMass;
            spare.I = 0.65 * spare.mass * spare.R * spare.R;
        }
    } else {
        hasSpare = false;
    }
    restLocal = local;
    wheelbase = 2.5;
    if (axleI.size() >= 2) wheelbase = xAt(axleI.front()) - xAt(axleI.back());

    sideSill.assign(nodes.size(), -1);
    if (!isProp) {
        const double wall = vs.width / 2 - vs.width / vs.widthCells * 1.2;
        for (int n = 0; n < bodyNodes; n++) {
            const V3& p = local[n];
            if (std::fabs(p.z) <= wall || p.y <= vs.layerY[1] + 1e-6) continue;
            int best = -1;
            for (int m = 0; m < bodyNodes; m++) {
                const V3& q = local[m];
                if (std::fabs(q.x - p.x) > 1e-3 || q.z * p.z <= 0 || std::fabs(q.z) <= wall || q.y > vs.layerY[0] + 1e-6) continue;
                if (best < 0 || std::fabs(q.z) > std::fabs(local[best].z)) best = m;
            }
            sideSill[n] = best;
        }
    }
    nodeTris.assign(nodes.size(), {});
    for (int t = 0; t < (int)tris.size(); t++)
        for (int c = 0; c < 3; c++) nodeTris[tris[t].v[c]].push_back(t);
    buildWheelRub();
    skinEdges.clear();
    {
        std::set<std::pair<int, int>> edges;
        for (const Tri& t : tris)
            for (int e = 0; e < 3; e++) {
                int a = t.v[e], b = t.v[(e + 1) % 3];
                edges.insert({std::min(a, b), std::max(a, b)});
            }
        skinEdges.assign(edges.begin(), edges.end());
    }

    mass = 0;
    double mx = 0;
    for (size_t i = 0; i < nodes.size(); i++) {
        nodes[i].invm = 1.0 / nodes[i].m;
        mass += nodes[i].m;
        mx += nodes[i].m * local[i].x;
    }
    if (axleI.size() >= 2) frontShare = clampd((mx / mass - xAt(axleI.back())) / (xAt(axleI.front()) - xAt(axleI.back())), 0, 1);

    // Downforce is spread over the body nodes by mass, tilted along the car so its
    // center of pressure splits it between the axles as the spec says.
    aeroShare.clear();
    if (vs.downforce > 0 && axleI.size() >= 2) {
        double M = 0, xm = 0, var = 0;
        for (int i = 0; i < bodyNodes; i++) {
            M += nodes[i].m;
            xm += nodes[i].m * local[i].x;
        }
        xm /= M;
        for (int i = 0; i < bodyNodes; i++) var += nodes[i].m * (local[i].x - xm) * (local[i].x - xm);
        var /= M;
        const double xf = xAt(axleI.front()), xr = xAt(axleI.back());
        const double xcp = xr + vs.aeroBalance * (xf - xr), beta = (xcp - xm) / std::max(var, 1e-6);
        double sum = 0;
        aeroShare.assign(bodyNodes, 0);
        for (int i = 0; i < bodyNodes; i++) {
            aeroShare[i] = std::max(0.0, nodes[i].m * (1 + beta * (local[i].x - xm)));
            sum += aeroShare[i];
        }
        for (double& w : aeroShare) w /= sum;
    }

    // Chassis frame: the lattice and everything bolted rigidly to it. On a body-on-frame
    // truck, two: the body tub, and the ladder frame with everything welded to it (it twists
    // more, as ladder frames do; the stiffer tub rides on its mounts).
    chassis = ChassisFrame();
    for (ChassisFrame& rf : railFrame) rf = ChassisFrame();
    if (!isProp) {
        auto setUp = [&](ChassisFrame& cf, const std::vector<int>& members, double kappa) {
            cf.kappa = kappa;
            cf.damping = 2 * 0.85 * std::sqrt(cf.kappa);
            cf.maxPull = 2500 * vs.bodyStiffness;
            V3 c;
            double M = 0;
            for (int i : members) {
                cf.nodes.push_back(i);
                c += local[i] * nodes[i].m;
                M += nodes[i].m;
            }
            c = c / M;
            for (int i : cf.nodes) cf.rest.push_back(local[i] - c);
            cf.attached.assign(cf.nodes.size(), 1);
            cf.release.assign(cf.nodes.size(), 0);
            cf.slot.assign(nodes.size(), -1);
            for (size_t k = 0; k < cf.nodes.size(); k++) cf.slot[cf.nodes[k]] = (int)k;
            cf.rot = Quat::axisAngle(V3(0, 1, 0), yaw);
        };
        auto range = [](int from, int to) {
            std::vector<int> v;
            for (int i = from; i < to; i++) v.push_back(i);
            return v;
        };
        if (ladderFrame) {
            setUp(chassis, range(0, bodyNodes), 300000 * kLadderTubStiffness);
            // Each rail with the brackets welded to it (the pads and brackets between the rails
            // go with the nearer one).
            // (One on the centreline, a diff's mount, say, is held by both, half each: the
            // truck stays the same both sides.)
            std::vector<int> side[2];
            for (int i = bodyNodes; i < frameNodes; i++) {
                if (std::fabs(local[i].z) < 0.05) side[0].push_back(i), side[1].push_back(i);
                else side[local[i].z > 0].push_back(i);
            }
            for (int r = 0; r < 2; r++) {
                ChassisFrame& rf = railFrame[r];
                setUp(rf, side[r], 300000 * vs.frameStiffness * kRailStiffness);
                // Boxed steel: stiff up to its yield, then it bends and stays bent. (A rail's
                // shape-holding pull is all that carries its bending: the lattice of a few
                // nodes per section is far softer than the steel.)
                rf.maxPull = 1e9;
                rf.yieldDist = vs.railYield;
                // The brackets, towers and pads welded on are stouter than the bare rail (a
                // coil-over tower is built for the bump stop's hit): held harder, to their own
                // yield force.
                rf.weight.assign(rf.nodes.size(), 1.0f);
                rf.section.assign(rf.nodes.size(), 0);
                int nextSection = ladderStations;
                for (size_t k = 0; k < rf.nodes.size(); k++) {
                    auto it = std::lower_bound(ladder.begin(), ladder.end(), rf.nodes[k]);
                    if (it != ladder.end() && *it == rf.nodes[k]) {
                        rf.section[k] = (int)(it - ladder.begin()) / 8;  // its station
                    } else {
                        const double hold = kBracketYield / (rf.kappa * nodes[rf.nodes[k]].m * rf.yieldDist);
                        // (One on the centreline sits on a crossmember between the rails: what
                        // holds it there is what makes the frame stiff in twist, so it's held
                        // like plain rail by each.)
                        rf.weight[k] = std::fabs(local[rf.nodes[k]].z) < 0.05 ? 1.0f : (float)clampd(hold, 1, 40);
                        rf.section[k] = nextSection++;  // (a bracket bends on its own)
                    }
                }
                // The rail is the same steel section end to end, so every station takes the
                // same force to bend; but the pull scales with each node's mass, and that
                // varies 3:1 along it (the frame's weight follows its brackets and horns, and
                // the stability pass lends some to the suspension). Held to the rail's mean, so
                // the light ends (the 4Runner's tail station carried 5.8 kg against 17 in the
                // middle) don't set at a third of the force the middle takes.
                double railM = 0;
                int railN = 0;
                for (size_t k = 0; k < rf.nodes.size(); k++)
                    if (rf.section[k] < ladderStations) railM += nodes[rf.nodes[k]].m, railN++;
                railM /= std::max(railN, 1);
                for (size_t k = 0; k < rf.nodes.size(); k++)
                    if (rf.section[k] < ladderStations) rf.weight[k] = (float)clampd(railM / nodes[rf.nodes[k]].m, 0.33, 3);
            }
        } else {
            setUp(chassis, range(0, frameNodes), 300000 * vs.frameStiffness);
        }
    }
    chassisRest0 = chassis.rest;

    // Recovery points: the frame nodes at the very front and back of the floor (where
    // the shackle mounts bolt on), and the shackle itself just outside the bumper.
    for (int e = 0; e < 2; e++) {
        hooks[e].clear();
        double ext = e == 0 ? -1e9 : 1e9;
        for (int i = 0; i < bodyNodes; i++) ext = e == 0 ? std::max(ext, local[i].x) : std::min(ext, local[i].x);
        if (ladderFrame) {  // (shackle mounts bolt to the frame's horns)
            for (int n : ladder)
                if (std::fabs(local[n].x - (e == 0 ? stationX.front() : stationX.back())) < 0.5) hooks[e].push_back(n);
        } else {
            for (int i = 0; i < bodyNodes; i++)
                if (local[i].y <= vs.layerY[0] + 0.15 && std::fabs(local[i].x - ext) < 0.7 && std::fabs(local[i].z) < vs.width * 0.4)
                    hooks[e].push_back(i);
        }
        hookRest[e] = V3(ext + (e == 0 ? 0.06 : -0.06), vs.layerY[0] + 0.1, 0);
    }

    for (int i = 0; i < bodyNodes; i++) {
        const V3& l = local[i];
        if (l.x > vs.length * 0.14) grpFront.push_back(i);
        if (l.x < -vs.length * 0.14) grpRear.push_back(i);
        if (l.z < -vs.width * 0.22) grpLeft.push_back(i);
        if (l.z > vs.width * 0.22) grpRight.push_back(i);
    }
    updateBounds();
}
