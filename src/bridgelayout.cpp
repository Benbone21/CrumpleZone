// A timber bridge's wear (bridgelayout.h).
#include "bridgelayout.h"

#include <algorithm>
#include <cmath>

#include "modelkit.h"

namespace proc {

double modelQuant(double v) {
    if (v == 0 || !std::isfinite(v)) return v;
    const double step = std::ldexp(1.0, std::ilogb(std::fabs(v)) - 5);
    return std::round(v / step) * step;
}

double BridgeLayout::Piece::topAt(double z) const {
    // (A broken piece is tipped about its sound end: its far end down.)
    if (kind != 2) return -drop;
    const double w = z1 - z0, u = roll > 0 ? (z - z0) : (z1 - z);
    return -drop - std::fabs(std::sin(roll)) * std::clamp(u, 0.0, w);
}

namespace {
struct Rng {
    uint32_t seed, n = 0;
    double operator()() { return mk::hash01(seed, n++); }
};
}  // namespace

double longestHole(const BridgeLayout& b, double z) {
    // (Sampled every cm along the track: a piece counts if it's under the track's line.)
    std::vector<std::pair<double, double>> cover;
    for (const BridgeLayout::Piece& p : b.deck)
        if (z >= p.z0 - 0.015 && z <= p.z1 + 0.015) cover.push_back({p.x0, p.x1});  // (a tire spans the crack between two pieces)
    std::sort(cover.begin(), cover.end());
    double reach = -b.E, worst = 0;
    for (const auto& c : cover) {
        if (c.first > reach) worst = std::max(worst, c.first - reach);
        reach = std::max(reach, c.second);
    }
    return std::max(worst, b.E - reach);
}

double lineWorstHole(const BridgeLayout& b) {
    double worst = 0;
    for (int sd = -1; sd <= 1; sd += 2)
        for (double o : {kTrackIn, 0.5 * (kTrackIn + kTrackOut), kTrackOut}) worst = std::max(worst, longestHole(b, b.lineZ + sd * o));
    return worst;
}

BridgeLayout bridgeLayout(const BridgeLook& look) {
    BridgeLayout b;
    b.Lh = 0.5 * std::max(4.0, look.length), b.E = b.Lh + 1, b.hw = std::max(1.6, look.hw);
    b.damage = std::clamp(look.damage, 0.0, 1.0);
    b.fair = b.damage < kFairDamage;
    Rng r{look.seed * 2654435761u + 17};
    const double d = b.damage;
    // The deck: rows of planks across, each in pieces between the stringers.
    const int rows = (int)std::floor(2 * b.E / kPlankPitch);
    const double pitch = 2 * b.E / rows;
    const int segs = b.fair ? 1 : std::max(3, (int)std::lround(2 * b.hw / 0.95));
    const double sw = 2 * b.hw / segs;
    // (state: 0 sound, 1 sagging, 2 broken, 3 gone)
    std::vector<uint8_t> state((size_t)rows * segs, 0);
    std::vector<double> drop((size_t)rows * segs, 0), roll((size_t)rows * segs, 0);
    auto S = [&](int row, int q) -> uint8_t& { return state[(size_t)row * segs + q]; };
    if (!b.fair) {
        const double missP = std::clamp(look.missing, 0.0, 1.0) * d;
        // Rot comes in patches: a few rows and a piece or two across, mostly gone.
        const int patches = (int)std::lround(missP * rows * segs / 5.0);
        for (int k = 0; k < patches; k++) {
            const int rc = (int)(r() * rows), qc = (int)(r() * segs), len = 1 + (int)(r() * (1 + 3 * d)), wid = 1 + (r() < 0.35);
            for (int row = rc; row < std::min(rows, rc + len); row++)
                for (int q = qc; q < std::min(segs, qc + wid); q++) {
                    const double u = r();
                    S(row, q) = u < 0.72 ? 3 : u < 0.88 ? 2 : 1;
                }
        }
        // ...and here and there a single plank piece gone, broken or sagging.
        for (int row = 0; row < rows; row++)
            for (int q = 0; q < segs; q++) {
                if (S(row, q)) continue;
                const double u = r();
                S(row, q) = u < 0.25 * missP ? 3 : u < 0.25 * missP + 0.07 * d ? 2 : u < 0.25 * missP + 0.17 * d ? 1 : 0;
            }
        // (A plank over a tie can't sag or tip: the tie holds it up.)
        for (int row = 0; row < rows; row++) {
            const double xm = -b.E + (row + 0.5) * pitch;
            bool tie = false;
            for (int t = 0; t <= bridgeTies(b.E); t++) tie |= std::fabs(xm - bridgeTieX(b.E, t)) < 0.5 * pitch + 0.1;
            for (int q = 0; q < segs && tie; q++)
                if (S(row, q) == 1 || S(row, q) == 2) S(row, q) = 0;
        }
        for (int k = 0; k < rows * segs; k++) {
            drop[k] = state[k] == 1 ? 0.03 + 0.06 * r() : state[k] == 2 ? 0.02 + 0.05 * r() : 0;
            roll[k] = state[k] == 2 ? (r() < 0.5 ? -1 : 1) * std::atan2(0.12 + 0.13 * r(), sw) : 0;
        }
    }
    auto build = [&]() {
        b.deck.clear();
        for (int row = 0; row < rows; row++) {
            const double x0 = -b.E + row * pitch + 0.5 * kPlankGap, x1 = -b.E + (row + 1) * pitch - 0.5 * kPlankGap;
            for (int q = 0; q < segs; q++) {
                const uint8_t s = S(row, q);
                if (s == 3) continue;
                const double z0 = -b.hw + q * sw + (q ? 0.01 : 0), z1 = -b.hw + (q + 1) * sw - (q + 1 < segs ? 0.01 : 0);
                b.deck.push_back({x0, x1, z0, z1, drop[(size_t)row * segs + q], roll[(size_t)row * segs + q], (uint8_t)std::min<int>(s, 2)});
            }
        }
    };
    // The drivable line: somewhere across the deck with room for its tracks; holes along it longer than maxGap get a plank back.
    b.lineZ = b.fair ? 0 : (2 * r() - 1) * std::max(0.0, b.hw - kTrackOut - 0.15);
    build();
    const double gap = std::clamp(look.maxGap, 0.3, 1.2);
    // (A track's holes: the stretches along it no piece covers, longer than a crack.)
    auto holes = [&](double z) {
        std::vector<std::pair<double, double>> cover, out;
        for (const BridgeLayout::Piece& p : b.deck)
            if (z >= p.z0 - 0.015 && z <= p.z1 + 0.015) cover.push_back({p.x0, p.x1});
        std::sort(cover.begin(), cover.end());
        double reach = -b.E;
        for (const auto& c : cover) {
            if (c.first - reach > 0.1) out.push_back({reach, c.first});
            reach = std::max(reach, c.second);
        }
        if (b.E - reach > 0.1) out.push_back({reach, b.E});
        return out;
    };
    auto mend = [&](double x, double z) {  // (the plank piece at (x, z) put back sound)
        const int row = std::clamp((int)std::floor((x + b.E) / pitch), 0, rows - 1), q = std::clamp((int)std::floor((z + b.hw) / sw), 0, segs - 1);
        S(row, q) = 0, drop[(size_t)row * segs + q] = 0, roll[(size_t)row * segs + q] = 0;
        build();
    };
    const double band[3] = {kTrackIn, 0.5 * (kTrackIn + kTrackOut), kTrackOut};
    for (int guard = 0; guard < 8 * rows && !b.fair; guard++) {
        bool fixed = false;
        // No hole along either track longer than maxGap...
        for (int sd = -1; sd <= 1 && !fixed; sd += 2)
            for (double o : band) {
                const double z = b.lineZ + sd * o;
                for (const auto& h : holes(z))
                    if (h.second - h.first > gap) {
                        mend(0.5 * (h.first + h.second), z), fixed = true;
                        break;
                    }
                if (fixed) break;
            }
        if (fixed) continue;
        // ...and never a hole under both tracks at once (the truck would drop both wheels of an axle in): one side's put back.
        std::vector<std::pair<double, double>> side[2];
        for (int k = 0; k < 2; k++)
            for (double o : band)
                for (const auto& h : holes(b.lineZ + (k ? o : -o))) side[k].push_back(h);
        for (const auto& l : side[0]) {
            for (const auto& rr : side[1]) {
                const double o0 = std::max(l.first, rr.first), o1 = std::min(l.second, rr.second);
                if (o1 - o0 < 0.05) continue;
                for (double o : band) mend(0.5 * (o0 + o1), b.lineZ + o);
                fixed = true;
                break;
            }
            if (fixed) break;
        }
        if (!fixed) break;
    }
    // Posts and rails.
    for (int side = 0; side < 2; side++) {
        const int sd = side ? 1 : -1;
        for (int k = 0; k <= 4; k++) {
            BridgeLayout::Post& p = b.posts[side][k];
            p.x = -b.Lh + 0.5 * b.Lh * k, p.z = sd * (b.hw + 0.05), p.lean = 0, p.tip = 0, p.state = 0;
            if (b.fair) continue;
            const double u = r();
            if (u < 0.22 * d) p.state = 2;
            else if (u < 0.22 * d + 0.5 * d) p.state = 1, p.lean = sd * (0.08 + 0.22 * r()), p.tip = (r() - 0.5) * 0.25;
        }
        for (int k = 0; k < 4; k++) {
            if (b.fair) continue;
            const double u = r();
            const int a = b.posts[side][k].state, c = b.posts[side][k + 1].state;  // (a rail stays whole only between two standing posts)
            b.rail[side][k] = a == 2 || c == 2 || u < 0.3 * d ? 2 : a == 1 || c == 1 || u < 0.6 * d ? 1 : 0;
        }
    }
    return b;
}

}  // namespace proc
