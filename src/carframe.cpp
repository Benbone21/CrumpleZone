#include "carframe.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include "modelkit.h"

namespace {

// How each truck's frame is built: the rail section (boxed or C-channel, where a boxed one turns to an open
// C), the crossmembers, the brackets and so on: indices into each model's designs (frames.cpp).
struct Style {
    const char* id;
    int rail;           // frame_rail design: 0 boxed, 1 C-channel
    double cTailX;      // a boxed frame turns C-channel behind this (rest x; -1e9: never)
    int cross;          // ordinary crossmembers: 0 channel, 1 tube
    double crossCX;     // ...and channels behind this (as the C-channel tail has riveted ones)
    int coat;
    double wear;
    int hanger, shackle, tower, stay, mount, hookFront, hookRear, skid, pad;
};

const Style kStyles[] = {
    // id        rail cTail  cross cCX   coat wear hang shk tow stay mnt hkF hkR skid pad
    {"blazer",   1,  -1e9,   0,  1e9,   0, 0.40,  0,   0,  1,  0,   0,  1,  0,  0,   0},  // 1972 K5: riveted C rails, channel crossmembers, flat skid
    {"bronco",   0,  -1e9,   1,  -1e9,  1, 0.05,  1,   1,  0,  1,   1,  0,  0,  1,   1},  // boxed all along, hydroformed tubes
    {"4runner",  0,  -1.50,  1,  -1.50, 0, 0.12,  0,   0,  0,  1,   0,  1,  0,  1,   1},  // boxed, an open C tail
    {"tacoma",   0,  -0.35,  1,  -0.35, 0, 0.10,  1,   1,  0,  1,   1,  0,  0,  0,   0},  // boxed front, C-channel rear, tubes up front
    {"hellboy",  0,  -1e9,   1,  -1e9,  0, 0.25,  0,   0,  1,  1,   0,  1,  0,  1,   1},  // Ram rails boxed with 3/8 plate, DOM tube crossmembers
};

const V3 kUp(0, 1, 0);

double cm(double v) { return std::round(v * 100) / 100; }  // (params in whole centimetres: few meshes to cache)

struct Maker {
    const Car& c;
    const VehicleSpec& vs;
    const Style& st;
    std::vector<Placed>& out;
    Frame fr;
    const double h, w;  // the rail section (m)

    V3 P(int n) const { return c.nodes[n].p; }
    int Ln(int station, int side, int corner) const { return c.ladder[station * 8 + side * 4 + corner]; }
    V3 L(int station, int side, int corner) const { return P(Ln(station, side, corner)); }
    V3 Lrest(int station, int side, int corner) const { return c.restLocal[Ln(station, side, corner)]; }

    // A model placed with its front toward X and its up near Y (turned to be square), +Z its left; `out` is which way is outboard
    // here (the models are built for the left side: "mir" builds the right).
    void put(const char* id, const V3& at, V3 X, V3 Y, const V3& scale, int variant, const V3& outward, std::initializer_list<std::pair<std::string, double>> params) {
        X = norm(X);
        Y = Y - X * dot(Y, X);
        Y = len(Y) > 1e-6 ? norm(Y) : norm(cross(cross(X, kUp), X));
        Placed pl = takePlaced();  // (last frame's, its room kept: carparts.h)
        pl.id = id, pl.p = at, pl.oriented = true, pl.bare = true, pl.variant = variant, pl.scale = scale;
        pl.ax[0] = X, pl.ax[1] = Y, pl.ax[2] = cross(X, Y);
        std::vector<std::pair<std::string, double>>& set = pl.set;  // (one grab: the part's few params and the three below)
        set.reserve(params.size() + 3);
        set.assign(params.begin(), params.end());
        set.push_back({"mir", dot(pl.ax[2], outward) < 0 ? 1.0 : 0.0});
        set.push_back({"coat", (double)st.coat});
        set.push_back({"wear", st.wear});
        out.push_back(std::move(pl));
    }

    // ---- The rails: one model per station-to-station stretch of each rail, from its eight corner nodes. ----
    void rails() {
        for (int side = 0; side < 2; side++)
            for (int s = 0; s + 1 < c.ladderStations; s++) {
                V3 a, b, bot, top, outer, inner;
                double width = 0, height = 0;
                for (int k = 0; k < 4; k++) {
                    const V3 pa = L(s, side, k), pb = L(s + 1, side, k);
                    a += pa * 0.25, b += pb * 0.25;
                    (k & 2 ? top : bot) += (pa + pb) * 0.25;
                    (k & 1 ? outer : inner) += (pa + pb) * 0.25;
                }
                for (int k = 0; k < 2; k++) {
                    width += (len(L(s, side, 1 + 2 * k) - L(s, side, 2 * k)) + len(L(s + 1, side, 1 + 2 * k) - L(s + 1, side, 2 * k))) * 0.25;
                    height += (len(L(s, side, 2 + k) - L(s, side, k)) + len(L(s + 1, side, 2 + k) - L(s + 1, side, k))) * 0.25;
                }
                const double span = len(b - a), restSpan = len(Lrest(s, side, 0) - Lrest(s + 1, side, 0));
                if (span < 0.2 * restSpan || span > 3 * restSpan) continue;  // (torn apart or squashed flat)
                const V3 centre = (a + b) * 0.5;
                const double restX = (Lrest(s, side, 0).x + Lrest(s + 1, side, 0).x) * 0.5;
                const bool channel = st.rail == 1 || restX < st.cTailX;
                put("frame_rail", centre, b - a, top - bot, V3(span / 0.42 * 1.05, height / h, width / w), channel ? 1 : 0, outer - inner,
                    {{"h", h}, {"w", w}});
            }
    }

    // ---- The crossmembers, between the rails at the stations the physics ties them at. ----
    void crossmembers() {
        if (c.crossStations.empty()) return;
        // The gearbox's: the one nearest its tail.
        const double xGear = vs.engineX - vs.engineLen / 2 - vs.gearboxLen;
        int gear = -1;
        double best = 1e9;
        for (int s : c.crossStations) {
            const double x = Lrest(s, 0, 0).x;
            if (s != c.crossStations.front() && s != c.crossStations.back() && std::fabs(x - xGear) < best) best = std::fabs(x - xGear), gear = s;
        }
        for (int s : c.crossStations) {
            V3 in0, in1, bot, top;
            for (int k : {0, 2}) {
                in0 += L(s, 0, k) * 0.5, in1 += L(s, 1, k) * 0.5;
                (k ? top : bot) += (L(s, 0, k) + L(s, 1, k)) * 0.5;
            }
            V3 S = in1 - in0, Z = norm(S);
            if (dot(Z, fr.left) < 0) Z = -Z;
            V3 Y = top - bot;
            Y = norm(Y - Z * dot(Y, Z));
            const V3 X = cross(Y, Z);
            const double span = cm(len(Lrest(s, 1, 0) - Lrest(s, 0, 0)));
            const double x = Lrest(s, 0, 0).x;
            const bool end = s == c.crossStations.front() || s == c.crossStations.back();
            const int kind = s == gear ? 2 : end ? 0 : (st.cross == 1 && x > st.crossCX) ? 1 : 0;
            put("frame_cross", (in0 + in1) * 0.5, X, Y, V3(1, 1, len(S) / std::max(0.1, span)), kind, Z, {{"span", span}, {"h", h}, {"rad", 0.036}});
        }
    }

    // ---- Where a point sits against its rail: for the brackets, towers and stays. ----
    struct Seat {
        V3 outB, outT;     // the rail's outside face at the point's place: bottom and top edge
        V3 out, up;        // outward across the rail (unit), up the rail's face
        double gap, dyb, dyt;  // how far outboard of the face the point is; how far above the rail's underside and top (m)
        int side;
    };
    Seat seatOf(int node) const {
        Seat q;
        const V3 rest = c.restLocal[node], p = P(node);
        q.side = rest.z > 0 ? 1 : -1;
        q.outB = c.railPoint(q.side, rest.x, 1), q.outT = c.railPoint(q.side, rest.x, 3);
        const V3 inB = c.railPoint(q.side, rest.x, 0);
        q.up = norm(q.outT - q.outB);
        V3 o = q.outB - inB;
        o = o - q.up * dot(o, q.up);
        q.out = norm(o);
        q.gap = dot(p - q.outB, q.out);
        q.dyb = dot(p - q.outB, q.up);
        q.dyt = dot(p - q.outT, q.up);
        return q;
    }
    V3 outwardOf(const V3& p) const { return fr.left * dot(p - fr.com, fr.left); }

    // A bar from the rail's face out to `p`'s place.
    void stayTo(const Seat& q, double reachUp, const V3& p) {
        const double L0 = q.gap + 0.05;
        if (q.gap < 0.03) return;
        const V3 base = (reachUp > 0 ? q.outT : q.outB);
        put("frame_stay", base + q.out * ((q.gap - 0.05) * 0.5) + q.up * (reachUp > 0 ? -0.02 : 0.02), q.out, q.up, V3(1, 1, 1), st.stay, outwardOf(p),
            {{"length", std::round(L0 * 100) / 100}});
    }

    // A bracket hung under the rail (or beside it) round a pin at node `n`: a spring hanger, a shackle mount or an arm's pivot.
    void hung(int n, bool shackle, double gap) {
        const Seat q = seatOf(n);
        const V3 p = P(n);
        double rise = -q.dyb;  // up to the rail's underside
        double g = gap;
        if (rise < 0.03) {     // the pin is in the rail's own depth (the frame pin of a shackle): plates either side of it
            rise = 0.045;
            if (std::fabs(q.gap) < 0.035) g = w + 0.02;
        }
        const double restRise = cm(rise);
        stayTo(q, -1, p);
        put(shackle ? "frame_shackle" : "frame_hanger", p, fr.fwd, fr.up, V3(1, restRise > 0.01 ? rise / restRise : 1, 1), shackle ? st.shackle : st.hanger, outwardOf(p),
            {{"rise", restRise}, {"gap", cm(g * 200) / 200.0}});
    }

    // A pedestal up from the rail's top to a spring seat or an upper arm's pivot at node `n`.
    void tower(int n) {
        const Seat q = seatOf(n);
        const V3 p = P(n);
        double drop = q.dyt;
        if (drop < 0.05) drop = 0.05;
        const double restDrop = cm(drop);
        stayTo(q, 1, p);
        put("frame_tower", p, fr.fwd, fr.up, V3(1, restDrop > 0.01 ? drop / restDrop : 1, 1), st.tower, outwardOf(p), {{"drop", restDrop}});
    }

    void brackets() {
        static thread_local std::vector<int> done;  // (kept from frame to frame: no garbage)
        done.clear();
        auto once = [&](int n) {
            if (std::find(done.begin(), done.end(), n) != done.end()) return false;
            done.push_back(n);
            return true;
        };
        for (const SuspLink& l : c.links) {
            const Beam& b = c.beams[l.beam];
            const int f = b.a < c.frameNodes ? b.a : b.b;
            if (f >= c.frameNodes || !once(f)) continue;
            switch (l.kind) {
            case LINK_LOWER_ARM: hung(f, false, 0.12); break;
            case LINK_UPPER_ARM: seatOf(f).dyt > 0.02 ? tower(f) : hung(f, false, 0.12); break;
            case LINK_TRAILING:
            case LINK_PANHARD:
            case LINK_LEAF: hung(f, false, 0.145); break;
            default: break;  // (the rack's tie rods, bushings and strut guides ride on things drawn elsewhere)
            }
        }
        for (const Wheel& wh : c.wheels) {
            if (wh.shackle >= 0 && once(wh.shackle)) hung(wh.shackle, true, 0.145);
            if (wh.spring >= 0 && wh.shackle < 0) {  // coil springs seat in a tower on the rail
                const Beam& b = c.beams[wh.spring];
                const int f = b.a < c.frameNodes ? b.a : b.b;
                if (f < c.frameNodes && once(f)) seatOf(f).dyt > 0.02 ? tower(f) : hung(f, false, 0.12);
            }
        }
    }

    // ---- Body mounts: on the rail beside every station the body's rubber mounts bolt to. ----
    void bodyMounts() {
        static thread_local std::vector<int> done;
        done.clear();
        for (int bi : c.bodyMountBeams) {
            const Beam& b = c.beams[bi];
            auto it = std::find(c.ladder.begin(), c.ladder.end(), b.a);  // (the rail's end of the mount: one of the two)
            if (it == c.ladder.end()) it = std::find(c.ladder.begin(), c.ladder.end(), b.b);
            if (it == c.ladder.end()) continue;
            const int idx = (int)(it - c.ladder.begin()), station = idx / 8, side = (idx / 4) % 2;
            if (std::find(done.begin(), done.end(), station * 2 + side) != done.end()) continue;
            done.push_back(station * 2 + side);
            const V3 top = L(station, side, 3);
            V3 along = station + 1 < c.ladderStations ? L(station, side, 3) - L(station + 1, side, 3) : L(station - 1, side, 3) - L(station, side, 3);
            put("frame_bodymount", top, along, L(station, side, 3) - L(station, side, 1), V3(1, 1, 1), st.mount, L(station, side, 3) - L(station, side, 2), {});
        }
    }

    // ---- Recovery points on the ends of both rails, and the skid plate. ----
    void hooks() {
        for (int end = 0; end < 2; end++) {
            const int s = end == 0 ? 0 : c.ladderStations - 1, t = end == 0 ? 1 : c.ladderStations - 2;
            for (int side = 0; side < 2; side++) {
                V3 mid, mid2, o;
                for (int k = 0; k < 4; k++) mid += L(s, side, k) * 0.25, mid2 += L(t, side, k) * 0.25;
                for (int k : {1, 3}) o += L(s, side, k) - L(s, side, k - 1);
                V3 Y = L(s, side, 2) - L(s, side, 0) + L(s, side, 3) - L(s, side, 1);
                put("frame_hook", mid, mid - mid2, Y, V3(1, 1, 1), end == 0 ? st.hookFront : st.hookRear, o, {{"h", h}, {"w", w}});
            }
        }
    }

    void skid() {
        if (c.skidNodes.size() < 3) return;
        V3 mid;
        for (int n : c.skidNodes) mid += P(n) / (double)c.skidNodes.size();
        const int near = [&] {  // the stations either side of it, for the rails' undersides over it
            int s = 0;
            const double x = c.restLocal[c.skidNodes[0]].x;
            while (s + 2 < c.ladderStations && Lrest(s + 1, 0, 0).x > x) s++;
            return s;
        }();
        V3 in0, in1, bot;
        for (int s : {near, near + 1}) {
            in0 += L(s, 0, 0) * 0.5, in1 += L(s, 1, 0) * 0.5;
        }
        V3 S = in1 - in0, Z = norm(S);
        if (dot(Z, fr.left) < 0) Z = -Z;
        const double rest = len(Lrest(near, 1, 0) - Lrest(near, 0, 0));
        const double rise = std::max(0.05, dot((in0 + in1) * 0.5 - mid, fr.up) + 0.03);  // (from the plate's top, 3 cm under the nodes')
        const double span = cm(rest);
        put("frame_skid", mid - fr.up * 0.03, fr.fwd, fr.up, V3(1, 1, len(S) / std::max(0.1, span)), st.skid, Z,
            {{"span", span}, {"rise", cm(rise)}, {"len", 0.5}});
    }

    // ---- The jack pads: one under each rail beside each wheel well (where Car::jackPoint is). ----
    void jackPads() {
        for (int k = 0; k < (int)c.wheels.size(); k++) {
            const V3 at = c.jackPoint(k);
            const int side = c.restLocal[c.wheels[k].inner].z > 0 ? 1 : -1;
            // The rail's axes there, from its stations' corners.
            const double x = c.jackPadX(k);
            const V3 xa = c.railPoint(side, x + 0.2, 1), xb = c.railPoint(side, x - 0.2, 1);
            const V3 oB = c.railPoint(side, x, 1), iB = c.railPoint(side, x, 0), oT = c.railPoint(side, x, 3);
            // The model's origin is the saddle spot (the pad's underside), `kJackPadDepth` under the rail's underside.
            put("frame_jackpad", at, xa - xb, oT - oB, V3(1, 1, 1), st.pad, oB - iB, {{"w", w}, {"mark", 1}});
        }
    }
};

const Style* styleOf(const VehicleSpec& vs) {
    for (const Style& s : kStyles)
        if (std::string(s.id) == vs.id) return &s;
    return nullptr;
}

}  // namespace

void carFrameModels(const Car& c, std::vector<Placed>& out, int level) {
    if (c.wheels.empty() || !c.spec || c.nodes.empty() || c.ladderStations < 2 || c.ladder.empty()) return;
    const Style* style = styleOf(*c.spec);
    if (!style) return;
    Maker m{c, *c.spec, *style, out, c.frame(), c.spec->railHeight, c.spec->railWidth};
    m.rails();
    m.crossmembers();
    m.hooks();
    m.skid();
    if (level < PARTS_ALL) return;
    m.brackets();
    m.bodyMounts();
    m.jackPads();
}
