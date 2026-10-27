// The procedural world's set pieces as models: waterfalls and timber bridges (procdress.h).
#include "procdress.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "bridgelayout.h"
#include "modelkit.h"
#include "procrules.h"
#include "procworld.h"
#include "world.h"

namespace proc {

thread_local bool dressWindows = !std::getenv("CZ_NO_PROCDRESS");  // (CZ_NO_PROCDRESS=1: the bare generator's window, to measure against)

namespace {
uint32_t spotHash(double gx, double gz, uint32_t salt) {  // (of a global spot, to 10 cm: every window that holds it alike)
    uint64_t h = 0xFA11ull + salt;
    for (int64_t v : {(int64_t)std::llround(gx * 10), (int64_t)std::llround(gz * 10)}) {
        h ^= (uint64_t)v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h = (h ^ (h >> 31)) * 0xBF58476D1CE4E5B9ull;
        h ^= h >> 29;
    }
    return (uint32_t)(h >> 32);
}
double spot01(double gx, double gz, uint32_t salt) { return (spotHash(gx, gz, salt) & 0xFFFFFF) / 16777216.0; }
}  // namespace

int bridgeVariant(double L) { return L <= 12 ? 0 : bridgeHasPier(L) ? 2 : 1; }

void dressFalls(World& w) {
    if (!dressWindows) return;
    const ProcWorld& G = ProcWorld::forSeed(w.proc.seed);
    const double ox = (double)w.proc.ox, oz = (double)w.proc.oz, half = w.halfSize() - 24;
    const uint64_t editsBefore = w.cellEdits;
    for (const FallSpan& s : fallSpans(G, ox - half, oz - half, ox + half, oz + half)) {
        const double H = s.top - s.bottom;
        if (H < 1.5) continue;
        const V3 d(s.fx, 0, s.fz), l(-s.fz, 0, s.fx);  // (down the fall; to its left)
        const V3 lip(s.x - ox, s.top, s.z - oz);
        const double W = std::max(1.8, s.width * 0.92), len = std::max(1.5, s.len);
        Falls f;
        f.lip = lip;
        f.yaw = std::atan2(-s.fz, s.fx);
        f.height = H;
        f.width = W;
        f.seed = spotHash(s.x, s.z, 1);
        f.poolR = 0.5 * W + 2.5, f.poolDepth = 1.3;
        // Its design: a short drop tumbles down the step; a tall one leaves the lip as a curtain (wide creeks now and then torn in strands).
        if (H < 5) f.variant = 2, f.throwOut = std::max(1.0, len - 0.6);
        else f.variant = W > 4.5 && spot01(s.x, s.z, 2) < 0.4 ? 1 : 0, f.throwOut = len;
        w.falls.push_back(f);
        auto onTrail = [&](const V3& p) {  // (within a couple of metres of a trail's tread, or a ford's gravel)
            for (int k = 0; k < 9; k++) {
                const RoadInfo ri = w.roadAt(p.x + 2 * (k % 3 - 1), p.z + 2 * (k / 3 - 1));
                if (ri.lateral != 0 || ri.paint == PAINT_TRAIL || ri.paint == PAINT_ROAD || ri.paint == PAINT_GRAVEL) return true;
            }
            return false;
        };
        // Dark, wet boulders: the lip's shoulders, and a few round the plunge pool's rim (none on a trail, none in the channel).
        auto boulder = [&](const V3& p, double r, uint32_t salt) {
            if (onTrail(p) || w.cellIndex(p.x, p.z) < 0) return;
            const double gx = p.x + ox, gz = p.z + oz;
            const double rr = r * (0.8 + 0.4 * spot01(gx, gz, salt));
            w.addRock(p.x, p.z, V3(rr, rr * 0.7, rr * 0.85), rr * 0.9, spotHash(gx, gz, salt + 7) & 0x3FFFFFFF, 0.5f);
        };
        // (Tall posed slabs flanking the sheet were tried: two big scanned rocks there cost ~0.45 ms of shadow GPU time on a 7900 XTX.)
        for (int sd = -1; sd <= 1; sd += 2) {
            boulder(lip - d * 0.6 + l * (sd * (0.5 * W + 1.4)), 1.0 + 0.04 * H, 11 + sd);
            boulder(lip + d * (len + 0.8) + l * (sd * (0.5 * W + 2.6)), 1.1 + 0.03 * H, 21 + sd);
            boulder(lip + d * (len + 7.0) + l * (sd * (0.5 * W + 2.2)), 0.8, 31 + sd);
        }
        // The water down the step: its grid levels stairstep there (the model is the water). Cells over the step whose water stands
        // between the lip's and the pool's go dry (the water mesh then stops at the lip and starts again at the pool).
        const double reach = 0.5 * W + 3.0;
        const double x0 = std::min(lip.x, lip.x + d.x * len) - reach - 1, x1 = std::max(lip.x, lip.x + d.x * len) + reach + 1;
        const double z0 = std::min(lip.z, lip.z + d.z * len) - reach - 1, z1 = std::max(lip.z, lip.z + d.z * len) + reach + 1;
        for (double x = std::floor(x0); x <= x1; x += 1)
            for (double z = std::floor(z0); z <= z1; z += 1) {
                const int id = w.cellIndex(x, z);
                if (id < 0) continue;
                const V3 c = w.cellCenter(id);
                const double t = (c.x - lip.x) * d.x + (c.z - lip.z) * d.z, a = (c.x - lip.x) * l.x + (c.z - lip.z) * l.z;
                if (t < -1.5 || t > len + 1.5 || std::fabs(a) > reach) continue;
                const float wl = w.waterY[id];
                if (wl > -1e8f && t > 0.2 && t < len + 0.8 && wl < s.top - 0.25 && wl > s.bottom + 0.25) w.waterY[id] = -1e9f, w.flowX[id] = 0, w.flowZ[id] = 0;
                // The step and the shoulders of rock beside it: bare (wet) rock, not grass on a 70-degree face.
                if (w.waterY[id] < -1e8f && w.roadAt(c.x, c.z).paint == PAINT_NONE && t > -1.0 && t < len + 1.0)
                    w.setCellSurface(id, SURF_ROCK, PAINT_ROCK);
            }
    }
    w.cellEdits = editsBefore;  // (the window as built: nothing was changed by hand)
}

// How worn a bridge is (0 sound .. 1): seeded by where it is, so every window and every player has it alike. Most are rough to
// very rough, a share (rules: bridges.fair_share) in fair shape; main trails' are kept up better.
double bridgeWear(const TrailEdge::Span& b, bool mainTrail) {
    const BridgeRule& R = rules().bridge;
    const double u = spot01(b.ax, b.az, 51), v = spot01(b.ax, b.az, 52);
    const double fair = std::clamp(R.fairShare * (mainTrail ? 1.4 : 0.5), 0.0, 1.0);
    if (u < fair) return 0.04 + 0.16 * v;
    return std::clamp(R.damage * (mainTrail ? 0.6 + 0.5 * v : 0.8 + 0.4 * v), kFairDamage + 0.05, 1.0);
}

void dressBridges(World& w, size_t firstBox, size_t firstPole) {
    if (!dressWindows || firstBox >= w.boxes.size() || !mk::findModel("timber_bridge")) return;
    for (size_t i = firstBox; i < w.boxes.size(); i++) w.boxes[i].hidden = true;  // (the stand-ins: collision only, under the model)
    std::vector<char> deadBox(w.boxes.size(), 0), deadPole(w.poles.size(), 0);
    size_t bc = firstBox, pc = firstPole;  // (buildBridges' boxes and posts, span by span: the same walk as its)
    const ProcWorld& G = ProcWorld::forSeed(w.proc.seed);
    const double half = w.halfSize() - 12;
    const int64_t i0 = floorDiv(w.proc.ox - (int64_t)w.halfSize(), kCell) - 1, i1 = floorDiv(w.proc.ox + (int64_t)w.halfSize(), kCell) + 1;
    const int64_t j0 = floorDiv(w.proc.oz - (int64_t)w.halfSize(), kCell) - 1, j1 = floorDiv(w.proc.oz + (int64_t)w.halfSize(), kCell) + 1;
    struct { double r, g, b; } const wood{0.36, 0.27, 0.18};
    for (int64_t i = i0; i <= i1; i++)
        for (int64_t j = j0; j <= j1; j++)
            for (int dir = 0; dir < 2; dir++) {
                const auto e = G.edge(i, j, dir);
                if (!e->on) continue;
                for (const TrailEdge::Span& b : e->bridges) {
                    const double ax = b.ax - (double)w.proc.ox, az = b.az - (double)w.proc.oz, bx = b.bx - (double)w.proc.ox, bz = b.bz - (double)w.proc.oz;
                    const double mx = 0.5 * (ax + bx), mz = 0.5 * (az + bz);
                    if (std::fabs(mx) > half || std::fabs(mz) > half) continue;
                    const double dx = bx - ax, dz = bz - az, L = std::sqrt(dx * dx + dz * dz);
                    if (L < 1) continue;
                    const double bed = w.height(mx, mz);
                    const bool standInPier = L > 22 && b.y - 0.7 > bed;
                    const size_t deckBox = bc, railBox[2] = {bc + 2, bc + 4}, posts0 = pc;
                    bc += 5 + (standInPier ? 1 : 0), pc += 10;
                    if (bc > w.boxes.size() || pc > w.poles.size()) return;  // (not buildBridges' walk after all: leave the stand-ins as they are)
                    const double yaw = std::atan2(-dz, dx), ux = dx / L, uz = dz / L;
                    // The model's numbers, as its mesh cache will round them (so the collision below is made from the same ones).
                    const double Lq = modelQuant(L), hw = modelQuant(e->half() + 0.6), sx = L / Lq;
                    BridgeLook look;
                    look.length = Lq, look.hw = hw;
                    look.damage = modelQuant(bridgeWear(b, e->road));
                    look.missing = modelQuant(rules().bridge.missingShare), look.maxGap = modelQuant(rules().bridge.maxGap);
                    const int s1 = 1 + (int)(spotHash(b.ax, b.az, 3) % 63), s2 = 1 + (int)(spotHash(b.ax, b.az, 4) % 63);
                    look.seed = bridgeSeed(s1, s2);
                    const BridgeLayout lay = bridgeLayout(look);
                    auto depth = [&](double x, double z) { return modelQuant(std::clamp(b.y - w.height(x, z), 0.6, 8.0)); };
                    Placed pl;
                    pl.id = "timber_bridge";
                    pl.p = V3(mx, b.y, mz);
                    pl.yaw = yaw;
                    pl.variant = bridgeVariant(L);
                    pl.scale = V3(sx, 1, 1);
                    pl.set = {{"length", Lq}, {"half", hw}, {"endA", depth(ax - ux * 0.5, az - uz * 0.5)}, {"endB", depth(bx + ux * 0.5, bz + uz * 0.5)},
                              {"seed", (double)s1}, {"seed2", (double)s2}, {"damage", look.damage}, {"missing", look.missing}, {"gap", look.maxGap}};
                    if (bridgeHasPier(L)) pl.set.push_back({"pier", modelQuant(std::max(0.0, b.y - bed))});
                    w.models.push_back(pl);
                    if (lay.fair) continue;  // (a sound deck: the stand-in's one box is it)
                    // A worn bridge: its deck, rails and posts are the layout's, piece by piece. (The outer stringers and the pier stay.)
                    deadBox[deckBox] = deadBox[railBox[0]] = deadBox[railBox[1]] = 1;
                    for (size_t k = posts0; k < posts0 + 10; k++) deadPole[k] = 1;
                    const V3 O(mx, b.y, mz), fw(ux, 0, uz), lf(-uz, 0, ux);
                    auto at = [&](double x, double y, double z) { return O + fw * (x * sx) + V3(0, y, 0) + lf * z; };
                    auto solid = [&](const V3& c, const V3& h, double roll) {
                        w.addBox(c, h, yaw, 0, roll, V3(wood.r, wood.g, wood.b));
                        w.boxes.back().hidden = true;
                    };
                    // Plank pieces: a sound piece's run along the deck (the same across, level) one box; a sagging or broken one its own.
                    std::vector<BridgeLayout::Piece> ps = lay.deck;
                    std::sort(ps.begin(), ps.end(), [](const BridgeLayout::Piece& p, const BridgeLayout::Piece& q) { return p.z0 != q.z0 ? p.z0 < q.z0 : p.x0 < q.x0; });
                    const double t = 0.3,  // (solid as deep as the stand-in deck: a thin box lets a tire's nodes through or wedges them; the top is the plank's)
                                 pitch = 2 * lay.E / std::floor(2 * lay.E / kPlankPitch);
                    for (size_t k = 0; k < ps.size();) {
                        const BridgeLayout::Piece& p = ps[k];
                        if (p.kind == 0) {
                            size_t m = k + 1;
                            while (m < ps.size() && ps[m].kind == 0 && ps[m].z0 == p.z0 && ps[m].x0 - ps[m - 1].x1 < pitch * 0.5) m++;
                            const double x0 = p.x0, x1 = ps[m - 1].x1;
                            solid(at(0.5 * (x0 + x1), -0.5 * t, 0.5 * (p.z0 + p.z1)), V3(0.5 * (x1 - x0) * sx, 0.5 * t, 0.5 * (p.z1 - p.z0)), 0);
                            k = m;
                            continue;
                        }
                        const double xm = 0.5 * (p.x0 + p.x1), hx = 0.5 * (p.x1 - p.x0) * sx, wz = p.z1 - p.z0;
                        if (p.kind == 1) solid(at(xm, -p.drop - 0.5 * t, 0.5 * (p.z0 + p.z1)), V3(hx, 0.5 * t, 0.5 * wz), 0);
                        else {  // tipped about its sound end (bridgelayout.cpp topAt)
                            const double th = p.roll, sn = std::sin(std::fabs(th)), cs = std::cos(th);
                            const double pz = th > 0 ? p.z0 : p.z1, dz2 = th > 0 ? cs : -cs;  // (pivot, and the way along it, across)
                            const double nz = th > 0 ? sn : -sn, ny = cs;                     // (its top's normal, in (z, y))
                            const double cz = pz + dz2 * 0.5 * wz - nz * 0.5 * t, cy = -p.drop - sn * 0.5 * wz - ny * 0.5 * t;
                            solid(at(xm, cy, cz), V3(hx, 0.5 * t, 0.5 * wz), th);
                        }
                        k++;
                    }
                    // Ties under them, and the stringers between the outer two: what a wheel through a hole comes down on.
                    for (int k = 0; k <= bridgeTies(lay.E); k++) solid(at(bridgeTieX(lay.E, k), -0.2, 0), V3(0.1 * sx, 0.1, hw + 0.1), 0);
                    const int ns = bridgeStringers(hw);
                    for (int k = 1; k + 1 < ns; k++) solid(at(0, -0.55, bridgeStringerZ(hw, k)), V3(lay.E * sx, 0.25, 0.25), 0);
                    // Whole rails, and posts where they still stand (a leaning one where it is at the height a bumper meets it).
                    for (int side = 0; side < 2; side++) {
                        for (int k = 0; k < 4; k++)
                            if (lay.rail[side][k] == 0) {
                                const double xa = lay.posts[side][k].x, xb = lay.posts[side][k + 1].x;
                                solid(at(0.5 * (xa + xb), 0.45, lay.posts[side][k].z), V3(0.5 * (xb - xa) * sx + 0.1, 0.08, 0.08), 0);
                            }
                        for (int k = 0; k <= 4; k++) {
                            const BridgeLayout::Post& p = lay.posts[side][k];
                            if (p.state == 2) continue;
                            const double reach = p.state == 1 ? 1.1 : 0.0;
                            const V3 q = at(p.x + std::sin(p.tip) * reach, 0, p.z + std::sin(p.lean) * reach);
                            w.poles.push_back({q.x, q.z, b.y - 0.1, b.y + (p.state == 1 ? 0.4 : 0.55), 0.09});
                        }
                    }
                }
            }
    // (The stand-ins a worn bridge's own pieces replaced: out.)
    size_t keep = firstBox;
    for (size_t k = firstBox; k < w.boxes.size(); k++)
        if (k >= deadBox.size() || !deadBox[k]) w.boxes[keep++] = w.boxes[k];
    w.boxes.resize(keep);
    keep = firstPole;
    for (size_t k = firstPole; k < w.poles.size(); k++)
        if (k >= deadPole.size() || !deadPole[k]) w.poles[keep++] = w.poles[k];
    w.poles.resize(keep);
}

}  // namespace proc
