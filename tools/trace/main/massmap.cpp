#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <set>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 2;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], 0, -50, 0);
    std::set<int> lad(c.ladder.begin(), c.ladder.end());
    double m[4] = {0, 0, 0, 0}, mx[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < c.nodes.size(); i++) {
        int g = (int)i < c.bodyNodes ? 0 : lad.count(i) ? 1 : (int)i < c.frameNodes ? 2 : 3;
        m[g] += c.nodes[i].m; mx[g] += c.nodes[i].m * c.restLocal[i].x;
    }
    const char* nm[4] = {"body", "frame", "pickups", "moving"};
    double M = 0, MX = 0;
    for (int g = 0; g < 4; g++) { printf("%-8s %7.1f kg  com x %+.3f\n", nm[g], m[g], m[g] > 0 ? mx[g] / m[g] : 0); M += m[g]; MX += mx[g]; }
    printf("total %.1f com x %+.3f\n", M, MX / M);
}
