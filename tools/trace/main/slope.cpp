#include "../../../src/world.h"
#include <cstdio>
#include <cmath>
int main() {
    World w(MAP_FRONTIER);
    for (double z = 262; z <= 282; z += 2) {
        double x = 416;
        V3 n = w.normal(x, z);
        printf("z %.0f height %.2f slope %.1f deg surface %d\n", z, w.height(x, z), std::acos(n.y) * 57.3, (int)w.surfaceAt(x, z));
    }
}
