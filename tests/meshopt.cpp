// The mesh simplifier (meshopt.h): flat things come down to their fewest triangles, hidden faces go, and nothing that
// shows changes (outline, area, colours, normals).
#include <cmath>
#include <set>

#include "../src/meshopt.h"
#include "../src/modelkit.h"
#include "harness.h"

namespace {

// An n x n grid of quads in the plane y = 0 (w a side), facing up.
void grid(mk::B& b, int n, double w, double y = 0) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            const double x0 = -w / 2 + w * i / n, x1 = -w / 2 + w * (i + 1) / n, z0 = -w / 2 + w * j / n, z1 = -w / 2 + w * (j + 1) / n;
            b.quad(V3(x0, y, z1), V3(x1, y, z1), V3(x1, y, z0), V3(x0, y, z0));
        }
}

// A cube with every face cut n x n.
void cutCube(mk::B& b, int n) {
    for (int f = 0; f < 6; f++) {
        b.push();
        if (f < 4) b.roll(90.0 * f);
        else b.yaw(90).roll(f == 4 ? 90 : -90);
        grid(b, n, 1, 0.5);
        b.pop();
    }
}

double area(const mk::Mesh& m) {
    double a = 0;
    for (size_t i = 0; i + 2 < m.v.size(); i += 3) {
        const V3 p(m.v[i].p[0], m.v[i].p[1], m.v[i].p[2]), q(m.v[i + 1].p[0], m.v[i + 1].p[1], m.v[i + 1].p[2]),
            r(m.v[i + 2].p[0], m.v[i + 2].p[1], m.v[i + 2].p[2]);
        a += 0.5 * len(cross(q - p, r - p));
    }
    return a;
}

std::set<std::array<float, 7>> looks(const mk::Mesh& m) {
    std::set<std::array<float, 7>> s;
    for (const mk::MVert& v : m.v) s.insert({v.n[0], v.n[1], v.n[2], v.c[0], v.c[1], v.c[2], (float)v.mat});
    return s;
}

bool facesUp(const mk::Mesh& m) {  // (every triangle's winding still agrees with its normal)
    for (size_t i = 0; i + 2 < m.v.size(); i += 3) {
        const V3 p(m.v[i].p[0], m.v[i].p[1], m.v[i].p[2]), q(m.v[i + 1].p[0], m.v[i + 1].p[1], m.v[i + 1].p[2]),
            r(m.v[i + 2].p[0], m.v[i + 2].p[1], m.v[i + 2].p[2]);
        if (dot(cross(q - p, r - p), V3(m.v[i].n[0], m.v[i].n[1], m.v[i].n[2])) <= 0) return false;
    }
    return true;
}

}  // namespace

SCENARIO(meshoptFlat, "meshopt/flat", "Flat things come down to their fewest triangles: a rectangle 2, a cube 12, a cut-up plane 2, a cut-up cube 12") {
    {
        mk::Mesh m;
        mk::B b(m);
        grid(b, 1, 2);
        optimizeMesh(m);
        ctx.check("a flat rectangle stays 2 triangles", m.triangles() == 2, std::to_string(m.triangles()));
    }
    {
        mk::Mesh m;
        mk::B b(m);
        b.box(V3(0, 0.5, 0), V3(1, 1, 1));
        optimizeMesh(m);
        ctx.check("a cube stays 12", m.triangles() == 12, std::to_string(m.triangles()));
    }
    {
        mk::Mesh m;
        mk::B b(m);
        grid(b, 10, 4);
        const double a0 = area(m);
        const auto l0 = looks(m);
        const V3 lo = (m.finish(), m.lo), hi = m.hi;
        const MeshOptStats s = optimizeMesh(m);
        ctx.check("a 10 x 10 plane collapses to 2", m.triangles() == 2 && s.coplanarSaved == 198, std::to_string(m.triangles()));
        ctx.check("...the same area, bounds and shading", std::fabs(area(m) - a0) < 1e-9 && len(m.lo - lo) < 1e-6 && len(m.hi - hi) < 1e-6 && looks(m) == l0 && facesUp(m));
    }
    {
        mk::Mesh m;
        mk::B b(m);
        cutCube(b, 4);
        const double a0 = area(m);
        optimizeMesh(m);
        ctx.check("a cube cut 4 x 4 a face comes down to 12", m.triangles() == 12, std::to_string(m.triangles()));
        ctx.check("...with its area", std::fabs(area(m) - a0) < 1e-6, std::to_string(area(m)) + " vs " + std::to_string(a0));
    }
}

SCENARIO(meshoptKeeps, "meshopt/keeps", "What would change the picture is kept: colours, materials, smooth normals, holes, corners other faces use") {
    {  // Two colours: each half on its own.
        mk::Mesh m;
        mk::B b(m);
        b.color(1, 0, 0);
        grid(b, 4, 2);
        b.color(0, 0, 1);
        b.move(2, 0, 0);
        grid(b, 4, 2);
        optimizeMesh(m);
        ctx.check("two colours: 2 triangles each", m.triangles() == 4 && looks(m).size() == 2, std::to_string(m.triangles()));
    }
    {  // Smooth normals: nothing merges.
        mk::Mesh m;
        mk::B b(m);
        b.sphere(V3(), V3(1, 1, 1), 2);
        const size_t n = m.triangles();
        optimizeMesh(m);
        ctx.check("a smooth sphere keeps every triangle", m.triangles() == n, std::to_string(m.triangles()) + " of " + std::to_string(n));
    }
    {  // A frame (a square with a square hole): can't be one outline, kept as it is.
        mk::Mesh m;
        mk::B b(m);
        const V3 o[4] = {V3(-2, 0, -2), V3(2, 0, -2), V3(2, 0, 2), V3(-2, 0, 2)}, in[4] = {V3(-1, 0, -1), V3(1, 0, -1), V3(1, 0, 1), V3(-1, 0, 1)};
        for (int k = 0; k < 4; k++) b.quad(o[k], in[k], in[(k + 1) % 4], o[(k + 1) % 4]);
        const double a0 = area(m);
        optimizeMesh(m);
        ctx.check("a frame keeps its hole", std::fabs(area(m) - a0) < 1e-9 && m.triangles() == 8, std::to_string(m.triangles()));
    }
    {  // A flat plane with a smooth-shaded strip along one edge, sharing its corners: they all stay (no T-junction).
        mk::Mesh m;
        mk::B b(m);
        grid(b, 4, 2);
        for (int i = 0; i < 4; i++) {
            const double x0 = -1 + 0.5 * i, x1 = x0 + 0.5;
            b.triN(V3(x0, 0, 1), V3(x1, 0, 1), V3(x1, 0.3, 1.3), V3(0, 1, 0), V3(0, 1, 0), V3(0, 0.7, 0.7));
            b.triN(V3(x0, 0, 1), V3(x1, 0.3, 1.3), V3(x0, 0.3, 1.3), V3(0, 1, 0), V3(0, 0.7, 0.7), V3(0, 0.7, 0.7));
        }
        optimizeMesh(m);
        std::set<std::pair<float, float>> edge;
        for (const mk::MVert& v : m.v)
            if (std::fabs(v.p[2] - 1) < 1e-6 && std::fabs(v.p[1]) < 1e-6) edge.insert({v.p[0], v.p[2]});
        ctx.check("the corners the strip shares stay", edge.size() == 5, std::to_string(edge.size()));
        ctx.check("...and the plane still merges", m.triangles() < 32 + 8, std::to_string(m.triangles()));
    }
    {  // Degenerate triangles go.
        mk::Mesh m;
        mk::B b(m);
        grid(b, 1, 1);
        mk::MVert z = m.v[0];
        for (int k = 0; k < 3; k++) m.v.push_back(z);
        const MeshOptStats s = optimizeMesh(m);
        ctx.check("a zero-area triangle is dropped", m.triangles() == 2 && s.degenerate == 1);
    }
}

SCENARIO(meshoptHidden, "meshopt/hidden", "Faces inside closed convex parts go; glass hides nothing; a decal on a surface stays") {
    {
        mk::Mesh m;
        mk::B b(m);
        b.box(V3(0, 1, 0), V3(2, 2, 2));
        b.color(1, 0, 0).box(V3(0, 1, 0), V3(1, 1, 1));
        optimizeMesh(m);
        ctx.check("a box inside a box is gone", m.triangles() == 12, std::to_string(m.triangles()));
    }
    {
        mk::Mesh m;
        mk::B b(m);
        b.box(V3(0, 0.5, 0), V3(2, 1, 2));
        b.box(V3(0, 1.25, 0), V3(1, 0.5, 1));
        optimizeMesh(m);
        ctx.check("a box stood on a box loses its bottom (only)", m.triangles() == 22, std::to_string(m.triangles()));
    }
    {
        mk::Mesh m;
        mk::B b(m);
        b.mat(mk::GLASS).box(V3(0, 1, 0), V3(2, 2, 2));
        b.mat(mk::MATTE).box(V3(0, 1, 0), V3(1, 1, 1));
        optimizeMesh(m);
        ctx.check("a box inside glass stays", m.triangles() == 24, std::to_string(m.triangles()));
    }
    {
        mk::Mesh m;
        mk::B b(m);
        b.box(V3(0, 0.5, 0), V3(1, 1, 1));
        b.color(1, 1, 0).quad(V3(-0.2, 1, 0.2), V3(0.2, 1, 0.2), V3(0.2, 1, -0.2), V3(-0.2, 1, -0.2));  // a sticker on its top
        optimizeMesh(m);
        ctx.check("a decal on the surface stays", m.triangles() == 14, std::to_string(m.triangles()));
    }
    {
        mk::Mesh m;
        mk::B b(m);
        grid(b, 1, 2, -1);
        b.box(V3(0, 1, 0), V3(1, 1, 1));
        MeshOptOptions o;
        o.groundMin = [](double, double, double, double) { return -0.5; };
        const MeshOptStats s = optimizeMesh(m, o);
        ctx.check("a face under the ground is buried", m.triangles() == 12 && s.buried == 2, std::to_string(m.triangles()));
    }
}
