// Export a settled vehicle's render geometry (body, parts, running gear, wheels) as triangles
// for an offline software render: "T x y z x y z x y z r g b" lines, and "W ix iy iz ox oy oz R".
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include "../../../src/carvisual.h"
#include "../../../src/render.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 4;
    const char* out = argc > 2 ? argv[2] : "car.tri";
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], 0, 0, 0);
    ctx.run(2.0);
    FILE* f = fopen(out, "w");
    const VisualTemplate& t = visualTemplate(c);
    std::vector<BodyDynamic> dyn; std::vector<unsigned> opaque, glass;
    evalBody(c, t, dyn, opaque, glass);
    const float* paint = c.spec->paintColor;
    float pr = paint[0] >= 0 ? paint[0] : c.color.x, pg = paint[0] >= 0 ? paint[1] : c.color.y, pb = paint[0] >= 0 ? paint[2] : c.color.z;
    for (size_t i = 0; i + 2 < opaque.size(); i += 3) {
        fprintf(f, "T");
        for (int k = 0; k < 3; k++) fprintf(f, " %.4f %.4f %.4f", dyn[opaque[i + k]].p[0], dyn[opaque[i + k]].p[1], dyn[opaque[i + k]].p[2]);
        fprintf(f, " %.3f %.3f %.3f\n", pr, pg, pb);
    }
    for (size_t i = 0; i + 2 < glass.size(); i += 3) {
        fprintf(f, "T");
        for (int k = 0; k < 3; k++) fprintf(f, " %.4f %.4f %.4f", dyn[glass[i + k]].p[0], dyn[glass[i + k]].p[1], dyn[glass[i + k]].p[2]);
        fprintf(f, " 0.10 0.13 0.18\n");
    }
    std::vector<Vtx> parts;
    evalParts(c, t, parts);
    buildRunningGear(c, parts);
    for (size_t i = 0; i + 2 < parts.size(); i += 3) {
        fprintf(f, "T");
        for (int k = 0; k < 3; k++) fprintf(f, " %.4f %.4f %.4f", parts[i + k].p[0], parts[i + k].p[1], parts[i + k].p[2]);
        fprintf(f, " %.3f %.3f %.3f\n", parts[i].c[0], parts[i].c[1], parts[i].c[2]);
    }
    for (const Wheel& w : c.wheels) {
        V3 a = c.nodes[w.inner].p, b = c.nodes[w.outer].p;
        fprintf(f, "W %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n", a.x, a.y, a.z, b.x, b.y, b.z, w.R);
    }
    fclose(f);
    printf("%zu body tris, %zu glass, %zu part tris\n", opaque.size() / 3, glass.size() / 3, parts.size() / 3);
}
