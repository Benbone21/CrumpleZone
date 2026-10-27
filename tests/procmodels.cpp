// The procedural world's set pieces as models (src/procdress.h): each fall in a window gets its falls mesh and its water down the
// step taken out; each bridge gets its timber model over the generator's stand-in boxes, which stay exactly what's driven on.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../src/bridgelayout.h"
#include "../src/modelkit.h"
#include "../src/procrules.h"
#include "../src/vehicles.h"
#include "../src/procdress.h"
#include "../src/procworld.h"
#include "../src/watermesh.h"
#include "harness.h"

using namespace proc;

namespace {
// The layout a placed bridge model was built with (its numbers as the mesh cache rounds them), and its frame in the world.
struct PlacedBridge {
    BridgeLayout lay;
    V3 o, fw, lf;
    double sx = 1, gap = 0;
    V3 at(double x, double y, double z) const { return o + fw * (x * sx) + V3(0, y, 0) + lf * z; }
};
PlacedBridge placedBridge(const Placed& pl) {
    auto get = [&](const char* k) {
        for (const auto& kv : pl.set)
            if (kv.first == k) return modelQuant(kv.second);
        return 0.0;
    };
    BridgeLook look;
    look.length = get("length"), look.hw = get("half"), look.damage = get("damage"), look.missing = get("missing"), look.maxGap = get("gap");
    look.seed = bridgeSeed((int)get("seed"), (int)get("seed2"));
    PlacedBridge b;
    b.lay = bridgeLayout(look);
    b.o = pl.p, b.fw = V3(std::cos(pl.yaw), 0, -std::sin(pl.yaw)), b.lf = V3(std::sin(pl.yaw), 0, std::cos(pl.yaw)), b.sx = pl.scale.x, b.gap = look.maxGap;
    return b;
}
// Whether a small ball just at (x, y, z) of the world touches anything solid (boxes, posts).
bool solidAt(const World& w, const V3& p, bool poles = true) {
    Contact c;
    return w.contact(p, 0.03, nullptr, 0, c, poles) && c.pen > 0.005;
}
// A bridge near the middle of the seeds' worlds: the roughest worn one, or the first fair one (seed 0: none). Its middle and span.
uint64_t findBridge(bool worn, double& gx, double& gz, double& len) {
    uint64_t best = 0;
    double bestWear = -1;
    for (uint64_t s : {1ull, 2ull, 3ull, 4ull, 5ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(s);
        for (int64_t i = -6; i < 6; i++)
            for (int64_t j = -6; j < 6; j++)
                for (int dir = 0; dir < 2; dir++) {
                    auto e = G.edge(i, j, dir);
                    if (!e->on) continue;
                    for (const TrailEdge::Span& b : e->bridges) {
                        const double d = bridgeWear(b, e->road);
                        if ((d >= kFairDamage) != worn || (worn ? d <= bestWear : best != 0)) continue;
                        best = s, bestWear = d, gx = 0.5 * (b.ax + b.bx), gz = 0.5 * (b.az + b.bz), len = std::hypot(b.bx - b.ax, b.bz - b.az);
                    }
                }
    }
    return best;
}

World windowRound(uint64_t seed, double gx, double gz, bool dress = true) {
    World::procSeed = seed;
    World::procOriginSet = true;
    World::procOriginX = (int64_t)gx, World::procOriginZ = (int64_t)gz;
    dressWindows = dress;
    World w(MAP_PROCEDURAL);
    dressWindows = true;
    World::procOriginSet = false;
    return w;
}
}  // namespace

SCENARIO(procModelsFalls, "procmodels/falls", "Every fall in a window is a World::falls with its falls mesh (sheet, foam, spray, ripples), the water grid's stairstep down its rock step taken out, wet rock round it") {
    int windows = 0, fallsSeen = 0, matched = 0, meshed = 0, stepWet = 0, rockCells = 0, rockChecked = 0;
    std::string where;
    for (uint64_t seed : {1ull, 2ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        const std::vector<FallSpan> all = fallSpans(G, -5000, -5000, 5000, 5000);
        if (all.empty()) continue;
        const FallSpan& f0 = all.front();
        World w = windowRound(seed, f0.x, f0.z);
        windows++;
        const double ox = (double)w.proc.ox, oz = (double)w.proc.oz, half = w.halfSize() - 24;
        std::vector<Vtx> wv;
        buildWaterMesh(w, wv);
        for (const FallSpan& s : fallSpans(G, ox - half, oz - half, ox + half, oz + half)) {
            if (s.top - s.bottom < 1.5) continue;
            fallsSeen++;
            const V3 lip(s.x - ox, s.top, s.z - oz);
            if (where.empty()) {
                char b[200];
                std::snprintf(b, sizeof b, "a fall: seed %llu, global %.0f,%.0f (window %lld,%lld: local %.1f,%.1f), %.1f m high, %.1f m wide, way down %.2f,%.2f", (unsigned long long)seed, s.x, s.z,
                              (long long)w.proc.ox, (long long)w.proc.oz, lip.x, lip.z, s.top - s.bottom, s.width, s.fx, s.fz);
                where = b;
            }
            const Falls* hit = nullptr;
            for (const Falls& f : w.falls)
                if (std::hypot(f.lip.x - lip.x, f.lip.z - lip.z) < 0.5 && std::fabs(f.lip.y - lip.y) < 0.05) hit = &f;
            if (!hit) continue;
            matched++;
            int n = 0;  // (the water mesh's falls vertices round it)
            for (const Vtx& v : wv)
                n += v.c[3] > 19.5f && std::hypot(v.p[0] - lip.x, v.p[2] - lip.z) < 40 && v.p[1] > lip.y - (s.top - s.bottom) - 1 && v.p[1] < lip.y + 30;
            meshed += n > 100;
            // Down the step: no grid water between the lip's level and the pool's.
            const V3 d(s.fx, 0, s.fz);
            for (double t = 0.5; t < s.len; t += 0.5) {
                const V3 q = lip + d * t;
                const double wl = w.waterLevel(q.x, q.z);
                stepWet += wl > -1e8 && wl < s.top - 0.3 && wl > s.bottom + 0.3;
                if (wl < -1e8) rockChecked++, rockCells += w.surfaceAt(q.x, q.z) == SURF_ROCK;
            }
        }
    }
    if (!where.empty()) ctx.note(where);
    ctx.note(std::to_string(fallsSeen) + " falls in " + std::to_string(windows) + " windows");
    ctx.check("there are falls to dress", fallsSeen > 0);
    ctx.check("each fall in a window is one of its World::falls", matched == fallsSeen, std::to_string(matched) + " of " + std::to_string(fallsSeen));
    ctx.check("each has its falls mesh in the water mesh", meshed == matched, std::to_string(meshed) + " of " + std::to_string(matched));
    ctx.check("no stairstepped grid water left down a step", stepWet == 0, std::to_string(stepWet) + " points");
    ctx.metric("share of the step's dry ground that's bare rock", rockChecked ? (double)rockCells / rockChecked : 0, "", 0.6, 1.0);
}

SCENARIO(procModelsBridgeWear, "procmodels/bridge-wear", "Bridges are worn by the rules: most rough, a few fair, main trails' kept better; every bridge keeps a drivable line with no hole on it longer than bridges.max_gap") {
    const BridgeRule& R = rules().bridge;
    long n = 0, fair = 0, mainN = 0, otherN = 0;
    double mainD = 0, otherD = 0, worstLine = 0, gone = 0, pieces = 0, roughest = 0;
    std::string roughAt, fairAt;
    for (uint64_t seed : {1ull, 2ull, 3ull, 4ull, 5ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        for (int64_t i = -8; i < 8; i++)
            for (int64_t j = -8; j < 8; j++)
                for (int dir = 0; dir < 2; dir++) {
                    auto e = G.edge(i, j, dir);
                    if (!e->on) continue;
                    for (const TrailEdge::Span& b : e->bridges) {
                        const double d = bridgeWear(b, e->road);
                        n++, fair += d < kFairDamage;
                        char at[120];
                        std::snprintf(at, sizeof at, "seed %llu at %.1f,%.1f (yaw %.1f deg, span %.0f m)", (unsigned long long)seed, 0.5 * (b.ax + b.bx), 0.5 * (b.az + b.bz),
                                      std::atan2(-(b.bz - b.az), b.bx - b.ax) * 180 / M_PI, std::hypot(b.bx - b.ax, b.bz - b.az));
                        if (d > roughest) roughest = d, roughAt = at;
                        if (d < kFairDamage && fairAt.empty()) fairAt = at;
                        (e->road ? mainD : otherD) += d, (e->road ? mainN : otherN)++;
                        BridgeLook look;
                        look.length = modelQuant(std::hypot(b.bx - b.ax, b.bz - b.az)), look.hw = modelQuant(e->half() + 0.6), look.damage = modelQuant(d);
                        look.missing = modelQuant(R.missingShare), look.maxGap = modelQuant(R.maxGap), look.seed = (uint32_t)(n * 7 + 3);
                        const BridgeLayout L = bridgeLayout(look);
                        worstLine = std::max(worstLine, lineWorstHole(L));
                        if (!L.fair) {
                            const int rows = (int)std::floor(2 * L.E / kPlankPitch), segs = std::max(3, (int)std::lround(2 * L.hw / 0.95));
                            pieces += rows * segs, gone += rows * segs - (double)L.deck.size();
                        }
                    }
                }
    }
    char b[200];
    std::snprintf(b, sizeof b, "%ld bridges: %ld fair; mean wear on main trails %.2f (%ld), others %.2f (%ld)", n, fair, mainN ? mainD / mainN : 0, mainN, otherN ? otherD / otherN : 0, otherN);
    ctx.note(b);
    ctx.note("the roughest (wear " + std::to_string(roughest) + "): " + roughAt + "; a fair one: " + fairAt);
    ctx.check("there are bridges", n >= 10);
    ctx.metric("share in fair shape", n ? (double)fair / n : 0, "", 0.03, 0.35);
    ctx.check("main trails' bridges kept better than others'", !mainN || !otherN || mainD / mainN < otherD / otherN);
    ctx.metric("worn decks' plank pieces gone", pieces ? gone / pieces : 0, "", 0.08, 0.45);
    ctx.metric("the longest hole on any bridge's drivable line", worstLine, "m", 0, R.maxGap + 0.04);
}

SCENARIO(procModelsBridges, "procmodels/bridge-collision", "A bridge model on every span. A fair bridge drives on the generator's stand-in boxes, unchanged; a worn one's collision is its planks as drawn (holes are holes), and its drivable line holds in the collision") {
    double gx = 0, gz = 0, len = 0;
    const uint64_t seed = findBridge(true, gx, gz, len);
    ctx.check("found a worn bridge", seed != 0);
    if (!seed) return;
    World bare = windowRound(seed, gx, gz, false), dressed = windowRound(seed, gx, gz, true);
    char b[220];
    std::snprintf(b, sizeof b, "a worn bridge: seed %llu at %.1f,%.1f (window %lld,%lld: local %.1f,%.1f), span %.1f m", (unsigned long long)seed, gx, gz, (long long)bare.proc.ox,
                  (long long)bare.proc.oz, gx - (double)bare.proc.ox, gz - (double)bare.proc.oz, len);
    ctx.note(b);
    std::vector<PlacedBridge> bridges;
    for (const Placed& pl : dressed.models)
        if (pl.id == "timber_bridge") bridges.push_back(placedBridge(pl));
    int worn = 0;
    for (const PlacedBridge& pb : bridges) worn += !pb.lay.fair;
    ctx.note(std::to_string(bridges.size()) + " bridges in the window, " + std::to_string(worn) + " worn");
    // Every box and post of the bare window is in the dressed one, but the decks, rails and posts of the worn bridges (3 boxes, 10 posts each).
    auto sameBox = [](const Box& p, const Box& q) { return !std::memcmp(&p.c, &q.c, sizeof p.c) && !std::memcmp(&p.h, &q.h, sizeof p.h) && !std::memcmp(p.ax, q.ax, sizeof p.ax) && p.solid == q.solid; };
    int boxesGone = 0, polesGone = 0;
    for (const Box& p : bare.boxes) boxesGone += std::none_of(dressed.boxes.begin(), dressed.boxes.end(), [&](const Box& q) { return sameBox(p, q); });
    for (const Pole& p : bare.poles) polesGone += std::none_of(dressed.poles.begin(), dressed.poles.end(), [&](const Pole& q) { return !std::memcmp(&p, &q, sizeof p); });
    ctx.check("the fair bridges and everything else: the same collision, bit for bit", boxesGone == 3 * worn && polesGone == 10 * worn,
              std::to_string(boxesGone) + " boxes and " + std::to_string(polesGone) + " posts gone");
    // A worn bridge's deck: solid just under each plank piece's top, and nothing where a piece is gone (between the ties).
    int on = 0, onMiss = 0, holes = 0, holeHit = 0;
    double worstLine = 0;
    for (const PlacedBridge& pb : bridges) {
        if (pb.lay.fair) continue;
        for (const BridgeLayout::Piece& p : pb.lay.deck) {
            const double xm = 0.5 * (p.x0 + p.x1), zm = 0.5 * (p.z0 + p.z1);
            const bool miss = !solidAt(dressed, pb.at(xm, p.topAt(zm) - 0.01, zm));
            on++, onMiss += miss;
            if (miss && std::getenv("BRIDGE_DEBUG")) std::printf("piece x %.2f..%.2f z %.2f..%.2f kind %d: not solid\n", p.x0, p.x1, p.z0, p.z1, p.kind);
        }
        // Holes: points on the deck's plane no piece covers, clear of the ties.
        const int rows = (int)std::floor(2 * pb.lay.E / kPlankPitch);
        const double pitch = 2 * pb.lay.E / rows;
        for (int r = 0; r < rows; r++)
            for (int q = 0, segs = std::max(3, (int)std::lround(2 * pb.lay.hw / 0.95)); q < segs; q++) {
                const double z = -pb.lay.hw + (q + 0.5) * 2 * pb.lay.hw / segs;
                const double x = -pb.lay.E + (r + 0.5) * pitch;
                bool covered = false;
                for (const BridgeLayout::Piece& p : pb.lay.deck) covered |= x >= p.x0 - 0.02 && x <= p.x1 + 0.02 && z >= p.z0 - 0.02 && z <= p.z1 + 0.02;
                bool tie = false;
                for (int t = 0; t <= bridgeTies(pb.lay.E); t++) tie |= std::fabs(x - bridgeTieX(pb.lay.E, t)) < 0.16;
                const V3 hp = pb.at(x, -0.05, z);
                if (covered || tie || dressed.height(hp.x, hp.z) > hp.y - 0.3) continue;  // (where the bank comes up through the deck: the land's, not a hole)
                holes++, holeHit += solidAt(dressed, hp, false);  // (nor a tree's trunk through it: the generator's, see the report)
                if (solidAt(dressed, hp, false) && std::getenv("BRIDGE_DEBUG")) std::printf("hole x %.2f z %.2f E %.2f hw %.2f\n", x, z, pb.lay.E, pb.lay.hw);
            }
        // The drivable line, in the collision: along each wheel track the longest stretch with no deck under it.
        for (int sd = -1; sd <= 1; sd += 2)
            for (double o : {kTrackIn, kTrackOut}) {
                const double z = pb.lay.lineZ + sd * o;
                double run = 0, worst = 0;
                for (double x = -pb.lay.E; x <= pb.lay.E; x += 0.02) {
                    bool deck = false;
                    for (double y : {-0.02, -0.08, -0.16, -0.24}) deck |= solidAt(dressed, pb.at(x, y, z));
                    run = deck ? 0 : run + 0.02 * pb.sx, worst = std::max(worst, run);
                }
                worstLine = std::max(worstLine, worst);
            }
    }
    ctx.check("a worn bridge to look at", worn > 0);
    ctx.check("every plank piece is solid where it's drawn", onMiss == 0, std::to_string(onMiss) + " of " + std::to_string(on) + " pieces");
    ctx.check("every hole is open (nothing solid at the deck's top where no plank is)", holes > 0 && holeHit == 0, std::to_string(holeHit) + " of " + std::to_string(holes) + " holes");
    ctx.metric("the longest hole on a worn bridge's drivable line, in its collision", worstLine, "m", 0, rules().bridge.maxGap + 0.08);
    // The model draws the same planks: a ray down through each piece's middle meets the mesh at its top; through a hole, no plank.
    const mk::ModelDef* def = mk::findModel("timber_bridge");
    ctx.check("the timber bridge model is registered, three designs", def && def->variants.size() == 3);
    if (!def) return;
    for (const Placed& pl : dressed.models) {
        if (pl.id != "timber_bridge") continue;
        const PlacedBridge pb = placedBridge(pl);
        if (pb.lay.fair) continue;
        mk::P p;
        for (const auto& kv : pl.set) p.v[kv.first] = modelQuant(kv.second);
        mk::Mesh m;
        mk::build(*def, pl.variant, p, m);
        auto topHit = [&](double x, double z) {  // (the highest triangle over (x, z) in the deck's band)
            double best = -1e9;
            for (size_t k = 0; k + 2 < m.v.size(); k += 3) {
                const float* a = m.v[k].p, *bb = m.v[k + 1].p, *c = m.v[k + 2].p;
                const double d = (bb[0] - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (bb[2] - a[2]);
                if (std::fabs(d) < 1e-12) continue;
                const double u = ((x - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (z - a[2])) / d, v = ((bb[0] - a[0]) * (z - a[2]) - (x - a[0]) * (bb[2] - a[2])) / d;
                if (u < 0 || v < 0 || u + v > 1) continue;
                const double y = a[1] + u * (bb[1] - a[1]) + v * (c[1] - a[1]);
                if (y < 0.05 && y > -0.35) best = std::max(best, y);
            }
            return best;
        };
        int bad = 0, n = 0;
        for (size_t k = 0; k < pb.lay.deck.size(); k += 7) {
            const BridgeLayout::Piece& q = pb.lay.deck[k];
            const double xm = 0.5 * (q.x0 + q.x1), zm = 0.5 * (q.z0 + q.z1);
            n++, bad += std::fabs(topHit(xm, zm) - q.topAt(zm)) > 0.02;
        }
        ctx.check("the drawn planks are where the collision's are", bad == 0, std::to_string(bad) + " of " + std::to_string(n) + " pieces");
        ctx.metric("a worn bridge's triangles", (double)m.triangles(), "", 500, 20000);
        break;
    }
    // A fair one: its stand-in boxes are its collision (checked above), its model the same three designs.
    double fx = 0, fz = 0, fl = 0;
    ctx.check("there are fair bridges too", findBridge(false, fx, fz, fl) != 0);
}

namespace {
// Drives `spec` across a worn bridge along its drivable line at a crawl: true if it gets off the far end on its wheels.
bool crossBridge(Ctx& ctx, const VehicleSpec& spec, const PlacedBridge& pb, double& worstDrop, double& seconds) {
    // (Set down on the bank before it, on the land itself: ctx.add's clearY looks over everything near, and a bridge's rail or a
    // tree's crown there would drop it from metres up and burst its tires.)
    const V3 start = pb.at(-pb.lay.E - 5, 0, pb.lay.lineZ), dir = pb.fw;
    double y = -1e9;
    for (double a : {-2.6, 0.0, 2.6})
        for (double b : {-1.0, 0.0, 1.0}) {
            const V3 q = start + dir * a + pb.lf * b;
            y = std::max(y, ctx.sim.world.height(q.x, q.z));
        }
    ctx.sim.addCar(spec, V3(start.x, y + 0.05, start.z), std::atan2(-dir.z, dir.x), ctx.sim.nextColor());
    Car& c = ctx.sim.cars.back();
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);  // (4L: a crawl)
    c.tractionControl = false;
    worstDrop = 0;
    const double t0 = ctx.t;
    auto along = [&](const V3& p) { return dot(p - pb.o, pb.fw) / pb.sx; };
    const bool ok = ctx.runUntil(90, [&] { return along(c.frame().com) > pb.lay.E + 4; }, [&] {
        const Frame fr = c.frame();
        const double x = along(fr.com);
        const V3 target = pb.at(x + 3, 0, pb.lay.lineZ);  // (a short look ahead: it keeps its wheels on the line)
        const V3 d = target - fr.com;
        c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 2.2, -0.55, 0.55);
        const double v = dot(fr.vel, fr.fwd), speed = 1.3;
        c.throttle = clampd(0.35 + (speed - v) * 0.9, 0, 1);
        c.brake = v > speed + 0.8 ? 0.3 : 0;
        c.handbrake = 0;
        if (std::fabs(x) < pb.lay.E) worstDrop = std::max(worstDrop, pb.o.y - fr.com.y);
        if (std::getenv("BRIDGE_DEBUG") && (int)(ctx.t * 2) != (int)((ctx.t - Ctx::kFrame) * 2))
            std::printf("%6.1f along %6.2f across %+.2f  y-deck %+.2f  v %.2f thr %.2f steer %+.2f rpm %.0f\n", ctx.t, x, dot(fr.com - pb.o, pb.lf) - pb.lay.lineZ, fr.com.y - pb.o.y, v, c.throttle, c.steer, c.rpm);
    });
    seconds = ctx.t - t0;
    int popped = 0;
    for (const Wheel& wh : c.wheels) popped += wh.popped;
    if (popped) ctx.note(std::string(spec.name) + ": " + std::to_string(popped) + " tires burst");
    const bool upright = c.frame().up.y > 0.7 && popped == 0;
    ctx.sim.cars.clear();
    return ok && upright;
}
}  // namespace

SCENARIO(procModelsBridgeDrive, "procmodels/bridge-drive", "A stock and a lifted Tacoma each crawl across a rough bridge along its drivable line and off the far end") {
    double gx = 0, gz = 0, len = 0;
    const uint64_t seed = findBridge(true, gx, gz, len);
    ctx.check("found a worn bridge", seed != 0);
    if (!seed) return;
    ctx.sim.world = windowRound(seed, gx, gz, true);
    const double lx = gx - (double)ctx.sim.world.proc.ox, lz = gz - (double)ctx.sim.world.proc.oz;
    std::vector<PlacedBridge> all;
    const PlacedBridge* pb = nullptr;
    for (const Placed& pl : ctx.sim.world.models)
        if (pl.id == "timber_bridge") all.push_back(placedBridge(pl));
    for (const PlacedBridge& b : all)
        if (std::hypot(b.o.x - lx, b.o.z - lz) < 1) pb = &b;
    ctx.check("its model is in the window", pb && !pb->lay.fair);
    if (!pb || pb->lay.fair) return;
    char b[160];
    std::snprintf(b, sizeof b, "seed %llu, bridge at %.1f,%.1f, deck %.2f m: span %.1f m, wear %.2f, %zu plank pieces, line at %+.2f m", (unsigned long long)seed, lx, lz, pb->o.y, len, pb->lay.damage, pb->lay.deck.size(), pb->lay.lineZ);
    ctx.note(b);
    VehicleSpec lifted = vehicleCatalog()[6];
    raiseBody(lifted, 0.12);
    lifted.name = "Tacoma (lifted)";
    for (const VehicleSpec* spec : {&vehicleCatalog()[6], (const VehicleSpec*)&lifted}) {
        double drop = 0, secs = 0;
        const bool ok = crossBridge(ctx, *spec, *pb, drop, secs);
        ctx.check(std::string(spec->name) + " crosses", ok, std::to_string(secs) + " s");
        ctx.metric(std::string(spec->name) + ": how far its middle came down below the deck's top", drop, "m", -2, 0.2);
    }
}
