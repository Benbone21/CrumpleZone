#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <map>
int main() {
    for (int v = 0; v < 5; v++) {
        Sim sim; Ctx ctx(sim);
        Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
        std::map<std::array<int,3>, int> cellOf;
        for (size_t i = 0; i < c.cellIJK.size(); i++) cellOf[c.cellIJK[i]] = (int)i;
        int out = 0, in = 0, miss = 0;
        for (const SkinQuad& q : c.quads) {
            auto it = cellOf.find({q.cell[0], q.cell[1], q.cell[2]});
            if (it == cellOf.end()) { miss++; continue; }
            V3 cc; for (int b = 0; b < 8; b++) cc += c.restLocal[c.cells[it->second][b]];
            cc = cc / 8.0;
            V3 b0 = c.restLocal[q.v[0]], b1 = c.restLocal[q.v[1]], b2 = c.restLocal[q.v[2]], b3 = c.restLocal[q.v[3]];
            V3 n = cross((b1 - b0) + (b2 - b3), (b3 - b0) + (b2 - b1));
            V3 qc = (b0 + b1 + b2 + b3) / 4.0;
            (dot(n, qc - cc) > 0 ? out : in)++;
        }
        printf("%s: quads %zu outward %d inward %d nocell %d\n", vehicleCatalog()[v].name, c.quads.size(), out, in, miss);
    }
}
