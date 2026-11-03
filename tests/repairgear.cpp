// Repairs and the gear for them (docs/repairs.md): the aimable markers pick the job you aim at even when several sit on one
// spot; every field repair the game offers can be done in a career with gear you can get (the starting toolbox, the station
// rack); old career saves (without the newer parts) still load.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "../src/career.h"
#include "../src/interact.h"
#include "../src/platform.h"
#include "../src/prompts.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

const VehicleSpec* specById(const char* id) {
    for (const VehicleSpec& v : vehicleCatalog())
        if (std::string(v.id) == id) return &v;
    return nullptr;
}

V3 hubAt(const Car& c, int w) { return (c.nodes[c.wheels[w].inner].p + c.nodes[c.wheels[w].outer].p) * 0.5; }

void standOn(const World& w, Walker& wk, const V3& q) {
    wk.p = V3(q.x, w.height(q.x, q.z), q.z);
    wk.v = V3();
    wk.active = true;
}

int leafBeam(const Car& c) {
    for (const SuspLink& l : c.links)
        if (l.kind == LINK_LEAF) return l.beam;
    return -1;
}

int armBeam(const Car& c) {
    for (const SuspLink& l : c.links)
        if ((l.kind == LINK_LOWER_ARM || l.kind == LINK_UPPER_ARM || l.kind == LINK_TRAILING) && c.beams[l.beam].type == BEAM_ARM) return l.beam;
    return -1;
}

}  // namespace

SCENARIO(repairAimClose, "repair/aim-close", "a CV axle, a tie rod and air up/down all at one front corner: each job gets its own marker apart from the others, and aiming at a marker picks that job") {
    Car& c = ctx.add(*specById("blazer"), -140, -50, 0);
    ctx.focus(c);
    c.handbrake = 1;
    ctx.run(1.0);
    int k = -1;
    for (int i = 0; i < (int)c.wheels.size(); i++)
        if (c.wheels[i].front && c.wheels[i].left) k = i;
    ctx.check("a front left corner", k >= 0);
    if (k < 0) return;
    Wheel& wh = c.wheels[k];
    wh.shaftBroken = true;
    for (int bi : wh.shafts) c.beams[bi].broken = true;
    wh.tieOffset = 0.05;
    c.tirePsi = c.spec->tirePsi - 10;
    c.wake();
    const Frame f = c.frame();
    const V3 fwd = norm(V3(f.fwd.x, 0, f.fwd.z)), left(fwd.z, 0, -fwd.x);
    Walker wk;
    standOn(ctx.sim.world, wk, hubAt(c, k) + left * 1.6 - fwd * 0.3);
    const V3 eye = wk.p + V3(0, 1.62, 0);
    Kit kit;  // (unlimited: sandbox)
    std::vector<InteractOption> nearby;
    PickOpts po;
    po.nearby = &nearby;
    Interact::pick(ctx.sim, wk, eye, norm(hubAt(c, k) - eye), nullptr, kit, nullptr, nullptr, &po);
    std::vector<InteractOption> mine;
    for (const InteractOption& o : nearby)
        if (o.car == 0 && (o.wheel == k || o.fix.wheel == k) && o.kind != IA_NONE) mine.push_back(o);
    bool cv = false, tie = false;
    for (const InteractOption& o : mine) cv |= o.kind == IA_FIX && o.fix.kind == Fix::CV_AXLE, tie |= o.kind == IA_FIX && o.fix.kind == Fix::TIE_ROD;
    ctx.check("the CV axle and the tie rod are both listed at the corner", cv && tie, std::to_string(mine.size()) + " jobs");
    bool apart = true;
    for (size_t i = 0; i < mine.size(); i++)
        for (size_t j = i + 1; j < mine.size(); j++)
            if (!mine[i].same(mine[j]) && len(mine[i].at - mine[j].at) < prompts::kSpreadNear - 1e-6) apart = false;
    ctx.check("no two of them on one spot", apart);
    int hits = 0, tries = 0;
    std::string miss;
    for (const InteractOption& o : mine) {
        if (len(o.at - (wk.p + V3(0, 1.0, 0))) > prompts::kReach) continue;
        tries++;
        const InteractOption got = Interact::pick(ctx.sim, wk, eye, norm(o.at - eye), nullptr, kit, nullptr, nullptr, nullptr);
        if (got.same(o)) hits++;
        else miss += o.prompt + " -> " + got.prompt + "; ";
    }
    ctx.check("aiming at each marker picks its own job", tries >= 3 && hits == tries, std::to_string(hits) + "/" + std::to_string(tries) + " " + miss);
    // The prompter shows one marker per job (none merged away).
    Interact ia;
    ia.update(ctx.sim, wk, eye, norm(hubAt(c, k) - eye), false, Ctx::kFrame);
    prompts::Prompter pr;
    const prompts::Focus fo = pr.update(ctx.sim, wk, ia, ia.nearby, eye, norm(hubAt(c, k) - eye));
    int fixes = 0;
    for (const prompts::Marker& m : fo.markers) fixes += m.kind == IA_FIX;
    ctx.check("a marker each for the CV axle and the tie rod", fixes >= 2, std::to_string(fixes));
}

SCENARIO(repairCareerGear, "repair/career-gear", "every field repair (docs/repairs.md) on a coil-sprung and a leaf-sprung truck can be done in a career: each step's tool and part is in the starting toolbox or on the station rack, and the repair then makes the part whole") {
    std::string doc;
    {
        std::ifstream in("docs/repairs.md");
        std::stringstream ss;
        ss << in.rdbuf();
        doc = ss.str();
    }
    ctx.check("docs/repairs.md is there", !doc.empty());
    for (int kind = Fix::CV_AXLE; kind <= Fix::SPRING; kind++) ctx.check(std::string("docs/repairs.md lists ") + jobName(kind), doc.find(jobName(kind)) != std::string::npos);
    // The rack has every tool and part.
    for (int t = 0; t < TOOL_COUNT; t++) {
        bool on = false;
        for (const ShopItem& it : stationRack()) on |= it.key == toolKey((Tool)t);
        ctx.check(std::string("the station sells ") + toolName((Tool)t), on);
    }
    for (int p = 0; p < PART_COUNT; p++) {
        bool on = false;
        for (const ShopItem& it : stationRack()) on |= it.key == repairPartKey((RepairPart)p);
        ctx.check(std::string("the station sells ") + repairPartName((RepairPart)p), on);
    }
    Career fresh;
    fresh.on = true;
    fresh.startFresh(DIFF_MEDIUM);
    const Kit start = fresh.kit();
    ctx.check("a new career starts with a lug wrench, a jack, wrenches, a socket set, a hammer, tire irons and a spare tire",
              start.has(TOOL_LUG_WRENCH) && start.has(TOOL_JACK) && start.has(TOOL_WRENCH) && start.has(TOOL_RATCHET) && start.has(TOOL_HAMMER) && start.has(TOOL_TIRE_IRONS) &&
                  start.hasPart(PART_TIRE) && !start.unlimited);

    const char* trucks[] = {"bronco", "blazer"};
    bool sawLeaf = false, sawCoil = false;
    for (const char* id : trucks) {
        const VehicleSpec* spec = specById(id);
        if (!spec) continue;
        // Each kind of damage on its own, on a fresh truck.
        const char* damages[] = {"cv", "tie", "tiebreak", "spring", "leaf", "armbend", "armbreak", "sway", "shaft", "tire", "flood"};
        for (const char* dmg : damages) {
            ctx.sim.cars.clear();
            Car& c = ctx.add(*spec, -140, -50, 0);
            ctx.run(0.3);
            int k = 0;
            for (int i = 0; i < (int)c.wheels.size(); i++)
                if (c.wheels[i].front && c.wheels[i].left) k = i;
            Wheel& wh = c.wheels[k];
            const std::string d = dmg;
            if (d == "cv") {
                if (wh.shafts.empty()) continue;
                wh.shaftBroken = true;
                for (int bi : wh.shafts) c.beams[bi].broken = true;
            } else if (d == "tie") wh.tieOffset = 0.05;
            else if (d == "tiebreak") {
                if (wh.tieRod < 0) continue;
                c.beams[wh.tieRod].broken = true;
            } else if (d == "spring") {
                if (wh.spring < 0 || c.beams[wh.spring].type != BEAM_SPRING) continue;
                c.beams[wh.spring].broken = true, sawCoil = true;
            } else if (d == "leaf") {
                const int lb = leafBeam(c);
                if (lb < 0) continue;
                c.beams[lb].broken = true, sawLeaf = true;
            } else if (d == "armbend" || d == "armbreak") {
                const int ab = armBeam(c);
                if (ab < 0) continue;
                if (d == "armbend") c.beams[ab].L0 = c.beams[ab].Lorig * 1.05;
                else c.beams[ab].broken = true;
            } else if (d == "sway") {
                if (c.axles.empty()) continue;
                c.axles[0].arbBend = 0.05;
            } else if (d == "shaft") {
                bool any = false;
                for (Axle& ax : c.axles)
                    if (ax.propShaft >= 0) c.beams[ax.propShaft].broken = true, any = true;
                if (!any) continue;
            } else if (d == "tire") wh.popped = true;
            else if (d == "flood") c.hydrolocked = true, c.engineDead = true;
            const std::vector<Fix> fx = fixesFor(c);
            ctx.check(std::string(id) + " " + d + ": the damage is a field repair", !fx.empty());
            for (const Fix& f : fx) {
                Career car;
                car.on = true;
                car.startFresh(DIFF_MEDIUM);
                car.cash = 100000;
                const std::vector<RepairStep> steps = repairSteps(f.kind, jobJacks(f.kind, f.wheel), jobWheelOff(c, f.kind, f.wheel));
                std::string why;
                bool ok = true;
                // Buy whatever the steps need that the toolbox doesn't have.
                for (const RepairStep& st : steps)
                    for (const ShopItem& it : stationRack()) {
                        const Kit kk = car.kit();
                        if ((st.tool >= 0 && it.key == toolKey((Tool)st.tool) && !kk.has((Tool)st.tool)) ||
                            (st.part >= 0 && it.key == repairPartKey((RepairPart)st.part) && !kk.hasPart((RepairPart)st.part)))
                            ok &= car.buyGear(it, why);
                    }
                for (Tool t : f.tools)
                    for (const ShopItem& it : stationRack())
                        if (it.key == toolKey(t) && !car.kit().has(t)) ok &= car.buyGear(it, why);
                const Kit kit = car.kit();
                std::string miss;
                for (const RepairStep& st : steps) {
                    const std::string m = stepMissing(st, kit, nullptr, -1, -1);
                    if (!m.empty()) miss += m + "; ";
                }
                ctx.check(std::string(id) + " " + d + " " + jobName(f.kind) + ": every step's gear can be had", ok && miss.empty(), miss + why);
                                bool listed = false;  // (a CV axle and a drive shaft can share a beam: one repair has made both whole)
                for (const Fix& g : fixesFor(c)) listed |= g.kind == f.kind && g.wheel == f.wheel && g.beam == f.beam && g.axle == f.axle;
                if (listed) ctx.check(std::string(id) + " " + d + " " + jobName(f.kind) + ": the repair makes it whole", applyFix(c, f));
                bool still = false;
                for (const Fix& g : fixesFor(c)) still |= g.kind == f.kind && g.wheel == f.wheel && g.beam == f.beam && g.axle == f.axle;
                ctx.check(std::string(id) + " " + d + " " + jobName(f.kind) + ": and it's no longer listed", !still);
            }
        }
    }
    ctx.check("a coilover and a leaf spring were both broken somewhere", sawCoil && sawLeaf);
}

SCENARIO(repairOldSave, "repair/old-save", "a career saved before the spring part and the starting toolbox loads: its tools and spares as they were (nothing added), and a save with a spring in stock loads too") {
    makeDir("build/test-career");
    const std::string path = "build/test-career/old-repair-career.txt";
    {
        std::ofstream f(path);
        f << "cash 900\nown TOOL:LUG WRENCH\nown TOOL:JACK\nstock 2 REPAIR:CV AXLE\nstock 1 REPAIR:ENGINE OIL\n";
    }
    Career c;
    c.on = true;
    ctx.check("an old career loads", c.load(path));
    const Kit k = c.kit();
    ctx.check("its tools and spares as saved", k.has(TOOL_LUG_WRENCH) && k.has(TOOL_JACK) && !k.has(TOOL_TIRE_IRONS) && k.parts[PART_CV_AXLE] == 2 && k.parts[PART_ENGINE_OIL] == 1 &&
                                                   k.parts[PART_SPRING] == 0 && c.cash == 900);
    c.grant(repairPartKey(PART_SPRING));
    ctx.check("saved with a spring", c.save(path));
    Career d;
    d.on = true;
    ctx.check("and loads back with it", d.load(path) && d.kit().parts[PART_SPRING] == 1);
    std::remove(path.c_str());
}
