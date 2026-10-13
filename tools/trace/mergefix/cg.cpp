// Builds every catalog vehicle once (CZ_CG=1 prints the stability pass's CG before and after).
#include "../../../tests/harness.h"
#include <cstdio>

int main() {
    for (const VehicleSpec& vs : vehicleCatalog()) {
        Car c;
        c.build(vs, V3(0, 1, 0), 0, V3(0.5, 0.5, 0.5));
        std::printf("  %s mass %.0f frontShare %.3f\n", vs.name, c.mass, c.frontShare);
    }
}
