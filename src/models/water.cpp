// Water: the waterfall (docs/plans/big-maps.md 3.9). Built in its own frame like every model, but it isn't solid and isn't lit
// like one: the lip is at (0, height, 0), the water goes over toward +X onto a pool whose surface is y = 0, and its parts are in
// the water materials (modelkit.h WATERFALL..RIPPLE), drawn see-through and moving by the water pass (watermesh.cpp, the water
// shader). Their vertex colours carry the shader's coordinates, not a colour: (across, down the fall, opacity or phase), in metres.
#include <algorithm>
#include <cmath>

#include "../modelkit.h"

using namespace mk;

namespace {

constexpr double kG = 9.81;

// Sets the colour (the shader's coordinates) of the last three vertices.
void stamp(B& b, const V3& c0, const V3& c1, const V3& c2) {
    auto& v = b.out().v;
    const V3 c[3] = {c0, c1, c2};
    for (int k = 0; k < 3; k++) {
        MVert& m = v[v.size() - 3 + k];
        m.c[0] = (float)c[k].x, m.c[1] = (float)c[k].y, m.c[2] = (float)c[k].z;
    }
}

// A ribbon of falling water: pos(u, v) for u across 0..1 and v down 0..1, with its shader coordinates coord(u, v).
template <class Pos, class Coord> void ribbon(B& b, int nu, int nv, Pos pos, Coord coord) {
    b.mat(WATERFALL);
    auto P = [&](int i, int j) { return pos((double)i / nu, (double)j / nv); };
    auto N = [&](int i, int j) {
        const V3 du = P(std::min(i + 1, nu), j) - P(std::max(i - 1, 0), j), dv = P(i, std::min(j + 1, nv)) - P(i, std::max(j - 1, 0));
        V3 n = norm(cross(dv, du));
        if (n.x < 0) n = n * -1.0;  // (facing out, the way it falls)
        return n;
    };
    for (int j = 0; j < nv; j++)
        for (int i = 0; i < nu; i++) {
            const int q[4][2] = {{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}};
            const int tri[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (const auto& tr : tri) {
                const int a = tr[0], c = tr[1], d = tr[2];
                b.triN(P(q[a][0], q[a][1]), P(q[c][0], q[c][1]), P(q[d][0], q[d][1]), N(q[a][0], q[a][1]), N(q[c][0], q[c][1]), N(q[d][0], q[d][1]));
                stamp(b, coord((double)q[a][0] / nu, (double)q[a][1] / nv), coord((double)q[c][0] / nu, (double)q[c][1] / nv),
                      coord((double)q[d][0] / nu, (double)q[d][1] / nv));
            }
        }
}

// A flat ring disc at height y round (cx, cz): radii rx (along the fall) and rz (across), `rings` bands; coords (distance from the
// middle in m, 0, opacity falling outward) in `mat`.
void disc(B& b, Mat mat, double cx, double cz, double y, double rx, double rz, int rings, int segs, double alpha) {
    b.mat(mat);
    auto P = [&](int r, int s) {
        const double f = (double)r / rings, a = 2 * M_PI * s / segs;
        return V3(cx + rx * f * std::cos(a), y, cz + rz * f * std::sin(a));
    };
    auto C = [&](int r, int s) {
        const V3 p = P(r, s);
        return V3(std::hypot(p.x - cx, p.z - cz), 0, alpha * (1 - (double)r / rings));
    };
    const V3 up(0, 1, 0);
    for (int r = 0; r < rings; r++)
        for (int s = 0; s < segs; s++) {
            if (r == 0) {
                b.triN(P(0, 0), P(1, s + 1), P(1, s), up, up, up);
                stamp(b, C(0, 0), C(1, s + 1), C(1, s));
                continue;
            }
            b.triN(P(r, s), P(r, s + 1), P(r + 1, s + 1), up, up, up);
            stamp(b, C(r, s), C(r, s + 1), C(r + 1, s + 1));
            b.triN(P(r, s), P(r + 1, s + 1), P(r + 1, s), up, up, up);
            stamp(b, C(r, s), C(r + 1, s + 1), C(r + 1, s));
        }
}

// A soft puff: two upright quads crossed at its middle (the shader fades the one seen edge-on), coords (u, v, phase).
void puff(B& b, Mat mat, const V3& c, double size, double phase) {
    b.mat(mat);
    const double h = size / 2;
    for (int k = 0; k < 2; k++) {
        const V3 side = k ? V3(0, 0, 1) : V3(1, 0, 0), n = k ? V3(1, 0, 0) : V3(0, 0, 1);
        const V3 p0 = c - side * h - V3(0, h, 0), p1 = c + side * h - V3(0, h, 0), p2 = c + side * h + V3(0, h, 0), p3 = c - side * h + V3(0, h, 0);
        b.triN(p0, p1, p2, n, n, n);
        stamp(b, V3(0, 0, phase), V3(1, 0, phase), V3(1, 1, phase));
        b.triN(p0, p2, p3, n, n, n);
        stamp(b, V3(0, 0, phase), V3(1, 1, phase), V3(0, 1, phase));
    }
}

// Everything at the foot of the falls (every variant): foam where it lands, ripples spreading over the pool, spray thrown up
// round it and mist drifting off. `land`: how far out it lands; w: how wide it is there.
void pool(B& b, double H, double w, double land, uint32_t seed) {
    const double rx = 1.5 + 0.12 * H, rz = w / 2 + 1.0;
    disc(b, FOAM, land, 0, 0.05, rx, rz, 4, 20, 1.0);
    disc(b, RIPPLE, land, 0, 0.03, 1.9 * std::max(rx, rz * 0.8), 1.6 * rz, 3, 24, 0.8);
    const int sprays = std::clamp(6 + (int)(H / 3), 6, 12);
    for (int k = 0; k < sprays; k++) {
        const double a = 2 * M_PI * hash01(seed, k * 3 + 1), r = 0.7 * hash01(seed, k * 3 + 2);
        const double size = (1.2 + 0.12 * H + 0.08 * w) * (0.7 + 0.6 * hash01(seed, k * 3 + 3));
        const V3 c(land + rx * r * std::cos(a), size * 0.5 + 0.05 + 0.05 * H * hash01(seed, k + 40), rz * 0.9 * r * std::sin(a) * 1.3);  // (all of it over the pool)
        puff(b, SPRAY, c, size, hash01(seed, k + 60));
    }
    for (int k = 0; k < 3; k++) {
        const double size = (0.7 * H + 0.5 * w + 3) * (0.8 + 0.4 * hash01(seed, k + 80));
        const V3 c(land + size * (0.15 + 0.2 * k), size * 0.5, (k - 1) * rz * 0.6);
        puff(b, MIST, c, size, hash01(seed, k + 90));
    }
}

// The sheet as it leaves the lip at speed and falls: out `land` m by the bottom, dropping `H`. Throws further in the middle.
V3 fallAt(double H, double land, double z, double s, double middle) {
    return V3(land * std::sqrt(s) * (0.88 + 0.12 * middle), H + 0.05 - s * (H + 0.35), z);  // (from just over the lip to under the pool)
}

// The water running up to the lip: a glassy apron over its last 2 m (the river's own surface stops short of the drop).
void apron(B& b, double H, double W) {
    ribbon(b, 4, 2, [&](double u, double v) { return V3(-2.0 + 2.0 * v, H + 0.04 - 0.02 * v, (u - 0.5) * W * 1.04); },
           [&](double u, double v) { return V3((u - 0.5) * W, -2.0 + 2.0 * v, 0.55 * (1 - std::pow(std::fabs(2 * u - 1), 6))); });
}

// SHEET: one smooth curtain, front and back skins.
void sheetFalls(B& b, const P& p) {
    const double H = p("height"), W = p("width"), T = p("lip");
    const uint32_t seed = (uint32_t)p.i("seed");
    apron(b, H, W);
    for (int layer = 0; layer < 2; layer++) {
        const double back = layer ? 0.3 : 0.0, op = layer ? 0.55 : 1.0;
        ribbon(b, 8, 14,
               [&](double u, double v) {
                   const double s = v * v * 0.35 + v * 0.65, mid = 1 - (2 * u - 1) * (2 * u - 1);
                   V3 q = fallAt(H, T, (u - 0.5) * W * (1 + 0.12 * s), s, mid);
                   q.x -= back * (0.3 + s);
                   return q;
               },
               [&](double u, double v) {
                   const double s = v * v * 0.35 + v * 0.65;
                   return V3((u - 0.5) * W, s * H, op * (1 - 0.85 * std::pow(std::fabs(2 * u - 1), 4)));
               });
    }
    pool(b, H, W * 1.12, T, seed);
}

// BROKEN: the curtain split over rock into 2-3 strands, each with its own width and throw.
void brokenFalls(B& b, const P& p) {
    const double H = p("height"), W = p("width"), T = p("lip");
    const uint32_t seed = (uint32_t)p.i("seed");
    const int n = std::clamp(p.i("strands"), 2, 3);
    apron(b, H, W);
    for (int k = 0; k < n; k++) {
        const double c = ((k + 0.5) / n - 0.5) * W * 0.85 + (hash01(seed, k + 5) - 0.5) * W * 0.08;
        const double w = W / n * (0.45 + 0.3 * hash01(seed, k + 9)), t = T * (0.75 + 0.35 * hash01(seed, k + 13));
        ribbon(b, 4, 14,
               [&](double u, double v) {
                   const double s = v * v * 0.35 + v * 0.65;
                   return fallAt(H, t, c + (u - 0.5) * w * (1 + 0.35 * s), s, 1 - (2 * u - 1) * (2 * u - 1));
               },
               [&](double u, double v) {
                   const double s = v * v * 0.35 + v * 0.65;
                   return V3(c + (u - 0.5) * w, s * H, 1 - 0.85 * std::pow(std::fabs(2 * u - 1), 3));
               });
    }
    pool(b, H, W, T, seed);
}

// CASCADE: white water down rock steps, hugging the face (which runs `lip` m out), a band of foam on every ledge.
void cascadeFalls(B& b, const P& p) {
    const double H = p("height"), W = p("width"), T = p("lip");
    const uint32_t seed = (uint32_t)p.i("seed");
    const int steps = std::clamp(p.i("steps"), 2, 5);
    apron(b, H, W);
    auto prof = [&](double t) {  // how far down (0..1) it is a share t of the way out: steep drops between flat ledges
        const double f = t * steps, k = std::min(std::floor(f), steps - 1.0);
        return (k + smoothstep(0.15, 0.85, f - k)) / steps;
    };
    const double out = 0.6;  // (off the face)
    ribbon(b, 8, 6 * steps,
           [&](double u, double v) { return V3(out + T * v, H + 0.05 - (H + 0.35) * prof(v), (u - 0.5) * W * (1 + 0.1 * v)); },
           [&](double u, double v) { return V3((u - 0.5) * W, prof(v) * H, 0.9 * (1 - 0.8 * std::pow(std::fabs(2 * u - 1), 4))); });
    // Foam on the ledges: where each drop lands.
    b.mat(FOAM);
    for (int k = 1; k < steps; k++) {
        const double t = (k + 0.0) / steps, y = H + 0.12 - (H + 0.35) * prof(t + 0.02), x = out + T * t;
        const V3 up(0, 1, 0);
        const V3 q[4] = {V3(x - 0.5, y, -W * 0.55), V3(x + 0.7, y, -W * 0.55), V3(x + 0.7, y, W * 0.55), V3(x - 0.5, y, W * 0.55)};
        b.triN(q[0], q[1], q[2], up, up, up);
        stamp(b, V3(0, 0, 0.9), V3(1.2, 0, 0.9), V3(1.2, 0, 0.9));
        b.triN(q[0], q[2], q[3], up, up, up);
        stamp(b, V3(0, 0, 0.9), V3(1.2, 0, 0.9), V3(0, 0, 0.9));
    }
    pool(b, H * 0.7, W, out + T, seed);
}

std::vector<Param> common(double h, double w, double lip) {
    return {num("height", "HEIGHT", h, 2, 30, 0.5), num("width", "WIDTH", w, 2, 20, 0.5), num("lip", "THROW", lip, 0.3, 6, 0.1), seedParam(7)};
}

std::vector<Param> plus(std::vector<Param> v, const Param& extra) {
    v.push_back(extra);
    return v;
}

Reg waterfall({"waterfall", "WATERFALL", "NATURE", "Bear Falls, Upper Falls: water over a lip into a plunge pool (sized per placement)", 18,
               {
                   {"SHEET", "One smooth curtain thrown out from the lip", common(18, 14, 3.2), sheetFalls},
                   {"BROKEN", "The curtain split over rock into strands", plus(common(12, 10, 2.4), num("strands", "STRANDS", 3, 2, 3, 1)), brokenFalls},
                   {"CASCADE", "White water down rock steps, foam on each ledge", plus(common(6, 12, 2.6), num("steps", "STEPS", 3, 2, 5, 1)), cascadeFalls},
               }});

}  // namespace
