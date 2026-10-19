#include "bodyshell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
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
// (projected onto its sheet), so it hinges and flies off with the panel. With keepOffset,
// how far the point stands off that sheet is kept too, as differences of the lattice nodes
// round the quad's corner (so a drawn shape that leans in further than the physics panel,
// a door's glass under a tucked roof, stays where it was drawn).
Weights bindPanel(const Car& car, const Panel& pnl, const V3& p, bool keepOffset) {
    Weights out;
    double best = 1e18;
    V3 resid;
    int bestCorner = -1;
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
        resid = p - at(u, v);
        bestCorner = q.v[0];
        const double w[4] = {(1 - u) * (1 - v), u * (1 - v), u * v, (1 - u) * v};
        out.w.clear();
        for (int c = 0; c < 4; c++) {
            auto it = std::find(pnl.bodyNodes.begin(), pnl.bodyNodes.end(), q.v[c]);
            int node = it == pnl.bodyNodes.end() ? q.v[c] : pnl.panelNodes[it - pnl.bodyNodes.begin()];
            out.w.push_back({node, (float)w[c]});
        }
    }
    if (keepOffset && bestCorner >= 0 && len(resid) > 1e-4) {
        for (const auto& cell : car.cells) {
            int b = -1;
            for (int k = 0; k < 8; k++)
                if (cell[k] == bestCorner) b = k;
            if (b < 0) continue;
            // resid = sum of a_k E_k over the cell's three edges from that corner.
            V3 E[3];
            for (int k = 0; k < 3; k++) E[k] = car.restLocal[cell[b ^ (1 << k)]] - car.restLocal[cell[b]];
            const double det = dot(E[0], cross(E[1], E[2]));
            if (std::fabs(det) < 1e-12) break;
            const double a[3] = {dot(resid, cross(E[1], E[2])) / det, dot(E[0], cross(resid, E[2])) / det, dot(E[0], cross(E[1], resid)) / det};
            for (int k = 0; k < 3; k++) {
                out.w.push_back({cell[b ^ (1 << k)], (float)a[k]});
                out.w.push_back({cell[b], (float)-a[k]});
            }
            break;
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
    double sillDrop = 0;       // the drawn rocker hangs this far below the lattice floor
    double extraTuck = 0;      // the drawn greenhouse leans in this much more than the cage does
    bool panelOffset = false;  // doors and the like keep the drawn shape off their physics sheet (else they lie on it)
    double hoodLift = 0;       // the drawn hood's nose stands this much over the lattice's, fading out at the cowl
    // The roof comes down to the belt at the cabin's back, and behind it the deck follows the
    // lattice's top line (a rear-engine car's engine lid); with openTail its middle is left open
    // over the engine and there's no rear panel at all (a Baja Bug's bare engine).
    bool tailLine = false, openTail = false;
    // A Beetle: the top is one dome from the steep little windshield over the roof and down the
    // long back to the tail (a spline, set out from the front axle and the floor), the greenhouse
    // curves in hard to a narrow round roof, and round fenders stand out of a narrow tub with a
    // running board between them (fenderOut: how far they stand out of it).
    bool dome = false;
    double fenderOut = 0;
};

const Style* styleFor(const VehicleSpec& vs) {
    // Third-generation 4Runner (2001-02): slab sides, a long flat hood and a blunt grille
    // face, a tall greenhouse leaning in ~4 degrees, upright liftgate.
    static const Style runner{1, 0.16, 0.09, 0.04, 0.10, 0.045, 0.05, 0.055, 0.012, 0.016, 0.0, 0.05, 0.04, 2.0, 0.06};
    // 2021 Bronco 4-door: a box, but every edge visibly radiused (the corners, the fender
    // tops into the hood, the hood's nose rolling down into the grille, the roof's edges), a
    // flat clamshell hood, an upright windshield and swing gate, a greenhouse leaning in,
    // squared-off wheel openings under black flares.
    static const Style bronco{2, 0.15, 0.11, 0.05, 0.20, 0.06, 0.08, 0.075, 0.012, 0.016, 0.008, 0.03, 0.03, 2.8, 0.03, 0.07, 0.09, true};
    // 1990 EF Civic hatch: a low wedge nose, soft shoulders, a slightly bowed side and a
    // sloping glass hatch.
    static const Style civic{3, 0.24, 0.13, 0.09, 0.28, 0.06, 0.07, 0.07, 0.03, 0.025, 0.025, 0.07, 0.05, 2.0, 0.05};
    // 3rd-gen Tacoma double cab (2020-23 TRD Pro): a tall, blunt, rounded nose (the hood
    // nearly level from the grille back to the cowl) over a big grille, softly radiused
    // fender tops and corners, a cab with a gently curved roof, then the bed: slab sides up
    // to a rail at the cab's belt line, square at the tailgate; squarish wheel openings.
    static const Style tacoma{4, 0.15, 0.07, 0.06, 0.16, 0.06, 0.07, 0.06, 0.025, 0.02, 0.012, 0.06, 0.03, 2.6, 0.07, 0.0, 0.0, true, 0.15};
    // 1972 K5 Blazer: a long box with a flat hood and a flat roof, softly rounded corners,
    // round wheel openings, an upright windshield and tailgate, the removable top over the
    // back half, a crease along the side at the belt.
    static const Style blazer{5, 0.06, 0.05, 0.02, 0.08, 0.03, 0.035, 0.045, 0.006, 0.012, 0.010, 0.0, 0.0, 2.2, 0.035};
    // 1969 Beetle as a Baja Bug: round everything (a rounded nose and tail in plan, a round
    // roof with a strong tumblehome falling in one curve to the back, fat shoulders over the
    // short fiberglass hood), the fenders bobbed into big round openings, the tail cut off
    // low over the bare engine.
    static const Style baja{6, 0.30, 0.22, 0.0, 0.30, 0.04, 0.10, 0.17, 0.05, 0.05, 0.0, 0.04, 0.06, 2.0, 0.24, 0.0, 0.22, true, 0.0, true, true, true, 0.16};
    const std::string id = vs.id;
    return id == "baja" ? &baja
           : id == "blazer" ? &blazer
           : id == "4runner"  ? &runner
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
    double xTail = -1e9;  // (tailLine: where the roof meets the belt at the back, and the deck behind it)
    std::vector<double> tailX, tailY;
    // (dome) the top line's points, and the fenders: middle x, half length, crown height.
    std::vector<double> domeX, domeY;
    struct Fender {
        double xc, hx, top;
    };
    std::vector<Fender> fenders;
    double boardX0 = 0, boardX1 = 0;  // the running board between the fenders

    ShellLines(const VehicleSpec& v, const Style& style) : vs(v), st(style) {
        F = vs.layerY[0];
        B = vs.layerY[2];
        T = vs.layerY[3];
        xf = vs.length / 2;
        xr = -vs.length / 2;
        W2 = vs.width / 2;
        tuck = vs.roofTuck + style.extraTuck;
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
        if (style.tailLine) {
            const int n = (int)vs.profile[0].size();
            xTail = latticeX(vs, gi1 + 1);
            for (int i = gi1 + 1; i <= n; i++) {
                tailX.push_back(latticeX(vs, i));
                tailY.push_back(i < (int)vs.topLine.size() && vs.topLine[i] > 0 ? vs.topLine[i] : B);
            }
        }
        if (style.dome && axles.size() >= 2) {
            // 1969 Beetle side profile (from the front axle back, from the floor up), lifted with the
            // body: the short fiberglass nose falling steeply, the frunk lid's curve up to the cowl, the
            // windshield, the roof's one arc and the long fall of the back to the cut tail.
            const double xF = axles.front(), xR = axles.back();
            static const double pts[][2] = {{0.62, 0.30}, {0.56, 0.40}, {0.44, 0.48}, {0.24, 0.55}, {0.00, 0.58}, {-0.25, 0.61},
                                            {-0.45, 0.64}, {-0.56, 0.80}, {-0.66, 0.97}, {-0.80, 1.11}, {-0.98, 1.18}, {-1.20, 1.205},
                                            {-1.45, 1.20}, {-1.72, 1.15}, {-2.00, 1.05}, {-2.27, 0.90}, {-2.52, 0.74}, {-2.75, 0.62},
                                            {-2.98, 0.53}, {-3.10, 0.50}, {-3.25, 0.48}};
            for (const auto& p : pts) {
                domeX.push_back(xF + p[0]);
                domeY.push_back(F + p[1]);
            }
            xa = xF - 0.45;       // the windshield's foot (the cowl)
            xRoofF = xF - 0.82;   // its header
            xTail = xR - 0.33;    // where the back falls below the belt: the deck lid, here cut away
            fenders = {{xF - 0.04, 0.47, F + 0.56}, {xR + 0.08, 0.50, F + 0.60}};  // (the Baja's: the front cut short, the back bobbed)
            boardX0 = xF - 0.52, boardX1 = xR + 0.45;
            tailX.clear(), tailY.clear();
        }
    }

    // (dome) Catmull-Rom through the profile's points.
    double domeTop(double x) const {
        const int n = (int)domeX.size();
        if (x >= domeX[0]) return domeY[0];
        if (x <= domeX[n - 1]) return domeY[n - 1];
        int k = 0;
        while (k + 1 < n && !(x <= domeX[k] && x >= domeX[k + 1])) k++;
        const double t = (domeX[k] - x) / (domeX[k] - domeX[k + 1]);
        const double p0 = domeY[std::max(0, k - 1)], p1 = domeY[k], p2 = domeY[k + 1], p3 = domeY[std::min(n - 1, k + 2)];
        return 0.5 * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
    }
    // (dome) the narrow tub's half width, and how far the fenders or the running board stand out of it at (x, y).
    double tubHalf(double x) const { return std::min(W2 - st.fenderOut, halfWidth(x)); }
    double fenderIn(double x) const {  // 0..1: how much fender there is at x
        double f = 0;
        for (const Fender& fd : fenders) {
            const double u = (x - fd.xc) / fd.hx;
            if (std::fabs(u) < 1) f = std::max(f, std::pow(1 - u * u, 0.35));
        }
        return f;
    }
    double sideOut(double x, double y) const {
        double out = 0;
        for (const Fender& fd : fenders) {
            const double u = (x - fd.xc) / fd.hx;
            if (std::fabs(u) >= 1) continue;
            const double ex = std::pow(1 - u * u, 0.35), rf = 0.17, y0 = fd.top - rf;
            const double ey = y <= y0 ? 1.0 : y >= fd.top ? 0.0 : std::sqrt(std::max(0.0, 1 - (y - y0) * (y - y0) / (rf * rf)));
            out = std::max(out, st.fenderOut * ex * ey);
        }
        if (x < boardX0 && x > boardX1 && y < F + 0.07) {  // the running board: a step out along the sill
            const double e = std::min({1.0, (boardX0 - x) / 0.06, (x - boardX1) / 0.06});
            out = std::max(out, 0.10 * e * smoothstep(F + 0.07, F + 0.05, y));
        }
        return out;
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
        if (st.dome && !domeX.empty()) return domeTop(x);
        if (inBed(x)) return railY;
        double y;
        if (x >= xa) {
            y = hoodY.back();
            for (size_t k = 0; k + 1 < hoodX.size(); k++)
                if (x <= hoodX[k] && x >= hoodX[k + 1]) {
                    double t = (x - hoodX[k + 1]) / (hoodX[k] - hoodX[k + 1]);
                    y = hoodY[k + 1] + (hoodY[k] - hoodY[k + 1]) * t;
                }
            y += st.hoodLift * std::min(1.0, std::max(0.0, (x - xa) / std::max(0.1, xf - xa)));
            // The hood's leading edge rolls down into the grille.
            y -= st.noseRoll * std::pow(smoothstep(xf - st.noseLen, xf, x), 2.0);
        } else {
            double shield = B + (T - B) * (xa - x) / std::max(0.05, xa - xRoofF);
            y = smin(shield, T, 0.12);  // rounded windshield header
        }
        if (st.tailLine && x < xTail) {  // the deck behind the cabin
            y = tailY.back();
            for (size_t k = 0; k + 1 < tailX.size(); k++)
                if (x <= tailX[k] && x >= tailX[k + 1]) {
                    double t = (x - tailX[k + 1]) / (tailX[k] - tailX[k + 1]);
                    y = tailY[k + 1] + (tailY[k] - tailY[k + 1]) * t;
                }
            // (rounding the deck into the back of the roof)
            const double back = B - (T - B) * (xTail - x) / rakeR;
            return std::max(y, std::min(back, B));
        }
        const double xBack = bed ? xCab : st.tailLine ? xTail : xr;
        // (A fastback's roof falls in a curve, not a straight rake: the Beetle's.)
        double t = std::max(0.0, ((xBack + rakeR) - x) / rakeR);
        double gate = st.tailLine ? T - (T - B) * t * t * (0.55 + 0.45 * t) : T - (T - B) * t;  // rear glass
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
        double y = F - st.sillDrop;
        for (double ax : axles) y = std::max(y, arch(x - ax));
        y += st.tuckF * smoothstep(xf - 0.3, xf, x) + st.tuckR * smoothstep(xr + 0.3, xr, x);
        return y;
    }

    bool greenhouse(double x) const { return x < xa && !inBed(x) && top(x) > B + 0.015; }

    // Half cross-section from the bottom center out and up to the top center.
    std::vector<Pt> section(double x) const {
        if (st.dome && !domeX.empty()) return domeSection(x);
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

    // (dome) A Beetle section: under the floor, round the sill, up the narrow tub with the fender
    // swelling out of it (or the running board at its foot), then the greenhouse curving in hard to
    // a round roof; ahead of the windshield and behind the cabin, over a rounded shoulder onto the
    // frunk lid or the deck, the fenders humped either side of it.
    std::vector<Pt> domeSection(double x) const {
        std::vector<Pt> pts;
        pts.reserve(kRows);
        const double yb = bottom(x), yt = top(x), tub = tubHalf(x), rl = st.rocker;
        const bool gh = greenhouse(x);
        auto sideZ = [&](double y) { return tub + sideOut(x, y); };
        const double rs = gh ? 0 : std::min(st.shoulder, std::max(0.01, (yt - yb - rl) * 0.3));
        const double y0 = yb + rl, y1 = gh ? B : yt - rs;
        const double zb = sideZ(yb);
        for (int k = 0; k < kNB; k++) pts.push_back({(zb - rl) * k / (kNB - 1), yb, SEG_BOTTOM});
        for (int k = 1; k <= kNL; k++) {
            const double a = -M_PI / 2 + M_PI / 2 * k / kNL;
            pts.push_back({zb - rl + rl * std::cos(a), yb + rl + rl * std::sin(a), SEG_CORNER});
        }
        for (int k = 1; k <= kNS; k++) {
            const double y = y0 + (y1 - y0) * k / kNS;
            pts.push_back({sideZ(y), y, SEG_SIDE});
        }
        double zEdge;
        if (gh) {
            // Tumblehome: the glass leans in harder the higher it goes, to a roof half as wide as the tub.
            const double re = std::min(st.roofEdge, std::max(0.006, (yt - B) * 0.35));
            auto zAt = [&](double y) { const double t = std::max(0.0, (y - B) / std::max(0.1, T - B)); return tub - tuck * std::pow(t, 1.35); };
            for (int k = 1; k <= kNG; k++) {
                const double y = B + (yt - re - B) * k / kNG;
                pts.push_back({zAt(y), y, SEG_UPPER});
            }
            const double zc = zAt(yt - re) - re, yc = yt - re;
            for (int k = 1; k <= kNE; k++) {
                const double a = M_PI / 2 * k / kNE;
                pts.push_back({zc + re * std::cos(a), yc + re * std::sin(a), SEG_EDGE});
            }
            zEdge = zc;
            for (int k = 1; k <= kNT; k++) {
                const double z = zEdge * (1.0 - (double)k / kNT), q = z / std::max(0.05, zEdge);
                pts.push_back({z, yt + st.roofCrown * (1 - q * q), SEG_TOP});
            }
            return pts;
        }
        const double ws = sideZ(y1);
        for (int k = 1; k <= kNG; k++) {
            const double a = M_PI / 2 * k / kNG;
            pts.push_back({ws - rs + rs * std::cos(a), yt - rs + rs * std::sin(a), SEG_UPPER});
        }
        for (int k = 1; k <= kNE; k++) pts.push_back({ws - rs, yt, SEG_EDGE});
        zEdge = ws - rs;
        // The lid between the fenders: a crown in the middle, a valley where it meets each fender, the
        // fender's own round top outboard of it.
        const double fi = fenderIn(x);
        for (int k = 1; k <= kNT; k++) {
            const double z = zEdge * (1.0 - (double)k / kNT), q = z / std::max(0.05, zEdge);
            const double valley = 0.035 * fi * std::exp(-((z - tub + 0.04) * (z - tub + 0.04)) / (0.06 * 0.06));
            pts.push_back({z, yt + st.hoodCrown * (1 - q * q) - valley, SEG_TOP});
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
        // Squared openings a hand over the 33s (a lift or bigger tires raise the body with
        // them, so the 35s and 37s keep about the same gap), from just below the rocker.
        centerY = vs.layerY[0] - 0.02;
        radius = 0.475;
    } else if (id == "civic") {
        // Snug round arches over the 185/60R14s.
        centerY = vs.wheelRadius + 0.012;
        radius = vs.wheelRadius + 0.05;
    } else if (id == "baja") {
        // The fenders bobbed: big round openings cut high over the 30s (room for 33s).
        centerY = vs.layerY[0] - 0.04;
        radius = 0.49;
    } else if (id == "blazer") {
        // Round openings cut well up into the fenders (room for ~33s), from just below the rocker.
        centerY = vs.layerY[0] - 0.08;
        radius = 0.47;
    } else if (id == "tacoma") {
        // Tall openings a hand over the 265/70R16s (~10 cm), raised with the body.
        centerY = vs.layerY[0] - 0.10;
        radius = 0.48;
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

double shellTopY(const VehicleSpec& vs, double x) {
    const Style* style = styleFor(vs);
    if (!style) return vs.layerY.back();
    const ShellLines L(vs, *style);
    return L.top(x) + (L.greenhouse(x) ? style->roofCrown : style->hoodCrown);
}

double shellRoofHalfWidth(const VehicleSpec& vs) {
    const Style* style = styleFor(vs);
    if (!style) return vs.width / 2;
    const ShellLines L(vs, *style);
    const double re = std::min(style->roofEdge, std::max(0.006, (L.T - L.B) * 0.35));
    return L.W2 - style->sideBow - L.tuck * (L.T - re - L.B) / std::max(0.1, L.T - L.B) - re;  // (as section() draws it mid-roof)
}

bool bedMouth(const VehicleSpec& vs, BedMouth& m) {
    const Style* style = styleFor(vs);
    if (!style) return false;
    const ShellLines L(vs, *style);
    if (!L.bed) return false;
    m.x = L.xr, m.floorY = L.bedY, m.railY = L.railY;
    m.inner = L.halfWidth(L.xr + 0.02) - style->sideBow - L.railW - 0.01;  // (the inside face of the wall, as bedSection draws it)
    return true;
}

bool buildBodyShell(const Car& car, VisualTemplate& t, bool merge) {
    const VehicleSpec& vs = *car.spec;
    const Style* style = styleFor(vs);
    if (!style || car.panels.empty()) return false;
    const ShellLines L(vs, *style);

    // Stations along the length: even spacing, plus every line that must fall on a
    // station (panel seams, windshield base and header, arch edges, the ends).
    std::vector<double> xs;
    for (double x = L.xr; x <= L.xf + 1e-9; x += style->dome ? 0.04 : 0.03) xs.push_back(x);  // (a dome is curved everywhere: little merges, so fewer stations)
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
        if (end < 0 && style->openTail) continue;  // (a Baja's cut tail: open, the engine out in the air)
        const int i = end > 0 ? NX - 1 : 0;
        std::vector<int> loop;
        for (int j = 0; j < NS; j++) loop.push_back(vid(i, j, 1));
        for (int j = NS - 2; j >= 1; j--) loop.push_back(vid(i, j, -1));
        if (end < 0 && L.bed) {
            // A pickup's tail is a U (floor and walls round the open bed): fanned from a centre it
            // would lay triangles across the bed's mouth, so ear-clip it and leave the bed open.
            std::vector<int> rest = loop;
            auto area2 = [&](int a, int b, int c) {  // twice the signed area in the (z, y) plane
                return (pos[b].z - pos[a].z) * (pos[c].y - pos[a].y) - (pos[c].z - pos[a].z) * (pos[b].y - pos[a].y);
            };
            double wind = 0;
            for (size_t k = 0; k < rest.size(); k++) wind += area2(rest[0], rest[k], rest[(k + 1) % rest.size()]);
            const double sg = wind < 0 ? -1 : 1;
            auto inside = [&](int a, int b, int c, int p) {
                return sg * area2(a, b, p) > 1e-9 && sg * area2(b, c, p) > 1e-9 && sg * area2(c, a, p) > 1e-9;
            };
            auto tri = [&](int a, int b, int c) {
                const V3 nrm = cross(pos[b] - pos[a], pos[c] - pos[a]);
                if (len(nrm) < 1e-12) return;
                raw.push_back(nrm.x * end < 0 ? RawFace{{a, c, b, b}, SEG_SIDE, true, end} : RawFace{{a, b, c, c}, SEG_SIDE, true, end});
            };
            int guard = 4 * (int)rest.size() * (int)rest.size();
            for (size_t i = 0; rest.size() > 3 && guard-- > 0; i++) {
                const size_t n = rest.size(), k = i % n;
                const int a = rest[(k + n - 1) % n], b = rest[k], c = rest[(k + 1) % n];
                const double ar = sg * area2(a, b, c);
                bool ear = ar > 1e-9;
                for (size_t m = 0; ear && m < n; m++) {
                    const int p = rest[m];
                    if (p != a && p != b && p != c && inside(a, b, c, p)) ear = false;
                }
                if (ar <= 1e-9 && std::fabs(ar) <= 1e-9) {  // a collinear point: drop it
                    rest.erase(rest.begin() + k);
                    continue;
                }
                if (!ear) continue;
                tri(a, b, c);
                rest.erase(rest.begin() + k);
            }
            if (rest.size() == 3) tri(rest[0], rest[1], rest[2]);
            continue;
        }
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
    const int pLift = panelIndex(gateName), pHood = panelIndex("HOOD"), pEngineLid = panelIndex("ENGINE COVER");
    const int pFrontBumper = panelIndex("FRONT BUMPER"), pRearBumper = panelIndex("REAR BUMPER");
    const double liftY = pLift >= 0 ? box[gateName].first.y : 1e9, liftZ = pLift >= 0 ? box[gateName].second.z : 0;
    const double frontBumperX = pFrontBumper >= 0 ? box["FRONT BUMPER"].first.x : 1e9;
    const double rearBumperX = pRearBumper >= 0 ? box["REAR BUMPER"].second.x : -1e9;
    const double bumperTop = pFrontBumper >= 0 ? box["FRONT BUMPER"].second.y : 0;

    struct FaceInfo {
        int group, region, parent;
        bool glass;
    };
    const int kHole = 99;  // (a face left out: an opening in the shell)
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
            const double xBack = L.bed ? L.xCab : style->tailLine ? L.xTail : L.xr;
            region = c.x > L.xa ? REG_TOP : c.x > L.xRoofF - 0.03 ? REG_WINDSHIELD : c.x < xBack + L.rakeR + 0.05 ? REG_REAR_WINDOW : REG_ROOF;
            if (style->tailLine && c.x < L.xTail + 0.01) region = REG_TOP;  // the deck behind the cabin
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
        if (region == REG_TOP && pHood >= 0 && rf.seg == SEG_TOP && c.x > L.xa && c.x < L.xf - 0.10 && std::fabs(c.z) < L.W2 - style->hoodInset) group = pHood;
        if (region == REG_TOP && pEngineLid >= 0 && style->tailLine && rf.seg == SEG_TOP && c.x < L.xTail - 0.03 && std::fabs(c.z) < L.W2 - style->hoodInset) group = pEngineLid;
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
        // A Baja's cut tail: the deck stops short of the engine, which stands up out of it in the air.
        if (style->openTail && !rf.cap && (rf.seg == SEG_TOP || rf.seg == SEG_EDGE) && c.x < L.xTail + (style->dome ? 0.04 : -0.30) &&
            std::fabs(c.z) < std::min(0.46, L.W2 - style->hoodInset - 0.06))
            info[f].region = kHole;
    }

    // Weights per (grid vertex, panel): one binding shared by the merge test and the topological vertices.
    std::map<std::pair<int, int>, Weights> bound;
    auto weightsOf = [&](int v, int group) -> const Weights& {
        auto it = bound.find({v, group});
        if (it == bound.end())
            it = bound.emplace(std::make_pair(v, group), group >= 0 ? bindPanel(car, car.panels[group], pos[v], style->panelOffset) : bindBody(car, pos[v])).first;
        return it->second;
    };

    // Flat merge: runs of grid quads that are one plane, one region, one panel and one skin-quad parent, and whose
    // inner vertices sit on the bilinear blend of the run's corner weights (they move with the same nodes, in the
    // same proportion), are cut again into the fewest triangles from their outline. Every vertex another face
    // uses stays, so no cracks open, and a run only goes where its vertices already shade as the plane. A run that
    // crosses a lattice cell or a panel edge fails the weight test and stays.
    const size_t gridFaces = 2 * (size_t)(NX - 1) * (NS - 1);
    if (merge) {
        auto cellId = [&](int s, int i, int j) { return ((size_t)(s < 0 ? 0 : 1) * (NX - 1) + i) * (NS - 1) + j; };
        auto cornerOf = [&](int s, int i, int j, int c) {  // c: 0 (i,j) 1 (i+1,j) 2 (i+1,j+1) 3 (i,j+1)
            return vid(i + (c == 1 || c == 2), j + (c >= 2), s);
        };
        std::vector<char> taken(gridFaces, 0);
        auto same = [&](size_t a, size_t b) {
            return info[a].group == info[b].group && info[a].region == info[b].region && info[a].parent == info[b].parent &&
                   info[a].glass == info[b].glass;
        };
        // The smooth normal each (vertex, panel) has at rest, as evalBody will sum it: a run may only go where its
        // vertices already shade as the flat plane (next to a curve the outline's blended normals must stay on quads).
        std::map<std::pair<int, int>, V3> restN;
        for (size_t f = 0; f < raw.size(); f++) {
            const V3 n = cross(pos[raw[f].v[2]] - pos[raw[f].v[0]], pos[raw[f].v[3]] - pos[raw[f].v[1]]);
            for (int c = 0; c < 4; c++) restN[{raw[f].v[c], info[f].group}] += n;
        }
        auto bilinearOk = [&](int s, int i0, int j0, int i1, int j1, int group) {
            const V3 P00 = pos[vid(i0, j0, s)], P10 = pos[vid(i1, j0, s)], P11 = pos[vid(i1, j1, s)], P01 = pos[vid(i0, j1, s)];
            const V3 nrm = cross(P10 - P00, P01 - P00);
            if (len(nrm) < 1e-9) return false;
            const V3 nn = norm(nrm);
            if (std::fabs(dot(P11 - P00, nn)) > 5e-4) return false;
            const Weights &W00 = weightsOf(vid(i0, j0, s), group), &W10 = weightsOf(vid(i1, j0, s), group),
                          &W11 = weightsOf(vid(i1, j1, s), group), &W01 = weightsOf(vid(i0, j1, s), group);
            for (int i = i0; i <= i1; i++)
                for (int j = j0; j <= j1; j++) {
                    const V3 vn = restN[{vid(i, j, s), group}];
                    if (std::fabs(dot(vn, nn)) < 0.9998 * len(vn)) return false;
                    const bool corner = (i == i0 || i == i1) && (j == j0 || j == j1);
                    if (corner) continue;
                    const V3 p = pos[vid(i, j, s)];
                    if (std::fabs(dot(p - P00, nn)) > 5e-4) return false;
                    double u = 0.5, v = 0.5;  // inverse bilinear (Gauss-Newton)
                    for (int it = 0; it < 12; it++) {
                        const V3 q = P00 * ((1 - u) * (1 - v)) + P10 * (u * (1 - v)) + P11 * (u * v) + P01 * ((1 - u) * v);
                        const V3 r = q - p;
                        const V3 du = (P10 - P00) * (1 - v) + (P11 - P01) * v, dv = (P01 - P00) * (1 - u) + (P11 - P10) * u;
                        const double a = dot(du, du), b = dot(du, dv), c = dot(dv, dv), e = dot(du, r), f = dot(dv, r);
                        const double det = a * c - b * b;
                        if (std::fabs(det) < 1e-18) return false;
                        const double ddu = (c * e - b * f) / det, ddv = (a * f - b * e) / det;
                        u -= ddu, v -= ddv;
                        if (std::fabs(ddu) + std::fabs(ddv) < 1e-10) break;
                    }
                    const V3 q = P00 * ((1 - u) * (1 - v)) + P10 * (u * (1 - v)) + P11 * (u * v) + P01 * ((1 - u) * v);
                    if (len(q - p) > 5e-4 || u < -1e-6 || u > 1 + 1e-6 || v < -1e-6 || v > 1 + 1e-6) return false;
                    std::map<int, double> want;
                    for (const auto& e2 : W00.w) want[e2.first] += (1 - u) * (1 - v) * e2.second;
                    for (const auto& e2 : W10.w) want[e2.first] += u * (1 - v) * e2.second;
                    for (const auto& e2 : W11.w) want[e2.first] += u * v * e2.second;
                    for (const auto& e2 : W01.w) want[e2.first] += (1 - u) * v * e2.second;
                    for (const auto& e2 : weightsOf(vid(i, j, s), group).w) want[e2.first] -= e2.second;
                    for (const auto& e2 : want)
                        if (std::fabs(e2.second) > 0.01) return false;
                }
            return true;
        };
        auto usable = [&](int s, int i0, int j0, int i1, int j1) {  // cells i0..i1-1 x j0..j1-1
            const size_t ref = cellId(s, i0, j0);
            for (int i = i0; i < i1; i++)
                for (int j = j0; j < j1; j++) {
                    const size_t c = cellId(s, i, j);
                    if (taken[c] || !same(ref, c)) return false;
                }
            return bilinearOk(s, i0, j0, i1, j1, info[ref].group);
        };
        struct Block {
            int s, i0, j0, i1, j1;
        };
        std::vector<Block> blocks;
        for (int s = -1; s <= 1; s += 2)
            for (int i = 0; i + 1 < NX; i++)
                for (int j = 0; j + 1 < NS; j++) {
                    if (taken[cellId(s, i, j)]) continue;
                    Block best{s, i, j, i + 1, j + 1};
                    for (int order = 0; order < 2; order++) {
                        Block b{s, i, j, i + 1, j + 1};
                        for (int pass = 0; pass < 2; pass++) {
                            const bool alongI = (pass == 0) == (order == 0);
                            for (;;) {
                                Block n = b;
                                if (alongI) n.i1++; else n.j1++;
                                if (n.i1 > NX - 1 || n.j1 > NS - 1 || !usable(s, n.i0, n.j0, n.i1, n.j1)) break;
                                b = n;
                            }
                        }
                        if ((b.i1 - b.i0) * (b.j1 - b.j0) > (best.i1 - best.i0) * (best.j1 - best.j0)) best = b;
                    }
                    for (int a = best.i0; a < best.i1; a++)
                        for (int c = best.j0; c < best.j1; c++) taken[cellId(s, a, c)] = 1;
                    blocks.push_back(best);
                }
        // Vertices that must stay: every corner of every run (a lone quad's four included) and the end caps' loops.
        std::set<int> keep;
        for (const Block& b : blocks)
            for (int c = 0; c < 4; c++) keep.insert(vid(c == 1 || c == 2 ? b.i1 : b.i0, c >= 2 ? b.j1 : b.j0, b.s));
        for (size_t f = gridFaces; f < raw.size(); f++)
            for (int c = 0; c < 4; c++) keep.insert(raw[f].v[c]);

        std::vector<RawFace> out;
        std::vector<FaceInfo> outInfo;
        for (const Block& b : blocks) {
            const size_t ref = cellId(b.s, b.i0, b.j0);
            if ((b.i1 - b.i0) * (b.j1 - b.j0) < 2) {
                out.push_back(raw[ref]);
                outInfo.push_back(info[ref]);
                continue;
            }
            // Outline, in the winding of the grid quads (a, b, c, d).
            std::vector<int> loop;
            for (int i = b.i0; i < b.i1; i++) loop.push_back(vid(i, b.j0, b.s));
            for (int j = b.j0; j < b.j1; j++) loop.push_back(vid(b.i1, j, b.s));
            for (int i = b.i1; i > b.i0; i--) loop.push_back(vid(i, b.j1, b.s));
            for (int j = b.j1; j > b.j0; j--) loop.push_back(vid(b.i0, j, b.s));
            std::vector<int> poly;
            for (int v : loop)
                if (keep.count(v)) poly.push_back(v);
            if (b.s < 0) std::reverse(poly.begin(), poly.end());
            // Ear-clip the (convex) outline, best-shaped ear first.
            std::vector<std::array<int, 3>> tris;
            std::vector<int> rest = poly;
            auto quality = [&](int a, int bb, int c) {  // smallest angle's sine-ish: 2 x area over the longest edge squared
                const V3 ab = pos[bb] - pos[a], bc = pos[c] - pos[bb], ca = pos[a] - pos[c];
                const double l2 = std::max({dot(ab, ab), dot(bc, bc), dot(ca, ca)});
                return len(cross(ab, ca)) / std::max(l2, 1e-12);
            };
            while (rest.size() > 3) {
                const size_t n = rest.size();
                size_t bestK = n;
                double bestQ = 1e-3;
                for (size_t k = 0; k < n; k++) {
                    const double q = quality(rest[(k + n - 1) % n], rest[k], rest[(k + 1) % n]);
                    if (q > bestQ) bestQ = q, bestK = k;
                }
                if (bestK == n) break;
                tris.push_back({rest[(bestK + n - 1) % n], rest[bestK], rest[(bestK + 1) % n]});
                rest.erase(rest.begin() + bestK);
            }
            if (rest.size() == 3 && quality(rest[0], rest[1], rest[2]) > 1e-3) tris.push_back({rest[0], rest[1], rest[2]});
            else {  // (no ear left: a collapsed outline) keep the block as it was
                for (int i = b.i0; i < b.i1; i++)
                    for (int j = b.j0; j < b.j1; j++) {
                        const size_t cell = cellId(b.s, i, j);
                        out.push_back(raw[cell]);
                        outInfo.push_back(info[cell]);
                    }
                continue;
            }
            for (const auto& t3 : tris) {
                const RawFace f{{t3[0], t3[1], t3[2], t3[2]}, raw[ref].seg, false, 0};
                out.push_back(f);
                outInfo.push_back(info[ref]);
            }
        }
        for (size_t f = gridFaces; f < raw.size(); f++) out.push_back(raw[f]), outInfo.push_back(info[f]);
        raw.swap(out);
        info.swap(outInfo);
    }

    // Topological vertices: one per (grid vertex, panel), so seams split when a panel moves.
    std::map<std::pair<int, int>, int> topo;
    std::vector<std::array<int, 4>> faceTopo(raw.size());
    for (size_t f = 0; f < raw.size(); f++)
        for (int c = 0; c < 4 && info[f].region != kHole; c++) {
            auto key = std::make_pair(raw[f].v[c], info[f].group);
            auto it = topo.find(key);
            if (it == topo.end()) {
                it = topo.emplace(key, t.topoCount++).first;
                t.wStart.push_back((int)t.wNode.size());
                const V3& p = pos[key.first];
                const Weights& w = weightsOf(key.first, key.second);
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
        if (info[f].region == kHole) continue;
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
                          (float)trimY,     (float)L.tuck,    (float)L.xr,     (float)(L.bed ? L.xCab : style->tailLine ? L.xTail : L.xr + L.rakeR)};  // (a pickup: its cab's back; a fastback: where its roof meets the deck)
    std::copy(sp, sp + 16, t.shellParam);
    if (L.bed && pLift < 0) {  // (no gate panel: the lamps sit on the walls' ends, outboard of the drawn gate, and the chrome step bumper ends a hand below the floor)
        BedMouth bm;
        if (bedMouth(vs, bm)) t.shellParam[10] = (float)(bm.floorY - 0.15), t.shellParam[11] = (float)(bm.inner + 0.01);
    }
    t.shellParam[16] = (float)fd.second.x;                                  // front door's rear edge (B pillar)
    t.shellParam[17] = (float)(pLift >= 0 ? box[gateName].second.y : L.T);  // gate top
    t.shellParam[18] = (float)bumperTop;
    t.shellParam[19] = (float)(L.archY + L.archR);                          // top of the wheel openings
    t.shell = style->shell;
    return true;
}
