// The garage shop (garageshop.h): its categories and parts come from career.h and mods.h; the keys and the mouse move
// through it; buying, fitting, trying a part on the truck and closing leave the truck, the cash and the garage right;
// sandbox has everything free, a career only what it owns and what the stations sell.
#include <cmath>
#include <set>

#include "../src/garageshop.h"
#include "../src/modelkit.h"
#include "harness.h"

using namespace shop;

namespace {

int vehicleIndex(const char* id) {
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        if (std::string(vehicleCatalog()[v].id) == id) return v;
    return -1;
}

int catIndex(int slot) {
    for (int i = 0; i < (int)categories().size(); i++)
        if (categories()[i].group == G_MOD && categories()[i].slot == slot) return i;
    return -1;
}

int firstWith(const Shop& s, State st) {
    for (int i = 0; i < (int)s.list.size(); i++)
        if (s.list[i].state == st) return i;
    return -1;
}

Career careerWith(int cash) {
    Career c;
    c.on = true;
    c.startFresh();
    c.cash = cash;
    return c;
}

}  // namespace

SCENARIO(shopCategories, "shop/categories", "The categories are every mod slot and every kind of rack item; each lists exactly the options and rack items there are") {
    const auto& cats = categories();
    int mods = 0;
    std::set<int> rackSeen;
    const int v = vehicleIndex("bronco");
    Career sandbox;
    for (const Category& c : cats) {
        const std::vector<Item> list = items(c, v, ModSet(), sandbox);
        if (c.group == G_MOD) {
            mods++;
            ctx.check("mod slot " + c.name + ": every option listed", (int)list.size() == (int)modOptions(v, c.slot).size());
        } else if (c.group == G_PAINT) {
            ctx.check("the PAINT page has its rows", (int)list.size() == PAINT_ROWS);
        } else if (c.group == G_VEHICLE) {
            ctx.check("the SWAP VEHICLE page is filled by the shop, from what it was given", list.empty());
        } else {
            ctx.check(c.name + ": has items", !list.empty());
            for (const Item& it : list) rackSeen.insert(it.rack);
        }
        ctx.check(c.name + ": has a name", !c.name.empty());
    }
    ctx.check("one category per mod slot", mods == MOD_SLOTS);
    ctx.check("every rack item is in one category", (int)rackSeen.size() == (int)stationRack().size());
}

SCENARIO(shopModels, "shop/models", "Every part of every vehicle, and every rack item, has a model to show on the turntable, sized to be seen") {
    Career sandbox;
    bool all = true, sized = true;
    std::string missing;
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        for (const Category& c : categories())
            for (const Item& it : items(c, v, ModSet(), sandbox)) {
                if (it.group == G_PAINT) continue;  // (the truck is its own picture)
                const Display d = displayFor(it, v, ModSet());
                if (!mk::findModel(d.id)) all = false, missing = d.id + " for " + it.name;
                if (!(d.size > 0.3 && d.size < 2.5)) sized = false;
            }
    ctx.check("every item's model exists", all, missing);
    ctx.check("every item shows 0.3 to 2.5 m big", sized);
    V3 lo, hi;
    Display d;
    d.id = "crate_engine_v8";
    modelBounds(d, lo, hi);
    ctx.check("a model's bounds are real", hi.y > lo.y + 0.2 && hi.x > lo.x + 0.2);
}

SCENARIO(shopNav, "shop/nav", "Keys: down the categories, into one onto the fitted part, through it (wrapping), Q/E to the next one, back, and Esc closes; the mouse picks rows") {
    Career sandbox;
    Shop s;
    const int v = vehicleIndex("bronco");
    ModSet fitted;
    fitted.pick[MOD_ENGINE] = 2;
    s.begin(v, fitted, sandbox, false, true);
    ctx.check("opens on the categories", s.open && s.focus == F_CATS && s.cat == 0);
    ctx.check("no part focused on the categories", s.item() == nullptr);
    s.key(K_DOWN, sandbox);
    ctx.check("down: the next category", s.cat == 1);
    s.key(K_UP, sandbox);
    s.key(K_UP, sandbox);
    ctx.check("up from the top wraps to the last", s.cat == (int)categories().size() - 1);
    s.key(K_DOWN, sandbox);
    ctx.check("and back to the first", s.cat == 0);
    s.key(K_ENTER, sandbox);
    ctx.check("enter: into the parts, on the fitted one", s.focus == F_ITEMS && s.item() && s.item()->option == 2 && s.item()->state == ST_FITTED);
    const int n = (int)s.list.size();
    for (int i = 0; i < n; i++) s.key(K_DOWN, sandbox);
    ctx.check("down through all of them wraps round", s.sel == 2);
    s.key(K_E, sandbox);
    ctx.check("E: the next category (suspension), its fitted part", s.cat == 1 && s.focus == F_ITEMS && s.item() && s.item()->state == ST_FITTED);
    s.key(K_LEFT, sandbox);
    ctx.check("left: back to the categories", s.focus == F_CATS && s.open);
    s.key(K_RIGHT, sandbox);
    ctx.check("right: into the parts", s.focus == F_ITEMS);
    s.key(K_ESC, sandbox);
    ctx.check("esc: back to the categories, still open", s.focus == F_CATS && s.open);
    const Result r = s.key(K_ESC, sandbox);
    ctx.check("esc on the categories closes", !s.open && r.close);

    // The mouse: rows the painter put down.
    s.begin(v, fitted, sandbox, false, true);
    s.hits = {{0, 0, 100, 20, 0, 0}, {0, 20, 100, 20, 0, 1}, {0, 40, 100, 20, 0, 2}, {120, 0, 100, 20, 1, 0}, {120, 20, 100, 20, 1, 1}};
    int col = -1, row = -1;
    ctx.check("hitAt finds a row", s.hitAt(50, 45, col, row) && col == 0 && row == 2);
    ctx.check("hitAt misses between", !s.hitAt(110, 5, col, row));
    s.hover(0, 2);
    ctx.check("hover a category focuses it", s.cat == 2 && s.focus == F_CATS);
    s.click(0, 0, sandbox);
    ctx.check("click a category opens it", s.cat == 0 && s.focus == F_ITEMS);
    s.hover(1, 0);
    ctx.check("hover a part focuses it", s.sel == 0);
    const ModSet before = s.fitted;
    s.click(1, 1, sandbox);
    ctx.check("click another part only focuses it", s.sel == 1 && s.fitted.pick[MOD_ENGINE] == before.pick[MOD_ENGINE]);
    const Result f = s.click(1, 1, sandbox);
    ctx.check("click the focused part again fits it", f.fitted && s.fitted.pick[MOD_ENGINE] == 1);
    s.turn(0.5, 2.0);
    ctx.check("the turntable turns, its tilt stays within limits", std::fabs(s.spin - 0.5) < 1e-9 && s.tilt <= 1.2);
    for (int i = 0; i < 40; i++) s.zoomBy(2);
    ctx.check("zoom stays within limits", s.zoom <= 2.2 && s.zoom >= 0.5);
}

SCENARIO(shopSandbox, "shop/sandbox", "Sandbox: every part is yours and free, fitted anywhere; the rack's gear is already in the truck; no cash moves") {
    Career sandbox;
    sandbox.cash = 777;
    Shop s;
    const int v = vehicleIndex("tacoma");
    s.begin(v, ModSet(), sandbox, false, false);  // (no station: sandbox doesn't care)
    bool noneLocked = true;
    for (int i = 0; i < (int)categories().size(); i++) {
        s.setCat(i);
        for (const Item& it : s.list) noneLocked &= it.state == ST_FITTED || it.state == ST_OWNED || it.state == ST_GEAR;
    }
    ctx.check("nothing is locked or for sale", noneLocked);
    s.setCat(catIndex(MOD_TIRES));
    s.key(K_ENTER, sandbox);
    s.key(K_DOWN, sandbox);
    const int want = s.item()->option;
    const Result r = s.key(K_ENTER, sandbox);
    ctx.check("fits away from a station", r.fitted && s.fitted.pick[MOD_TIRES] == want);
    ctx.check("no cash moved, nothing saved to a career", sandbox.cash == 777 && !r.saveCareer);
    ctx.check("its label says free", s.list[0].state == ST_OWNED && (s.setSel(0), s.actionLabel(sandbox) == "FIT  (FREE)"));
    s.key(K_ESC, sandbox);
    s.key(K_DOWN, sandbox);  // (wraps to the first rack category after the mod slots? walk to one)
    for (int i = 0; i < (int)categories().size() && s.category().group != G_GEAR; i++) s.key(K_DOWN, sandbox);
    s.key(K_ENTER, sandbox);
    const Result g = s.key(K_ENTER, sandbox);
    ctx.check("gear: nothing to buy in sandbox", !g.saveCareer && sandbox.cash == 777 && g.toast.find("SANDBOX") != std::string::npos);
    s.key(K_R, sandbox);
    ctx.check("R on gear takes nothing off", s.fitted.pick[MOD_TIRES] == want);
}

SCENARIO(shopCareer, "shop/career", "Career: buy and fit what a station sells (cash goes, the part is owned), not what you can't afford or what isn't sold, and only at a station; the rack's gear is bought one at a time") {
    Career c = careerWith(30000);
    const int v = vehicleIndex("bronco");
    Shop s;
    s.begin(v, ModSet(), c, true, true);
    s.setCat(catIndex(MOD_ENGINE));
    s.key(K_ENTER, c);
    const int sale = firstWith(s, ST_FOR_SALE), locked = firstWith(s, ST_LOCKED);
    ctx.check("the engines include some for sale and some locked", sale >= 0 && locked >= 0);
    if (sale < 0 || locked < 0) return;
    // Locked: nothing happens, and it says where it's found (if anywhere).
    s.setSel(locked);
    const Result l = s.key(K_ENTER, c);
    ctx.check("a locked part isn't sold", !l.fitted && c.cash == 30000 && l.toast.rfind("NOT SOLD", 0) == 0);
    // Too dear.
    s.setSel(sale);
    const int price = s.item()->price, option = s.item()->option;
    c.cash = price - 1;
    Result r = s.key(K_ENTER, c);
    ctx.check("short of cash: not bought", !r.fitted && c.cash == price - 1 && !c.owns(v, MOD_ENGINE, option) && s.actionLabel(c).rfind("NEED", 0) == 0);
    // Away from a station.
    c.cash = 30000;
    s.atStation = false;
    r = s.key(K_ENTER, c);
    ctx.check("away from a station: not bought", !r.fitted && c.cash == 30000);
    s.atStation = true;
    r = s.key(K_ENTER, c);
    ctx.check("bought and fitted", r.fitted && r.saveCareer && s.fitted.pick[MOD_ENGINE] == option);
    ctx.check("the price came off the cash, the part is owned", c.cash == 30000 - price && c.owns(v, MOD_ENGINE, option));
    ctx.check("the list says fitted", s.item() && s.item()->state == ST_FITTED);
    // The factory part back, and the bought one is still owned (fits again free).
    r = s.key(K_R, c);
    ctx.check("R: the factory engine back on", r.fitted && s.fitted.pick[MOD_ENGINE] == 0);
    s.setSel(sale);
    ctx.check("the bought part is owned now", s.item()->state == ST_OWNED);
    r = s.key(K_ENTER, c);
    ctx.check("fitting it again costs nothing", r.fitted && !r.saveCareer && c.cash == 30000 - price);
    // Rack gear: a tool once, repair parts as many as you like.
    for (int i = 0; i < (int)categories().size(); i++) {
        const Category& cat = categories()[i];
        if (cat.group != G_GEAR) continue;
        s.focus = F_CATS;
        s.setCat(i);
        s.key(K_ENTER, c);
        const Item first = s.list[0];
        const int cash = c.cash;
        r = s.key(K_ENTER, c);
        if (first.tool && first.carried > 0) {
            ctx.check(cat.name + ": a tool you have isn't bought again", !r.saveCareer && c.cash == cash);
        } else {
            ctx.check(cat.name + ": bought one", r.saveCareer && c.cash == cash - first.price && s.list[0].carried == std::max(0, first.carried) + 1);
            if (!first.tool) {
                s.key(K_ENTER, c);
                ctx.check(cat.name + ": and another", s.list[0].carried == std::max(0, first.carried) + 2);
            }
        }
    }
}

SCENARIO(shopPreview, "shop/preview", "Trying parts on rebuilds the truck on them and follows the list; leaving the list or closing puts the fitted parts back; buying while trying keeps it on; the garage and cash only change on a buy or fit") {
    const int v = vehicleIndex("bronco");
    Car& car = ctx.add(vehicleCatalog()[v], -60, -50, 0);
    ctx.sim.player = 0;
    (void)car;
    ctx.run(0.5);
    Career c = careerWith(30000);
    Shop s;
    const ModSet start;
    s.begin(v, start, c, true, true);
    auto spec = [&]() { return ctx.sim.cars[ctx.sim.player].spec; };
    ctx.check("opening changes nothing", !liftSync(ctx.sim, s) && spec() == &vehicleCatalog()[v]);
    s.setCat(catIndex(MOD_SUSPENSION));
    s.key(K_ENTER, c);
    s.key(K_DOWN, c);
    const int tried = s.item()->option;
    s.key(K_T, c);
    ctx.check("T: trying it on", s.previewing && s.shown().pick[MOD_SUSPENSION] == tried);
    // Not built yet: waits.
    ctx.check("waits for the visual", !liftSync(ctx.sim, s, [](const VehicleSpec&) { return false; }) && spec() == &vehicleCatalog()[v]);
    ctx.check("the truck wears it", liftSync(ctx.sim, s) && spec() == &tunedSpec(v, s.shown()));
    ctx.check("and once is enough", !liftSync(ctx.sim, s));
    ctx.run(0.5);
    ctx.check("the truck stands on it", ctx.sim.cars[ctx.sim.player].frame().com.y > ctx.sim.world.height(-60, -50));
    s.key(K_DOWN, c);
    const int next = s.item()->option;
    liftSync(ctx.sim, s);
    ctx.check("moving on tries the next one", spec() == &tunedSpec(v, s.shown()) && s.shown().pick[MOD_SUSPENSION] == next);
    s.key(K_LEFT, c);
    ctx.check("leaving the list stops trying", !s.previewing && s.shown().pick[MOD_SUSPENSION] == 0);
    liftSync(ctx.sim, s);
    ctx.check("the fitted parts are back on", spec() == &vehicleCatalog()[v]);
    // Try, then close.
    s.key(K_ENTER, c);
    s.key(K_DOWN, c);
    s.key(K_T, c);
    liftSync(ctx.sim, s);
    const Result r = s.key(K_G, c);
    ctx.check("G closes", r.close && !s.open);
    liftSync(ctx.sim, s);
    ctx.check("closed: the fitted parts are back on", spec() == &vehicleCatalog()[v] && s.fitted.stock() && c.cash == 30000);
    // Try, then buy it while it's on: it stays on (no rebuild), now fitted.
    s.begin(v, start, c, true, true);
    s.setCat(catIndex(MOD_SUSPENSION));
    s.key(K_ENTER, c);
    int sale = firstWith(s, ST_FOR_SALE);
    ctx.check("a suspension for sale", sale >= 0);
    if (sale < 0) return;
    s.setSel(sale);
    s.key(K_T, c);
    liftSync(ctx.sim, s);
    const VehicleSpec* wearing = spec();
    const Result b = s.key(K_ENTER, c);
    ctx.check("bought while trying it on", b.fitted && s.fitted.pick[MOD_SUSPENSION] == s.list[sale].option && !s.previewing);
    ctx.check("still on: nothing to rebuild", !liftSync(ctx.sim, s) && spec() == wearing && wearing == &tunedSpec(v, s.fitted));
    s.close();
    ctx.check("closing keeps what was bought", !liftSync(ctx.sim, s) && spec() == &tunedSpec(v, s.fitted));
    // Gear can't be tried on.
    s.begin(v, s.fitted, c, true, true);
    for (int i = 0; i < (int)categories().size(); i++)
        if (categories()[i].group == G_GEAR) {
            s.setCat(i);
            break;
        }
    s.key(K_ENTER, c);
    s.key(K_T, c);
    ctx.check("gear: nothing to try on", !s.previewing && !liftSync(ctx.sim, s));
}

SCENARIO(shopStats, "shop/stats", "The card's numbers are the real ones: power from the tuned spec, weight always shown, a bigger tire shows its size and clearance") {
    const int v = vehicleIndex("bronco");
    ModSet with;
    with.pick[MOD_ENGINE] = 3;  // (the Raptor V6)
    const std::vector<Stat> st = compare(v, ModSet(), with);
    bool power = false, weight = false;
    for (const Stat& s : st) {
        if (s.label == "POWER")
            power = std::fabs(s.now - peakPower(vehicleCatalog()[v]) / 745.7) < 1e-6 && std::fabs(s.with - peakPower(tunedSpec(v, with)) / 745.7) < 1e-6 && s.with > s.now;
        if (s.label == "WEIGHT") weight = s.with > s.now;
    }
    ctx.check("power: fitted and with it, from the specs", power);
    ctx.check("weight: the heavier engine shows", weight);
    ModSet lift;
    lift.pick[MOD_SUSPENSION] = 2;  // (3 in lift and 37s)
    bool tire = false, clear = false;
    for (const Stat& s : compare(v, ModSet(), lift)) {
        if (s.label == "TIRE") tire = s.with > s.now;
        if (s.label == "CLEARANCE") clear = s.with > s.now;
    }
    ctx.check("the lift kit shows taller tires and more clearance", tire && clear);
    const std::vector<Stat> same = compare(v, ModSet(), ModSet());
    ctx.check("the part that's fitted shows only power and weight", same.size() == 2);
}

namespace {
int swapCat() {
    for (int i = 0; i < (int)categories().size(); i++)
        if (categories()[i].group == G_VEHICLE) return i;
    return -1;
}
}  // namespace

SCENARIO(shopSwapSandbox, "shop/swap-sandbox", "Sandbox: the SWAP VEHICLE page offers every listed vehicle, marks the one you're in, and ENTER on another picks it and closes the shop") {
    Career c;  // (sandbox)
    Shop s;
    const int here = listedVehicles()[1];
    for (int v : listedVehicles()) s.swaps.push_back({v, -1, v == here});
    s.begin(here, ModSet(), c, false, false);
    ctx.check("the page is in the categories", swapCat() >= 0 && std::string(categories()[swapCat()].name) == "SWAP VEHICLE");
    s.setCat(swapCat());
    s.key(K_ENTER, c);
    ctx.check("opens onto the list: every listed vehicle", s.focus == F_ITEMS && s.list.size() == listedVehicles().size());
    int cur = 0;
    for (const Item& it : s.list) cur += it.state == ST_FITTED;
    ctx.check("exactly one is marked as the one you're in", cur == 1 && s.list[1].state == ST_FITTED);
    ctx.check("no truck to preview, nothing to try on", s.item() == nullptr);
    s.setSel(1);
    Result r = s.key(K_ENTER, c);
    ctx.check("ENTER on your own vehicle does nothing but say so", r.swapVehicle < 0 && !r.close && !r.toast.empty());
    s.setSel(0);
    r = s.key(K_ENTER, c);
    ctx.check("ENTER on another: its catalog index, no career slot, shop closes", r.swapVehicle == listedVehicles()[0] && r.swapSlot == -1 && r.close);
    ctx.check("a click picks too (the second click, on the focused row)", [&] {
        Shop m = s;
        m.focus = F_CATS;
        m.setCat(swapCat());
        m.click(0, swapCat(), c);
        const Result first = m.click(1, 2, c);  // (a click on another row only focuses it)
        const Result q = m.click(1, 2, c);      // (on the focused row it picks)
        return first.swapVehicle < 0 && q.swapVehicle == listedVehicles()[2];
    }());
}

SCENARIO(shopSwapCareer, "shop/swap-career", "Career: the SWAP VEHICLE page lists the vehicles you own here (what main.cpp gave it), only at a station") {
    Career c = careerWith(1000);
    Shop s;
    const int a = listedVehicles()[0], b = listedVehicles()[2];
    s.swaps.push_back({a, 0, true});
    s.swaps.push_back({b, 1, false});
    s.begin(a, ModSet(), c, true, true);
    s.setCat(swapCat());
    s.key(K_ENTER, c);
    ctx.check("only the two you own, not the whole catalog", s.list.size() == 2 && s.list[0].state == ST_FITTED && s.list[1].state == ST_OWNED);
    s.setSel(1);
    Result r = s.key(K_ENTER, c);
    ctx.check("at a station: the pick, with its profile slot, and the shop closes", r.swapVehicle == b && r.swapSlot == 1 && r.close);
    Shop away;
    away.swaps = s.swaps;
    away.begin(a, ModSet(), c, true, false);
    away.setCat(swapCat());
    away.key(K_ENTER, c);
    away.setSel(1);
    r = away.key(K_ENTER, c);
    ctx.check("away from a station: refused, shop stays open", r.swapVehicle < 0 && !r.close && r.toast.find("STATION") != std::string::npos);
    Shop lone;
    lone.swaps.push_back({a, 0, true});
    lone.begin(a, ModSet(), c, true, true);
    lone.setCat(swapCat());
    lone.key(K_ENTER, c);
    ctx.check("owning only the one you're in: it says so", lone.list.size() == 1 && lone.key(K_ENTER, c).swapVehicle < 0);
}
