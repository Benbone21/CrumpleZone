#include "lights.h"

#include <map>

#include "carvisual.h"
#include "vehicles.h"

namespace {

// Where a vehicle type's lamps are at rest (car frame: x forward, y up, z right), the
// body node each one rides on, and the body's rest center of mass.
struct Layout {
    V3 rest[4];       // headlamp left, right; tail lamp left, right
    int node[4] = {-1, -1, -1, -1};
    V3 rc;
};

// Lamp centers from the same lines the body shader draws them with (render.cpp).
void lampRest(const Car& car, const VisualTemplate& t, V3 head[2], V3 tail[2]) {
    const VehicleSpec& vs = *car.spec;
    const float* sp = t.shellParam;
    const double shB = sp[1], shHood = sp[3], shXf = sp[4], shW2 = sp[5], shLiftY = sp[10], shLiftZ = sp[11],
                 shXr = sp[14], shBump = sp[18];
    double hx, hy, hz, tx, ty, tz;
    if (t.shell == 1) {         // 4Runner: wide lamps beside the grille, tails up the rear corners
        hx = shXf - 0.04, hy = shHood - 0.135, hz = 0.535;
        tx = shXr + 0.03, ty = shB - 0.13, tz = 0.5 * (shLiftZ + shW2);
    } else if (t.shell == 2) {  // Bronco: round lamps in the grille, vertical tails on the corners
        hx = shXf - 0.04, hy = 0.5 * ((shBump + 0.03) + (shHood - 0.03)), hz = shW2 - 0.25;
        tx = shXr + 0.03, ty = shB - 0.21, tz = shW2 - 0.06;
    } else if (t.shell == 3) {  // EF Civic: flush wide lamps, a full-width tail band
        hx = shXf - 0.04, hy = shHood - 0.065, hz = 0.52;
        tx = shXr + 0.03, ty = 0.5 * (shLiftY + 0.16 + shB - 0.02), tz = 0.60;
    } else if (vs.model == 1) {  // Jesko: teardrop lenses on the front fenders (buildCoachwork)
        hx = vs.length / 2 - 0.32, hy = 0.62, hz = 0.74;
        tx = -vs.length / 2 + 0.02, ty = 0.60, tz = 0.58;
    } else if (vs.model == 4 || vs.model == 5) {  // tube-frame vehicles: lamps in the nose cone / hood, tails on the tail panel
        const double F = vs.layerY[0];
        hx = vs.length / 2 - 0.04, hy = F + (vs.model == 4 ? 0.40 : 0.36), hz = vs.model == 4 ? 0.46 : 0.42;
        tx = -vs.length / 2 + 0.03, ty = F + (vs.model == 4 ? 0.32 : 0.34), tz = vs.model == 4 ? 0.48 : 0.40;
    } else {
        // Smoothed cage: the lamp rows the shader uses (uBody.z/w), at the ends.
        double xf = -1e9, xr = 1e9;
        for (int n = 0; n < car.bodyNodes; n++) {
            xf = std::max(xf, car.restLocal[n].x);
            xr = std::min(xr, car.restLocal[n].x);
        }
        auto rowY = [&](double k) {
            const int k0 = std::min(std::max((int)k, 0), (int)vs.layerY.size() - 2);
            return vs.layerY[k0] + (vs.layerY[k0 + 1] - vs.layerY[k0]) * clampd(k - k0, 0, 1);
        };
        hx = xf - 0.04, hy = rowY(t.body[2]), hz = vs.width / 2 - 0.38;
        tx = xr + 0.04, ty = rowY(t.body[3]), tz = vs.width / 2 - 0.30;
    }
    for (int s = 0; s < 2; s++) {
        const double side = s == 0 ? -1 : 1;  // left is -z
        head[s] = V3(hx, hy, side * hz);
        tail[s] = V3(tx, ty, side * tz);
    }
}

const Layout& layoutFor(const Car& car, const VisualTemplate& t) {
    static std::map<const VehicleSpec*, Layout> cache;  // (the render thread only)
    auto it = cache.find(car.spec);
    if (it != cache.end() && it->second.node[0] < car.bodyNodes) return it->second;
    Layout L;
    lampRest(car, t, L.rest, L.rest + 2);
    // Each lamp rides on the nearest body node: it follows that bit of bodywork through
    // dents, and goes with the panel if the panel comes off.
    for (int k = 0; k < 4; k++) {
        double best = 1e18;
        for (int n = 0; n < car.bodyNodes; n++) {
            const V3 d = car.restLocal[n] - L.rest[k];
            if (dot(d, d) < best) best = dot(d, d), L.node[k] = n;
        }
    }
    double m = 0;
    for (int n = 0; n < car.bodyNodes; n++) {
        L.rc += car.restLocal[n] * car.nodes[n].m;
        m += car.nodes[n].m;
    }
    L.rc = m > 0 ? L.rc / m : V3();
    return cache[car.spec] = L;
}

}  // namespace

const char* lampModeName(int mode) {
    static const char* names[LAMP_MODES] = {"LIGHTS: AUTO (ON AT DUSK)", "LOW BEAM", "HIGH BEAM", "LIGHTS OFF"};
    return names[mode < 0 || mode >= LAMP_MODES ? 0 : mode];
}

int lampBeam(int mode, float night) {
    if (mode == LAMPS_LOW) return 1;
    if (mode == LAMPS_HIGH) return 2;
    if (mode == LAMPS_OFF) return 0;
    return night > 0.3f ? 1 : 0;  // about when the sun is a few degrees down
}

void carLamps(const Car& car, const VisualTemplate& t, int beam, CarLamps& out) {
    out = CarLamps();
    out.beam = beam;
    if (car.isProp() || car.spec->model < 0 || car.bodyNodes == 0 || car.chassis.nodes.empty()) return;
    const Layout& L = layoutFor(car, t);
    const Frame fr = car.frame();
    out.fwd = fr.fwd;
    out.up = fr.up;
    out.placed = true;
    // Car frame to world: x forward, y up, z right (= -left).
    auto toWorld = [&](const V3& v) { return fr.fwd * v.x + fr.up * v.y - fr.left * v.z; };
    auto toLocal = [&](const V3& v) { return V3(dot(v, fr.fwd), dot(v, fr.up), -dot(v, fr.left)); };
    for (int k = 0; k < 4; k++) {
        const int n = L.node[k];
        if (n < 0) continue;
        const V3& p = car.nodes[n].p;
        // How far the bodywork under the lamp has been pushed out of place: a crushed
        // corner or a missing bumper or hood (the node went with it) puts the lamp out.
        const V3 moved = toLocal(p - fr.com) + L.rc - car.restLocal[n];
        const float ok = (float)(1 - smoothstep(0.07, 0.14, len(moved)));
        if (k < 2) {
            out.headPos[k] = p + toWorld(L.rest[k] - car.restLocal[n]);
            out.head[k] = ok;
        } else {
            out.tail[k - 2] = ok;
        }
    }
}
