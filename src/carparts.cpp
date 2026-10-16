#include "carparts.h"

#include <map>
#include <mutex>
#include <string>

#include "modelkit.h"

namespace {

const V3 kUp(0, 1, 0);
// (No shift levers: they'd stand up through the floor.)
const std::vector<std::pair<std::string, double>> kNoLever = {{"shifter", 0}, {"lever", 0}};

// What a part measures built bare (as fitted: no stand under it, its own axis at the origin): the
// design the lab picked, or `variant`. Built once.
struct Bnd {
    V3 lo, hi;
    V3 mid() const { return (lo + hi) * 0.5; }
    V3 size() const { return hi - lo; }
};
Bnd bounds(const char* id, int variant = -1) {
    static std::map<std::string, Bnd> cache;
    static std::mutex lock;
    static uint64_t cachedFor = 0;
    std::lock_guard<std::mutex> g(lock);
    if (cachedFor != mk::picksVersion()) cache.clear(), cachedFor = mk::picksVersion();
    const std::string key = std::string(id) + "#" + std::to_string(variant);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Bnd b{V3(-0.1, -0.1, -0.1), V3(0.1, 0.1, 0.1)};
    if (const mk::ModelDef* d = mk::findModel(id)) {
        const int v = variant < 0 ? mk::pickedVariant(*d) : variant;
        mk::P p = mk::pickedParams(*d, v);
        p.v["bare"] = 1;
        mk::Mesh m;
        mk::build(*d, v, p, m);
        if (!m.v.empty()) b = {m.lo, m.hi};
    }
    return cache[key] = b;
}

int pickedOf(const char* id) {
    const mk::ModelDef* d = mk::findModel(id);
    return d ? mk::pickedVariant(*d) : 0;
}

double clampTo(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

struct Maker {
    const Car& c;
    const VehicleSpec& vs;
    std::vector<Placed>& out;
    Frame fr;
    double sz;  // how big its tires are: the small parts follow them a little

    V3 P(int n) const { return c.nodes[n].p; }

    // The model `id` with its origin at `at`, its front toward X, its up near Y, scaled along (front, up, left).
    void put(const char* id, const V3& at, V3 X, V3 Y, const V3& scale = V3(1, 1, 1), int variant = -1,
             const std::vector<std::pair<std::string, double>>& set = {}) {
        X = norm(X);
        Y = Y - X * dot(Y, X);
        Y = len(Y) > 1e-6 ? norm(Y) : norm(cross(cross(X, kUp), X));
        Placed pl;
        pl.id = id, pl.p = at, pl.oriented = true, pl.bare = true, pl.variant = variant, pl.scale = scale, pl.set = set;
        pl.ax[0] = X, pl.ax[1] = Y, pl.ax[2] = cross(X, Y);
        out.push_back(pl);
    }
    // The engine's pose from its eight block corners.
    struct Block {
        bool ok = false;
        V3 c, X, Y, Z;        // middle, front, up and across
        V3 size;              // its length, height and width now
        V3 origin;            // where the crank axis meets the flywheel's end (the model's origin)
        V3 scale;
        V3 rear;              // the middle of the face the gearbox bolts to
    } blk;

    void engine() {
        if (c.engineNodes.size() < 12) return;
        V3 e[8], sum, front, back, top, bot;
        for (int b = 0; b < 8; b++) {
            e[b] = P(c.engineNodes[b]);
            sum += e[b];
            (b & 1 ? front : back) += e[b];
            (b & 2 ? top : bot) += e[b];
        }
        blk.c = sum / 8.0;
        const V3 fwd = (front - back) * 0.25, up = (top - bot) * 0.25;
        blk.size.x = len(fwd);
        blk.size.y = len(up);
        {
            V3 l, r;
            for (int b = 0; b < 8; b++) (b & 4 ? l : r) += e[b];
            blk.size.z = len((l - r) * 0.25);
        }
        blk.X = norm(fwd);
        blk.Y = up - blk.X * dot(up, blk.X);
        blk.Y = norm(blk.Y);
        blk.Z = cross(blk.X, blk.Y);
        for (int b = 0; b < 8; b += 2) blk.rear += e[b];
        blk.rear = blk.rear * 0.25;
        const char* id = vs.cylinders >= 7 ? "crate_engine_v8" : vs.cylinders >= 5 ? "crate_engine_v6" : "crate_engine_i4";
        const Bnd b = bounds(id);
        const V3 sc = b.size();
        blk.scale = V3(clampTo(blk.size.x / sc.x, 0.75, 1.2), clampTo((blk.size.y - 0.1) / sc.y, 0.7, 1.2), clampTo(blk.size.z / sc.z, 0.75, 1.2));
        // Its middle in the block's, its top a hand under the block's (the hood is over it).
        const V3 m = b.mid();
        blk.origin = blk.c - blk.X * (m.x * blk.scale.x) + blk.Y * (blk.size.y * 0.5 - 0.1 - b.hi.y * blk.scale.y) - blk.Z * (m.z * blk.scale.z);
        put(id, blk.origin, blk.X, blk.Y, blk.scale);
        blk.rear = blk.origin + blk.X * (b.lo.x * blk.scale.x);  // (on the crank axis)
        blk.ok = true;
    }

    // Gearbox (and, on a 4x4, the transfer case behind it) in line with the crank axis, from the
    // flywheel to where the prop shaft starts.
    void drivetrain() {
        if (!blk.ok) return;
        const bool front = !vs.midEngine && !vs.transverse;
        V3 tail;
        for (size_t i = 8; i < c.engineNodes.size() && i < 12; i++) tail += P(c.engineNodes[i]) * 0.25;
        V3 D = -blk.X;
        double total = std::max(0.3, vs.gearboxLen);
        if (front && c.gearboxOut >= 0) {
            const V3 g = P(c.gearboxOut) - blk.rear;
            total = std::max(0.3, len(g));
            D = norm(g);
        } else {
            total = std::max(0.3, dot(tail - blk.rear, D));
        }
        const bool automatic = vs.stallRpm > 0;
        const char* gb = automatic ? "gearbox_auto" : "gearbox_manual";
        const bool xfer = front && vs.transfer != XFER_NONE;
        const double lg = xfer ? total * 0.6 : total;
        const Bnd g = bounds(gb);
        const double bell = g.hi.x - 0.04;  // (the bell housing's face on the engine)
        const double sg = clampTo(lg / (bell - g.lo.x), 0.3, 1.1);
        const double cs = clampTo(vs.engineWid * 0.62 / g.size().z, 0.7, 1.05);
        const V3 up = blk.Y;
        put(gb, blk.rear + D * (bell * sg - 0.02), -D, up, V3(sg, cs, cs), -1, kNoLever);
        if (!xfer) return;
        const char* tc = "transfer_case";
        const Bnd t = bounds(tc);
        const double xin = 0.2;
        const double st = clampTo((total - lg + 0.04) / (xin - t.lo.x), 0.3, 1.1), ts = clampTo(cs * 0.9, 0.7, 1.0);
        put(tc, blk.rear + D * (lg - 0.02 + xin * st), -D, up, V3(st, ts, ts), -1, kNoLever);
    }

    // One axle's differential, drive flange stubs out to each side.
    void diffs() {
        for (const Axle& ax : c.axles) {
            if (ax.wheel[0] < 0 || ax.wheel[1] < 0) continue;
            const Wheel &wl = c.wheels[ax.wheel[0]], &wr = c.wheels[ax.wheel[1]];
            if (!wl.driven && !wr.driven) continue;
            V3 lat = norm(P(wr.inner) - P(wl.inner));
            V3 at, X = ax.front ? fr.fwd : -fr.fwd;  // (the model's pinion is at its back: toward the engine on a front axle)
            if (!ax.housing.empty()) {
                // A live axle: the pinion's snout points up the shaft.
                at = P(ax.housing[0]);
                if (ax.housing.size() > 1) X = norm(at - P(ax.housing[1]));
            } else if (ax.diffNode >= 0) {
                at = P(ax.diffNode);
            } else {
                continue;
            }
            const double sgn = dot(X, fr.fwd) >= 0 ? 1.0 : -1.0;
            V3 Z = lat * sgn;
            Z = norm(Z - X * dot(Z, X));
            put("differential", at, X, cross(Z, X), V3(1, 1, 1), 0);
        }
    }

    void shafts(int level) {
        // Prop shafts: a double-ended telescoping tube, stretched to the beam.
        for (const Axle& ax : c.axles) {
            if (ax.propShaft < 0) continue;
            const Beam& b = c.beams[ax.propShaft];
            if (b.broken) continue;
            const V3 a = P(b.a), e = P(b.b);
            const Bnd s = bounds("drive_shaft");
            const double L = len(e - a);
            if (L < 0.1) continue;
            put("drive_shaft", (a + e) * 0.5, e - a, fr.up, V3(L / s.size().x, 0.75 * sz, 0.75 * sz));
        }
        if (level < PARTS_ALL) return;
        // Half shafts to independent wheels, from the diff's flange to the hub.
        for (const Wheel& wh : c.wheels) {
            if (wh.detached || wh.shaftBroken) continue;
            for (int bi : wh.shafts) {
                const Beam& b = c.beams[bi];
                if (b.type != BEAM_SHAFT || b.broken || (b.a != wh.inner && b.b != wh.inner)) continue;
                const V3 hub = P(b.a == wh.inner ? b.a : b.b), diff = P(b.a == wh.inner ? b.b : b.a);
                const V3 d = norm(hub - diff);
                const V3 from = diff + d * 0.2;  // (the housing's stub ends there)
                const double L = dot(hub - from, d);
                if (L < 0.12) continue;
                const Bnd s = bounds("cv_axle");
                put("cv_axle", (from + hub) * 0.5, d, fr.up, V3(L / s.size().x, sz, sz));
            }
        }
    }

    // Arms, tie rods and trailing links along their beams.
    void links() {
        // A wishbone's two legs make one A-arm: the ball joint at the tip, the pivots apart behind it.
        struct Arm {
            int n = 0;
            V3 body[2], joint;
        };
        std::map<int, Arm> arms;  // (wheel * 2 + upper)
        for (const SuspLink& l : c.links) {
            const Beam& b = c.beams[l.beam];
            if (b.broken) continue;
            const bool aBody = b.a < c.frameNodes;
            const V3 body = aBody ? P(b.a) : P(b.b), joint = aBody ? P(b.b) : P(b.a);
            const double span = len(joint - body);
            if (span < 0.04) continue;
            const double w = clampTo(sz, 0.8, 1.1);
            switch (l.kind) {
            case LINK_LOWER_ARM:
            case LINK_UPPER_ARM: {
                Arm& a = arms[l.wheel * 2 + (l.kind == LINK_UPPER_ARM)];
                if (a.n < 2) a.body[a.n] = body;
                a.n++;
                a.joint = joint;
                break;
            }
            case LINK_TIE_ROD:
            case LINK_TOE_LINK: {
                const Bnd s = bounds("tie_rod");
                put("tie_rod", (body + joint) * 0.5, joint - body, fr.up, V3(span / s.size().x, w, w));
                break;
            }
            case LINK_TRAILING:
            case LINK_PANHARD: {
                const Bnd s = bounds("control_arm", 2);  // (the straight radius arm)
                put("control_arm", (body + joint) * 0.5, joint - body, fr.up, V3(span / (s.size().x * 0.95), w, w), 2);
                break;
            }
            default: break;
            }
        }
        for (const auto& kv : arms) {
            const Arm& a = kv.second;
            const int v = pickedOf("control_arm") == 2 ? 0 : pickedOf("control_arm");
            const Bnd s = bounds("control_arm", v);
            if (a.n >= 2) {
                const V3 m = (a.body[0] + a.body[1]) * 0.5;
                const V3 X = norm(a.joint - m);
                V3 Z = a.body[0] - a.body[1];
                Z = Z - X * dot(Z, X);
                const double spread = len(Z);
                Z = norm(Z);
                V3 Y = cross(Z, X);
                if (dot(Y, fr.up) < 0) Z = -Z, Y = -Y;
                put("control_arm", (m + a.joint) * 0.5, X, Y, V3(len(a.joint - m) / (s.size().x * 0.92), 1, clampTo(spread / 0.34, 0.4, 2.5)), v);
            } else if (a.n == 1) {  // (one leg gone: what's left is a bare link)
                const Bnd r = bounds("control_arm", 2);
                const V3 d = a.joint - a.body[0];
                put("control_arm", (a.body[0] + a.joint) * 0.5, d, fr.up, V3(len(d) / (r.size().x * 0.95), 1, 1), 2);
            }
        }
    }

    // Coil-overs on the spring beams (a leaf-sprung axle's spring is its leaf, drawn below).
    void springs() {
        const char* id = "coilover";
        const int v = pickedOf(id);
        // Eye to eye, the middle's offset along the axis, and the spring's axis above the model's floor.
        static const double eyes[3] = {0.714, 0.62, 0.733}, midX[3] = {-0.005, -0.025, -0.0085}, axisY[3] = {0.0785, 0.06, 0.0925};
        const int k = std::min(v, 2);
        for (const Wheel& wh : c.wheels) {
            if (wh.spring < 0 || wh.shackle >= 0) continue;
            const Beam& b = c.beams[wh.spring];
            if (b.broken) continue;
            const bool aBody = b.a < c.frameNodes;
            const V3 lower = aBody ? P(b.b) : P(b.a), upper = aBody ? P(b.a) : P(b.b);
            const double L = len(upper - lower);
            if (L < 0.08) continue;
            const double cs = clampTo(sz, 0.8, 1.1), sx = L / eyes[k];
            V3 X = norm(upper - lower), Y = fr.up - X * dot(fr.up, X);
            Y = norm(Y);
            V3 Z = cross(X, Y);
            // Its reservoir (and the like) on the inboard side.
            const V3 inward = fr.com - (lower + upper) * 0.5;
            if (dot(Z, inward) < 0) Y = -Y, Z = -Z;
            const V3 mid = (lower + upper) * 0.5;
            put(id, mid - X * (midX[k] * sx) - Y * (axisY[k] * cs), X, Y, V3(sx, cs, cs), -1);
        }
    }

    // Leaf packs: the front eye on its hanger node, the rear eye hanging from the shackle node, the
    // middle clamped on the axle tube (spring over axle). The hangers sit above the axle, so the pack
    // is arched ends-up; the model's arch is fitted so the eyes land on their mounts (in 1 cm steps:
    // each is a mesh).
    void leaves() {
        static const double stackT[2] = {0.009, 0.016}, eyeDrop[2] = {0.015, 0.01}, eyeHalf[2] = {0.662, 0.652};
        static const double lift = 0.052, shackle = 0.075;  // (parts.cpp: the stack's underside above the tube's axis; the shackle's length)
        const mk::ModelDef* def = mk::findModel("leaf_pack");
        if (!def) return;
        const int v = pickedOf("leaf_pack"), k = std::min(v, 1);
        const int leaves = std::max(2, mk::pickedParams(*def, v).i("leaves"));
        for (const SuspLink& l : c.links) {
            if (l.kind != LINK_LEAF || l.wheel < 0) continue;
            const Wheel& wh = c.wheels[l.wheel];
            const Beam& b = c.beams[l.beam];
            if (b.broken || wh.shackle < 0) continue;
            const bool aBody = b.a < c.frameNodes;
            const V3 front = aBody ? P(b.a) : P(b.b), seat = aBody ? P(b.b) : P(b.a);
            const V3 rear = P(wh.shackle) - fr.up * shackle;  // (the eye hangs under the frame pin)
            const double span = len(front - rear);
            if (span < 0.4) continue;
            V3 X = norm(front - rear), Y = fr.up - X * dot(fr.up, X);
            Y = len(Y) > 1e-6 ? norm(Y) : norm(cross(cross(X, kUp), X));
            // The tube under its middle: the point on the housing's line nearest the eyes' middle.
            const V3 mid = (front + rear) * 0.5;
            V3 tube = seat - Y * 0.06;
            for (const Axle& ax : c.axles) {
                if (ax.housing.empty() || (ax.wheel[0] != l.wheel && ax.wheel[1] != l.wheel)) continue;
                const V3 a = P(ax.housing[0]), z = (wh.tieRod >= 0 ? (P(wh.lower) + P(wh.top)) * 0.5 : P(wh.inner)) - a;
                const double zz = dot(z, z);
                tube = a + z * (zz > 1e-9 ? clampTo(dot(mid - a, z) / zz, 0, 1) : 0);
            }
            // The eyes' height over the tube, across the chord: the arch that puts them there.
            const V3 at = tube + X * dot(mid - tube, X);
            const double rise = dot(mid - at, Y);
            const double camber = clampTo(std::round((lift + leaves * stackT[k] - eyeDrop[k] - rise) * 100) / 100, -0.3, 0.14);
            const double eyeY = lift - camber + leaves * stackT[k] - eyeDrop[k];
            put("leaf_pack", at + Y * (rise - eyeY), X, Y, V3(span * 0.5 / eyeHalf[k], 1, 1), -1, {{"camber", camber}});
        }
    }
};

}  // namespace

void carPartModels(const Car& c, std::vector<Placed>& out, int level) {
    if (c.wheels.empty() || !c.spec || c.nodes.empty()) return;
    Maker m{c, *c.spec, out, c.frame(), c.spec->wheelRadius / 0.32};
    m.engine();
    m.drivetrain();
    m.diffs();
    m.shafts(level);
    if (level < PARTS_ALL) return;
    m.links();
    m.springs();
    m.leaves();
}
