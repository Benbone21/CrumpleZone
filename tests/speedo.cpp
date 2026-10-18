// The speedometer: wheel speed on the driven wheels against the factory tire's rolling radius,
// so gearing matches the textbook formula, bigger tires read low, wheelspin and aired-down
// tires read high, locked wheels read zero, and mph and km/h agree.
#include <cmath>
#include <string>

#include "../src/mods.h"
#include "harness.h"

namespace {

const int kBronco = 2;

// Factory rolling radius, the way the gauge is calibrated (same sag model).
double calRadius(Car& c) { return c.speedoCalR > 0 ? c.speedoCalR : c.rollingRadius(c.wheels[0], c.spec->tirePsi); }

// The tallest wheels and tires another vehicle offers this one.
const VehicleSpec* bigTires(int vehicle) {
    const VehicleSpec* best = &vehicleCatalog()[vehicle];
    for (int o = 0; o < (int)modOptions(vehicle, MOD_WHEELS).size(); o++) {
        ModSet m;
        m.pick[MOD_WHEELS] = o;
        const VehicleSpec& s = tunedSpec(vehicle, m);
        if (s.wheelRadius > best->wheelRadius) best = &s;
    }
    return best;
}

// Rolling at `v` in `gear`, coasting engaged; the player car, ready to read.
Car& rolling(Ctx& ctx, const VehicleSpec& spec, double v, int gear) {
    Car& c = ctx.add(spec, -60, -50, 0);
    ctx.focus(c);
    ctx.run(1);
    c.manualShift = true;
    ctx.launch(c, v);
    c.gear = gear;
    ctx.run(0.6, [&] { c.throttle = 0.12; c.brake = c.handbrake = 0; });
    return c;
}

double trueSpeed(const Car& c) { return len(c.frame().vel); }

}  // namespace

SCENARIO(speedoGears, "speedo/gears", "At a steady speed in each gear the gauge shows rpm * 2 pi * r / (60 * gear * final), r the factory rolling radius") {
    const VehicleSpec& spec = vehicleCatalog()[kBronco];
    for (int g = 1; g <= (int)spec.gears.size(); g++) {
        const double v = std::min(30.0, 2200 * 2 * M_PI / 60 * spec.wheelRadius / (spec.gears[g - 1] * spec.finalDrive));
        ctx.sim.cars.clear();
        Car& c = rolling(ctx, spec, v, g);
        const double expect = c.rpm * 2 * M_PI * calRadius(c) / (60 * spec.gears[g - 1] * spec.finalDrive);
        const std::string tag = "gear " + std::to_string(g) + ": " + std::to_string(c.speedoMs()) + " vs formula " + std::to_string(expect) + " (true " + std::to_string(trueSpeed(c)) + ")";
        const double free = c.rpm * 2 * M_PI * c.wheels[0].R / (60 * spec.gears[g - 1] * spec.finalDrive);
        ctx.check("true speed matches the formula on the sim's tire radius in gear " + std::to_string(g), std::fabs(trueSpeed(c) / free - 1) < 0.01, tag);
        ctx.check("gauge matches the gearing formula in " + tag, c.gear == g && std::fabs(c.speedoMs() / expect - 1) < 0.03, tag);
        ctx.check("and the stock tire reads true in gear " + std::to_string(g), std::fabs(c.speedoMs() / trueSpeed(c) - 1) < 0.03, tag);
    }
}

SCENARIO(speedoBigTires, "speedo/big-tires", "Bigger tires fitted without recalibrating read low by the rolling-radius ratio; recalibrating reads true") {
    const VehicleSpec& stock = vehicleCatalog()[kBronco];
    const VehicleSpec* big = bigTires(kBronco);
    ctx.check("another vehicle offers taller tires", big->wheelRadius > stock.wheelRadius * 1.05);
    Car& c = rolling(ctx, *big, 14, 3);
    const double ratio = c.speedoMs() / trueSpeed(c);
    Wheel ref = c.wheels[0];
    ref.R = stock.wheelRadius;
    const double expect = c.rollingRadius(ref, stock.tirePsi) / c.rollingRadius(c.wheels[0], c.tirePsi);
    ctx.check("bigger tires read low", ratio < 0.97, std::to_string(ratio));
    ctx.check("by the ratio of the rolling radii", std::fabs(ratio / expect - 1) < 0.02, std::to_string(ratio) + " vs " + std::to_string(expect));
    c.recalibrateSpeedo();
    ctx.check("recalibrated, it reads true", std::fabs(c.speedoMs() / trueSpeed(c) - 1) < 0.02, std::to_string(c.speedoMs() / trueSpeed(c)));
}

SCENARIO(speedoSlip, "speedo/slip-lock-air", "Wheelspin reads high, locked wheels read zero, aired-down tires read slightly high") {
    const VehicleSpec& spec = vehicleCatalog()[kBronco];
    Car& c = rolling(ctx, spec, 14, 3);
    const double v = trueSpeed(c), base = c.speedoMs();
    ctx.check("stock tires at street pressure read true", std::fabs(base / v - 1) < 0.03, std::to_string(base / v));
    c.tirePsi = 12;
    const double aired = c.speedoMs();
    ctx.check("aired down reads slightly high", aired > base * 1.005 && aired < base * 1.06, std::to_string(aired / base));
    c.tirePsi = spec.tirePsi;
    std::vector<double> om;
    for (Wheel& w : c.wheels) om.push_back(w.omega);
    for (Wheel& w : c.wheels) w.omega *= 1.4;
    ctx.check("wheelspin reads high", std::fabs(c.speedoMs() / base - 1.4) < 0.02, std::to_string(c.speedoMs() / base));
    for (Wheel& w : c.wheels) w.omega = 0;
    ctx.check("locked wheels read zero", c.speedoMs() < 0.01, std::to_string(c.speedoMs()));
    c.recalibrateSpeedo();
    for (size_t i = 0; i < om.size(); i++) c.wheels[i].omega = om[i];
    ctx.check("recalibrating the stock tire changes nothing", std::fabs(c.speedoMs() / base - 1) < 0.001);
}

SCENARIO(speedoUnits, "speedo/units", "mph and km/h agree") {
    for (double ms : {5.0, 13.4, 27.8, 40.0}) {
        const int kmh = Car::speedoDisplay(ms, false), mph = Car::speedoDisplay(ms, true);
        ctx.check("km/h is mph * 1.609 at " + std::to_string(ms) + " m/s", std::fabs(kmh - mph * 1.609344) < 1.0 + 0.0, std::to_string(kmh) + " vs " + std::to_string(mph));
        ctx.check("km/h is 3.6 times m/s at " + std::to_string(ms) + " m/s", std::fabs(kmh - ms * 3.6) <= 0.5);
    }
}

SCENARIO(speedoGovernor, "speedo/governor-tires", "A governed truck's limiter follows its tires' speed rating; an ungoverned car keeps none") {
    int bronco = -1, jesko = -1;
    const auto& cat = vehicleCatalog();
    for (int v = 0; v < (int)cat.size(); v++) {
        if (std::string(cat[v].name).find("BRONCO") != std::string::npos) bronco = v;
        if (std::string(cat[v].name).find("JESKO") != std::string::npos) jesko = v;
    }
    ctx.check("the Bronco and the Jesko are in the catalog", bronco >= 0 && jesko >= 0);
    if (bronco < 0 || jesko < 0) return;
    auto withTires = [&](int v, const char* tire) -> const VehicleSpec* {
        const auto& opts = modOptions(v, MOD_TIRES);
        for (int k = 1; k < (int)opts.size(); k++)
            if (opts[k].name.find(tire) != std::string::npos) {
                ModSet m;
                m.pick[MOD_TIRES] = k;
                return &tunedSpec(v, m);
            }
        return nullptr;
    };
    ctx.check("stock Bronco governed at 100 mph", std::fabs(cat[bronco].speedLimit - 161) < 0.5);
    const VehicleSpec* street = withTires(bronco, "STREET");
    const VehicleSpec* mud = withTires(bronco, "MUD");
    ctx.check("street tires raise it to their rating", street && std::fabs(street->speedLimit - tireSpeedRatingKmh(TIRE_STREET)) < 0.5 && street->speedLimit > 161);
    ctx.check("mud-terrains keep about 100 mph", mud && std::fabs(mud->speedLimit - 161) < 0.5);
    const VehicleSpec* jslick = withTires(jesko, "SLICK");
    ctx.check("the Jesko stays ungoverned on any tires", jslick && jslick->speedLimit == 0);
}
