#include "bodyshell.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace {

// ---- Binding to the physics -------------------------------------------------------

V3 trilinear(const V3 P[8], const V3& u, double w[8]) {
    V3 p;
    for (int b = 0; b < 8; b++) {
        w[b] = ((b & 1) ? u.x : 1 - u.x) * ((b & 2) ? u.y : 1 - u.y) * ((b & 4) ? u.z : 1 - u.z);
        p += P[b] * w[b];
    }
    return p;
}

// Lattice coordinates u of p in the hexahedron P (Newton on the trilinear map); returns
// how far outside the cell p lies (0 = inside).
double solveTrilinear(const V3 P[8], const V3& p, V3& u) {
    u = V3(0.5, 0.5, 0.5);
    double w[8];
    for (int it = 0; it < 14; it++) {
        V3 r = trilinear(P, u, w) - p;
        V3 J[3];
        for (int a = 0; a < 3; a++) {
            V3 up = u;
            (a == 0 ? up.x : a == 1 ? up.y : up.z) += 1e-4;
            double w2[8];
            J[a] = (trilinear(P, up, w2) - (r + p)) / 1e-4;
        }
        double det = dot(J[0], cross(J[1], J[2]));
        if (std::fabs(det) < 1e-12) break;
        V3 d(dot(r, cross(J[1], J[2])) / det, dot(J[0], cross(r, J[2])) / det, dot(J[0], cross(J[1], r)) / det);
        u -= d;
        if (len(d) < 1e-8) break;
    }
    return std::max({0.0, -u.x, u.x - 1, -u.y, u.y - 1, -u.z, u.z - 1});
}

struct Weights {
    std::vector<std::pair<int, float>> w;
};

// A point on the welded body: trilinear weights in the cell that holds it (or the
// nearest one, extrapolating a little for the shell's curves).
Weights bindBody(const Car& car, const V3& p) {
    double best = 1e18;
    Weights out;
    for (const auto& cell : car.cells) {
        V3 lo(1e9, 1e9, 1e9), hi(-1e9, -1e9, -1e9), P[8];
        for (int b = 0; b < 8; b++) {
            P[b] = car.restLocal[cell[b]];
            lo = V3(std::min(lo.x, P[b].x), std::min(lo.y, P[b].y), std::min(lo.z, P[b].z));
            hi = V3(std::max(hi.x, P[b].x), std::max(hi.y, P[b].y), std::max(hi.z, P[b].z));
        }
        double gap = std::max({lo.x - p.x, p.x - hi.x, lo.y - p.y, p.y - hi.y, lo.z - p.z, p.z - hi.z, 0.0});
        if (gap > std::min(best, 0.0) + 0.3) continue;
        V3 u;
        double outside = solveTrilinear(P, p, u);
        if (outside >= best - 1e-9) continue;
        best = outside;
        double w[8];
        trilinear(P, u, w);
        out.w.clear();
        for (int b = 0; b < 8; b++)
            if (std::fabs(w[b]) > 1e-6) out.w.push_back({cell[b], (float)w[b]});
    }
    return out;
}

// A point on a bolt-on panel: bilinear weights on the nearest of the panel's own quads
// (projected onto its sheet), so it hinges and flies off with the panel.
Weights bindPanel(const Car& car, const Panel& pnl, const V3& p) {
    Weights out;
    double best = 1e18;
    for (int qi : pnl.quads) {
        const SkinQuad& q = car.quads[qi];
        V3 P[4];
        for (int c = 0; c < 4; c++) P[c] = car.restLocal[q.v[c]];
        auto at = [&](double u, double v) {
            return P[0] * ((1 - u) * (1 - v)) + P[1] * (u * (1 - v)) + P[2] * (u * v) + P[3] * ((1 - u) * v);
        };
        double u = 0.5, v = 0.5;
        for (int it = 0; it < 12; it++) {
            V3 r = at(u, v) - p;
            V3 pu = (P[1] - P[0]) * (1 - v) + (P[2] - P[3]) * v, pv = (P[3] - P[0]) * (1 - u) + (P[2] - P[1]) * u;
            double a = dot(pu, pu), b = dot(pu, pv), c = dot(pv, pv), det = a * c - b * b;
            if (det < 1e-14) break;
            double gu = dot(pu, r), gv = dot(pv, r);
            u = std::min(1.3, std::max(-0.3, u - (c * gu - b * gv) / det));
            v = std::min(1.3, std::max(-0.3, v - (a * gv - b * gu) / det));
        }
        double outside = std::max({0.0, -u, u - 1, -v, v - 1});
        double score = len(at(u, v) - p) + outside * 2;
        if (score >= best) continue;
        best = score;
        const double w[4] = {(1 - u) * (1 - v), u * (1 - v), u * v, (1 - u) * v};
        out.w.clear();
        for (int c = 0; c < 4; c++) {
            auto it = std::find(pnl.bodyNodes.begin(), pnl.bodyNodes.end(), q.v[c]);
            int node = it == pnl.bodyNodes.end() ? q.v[c] : pnl.panelNodes[it - pnl.bodyNodes.begin()];
            out.w.push_back({node, (float)w[c]});
        }
    }
    return out;
}

double smin(double a, double b, double k) {  // smooth minimum (blend width k)
    double h = std::max(k - std::fabs(a - b), 0.0) / k;
    return std::min(a, b) - h * h * k * 0.25;
}
double smoothstep(double e0, double e1, double x) {
    double t = std::min(1.0, std::max(0.0, (x - e0) / (e1 - e0)));
    return t * t * (3 - 2 * t);
}

// ---- Body lines ------------------------------------------------------------------

enum Seg { SEG_BOTTOM, SEG_CORNER, SEG_SIDE, SEG_UPPER, SEG_EDGE, SEG_TOP };
const int kNB = 5, kNL = 4, kNS = 14, kNG = 12, kNE = 4, kNT = 10;
const int kRows = kNB + kNL + kNS + kNG + kNE + kNT;  // points in a half cross-section

struct Pt {
    double z, y;
    int seg;
};

// How one vehicle's body is drawn: the character the lattice alone can't give.
struct Style {
    int shell;                 // shader id: 1 4Runner, 2 Bronco, 3 Civic, 4 Tacoma
    double crF, crR;           // plan-view corner radii at the nose and the tail
    double noseRoll, noseLen;  // the hood's leading edge rolls down this much over this length
    double rocker;             // radius of the rocker's lower corner
    double shoulder, roofEdge; // largest radius of the hood shoulder and the roof edge
    double hoodCrown, roofCrown;
    double sideBow;            // side panels bulge out this much at mid height
    double tuckF, tuckR;       // bumper undersides rise this much at the ends
    double archPow;            // wheel arch shape: 2 round, higher = squarer
    double hoodInset;          // hood seam this far in from the body side
};

const Style* styleFor(const VehicleSpec& vs) {
    // Third-generation 4Runner (2001-02): slab sides, a long flat hood and a blunt grille
    // face, a tall greenhouse leaning in ~4 degrees, upright liftgate.
    static const Style runner{1, 0.16, 0.09, 0.04, 0.10, 0.045, 0.05, 0.055, 0.012, 0.016, 0.0, 0.05, 0.04, 2.0, 0.06};
    // 2021 Bronco 4-door: a box with crisp corners, a flat clamshell hood, an upright
    // windshield and swing gate, squared-off wheel openings under black flares.
    static const Style bronco{2, 0.07, 0.06, 0.015, 0.05, 0.03, 0.03, 0.035, 0.008, 0.010, 0.0, 0.03, 0.03, 3.5, 0.03};
    // 1990 EF Civic hatch: a low wedge nose, soft shoulders, a slightly bowed side and a
    // sloping glass hatch.
    static const Style civic{3, 0.24, 0.13, 0.09, 0.28, 0.06, 0.07, 0.07, 0.03, 0.025, 0.025, 0.07, 0.05, 2.0, 0.05};
    // 3rd-gen Tacoma double cab: a tall, rounded nose with a domed hood over a big grille,
    // a cab with a gently curved roof, then the bed: slab sides up to a rail at the cab's
    // belt line, square at the tailgate; wheel arches with a squarish flare.
    static const Style tacoma{4, 0.20, 0.05, 0.08, 0.12, 0.04, 0.06, 0.06, 0.02, 0.02, 0.012, 0.06, 0.03, 2.6, 0.07};
    const std::string id = vs.id;
    return id == "4runner"  ? &runner
           : id == "bronco" ? &bronco
           : id == "civic"  ? &civic
           : id == "tacoma" ? &tacoma
                            : nullptr;
}

// The body's lines, placed relative to the physics lattice (floor, belt, roof, hood
// line), so lifts, body lifts and bigger tires move the shell with the body.
struct ShellLines {
    const VehicleSpec& vs;
    const Style& st;
    double F, B, T, xf, xr, W2, xa, xRoofF, tuck, rakeR, archY, archR;
    // A pickup: the cab ends at xCab (its back glass there), and behind it the bed: walls up
    // to the rail at railY, their caps railW across, the floor at bedY.
    bool bed = false;
    double xCab = -1e9, bedY = 0, railY = 0, railW = 0.10;
    std::vector<double> axles, hoodX, hoodY;

    ShellLines(const VehicleSpec& v, const Style& style) : vs(v), st(style) {
        F = vs.layerY[0];
        B = vs.layerY[2];
        T = vs.layerY[3];
        xf = vs.length / 2;
        xr = -vs.length / 2;
        W2 = vs.width / 2;
        tuck = vs.roofTuck;
        rakeR = std::max(0.02, vs.rakeRear);
        int gi0 = 99, gi1 = -1;
        for (const std::string& row : vs.profile)
            for (int i = 0; i < (int)row.size(); i++) {
                if (row[i] == 'G') gi0 = std::min(gi0, i), gi1 = std::max(gi1, i);
                if (row[i] == 'B') bed = true;
            }
        xa = latticeX(vs, gi0);
        if (bed) {
            xCab = latticeX(vs, gi1 + 1);
            bedY = vs.layerY[1];
            railY = vs.layerY[2];
        }
        xRoofF = xa - vs.rakeFront;
        for (int c : axleColumns(vs)) axles.push_back(latticeX(vs, c));
        shellArch(vs, archY, archR);
        // Hood line through the physics top nodes in front of the windshield.
        for (int i = 0; i < gi0; i++) {
            hoodX.push_back(latticeX(vs, i));
            hoodY.push_back(i < (int)vs.topLine.size() && vs.topLine[i] > 0 ? vs.topLine[i] : B);
        }
        hoodX.push_back(xa);
        hoodY.push_back(B);
    }

    double halfWidth(double x) const {
        double w = W2;
        if (x > xf - st.crF) {
            double t = std::min(1.0, (x - (xf - st.crF)) / st.crF);
            w -= st.crF * (1 - std::sqrt(1 - t * t));
        }
        if (x < xr + st.crR) {
            double t = std::min(1.0, ((xr + st.crR) - x) / st.crR);
            w -= st.crR * (1 - std::sqrt(1 - t * t));
        }
        return w;
    }

    bool inBed(double x) const { return bed && x < xCab - 1e-6; }

    // Top of the body: hood, windshield, roof, then down the rear glass (a pickup's bed: the
    // rail along its walls).
    double top(double x) const {
        if (inBed(x)) return railY;
        double y;
        if (x >= xa) {
            y = hoodY.back();
            for (size_t k = 0; k + 1 < hoodX.size(); k++)
                if (x <= hoodX[k] && x >= hoodX[k + 1]) {
                    double t = (x - hoodX[k + 1]) / (hoodX[k] - hoodX[k + 1]);
                    y = hoodY[k + 1] + (hoodY[k] - hoodY[k + 1]) * t;
                }
            // The hood's leading edge rolls down into the grille.
            y -= st.noseRoll * std::pow(smoothstep(xf - st.noseLen, xf, x), 2.0);
        } else {
            double shield = B + (T - B) * (xa - x) / std::max(0.05, xa - xRoofF);
            y = smin(shield, T, 0.12);  // rounded windshield header
        }
        const double xBack = bed ? xCab : xr;
        double gate = T - (T - B) * ((xBack + rakeR) - x) / rakeR;  // rear glass
        if (x < xBack + rakeR + 0.08) y = smin(y, gate, 0.09);
        return y;
    }

    // Height of the wheel opening at distance d from an axle (or below the body if none).
    double arch(double d) const {
        double q = std::fabs(d) / archR;
        if (q >= 1) return -1e9;
        return archY + archR * std::pow(1 - std::pow(q, st.archPow), 1 / st.archPow);
    }

    // Bottom edge: the rocker, cut up into arches around the tires.
    double bottom(double x) const {
        double y = F;
        for (double ax : axles) y = std::max(y, arch(x - ax));
        y += st.tuckF * smoothstep(xf - 0.3, xf, x) + st.tuckR * smoothstep(xr + 0.3, xr, x);
        return y;
    }

    bool greenhouse(double x) const { return x < xa && !inBed(x) && top(x) > B + 0.015; }

    // Half cross-section from the bottom center out and up to the top center.
    std::vector<Pt> section(double x) const {
        if (inBed(x)) return bedSection(x);
        std::vector<Pt> pts;
        pts.reserve(kRows);
        const double yb = bottom(x), yt = top(x), w = halfWidth(x);
        const double rl = st.rocker;
        const bool gh = greenhouse(x);
        const double rs = std::min(st.shoulder, std::max(0.01, (yt - yb - rl) * 0.3));
        const double re = gh ? std::min(st.roofEdge, std::max(0.006, (yt - B) * 0.35)) : 0;
        const double y0 = yb + rl, y1 = gh ? B : yt - rs;
        // The side's bulge (zero at the rocker and the belt, most in between).
        auto bow = [&](double y) {
            double t = (y - F) / std::max(0.1, B - F);
            return st.sideBow * std::max(0.0, 1 - (2 * t - 1) * (2 * t - 1));
        };
        const double wb = w - st.sideBow;  // the bow comes out of the overall width
        for (int k = 0; k < kNB; k++) pts.push_back({(wb + bow(yb) - rl) * k / (kNB - 1), yb, SEG_BOTTOM});
        for (int k = 1; k <= kNL; k++) {
            double a = -M_PI / 2 + M_PI / 2 * k / kNL;
            pts.push_back({wb + bow(yb) - rl + rl * std::cos(a), yb + rl + rl * std::sin(a), SEG_CORNER});
        }
        for (int k = 1; k <= kNS; k++) {
            double y = y0 + (y1 - y0) * k / kNS;
            pts.push_back({wb + bow(y), y, SEG_SIDE});
        }
        double zEdge;
        if (gh) {
            // Greenhouse leaning in toward the roof (as the physics cage does).
            auto zAt = [&](double y) { return wb - tuck * (y - B) / std::max(0.1, T - B); };
            for (int k = 1; k <= kNG; k++) {
                double y = B + (yt - re - B) * k / kNG;
                pts.push_back({zAt(y), y, SEG_UPPER});
            }
            const double zc = zAt(yt - re) - re, yc = yt - re;
            for (int k = 1; k <= kNE; k++) {
                double a = M_PI / 2 * k / kNE;
                pts.push_back({zc + re * std::cos(a), yc + re * std::sin(a), SEG_EDGE});
            }
            zEdge = zc;
        } else {
            // No glass here: a rounded shoulder from the fender onto the hood.
            const double ws = wb + bow(y1);
            for (int k = 1; k <= kNG; k++) {
                double a = M_PI / 2 * k / kNG;
                pts.push_back({ws - rs + rs * std::cos(a), yt - rs + rs * std::sin(a), SEG_UPPER});
            }
            for (int k = 1; k <= kNE; k++) pts.push_back({ws - rs, yt, SEG_EDGE});
            zEdge = ws - rs;
        }
        // Top: a slight crown across the roof and hood.
        const double crown = gh ? st.roofCrown : st.hoodCrown;
        for (int k = 1; k <= kNT; k++) {
            double z = zEdge * (1.0 - (double)k / kNT);
            pts.push_back({z, yt + crown * (1 - (z / std::max(0.05, zEdge)) * (z / std::max(0.05, zEdge))), SEG_TOP});
        }
        return pts;
    }

    // A pickup bed, the same number of points: under the floor, up the outside, over the
    // rail cap, down the inside wall and across the bed floor to the middle.
    std::vector<Pt> bedSection(double x) const {
        std::vector<Pt> pts;
        pts.reserve(kRows);
        const double yb = bottom(x), w = halfWidth(x), rl = st.rocker, rr = railW / 2;
        const double ws = w - st.sideBow, y0 = yb + rl, y1 = railY - rr, zIn = ws - railW;
        for (int k = 0; k < kNB; k++) pts.push_back({(ws - rl) * k / (kNB - 1), yb, SEG_BOTTOM});
        for (int k = 1; k <= kNL; k++) {
            double a = -M_PI / 2 + M_PI / 2 * k / kNL;
            pts.push_back({ws - rl + rl * std::cos(a), yb + rl + rl * std::sin(a), SEG_CORNER});
        }
        for (int k = 1; k <= kNS; k++) pts.push_back({ws, y0 + (y1 - y0) * k / kNS, SEG_SIDE});
        for (int k = 1; k <= kNG; k++) {  // the rail cap, outside over to inside
            double a = M_PI * k / kNG;
            pts.push_back({ws - rr + rr * std::cos(a), y1 + rr * std::sin(a), SEG_UPPER});
        }
        for (int k = 1; k <= kNE; k++) pts.push_back({zIn - 0.01 * k / kNE, y1 + (bedY - y1) * k / kNE, SEG_EDGE});
        for (int k = 1; k <= kNT; k++) pts.push_back({(zIn - 0.01) * (1.0 - (double)k / kNT), bedY, SEG_TOP});
        return pts;
    }
};

}  // namespace

void shellArch(const VehicleSpec& vs, double& centerY, double& radius) {
    const std::string id = vs.id;
    if (id == "bronco") {
        // Squared openings, tall enough for 35s, starting just below the rocker.
        centerY = vs.layerY[0] - 0.02;
        radius = 0.56;
    } else if (id == "civic") {
        // Snug round arches over the 185/60R14s.
        centerY = vs.wheelRadius + 0.012;
        radius = vs.wheelRadius + 0.05;
    } else if (id == "tacoma") {
        // Tall openings with room over the 265/70R16s (a ~13 cm gap), raised with the body.
        centerY = vs.layerY[0] - 0.08;
        radius = 0.50;
    } else {
        // 4Runner: arches sized for the factory 265/70R16 (radius 0.389 m) with a ~8 cm
        // gap, raised with the body: lifts and body lifts open up the gap over bigger tires.
        centerY = vs.layerY[0] - 0.40 + 0.389;
        radius = 0.47;
    }
}

double shellArchHeight(const VehicleSpec& vs, double d) {
    const Style* st = styleFor(vs);
    double cy, r;
    shellArch(vs, cy, r);
    const double p = st ? st->archPow : 2.0, q = std::fabs(d) / r;
    return q >= 1 ? -1e9 : cy + r * std::pow(1 - std::pow(q, p), 1 / p);
}

bool hasBodyShell(const VehicleSpec& vs) { return styleFor(vs) != nullptr; }

bool buildBodyShell(const Car& car, VisualTemplate& t) {
    const VehicleSpec& vs = *car.spec;
    const Style* style = styleFor(vs);
    if (!style || car.panels.empty()) return false;
    const ShellLines L(vs, *style);

    // Stations along the length: even spacing, plus every line that must fall on a
    // station (panel seams, windshield base and header, arch edges, the ends).
    std::vector<double> xs;
    for (double x = L.xr; x <= L.xf + 1e-9; x += 0.03) xs.push_back(x);
    std::vector<double> forced = {L.xf, L.xr, L.xa, L.xRoofF, L.xr + L.rakeR, L.xf - 0.10};
    if (L.bed) forced.insert(forced.end(), {L.xCab, L.xCab - 0.012, L.xCab + L.rakeR});  // (the cab's back wall)
    std::map<std::string, std::pair<V3, V3>> box;  // panel bounding boxes
    for (const Panel& p : car.panels) {
        V3 lo(1e9, 1e9, 1e9), hi(-1e9, -1e9, -1e9);
        for (int n : p.bodyNodes) {
            const V3& q = car.restLocal[n];
            lo = V3(std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z));
            hi = V3(std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z));
        }
        box[p.name] = {lo, hi};
        if (std::string(p.name).find("DOOR") != std::string::npos) {
            forced.push_back(lo.x);
            forced.push_back(hi.x);
        }
    }
    for (double ax : L.axles) {
        forced.push_back(ax - L.archR);
        forced.push_back(ax + L.archR);
    }
    for (double f : forced) {
        if (f < L.xr - 1e-9 || f > L.xf + 1e-9) continue;
        auto it = std::min_element(xs.begin(), xs.end(), [&](double a, double b) { return std::fabs(a - f) < std::fabs(b - f); });
        *it = f;  // snap the nearest station onto it
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end(), [](double a, double b) { return std::fabs(a - b) < 0.008; }), xs.end());
    const int NX = (int)xs.size(), NS = kRows;

    // Grid vertices: +z half, then -z half; the bottom and top center lines are shared.
    std::vector<V3> pos;
    std::vector<int> seg;
    std::vector<std::vector<Pt>> sec(NX);
    for (int i = 0; i < NX; i++) sec[i] = L.section(xs[i]);
    auto vid = [&](int i, int j, int s) { return (s > 0 || j == 0 || j == NS - 1) ? i * NS + j : NX * NS + i * NS + j; };
    pos.resize(2 * NX * NS);
    seg.resize(2 * NX * NS);
    for (int i = 0; i < NX; i++)
        for (int j = 0; j < NS; j++)
            for (int s = -1; s <= 1; s += 2) {
                pos[vid(i, j, s)] = V3(xs[i], sec[i][j].y, s * sec[i][j].z);
                seg[vid(i, j, s)] = sec[i][j].seg;
            }

    struct RawFace {
        int v[4];
        int seg;
        bool cap;
        int capEnd;  // +1 front, -1 rear
    };
    std::vector<RawFace> raw;
    for (int s = -1; s <= 1; s += 2)
        for (int i = 0; i + 1 < NX; i++)
            for (int j = 0; j + 1 < NS; j++) {
                int a = vid(i, j, s), b = vid(i + 1, j, s), c = vid(i + 1, j + 1, s), d = vid(i, j + 1, s);
                RawFace f{{a, b, c, d}, sec[i][j + 1].seg, false, 0};
                if (s < 0) f = RawFace{{a, d, c, b}, sec[i][j + 1].seg, false, 0};
                raw.push_back(f);
            }
    // End caps: the grille face and the liftgate's lower face, fanned from a center.
    for (int end = -1; end <= 1; end += 2) {
        const int i = end > 0 ? NX - 1 : 0;
        std::vector<int> loop;
        for (int j = 0; j < NS; j++) loop.push_back(vid(i, j, 1));
        for (int j = NS - 2; j >= 1; j--) loop.push_back(vid(i, j, -1));
        V3 c;
        for (int v : loop) c += pos[v];
        c = c / (double)loop.size();
        c.x += end * 0.012;
        int center = (int)pos.size();
        pos.push_back(c);
        seg.push_back(SEG_SIDE);
        for (size_t k = 0; k < loop.size(); k++) {
            int p0 = loop[k], p1 = loop[(k + 1) % loop.size()];
            V3 nrm = cross(pos[p0] - c, pos[p1] - c);
            if (len(nrm) < 1e-12) continue;
            RawFace f{{center, p0, p1, p1}, SEG_SIDE, true, end};
            if (nrm.x * end < 0) f = RawFace{{center, p1, p0, p0}, SEG_SIDE, true, end};
            raw.push_back(f);
        }
    }

    // Which panel (or the body) each face rides on, its shader region, and its parent
    // skin quad (whose tearing hides it and whose glass shattering breaks it).
    auto panelIndex = [&](const char* name) {
        for (size_t p = 0; p < car.panels.size(); p++)
            if (std::string(car.panels[p].name) == name) return (int)p;
        return -1;
    };
    auto inX = [&](const char* name, double x) {
        auto it = box.find(name);
        return it != box.end() && x > it->second.first.x + 1e-6 && x < it->second.second.x - 1e-6;
    };
    std::vector<V3> quadCenter(car.quads.size());
    for (size_t q = 0; q < car.quads.size(); q++) {
        V3 c;
        for (int k = 0; k < 4; k++) c += car.restLocal[car.quads[q].v[k]] * 0.25;
        quadCenter[q] = c;
    }
    // The back opening: a liftgate (hinged at the top) or a swing gate (at the side).
    const char* gateName = panelIndex("LIFTGATE") >= 0 ? "LIFTGATE" : "TAILGATE";
    const int pLift = panelIndex(gateName), pHood = panelIndex("HOOD");
    const int pFrontBumper = panelIndex("FRONT BUMPER"), pRearBumper = panelIndex("REAR BUMPER");
    const double liftY = pLift >= 0 ? box[gateName].first.y : 1e9, liftZ = pLift >= 0 ? box[gateName].second.z : 0;
    const double frontBumperX = pFrontBumper >= 0 ? box["FRONT BUMPER"].first.x : 1e9;
    const double rearBumperX = pRearBumper >= 0 ? box["REAR BUMPER"].second.x : -1e9;
    const double bumperTop = pFrontBumper >= 0 ? box["FRONT BUMPER"].second.y : 0;

    struct FaceInfo {
        int group, region, parent;
        bool glass;
    };
    std::vector<FaceInfo> info(raw.size());
    for (size_t f = 0; f < raw.size(); f++) {
        const RawFace& rf = raw[f];
        V3 c = (pos[rf.v[0]] + pos[rf.v[1]] + pos[rf.v[2]] + pos[rf.v[3]]) * 0.25;
        int region, group = -1;
        if (rf.cap) {
            region = rf.capEnd > 0 ? REG_FRONT : REG_REAR;
            if (rf.capEnd < 0 && c.y > liftY && std::fabs(c.z) < liftZ) group = pLift;
        } else if (rf.seg == SEG_BOTTOM) {
            region = c.y > L.F + 0.03 ? REG_WELL : REG_UNDER;
        } else if (rf.seg == SEG_CORNER || rf.seg == SEG_SIDE) {
            region = REG_SIDE;
        } else if (rf.seg == SEG_UPPER) {
            region = L.greenhouse(c.x) ? REG_CABIN_SIDE : L.inBed(c.x) ? REG_TOP : REG_SIDE;
        } else if (L.inBed(c.x)) {
            region = rf.seg == SEG_EDGE ? REG_STEP : REG_BED;  // inside the bed: its walls and floor
        } else {
            const double xBack = L.bed ? L.xCab : L.xr;
            region = c.x > L.xa ? REG_TOP : c.x > L.xRoofF - 0.03 ? REG_WINDSHIELD : c.x < xBack + L.rakeR + 0.05 ? REG_REAR_WINDOW : REG_ROOF;
        }
        // The cab's back wall (between its last station and the bed's first): the back glass
        // and the panel round it.
        if (!rf.cap && L.bed && c.x < L.xCab && c.x > L.xCab - 0.013 && rf.seg != SEG_BOTTOM && rf.seg != SEG_CORNER && rf.seg != SEG_SIDE)
            region = REG_REAR_WINDOW;
        if (!rf.cap && (rf.seg == SEG_SIDE || rf.seg == SEG_UPPER) && std::fabs(c.z) > 0.3) {
            const bool left = c.z < 0;
            if (inX(left ? "LEFT DOOR" : "RIGHT DOOR", c.x)) group = panelIndex(left ? "LEFT DOOR" : "RIGHT DOOR");
            else if (inX(left ? "LEFT REAR DOOR" : "RIGHT REAR DOOR", c.x)) group = panelIndex(left ? "LEFT REAR DOOR" : "RIGHT REAR DOOR");
        }
        if (region == REG_TOP && pHood >= 0 && rf.seg == SEG_TOP && c.x < L.xf - 0.10 && std::fabs(c.z) < L.W2 - style->hoodInset) group = pHood;
        if (region == REG_REAR_WINDOW && pLift >= 0 && std::fabs(c.z) < liftZ) group = pLift;
        // Bumper covers: the ends of the body below the bumper line.
        if (c.y < bumperTop - 0.02 && rf.seg != SEG_TOP) {
            if (pFrontBumper >= 0 && c.x > frontBumperX + 0.02) group = pFrontBumper;
            if (pRearBumper >= 0 && c.x < rearBumperX - 0.02) group = pRearBumper;
        }

        int parent = -1;
        double best = 1e18;
        for (size_t q = 0; q < car.quads.size(); q++) {
            int qp = car.quadPanel.empty() ? -1 : car.quadPanel[q];
            if (qp != group) continue;
            double d = len(quadCenter[q] - c);
            if (d < best) best = d, parent = (int)q;
        }
        if (parent < 0) parent = 0;
        info[f] = {group, region, parent, region == REG_CABIN_SIDE || region == REG_WINDSHIELD || region == REG_REAR_WINDOW};
    }

    // Topological vertices: one per (grid vertex, panel), so seams split when a panel moves.
    std::map<std::pair<int, int>, int> topo;
    std::vector<std::array<int, 4>> faceTopo(raw.size());
    for (size_t f = 0; f < raw.size(); f++)
        for (int c = 0; c < 4; c++) {
            auto key = std::make_pair(raw[f].v[c], info[f].group);
            auto it = topo.find(key);
            if (it == topo.end()) {
                it = topo.emplace(key, t.topoCount++).first;
                t.wStart.push_back((int)t.wNode.size());
                const V3& p = pos[key.first];
                Weights w = key.second >= 0 ? bindPanel(car, car.panels[key.second], p) : bindBody(car, p);
                for (const auto& e : w.w) {
                    t.wNode.push_back(e.first);
                    t.wVal.push_back(e.second);
                }
            }
            faceTopo[f][c] = it->second;
        }
    t.wStart.push_back((int)t.wNode.size());

    // Render vertices carry the rest position (m) for the shader's decals.
    std::map<std::tuple<int, int, int>, int> rvIndex;
    std::vector<int> topoGrid(t.topoCount);
    for (const auto& e : topo) topoGrid[e.second] = e.first.first;
    for (size_t f = 0; f < raw.size(); f++) {
        VisualTemplate::Face face;
        face.parent = info[f].parent;
        face.glass = info[f].glass;
        for (int c = 0; c < 4; c++) {
            int tv = faceTopo[f][c];
            face.topo[c] = tv;
            auto key = std::make_tuple(tv, info[f].region, face.glass ? face.parent : -1);
            auto it = rvIndex.find(key);
            if (it == rvIndex.end()) {
                it = rvIndex.emplace(key, (int)t.rvTopo.size()).first;
                t.rvTopo.push_back(tv);
                const V3& p = pos[topoGrid[tv]];
                BodyStatic st;
                st.lat[0] = (float)p.x;
                st.lat[1] = (float)p.y;
                st.lat[2] = (float)p.z;
                st.region = (float)info[f].region;
                t.rvStatic.push_back(st);
            }
            face.rv[c] = it->second;
        }
        t.faces.push_back(face);
    }

    // Lines the shader needs (all in meters, rest pose).
    auto doorBox = [&](const char* n) { return box.count(n) ? box[n] : std::make_pair(V3(), V3()); };
    const auto fd = doorBox("RIGHT DOOR"), rd = box.count("RIGHT REAR DOOR") ? doorBox("RIGHT REAR DOOR") : doorBox("RIGHT DOOR");
    const double trimY = vs.trimTop > 0 ? L.F + vs.trimTop * (vs.layerY[1] - L.F) : 0;
    const float sp[16] = {(float)L.F,       (float)L.B,       (float)L.T,      (float)L.top(L.xf - 0.10),
                          (float)L.xf,      (float)L.W2,      (float)L.xa,     (float)L.xRoofF,
                          (float)fd.first.x, (float)rd.first.x, (float)liftY,  (float)liftZ,
                          (float)trimY,     (float)L.tuck,    (float)L.xr,     (float)(L.bed ? L.xCab : L.xr + L.rakeR)};  // (a pickup: its cab's back)
    std::copy(sp, sp + 16, t.shellParam);
    t.shellParam[16] = (float)fd.second.x;                                  // front door's rear edge (B pillar)
    t.shellParam[17] = (float)(pLift >= 0 ? box[gateName].second.y : L.T);  // gate top
    t.shellParam[18] = (float)bumperTop;
    t.shellParam[19] = (float)(L.archY + L.archR);                          // top of the wheel openings
    t.shell = style->shell;
    return true;
}
