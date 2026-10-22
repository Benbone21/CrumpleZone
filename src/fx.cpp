#include "fx.h"

#include <algorithm>
#include <cmath>

#include "render.h"

namespace {
const size_t kMaxParticles = 6000;
// Tire tracks and skid marks: a segment every ~20 cm (a ring, markLimits() says how many), each
// fading out by its age, its place in the ring and its distance from the camera.
const double kRutWallDist = 70;  // the ground shoved up beside them is drawn this far (the decals: markLimits)

// Fx vertex layout: normal.xy = billboard uv (-1..1), normal.z = shading mode.
// Tracks (FX_TRACK): normal.x = across the mark (-1..1), normal.y = along it (m, the tire's own
// travel: its tread scrolls with the wheel), normal.z = 3 + the look + how deep the groove is (0..1 =
// 0..15 cm). The look is the TireType + 7 * (smear 0..3 + 4 * reversing); rubber marks are FX_RUBBER.
constexpr float kFxFlame = 1.2f;  // (glow mode 1 with a round shape: render.cpp's fx shader)
enum FxMode { FX_SOFT = 0, FX_GLOW = 1, FX_FLAT = 2, FX_TRACK = 3, FX_RUBBER = 100 };
float treadLook(int type, double spin, double groundSpeed, double omegaR, bool reverse) {
    const double r = std::fabs(spin) / std::max(std::max(std::fabs(groundSpeed), std::fabs(omegaR)), 1.5);  // spinning or locked: smeared
    const int smear = r < 0.12 ? 0 : r < 0.3 ? 1 : r < 0.55 ? 2 : 3;
    return (float)(std::min(std::max(type, 0), (int)TIRE_TYPES - 1) + 7 * (smear + 4 * (reverse ? 1 : 0)));
}

// The lit shader's terrain material (the ground's own paint at that spot, no road).
Vtx groundVtx(const V3& p, const V3& n) {
    return Vtx{{(float)p.x, (float)p.y, (float)p.z}, {(float)n.x, (float)n.y, (float)n.z}, {0, 50, 0, 7}};
}
// A raised lip of shoved-aside ground between `in` (at the groove's edge) and `out`
// (back down to the ground), its crest `up` high: two strips of quads along a0 -> a1.
// `dir` points away from the groove at each end.
void lipQuads(std::vector<Vtx>& out, const V3 in[2], const V3 crest[2], const V3 outer[2], const V3 n[2], const V3 dir[2],
              const float h[2], const float wIn[2], const float wOut[2]) {
    V3 nIn[2], nOut[2];
    for (int e = 0; e < 2; e++) {
        nIn[e] = norm(n[e] * wIn[e] - dir[e] * h[e]);
        nOut[e] = norm(n[e] * wOut[e] + dir[e] * h[e]);
    }
    const Vtx q1[6] = {groundVtx(in[0], nIn[0]), groundVtx(in[1], nIn[1]), groundVtx(crest[1], n[1]),
                       groundVtx(in[0], nIn[0]), groundVtx(crest[1], n[1]), groundVtx(crest[0], n[0])};
    const Vtx q2[6] = {groundVtx(crest[0], n[0]), groundVtx(crest[1], n[1]), groundVtx(outer[1], nOut[1]),
                       groundVtx(crest[0], n[0]), groundVtx(outer[1], nOut[1]), groundVtx(outer[0], nOut[0])};
    out.insert(out.end(), q1, q1 + 6);
    out.insert(out.end(), q2, q2 + 6);
}

// How a track darkens and tints the ground it's pressed into, and how strongly.
void trackLook(Surface s, float tint[3], float& alpha) {
    auto set = [&](float r, float g, float b, float a) { tint[0] = r, tint[1] = g, tint[2] = b, alpha = a; };
    switch (s) {
        case SURF_SAND: set(0.80f, 0.74f, 0.66f, 0.95f); break;   // damp sand turned up from under the dry crust
        case SURF_MUD: set(0.55f, 0.50f, 0.46f, 0.95f); break;    // wet, smeared
        case SURF_SNOW: set(0.70f, 0.77f, 0.92f, 0.95f); break;   // packed, blue in its own shade
        case SURF_DIRT: set(0.74f, 0.68f, 0.62f, 0.7f); break;
        case SURF_GRASS: set(0.80f, 0.86f, 0.62f, 0.55f); break;  // flattened, paler
        case SURF_GRAVEL:
        case SURF_SCREE: set(0.80f, 0.78f, 0.75f, 0.6f); break;   // stones pushed aside
        default: set(0.85f, 0.85f, 0.85f, 0.5f); break;
    }
}

Vtx fxVtx(const V3& p, float u, float v, float mode, const float c[4]) {
    return Vtx{{(float)p.x, (float)p.y, (float)p.z}, {u, v, mode}, {c[0], c[1], c[2], c[3]}};
}
}  // namespace

void Effects::clear() {
    particles.clear();
    skids.clear();
    skidHead = skidCount = 0;
    clock_ = 0;
    tracks.clear();
    trackOwner.clear();
}

void Effects::emit(Kind kind, const V3& p, const V3& v, float life, float s0, float s1, float r, float g, float b,
                   float a) {
    if (particles.size() >= kMaxParticles) return;
    Particle pt;
    pt.p = p;
    pt.v = v;
    pt.life = life;
    pt.size0 = s0;
    pt.size1 = s1;
    pt.col[0] = r;
    pt.col[1] = g;
    pt.col[2] = b;
    pt.col[3] = a;
    pt.kind = kind;
    particles.push_back(pt);
}

MarkLimits markLimits(int preset) {
    switch (preset) {
        case 4: return {1500, 20.0f, 80.0f};    // POTATO
        case 0: return {4000, 40.0f, 150.0f};   // LOW
        case 2: return {12000, 75.0f, 300.0f};  // HIGH
        case 3: return {kMaxMarkCap, 90.0f, 400.0f};  // ULTRA
        default: return {8000, 60.0f, 220.0f};  // MEDIUM, CUSTOM
    }
}

void Effects::setLimits(const MarkLimits& m) {
    lim_ = m;
    lim_.cap = std::min(std::max<size_t>(lim_.cap, 1), kMaxMarkCap);
    while (skidCount > lim_.cap) skidHead = (skidHead + 1) % skids.size(), skidCount--;
}

MarkStats Effects::markStats() const {
    MarkStats s;
    s.marks = skidCount, s.drawn = lastDrawn_, s.bytes = skids.capacity() * sizeof(Rut);
    return s;
}

Effects::MarkInfo Effects::markInfo(size_t i) const {
    const Rut& r = markAt(i);
    return {r.p[0], len(r.side[0]) * 2, r.kind == RUT_RUBBER, clock_ - r.born};
}

void Effects::push(const Rut& r) {
    if (skids.empty()) skids.resize(kMaxMarkCap);
    if (skidCount >= lim_.cap) skidHead = (skidHead + 1) % skids.size(), skidCount--;  // the oldest go
    Rut& d = skids[(skidHead + skidCount) % skids.size()];
    d = r;
    d.born = (float)clock_;
    skidCount++;
}

float Effects::markFade(size_t i, const Rut& r, const V3& eye, double far) const {
    // By ring place (the oldest fade before they're overwritten: the last 20%), age, and distance.
    const double left = (double)(lim_.cap - skidCount + i + 1);
    const double ring = std::min(1.0, left / (lim_.cap * 0.2));
    const double age = clock_ - r.born;
    const double old = smoothstep(lim_.life, lim_.life * 0.7, age);
    const V3 e = r.p[0] - eye;
    const double d = smoothstep(far, far * 0.7, std::sqrt(dot(e, e)));
    return (float)(ring * old * d);
}

void Effects::addSkid(const World& world, Track& t, RutKind kind, const V3& p, const V3& n, double halfWidth, double depth,
                      double lip, const float tint[3], float alpha, float look) {
    if (!t.active) {
        t.active = true;
        t.p = p, t.n = n, t.side = V3();
        t.along = 0, t.depth = (float)depth, t.lip = 0, t.segs = 0, t.roll = 0;
        return;
    }
    const V3 d = p - t.p;
    const double dl = len(d);
    if (dl > 3.0) {  // teleported or airborne for a while: restart the strip
        t.active = false;
        addSkid(world, t, kind, p, n, halfWidth, depth, lip, tint, alpha, look);
        return;
    }
    if (dl < 0.2) return;
    const V3 side = norm(cross(n, d)) * halfWidth;
    V3 s0 = t.side;
    if (len(s0) < 1e-6) s0 = side;           // first segment: square start
    else if (dot(s0, side) < 0) s0 = -s0;    // backing up over it (the mark is the same both ways)
    if (t.along >= 64) t.along -= 64;        // (the tread print repeats every few cm: 64 m is a whole number of them)
    // The print advances by what the tire turned (a locked wheel prints one smear, a spinning one a bunched-up one).
    const float adv = kind == RUT_RUBBER ? (float)dl : std::fabs(t.roll);
    t.roll = 0;
    // The lips rise over the first few segments of a strip, so its end isn't a cut-off wall.
    const float lip1 = (float)(lip * std::min(1.0, (t.segs + 1) / 3.0));
    // A long stretch (fast driving) is laid in ~30 cm pieces, each put back on the terrain, so the
    // decal hugs the slopes and bumps between two contact samples.
    const int K = std::min(8, std::max(1, (int)std::ceil(dl / 0.3)));
    const double h0 = world.height(t.p.x, t.p.z), h1 = world.height(p.x, p.z);
    const double o0 = t.p.y - h0, o1 = p.y - h1;
    auto at = [&](double f) {
        V3 q = t.p + d * f;
        if (f > 0 && f < 1) q.y = world.height(q.x, q.z) + o0 + (o1 - o0) * f;
        return q;
    };
    for (int j = 0; j < K; j++) {
        const double f0 = (double)j / K, f1 = (double)(j + 1) / K;
        Rut r;
        r.p[0] = at(f0), r.p[1] = at(f1);
        r.side[0] = s0 * (1 - f0) + side * f0, r.side[1] = s0 * (1 - f1) + side * f1;
        r.n[0] = norm(t.n * (1 - f0) + n * f0), r.n[1] = norm(t.n * (1 - f1) + n * f1);
        r.along[0] = t.along + (float)(adv * f0), r.along[1] = t.along + (float)(adv * f1);
        r.depth[0] = (float)(t.depth + (depth - t.depth) * f0), r.depth[1] = (float)(t.depth + (depth - t.depth) * f1);
        r.lip[0] = (float)(t.lip + (lip1 - t.lip) * f0), r.lip[1] = (float)(t.lip + (lip1 - t.lip) * f1);
        for (int k = 0; k < 3; k++) r.tint[k] = tint[k];
        r.alpha = alpha;
        r.kind = kind;
        r.look = look;
        push(r);
    }
    t.p = p, t.n = n, t.side = side;
    t.along += adv, t.depth = (float)depth, t.lip = lip1;
    t.segs++;
}

FxEvents Effects::update(Sim& sim, double dt) {
    FxEvents ev;
    const World& world = sim.world;
    if (dt > 0) {
        clock_ += dt;
        // (marks are laid in order: the expired ones are the oldest)
        while (skidCount && clock_ - skids[skidHead].born > lim_.life) skidHead = (skidHead + 1) % skids.size(), skidCount--;
    }

    // Track state follows cars by identity; rebuild when cars are added or removed.
    if (trackOwner.size() != sim.cars.size()) {
        tracks.assign(sim.cars.size(), {});
        trackOwner.assign(sim.cars.size(), nullptr);
    }

    for (size_t ci = 0; ci < sim.cars.size(); ci++) {
        Car& car = sim.cars[ci];
        if (trackOwner[ci] != &car || tracks[ci].size() != car.wheels.size()) {
            tracks[ci].assign(car.wheels.size(), Track());
            trackOwner[ci] = &car;
        }

        // Events the physics queued up.
        for (const V3& p : car.glassBursts) {
            ev.glass++;
            Frame fr = car.frame();
            for (int k = 0; k < 22; k++) {
                V3 v = fr.vel * 0.7 + V3(randS(), rand01() * 0.8 + 0.2, randS()) * 3.0;
                float s = 0.025f + 0.03f * rand01();
                float tint = 0.75f + 0.25f * rand01();
                emit(SHARD, p + V3(randS(), randS(), randS()) * 0.15, v, 5.0f + 3 * rand01(), s, s, 0.75f * tint,
                     0.88f * tint, 0.95f * tint, 0.9f);
            }
        }
        car.glassBursts.clear();
        if (car.tirePops > 0) {
            ev.tirePops += car.tirePops;
            for (const Wheel& wh : car.wheels)
                if (wh.popped)
                    for (int k = 0; k < 12; k++)
                        emit(SMOKE, wh.contact + V3(0, 0.1, 0), V3(randS(), rand01() * 1.5, randS()) * 1.5, 1.2f,
                             0.2f, 1.2f, 0.7f, 0.7f, 0.7f, 0.5f);
            car.tirePops = 0;
        }
        if (dt <= 0 || car.sleeping) continue;

        // Tires: smoke on pavement, dust on grass, roost, tracks and skid marks.
        for (size_t wi = 0; wi < car.wheels.size(); wi++) {
            const Wheel& wh = car.wheels[wi];
            Track& tr = tracks[ci][wi];
            if (car.remote && !wh.detached) {
                // Another player's car: its tires have no contact state here, so find where
                // they meet the ground (sunk about as far as they're squashed past ~3 cm).
                const Node& a = car.nodes[wh.inner];
                const Node& b = car.nodes[wh.outer];
                const V3 c = (a.p + b.p) * 0.5;
                V3 ax = norm(b.p - a.p);
                if (!wh.left) ax = -ax;
                Contact ct;
                if (!world.contact(c, wh.R, &ax, wh.width * 0.5, ct) || ct.pen < 0.004) {
                    tr.active = false;
                    continue;
                }
                const V3 ground = c - ct.n * (wh.R - ct.pen);
                const V3 rv = (a.v + b.v) * 0.5;
                const double sp = len(rv);
                tr.roll += (float)(dot(rv, norm(cross(ax, ct.n))) * dt);  // (no wheel speed sent: it rolls with the car)
                const int type = car.spec ? car.spec->tireType : (int)TIRE_ALL_TERRAIN;
                const float look = treadLook(type, 0, sp, sp, dot(rv, norm(cross(ax, ct.n))) < -0.3);
                if (ct.surface == SURF_PAVED || ct.surface == SURF_WET || ct.surface == SURF_ICE || ct.surface == SURF_ROCK) {
                    // Hard ground: a skid when the car is going sideways over its tires.
                    const double slide = std::fabs(dot(rv, ax));
                    if (sp > 6 && slide > 0.25 * sp) {
                        const float alpha = (float)clampd(slide / sp * 1.1, 0.15, 0.6);
                        const float g = ct.surface == SURF_PAVED || ct.surface == SURF_ROCK ? 0.22f : 0.5f;
                        const float rubber[3] = {g, g, g * 1.03f};
                        addSkid(world, tr, RUT_RUBBER, ground, ct.n, wh.width * 0.5, 0, 0, rubber, alpha, look);
                    } else {
                        tr.active = false;
                    }
                    continue;
                }
                const SurfaceInfo& si = surfaceInfo(ct.surface);
                const double sink = std::min(si.sink, std::max(0.0, ct.pen - 0.03));
                float tint[3], alpha;
                trackLook(ct.surface, tint, alpha);
                addSkid(world, tr, RUT_GROOVE, ground, ct.n, wh.width * 0.5, si.sink > 0 ? sink + 0.01 : 0.008,
                        si.sink > 0 ? std::min(0.12, 0.9 * sink + 0.008) : 0.0, tint, alpha, look);
                continue;
            }
            if (wh.load < 300 || wh.detached) {  // in the air, or barely touching: no mark
                tr.active = false;
                continue;
            }
            tr.roll += (float)(wh.omega * wh.R * dt);
            const Node& a = car.nodes[wh.inner];
            const Node& b = car.nodes[wh.outer];
            V3 vel = (a.v + b.v) * 0.5;
            double speed = len(vel);
            const SurfaceInfo& si = surfaceInfo(wh.surface);
            const bool hard = wh.surface == SURF_PAVED || wh.surface == SURF_WET || wh.surface == SURF_ICE || wh.surface == SURF_ROCK;
            const bool onPad = wh.surface == SURF_PAVED;
            const float* dc = si.dust;
            double slide = wh.slip * std::max(speed, std::fabs(wh.omega * wh.R));
            if (wh.slip > 0.3 && slide > 2.0 && wh.surface != SURF_WET && wh.surface != SURF_ICE) {
                int n = count(std::min(80.0, (wh.slip - 0.3) * 90.0 + slide * 1.5), dt);
                for (int k = 0; k < n; k++) {
                    V3 p = wh.contact + V3(randS(), 0.1 + rand01() * 0.1, randS()) * 0.15;
                    V3 v = vel * 0.25 + V3(randS() * 0.6, 0.4 + rand01() * 0.6, randS() * 0.6);
                    if (onPad || wh.surface == SURF_ROCK)
                        emit(SMOKE, p, v, 2.2f + rand01(), 0.25f, 1.8f + rand01(), 0.86f, 0.86f, 0.88f, 0.28f);
                    else if (wh.surface != SURF_MUD)  // (mud doesn't make dust)
                        emit(DUST, p, v, 1.6f + rand01(), 0.2f, 1.4f, dc[0], dc[1], dc[2], wh.surface == SURF_SNOW ? 0.5f : 0.35f);
                }
            } else if (!hard && wh.surface != SURF_MUD && speed > 8) {  // driving on loose ground kicks up a little dust anyway
                int n = count(speed * 0.4, dt);
                for (int k = 0; k < n; k++)
                    emit(DUST, wh.contact + V3(0, 0.1, 0), vel * 0.1 + V3(randS(), rand01(), randS()) * 0.5, 1.2f,
                         0.2f, 1.0f, dc[0], dc[1], dc[2], 0.18f);
            }
            // Roost: a tire spinning on loose ground throws what it's standing on out behind it.
            const double spin = wh.omega * wh.R - wh.groundSpeed;
            if (!hard && std::fabs(spin) > 2.0) {
                V3 ax = norm(b.p - a.p);
                if (!wh.left) ax = -ax;
                const V3 fdir = norm(cross(ax, wh.contactNormal));
                const double back = spin > 0 ? -1 : 1;
                int n = count(std::min(120.0, (std::fabs(spin) - 2.0) * 25.0), dt);
                for (int k = 0; k < n; k++) {
                    const double sp = std::fabs(spin) * (0.35 + 0.4 * rand01());
                    V3 v = vel + fdir * (back * sp) + wh.contactNormal * (1.0 + sp * 0.45 * rand01()) + ax * (randS() * 1.2);
                    const float shade = 0.75f + 0.35f * (float)rand01();
                    emit(CLOD, wh.contact + wh.contactNormal * 0.12 + fdir * (back * wh.R * 0.6), v, 0.8f + 0.6f * (float)rand01(),
                         wh.surface == SURF_GRAVEL || wh.surface == SURF_SCREE ? 0.035f : 0.05f, 0.04f, dc[0] * shade, dc[1] * shade,
                         dc[2] * shade, 0.95f);
                }
            }
            // Spray off wet asphalt at speed.
            if (wh.surface == SURF_WET && speed > 12) {
                int n = count(speed * 1.2, dt);
                for (int k = 0; k < n; k++)
                    emit(SMOKE, wh.contact + V3(randS() * 0.15, 0.15, randS() * 0.15), vel * 0.35 + V3(randS(), 0.5 + rand01(), randS()) * 0.8,
                         0.9f, 0.15f, 1.0f, 0.82f, 0.85f, 0.88f, 0.16f);
            }
            // Wading: a bow wave of spray off a tire moving through water (brown over a mud bed).
            if (world.hasWater() && speed > 1.5) {
                const double wl = world.waterLevel(wh.contact.x, wh.contact.z);
                if (wl > wh.contact.y + 0.02 && wl < wh.contact.y + wh.R * 1.6) {
                    const bool murky = wh.surface == SURF_MUD;
                    const float c[3] = {murky ? 0.42f : 0.82f, murky ? 0.31f : 0.88f, murky ? 0.2f : 0.92f};
                    int n = count(std::min(60.0, speed * 3.0), dt);
                    for (int k = 0; k < n; k++)
                        emit(SMOKE, V3(wh.contact.x + randS() * 0.2, wl + 0.03, wh.contact.z + randS() * 0.2),
                             vel * 0.2 + V3(randS() * 0.8, 0.6 + rand01() * (0.4 + speed * 0.15), randS() * 0.8), 0.7f + 0.4f * (float)rand01(),
                             0.08f, 0.5f, c[0], c[1], c[2], 0.3f);
                }
            }
            // Tracks. On loose ground every tire leaves one: a groove the width of the tread,
            // as deep as it sank (with the ground shoved up either side in sand, mud and
            // snow), darker where it spun. On hard ground, black rubber where it slid.
            const int tread = wh.chained ? (int)TIRE_CHAINS : car.spec ? car.spec->tireType : (int)TIRE_ALL_TERRAIN;
            const float look = treadLook(tread, wh.omega * wh.R - wh.groundSpeed, wh.groundSpeed, wh.omega * wh.R, wh.omega < -0.5);
            const V3 ground = wh.contact + wh.contactNormal * wh.sink;  // (the contact is the tire's bottom, down in it)
            if (hard) {
                if (wh.slip > 0.22 && wh.load > 800) {
                    const float alpha = (float)clampd((wh.slip - 0.15) * 1.6, 0.15, 0.6);
                    const float g = onPad || wh.surface == SURF_ROCK ? 0.22f : 0.5f;
                    const float rubber[3] = {g, g, g * 1.03f};
                    const float a2 = alpha * (float)(0.7 + 0.3 * clampd(wh.load / 4000, 0, 1));
                    addSkid(world, tr, RUT_RUBBER, ground, wh.contactNormal, wh.width * 0.5, 0, 0, rubber, a2, look);
                } else {
                    tr.active = false;
                }
            } else {
                float tint[3], alpha;
                trackLook(wh.surface, tint, alpha);
                const double dug = clampd((std::fabs(spin) - 1.0) / 6.0, 0, 1);  // spinning churns it up
                for (float& c : tint) c *= (float)(1 - 0.18 * dug);
                const bool soft = si.sink > 0;
                const double depth = soft ? wh.sink + 0.01 + 0.02 * dug : wh.surface == SURF_GRAVEL || wh.surface == SURF_SCREE ? 0.012 : 0.008;
                const double lip = soft ? std::min(0.12, 0.9 * wh.sink + 0.008) : 0.0;
                // (heavier on it: darker)
                alpha *= (float)(0.65 + 0.35 * clampd(wh.load / 3500, 0, 1));
                addSkid(world, tr, RUT_GROOVE, ground, wh.contactNormal, wh.width * 0.5, depth, lip, tint, alpha, look);
            }
        }

        // Sparks from body nodes scraping the ground.
        for (int i = 0; i < car.bodyNodes; i++) {
            const Node& n = car.nodes[i];
            if (n.scrape < 3.0f) continue;
            int k = count(n.scrape * 1.2, dt);
            for (int s = 0; s < k; s++) {
                V3 v = n.v * 0.6 + V3(randS() * 2, 1.0 + rand01() * 2.5, randS() * 2);
                emit(SPARK, n.p, v, 0.25f + 0.35f * rand01(), 0.05f, 0.03f, 1.0f, 0.75f + 0.2f * rand01(), 0.35f, 1.0f);
            }
        }

        // Smoke from a damaged engine bay.
        if (car.engineHealth < 0.65) {
            double sev = 1.0 - car.engineHealth;
            int n = count(sev * (car.engineDead ? 30.0 : 14.0), dt);
            Frame fr = car.frame();
            V3 bay = car.engineBay() + fr.up * 0.45;
            float g = car.engineDead ? 0.18f : 0.45f;
            for (int k = 0; k < n; k++)
                emit(SMOKE, bay + V3(randS(), 0, randS()) * 0.3, fr.vel * 0.5 + V3(randS() * 0.3, 0.8 + rand01() * 0.6, randS() * 0.3),
                     2.5f + rand01() * 1.5f, 0.3f, 2.0f, g, g, g + 0.02f, 0.35f);
        }
    }

    // Integrate.
    if (dt > 0) {
        for (Particle& p : particles) {
            p.age += (float)dt;
            switch (p.kind) {
                case SMOKE:
                case DUST:
                    p.v *= std::exp(-1.5 * dt);
                    p.v.y += 0.35 * dt;  // warm smoke rises a little
                    break;
                case SPARK:
                    p.v.y -= 9.81 * dt;
                    break;
                case FLAME:
                    p.v *= std::exp(-1.2 * dt);
                    p.v.y += 2.2 * dt;  // a flame lifts and thins
                    break;
                case SHARD:
                case CLOD:
                    p.v.y -= 9.81 * dt;
                    break;
            }
            p.p += p.v * dt;
            if (p.kind == SHARD || p.kind == SPARK || p.kind == CLOD) {
                double gy = world.height(p.p.x, p.p.z);
                if (p.p.y < gy + 0.01) {
                    p.p.y = gy + 0.01;
                    if (p.kind == SPARK || p.kind == CLOD) p.age = p.life;
                    p.v = V3(p.v.x * 0.4, std::fabs(p.v.y) * 0.25, p.v.z * 0.4);
                }
            }
        }
        particles.erase(std::remove_if(particles.begin(), particles.end(),
                                       [](const Particle& p) { return p.age >= p.life; }),
                        particles.end());
    }
    return ev;
}

void Effects::campfire(const V3& base, double dt, const V3& wind) {
    // Flames: a few tongues a frame, rising from a ring of about 0.25 m over the logs.
    for (int k = count(46, dt); k > 0; k--) {
        const double a = rand01() * 6.2831853, r = 0.28 * std::sqrt(rand01());
        emit(FLAME, base + V3(std::cos(a) * r, 0.22 + rand01() * 0.1, std::sin(a) * r), V3(randS() * 0.18, 0.7 + rand01() * 0.9, randS() * 0.18) + wind * 0.3,
             0.5f + rand01() * 0.35f, 0.2f + rand01() * 0.08f, 0.05f, 1.0f, 0.78f, 0.34f, 0.85f);
    }
    // Smoke: grey puffs that grow and lean downwind.
    for (int k = count(5, dt); k > 0; k--)
        emit(SMOKE, base + V3(randS() * 0.2, 0.9, randS() * 0.2), V3(randS() * 0.15, 0.9 + rand01() * 0.4, randS() * 0.15) + wind,
             5.0f + rand01() * 3.0f, 0.18f, 1.6f, 0.42f, 0.4f, 0.38f, 0.22f);
    // Sparks: now and then one, thrown up and across.
    for (int k = count(2.2, dt); k > 0; k--)
        emit(SPARK, base + V3(randS() * 0.15, 0.7, randS() * 0.15), V3(randS() * 0.8, 1.6 + rand01() * 2.2, randS() * 0.8) + wind * 0.5, 0.7f + rand01() * 1.0f,
             0.018f, 0.01f, 1.0f, 0.55f, 0.16f, 1.0f);
}

void Effects::translate(const V3& d) {
    for (Particle& p : particles) p.p += d;
    for (size_t i = 0; i < skidCount; i++) {
        Rut& r = skids[(skidHead + i) % skids.size()];
        r.p[0] += d, r.p[1] += d;
    }
    for (auto& car : tracks)
        for (Track& t : car) t.p += d;
}

void Effects::buildSkids(const V3& eye, std::vector<Vtx>& out) const {
    const size_t v0 = out.size();
    const double far2 = lim_.fadeDist * lim_.fadeDist;
    for (size_t i = 0; i < skidCount; i++) {
        const Rut& r = markAt(i);
        const V3 e = r.p[0] - eye;
        if (dot(e, e) > far2) continue;
        const float c[4] = {r.tint[0], r.tint[1], r.tint[2], r.alpha * markFade(i, r, eye, lim_.fadeDist)};
        if (c[3] < 0.01f) continue;
        Vtx v[4];  // left and right at each end
        for (int k = 0; k < 2; k++) {
            const V3 lift = r.n[k] * 0.01;
            const float mode = r.kind == RUT_RUBBER ? (float)FX_RUBBER : FX_TRACK + r.look + std::min(0.98f, r.depth[k] / 0.15f);
            v[k * 2] = fxVtx(r.p[k] + r.side[k] + lift, 1, r.along[k], mode, c);
            v[k * 2 + 1] = fxVtx(r.p[k] - r.side[k] + lift, -1, r.along[k], mode, c);
        }
        const int order[6] = {0, 2, 3, 0, 3, 1};
        for (int k : order) out.push_back(v[k]);
    }
    lastDrawn_ = (out.size() - v0) / 6;
}

void Effects::buildRutWalls(const Sim& sim, const V3& eye, std::vector<Vtx>& out) const {
    // Lips along the fresh grooves.
    const double far2 = kRutWallDist * kRutWallDist;
    for (size_t i = 0; i < skidCount; i++) {
        const Rut& r = markAt(i);
        if (r.kind != RUT_GROOVE || std::max(r.lip[0], r.lip[1]) < 0.004f) continue;
        const V3 e = r.p[0] - eye;
        if (dot(e, e) > far2) continue;
        const float fade = markFade(i, r, eye, kRutWallDist);
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            V3 in[2], crest[2], outer[2], dir[2];
            float h[2], wIn[2], wOut[2];
            for (int k = 0; k < 2; k++) {
                const double hw = std::max(1e-3, len(r.side[k]));
                dir[k] = r.side[k] * (sgn / hw);
                h[k] = r.lip[k] * fade;
                wIn[k] = 0.02f + 0.45f * h[k];
                wOut[k] = 0.06f + 2.5f * h[k];
                in[k] = r.p[k] + dir[k] * hw + r.n[k] * 0.002;
                crest[k] = r.p[k] + dir[k] * (hw + wIn[k]) + r.n[k] * h[k];
                outer[k] = r.p[k] + dir[k] * (hw + wIn[k] + wOut[k]) - r.n[k] * 0.01;
            }
            lipQuads(out, in, crest, outer, r.n, dir, h, wIn, wOut);
        }
    }
    // A collar round every tire sunk into sand, mud or snow: the ground it pushed aside,
    // banked up round the sides (and the front, rolling: behind it is the groove).
    const double collar2 = 90.0 * 90.0;
    for (const Car& car : sim.cars) {
        if (car.wheels.empty()) continue;
        const V3 ce = car.frame().com - eye;
        if (dot(ce, ce) > collar2) continue;
        for (const Wheel& wh : car.wheels) {
            if (wh.detached) continue;
            const Node& a = car.nodes[wh.inner];
            const Node& b = car.nodes[wh.outer];
            const V3 c = (a.p + b.p) * 0.5;
            V3 ax = norm(b.p - a.p);
            V3 n, g;
            double sink;
            const double R = wh.rollR();
            if (car.remote) {
                Contact ct;
                if (!sim.world.contact(c, R, &ax, wh.width * 0.5, ct) || surfaceInfo(ct.surface).sink <= 0) continue;
                n = ct.n;
                g = c - n * (R - ct.pen);
                sink = std::min(surfaceInfo(ct.surface).sink, std::max(0.0, ct.pen - 0.03));
            } else {
                if (wh.load <= 0 || surfaceInfo(wh.surface).sink <= 0) continue;
                n = wh.contactNormal;
                g = wh.contact + n * wh.sink;
                sink = wh.sink;
            }
            const double down = R - dot(c - g, n);  // how far the tire's bottom is under the ground
            const double h0 = std::min(0.12, 0.9 * sink + 0.008);
            if (down < 0.005 || h0 < 0.004) continue;
            V3 fwd = cross(ax, n);
            if (len(fwd) < 0.3) continue;
            fwd = norm(fwd);
            const V3 side = norm(cross(n, fwd));
            const V3 vel = (a.v + b.v) * 0.5;
            const double vf = dot(vel, fwd);
            if (vf < 0) fwd = -fwd;  // (the way it's rolling)
            const double rolling = smoothstep(0.3, 1.5, std::fabs(vf));
            const double L = std::sqrt(std::max(0.0, 2 * R * down - down * down)) + 0.02, W = wh.width * 0.5 + 0.015;
            const int K = 28;
            V3 in[2], crest[2], outer[2], dir[2], nn[2] = {n, n};
            float h[2], wIn[2], wOut[2];
            for (int k = 0; k <= K; k++) {
                const double th = 2 * M_PI * k / K, ct = std::cos(th), st = std::sin(th);
                const V3 d = fwd * ct + side * st;
                const double rIn = 1.0 / std::sqrt((ct * ct) / (L * L) + (st * st) / (W * W));
                const double hk = h0 * (1 - rolling * smoothstep(-0.2, -0.75, ct)) * (1 + 0.5 * rolling * std::max(0.0, ct));
                const int e = k & 1;
                dir[e] = d;
                h[e] = (float)hk;
                wIn[e] = 0.02f + 0.45f * h[e];
                wOut[e] = 0.06f + 2.5f * h[e];
                in[e] = g + d * rIn + n * 0.002;
                crest[e] = g + d * (rIn + wIn[e]) + n * hk;
                outer[e] = g + d * (rIn + wIn[e] + wOut[e]) - n * 0.01;
                if (k == 0) continue;
                // (ends 0/1 alternate: put the previous one first)
                const int p = e ^ 1;
                const V3 in2[2] = {in[p], in[e]}, cr2[2] = {crest[p], crest[e]}, ou2[2] = {outer[p], outer[e]}, di2[2] = {dir[p], dir[e]};
                const float h2[2] = {h[p], h[e]}, wi2[2] = {wIn[p], wIn[e]}, wo2[2] = {wOut[p], wOut[e]};
                lipQuads(out, in2, cr2, ou2, nn, di2, h2, wi2, wo2);
            }
        }
    }
}

void Effects::buildParticles(const V3& eye, std::vector<Vtx>& out) {
    // Back-to-front so the soft smoke blends correctly.
    std::sort(particles.begin(), particles.end(), [&](const Particle& a, const Particle& b) {
        return dot(a.p - eye, a.p - eye) > dot(b.p - eye, b.p - eye);
    });
    for (const Particle& p : particles) {
        float t = p.age / p.life;
        float size = p.size0 + (p.size1 - p.size0) * t;
        float c[4] = {p.col[0], p.col[1], p.col[2], p.col[3]};
        V3 view = norm(p.p - eye);
        V3 right = norm(cross(view, V3(0, 1, 0)));
        if (len(right) < 0.5) right = V3(1, 0, 0);
        V3 up = cross(right, view);
        V3 q[4];
        float mode = FX_SOFT;
        if (p.kind == FLAME) {
            mode = kFxFlame;
            c[3] *= (1.0f - t) * std::min(1.0f, t * 10.0f + 0.3f);
            // (a flame cools as it rises: yellow-white at the logs, orange, then red)
            c[1] *= 1.0f - 0.45f * t, c[2] *= 1.0f - 0.8f * t;
            q[0] = p.p - right * size - up * size;
            q[1] = p.p + right * size - up * size;
            q[2] = p.p + right * size + up * size;
            q[3] = p.p - right * size + up * size;
        } else if (p.kind == SPARK) {
            // Streak along the velocity.
            mode = FX_GLOW;
            c[3] = 1.0f - t * 0.5f;
            V3 tail = p.v * 0.025;
            V3 side = norm(cross(view, p.v)) * size;
            if (len(side) < 1e-6) side = right * size;
            q[0] = p.p - side - tail;
            q[1] = p.p + side - tail;
            q[2] = p.p + side + tail;
            q[3] = p.p - side + tail;
        } else {
            if (p.kind == SHARD || p.kind == CLOD) {
                mode = FX_FLAT;
                c[3] *= std::min(1.0f, (1.0f - t) * 4.0f);
            } else {
                c[3] *= (1.0f - t) * std::min(1.0f, t * 8.0f + 0.2f);  // fade in fast, out slow
            }
            q[0] = p.p - right * size - up * size;
            q[1] = p.p + right * size - up * size;
            q[2] = p.p + right * size + up * size;
            q[3] = p.p - right * size + up * size;
        }
        const int order[6] = {0, 1, 2, 0, 2, 3};
        const float uv[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int k : order) out.push_back(fxVtx(q[k], uv[k][0], uv[k][1], mode, c));
    }
}
