// The fuel can and the pumps (src/fuel.*): pouring a can, the hose from pump to filler, what it costs, money running out, driving off with it in,
// refilling a can, and all of it across players.
#include "../src/career.h"
#include "../src/fuel.h"
#include "../src/interact.h"
#include "../src/modelkit.h"
#include "../src/repairsync.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

V3 eyeOf(const Walker& wk) { return wk.p + V3(0, 1.62, 0); }

void standBy(const World& w, const V3& from, const V3& at, Walker& wk, double back) {
    V3 out = at - from;
    out.y = 0;
    out = norm(out);
    const V3 q = at + out * back;
    wk.p = V3(q.x, w.height(q.x, q.z), q.z), wk.v = V3(), wk.active = true;
}

// Holds E on `at` until the job in front of you is done: what was said.
std::string doAt(Sim& sim, Interact& ia, Walker& wk, const V3& at, InteractKind want, double secs = 8) {
    if (sim.hose.held()) sim.hose.hand = wk.p + V3(0, 1, 0);
    for (int i = 0; i < 2; i++) ia.update(sim, wk, eyeOf(wk), at - eyeOf(wk), false, Ctx::kFrame);
    if (ia.current.kind != want) return "(wrong job: " + ia.current.prompt + ")";
    std::string said;
    for (double t = 0; t < secs && ia.current.kind == want && !ia.waitRelease; t += Ctx::kFrame) {
        const std::string s = ia.update(sim, wk, eyeOf(wk), at - eyeOf(wk), true, Ctx::kFrame);
        if (!s.empty()) said = s;
    }
    ia.update(sim, wk, eyeOf(wk), at - eyeOf(wk), false, Ctx::kFrame);
    return said;
}

// A truck parked beside a pump: its filler within the hose's reach.
struct Forecourt {
    Car* car;
    V3 pump, fill;
    Walker wk;
    Interact ia;
    explicit Forecourt(Ctx& ctx, int vehicle = 2, double x = 0) {
        car = &ctx.add(vehicleCatalog()[vehicle], x, 0, 0);
        ctx.focus(*car);
        ctx.run(0.5);
        const Frame fr = car->frame();
        const V3 at = fr.com + fr.left * 3.5 - fr.fwd * 1.0;
        ctx.sim.world.placeModel("fuel_pump", V3(at.x, ctx.sim.world.height(at.x, at.z), at.z), 0);
        ctx.sim.world.models.back().variant = 0;
        std::vector<V3> ps;
        fuel::findPumps(ctx.sim.world, ps);
        pump = ps.back();
        fill = fuel::filler(*car);
        standBy(ctx.sim.world, fr.com, pump, wk, 0.8);
        car->fuel = 20;
    }
    std::string takeAndPlug(Ctx& ctx) {
        const std::string a = doAt(ctx.sim, ia, wk, pump, IA_TAKE_HOSE);
        if (!ctx.sim.hose.held()) return "(no hose: " + a + ")";
        standBy(ctx.sim.world, car->frame().com, fill, wk, 1.0);
        ctx.sim.hose.hand = wk.p + V3(0, 1, 0);
        doAt(ctx.sim, ia, wk, fill, IA_PLUG_HOSE);
        return ctx.sim.hose.state == Sim::FuelHose::PLUGGED ? "" : "(not plugged: " + ia.current.prompt + ")";
    }
};

}  // namespace

SCENARIO(fuelFiller, "fuel/filler", "Every vehicle's filler is on its body, on the side (or deck) it's drawn on, and follows it") {
    int k = 0;
    for (const VehicleSpec& s : vehicleCatalog()) {
        Car& c = ctx.add(s, k * 12.0, 0, 0);
        k++;
        ctx.run(0.1);
        const Frame fr = c.frame();
        const V3 d = fuel::filler(c) - fr.com;
        const double along = dot(d, fr.fwd), up = dot(d, fr.up), across = dot(d, fr.left);
        ctx.check(std::string(s.id) + ": on the body", std::fabs(along) < s.length / 2 + 0.1 && std::fabs(across) < s.width / 2 + 0.25 && up > -0.6 && up < 2,
                  std::to_string(along) + " " + std::to_string(up) + " " + std::to_string(across));
        if (std::string(s.id) == "bronco") ctx.check("the Bronco's on its left rear", along < 0 && across > 0.5);
        if (std::string(s.id) == "civic") ctx.check("the Civic's on its right rear", along < 0 && across < -0.4);
    }
}

SCENARIO(fuelCanPour, "fuel/can-pour", "A can in your hand at the filler: hold E and 20 L go in; a career's can goes empty and stays; sandbox's never runs out; a full tank refuses") {
    Forecourt f(ctx);
    Car& c = *f.car;
    Career career;
    career.on = true, career.cash = 100;
    career.grant(repairPartKey(PART_FUEL_CAN));
    c.fuel = 10;
    standBy(ctx.sim.world, c.frame().com, f.fill, f.wk, 1.0);
    f.ia.update(ctx.sim, f.wk, eyeOf(f.wk), f.fill - eyeOf(f.wk), false, Ctx::kFrame);
    ctx.check("no can, no marker on the filler", f.ia.current.kind != IA_POUR_CAN);
    f.wk.hand = TrunkRef{TK_PART, PART_FUEL_CAN};
    const std::string said = doAt(ctx.sim, f.ia, f.wk, f.fill, IA_POUR_CAN);
    ctx.check("20 L in", std::fabs(c.fuel - 30) < 1e-6 && said.find("20 L") != std::string::npos, std::to_string(c.fuel) + " " + said);
    ctx.check("an empty can in your hand", f.wk.hand == (TrunkRef{TK_PART, PART_EMPTY_CAN}));
    ctx.check("it counts", fuel::settle(f.ia, career) && career.kit().parts[PART_FUEL_CAN] == 0 && career.kit().parts[PART_EMPTY_CAN] == 1);
    // Sandbox: the same, nothing counted.
    Career sandbox;
    f.wk.hand = TrunkRef{TK_PART, PART_FUEL_CAN};
    doAt(ctx.sim, f.ia, f.wk, f.fill, IA_POUR_CAN);
    ctx.check("sandbox pours too", std::fabs(c.fuel - 50) < 1e-6);
    ctx.check("and nothing's used", !fuel::settle(f.ia, sandbox) && sandbox.stock.empty());
    c.fuel = c.spec->fuelCapacity - 0.2;
    f.wk.hand = TrunkRef{TK_PART, PART_FUEL_CAN};
    f.ia.update(ctx.sim, f.wk, eyeOf(f.wk), f.fill - eyeOf(f.wk), false, Ctx::kFrame);
    ctx.check("a full tank refuses", f.ia.current.kind == IA_POUR_CAN && f.ia.current.blocked, f.ia.current.prompt);
}

SCENARIO(fuelCanItem, "fuel/can-item", "The can is on a station's rack, in the supply caches, in sandbox's back; the empty one isn't sold or shown there") {
    bool sold = false, emptySold = false;
    for (const ShopItem& it : stationRack()) sold |= it.key == repairPartKey(PART_FUEL_CAN), emptySold |= it.key == repairPartKey(PART_EMPTY_CAN);
    ctx.check("sold", sold && !emptySold);
    ctx.check("it has a model", mk::findModel(repairPartModel(PART_FUEL_CAN)) != nullptr);
    Car& c = ctx.add(vehicleCatalog()[2], 0, 0, 0);
    TrunkStock own;
    bool full = false, empty = false;
    for (const TrunkItem& it : trunk::wholeLoad(c, own, true, TrunkRef(), false)) full |= it.ref == TrunkRef{TK_PART, PART_FUEL_CAN}, empty |= it.ref == TrunkRef{TK_PART, PART_EMPTY_CAN};
    ctx.check("in sandbox's back, full", full && !empty);
    int caches = 0;
    for (const CacheSpot& s : cacheSpots(ctx.sim.world))
        for (const std::string& g : s.gear) caches += g == repairPartKey(PART_FUEL_CAN);
    ctx.check("in a cache", caches >= 1, std::to_string(caches));
}

SCENARIO(fuelPumpFlow, "fuel/pump-flow", "Hose from the pump to the filler: fuel flows at the pump's rate and a career pays by the litre, the gauge says so, it stops when full, comes out and hangs back") {
    Forecourt f(ctx);
    Car& c = *f.car;
    int cash = 1000;
    f.ia.money = cash;
    const std::string err = f.takeAndPlug(ctx);
    ctx.check("hose in the filler", err.empty(), err);
    fuel::step(ctx.sim, &f.wk, 1.0, &cash);
    ctx.check("flowing", std::fabs(c.fuel - 25) < 1e-6, std::to_string(c.fuel));
    ctx.check("paying", cash == 1000 - 10, std::to_string(cash));
    ctx.check("the gauge", fuel::gauge(ctx.sim).find("PUMPING 5.0 L  $10") == 0, fuel::gauge(ctx.sim));
    std::string said;
    for (int i = 0; i < 40 && !ctx.sim.hose.full; i++) said = fuel::step(ctx.sim, &f.wk, 1.0, &cash);
    ctx.check("stops when full", ctx.sim.hose.full && c.fuel == c.spec->fuelCapacity && said.rfind("TANK FULL", 0) == 0, said);
    const double liters = c.spec->fuelCapacity - 20;
    ctx.check("charged per litre", std::abs(1000 - cash - (int)(liters * fuel::kPricePerLiter)) <= 1, std::to_string(cash));
    const int spent = cash;
    ctx.check("a truck's spare can tops up after the tank", c.jerry < 0 || c.jerry == c.spec->jerryCan);
    fuel::step(ctx.sim, &f.wk, 5.0, &cash);
    ctx.check("no more flows", cash == spent);
    doAt(ctx.sim, f.ia, f.wk, f.fill, IA_UNPLUG_HOSE);
    ctx.check("E takes it out, back in your hand", ctx.sim.hose.held());
    standBy(ctx.sim.world, c.frame().com, f.pump, f.wk, 0.8);
    doAt(ctx.sim, f.ia, f.wk, f.pump, IA_HANG_HOSE);
    ctx.check("hung back", !ctx.sim.hose.on());
    // Sandbox: free.
    c.fuel = 20;
    f.ia.money = -1;
    ctx.check("sandbox plugs in too", f.takeAndPlug(ctx).empty());
    fuel::step(ctx.sim, &f.wk, 2.0, nullptr);
    ctx.check("and pays nothing", std::fabs(c.fuel - 30) < 1e-6 && ctx.sim.hose.paid == 0);
}

SCENARIO(fuelNoMoney, "fuel/no-money", "With no money the pump refuses the hose; money running out stops the flow there") {
    Forecourt f(ctx);
    Car& c = *f.car;
    f.ia.money = 0;
    f.ia.update(ctx.sim, f.wk, eyeOf(f.wk), f.pump - eyeOf(f.wk), false, Ctx::kFrame);
    ctx.check("refused", f.ia.current.kind == IA_TAKE_HOSE && f.ia.current.blocked && f.ia.current.prompt.find("NO MONEY") != std::string::npos, f.ia.current.prompt);
    int cash = 9;
    f.ia.money = cash;
    ctx.check("plugs in with some", f.takeAndPlug(ctx).empty());
    std::string said;
    for (int i = 0; i < 600 && !ctx.sim.hose.broke; i++) {
        const std::string s = fuel::step(ctx.sim, &f.wk, 1.0 / 60, &cash);
        if (!s.empty()) said = s;
    }
    ctx.check("stops at what you can pay", ctx.sim.hose.broke && cash >= 0 && cash <= 1 && std::fabs(c.fuel - 24.5) < 0.6, std::to_string(cash) + " " + std::to_string(c.fuel));
    ctx.check("says so", said.rfind("OUT OF MONEY", 0) == 0, said);
    ctx.check("the gauge too", fuel::gauge(ctx.sim).rfind("OUT OF MONEY", 0) == 0);
}

SCENARIO(fuelDriveOff, "fuel/drive-off", "Driving off with the hose in pops it out: nothing explodes; walking off with it springs it back; getting in sends it back") {
    Forecourt f(ctx);
    Car& c = *f.car;
    ctx.check("plugged", f.takeAndPlug(ctx).empty());
    std::string said;
    for (int i = 0; i < 1200 && ctx.sim.hose.on(); i++) {
        ctx.sim.drive(true, false, false, false, false, 1.0 / 60);
        ctx.run(1.0 / 60);
        const std::string s = fuel::step(ctx.sim, nullptr, 1.0 / 60, nullptr);
        if (!s.empty()) said = s;
    }
    ctx.check("it popped out", !ctx.sim.hose.on() && said.rfind("THE HOSE POPPED OUT", 0) == 0, said);
    const V3 p = c.frame().com;
    ctx.check("the truck's whole", std::isfinite(p.x + p.y + p.z) && c.damage < 0.01);
    // Walking off.
    f.ia.money = -1;
    Forecourt g(ctx, 3, 60);
    doAt(ctx.sim, g.ia, g.wk, g.pump, IA_TAKE_HOSE);
    ctx.check("in your hand", ctx.sim.hose.held());
    ctx.sim.hose.hand = ctx.sim.hose.pump + V3(fuel::kHoseLen + fuel::kHoseSlack + 0.5, 0, 0);
    said = fuel::step(ctx.sim, &g.wk, 1.0 / 60, nullptr);
    ctx.check("too far: it goes back", !ctx.sim.hose.on() && said.find("SPRINGS BACK") != std::string::npos, said);
    doAt(ctx.sim, g.ia, g.wk, g.pump, IA_TAKE_HOSE);
    said = fuel::step(ctx.sim, nullptr, 1.0 / 60, nullptr);
    ctx.check("getting in: it goes back", !ctx.sim.hose.on(), said);
}

SCENARIO(fuelCanRefill, "fuel/can-refill", "An empty can at a pump: hold E, it's full for what 20 L cost; not without the money") {
    Forecourt f(ctx);
    Career career;
    career.on = true, career.cash = 100;
    career.grant(repairPartKey(PART_EMPTY_CAN));
    f.wk.hand = TrunkRef{TK_PART, PART_EMPTY_CAN};
    f.ia.money = 10;
    f.ia.update(ctx.sim, f.wk, eyeOf(f.wk), f.pump - eyeOf(f.wk), false, Ctx::kFrame);
    ctx.check("not enough cash", f.ia.current.kind == IA_FILL_CAN && f.ia.current.blocked, f.ia.current.prompt);
    f.ia.money = career.cash;
    doAt(ctx.sim, f.ia, f.wk, f.pump, IA_FILL_CAN);
    ctx.check("full in your hand", f.wk.hand == (TrunkRef{TK_PART, PART_FUEL_CAN}));
    ctx.check("paid for and counted", fuel::settle(f.ia, career) && career.cash == 100 - fuel::kCanFillCost && career.kit().parts[PART_FUEL_CAN] == 1 && career.kit().parts[PART_EMPTY_CAN] == 0);
}

SCENARIO(fuelShared, "fuel/shared", "A buddy's vehicle fills through its owner's machine, a full tank is told back, a hose shows on the others' machines and is forgotten") {
    Forecourt f(ctx);
    Car& mine = *f.car;
    mine.netId = 77, mine.remote = true;  // (seen from the buddy's machine)
    int cash = 500;
    f.ia.money = cash;
    ctx.check("plugged into a buddy's", f.takeAndPlug(ctx).empty());
    double told = 0;
    std::vector<std::string> lines;
    for (int i = 0; i < 120; i++) {
        fuel::step(ctx.sim, &f.wk, 1.0 / 60, &cash);
        for (const std::string& t : ctx.sim.hose.outbox) lines.push_back(t);
        ctx.sim.hose.outbox.clear();
    }
    bool saidHose = false;
    Sim owner;
    owner.setupEmpty();
    owner.addCar(vehicleCatalog()[2], V3(0, 0, 0), 0, V3(0.5, 0.5, 0.5));
    Car& theirs = owner.cars.back();
    theirs.netId = 77, theirs.fuel = 70;
    for (const std::string& t : lines) {
        double l = 0;
        if (fuel::takeFuel(owner, t, l)) told += l;
        saidHose |= t.rfind("hose ", 0) == 0;
    }
    ctx.check("the owner's tank fills", told > 7 && std::fabs(theirs.fuel - 70 - told) < 1e-6, std::to_string(told));
    ctx.check("the buddy pays", cash < 500 - 15, std::to_string(cash));
    ctx.check("the hose is told", saidHose);
    double l = 0;
    fuel::takeFuel(owner, "fuel 77 40", l);
    ctx.check("clamped at the tank", theirs.fuel == theirs.spec->fuelCapacity);
    ctx.check("and told back", !owner.hose.outbox.empty() && owner.hose.outbox.back() == "tankfull 77");
    fuel::onMessage(ctx.sim, 1, owner.hose.outbox.back(), 0);
    ctx.check("the buddy stops", ctx.sim.hose.full);
    ctx.check("a fuel line that's nonsense does nothing", fuel::takeFuel(owner, "fuel 77 1e9", l) && l == 0);
    // The others see it.
    fuel::onMessage(owner, 2, "hose 1.0 2.0 3.0 0", 10);
    ctx.check("heard", owner.remoteHoses.size() == 1 && owner.remoteHoses[0].player == 2);
    fuel::updateRemote(owner, 11, [](int, V3& h) { h = V3(4, 1, 3); return true; });
    ctx.check("drawn to their hand", owner.remoteHoses[0].shown && len(owner.remoteHoses[0].end - V3(4, 1, 3)) < 1e-9);
    fuel::updateRemote(owner, 11 + fuel::kHoseExpiry, [](int, V3&) { return false; });
    ctx.check("forgotten", owner.remoteHoses.empty());
    fuel::onMessage(owner, 2, "hose 1 2 3 0", 10);
    fuel::onMessage(owner, 2, "hose -", 10);
    ctx.check("taken away", owner.remoteHoses.empty());
    // The work line carries the new jobs.
    repairsync::Work w, back;
    w.job.kind = IA_POUR_CAN, w.carId = 77, w.hand = TrunkRef{TK_PART, PART_FUEL_CAN};
    ctx.check("the work message reads a can", repairsync::decode(repairsync::encode(w), back) && back.job.kind == IA_POUR_CAN && back.hand == w.hand);
}
