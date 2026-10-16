// THROWAWAY passability probe: the set-up Blazer (ZZ4 crate 350, 4 in lift and 35s, 4.56 gears and lockers, 4L, lockers in, 12 psi)
// crawls the line each obstacle section reports (SectionBuilt::line) with a pure-pursuit driver, backing off and rushing it again when
// it stalls. Physics only; this is not a gameplay check. Scanned rock hulls are on, as in the game.
//
//   scenarios:  rp/info/<section>-<seed>            rock counts, line, build time
//               rp/pass/<section>-<seed>[-v<dm/s>]  one crawl: where it got to, stuck where, damage
//   sections:   hill mud river wall timber dunes shelf
//
// Build (from the repo root, after `make -j4 build/run_tests`; links only run_tests.o + harness.o, so only these scenarios exist):
//   OBJS=$(make -n -B build/run_tests 2>/dev/null | grep -o 'build/src/[^ ]*\.o' | sort -u)
//   g++ -O2 -march=native -std=c++17 -I tests -I src -c prototypes/sections/passability.cpp -o build/rp.o
//   g++ -O2 -o build/rp build/tests/run_tests.o build/tests/harness.o $OBJS build/rp.o -lpthread -lm
//   nice -n 19 ./build/rp --jobs 4 rp/pass/wall
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "harness.h"
#include "mods.h"
#include "props.h"
#include "sections.h"

namespace {

int catIdx(const std::string& id) {
    const auto& cat = vehicleCatalog();
    for (int i = 0; i < (int)cat.size(); i++)
        if (id == cat[i].id) return i;
    std::abort();
}
int optIdx(int v, int slot, const char* name) {
    const auto& o = modOptions(v, slot);
    for (int i = 0; i < (int)o.size(); i++)
        if (o[i].name == name) return i;
    std::fprintf(stderr, "no option %s\n", name);
    std::abort();
}
// The set-up Blazer (handoff 3.3).
const VehicleSpec& blazerSetup() {
    static VehicleSpec spec = [] {
        const int v = catIdx("blazer");
        ModSet m;
        m.pick[MOD_ENGINE] = optIdx(v, MOD_ENGINE, "ZZ4 CRATE 350");
        m.pick[MOD_SUSPENSION] = optIdx(v, MOD_SUSPENSION, "4 IN LIFT AND 35S");
        m.pick[MOD_TRANSMISSION] = optIdx(v, MOD_TRANSMISSION, "4.56 GEARS AND LOCKERS");
        return tunedSpec(v, m);
    }();
    return spec;
}

struct Which {
    const char* key;
    SectionType t;
};
const Which kWhich[] = {{"hill", SEC_STEEP_HILL}, {"mud", SEC_MUD_PIT}, {"river", SEC_ROCKY_RIVER}, {"wall", SEC_ROCK_WALL},
                        {"dunes", SEC_DUNES},     {"shelf", SEC_CLIFF_SHELF}, {"timber", SEC_TIMBER}};

struct Built {
    SectionFrame f;
    SectionBuilt b;
    double buildMs = 0;
};
Built stamp(Ctx& ctx, SectionType t, uint32_t seed, double heading = 0) {
    World& w = ctx.sim.world;
    Built r;
    const double L = sectionLength(t);
    r.f.heading = heading;
    r.f.entry = V3(-300 - std::cos(heading) * L / 2, 0, -300 + std::sin(heading) * L / 2);
    r.f.entry.y = w.height(r.f.entry.x, r.f.entry.z);
    props::ScopedRockScans on(props::sharedHullLibrary().loaded() ? &props::sharedHullLibrary() : nullptr);
    const auto t0 = std::chrono::steady_clock::now();
    r.b = buildSection(w, t, seed, r.f);
    w.buildGrid();
    if (r.b.line.empty()) {  // (a build from before the line was reported: straight down the middle; through the stairs for the wall)
        const double dxu = std::cos(heading), dzu = -std::sin(heading), pxv = std::sin(heading), pzv = std::cos(heading);
        std::vector<std::array<double, 3>> stairs;  // u0, u1, v of each notch's staircase (its slabs are 2.8 m wide)
        if (t == SEC_ROCK_WALL) {
            std::vector<std::array<double, 3>> sl;
            for (const Box& b : w.boxes) {
                const double u = (b.c.x - r.f.entry.x) * dxu + (b.c.z - r.f.entry.z) * dzu, v = (b.c.x - r.f.entry.x) * pxv + (b.c.z - r.f.entry.z) * pzv;
                if (b.rock && std::fabs(b.h.z - 1.4) < 1e-9 && u > 20 && u < L) sl.push_back({u - b.h.x, u + b.h.x, v});
            }
            std::sort(sl.begin(), sl.end());
            for (const auto& q : sl) {
                if (!stairs.empty() && q[0] < stairs.back()[1] + 1.5 && std::fabs(q[2] - stairs.back()[2]) < 0.2) stairs.back()[1] = std::max(stairs.back()[1], q[1]);
                else stairs.push_back(q);
            }
        }
        std::vector<std::pair<double, double>> A = {{0.0, 0.0}};
        for (const auto& st : stairs) A.push_back({st[0] - 3.2, st[2]}), A.push_back({st[1] + 2.2, st[2]});
        A.push_back({L, 0.0});
        for (size_t i = 1; i < A.size(); i++) A[i].first = std::max(A[i].first, A[i - 1].first + 0.5);
        for (double u = 0; u <= L; u += 2) {
            double v = 0;
            for (size_t i = 1; i < A.size(); i++)
                if (u <= A[i].first) {
                    v = A[i - 1].second + (A[i].second - A[i - 1].second) * smoothstep(A[i - 1].first, A[i].first, u);
                    break;
                }
            V3 p(r.f.entry.x + dxu * u + pxv * v, 0, r.f.entry.z + dzu * u + pzv * v);
            p.y = w.height(p.x, p.z);
            r.b.line.push_back(p);
        }
    }
    r.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

void info(Ctx& ctx, SectionType t, uint32_t seed) {
    World& w = ctx.sim.world;
    const Built s = stamp(ctx, t, seed);
    const SectionFootprint fp = sectionFootprint(t);
    auto inside = [&](double x, double z) {  // in the cleared ground
        const double u = (x - s.f.entry.x) * std::cos(s.f.heading) - (z - s.f.entry.z) * std::sin(s.f.heading), v = (x - s.f.entry.x) * std::sin(s.f.heading) + (z - s.f.entry.z) * std::cos(s.f.heading);
        return std::max({-fp.apron - u, u - (fp.length + fp.apron), std::fabs(v) - fp.half}) < 0;
    };
    int rocks = 0, boxes = 0, onLine = 0, within3 = 0, scans = 0;
    for (const Rock& rk : w.rocks) {
        if (!inside(rk.c.x, rk.c.z)) continue;
        double best = 1e9;
        for (const V3& p : s.b.line) best = std::min(best, std::hypot(p.x - rk.c.x, p.z - rk.c.z));
        rocks++, within3 += best < 3, onLine += best < 1.2, scans += rk.scan >= 0;
    }
    for (const Box& b : w.boxes) boxes += inside(b.c.x, b.c.z);
    std::printf("RP info %s seed %u: %d rocks (%d scanned, %d within 3 m of the line, %d within 1.2 m), %d boxes, build %.1f ms\n", sectionName(t), seed, rocks, scans, within3, onLine, boxes, s.buildMs);
    ctx.metric("x", 0, "", -1, 1);
}

// Index of the nearest line point to p, searching forward from `from`.
int nearest(const std::vector<V3>& line, const V3& p, int from) {
    int best = from;
    double bd = 1e18;
    for (int i = std::max(0, from - 4); i < (int)line.size() && i < from + 40; i++) {
        const double d = (line[i].x - p.x) * (line[i].x - p.x) + (line[i].z - p.z) * (line[i].z - p.z);
        if (d < bd) bd = d, best = i;
    }
    return best;
}

void pass(Ctx& ctx, SectionType t, uint32_t seed, double speed, double heading = 0) {
    World& w = ctx.sim.world;
    const Built s = stamp(ctx, t, seed, heading);
    const std::vector<V3>& line = s.b.line;
    if (const char* clear = std::getenv("RP_CLEAR")) {  // (debug: drop the rocks within RP_CLEAR m of the line, to see what the ground and slabs alone do)
        const double rad = std::atof(clear);
        auto& rk = w.rocks;
        rk.erase(std::remove_if(rk.begin(), rk.end(), [&](const Rock& r) {
                     for (const V3& q : line)
                         if (std::hypot(q.x - r.c.x, q.z - r.c.z) < rad + r.r * 0.5) return true;
                     return false;
                 }),
                 rk.end());
        w.buildGrid();
    }
    const V3 d0 = norm(line[1] - line[0]);
    const V3 start = line[0] - d0 * 10;
    const VehicleSpec& spec = blazerSetup();
    Car& c = ctx.add(spec, start.x, start.z, std::atan2(-d0.z, d0.x) * 180 / M_PI);
    ctx.focus(c);
    ctx.run(1.0);
    if (!driveModes(spec.transfer).empty()) c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    c.setLocker(true, true), c.setLocker(false, true);
    c.swayDisconnected = spec.swayDisconnect > 0;
    c.tirePsi = 12;
    c.tractionControl = false;
    int idx = 0, bestIdx = 0, tries = 1;
    double lastProgressT = ctx.t, backUntil = -1, v = speed;
    bool done = false;
    const double t0 = ctx.t;
    std::vector<std::pair<int, double>> stalls;  // (line index, time)
    ctx.runUntil(420, [&] { return done; }, [&] {
        const Frame fr = c.frame();
        idx = nearest(line, fr.com, idx);
        if (std::getenv("RP_TRACE") && (int)(ctx.t * 60 + 0.5) % 60 == 0 && ctx.t - t0 < std::atof(std::getenv("RP_TRACE"))) {
            const double hd = s.f.heading, cu = (fr.com.x - s.f.entry.x) * std::cos(hd) - (fr.com.z - s.f.entry.z) * std::sin(hd), cv = (fr.com.x - s.f.entry.x) * std::sin(hd) + (fr.com.z - s.f.entry.z) * std::cos(hd);
            std::printf("RP t=%.0f u=%.1f v=%.2f y=%.2f ground=%.2f spd=%.2f steer=%.2f thr=%.2f idx=%d line.v~%.2f\n", ctx.t - t0, cu, cv, fr.com.y, w.height(fr.com.x, fr.com.z), dot(fr.vel, fr.fwd), c.steer, c.throttle, idx,
                        ((line[idx].x - s.f.entry.x) * std::sin(hd) + (line[idx].z - s.f.entry.z) * std::cos(hd)));
        }
        if (idx > bestIdx + 1) bestIdx = idx, lastProgressT = ctx.t;  // (2 m of progress)
        if (idx >= (int)line.size() - 4) {
            done = true;
            return;
        }
        // Look ahead along the line.
        const int la = std::min((int)line.size() - 1, idx + 1 + (int)(std::max(0.0, dot(fr.vel, fr.fwd)) * 0.6));  // (about 2-4 m ahead: the notches are only 2.8 m wide)
        const V3 target = line[la];
        const V3 dd = target - fr.com;
        const double along = dot(dd, fr.fwd), lat = dot(dd, fr.left);
        const double speedNow = dot(fr.vel, fr.fwd);
        if (ctx.t < backUntil) {  // backing off for a run-up
            const V3 back = line[std::max(0, idx - 3)] - fr.com;
            c.steer = clampd(std::atan2(dot(back, fr.left), std::max(-dot(back, fr.fwd), 0.5)) * 1.0, -0.5, 0.5) * (speedNow > 0.3 ? 0 : -1);
            c.gear = -1, c.throttle = 0.45, c.brake = 0, c.handbrake = 0;
            return;
        }
        if (c.gear < 0) c.gear = 1;
        c.steer = clampd(std::atan2(lat, std::max(along, 0.5)) * 1.6, -0.55, 0.55);
        c.throttle = clampd(0.3 + (v - speedNow) * 0.9, 0, 1);
        c.brake = speedNow > v + 1.0 ? 0.3 : 0;
        c.handbrake = 0;
        if (ctx.t - lastProgressT > 14) {  // stuck: back off 5 s, then come at it faster
            if (stalls.empty() && std::getenv("RP_DEBUG")) {
                const V3 p = c.frame().com;
                const double hd = s.f.heading, dxu = std::cos(hd), dzu = -std::sin(hd), pxv = std::sin(hd), pzv = std::cos(hd);
                auto loc = [&](double x, double z, double& u, double& v) { u = (x - s.f.entry.x) * dxu + (z - s.f.entry.z) * dzu, v = (x - s.f.entry.x) * pxv + (z - s.f.entry.z) * pzv; };
                double cu, cv;
                loc(p.x, p.z, cu, cv);
                std::printf("RP   stuck: car at u=%.1f v=%.1f y %.2f (ground %.2f), heading %.0f\n", cu, cv, p.y, w.height(p.x, p.z), std::atan2(-c.frame().fwd.z, c.frame().fwd.x) * 180 / M_PI - hd * 180 / M_PI);
                for (const Rock& rk : w.rocks) {
                    double u, v;
                    loc(rk.c.x, rk.c.z, u, v);
                    if (std::hypot(u - cu, v - cv) < rk.r + 3.0) {
                        double top = -1e9;
                        for (const V3& q : rk.v) top = std::max(top, q.y);
                        std::printf("RP     rock u=%.1f v=%.1f r %.2f top %.2f ground %.2f\n", u, v, rk.r, top, w.height(rk.c.x, rk.c.z));
                    }
                }
                for (const Box& b : w.boxes) {
                    double u, v;
                    loc(b.c.x, b.c.z, u, v);
                    if (std::hypot(u - cu, v - cv) < b.bound + 2.0) std::printf("RP     box u=%.1f v=%.1f h %.2f %.2f %.2f top %.2f\n", u, v, b.h.x, b.h.y, b.h.z, b.c.y + b.h.y);
                }
            }
            stalls.push_back({idx, ctx.t - t0});
            backUntil = ctx.t + 5, lastProgressT = ctx.t + 5, tries++;
            static const double kTry[5] = {0.8, 1.3, 0.6, 2.0, 1.0};
            v = std::getenv("RP_CYCLE") ? kTry[tries % 5] : std::min(v + 0.8, 5.0);
        }
    });
    const bool ok = done;
    int lost = 0, shafts = 0, popped = 0;
    for (const Wheel& wh : c.wheels) lost += wh.detached, shafts += wh.shaftBroken, popped += wh.popped;
    const V3 at = line[std::min(bestIdx, (int)line.size() - 1)];
    std::printf("RP pass %s seed %u v=%.1f: %s, reached u=%d of %d m (%.0f s, %d tries) at %.1f,%.1f ground %.2f; damage %.1f%% shafts %d wheels lost %d tires popped %d%s\n", sectionName(t), seed,
                speed, ok ? "THROUGH" : "STUCK", bestIdx * 2, (int)sectionLength(t), ctx.t - t0, tries, at.x, at.z, at.y, c.damage * 100, shafts, lost, popped,
                c.engineDead ? " ENGINE-DEAD" : "");
    for (const auto& st : stalls) std::printf("RP   stall at u=%d after %.0f s\n", st.first * 2, st.second);
    (void)w;
    ctx.metric("through", ok, "", 0, 1);
}

// How long the proving ground takes to build with the section park (best of 3), scanned rocks on as in the game.
void buildTime(Ctx& ctx) {
    props::ScopedRockScans on(props::sharedHullLibrary().loaded() ? &props::sharedHullLibrary() : nullptr);
    const bool was = World::withShowcase;
    World::withShowcase = true;
    double best = 1e9;
    size_t rocks = 0, boxes = 0;
    for (int i = 0; i < 3; i++) {
        const auto t0 = std::chrono::steady_clock::now();
        World w(MAP_PROVING_GROUND);
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        rocks = w.rocks.size(), boxes = w.boxes.size();
    }
    World::withShowcase = was;
    std::printf("RP build: proving ground with the section park %.0f ms (%zu rocks, %zu boxes)\n", best, rocks, boxes);
    ctx.metric("x", 0, "", -1, 1);
}

int reg = [] {
    auto add = [](const std::string& n, std::function<void(Ctx&)> f) { scenarioRegistry().push_back({"rp/" + n, n, std::move(f)}); };
    add("build", buildTime);
    for (const Which& wh : kWhich)
        for (uint32_t seed : {1u, 2u, 3u}) {
            const SectionType t = wh.t;
            add(std::string("info/") + wh.key + "-" + std::to_string(seed), [t, seed](Ctx& c) { info(c, t, seed); });
            for (int dm : {4, 6, 10, 15, 25})
                add(std::string("pass/") + wh.key + "-" + std::to_string(seed) + "-v" + std::to_string(dm), [t, seed, dm](Ctx& c) { pass(c, t, seed, dm / 10.0); });
        }
    return 0;
}();

}  // namespace
