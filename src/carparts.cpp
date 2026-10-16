#include "carparts.h"

#include <map>
#include <mutex>
#include <string>

#include <algorithm>
#include <cmath>

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

    std::vector<PartTag>* tags = nullptr;  // (the hover's: what each placement is)
    PartTag cur;                           // the part being laid out

    V3 P(int n) const { return c.nodes[n].p; }

    // The part the next placements belong to, and how it is.
    void part(PartKind k, PartState st, const std::string& name, int wheel = -1, int axle = -1, int beam = -1, int beam2 = -1, const std::string& detail = "") {
        cur = PartTag();
        cur.kind = k, cur.state = st, cur.wheel = wheel, cur.axle = axle, cur.beam = beam, cur.beam2 = beam2;
        if (tags) cur.name = name, cur.detail = detail;
    }
    std::string corner(int w) const { return std::string(c.wheels[w].front ? "FRONT " : "REAR ") + (c.wheels[w].left ? "LEFT" : "RIGHT"); }
    static uint8_t wearOf(PartState st) { return st == PS_BENT ? 1 : st == PS_BROKEN ? 2 : st == PS_FLOODED || st == PS_DEAD ? 3 : st == PS_DAMAGED ? 1 : 0; }

    // The model `id` with its origin at `at`, its front toward X, its up near Y, scaled along (front, up, left).
    Placed make(const char* id, const V3& at, V3 X, V3 Y, const V3& scale = V3(1, 1, 1), int variant = -1,
                const std::vector<std::pair<std::string, double>>& set = {}) const {
        X = norm(X);
        Y = Y - X * dot(Y, X);
        Y = len(Y) > 1e-6 ? norm(Y) : norm(cross(cross(X, kUp), X));
        Placed pl;
        pl.id = id, pl.p = at, pl.oriented = true, pl.bare = true, pl.variant = variant, pl.scale = scale, pl.set = set;
        pl.ax[0] = X, pl.ax[1] = Y, pl.ax[2] = cross(X, Y);
        return pl;
    }
    void add(Placed pl, int wear = -1) {
        pl.wear = wear >= 0 ? (uint8_t)wear : wearOf(cur.state);
        if (tags) {
            PartTag t = cur;
            t.index = out.size();
            tags->push_back(t);
        }
        out.push_back(pl);
    }
    void put(const char* id, const V3& at, V3 X, V3 Y, const V3& scale = V3(1, 1, 1), int variant = -1,
             const std::vector<std::pair<std::string, double>>& set = {}) {
        add(make(id, at, X, Y, scale, variant, set));
    }

    // A snapped part: `full` is it whole, lying from `a` (its -X end) to `e`. Cut at `frac` of its length, each
    // half hangs from its own end, drooped by gravity (about the horizontal across it), the torn ends capped.
    // `capR`: the tube's radius there (m).
    void snap(const Placed& full, const char* id, const V3& a, const V3& e, double frac, double droop, double capR, double flat = 1.0) {
        const Bnd bd = bounds(id, full.variant);
        const double cutLocal = bd.lo.x + (bd.hi.x - bd.lo.x) * frac;
        const V3 cutW = full.p + full.ax[0] * (cutLocal * full.scale.x);
        for (int h = 0; h < 2; h++) {
            const V3 q = h == 0 ? a : e, X = h == 0 ? full.ax[0] : -full.ax[0];  // (the way its free end lies from q)
            V3 hinge = cross(X, V3(0, -1, 0));
            hinge = len(hinge) > 0.25 ? norm(hinge) : fr.fwd;
            auto rot = [&](const V3& v) { return rotateAxis(v, hinge, droop); };
            Placed half = full;
            half.clip = true;
            if (h == 0) half.clipHi = (float)cutLocal;
            else half.clipLo = (float)cutLocal;
            half.p = q + rot(full.p - q);
            for (V3& v : half.ax) v = rot(v);
            add(half, 2);
            const double r = capR / 0.05;
            add(make("torn_end", q + rot(cutW - q), h == 0 ? half.ax[0] : -half.ax[0], half.ax[1], V3(std::max(0.5, r), r, r * flat)), 2);
        }
    }
    // A bent part: `full` as it lies, kinked at its middle by `h` (m) toward `u` (across it).
    void kink(const Placed& full, double span, V3 u, double h) {
        u = u - full.ax[0] * dot(u, full.ax[0]);
        u = len(u) > 1e-6 ? norm(u) : full.ax[1];
        const double beta = std::atan2(h, span * 0.5);
        const V3 n = norm(cross(full.ax[0], u));
        const V3 K = full.p + u * h;
        for (int s = 0; s < 2; s++) {
            Placed half = full;
            half.clip = true;
            if (s == 0) half.clipHi = 0.0f;
            else half.clipLo = 0.0f;
            half.p = K;
            for (V3& v : half.ax) v = rotateAxis(v, n, s == 0 ? beta : -beta);
            half.scale.x = full.scale.x / std::cos(beta);
            add(half, 1);
        }
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
        part(PK_ENGINE, c.hydrolocked ? PS_FLOODED : c.engineDead ? PS_DEAD : c.engineHealth < 0.6 ? PS_DAMAGED : PS_OK, "ENGINE", -1, -1, -1, -1, vs.engineName);
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
        part(PK_GEARBOX, PS_OK, automatic ? "AUTOMATIC GEARBOX" : "MANUAL GEARBOX", -1, -1, -1, -1, vs.gearboxName);
        put(gb, blk.rear + D * (bell * sg - 0.02), -D, up, V3(sg, cs, cs), -1, kNoLever);
        if (!xfer) return;
        const char* tc = "transfer_case";
        const Bnd t = bounds(tc);
        part(PK_TRANSFER, PS_OK, "TRANSFER CASE");
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
            part(PK_DIFF, PS_OK, std::string(ax.front ? "FRONT" : "REAR") + " DIFFERENTIAL", -1, (int)(&ax - &c.axles[0]));
            put("differential", at, X, cross(Z, X), V3(1, 1, 1), 0);
        }
    }

    // A hash for how a break falls (where along it, which way an arm kinks): the same beam breaks the same way.
    V3 sideways(int beam, int salt) const { return fr.up * (luck(beam, salt) < 0.5 ? 1.0 : -1.0); }
    static double luck(int beam, int salt) { return mk::hash01((uint32_t)beam * 7u + (uint32_t)salt, 3u); }

    void shafts(int level) {
        // Prop shafts: a double-ended telescoping tube, stretched to the beam. A snapped one hangs in two halves.
        for (int ai = 0; ai < (int)c.axles.size(); ai++) {
            const Axle& ax = c.axles[ai];
            if (ax.propShaft < 0) continue;
            const Beam& b = c.beams[ax.propShaft];
            const V3 a = P(b.a), e = P(b.b);
            const Bnd s = bounds("drive_shaft");
            const double L = len(e - a);
            if (L < 0.1) continue;
            part(PK_PROP_SHAFT, b.broken ? PS_BROKEN : PS_OK, std::string(ax.front ? "FRONT" : "REAR") + " DRIVE SHAFT", -1, ai, ax.propShaft);
            const Placed full = make("drive_shaft", (a + e) * 0.5, e - a, fr.up, V3(L / s.size().x, 0.75 * sz, 0.75 * sz));
            if (b.broken) snap(full, "drive_shaft", a, e, 0.42 + 0.16 * luck(ax.propShaft, 1), 0.6, 0.0445 * 0.75 * sz);
            else add(full);
        }
        if (level < PARTS_ALL) return;
        // Half shafts to independent wheels, from the diff's flange to the hub.
        for (int wi = 0; wi < (int)c.wheels.size(); wi++) {
            const Wheel& wh = c.wheels[wi];
            if (wh.detached) continue;
            for (int bi : wh.shafts) {
                const Beam& b = c.beams[bi];
                if (b.type != BEAM_SHAFT || (b.a != wh.inner && b.b != wh.inner)) continue;
                const V3 hub = P(b.a == wh.inner ? b.a : b.b), diff = P(b.a == wh.inner ? b.b : b.a);
                const V3 d = norm(hub - diff);
                const V3 from = diff + d * 0.2;  // (the housing's stub ends there)
                const double L = dot(hub - from, d);
                if (L < 0.12) continue;
                const bool snapped = b.broken || wh.shaftBroken;
                part(PK_CV_AXLE, snapped ? PS_BROKEN : PS_OK, corner(wi) + " CV AXLE", wi, -1, bi);
                const Bnd s = bounds("cv_axle");
                const Placed full = make("cv_axle", (from + hub) * 0.5, d, fr.up, V3(L / s.size().x, sz, sz));
                if (snapped) snap(full, "cv_axle", from, hub, 0.4 + 0.2 * luck(bi, 2), 0.7, 0.017 * sz);
                else add(full);
            }
        }
    }

    // Arms, tie rods and trailing links along their beams. A bent one kinks; a snapped one hangs in halves.
    void links() {
        // A wishbone's two legs make one A-arm: the ball joint at the tip, the pivots apart behind it.
        struct Arm {
            int n = 0, beam[2] = {-1, -1};
            V3 body[2], joint;
            bool broken = false, bent = false;
            bool upper = false;
            int wheel = -1;
        };
        std::map<int, Arm> arms;  // (wheel * 2 + upper)
        for (const SuspLink& l : c.links) {
            const Beam& b = c.beams[l.beam];
            const bool aBody = b.a < c.frameNodes;
            const V3 body = aBody ? P(b.a) : P(b.b), joint = aBody ? P(b.b) : P(b.a);
            const double span = len(joint - body);
            if (span < 0.04) continue;
            const double w = clampTo(sz, 0.8, 1.1);
            const bool isBent = beamBent(b);
            switch (l.kind) {
            case LINK_LOWER_ARM:
            case LINK_UPPER_ARM: {
                Arm& a = arms[l.wheel * 2 + (l.kind == LINK_UPPER_ARM)];
                if (a.n < 2) a.body[a.n] = body, a.beam[a.n] = l.beam;
                a.n++;
                a.joint = joint;
                a.broken |= b.broken, a.bent |= isBent;
                a.upper = l.kind == LINK_UPPER_ARM, a.wheel = l.wheel;
                break;
            }
            case LINK_TIE_ROD:
            case LINK_TOE_LINK: {
                const Wheel* wh = l.wheel >= 0 ? &c.wheels[l.wheel] : nullptr;
                const double off = wh ? std::fabs(wh->tieOffset) : 0;
                const bool kinked = !b.broken && (off > 0.002 || isBent);
                part(PK_TIE_ROD, b.broken ? PS_BROKEN : kinked ? PS_BENT : PS_OK, (l.wheel >= 0 ? corner(l.wheel) + " " : std::string()) + (l.kind == LINK_TOE_LINK ? "TOE LINK" : "TIE ROD"), l.wheel, -1, l.beam);
                const Bnd s = bounds("tie_rod");
                const Placed full = make("tie_rod", (body + joint) * 0.5, joint - body, fr.up, V3(span / s.size().x, w, w));
                if (b.broken) snap(full, "tie_rod", body, joint, 0.35 + 0.3 * luck(l.beam, 3), 0.7, 0.0128 * w);
                else if (kinked) kink(full, span, sideways(l.beam, 4), bendOf(span, std::max(off, std::fabs(b.L0 - b.Lorig))));
                else add(full);
                break;
            }
            case LINK_TRAILING:
            case LINK_PANHARD: {
                part(PK_TRAILING, b.broken ? PS_BROKEN : isBent ? PS_BENT : PS_OK, (l.wheel >= 0 ? corner(l.wheel) + " " : std::string(c.axles.size() > 1 ? "REAR " : "")) + (l.kind == LINK_PANHARD ? "TRACK BAR" : "TRAILING ARM"), l.wheel, -1, l.beam);
                const Bnd s = bounds("control_arm", 2);  // (the straight radius arm)
                const Placed full = make("control_arm", (body + joint) * 0.5, joint - body, fr.up, V3(span / (s.size().x * 0.95), w, w), 2);
                if (b.broken) snap(full, "control_arm", body, joint, 0.35 + 0.3 * luck(l.beam, 5), 0.6, 0.02 * w);
                else if (isBent) kink(full, span, sideways(l.beam, 6), bendOf(span, std::fabs(b.L0 - b.Lorig)));
                else add(full);
                break;
            }
            default: break;
            }
        }
        for (const auto& kv : arms) {
            const Arm& a = kv.second;
            const int v = pickedOf("control_arm") == 2 ? 0 : pickedOf("control_arm");
            const Bnd s = bounds("control_arm", v);
            const PartState st = a.broken ? PS_BROKEN : a.bent ? PS_BENT : PS_OK;
            part(a.upper ? PK_UPPER_ARM : PK_LOWER_ARM, st, (a.wheel >= 0 ? corner(a.wheel) + " " : std::string()) + (a.upper ? "UPPER" : "LOWER") + " CONTROL ARM", a.wheel, -1, a.beam[0], a.beam[1]);
            if (a.n >= 2) {
                const V3 m = (a.body[0] + a.body[1]) * 0.5;
                const V3 X = norm(a.joint - m);
                V3 Z = a.body[0] - a.body[1];
                Z = Z - X * dot(Z, X);
                const double spread = len(Z);
                Z = norm(Z);
                V3 Y = cross(Z, X);
                if (dot(Y, fr.up) < 0) Z = -Z, Y = -Y;
                const double span = len(a.joint - m);
                const Placed full = make("control_arm", (m + a.joint) * 0.5, X, Y, V3(span / (s.size().x * 0.92), 1, clampTo(spread / 0.34, 0.4, 2.5)), v);
                double dl = 0;
                for (int k = 0; k < 2; k++)
                    if (a.beam[k] >= 0) dl = std::max(dl, std::fabs(c.beams[a.beam[k]].L0 - c.beams[a.beam[k]].Lorig));
                if (a.broken) snap(full, "control_arm", m, a.joint, 0.4 + 0.2 * luck(a.beam[0], 7), 0.6, 0.022, 1.0);
                else if (a.bent) kink(full, span, sideways(a.beam[0], 8), bendOf(span, dl));
                else add(full);
            } else if (a.n == 1) {  // (one leg gone: what's left is a bare link)
                const Bnd r = bounds("control_arm", 2);
                const V3 d = a.joint - a.body[0];
                const Placed full = make("control_arm", (a.body[0] + a.joint) * 0.5, d, fr.up, V3(len(d) / (r.size().x * 0.95), 1, 1), 2);
                if (a.broken) snap(full, "control_arm", a.body[0], a.joint, 0.5, 0.6, 0.02);
                else if (a.bent) kink(full, len(d), fr.up, bendOf(len(d), std::fabs(c.beams[a.beam[0]].L0 - c.beams[a.beam[0]].Lorig)));
                else add(full);
            }
        }
    }

    // How far a part of `span` long, shortened (or stretched) by `dl`, bows at its middle (two straight halves of the
    // original length meeting at a kink), at least enough to see.
    static double bendOf(double span, double dl) { return clampTo(0.5 * std::sqrt(std::max(0.0, dl) * (2 * span + dl)), 0.025, span * 0.3); }

    // Coil-overs on the spring beams (a leaf-sprung axle's spring is its leaf, drawn below). A broken one is snapped.
    void springs() {
        const char* id = "coilover";
        const int v = pickedOf(id);
        // Eye to eye, the middle's offset along the axis, and the spring's axis above the model's floor.
        static const double eyes[3] = {0.714, 0.62, 0.733}, midX[3] = {-0.005, -0.025, -0.0085}, axisY[3] = {0.0785, 0.06, 0.0925};
        const int k = std::min(v, 2);
        for (int wi = 0; wi < (int)c.wheels.size(); wi++) {
            const Wheel& wh = c.wheels[wi];
            if (wh.spring < 0 || wh.shackle >= 0) continue;
            const Beam& b = c.beams[wh.spring];
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
            part(PK_COILOVER, b.broken ? PS_BROKEN : PS_OK, corner(wi) + " COILOVER", wi, -1, wh.spring);
            const Placed full = make(id, mid - X * (midX[k] * sx) - Y * (axisY[k] * cs), X, Y, V3(sx, cs, cs), -1);
            if (b.broken) snap(full, id, lower, upper, 0.45 + 0.1 * luck(wh.spring, 9), 0.7, 0.035 * cs);
            else add(full);
        }
    }

    // Leaf packs: the front eye on its hanger node, the rear eye hanging from the shackle node, the
    // middle clamped on the axle tube (spring over axle). The hangers sit above the axle, so the pack
    // is arched ends-up; the model's arch is fitted so the eyes land on their mounts (in 1 cm steps:
    // each is a mesh). A broken leaf spring sags in two halves from its eyes.
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
            if (wh.shackle < 0) continue;
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
            part(PK_LEAF, b.broken ? PS_BROKEN : PS_OK, corner(l.wheel) + " LEAF SPRING", l.wheel, -1, l.beam);
            const Placed full = make("leaf_pack", at + Y * (rise - eyeY), X, Y, V3(span * 0.5 / eyeHalf[k], 1, 1), -1, {{"camber", camber}});
            if (b.broken) snap(full, "leaf_pack", rear, front, 0.5, 0.45, 0.03, 1.6);
            else add(full);
        }
    }
};

}  // namespace

void carPartModels(const Car& c, std::vector<Placed>& out, int level, std::vector<PartTag>* tags) {
    if (c.wheels.empty() || !c.spec || c.nodes.empty()) return;
    Maker m{c, *c.spec, out, c.frame(), c.spec->wheelRadius / 0.32};
    m.tags = tags;
    m.engine();
    m.drivetrain();
    m.diffs();
    m.shafts(level);
    if (level < PARTS_ALL) return;
    m.links();
    m.springs();
    m.leaves();
}

const char* partStateText(PartState s) {
    switch (s) {
    case PS_OK: return "OK";
    case PS_BENT: return "BENT";
    case PS_BROKEN: return "BROKEN";
    case PS_FLOODED: return "FLOODED";
    case PS_DEAD: return "DEAD";
    case PS_DAMAGED: return "DAMAGED";
    }
    return "";
}

std::string PartTag::label() const { return name + ": " + (detail.empty() ? "" : detail + ", ") + partStateText(state); }

bool partMatchesFix(const PartTag& t, const Fix& f) {
    switch (f.kind) {
    case Fix::CV_AXLE: return t.kind == PK_CV_AXLE && t.wheel == f.wheel;
    case Fix::DRIVE_SHAFT: return t.kind == PK_PROP_SHAFT && t.axle == f.axle;
    case Fix::TIE_ROD: return t.kind == PK_TIE_ROD && (f.beam >= 0 ? t.beam == f.beam : t.wheel == f.wheel);
    case Fix::CONTROL_ARM:
        return (t.kind == PK_LOWER_ARM || t.kind == PK_UPPER_ARM || t.kind == PK_TRAILING) && f.beam >= 0 && (t.beam == f.beam || t.beam2 == f.beam);
    case Fix::SPARK_PLUGS: return t.kind == PK_ENGINE;
    default: return false;
    }
}

namespace {

// Where a ray meets a box in a placement's own space (its model's bounds, clipped to what's drawn, padded `pad` m).
bool rayBox(const Placed& pl, const Bnd& bd, const V3& eye, const V3& dir, double pad, double& tHit) {
    const V3 rel = eye - pl.p;
    double t0 = 0, t1 = 1e9;
    for (int i = 0; i < 3; i++) {
        const double sc = i == 0 ? pl.scale.x : i == 1 ? pl.scale.y : pl.scale.z;
        if (sc < 1e-6) return false;
        double lo = i == 0 ? bd.lo.x : i == 1 ? bd.lo.y : bd.lo.z, hi = i == 0 ? bd.hi.x : i == 1 ? bd.hi.y : bd.hi.z;
        if (i == 0 && pl.clip) lo = std::max(lo, (double)pl.clipLo), hi = std::min(hi, (double)pl.clipHi);
        lo -= pad / sc, hi += pad / sc;
        const double o = dot(rel, pl.ax[i]) / sc, d = dot(dir, pl.ax[i]) / sc;
        if (std::fabs(d) < 1e-9) {
            if (o < lo || o > hi) return false;
            continue;
        }
        double a = (lo - o) / d, b = (hi - o) / d;
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a), t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    tHit = t0;
    return true;
}

// The first time a ray meets the car's skin (any triangle that's still on) or a tire, up to `limit` (1e9: none).
double skinBlocks(const Car& c, const V3& eye, const V3& dir, double limit) {
    double best = limit;
    for (const Tri& tr : c.tris) {
        if (tr.torn || tr.shattered) continue;
        const V3 a = c.nodes[tr.v[0]].p, e1 = c.nodes[tr.v[1]].p - a, e2 = c.nodes[tr.v[2]].p - a;
        const V3 pv = cross(dir, e2);
        const double det = dot(e1, pv);
        if (std::fabs(det) < 1e-12) continue;
        const V3 tv = eye - a;
        const double u = dot(tv, pv) / det;
        if (u < 0 || u > 1) continue;
        const V3 qv = cross(tv, e1);
        const double v = dot(dir, qv) / det;
        if (v < 0 || u + v > 1) continue;
        const double t = dot(e2, qv) / det;
        if (t > 0.02 && t < best) best = t;
    }
    for (const Wheel& wh : c.wheels) {
        if (wh.detached || !wh.mounted) continue;
        const V3 hub = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5, ax = norm(c.nodes[wh.outer].p - c.nodes[wh.inner].p);
        const V3 o = eye - hub;
        const double oa = dot(o, ax), da = dot(dir, ax);
        const V3 op = o - ax * oa, dp = dir - ax * da;
        const double A = dot(dp, dp), B = 2 * dot(op, dp), C = dot(op, op) - wh.R * wh.R;
        if (A < 1e-12) continue;
        const double disc = B * B - 4 * A * C;
        if (disc < 0) continue;
        const double sq = std::sqrt(disc);
        for (double t : {(-B - sq) / (2 * A), (-B + sq) / (2 * A)})  // (the side wall of the tire, within its width)
            if (t > 0.02 && t < best && std::fabs(oa + da * t) < wh.width * 0.5) best = t;
        if (std::fabs(da) > 1e-9)  // ...or through a face
            for (double side : {-0.5, 0.5}) {
                const double t = (side * wh.width - oa) / da;
                if (t > 0.02 && t < best) {
                    const V3 q = op + dp * t;
                    if (dot(q, q) < wh.R * wh.R) best = t;
                }
            }
    }
    return best;
}

}  // namespace

bool pickCarPart(const Car& c, const V3& eye, const V3& dir, double maxDist, bool occlude, PartHit& hit) {
    hit = PartHit();
    if (c.isProp() || c.nodes.empty() || len(dir) < 1e-9) return false;
    // (not worth the build for a car nowhere near the ray)
    const V3 d = norm(dir);
    if (len(c.frame().com - eye) > maxDist + 6) return false;
    std::vector<Placed> placed;
    std::vector<PartTag> tags;
    carPartModels(c, placed, PARTS_ALL, &tags);
    struct Cand {
        double t;
        size_t i;
    };
    std::vector<Cand> cands;
    for (size_t i = 0; i < tags.size(); i++) {
        const Placed& pl = placed[tags[i].index];
        double t;
        if (rayBox(pl, bounds(pl.id.c_str(), pl.variant), eye, d, 0.035, t) && t <= maxDist) cands.push_back({t, i});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.t < b.t; });
    for (const Cand& cd : cands) {
        const PartState st = tags[cd.i].state;
        const bool glows = st == PS_BENT || st == PS_BROKEN;  // (a hurt part shows through the body: Renderer::xrayRange)
        if (occlude && !glows && skinBlocks(c, eye, d, cd.t - 0.03) < cd.t - 0.03) continue;
        hit.ok = true, hit.tag = tags[cd.i], hit.t = cd.t, hit.at = eye + d * cd.t;
        return true;
    }
    return false;
}
