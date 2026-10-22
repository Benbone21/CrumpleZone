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
    const double dflt[3][4] = {{0, 0, 0, 1}, {0, 0, 0, 2}, {0, 0, 0, 0}};
    for (int k = 0; k < 3; k++) m.veh[k].slot = (int)dflt[k][3];
    for (const std::string& line : lv.menuLines) {
        std::istringstream in(line);
        std::string tag, key;
        in >> tag >> key;
        if (key == "veh") {
            int i = -1;
            Vehicle v;
            if (in >> i >> v.x >> v.z >> v.yawDeg >> v.slot && i >= 0 && i < 3) m.veh[i] = v;
        } else if (key == "cam") {
            Camera c;
            if (in >> c.ex >> c.ey >> c.ez >> c.tx >> c.ty >> c.tz >> c.drift) m.cam = c;
        } else if (key == "look") {
            in >> m.look;
        }
    }
    const int n = (int)listedVehicles().size();
    for (Vehicle& v : m.veh) v.slot = n > 0 ? ((v.slot % n) + n) % n : 0;
    return m;
}

void setMeta(Level& lv, const Meta& m) {
    lv.menuLines.clear();
    char b[256];
    lv.menuLines.push_back("#menu scene 1");
    for (int k = 0; k < 3; k++) {
        std::snprintf(b, sizeof b, "#menu veh %d %.3f %.3f %.2f %d", k, m.veh[k].x, m.veh[k].z, m.veh[k].yawDeg, m.veh[k].slot);
        lv.menuLines.push_back(b);
    }
    std::snprintf(b, sizeof b, "#menu cam %.3f %.3f %.3f %.3f %.3f %.3f %.2f", m.cam.ex, m.cam.ey, m.cam.ez, m.cam.tx, m.cam.ty, m.cam.tz, m.cam.drift);
    lv.menuLines.push_back(b);
    std::snprintf(b, sizeof b, "#menu look %.2f", m.look);
    lv.menuLines.push_back(b);
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
