// Scanned props (src/props.*): the rock hulls the baker writes (assets/props/rocks.hull), the
// hull builder itself, and the physics with scanned rocks on: what you hit is what you see.
// run_tests never turns the scanned hulls on by itself (every other scenario keeps the
// code-made rocks it was tuned on); these scenarios load the library and turn it on for
// their own thread (props::ScopedRockScans).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "../src/level.h"
#include "../src/objshape.h"
#include "../src/props.h"
#include "harness.h"

namespace {

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }

const props::HullLibrary* library(Ctx& ctx) {
    const props::HullLibrary& lib = props::sharedHullLibrary();
    // (No bake in this checkout, e.g. where the Poly Haven sources can't be fetched: the
    // scanned parts are skipped, not failed. The game falls back to code-made rocks too.)
    if (!lib.loaded()) {
        std::printf("     skip assets/props/rocks.hull missing: run `make bake` (tools/bakeprops.cpp)\n");
        return nullptr;
    }
    (void)ctx;
    return &lib;
}

// Closed, convex, wound outward; how far the farthest corner is outside any face (m).
struct HullShape {
    bool closed = true;
    double bulge = 0;  // worst corner in front of a face plane
    int verts = 0, faces = 0;
};
HullShape shapeOf(const std::vector<V3>& v, const std::vector<std::array<int, 3>>& f) {
    HullShape s;
    s.verts = (int)v.size(), s.faces = (int)f.size();
    std::map<std::pair<int, int>, int> edges;
    for (const auto& t : f)
        for (int k = 0; k < 3; k++) edges[{t[k], t[(k + 1) % 3]}]++;
    for (const auto& e : edges)
        if (e.second != 1 || !edges.count({e.first.second, e.first.first})) s.closed = false;
    if ((int)v.size() - (int)edges.size() / 2 + (int)f.size() != 2) s.closed = false;  // Euler: a sphere
    V3 c;
    for (const V3& p : v) c += p;
    c = c / (double)std::max<size_t>(1, v.size());
    for (const auto& t : f) {
        const V3 n = norm(cross(v[t[1]] - v[t[0]], v[t[2]] - v[t[0]]));
        if (dot(n, v[t[0]] - c) <= 0) s.closed = false;  // wound inward
        for (const V3& p : v) s.bulge = std::max(s.bulge, dot(n, p - v[t[0]]));
    }
    return s;
}

V3 closestOnTri(const V3& p, const V3& a, const V3& b, const V3& c) {
    const V3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const V3 bp = p - b;
    const double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    const V3 cp = p - c;
    const double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const double den = 1.0 / (va + vb + vc);
    return a + ab * (vb * den) + ac * (vc * den);
}

// The rock's visible mesh (the scan's first LOD) in the world.
struct WorldMesh {
    std::vector<V3> p;
    std::vector<std::array<int, 3>> t;
    double distance(const V3& q) const {
        double best = 1e18;
        for (const auto& f : t) {
            const V3 c = closestOnTri(q, p[f[0]], p[f[1]], p[f[2]]);
            best = std::min(best, dot(q - c, q - c));
        }
        return std::sqrt(best);
    }
};
bool visibleMesh(const Rock& k, const props::HullLibrary& lib, WorldMesh& out) {
    if (k.scan < 0 || k.scan >= (int)lib.hulls.size()) return false;
    props::Model m;
    if (!props::loadModel(props::assetDir() + "/" + lib.hulls[k.scan].name + ".prop", m) || m.lods.empty()) return false;
    for (const props::Sub& s : m.lods[0].subs) {
        const int base = (int)out.p.size();
        for (const props::Vert& v : s.v) out.p.push_back(k.origin + k.axis[0] * v.p[0] + k.axis[1] * v.p[1] + k.axis[2] * v.p[2]);
        for (size_t i = 0; i + 2 < s.idx.size(); i += 3) out.t.push_back({base + s.idx[i], base + s.idx[i + 1], base + s.idx[i + 2]});
    }
    return !out.t.empty();
}

void crawlEast(Car& c, double z, double speed) {
    Frame fr = c.frame();
    V3 d = V3(fr.com.x + 6, fr.com.y, z) - fr.com;
    c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 1.5, -0.55, 0.55);
    double v = dot(fr.vel, fr.fwd);
    c.throttle = clampd(0.3 + (speed - v) * 0.9, 0, 1);
    c.brake = v > speed + 1.0 ? 0.3 : 0;
    c.handbrake = 0;
}

}  // namespace

SCENARIO(propHulls, "props/hulls", "Scanned rock hulls: the builder, the baked library (closed, convex, <= 48 corners, hugging the scan), and World::addRock with scans on and off") {
    // The builder on a lumpy point cloud (no assets needed).
    {
        std::vector<V3> pts;
        uint32_t s = 12345;
        auto rnd = [&]() {
            s ^= s << 13, s ^= s >> 17, s ^= s << 5;
            return (s & 0xFFFFFF) / double(0x1000000);
        };
        for (int i = 0; i < 3000; i++) {
            const double y = 1 - 2 * rnd(), a = rnd() * 2 * M_PI, rr = std::sqrt(std::max(0.0, 1 - y * y));
            const double k = 0.85 + 0.15 * std::sin(a * 5) * std::cos(y * 4);
            pts.push_back(V3(std::cos(a) * rr * 1.2 * k, y * 0.6 * k, std::sin(a) * rr * 0.9 * k));
        }
        double miss = 0;
        const props::RockHull h = props::buildRockHull("cloud", pts, 48, &miss);
        const HullShape sh = shapeOf(h.v, h.f);
        ctx.check("builder: closed, wound outward", sh.closed);
        ctx.metric("builder: corners", sh.verts, "", 8, 48);
        ctx.metric("builder: worst corner outside a face", sh.bulge * 1000, "mm", 0, 0.1);
        ctx.metric("builder: worst point missed (1.2 m cloud)", miss * 1000, "mm", 0, 60);
    }
    // Off by default: run_tests keeps the code-made rocks.
    ctx.check("scanned hulls off by default", props::activeRockScans() == nullptr);
    {
        World& w = ctx.sim.world;
        Rock& k = w.addRock(60, -110, V3(0.8, 0.6, 0.7), 0.5, 4242, 1);
        ctx.check("code-made rock without the library", k.scan < 0);
        w.rocks.pop_back();
    }
    const props::HullLibrary* lib = library(ctx);
    if (!lib) return;
    ctx.metric("hulls in the library", (double)lib->hulls.size(), "", 20, 64);
    int bad = 0, over = 0;
    double worstBulge = 0, worstPoke = 0, worstGap = 0;
    std::string worstPokeName;
    for (const props::RockHull& h : lib->hulls) {
        const HullShape sh = shapeOf(h.v, h.f);
        bad += !sh.closed;
        over += sh.verts > 48;
        worstBulge = std::max(worstBulge, sh.bulge);
        // The scan's first LOD against its hull: how far it pokes out, and how far the hull's
        // corners stand off it (the hull hugs the scan: its corners are the scan's).
        props::Model m;
        if (!props::loadModel(props::assetDir() + "/" + h.name + ".prop", m) || m.lods.empty()) {
            ctx.check(h.name + ": model file", false);
            continue;
        }
        WorldMesh mesh;
        for (const props::Sub& s : m.lods[0].subs) {
            const int base = (int)mesh.p.size();
            for (const props::Vert& v : s.v) mesh.p.push_back(V3(v.p[0], v.p[1], v.p[2]));
            for (size_t i = 0; i + 2 < s.idx.size(); i += 3) mesh.t.push_back({base + s.idx[i], base + s.idx[i + 1], base + s.idx[i + 2]});
        }
        double poke = 0;
        for (const V3& p : mesh.p) {
            double out = -1e9;
            for (const auto& f : h.f) {
                const V3 n = norm(cross(h.v[f[1]] - h.v[f[0]], h.v[f[2]] - h.v[f[0]]));
                out = std::max(out, dot(n, p - h.v[f[0]]));
            }
            poke = std::max(poke, out);
        }
        if (poke > worstPoke) worstPoke = poke, worstPokeName = h.name;
        for (const V3& c : h.v) worstGap = std::max(worstGap, mesh.distance(c));
    }
    ctx.check("every hull closed and wound outward", bad == 0, std::to_string(bad) + " bad");
    ctx.check("every hull has 48 corners or fewer", over == 0, std::to_string(over) + " over");
    ctx.metric("worst corner outside a face (all hulls)", worstBulge * 1000, "mm", 0, 0.5);
    ctx.metric("visible scan poking out of its hull (worst, " + worstPokeName + ")", worstPoke * 1000, "mm", 0, 60);
    // (Corners are the drawn mesh's own, pushed out at most 1 cm. A hull made from the raw scan
    // stood off the simplified first LOD by up to 54 mm: that is what this limit guards.)
    ctx.metric("hull corner off the visible scan (worst)", worstGap * 1000, "mm", 0, 15);
    // addRock with the library on: a scan, sized as asked, its top where asked.
    props::ScopedRockScans on(lib);
    World& w = ctx.sim.world;
    int notScan = 0;
    double worstTop = 0, worstSize = 0;
    std::set<int> used;
    for (int i = 0; i < 40; i++) {
        const V3 radii(0.4 + 0.05 * i, (0.4 + 0.05 * i) * (0.5 + 0.01 * i), (0.4 + 0.05 * i) * 0.8);
        const double top = radii.y * 1.2;
        Rock& k = w.addRock(40 + 3.0 * (i % 10), -120 + 4.0 * (i / 10), radii, top, 500 + i, 1);
        notScan += k.scan < 0;
        used.insert(k.scan);
        double ymax = -1e9, ymin = 1e9;
        for (const V3& p : k.v) ymax = std::max(ymax, p.y), ymin = std::min(ymin, p.y);
        worstTop = std::max(worstTop, std::fabs(ymax - (w.height(k.c.x, k.c.z) + top)));
        worstSize = std::max(worstSize, std::fabs((ymax - ymin) / (2 * radii.y) - 1));
        const HullShape sh = shapeOf(k.v, k.f);
        if (!sh.closed || sh.verts > 48) notScan++;
    }
    for (int i = 0; i < 40; i++) w.rocks.pop_back();
    ctx.check("addRock with the library: every rock a scanned hull", notScan == 0, std::to_string(notScan) + " not");
    ctx.metric("addRock: different scans used (40 rocks)", (double)used.size(), "", 6, 64);
    ctx.metric("addRock: top off where asked (worst)", worstTop * 1000, "mm", 0, 2);
    ctx.metric("addRock: height off the asked size (worst)", worstSize * 100, "%", 0, 12);
}

SCENARIO(propBroncoBoulder, "props/bronco-boulder", "A set-up Bronco in 4L crawls straight over a scanned boulder (0.4 m showing) without damage") {
    const props::HullLibrary* lib = library(ctx);
    if (!lib) return;
    props::ScopedRockScans on(lib);
    World& w = ctx.sim.world;
    const double z = -110, rx = 50;
    // A rounded boulder a 35 in tire can walk up (the Bronco's measured step in 4L is
    // ~0.45 m against a square ledge: see pilotsense stepCap).
    Rock& k = w.addRock(rx, z, V3(1.0, 0.55, 0.9), 0.42, 77, 1);
    ctx.check("the boulder is a scan", k.scan >= 0, k.scan >= 0 ? lib->hulls[k.scan].name : "");
    w.buildGrid();
    const VehicleSpec& spec = bronco();
    Car& c = ctx.add(spec, rx - 10, z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);  // 4L
    c.tractionControl = false;
    double onRock = 0, maxPitch = 0, maxDrift = 0;
    const bool over = ctx.runUntil(40, [&] { return c.frame().com.x > rx + 8; }, [&] {
        crawlEast(c, z, 1.5);
        for (const Wheel& wh : c.wheels)
            if (wh.load > 0 && wh.surface == SURF_ROCK) onRock += Ctx::kFrame;
        const Frame fr = c.frame();
        maxPitch = std::max(maxPitch, std::asin(clampd(fr.fwd.y, -1, 1)) * 180 / M_PI);
        maxDrift = std::max(maxDrift, std::fabs(fr.com.z - z));
    });
    ctx.check("crawled over and past the boulder", over, over ? "" : "stopped at x = " + std::to_string(c.frame().com.x - rx) + " m from the rock");
    ctx.metric("tire-seconds on the rock", onRock, "s", 1.0, 60);
    ctx.metric("nose up climbing it", maxPitch, "deg", 5, 45);
    ctx.metric("off the line (it went over, not round)", maxDrift, "m", 0, 0.8);
    ctx.metric("damage", c.damage * 100, "%", 0, 1.0);
}

SCENARIO(propRestOnRock, "props/rest-on-rock", "A Bronco settled with a wheel on a scanned rock touches it within 5 cm of the rock's visible mesh") {
    const props::HullLibrary* lib = library(ctx);
    if (!lib) return;
    props::ScopedRockScans on(lib);
    World& w = ctx.sim.world;
    const VehicleSpec& spec = bronco();
    const double x = 60, z = -110;
    Car& c = ctx.add(spec, x, z, 0);
    ctx.focus(c);
    ctx.run(1.0, [&] { c.handbrake = 1; });
    // Lift it, slide a wide, low rock under the front left tire, let it down.
    int fl = -1;
    for (int i = 0; i < (int)c.wheels.size(); i++)
        if (c.wheels[i].front && c.wheels[i].left) fl = i;
    const V3 hub = (c.nodes[c.wheels[fl].inner].p + c.nodes[c.wheels[fl].outer].p) * 0.5;
    for (Node& n : c.nodes) n.p.y += 0.7;
    Rock& k = w.addRock(hub.x, hub.z, V3(0.9, 0.4, 0.8), 0.35, 31, 1);
    w.buildGrid();
    WorldMesh mesh;
    ctx.check("the rock is a scan with its mesh", visibleMesh(k, *lib, mesh), k.scan >= 0 ? lib->hulls[k.scan].name : "");
    ctx.run(4.0, [&] { c.handbrake = 1; });
    const Wheel& wh = c.wheels[fl];
    ctx.check("front left tire resting on the rock", wh.load > 0 && wh.surface == SURF_ROCK);
    if (mesh.t.empty() || wh.surface != SURF_ROCK) return;
    // Where the tire touches the hull, against the scan's drawn surface; and the tire's
    // own surface (hub distance less the radius) against it.
    const double gap = mesh.distance(wh.contact);
    const V3 h = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5;
    // A convex hull bridges the hollows between a lumpy scan's ridges (the moss rocks, scaled up
    // to 1.8 m, are lumpy to 5-10 cm: hull-to-scan distance did not shrink with more corners, 48
    // to 128), so a point on a flat hull face is off the drawn rock by about that much: 2-11 cm
    // over 30 seeds, median 5.5. The float a driver sees is the tread line below, which stays
    // tight (median 1.3 cm). This one catches a hull in the wrong place or scale (tens of cm).
    ctx.metric("tire contact off the visible rock", gap * 100, "cm", 0, 12);
    ctx.metric("tire tread off the visible rock (hub distance - radius)", (mesh.distance(h) - wh.R) * 100, "cm", -5, 5);
    ctx.metric("rock height under the tire", wh.contact.y - w.height(wh.contact.x, wh.contact.z), "m", 0.15, 0.5);
}

// ---- Drawing them: LODs and culling (src/props.h; propdraw.cpp draws with these) ----------------------------------
namespace {

// The models in the bake (empty, and the checks skipped, where assets/props isn't there).
std::vector<props::Model> bakedModels() {
    std::vector<props::Model> out;
    const std::string dir = props::assetDir();
    if (dir.empty()) return out;
    for (const props::ManifestEntry& e : props::readManifest(dir)) {
        props::Model m;
        if (props::loadModel(dir + "/" + e.name + ".prop", m)) out.push_back(std::move(m));
    }
    return out;
}

// FNV-1a over a LOD's vertices and indices: the very mesh, not just its size.
uint64_t lodHash(const props::Lod& l) {
    uint64_t h = 1469598103934665603ull;
    auto bytes = [&](const void* p, size_t n) {
        const unsigned char* c = (const unsigned char*)p;
        for (size_t i = 0; i < n; i++) h = (h ^ c[i]) * 1099511628211ull;
    };
    for (const props::Sub& s : l.subs) {
        bytes(&s.mat, sizeof s.mat);
        for (const props::Vert& v : s.v) bytes(v.p, sizeof v.p), bytes(v.n, sizeof v.n), bytes(v.uv, sizeof v.uv);
        bytes(s.idx.data(), s.idx.size() * sizeof(uint16_t));
    }
    return h;
}

uint32_t rng(uint32_t& s) { return s = s * 1664525u + 1013904223u; }
double rnd(uint32_t& s, double lo, double hi) { return lo + (hi - lo) * (rng(s) >> 8) / 16777216.0; }

}  // namespace

SCENARIO(propLodBudgets, "props/lod-budgets", "Every scanned rock and cliff: three LODs, each well under the one before (the simplifier stopped at 196 on one LOD2), LOD0 within its budget; the near ones always drawn at LOD0") {
    const std::vector<props::Model> models = bakedModels();
    if (models.empty()) {
        std::printf("     skip assets/props missing: run `make bake` (tools/bakeprops.cpp)\n");
        return;
    }
    int bad = 0, nearBad = 0;
    for (const props::Model& m : models) {
        if (m.kind != props::KIND_ROCK && m.kind != props::KIND_CLIFF) continue;
        const int budget = m.kind == props::KIND_CLIFF ? 3600 : 1900;
        const bool ok = m.lods.size() == 3 && m.lods[0].tris() <= budget && m.lods[1].tris() * 3 <= m.lods[0].tris() && m.lods[2].tris() * 2 <= m.lods[1].tris() &&
                        m.lods[2].tris() >= 40;
        if (!ok) bad++, std::printf("     %s: %d / %d / %d tris\n", m.name.c_str(), m.lods.size() > 0 ? m.lods[0].tris() : 0, m.lods.size() > 1 ? m.lods[1].tris() : 0, m.lods.size() > 2 ? m.lods[2].tris() : 0), ctx.note(m.name + ": " + std::to_string(m.lods.size()) + " LODs, " + std::to_string(m.lods.empty() ? 0 : m.lods[0].tris()) + " tris at LOD0");
        if (m.lods.size() != 3) continue;
        // Up close (nearer than three times its size, at high and ultra) it's the full mesh.
        const int tris[3] = {m.lods[0].tris(), m.lods[1].tris(), m.lods[2].tris()};
        for (double detail : {1.0, 1.5})
            for (double k = 0.1; k <= 3.0; k += 0.1)
                if (props::solidLod(tris, 1.0, k, detail) != 0) nearBad++;
    }
    ctx.check("three LODs, LOD1 under a third of LOD0, LOD2 under half of LOD1, LOD0 in budget", bad == 0, std::to_string(bad) + " models off");
    ctx.check("LOD0 nearer than three times its size", nearBad == 0, std::to_string(nearBad) + " near picks coarser");
    // The pick only ever gets coarser with distance, and drops a copy only when it's a few pixels.
    const int tris[3] = {1500, 400, 100};
    int last = 0, backwards = 0;
    for (double d = 0.5; d < 2000; d *= 1.05) {
        const int l = props::solidLod(tris, 2.0, d, 1.0);
        const int rank = l < 0 ? 3 : l;
        if (rank < last) backwards++;
        last = rank;
        if (l < 0 && 2.0 / d * props::kLodRefPxPerRad > 3.5) backwards++;
    }
    ctx.check("coarser with distance, dropped only under ~3 px", backwards == 0);
}

SCENARIO(propLod0, "props/lod0-unchanged", "The scanned models' LOD0 is the mesh that was baked (vertex for vertex): the LOD work leaves what you see up close alone") {
    const std::vector<props::Model> models = bakedModels();
    if (models.empty()) {
        std::printf("     skip assets/props missing: run `make bake` (tools/bakeprops.cpp)\n");
        return;
    }
    // (CZ_LOD0_HASHES=1 prints these afresh, after a deliberate re-bake.)
    static const std::map<std::string, uint64_t> want = {
        {"rock_moss1_a", 0x135b28b73d4256caull},
        {"rock_moss1_b", 0x902c1001fc72e3a3ull},
        {"rock_moss1_c", 0x84501cc36a78e9abull},
        {"rock_moss1_d", 0x2f9e1a2ce158e4abull},
        {"rock_moss1_e", 0x77deec32bd59b16eull},
        {"rock_moss1_f", 0xb7fd22355b3169b5ull},
        {"rock_moss2_a", 0x1d7da905c98f8c76ull},
        {"rock_moss2_b", 0xb526cec78303871cull},
        {"rock_moss2_c", 0x640db29ea209fc71ull},
        {"rock_moss2_d", 0x21ba376af5b448bbull},
        {"rock_moss2_e", 0xd6229f0b372f0839ull},
        {"rock_moss2_f", 0xf09fdb87f09cb0c3ull},
        {"rock_moss2_g", 0xa8d435d39d4bbef0ull},
        {"rock_07", 0x908a2e5c060fe622ull},
        {"rock_09", 0xc956094ae25374e7ull},
        {"boulder_01", 0x4a13e6163802eb45ull},
        {"stone_01", 0x0ff715d61dd7bba7ull},
        {"nama_boulder_03", 0x19e2a0e8e33ae1b8ull},
        {"nama_boulder_04", 0x12d75becd9be234bull},
        {"nama_boulder_05", 0x6c690b579833481bull},
        {"nama_boulders_01a", 0x6c1829f7661b6c2eull},
        {"nama_boulders_01b", 0xff2df9979b523941ull},
        {"nama_stone_a", 0x7ee4dee18f9deafbull},
        {"nama_stone_b", 0x7ce60d17d23a42f2ull},
        {"nama_stone_c", 0x11777f343c290921ull},
        {"nama_stone_d", 0x7ac1160a1c3a302dull},
        {"nama_stone_e", 0xa178fce98ae37764ull},
        {"cliff_face_01", 0xfbaadc530006ef01ull},
        {"cliff_face_02", 0x3bee26a882505e9aull},
        {"cliff_nama_01", 0x10d0746f14114043ull},
    };
    int bad = 0, seen = 0;
    for (const props::Model& m : models) {
        if (m.lods.empty()) continue;
        const uint64_t h = lodHash(m.lods[0]);
        char line[160];
        std::snprintf(line, sizeof line, "{\"%s\", 0x%016llxull},", m.name.c_str(), (unsigned long long)h);
        auto it = want.find(m.name);
        if (std::getenv("CZ_LOD0_HASHES")) std::printf("%s\n", line);
        if (it == want.end()) continue;
        seen++;
        if (it->second != h) bad++, ctx.note(std::string("changed: ") + line);
    }
    ctx.check("every LOD0 the same as baked", bad == 0 && seen == (int)want.size(), std::to_string(bad) + " changed, " + std::to_string(seen) + " of " + std::to_string(want.size()) + " found");
}

SCENARIO(propCulling, "props/culling", "Frustum culling (main view and shadow box) never drops a sphere or box that reaches into the view") {
    uint32_t seed = 12345;
    int dropped = 0, kept = 0, tried = 0;
    for (int cam = 0; cam < 200; cam++) {
        const V3 eye(rnd(seed, -300, 300), rnd(seed, 0, 80), rnd(seed, -300, 300));
        const V3 at = eye + V3(rnd(seed, -1, 1), rnd(seed, -0.6, 0.3), rnd(seed, -1, 1));
        M4 vp;
        if (cam % 2) vp = perspective((float)rnd(seed, 0.6, 1.6), (float)rnd(seed, 1.0, 2.4), 0.05f, 6000.0f) * lookAt(eye, at, V3(0, 1, 0));
        else {  // a shadow cascade's box
            const float h = (float)rnd(seed, 20, 240);
            vp = ortho(-h, h, -h, h, -500, 500) * lookAt(eye, eye - norm(V3(rnd(seed, -1, 1), rnd(seed, 0.3, 1), rnd(seed, -1, 1))), V3(0, 1, 0));
        }
        float pl[6][4];
        props::frustumPlanes(vp, pl);
        auto inside = [&](const V3& p) {
            const float* m = vp.m;
            const double x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
            const double z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14], w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
            return w > 0 && std::fabs(x) <= w && std::fabs(y) <= w && std::fabs(z) <= w;
        };
        for (int k = 0; k < 400; k++) {
            const V3 c = eye + V3(rnd(seed, -250, 250), rnd(seed, -60, 60), rnd(seed, -250, 250));
            const double r = rnd(seed, 0.1, 12);
            // Seen if any point of it is in the view (its middle, and points over its surface and inside).
            bool seen = inside(c);
            for (int s = 0; s < 64 && !seen; s++) {
                const V3 d = norm(V3(rnd(seed, -1, 1), rnd(seed, -1, 1), rnd(seed, -1, 1)));
                seen = inside(c + d * (r * (s % 2 ? 1.0 : rnd(seed, 0, 1))));
            }
            tried++;
            if (!seen) continue;
            const V3 h(r, r, r);  // (a box round it, as a cell's box is round its copies' spheres)
            if (!props::sphereInFrustum(pl, c, r) || !props::boxInFrustum(pl, c - h, c + h)) dropped++;
            else kept++;
        }
    }
    ctx.metric("seen and kept", kept, "", 1000, 1e9);
    ctx.check("nothing seen is culled", dropped == 0, std::to_string(dropped) + " of " + std::to_string(kept + dropped) + " dropped (" + std::to_string(tried) + " tried)");
}

// ---- Hit what's drawn: a placed, scaled, flat rock -------------------------------------------------
SCENARIO(propFlatRockTop, "props/flat-rock-top", "A big flat rock placed the editor's way (any scan, scaled flat/long, posed): the surface a car meets is within a few cm of the drawn mesh's top") {
    const props::HullLibrary* lib = library(ctx);
    if (!lib) return;
    props::ScopedRockScans on(lib);
    World& w = ctx.sim.world;
    double worstAbove = 0, tallAbove = 0, worstBelow = 0, worstAll = 0;
    std::string worstName;
    for (int scan = 0; scan < (int)lib->hulls.size(); scan++) {
        for (int variant = 0; variant < 3; variant++) {
            LevelObject o;
            o.kind = OBJ_ROCK, o.sub = 1, o.scan = variant == 2 ? -1 : scan, o.seed = 7 + scan;
            o.p = V3(40 + 14 * (scan % 4), 0, -150 - 14 * (scan / 4 % 6));
            o.size = variant == 0 ? V3(5, 0.5, 3.5) : variant == 1 ? lib->hulls[scan].half() * 3.0 : V3(6, 0.6, 4.5);
            o.top = o.size.y * 1.2;
            o.yaw = 0.7 * scan, o.pitch = variant == 1 ? 0.1 : 0, o.roll = variant == 1 ? -0.08 : 0;
            const size_t n = w.rocks.size();
            placeObject(o, w);
            w.buildGrid();
            const Rock& k = w.rocks[n];
            WorldMesh mesh;
            if (!visibleMesh(k, *lib, mesh)) continue;
            for (double fx = -0.9; fx <= 0.9; fx += 0.15)
                for (double fz = -0.9; fz <= 0.9; fz += 0.15) {
                    const V3 q = k.c + V3(fx * k.r, 0, fz * k.r);
                    auto topAt = [&](double x, double z) {  // (the mesh's top at this x, z)
                        double best = -1e9;
                        for (const auto& t : mesh.t) {
                            const V3 &a = mesh.p[t[0]], &b = mesh.p[t[1]], &c = mesh.p[t[2]];
                            const double d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
                            if (std::fabs(d) < 1e-12) continue;
                            const double l1 = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / d;
                            const double l2 = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / d;
                            if (l1 < 0 || l2 < 0 || l1 + l2 > 1) continue;
                            best = std::max(best, l1 * a.y + l2 * b.y + (1 - l1 - l2) * c.y);
                        }
                        return best;
                    };
                    const double drawn = topAt(q.x, q.z);
                    // (only where the drawn top is gentle: on a steep edge a few cm sideways is a lot of height)
                    bool gentle = drawn > -1e8 && drawn > w.height(q.x, q.z) + 0.3;
                    for (int e = 0; e < 4 && gentle; e++) {
                        const double nb = topAt(q.x + (e == 0) * 0.08 - (e == 1) * 0.08, q.z + (e == 2) * 0.08 - (e == 3) * 0.08);
                        gentle = nb > -1e8 && std::fabs(nb - drawn) < 0.06;
                    }
                    if (!gentle) continue;
                    objshape::WorldHit h;
                    double hit = -1e9;
                    if (objshape::rayWorld(w, V3(q.x, drawn + 5, q.z), V3(0, -1, 0), 20, h) && h.kind == objshape::SH_ROCK) hit = h.hit.p.y;
                    if (hit < -1e8) hit = drawn - 1;  // (missed it: counts as well below)
                    const double dy = hit - drawn;
                    const double off = hit > -1e8 ? mesh.distance(V3(q.x, hit, q.z)) : 1;  // (how far the surface met is from the drawn one: fair on a slope too)
                    if (std::fabs(dy) > worstAll) worstAll = std::fabs(dy), worstName = lib->hulls[k.scan].name + " v" + std::to_string(variant);
                    double& above = variant == 1 ? tallAbove : worstAbove;
                    above = std::max(above, off), worstBelow = std::max(worstBelow, -dy);
                }
            w.rocks.resize(n);
        }
    }
    std::printf("     worst: %s\n", worstName.c_str());
    // (A single convex hull stood 20-30 cm over a flat scaled scan, 80 cm over a tall one; the pieces keep within a few cm to ~15 cm of a lumpy 3x boulder.)
    ctx.metric("flat rocks: collision surface off the drawn mesh, above it (worst)", worstAbove * 100, "cm", 0, 9);
    ctx.metric("scaled boulders: collision surface off the drawn mesh, above it (worst)", tallAbove * 100, "cm", 0, 20);
    ctx.metric("collision below the drawn top (worst)", worstBelow * 100, "cm", 0, 5);
}
