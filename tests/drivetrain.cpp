// Driver-visible gear controls and coast-down regressions, using the real drivetrain.
#include "harness.h"
#include "../src/gamepad.h"

VEHICLE_SCENARIO(manualNeutral, "drivetrain/manual-neutral", "Manual R-N-1 selection, free revs, pedal release and independent brake controls") {
    Car& c = ctx.add(spec, -60, -50, 0);
    ctx.focus(c);
    c.manualShift = true;
    ctx.run(1);
    ctx.check("Q from first selects neutral", c.shift(-1) && c.gear == 0);
    const V3 start = c.frame().com;
    ctx.run(1.5, [&] { ctx.sim.drive(true, false, false, false, false, Ctx::kFrame); });
    ctx.check("gas preserves neutral", c.gear == 0);
    ctx.metric("engine revs freely in neutral", c.rpm - spec.idleRpm, "rpm", 2500, spec.redline);
    ctx.metric("neutral sends no propulsion to the wheels", len(c.frame().com - start), "m", 0, 0.15);
    ctx.run(2, [&] { ctx.sim.drive(false, false, false, false, false, Ctx::kFrame); });
    ctx.metric("released pedal returns to idle", std::fabs(c.rpm - spec.idleRpm), "rpm", 0, 10);
    ctx.check("Q from neutral selects reverse", c.shift(-1) && c.gear == -1);
    ctx.run(spec.shiftTime + 0.1);
    ctx.sim.drive(true, false, false, false, false, Ctx::kFrame);
    ctx.check("manual reverse: W is gas, gear stays reverse", c.throttle == 1 && c.brake == 0 && c.gear == -1);
    ctx.sim.drive(false, true, false, false, false, Ctx::kFrame);
    ctx.check("manual reverse: S brakes without selecting forward", c.throttle == 0 && c.brake == 1 && c.gear == -1);
    const auto p = pad::pedals(0.7, 0.2, -3, -1, true);
    ctx.check("manual controller pedals preserve reverse", p.gear == -1 && p.throttle == 0.7 && p.brake == 0.2);
    ctx.check("E from reverse selects neutral", c.shift(1) && c.gear == 0);
    ctx.run(spec.shiftTime + 0.1);
    ctx.launch(c, 10);
    c.gear = 0;  // launch() normally picks a suitable forward gear for its speed
    ctx.run(0.05, [&] { c.throttle = c.brake = c.handbrake = 0; });
    ctx.check("reverse cannot engage while rolling at speed", !c.shift(-1) && c.gear == 0);
    ctx.check("E from neutral selects first", c.shift(1) && c.gear == 1);
    ctx.run(spec.shiftTime + 0.1);
    ctx.launch(c, 50);
    c.gear = 0;
    ctx.run(0.05, [&] { c.throttle = c.brake = c.handbrake = 0; });
    ctx.check("neutral cannot select first at an over-revving road speed", !c.shift(1) && c.gear == 0);
}

VEHICLE_SCENARIO(reverseRelease, "drivetrain/reverse-release", "Reverse drive then pedal release: assist must not sustain acceleration or rev limiting") {
    Car& c = ctx.add(spec, -50, -55, 0);
    ctx.focus(c);
    ctx.run(1);
    c.gear = -1;
    ctx.run(3, [&] { c.throttle = 0.6; c.brake = c.handbrake = 0; });
    // Tire/shaft energy already stored under power can accelerate the body briefly
    // as wheelspin settles; judge sustained drive after that transient.
    ctx.run(0.35, [&] { ctx.sim.drive(false, false, false, false, false, Ctx::kFrame); });
    const double startSpeed = -dot(c.frame().vel, c.frame().fwd);
    double peak = startSpeed;
    ctx.check("vehicle is moving backwards", startSpeed > 1);
    ctx.run(5, [&] {
        ctx.sim.drive(false, false, false, false, false, Ctx::kFrame);
        peak = std::max(peak, -dot(c.frame().vel, c.frame().fwd));
    });
    ctx.metric("no reverse acceleration after pedal release", peak - startSpeed, "m/s", 0, 0.6);
    ctx.metric("speed falls after throttle release", -dot(c.frame().vel, c.frame().fwd) / startSpeed, "share", 0, 0.98);
    ctx.metric("RPM leaves the rev limiter", c.rpm / spec.redline, "share", 0, 0.85);
    ctx.metric("drag assist closes", c.dragAssist, "throttle", 0, 0.05);
}

SCENARIO(shiftMatch, "drivetrain/shift-match", "An upshift opens the clutch and matches RPM over the shift instead of snapping in one physics step") {
    Car& c = ctx.add(vehicleCatalog()[0], -80, -50, 0);
    ctx.focus(c); c.manualShift = true;
    ctx.run(1);
    ctx.launch(c, 8);
    ctx.run(0.1, [&] { c.throttle = 0.3; });
    const double before = c.rpm;
    ctx.check("upshift begins", c.shift(1) && c.gear == 2);
    ctx.sim.substep();
    ctx.metric("RPM change in the first 0.5 ms", std::fabs(c.rpm - before), "rpm", 0, 5);
    ctx.run(c.spec->shiftTime + 0.2, [&] { c.throttle = 0; });
    double omega = 0; int n = 0;
    for (const Wheel& wh : c.wheels) if (c.wheelPowered(wh)) { omega += wh.sideOmega; n++; }
    const double inputRpm = omega / n * c.spec->gears[c.gear - 1] * c.spec->finalDrive * 60 / (2 * M_PI);
    ctx.metric("RPM matches the selected ratio after clutch closure", std::fabs(c.rpm - inputRpm), "rpm", 0, 150);
    ctx.check("manual selection survives coast-down", c.gear == 2);
}

SCENARIO(converterShift, "drivetrain/converter-shift", "An automatic unlocks its converter during a commanded shift without snapping pump RPM to wheel RPM") {
    Car& c = ctx.add(vehicleCatalog()[2], -80, -50, 0);
    ctx.focus(c); c.manualShift = true;
    ctx.run(1); ctx.launch(c, 8);
    c.gear = 2; c.rpm = 4000;
    ctx.check("manual selection starts an automatic's gear change", c.shift(1) && c.gear == 3);
    const double before = c.rpm;
    ctx.sim.substep();
    ctx.metric("pump RPM changes continuously at shift onset", std::fabs(c.rpm - before), "rpm", 0, 30);
    double omega = 0; int count = 0;
    for (const Wheel& wh : c.wheels) if (c.wheelPowered(wh)) { omega += wh.sideOmega; count++; }
    const double turbine = omega / count * c.spec->gears[2] * c.spec->finalDrive * 60 / (2 * M_PI);
    ctx.check("unlocked converter keeps engine speed independent of turbine", c.rpm > turbine + 300);
}
