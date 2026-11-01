// Car condition saves (carsave.h): a crashed Blazer comes back the same wreck; a save for
// another vehicle leaves the car undamaged where it was saved; garbage is refused.
#include <cmath>
#include <cstdint>
#include <string>

#include "../src/carsave.h"
#include "../src/mods.h"
#include "../src/net.h"
#include "../src/vehicles.h"
#include "harness.h"

namespace {

int catalogIndex(const char* id) {
    const auto& cat = vehicleCatalog();
    for (int v = 0; v < (int)cat.size(); v++)
        if (std::string(cat[v].id) == id) return v;
    return -1;
}

// A Blazer driven into a barrier at 40 km/h and left to settle, its front-left tire flat and 23.5 L in the tank.
Car& crashedBlazer(Ctx& c) {
    c.sim.setupEmpty();
    c.addProp(barrierSpec(), 0, 0, 90);
    const VehicleSpec& spec = vehicleCatalog()[catalogIndex("blazer")];
    Car& car = c.add(spec, -1.5 - spec.length / 2, 0, 0);
    c.run(1.0);
    c.launch(car, 40 / 3.6);
    c.run(3.0);
    for (Wheel& wh : car.wheels)
        if (wh.front && wh.left) wh.popped = true, wh.poppedAnim = 1;
    car.fuel = 23.5;
    // A beam torn, a pin let go and a window out as well, so the save has them to keep.
    for (Beam& b : car.beams)
        if (b.type == BEAM_BODY && !b.broken) {
            b.broken = true;
            break;
        }
    if (!car.pins.empty()) car.pins.back().broken = true;
    for (Tri& t : car.tris)
        if (t.mat == MAT_GLASS) {
            t.shattered = true;
            break;
        }
    car.throttle = 0, car.handbrake = 1, car.parkBrake = true;  // (parked where it stopped)
    c.run(2.0);
    return car;
}

// The worst any node is off between two dent lists (mm).
double worstDent(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
    if (a.size() != b.size()) return 1e9;
    double worst = 0;
    for (size_t i = 0; i + 2 < a.size(); i += 3) {
        const double dx = a[i] - b[i], dy = a[i + 1] - b[i + 1], dz = a[i + 2] - b[i + 2];
        worst = std::max(worst, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return worst;
}

bool flatFrontLeft(const Car& c) {
    for (const Wheel& wh : c.wheels)
        if (wh.front && wh.left) return wh.popped;
    return false;
}

}  // namespace

SCENARIO(carsaveRoundtrip, "carsave/roundtrip", "A Blazer crashed into a barrier, a tire flat and 23.5 L in the tank and the battery at 40%: saved, rebuilt in a new world and restored, the same wreck") {
    Car& orig = crashedBlazer(ctx);
    ctx.check("the crash damaged it", orig.damage > 0.01, std::to_string(orig.damage));
    orig.battery = 0.4;
    const std::string text = carsave::capture(ctx.sim, orig);
    ctx.check("one line", !text.empty() && text.find('\n') == std::string::npos && text.find('\r') == std::string::npos, std::to_string(text.size()) + " chars");
    ctx.check("vehicleOf: the Blazer", carsave::vehicleOf(text) == catalogIndex("blazer"));
    ctx.check("modsOf: stock", carsave::modsOf(text).stock());
    const std::vector<int16_t> dents = net::carDents(orig);
    const V3 com = orig.frame().com;

    Sim sim2;
    sim2.setupEmpty();
    sim2.addCar(tunedSpec(carsave::vehicleOf(text), carsave::modsOf(text)), V3(30, 2, 20), 1.0, V3(1, 0, 0));
    Car& back = sim2.cars.back();
    ctx.check("restore says yes", carsave::restore(sim2, back, text));
    const double off = worstDent(dents, net::carDents(back));
    ctx.check("every node within 2 mm of where it was", off <= 2, std::to_string(off) + " mm");
    ctx.check("at the saved place", len(back.frame().com - com) < 0.002, std::to_string(len(back.frame().com - com)) + " m");
    ctx.check("the same damage", std::fabs(back.damage - orig.damage) <= 1e-6, std::to_string(back.damage) + " vs " + std::to_string(orig.damage));
    ctx.check("the same beams broken", back.brokenBeams == orig.brokenBeams, std::to_string(back.brokenBeams) + " vs " + std::to_string(orig.brokenBeams));
    bool beamsSame = back.beams.size() == orig.beams.size(), pinsSame = back.pins.size() == orig.pins.size(), glassSame = back.tris.size() == orig.tris.size();
    int bent = 0;
    for (size_t i = 0; beamsSame && i < orig.beams.size(); i++) {
        const Beam &a = orig.beams[i], &b = back.beams[i];
        beamsSame &= a.broken == b.broken && (a.type == BEAM_TIEROD || std::fabs(a.L0 - b.L0) < 1e-12);
        bent += a.L0 != a.Lorig;
    }
    for (size_t i = 0; pinsSame && i < orig.pins.size(); i++) pinsSame &= orig.pins[i].broken == back.pins[i].broken;
    for (size_t i = 0; glassSame && i < orig.tris.size(); i++) glassSame &= orig.tris[i].shattered == back.tris[i].shattered && orig.tris[i].torn == back.tris[i].torn;
    ctx.check("every beam bent and broken the same", beamsSame && bent > 0, std::to_string(bent) + " bent");
    ctx.check("the same pins let go", pinsSame);
    ctx.check("the same glass out", glassSame);
    double restOff = back.chassis.rest.size() == orig.chassis.rest.size() ? 0 : 1e9;
    for (size_t k = 0; restOff < 1 && k < orig.chassis.rest.size(); k++) restOff = std::max(restOff, len(orig.chassis.rest[k] - back.chassis.rest[k]));
    ctx.check("the body's bent rest shape", restOff < 1e-6, std::to_string(restOff * 1000) + " mm");
    ctx.check("engine health", std::fabs(back.engineHealth - orig.engineHealth) <= 1e-9);
    ctx.check("fuel 23.5 L", std::fabs(back.fuel - 23.5) < 1e-9, std::to_string(back.fuel));
    ctx.check("jerry can as it was", back.jerry == orig.jerry);
    ctx.check("battery 0.4", std::fabs(back.battery - 0.4) < 1e-12, std::to_string(back.battery));
    ctx.check("front-left tire flat", flatFrontLeft(back));
    double speed = 0;
    for (const Node& n : back.nodes) speed = std::max(speed, len(n.v));
    ctx.check("at rest", speed == 0);
    ctx.check("a second save reads the same", carsave::capture(sim2, back) == carsave::capture(sim2, back));

    // Left alone it stays the wreck it was: nothing springs back out.
    Ctx c2(sim2);
    c2.run(1.0);
    ctx.metric("dents moved settling 1 s", worstDent(dents, net::carDents(back)), "mm", 0, 10);
    ctx.metric("damage change settling 1 s", std::fabs(back.damage - orig.damage) * 100, "%", 0, 0.2);
    ctx.metric("moved settling 1 s", len(back.frame().com - com) * 1000, "mm", 0, 10);
    ctx.check("still flat", flatFrontLeft(back));
}

SCENARIO(carsaveMismatch, "carsave/mismatch", "A Blazer's save restored onto a Civic: refused, the Civic undamaged at the saved place") {
    Car& orig = crashedBlazer(ctx);
    const std::string text = carsave::capture(ctx.sim, orig);
    const V3 com = orig.frame().com;
    Sim sim2;
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[catalogIndex("civic")], V3(30, 2, 20), 1.0, V3(1, 0, 0));
    Car& civic = sim2.cars.back();
    ctx.check("refused", !carsave::restore(sim2, civic, text));
    civic.updateStats();
    ctx.check("undamaged", civic.damage == 0 && civic.brokenBeams == 0, std::to_string(civic.damage));
    ctx.check("full tank", civic.fuel == -1);
    bool flat = false;
    for (const Wheel& wh : civic.wheels) flat |= wh.popped;
    ctx.check("no flat tire", !flat);
    ctx.check("at the saved place", len(civic.frame().com - com) < 0.002, std::to_string(len(civic.frame().com - com)) + " m");
    ctx.check("finite", civic.finite());

    // The same Blazer with other mods (a changed build): refused the same way.
    std::string modded = text;
    const size_t at = modded.find(" mods=");
    ctx.check("has mods", at != std::string::npos);
    if (at != std::string::npos) modded.replace(at, 15, " mods=1,0,0,0,0");
    sim2.addCar(vehicleCatalog()[catalogIndex("blazer")], V3(-30, 2, 20), 0, V3(1, 0, 0));
    ctx.check("other mods refused", !carsave::restore(sim2, sim2.cars.back(), modded));
}

SCENARIO(carsaveGarbage, "carsave/garbage", "Empty, random and cut-short condition text: refused, nothing crashes") {
    Sim sim;
    sim.setupEmpty();
    const int blazer = catalogIndex("blazer");
    sim.addCar(vehicleCatalog()[blazer], V3(0, 2, 0), 0, V3(1, 0, 0));
    Car& c = sim.cars.back();
    ctx.check("empty refused", !carsave::restore(sim, c, ""));
    ctx.check("empty: no vehicle", carsave::vehicleOf("") == -1);
    ctx.check("empty: stock mods", carsave::modsOf("").stock());
    uint32_t seed = 12345;
    auto rnd = [&]() { return seed = seed * 1664525u + 1013904223u, seed >> 24; };
    bool allRefused = true;
    for (int k = 0; k < 20; k++) {
        std::string junk;
        for (int i = 0; i < 400; i++) junk += (char)rnd();
        allRefused &= !carsave::restore(sim, c, junk);
        carsave::vehicleOf(junk), carsave::modsOf(junk);
    }
    ctx.check("random bytes refused", allRefused);
    ctx.check("still finite", c.finite());

    // A real save cut short, or with its dents scrambled.
    Ctx c2(sim);
    c2.run(0.5);
    const std::string text = carsave::capture(sim, c);
    bool cutRefused = true;
    for (size_t n : {text.size() / 4, text.size() / 2, text.size() - 4}) cutRefused &= !carsave::restore(sim, c, text.substr(0, n));
    ctx.check("cut short refused", cutRefused);
    std::string scrambled = text;
    const size_t d = scrambled.find(" dents=");
    if (d != std::string::npos) scrambled[d + 9] = '!';
    ctx.check("bad dents refused", !carsave::restore(sim, c, scrambled));
    ctx.check("vehicleOf with junk keys", carsave::vehicleOf("car1 veh=abc end") == -1 && carsave::vehicleOf("car1 veh=99999 end") == -1);
    ctx.check("still finite after all that", c.finite());
}
