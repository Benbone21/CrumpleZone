// The main menu camp as data (menucamp.h): its settings ride in "#menu" lines of a level file and come back from a save and a load unchanged.
#include "../src/editcore.h"
#include "../src/level.h"
#include "../src/menucamp.h"
#include "../src/vehicles.h"
#include "harness.h"

using namespace menucamp;

namespace {
Level sample() {
    Level lv;
    lv.name = "MENU CAMP";
    lv.map = MAP_WILDS;
    lv.timeOfDay = 17.55;
    LevelObject fire;
    fire.kind = OBJ_MODEL, fire.name = "campfire", fire.p = V3(80, 0, 40), fire.size = V3(1, 1, 1);
    lv.objects.push_back(fire);
    LevelObject rock;
    rock.kind = OBJ_ROCK, rock.p = V3(82, 0, 41), rock.size = V3(1, 0.8, 1), rock.top = 1.2, rock.seed = 7, rock.col[0] = 1.0f;
    lv.objects.push_back(rock);
    lv.removed.push_back(V3(75, 2, 33));
    lv.heights[1234] = 2.5f;
    Meta m;
    m.veh[0] = {83.5, 41.25, 169.0, 1};
    m.veh[1] = {70.0, 31.0, -20.5, 2};
    m.veh[2] = {75.0, 45.5, 90.0, 0};
    m.cam = {78.0, 2.6, 52.0, 77.5, 2.2, 35.0, 0.5};
    m.look = 18.55;
    for (int k = 0; k < 3; k++) addVehicle(lv, m.veh[k]);
    setMeta(lv, m);
    return lv;
}
}  // namespace

SCENARIO(campRoundTrip, "menucamp/roundtrip", "The camp level with its menu settings saves and loads back equal: the same text, the same objects, the same settings") {
    const Level a = sample();
    const std::string t1 = saveLevelText(a);
    Level b;
    std::string err;
    ctx.check("it parses", parseLevelText(t1, b, &err));
    ctx.check("it is still the menu camp", isCamp(b));
    const std::string t2 = saveLevelText(b);
    ctx.check("load -> save gives the same text", t1 == t2);
    ctx.check("the settings are equal", sameMeta(metaOf(a), metaOf(b)));
    ctx.check("so are the objects, the cleared ground and the ground edits", a.objects.size() == b.objects.size() && a.removed.size() == b.removed.size() && a.heights == b.heights);
    V3 f;
    ctx.check("the fire is the level's campfire", fireOf(b, f) && f.x == 80 && f.z == 40);
    ctx.check("a level that isn't the camp isn't one", !isCamp(Level()));
}

SCENARIO(campSlots, "menucamp/slots", "A vehicle slot changed in the settings survives a save and a load, and always names a listed vehicle") {
    Level a = sample();
    Meta m = metaOf(a);
    ctx.check("slot 0 starts as the second listed vehicle", m.veh[0].slot == 1);
    m.veh[0].slot = 3, m.veh[1].x = 71.5;
    setMeta(a, m);
    Level b;
    parseLevelText(saveLevelText(a), b);
    const Meta r = metaOf(b);
    ctx.check("the slot change persists", r.veh[0].slot == 3 && r.veh[1].x == 71.5 && sameMeta(m, r));
    const int n = (int)listedVehicles().size();
    m.veh[2].slot = n + 4;
    setMeta(a, m);
    ctx.check("a slot past the list wraps into it", metaOf(a).veh[2].slot == 4 % n);
    const Meta d = metaOf(Level());
    bool ok = true;
    for (int k = 0; k < 3; k++) ok = ok && d.veh[k].slot >= 0 && d.veh[k].slot < n;
    ctx.check("a level with no menu lines has the default slots", ok);
}

SCENARIO(campVehicleObjects, "menucamp/vehicle-objects", "The parked vehicles are placed objects: the settings follow them when they are moved, and a deleted one is not parked") {
    Level a = sample();
    Meta m = metaOf(a);
    ctx.check("three vehicles, in the level's objects", m.vehicles == 3 && m.veh[0].x == 83.5 && m.veh[1].slot == 2);
    int k0 = -1;
    for (int i = 0; i < (int)a.objects.size(); i++)
        if (a.objects[i].name == kVehicleModel && k0 < 0) k0 = i;
    a.objects[k0].p.x = 90, a.objects[k0].yaw = 1.0;
    m = metaOf(a);
    ctx.check("moving and turning the object moves the settings", m.veh[0].x == 90 && std::fabs(m.veh[0].yawDeg - 57.2958) < 0.01);
    a.objects.erase(a.objects.begin() + k0);
    ctx.check("a deleted vehicle isn't counted", metaOf(a).vehicles == 2);
    ctx.check("...and can be added again", addVehicle(a, m.veh[0]) == 2 && metaOf(a).vehicles == 3);
    Level old;
    old.menuLines = {"#menu scene 1", "#menu veh 0 80 41 90 1", "#menu veh 1 70 31 0 2", "#menu veh 2 75 45 45 0"};
    ctx.check("an older file keeps its vehicles in lines", metaOf(old).vehicles == 3 && metaOf(old).veh[1].x == 70);
    upgrade(old);
    ctx.check("upgrading turns them into objects with the same places", metaOf(old).vehicles == 3 && metaOf(old).veh[1].x == 70 && metaOf(old).veh[2].slot == 0);
}

SCENARIO(campPickMove, "menucamp/pick-move", "In the editor a click on a vehicle's body picks it, moving it moves the object and its drawn stand-in, and it saves and loads where it was put") {
    World w(MAP_WILDS);
    Level lv = sample();
    for (LevelObject& o : lv.objects)
        if (o.name == kVehicleModel) o.p.x = 80 + 3 * (&o - &lv.objects[0]), o.p.z = 40;  // (spread out)
    EditCore ed;
    ed.begin(w, lv);
    int placed = 0;
    for (const Placed& pl : w.models) placed += pl.id == kVehicleModel;
    ctx.check("the stand-ins are in the editor's world", placed == 3);
    int k = -1;
    for (int i = 0; i < (int)ed.level.objects.size(); i++)
        if (ed.level.objects[i].name == kVehicleModel) {
            k = i;
            break;
        }
    const V3 at(ed.level.objects[k].p.x, w.height(ed.level.objects[k].p.x, ed.level.objects[k].p.z) + 0.9, ed.level.objects[k].p.z);
    const V3 from = at + V3(-9, 6, 8);
    V3 d = norm(at - from);
    V3 gp;
    const double hit = ed.rayGround(from, d, 2000, gp) ? len(gp - from) : 1e9;
    const EditPick p = ed.pick(from, d, hit, true, false);
    ctx.check("a ray at the body picks that vehicle", p.kind == EditPick::OBJECT && p.index == k);
    const V3 before = ed.level.objects[k].p;
    ed.moveObjects({k}, 4.0, -3.0);
    ctx.check("moving it moves the object", std::fabs(ed.level.objects[k].p.x - before.x - 4) < 0.01 && std::fabs(ed.level.objects[k].p.z - before.z + 3) < 0.01);
    bool drawn = false;
    for (const Placed& pl : w.models) drawn |= pl.id == kVehicleModel && std::fabs(pl.p.x - before.x - 4) < 0.05 && std::fabs(pl.p.y - w.height(pl.p.x, pl.p.z)) < 0.05;
    ctx.check("its drawn stand-in follows, standing on the ground", drawn);
    Level back;
    parseLevelText(saveLevelText(ed.level), back);
    const Meta m = metaOf(back);
    int idx = 0;
    for (int i = 0; i < k; i++) idx += ed.level.objects[i].name == kVehicleModel;
    ctx.check("saved and loaded, the vehicle is where it was put", std::fabs(m.veh[idx].x - (before.x + 4)) < 0.01 && std::fabs(m.veh[idx].z - (before.z - 3)) < 0.01);
}
