#include "vehiclepreview.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <tuple>

#include "carvisual.h"
#include "vehicles.h"

namespace {

struct Slot {
    std::unique_ptr<Car> car;  // (built at the origin, x ahead, until the template is ready)
    VehiclePreview pv;
    bool done = false;
};

void put(mk::Mesh& m, const V3& p, const V3& n, const V3& c, int mat) {
    mk::MVert v;
    v.p[0] = (float)p.x, v.p[1] = (float)p.y, v.p[2] = (float)p.z;
    v.n[0] = (float)n.x, v.n[1] = (float)n.y, v.n[2] = (float)n.z;
    v.c[0] = (float)c.x, v.c[1] = (float)c.y, v.c[2] = (float)c.z;
    v.mat = (uint8_t)mat;
    m.v.push_back(v);
}

V3 paintOf(const VehicleSpec& vs, int idx) {
    if (vs.paintColor[0] >= 0) return V3(vs.paintColor[0], vs.paintColor[1], vs.paintColor[2]);
    static const V3 kPalette[] = {V3(0.62, 0.12, 0.10), V3(0.13, 0.30, 0.55), V3(0.75, 0.70, 0.62), V3(0.20, 0.38, 0.22)};
    return kPalette[idx % 4];
}

void build(Slot& s, const VisualTemplate& t) {
    const Car& car = *s.car;
    mk::Mesh& m = s.pv.mesh;
    std::vector<V3> P(car.nodes.size());
    for (size_t i = 0; i < car.nodes.size(); i++) P[i] = car.nodes[i].p, s.pv.massKg += car.nodes[i].m;

    // The body: painted, its glass dark (the game draws glass in a pass of its own; a picture has one pass).
    std::vector<BodyDynamic> dyn;
    std::vector<unsigned> opaque, glass;
    evalBody(car, P, t, dyn, opaque, glass);
    std::set<std::tuple<unsigned, unsigned, unsigned>> glassTris;
    for (size_t i = 0; i + 2 < glass.size(); i += 3) glassTris.insert({glass[i], glass[i + 1], glass[i + 2]});
    const V3 paint = car.color, tint(0.05, 0.07, 0.09);
    // (The game's body shader paints the windows, lamps and grille per pixel from each vertex's region; here a face takes its
    // region's colour: the glass regions dark, the underside and wheel wells dark grey.)
    auto regionOf = [&](unsigned rv) { return rv < t.rvStatic.size() ? (int)t.rvStatic[rv].region : (int)REG_SIDE; };
    auto body = [&](const std::vector<unsigned>& idx, bool isGlass) {
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            if (!isGlass && glassTris.count({idx[i], idx[i + 1], idx[i + 2]})) continue;  // (drawn as glass below)
            const int r0 = regionOf(idx[i]), r1 = regionOf(idx[i + 1]), r2 = regionOf(idx[i + 2]);
            const int reg = r0 == r1 || r0 == r2 ? r0 : r1 == r2 ? r1 : (int)REG_SIDE;
            const bool glassy = isGlass || reg == REG_WINDSHIELD || reg == REG_REAR_WINDOW || reg == REG_CABIN_SIDE;
            const bool dark = reg == REG_UNDER || reg == REG_WELL;
            for (int k = 0; k < 3; k++) {
                const BodyDynamic& d = dyn[idx[i + k]];
                put(m, V3(d.p[0], d.p[1], d.p[2]), V3(d.n[0], d.n[1], d.n[2]), glassy ? tint : dark ? V3(0.06, 0.06, 0.065) : paint,
                    glassy || dark ? mk::MATTE : mk::PAINT);  // (the icon shader lights GLASS as a pale sheen: matte reads as tinted glass)
            }
        }
    };
    body(opaque, false);
    body(glass, true);

    // Bolt-on parts (bumpers, lamps, racks, the bed...) and the running gear, in their own colours and materials.
    std::vector<Vtx> parts;
    evalParts(P, t, parts);
    buildRunningGear(car, parts);
    for (const Vtx& v : parts) put(m, V3(v.p[0], v.p[1], v.p[2]), V3(v.n[0], v.n[1], v.n[2]), V3(v.c[0], v.c[1], v.c[2]), (int)v.c[3]);

    // The tyres and rims (the game draws these from meshes of its own).
    mk::B b(m);
    for (const Wheel& w : car.wheels) {
        const V3 a = car.nodes[w.inner].p, o = car.nodes[w.outer].p, c = (a + o) * 0.5;
        V3 ax = o - a;
        const double l = len(ax);
        if (l < 1e-6) continue;
        ax = ax * (1.0 / l);
        b.style(V3(0.035, 0.035, 0.04), mk::MATTE);
        b.cyl(c - ax * (w.width / 2), c + ax * (w.width / 2), w.R, 28);
        b.style(V3(0.55, 0.56, 0.58), mk::METAL);
        b.cyl(c + ax * (w.width / 2 - 0.02), c + ax * (w.width / 2 + 0.006), w.rimR * 0.96, 24);
        b.cyl(c - ax * (w.width / 2 + 0.006), c - ax * (w.width / 2 - 0.02), w.rimR * 0.96, 24);
    }
    m.finish();
}

}  // namespace

const VehiclePreview* vehiclePreview(int idx) {
    const auto& cat = vehicleCatalog();
    if (idx < 0 || idx >= (int)cat.size()) return nullptr;
    static std::map<int, Slot> slots;
    Slot& s = slots[idx];
    if (s.done) return s.pv.mesh.v.empty() ? nullptr : &s.pv;
    if (!s.car) {
        s.car = std::make_unique<Car>();
        s.car->build(cat[idx], V3(), 0, paintOf(cat[idx], idx));
    }
    const VisualTemplate* t = tryVisualTemplate(*s.car);
    if (!t) return nullptr;
    build(s, *t);
    s.done = true;
    s.car.reset();
    return s.pv.mesh.v.empty() ? nullptr : &s.pv;
}
