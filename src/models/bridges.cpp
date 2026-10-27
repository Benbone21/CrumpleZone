// Bridges: the procedural world's timber bridges over the big rivers (src/procdress.cpp puts one on every span the generator lays).
//
// Built along X (the span), +Z to its left, the origin on the middle of the deck's top (y = 0, the height the trail drives at). It is
// drawn only; what's driven on and hit keeps to it (procdress.cpp): on a bridge in fair shape the generator's stand-in boxes (a deck
// 0.3 m thick reaching a metre past each end, rails hw + 0.05 out, 0.37-0.53 m up, on five posts); on a worn one a box for each plank
// piece, tie, stringer and whole rail of its bridgelayout.h layout. Either way the two outer log stringers (hw - 0.3 out, 0.3-0.8 m
// down) and on a span over 22 m a pier 2 m long under the middle are the generator's boxes.
//
// Params: length (the span between the banks, m), half (the deck's half-width), endA / endB (how far the deck stands over the bank
// at its -X / +X end: the abutments reach down that far), pier (deck to river bed under the middle; the LONG design), damage, missing
// and gap (its wear: bridgelayout.h), seed and seed2 (each 1..63: kept exact by the mesh cache's rounding).
#include <algorithm>
#include <cmath>

#include "../bridgelayout.h"
#include "../modelkit.h"

using namespace mk;

namespace {

const V3 kPlank(0.50, 0.41, 0.30), kLog(0.34, 0.25, 0.17), kStone(0.47, 0.45, 0.41);

V3 vary(const V3& c, uint32_t seed, uint32_t i, double a = 0.12) {
    const double v = hash01(seed, i), w = hash01(seed + 31, i);
    return V3(c.x * (1 + (v - 0.5) * 2 * a + (w - 0.5) * 0.05), c.y * (1 + (v - 0.5) * 2 * a), c.z * (1 + (v - 0.5) * 2 * a - (w - 0.5) * 0.05));
}

struct Dims {
    double Lh, E, hw, endA, endB, pier;
    uint32_t seed;
    proc::BridgeLayout lay;
};
Dims dims(const P& p) {
    Dims d;
    d.Lh = 0.5 * std::max(4.0, p("length")), d.E = d.Lh + 1, d.hw = std::max(1.6, p("half"));
    d.endA = std::max(0.6, p("endA")), d.endB = std::max(0.6, p("endB")), d.pier = p("pier");
    d.seed = (uint32_t)std::max(1, p.i("seed"));
    proc::BridgeLook look;
    look.length = 2 * d.Lh, look.hw = d.hw, look.damage = p("damage"), look.missing = p("missing"), look.maxGap = p("gap");
    look.seed = proc::bridgeSeed(p.i("seed"), p.i("seed2"));
    d.lay = proc::bridgeLayout(look);
    return d;
}

// Old timber: browner when sound, silver-grey as it weathers, a green cast of moss on some.
V3 oldWood(const V3& c, const Dims& d, uint32_t salt, uint32_t i) {
    const double grey = std::clamp(0.25 + 0.7 * d.lay.damage, 0.0, 0.9) * (0.6 + 0.4 * hash01(d.seed + salt, i));
    V3 w = mix(vary(c, d.seed + salt, i, 0.16), V3(0.47, 0.46, 0.43) * (0.8 + 0.3 * hash01(d.seed + salt + 1, i)), grey);
    if (hash01(d.seed + salt + 2, i) < 0.12 * d.lay.damage) w = mix(w, V3(0.30, 0.35, 0.19), 0.2 + 0.25 * hash01(d.seed + salt + 3, i));
    return w;
}

// A log from a to b: 7-sided (low-poly), its ends showing; a green back of moss on top of the old ones.
void logAt(B& b, const V3& a, const V3& e, double r, uint32_t seed, uint32_t i, double moss = 0) {
    b.style(vary(kLog, seed, i, 0.14), WOOD);
    const double rr = r * (0.92 + 0.16 * hash01(seed + 5, i));
    b.cyl(a, e, rr, 7, true);
    if (moss > 0 && hash01(seed + 6, i) < moss) {
        const double t0 = 0.6 * hash01(seed + 7, i), t1 = std::min(1.0, t0 + 0.2 + 0.4 * hash01(seed + 8, i));
        b.style(V3(0.24, 0.32, 0.13) * (0.85 + 0.3 * hash01(seed + 9, i)), FOLIAGE);
        b.beam(a + (e - a) * t0 + V3(0, rr * 0.82, 0), a + (e - a) * t1 + V3(0, rr * 0.82, 0), rr * 1.1, rr * 0.35, V3(0, 1, 0));
    }
}

// The deck: plank pieces as bridgelayout.h has them (the same numbers make its collision: procdress.cpp), on ties across the
// stringers. A bridge in fair shape has its planks whole and running planks along the wheel tracks (their top the deck's); a worn
// one's planks are the deck's top themselves, some sagging, some broken and tipped, many gone.
void deck(B& b, const Dims& d) {
    const proc::BridgeLayout& L = d.lay;
    const double top = L.fair ? -0.05 : 0.0;
    uint32_t k = 0;
    for (const proc::BridgeLayout::Piece& q : L.deck) {
        k++;
        b.style(oldWood(kPlank, d, 11, k), WOOD);
        const double xm = 0.5 * (q.x0 + q.x1), wx = q.x1 - q.x0, w = q.z1 - q.z0;
        if (q.kind == 2) {  // tipped about its sound end
            const double th = q.roll, s = std::sin(std::fabs(th)), c = std::cos(th);
            const V3 piv(xm, top - q.drop, th > 0 ? q.z0 : q.z1), dir(0, -s, th > 0 ? c : -c);
            const V3 n(0, c, th > 0 ? s : -s);
            b.beam(piv + dir * 0.02 - n * (0.5 * proc::kPlankThick), piv + dir * w - n * (0.5 * proc::kPlankThick), wx, proc::kPlankThick, n);
        } else
            b.box(V3(xm, top - q.drop - 0.5 * proc::kPlankThick, 0.5 * (q.z0 + q.z1)), V3(wx, proc::kPlankThick, w - 0.01));
    }
    if (L.fair) {
        const double track = std::min(0.95, d.hw * 0.45);
        for (int sd = -1; sd <= 1; sd += 2)
            for (int q = -1; q <= 1; q++) {
                b.style(oldWood(kPlank * 0.92, d, 13, (uint32_t)(sd * 3 + q + 10)), WOOD);
                b.box(V3(0, -0.025, sd * track + q * 0.24), V3(2 * d.E - 0.1, 0.05, 0.22), 0.008);
            }
    }
    // Ties across under the planks (and out past the edges at the posts: the braces' feet).
    const double tieTop = top - proc::kPlankThick;
    for (int t = 0; t <= proc::bridgeTies(d.E); t++) {
        const double x = proc::bridgeTieX(d.E, t);
        b.style(oldWood(kLog * 1.1, d, 15, t), WOOD);
        b.box(V3(x, 0.5 * (tieTop - 0.3), 0), V3(0.2, tieTop + 0.3, 2 * d.hw + 0.2), 0.01);
    }
    // Stringers: logs end to end on the abutments (and the pier).
    const int n = proc::bridgeStringers(d.hw);
    for (int s = 0; s < n; s++) logAt(b, V3(-d.E, -0.55, proc::bridgeStringerZ(d.hw, s)), V3(d.E, -0.55, proc::bridgeStringerZ(d.hw, s)), 0.25, d.seed, 40 + s, 0.6 * L.damage);
}

// Rails between the posts (where the collision's are): whole, snapped (one half hanging off its post), or gone; posts standing,
// leaning out, or gone; each post braced down to a tie that reaches out under it.
void rails(B& b, const Dims& d) {
    const proc::BridgeLayout& L = d.lay;
    for (int side = 0; side < 2; side++) {
        const int sd = side ? 1 : -1;
        const double z = sd * (d.hw + 0.05);
        for (int k = 0; k < 4; k++) {
            const double xa = L.posts[side][k].x, xb = L.posts[side][k + 1].x;
            b.style(oldWood(kPlank * 0.95, d, 17, (uint32_t)(side * 8 + k)), WOOD);
            if (L.rail[side][k] == 0) b.box(V3(0.5 * (xa + xb), 0.45, z), V3(xb - xa + 0.2, 0.16, 0.15), 0.015);
            else if (L.rail[side][k] == 1) {  // snapped: the half on a standing post hangs from it
                const bool fromA = L.posts[side][k].state != 2 && (L.posts[side][k + 1].state == 2 || hash01(d.seed + 19, side * 8 + k) < 0.5);
                const double x0 = fromA ? xa : xb, dir = fromA ? 1 : -1, len = (xb - xa) * (0.35 + 0.25 * hash01(d.seed + 21, side * 8 + k));
                const double sag = 0.25 + 0.5 * hash01(d.seed + 23, side * 8 + k);
                b.beam(V3(x0, 0.45, z), V3(x0 + dir * len * std::cos(sag), 0.45 - len * std::sin(sag), z + sd * 0.1), 0.15, 0.16, V3(0, 1, 0));
            }
        }
        for (int k = 0; k <= 4; k++) {
            const proc::BridgeLayout::Post& p = L.posts[side][k];
            b.style(oldWood(kLog * 1.15, d, 25, (uint32_t)(side * 8 + k)), WOOD);
            b.box(V3(p.x, -0.3, sd * (d.hw + 0.3)), V3(0.2, 0.14, 0.75), 0.01);  // (the tie's end out under it)
            if (p.state == 2) continue;
            const V3 base(p.x, -0.8, p.z), up(std::sin(p.tip), std::cos(p.lean) * std::cos(p.tip), std::sin(p.lean));
            b.style(oldWood(kPlank * 0.85, d, 27, (uint32_t)(side * 8 + k)), WOOD);
            b.beam(base, base + up * 1.36, 0.18, 0.18, V3(1, 0, 0));
            if (p.state == 0) {
                b.style(oldWood(kLog * 1.15, d, 29, (uint32_t)(side * 8 + k)), WOOD);
                b.beam(V3(p.x, -0.3, sd * (d.hw + 0.65)), V3(p.x, 0.2, z + sd * 0.05), 0.12, 0.12, V3(1, 0, 0));
            }
        }
    }
}

// An abutment where the deck meets the bank: a log crib (or, on a short span, dry stone) under the deck's end, from just under the
// stringers down `depth` into the bank. at: -1 the -X end, +1 the +X end.
void abutment(B& b, const Dims& d, int at, double depth, bool stone) {
    const double x0 = at * (d.Lh - 0.4), x1 = at * (d.Lh + 1.6), xm = 0.5 * (x0 + x1), xl = std::fabs(x1 - x0);
    const double zw = d.hw + 0.3, top = -0.8, bot = -(depth + 0.5);
    if (stone) {
        const int rows = std::max(1, (int)std::ceil((top - bot) / 0.42));
        for (int r = 0; r < rows; r++) {
            const double y = top - 0.21 - r * 0.42;
            double z = -zw - (r % 2) * 0.35;
            int k = 0;
            while (z < zw) {
                const double len = std::min(zw - z, 0.55 + 0.5 * hash01(d.seed + 50 + r, k));
                if (len > 0.15) {
                    b.style(vary(kStone, d.seed + 60 + at, (uint32_t)(r * 31 + k), 0.18), ROCK);
                    b.box(V3(xm + at * 0.03 * r, y, z + 0.5 * len), V3(xl + 0.06 * r, 0.4, len - 0.04), 0.05);
                }
                z += len, k++;
            }
        }
        return;
    }
    // Crib: courses of logs, the long ones across (under the stringers' ends), the short ones along tying them, alternately.
    const int courses = std::max(2, (int)std::ceil((top - bot) / 0.3));
    for (int c = 0; c < courses; c++) {
        const double y = top - 0.15 - c * 0.3;
        if (c % 2 == 0)
            for (double x : {x0 + at * 0.25, x1 - at * 0.25}) logAt(b, V3(x, y, -zw - 0.25), V3(x, y, zw + 0.25), 0.15, d.seed + 70, (uint32_t)(c * 4 + (x == x0 ? 0 : 1)));
        else
            for (int sd = -1; sd <= 1; sd += 2) logAt(b, V3(x0 - at * 0.05, y, sd * zw), V3(x1 + at * 0.05, y, sd * zw), 0.15, d.seed + 71, (uint32_t)(c * 4 + sd + 2));
    }
    // Stone fill showing between the logs.
    b.style(vary(kStone * 0.9, d.seed + 80, (uint32_t)(at + 2)), ROCK);
    b.box(V3(xm, 0.5 * (top + bot) - 0.1, 0), V3(xl - 0.5, top - bot - 0.25, 2 * zw - 0.3), 0.08);
}

// The middle pier (LONG): where the collision's is (2 m along, out to hw - 0.4 across, from just under the stringers to 0.6 m into
// the bed). Log crib filled with stone, or on the longest spans courses of stone with a cutwater either way.
void pier(B& b, const Dims& d, bool stone) {
    if (d.pier <= 0.5) return;
    const double top = -0.8, bot = -(d.pier + 0.6), zw = d.hw - 0.4;
    if (stone) {
        const int rows = std::max(1, (int)std::ceil((top - bot) / 0.45));
        for (int r = 0; r < rows; r++) {
            const double y0 = top - (r + 1) * 0.45, f = 1.0 - 0.1 * (rows - 1 - r) / std::max(1, rows - 1);  // (battered: wider low down)
            const double hx = 0.9 * (0.82 + 0.18 * f), hz = zw * (0.9 + 0.1 * f), cut = std::min(hx, 0.8);
            const std::vector<V2> poly = {V2(-hx, -hz + cut), V2(0, -hz), V2(hx, -hz + cut), V2(hx, hz - cut), V2(0, hz), V2(-hx, hz - cut)};
            b.style(vary(kStone, d.seed + 90, (uint32_t)r, 0.12), ROCK);
            b.extrudeUp(poly, 0.43, y0);
        }
        b.style(vary(kLog, d.seed + 91, 1), WOOD);
        b.box(V3(0, top - 0.05, 0), V3(1.9, 0.12, 2 * zw), 0.02);  // (a timber cap under the stringers)
        return;
    }
    const int courses = std::max(2, (int)std::ceil((top - bot) / 0.3));
    for (int c = 0; c < courses; c++) {
        const double y = top - 0.15 - c * 0.3;
        if (c % 2 == 0)
            for (int sx = -1; sx <= 1; sx += 2) logAt(b, V3(sx * 0.82, y, -zw), V3(sx * 0.82, y, zw), 0.15, d.seed + 92, (uint32_t)(c * 4 + sx + 1));
        else
            for (int sz = -1; sz <= 1; sz += 2) logAt(b, V3(-1.0, y, sz * (zw - 0.15)), V3(1.0, y, sz * (zw - 0.15)), 0.15, d.seed + 93, (uint32_t)(c * 4 + sz + 2));
    }
    b.style(vary(kStone * 0.85, d.seed + 94, 3), ROCK);
    b.box(V3(0, 0.5 * (top + bot), 0), V3(1.4, top - bot - 0.2, 2 * zw - 0.4), 0.08);
}

// SHORT (up to 12 m): log stringers on dry-stone abutments.
void shortBridge(B& b, const P& p) {
    const Dims d = dims(p);
    deck(b, d);
    rails(b, d);
    abutment(b, d, -1, d.endA, true);
    abutment(b, d, 1, d.endB, true);
}

// MEDIUM (12-22 m): log cribs at the banks, struts from each crib's face up under the stringers.
void mediumBridge(B& b, const P& p) {
    const Dims d = dims(p);
    deck(b, d);
    rails(b, d);
    abutment(b, d, -1, d.endA, false);
    abutment(b, d, 1, d.endB, false);
    for (int at = -1; at <= 1; at += 2) {
        const double depth = at < 0 ? d.endA : d.endB, foot = -std::min(depth, 2.4);
        for (int sd = -1; sd <= 1; sd += 2) {
            const double z = sd * (d.hw - 0.3);
            logAt(b, V3(at * (d.Lh - 0.35), foot, z), V3(at * (d.Lh - 3.2), -0.75, z), 0.13, d.seed + 100, (uint32_t)(at + 2) * 4 + sd + 1);
        }
    }
}

// LONG (over 22 m): log cribs at the banks and a pier in the middle (crib, or stone past 34 m).
void longBridge(B& b, const P& p) {
    const Dims d = dims(p);
    deck(b, d);
    rails(b, d);
    abutment(b, d, -1, d.endA, false);
    abutment(b, d, 1, d.endB, false);
    pier(b, d, d.Lh * 2 > 34);
}

std::vector<Param> params(double len, double pierDepth, double damage) {
    return {num("length", "SPAN", len, 4, 64, 0.5), num("half", "HALF WIDTH", 2.6, 1.6, 4.5, 0.05), num("endA", "BANK A", 1.8, 0.6, 8, 0.1),
            num("endB", "BANK B", 1.8, 0.6, 8, 0.1), num("pier", "PIER", pierDepth, 0, 10, 0.1), num("damage", "WEAR", damage, 0, 1, 0.05),
            num("missing", "PLANKS GONE (WORST)", 0.35, 0, 0.8, 0.05), num("gap", "LONGEST HOLE ON THE LINE", 0.75, 0.3, 1.2, 0.05), seedParam(5),
            Param{"seed2", "SEED 2", 1, 1, 63, 1, {}}};
}

Reg timberBridge({"timber_bridge", "TIMBER BRIDGE", "TRAIL AND ROAD", "The procedural world's bridges over the big rivers (by span: procdress.cpp)", 20,
                  {
                      {"SHORT", "Log stringers on dry-stone abutments (up to 12 m)", params(10, 0, 0.6), shortBridge},
                      {"MEDIUM", "Log cribs at the banks, struts under the stringers (12-22 m)", params(18, 0, 0.8), mediumBridge},
                      {"LONG", "Log cribs and a middle pier: crib, stone past 34 m (over 22 m)", params(30, 3.2, 0.1), longBridge},
                  }});

}  // namespace
