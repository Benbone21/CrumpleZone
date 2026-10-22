#include "menucamp.h"

#include <cmath>
#include <cstdio>
#include <sstream>

#include "vehicles.h"

namespace menucamp {

bool isCamp(const Level& lv) {
    for (const std::string& l : lv.menuLines)
        if (l.rfind("#menu scene", 0) == 0) return true;
    return false;
}

Meta metaOf(const Level& lv) {
    Meta m;
    const int dflt[3] = {1, 2, 0};
    for (int k = 0; k < 3; k++) m.veh[k].slot = dflt[k];
    int found = 0;
    for (const LevelObject& o : lv.objects)  // the vehicles: the level's stand-ins, in order
        if (o.kind == OBJ_MODEL && o.name == kVehicleModel && found < 3) {
            Vehicle& v = m.veh[found++];
            v.x = o.p.x, v.z = o.p.z, v.yawDeg = o.yaw * 180 / M_PI, v.slot = o.sub;
        }
    int legacy = 0;
    for (const std::string& line : lv.menuLines) {
        std::istringstream in(line);
        std::string tag, key;
        in >> tag >> key;
        if (key == "veh") {  // (an older file: the vehicles in lines)
            int i = -1;
            Vehicle v;
            if (in >> i >> v.x >> v.z >> v.yawDeg >> v.slot && i >= 0 && i < 3 && !found) m.veh[i] = v, legacy++;
        } else if (key == "cam") {
            Camera c;
            if (in >> c.ex >> c.ey >> c.ez >> c.tx >> c.ty >> c.tz >> c.drift) m.cam = c;
        } else if (key == "look") {
            in >> m.look;
        }
    }
    m.vehicles = found ? found : legacy ? 3 : 0;
    const int n = (int)listedVehicles().size();
    for (Vehicle& v : m.veh) {
        v.slot = n > 0 ? ((v.slot % n) + n) % n : 0;
        v.yawDeg = std::fmod(std::fmod(v.yawDeg, 360.0) + 360.0, 360.0);
    }
    return m;
}

void setMeta(Level& lv, const Meta& m) {
    lv.menuLines.clear();
    char b[256];
    lv.menuLines.push_back("#menu scene 1");
    std::snprintf(b, sizeof b, "#menu cam %.3f %.3f %.3f %.3f %.3f %.3f %.2f", m.cam.ex, m.cam.ey, m.cam.ez, m.cam.tx, m.cam.ty, m.cam.tz, m.cam.drift);
    lv.menuLines.push_back(b);
    std::snprintf(b, sizeof b, "#menu look %.2f", m.look);
    lv.menuLines.push_back(b);
    // The vehicles are objects: the k-th stand-in takes its place, kind and way (a missing one is added).
    int k = 0;
    for (LevelObject& o : lv.objects)
        if (o.kind == OBJ_MODEL && o.name == kVehicleModel && k < 3) {
            const Vehicle& v = m.veh[k++];
            o.p.x = v.x, o.p.z = v.z, o.yaw = v.yawDeg * M_PI / 180, o.sub = v.slot;
        }
}

int addVehicle(Level& lv, const Vehicle& v) {
    int n = 0;
    for (const LevelObject& o : lv.objects) n += o.kind == OBJ_MODEL && o.name == kVehicleModel;
    if (n >= 3) return -1;
    LevelObject o;
    o.kind = OBJ_MODEL, o.name = kVehicleModel, o.size = V3(1, 1, 1);
    o.p = V3(v.x, 0, v.z), o.yaw = v.yawDeg * M_PI / 180, o.sub = v.slot;
    lv.objects.push_back(o);
    return n;
}

void upgrade(Level& lv) {
    int have = 0;
    for (const LevelObject& o : lv.objects) have += o.kind == OBJ_MODEL && o.name == kVehicleModel;
    if (have) return;
    const Meta m = metaOf(lv);  // (an older file keeps its vehicles in lines)
    for (int k = 0; k < m.vehicles; k++) addVehicle(lv, m.veh[k]);
    setMeta(lv, m);
}

bool sameMeta(const Meta& a, const Meta& b) {
    auto eq = [](double x, double y) { return std::fabs(x - y) < 1e-3; };
    for (int k = 0; k < 3; k++)
        if (!eq(a.veh[k].x, b.veh[k].x) || !eq(a.veh[k].z, b.veh[k].z) || !eq(a.veh[k].yawDeg, b.veh[k].yawDeg) || a.veh[k].slot != b.veh[k].slot) return false;
    return eq(a.cam.ex, b.cam.ex) && eq(a.cam.ey, b.cam.ey) && eq(a.cam.ez, b.cam.ez) && eq(a.cam.tx, b.cam.tx) && eq(a.cam.ty, b.cam.ty) && eq(a.cam.tz, b.cam.tz) &&
           eq(a.cam.drift, b.cam.drift) && eq(a.look, b.look);
}

bool fireOf(const Level& lv, V3& at) {
    for (const LevelObject& o : lv.objects)
        if (o.kind == OBJ_MODEL && o.name == "campfire") {
            at = o.p;
            return true;
        }
    return false;
}

}  // namespace menucamp
