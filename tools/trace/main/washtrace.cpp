#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[argc > 1 ? atoi(argv[1]) : 0], -10, -70, 0);
    ctx.run(1.0);
    ctx.launch(c, 40 / 3.6);
    int n = 0; double maxrub[4] = {0,0,0,0};
    ctx.run(14, [&] {
        double v = len(c.frame().vel) * 3.6;
        for (int i = 0; i < 4; i++) maxrub[i] = std::max(maxrub[i], (double)c.wheels[i].rub);
        double spin = 0;
        for (const Wheel& w : c.wheels) if (c.wheelPowered(w)) spin = std::max(spin, std::fabs(w.omega * w.rollR()) - std::fabs(w.groundSpeed));
        c.throttle = std::max(0.0, std::min(1.0, (40 - v) * 0.15)) * std::max(0.25, std::min(1.0, 1 - (spin - 1.5) / 4));
        { Frame f = c.frame(); V3 d = V3(f.com.x + 8, f.com.y, -70) - f.com; c.steer = std::max(-0.3, std::min(0.3, std::atan2(dot(d, f.left), dot(d, f.fwd)) * 1.2)); }
        if (n++ % 3 == 0) {
            printf("t=%.2f x=%5.1f v=%5.1f g=%d rpm=%5.0f thr=%.2f shift=%.2f |", ctx.t, c.frame().com.x, v, c.gear, c.rpm, c.throttle, c.shiftTimer);
            for (auto& w : c.wheels) printf(" %5.1f/%5.1f T%5.0f L%5.0f", w.omega, w.sideOmega, w.shaftTorque, w.load);
            printf("\n");
        }
    });
    { int nb = 0; double ylo = 0, yhi = 0, xlo = 1e9, xhi = -1e9;
      for (const Beam& b : c.beams) { if (b.type != BEAM_BODY && b.type != BEAM_CABIN) continue; if (std::fabs(b.L0 - b.Lorig) / b.Lorig * 8 > 0.05 || b.broken) { nb++; V3 m = (c.restLocal[b.a] + c.restLocal[b.b]) * 0.5; ylo += m.y; xlo = std::min(xlo, m.x); xhi = std::max(xhi, m.x); } }
      printf("damaged beams %d, mean rest height %.2f, x %.2f..%.2f\n", nb, nb ? ylo / nb : 0, xlo, xhi); }
    printf("max rub %.0f %.0f %.0f %.0f damage %.2f%%\n", maxrub[0], maxrub[1], maxrub[2], maxrub[3], c.damage * 100);
}
