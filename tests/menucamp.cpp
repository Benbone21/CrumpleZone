// The main menu camp as data (menucamp.h): its settings ride in "#menu" lines of a level file and come back from a save and a load unchanged.
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
