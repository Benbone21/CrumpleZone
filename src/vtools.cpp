#include "vtools.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "autopilot.h"

namespace vtools {

namespace {

std::string upper(std::string s) {
    for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
    return s;
}

// Who a vehicle belongs to, for numbering: 0 mine, else the other player's number + 1.
int ownerKey(const Car& c) { return c.remote ? (int)(c.netId >> 16) + 1 : 0; }
int groupKey(const Car& c) { return (c.spec ? c.spec->model + 2 : 0) * 64 + ownerKey(c); }

bool vehicle(const Car& c) { return !c.isProp(); }
bool mine(const Car& c) { return !c.isProp() && !c.remote; }

V3 flat(const V3& v) {
    V3 f(v.x, 0, v.z);
    return len(f) > 1e-6 ? norm(f) : V3(1, 0, 0);
}

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

}  // namespace

// ---- Names -----------------------------------------------------------------------------------

const char* modelName(const VehicleSpec& vs) {
    const std::string id = vs.id ? vs.id : "";
    if (id == "civic") return "CIVIC";
    if (id == "jesko") return "JESKO";
    if (id == "bronco") return "BRONCO";
    if (id == "4runner") return "4RUNNER";
    if (id == "buggy") return "BUGGY";
    if (id == "utv") return "UTV";
    if (id == "tacoma") return "TACOMA";
    if (vs.name && std::strstr(vs.name, "CRASH")) return "BOX";
    if (vs.name && std::strstr(vs.name, "BARRIER")) return "BARRIER";
    return "VEHICLE";
}

std::string colourName(const V3& c) {
    const double v = std::max(c.x, std::max(c.y, c.z)), mn = std::min(c.x, std::min(c.y, c.z));
    const double s = v > 1e-6 ? (v - mn) / v : 0;
    if (v < 0.16) return "BLACK";
    if (s < 0.18) return v > 0.78 ? "WHITE" : v > 0.52 ? "SILVER" : "GREY";
    double h;
    const double d = v - mn;
    if (v == c.x) h = 60 * std::fmod((c.y - c.z) / d + 6, 6);
    else if (v == c.y) h = 60 * ((c.z - c.x) / d + 2);
    else h = 60 * ((c.x - c.y) / d + 4);
    if (h < 15 || h >= 345) return v < 0.4 ? "MAROON" : "RED";
    if (h < 45) return v < 0.55 ? "BROWN" : "ORANGE";
    if (h < 70) return "YELLOW";
    if (h < 165) return "GREEN";
    if (h < 195) return "TEAL";
    if (h < 255) return v > 0.75 && s < 0.6 ? "SKY BLUE" : "BLUE";
    if (h < 300) return "PURPLE";
    return "PINK";
}

void assign(Sim& sim) {
    static std::atomic<uint32_t> nextUid{1};
    for (Car& c : sim.cars)
        if (!c.uid) c.uid = nextUid++;
    // Numbers: a vehicle keeps the one it has while its model and owner stay; the rest take the
    // lowest number nobody of that group is using.
    const int n = (int)sim.cars.size();
    std::vector<char> keep(n, 0);
    for (int i = 0; i < n; i++) {
        const Car& c = sim.cars[i];
        if (!vehicle(c) || c.labelNo <= 0 || c.labelModel != groupKey(c)) continue;
        bool dup = false;
        for (int k = 0; k < i; k++)
            if (keep[k] && sim.cars[k].labelModel == c.labelModel && sim.cars[k].labelNo == c.labelNo) dup = true;
        keep[i] = !dup;
    }
    for (int i = 0; i < n; i++) {
        Car& c = sim.cars[i];
        if (!vehicle(c) || keep[i]) continue;
        const int g = groupKey(c);
        int no = 1;
        for (;; no++) {
            bool used = false;
            for (int k = 0; k < n && !used; k++)
                if (k != i && keep[k] && sim.cars[k].labelModel == g && sim.cars[k].labelNo == no) used = true;
            if (!used) break;
        }
        c.labelNo = no;
        c.labelModel = g;
        keep[i] = 1;
    }
}

std::string tag(const Sim& sim, int car, const OwnerName& owner) {
    if (car < 0 || car >= (int)sim.cars.size()) return "?";
    const Car& c = sim.cars[car];
    const std::string model = modelName(*c.spec);
    if (c.isProp()) return model;
    const std::string num = c.labelNo > 0 ? " " + std::to_string(c.labelNo) : "";
    if (c.remote) {
        int same = 0;
        for (const Car& o : sim.cars) same += vehicle(o) && o.remote && groupKey(o) == groupKey(c);
        const std::string who = owner ? upper(owner(c.netId)) : "PLAYER";
        return who + ": " + model + (same > 1 ? num : "");
    }
    return (car == sim.player ? "YOU: " : "") + model + num;
}

std::string name(const Sim& sim, int car, const OwnerName& owner) {
    if (car < 0 || car >= (int)sim.cars.size()) return "?";
    const Car& c = sim.cars[car];
    if (c.isProp()) return tag(sim, car, owner);
    return tag(sim, car, owner) + " (" + colourName(c.color) + ")";
}

std::string distanceText(double d) {
    char b[32];
    if (d > 950) std::snprintf(b, sizeof b, "%.1f KM", d / 1000);
    else std::snprintf(b, sizeof b, "%.0f M", d);
    return b;
}

std::string directionText(const V3& from, const V3& facing, const V3& to) {
    const V3 f = flat(facing);
    V3 d(to.x - from.x, 0, to.z - from.z);
    if (len(d) < 1.0) return "HERE";
    d = norm(d);
    const V3 left(f.z, 0, -f.x);  // up x forward
    const double a = std::atan2(dot(d, left), dot(d, f)) * 180 / M_PI;  // + is to the left
    const double m = std::fabs(a);
    const char* side = a > 0 ? "LEFT" : "RIGHT";
    if (m < 22.5) return "AHEAD";
    if (m < 67.5) return std::string("AHEAD ") + side;
    if (m < 112.5) return side;
    if (m < 157.5) return std::string("BEHIND ") + side;
    return "BEHIND";
}

std::string listLine(const Sim& sim, int car, const V3& from, const V3& facing, const OwnerName& owner) {
    if (car < 0 || car >= (int)sim.cars.size()) return "?";
    const Car& c = sim.cars[car];
    const V3 p = c.frame().com;
    const double d = std::hypot(p.x - from.x, p.z - from.z);
    std::string s = tag(sim, car, owner) + " - " + colourName(c.color) + " - ";
    s += d < 3 ? "RIGHT HERE" : distanceText(d) + " " + directionText(from, facing, p);
    return s;
}

int indexOfUid(const Sim& sim, uint32_t uid) {
    if (!uid) return -1;
    for (int i = 0; i < (int)sim.cars.size(); i++)
        if (sim.cars[i].uid == uid) return i;
    return -1;
}

std::string status(const Sim& sim, int car) {
    if (car < 0 || car >= (int)sim.cars.size()) return "";
    const Car& c = sim.cars[car];
    if (c.remote) return "ANOTHER PLAYER'S";
    if (c.ai) return "AI TRAFFIC";
    if (c.pilot.active) return (c.pilot.followCar >= 0 ? "FOLLOWING: " : "AUTOPILOT: ") + c.pilot.goalName;
    if (car == sim.player) return "DRIVEN BY YOU";
    return c.handbrake > 0.5 ? "PARKED" : "STANDING";
}

// ---- Finding vehicles ------------------------------------------------------------------------

int pickVehicle(const Sim& sim, const V3& o, const V3& d, double maxDist, double slack) {
    int best = -1;
    double bt = maxDist;
    for (int i = 0; i < (int)sim.cars.size(); i++) {
        const Car& c = sim.cars[i];
        if (!vehicle(c)) continue;
        const V3 lo = c.bbMin - V3(slack, slack, slack), hi = c.bbMax + V3(slack, slack, slack);
        double t0 = 0, t1 = bt;
        bool hit = true;
        const double oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z}, a[3] = {lo.x, lo.y, lo.z}, b[3] = {hi.x, hi.y, hi.z};
        for (int k = 0; k < 3 && hit; k++) {
            if (std::fabs(dd[k]) < 1e-9) {
                if (oo[k] < a[k] || oo[k] > b[k]) hit = false;
            } else {
                double u0 = (a[k] - oo[k]) / dd[k], u1 = (b[k] - oo[k]) / dd[k];
                if (u0 > u1) std::swap(u0, u1);
                t0 = std::max(t0, u0);
                t1 = std::min(t1, u1);
                if (t0 > t1) hit = false;
            }
        }
        if (hit && t0 < bt) bt = t0, best = i;
    }
    return best;
}

int aimedVehicle(const Sim& sim, const V3& eye, const V3& dir, int skip, double maxDist, double maxAngle) {
    int best = -1;
    double bs = 1e18;
    const V3 f = norm(dir);
    for (int i = 0; i < (int)sim.cars.size(); i++) {
        const Car& c = sim.cars[i];
        if (i == skip || !vehicle(c)) continue;
        const V3 to = c.frame().com - eye;
        const double dist = len(to);
        if (dist < 2 || dist > maxDist) continue;
        const double ang = std::acos(clampd(dot(to / dist, f), -1, 1));
        if (ang > maxAngle) continue;
        const double score = ang * 100 + dist * 0.05;  // mostly what's in the middle, then what's near
        if (score < bs) bs = score, best = i;
    }
    return best;
}

// ---- Removing --------------------------------------------------------------------------------

bool canRemove(const Sim& sim, int car) {
    if (car < 0 || car >= (int)sim.cars.size() || car == sim.player) return false;
    return mine(sim.cars[car]);
}

bool jobAfterRemoval(Job& job, int removed) {
    if (job.wreck == removed) {
        job.wreck = -1;
        return false;
    }
    if (job.wreck > removed) job.wreck--;
    return true;
}

bool removeVehicle(Sim& sim, Job* job, bool* jobOn, int car) {
    if (!canRemove(sim, car)) return false;
    const uint32_t netId = sim.cars[car].netId;
    if (!sim.removeCar(car)) return false;
    if (job) {
        const bool wreckGone = (job->wreckId && netId == job->wreckId) || job->wreck == car;
        const bool ok = jobAfterRemoval(*job, car);
        if ((wreckGone || !ok) && jobOn) *jobOn = false;  // the broken-down truck is gone: nothing left to recover
    }
    return true;
}

int spareCount(const Sim& sim) {
    int n = 0;
    for (int i = 0; i < (int)sim.cars.size(); i++) n += canRemove(sim, i);
    return n;
}

int removeSpares(Sim& sim, Job* job, bool* jobOn) {
    int n = 0;
    for (int i = (int)sim.cars.size() - 1; i >= 0; i--)
        if (canRemove(sim, i) && removeVehicle(sim, job, jobOn, i)) n++;
    return n;
}

// ---- Sending vehicles ------------------------------------------------------------------------

const char* gapName(int g) { return g == GAP_CLOSE ? "CLOSE" : g == GAP_MEDIUM ? "MEDIUM" : "FAR"; }
double gapMeters(int g) { return g == GAP_CLOSE ? 0 : g == GAP_MEDIUM ? 15 : 40; }

double kmhFor(const Car& c, double defKmh) { return c.wantKmh > 0 ? c.wantKmh : defKmh; }
int routeFor(const Car& c, double defRoute) { return c.wantRoute >= 0 ? c.wantRoute : (int)defRoute; }

V3 followGoal(const Car& leader, double gap) {
    const Frame f = leader.frame();
    if (gap <= 0) return f.com;
    return f.com - flat(f.fwd) * gap;
}

std::string sendTo(Sim& sim, int car, const V3& goal, const std::string& goalName, double defKmh, int defRoute) {
    if (car < 0 || car >= (int)sim.cars.size() || !mine(sim.cars[car])) return "";
    Car& c = sim.cars[car];
    c.pilot.followCar = -1;
    c.ai = false;
    return pilotStart(c, sim.world, goal, goalName, kmhFor(c, defKmh) / 3.6, routeFor(c, defRoute));
}

std::string sendToStation(Sim& sim, int car, double defKmh, int defRoute) {
    if (car < 0 || car >= (int)sim.cars.size() || !mine(sim.cars[car])) return "";
    const int st = sim.nearestStation(sim.cars[car].frame().com);
    if (st < 0) return "NO REPAIR STATION ON THIS MAP";
    return sendTo(sim, car, sim.world.stations[st].p, sim.world.stations[st].name, defKmh, defRoute);
}

std::string cruiseLoop(Sim& sim, int car, double defKmh) {
    if (car < 0 || car >= (int)sim.cars.size() || !mine(sim.cars[car])) return "";
    if (sim.world.loopRoad.empty()) return "NO LOOP ROAD ON THIS MAP";
    Car& c = sim.cars[car];
    c.pilot.active = false;
    c.pilot.followCar = -1;
    c.ai = true;
    c.aiIndex = -1;
    c.aiCruise = kmhFor(c, defKmh) / 3.6;
    return "AUTOPILOT: CRUISING THE LOOP ROAD";
}

std::string follow(Sim& sim, int car, int leader, double defKmh, int defRoute, const OwnerName& owner, double gap) {
    if (car < 0 || car >= (int)sim.cars.size() || !mine(sim.cars[car])) return "";
    if (leader < 0 || leader >= (int)sim.cars.size() || leader == car || !vehicle(sim.cars[leader])) return "NOTHING TO FOLLOW";
    Car& c = sim.cars[car];
    c.followGap = gap;
    c.pilot.followCar = leader;
    c.ai = false;
    return pilotStart(c, sim.world, followGoal(sim.cars[leader], gap), name(sim, leader, owner), kmhFor(c, defKmh) / 3.6, routeFor(c, defRoute));
}

std::string stop(Sim& sim, int car) {
    if (car < 0 || car >= (int)sim.cars.size()) return "";
    Car& c = sim.cars[car];
    const bool was = c.pilot.active || c.ai;
    c.pilot.active = false;
    c.pilot.followCar = -1;
    c.ai = false;
    if (c.gear < 0) c.gear = 1;
    if (was && car != sim.player) {  // left where it is, braking
        c.throttle = 0;
        c.brake = 0.5;
        c.handbrake = 1;
    }
    return was ? "AUTOPILOT OFF: " + tag(sim, car, nullptr) : "NOT ON AUTOPILOT";
}

// ---- The popup -------------------------------------------------------------------------------

namespace {

const char* routeName(int r) { return pilotRouteName(clampi(r, 0, 2)); }

std::string speedText(double kmh, bool mph) {
    char b[24];
    if (mph) std::snprintf(b, sizeof b, "%d MPH", (int)std::lround(kmh / 1.609344));
    else std::snprintf(b, sizeof b, "%d KM/H", (int)std::lround(kmh));
    return b;
}

// Local vehicles nearest first (to `p`), or every vehicle but `skip`.
std::vector<int> byDistance(const Sim& sim, const V3& p, bool localOnly, int skip) {
    std::vector<std::pair<double, int>> v;
    for (int i = 0; i < (int)sim.cars.size(); i++) {
        const Car& c = sim.cars[i];
        if (i == skip || !vehicle(c) || (localOnly && c.remote)) continue;
        const V3 q = c.frame().com;
        v.push_back({std::hypot(q.x - p.x, q.z - p.z), i});
    }
    std::sort(v.begin(), v.end());
    std::vector<int> out;
    for (auto& e : v) out.push_back(e.second);
    return out;
}

}  // namespace

int Popup::selIndex(const Sim& sim) const {
    int i = indexOfUid(sim, sel);
    if (i >= 0 && mine(sim.cars[i])) return i;
    return sim.player >= 0 && sim.player < (int)sim.cars.size() && mine(sim.cars[sim.player]) ? sim.player : -1;
}

int Popup::targetIndex(const Sim& sim) const { return kind == VEHICLE ? indexOfUid(sim, target) : -1; }

void Popup::close() {
    open = false;
    page = PG_MAIN;
    armedRemove = false;
}

void Popup::openOnVehicle(Env& c, int car, float px, float py) {
    if (car < 0 || car >= (int)c.sim.cars.size()) return;
    const Car& t = c.sim.cars[car];
    open = true;
    kind = VEHICLE;
    target = t.uid;
    x = px, y = py;
    page = PG_MAIN;
    cursor = scroll = 0;
    armedRemove = false;
    say.clear();
    dst = D_NONE;
    if (mine(t)) {
        sel = t.uid;
        kmh = kmhFor(t, c.defKmh);
        route = routeFor(t, c.defRoute);
        gap = t.followGap >= 40 ? GAP_FAR : t.followGap >= 15 ? GAP_MEDIUM : GAP_CLOSE;
        if (t.pilot.active && t.pilot.followCar >= 0 && t.pilot.followCar < (int)c.sim.cars.size()) {
            dst = D_FOLLOW;
            followUid = c.sim.cars[t.pilot.followCar].uid;
        }
    } else if (indexOfUid(c.sim, sel) < 0) {
        sel = 0;
    }
    const int si = selIndex(c.sim);
    if (!mine(t) && si >= 0) {
        kmh = kmhFor(c.sim.cars[si], c.defKmh);
        route = routeFor(c.sim.cars[si], c.defRoute);
    }
}

void Popup::openOnGround(Env& c, const V3& at, float px, float py) {
    open = true;
    kind = GROUND;
    point = at;
    x = px, y = py;
    page = PG_MAIN;
    cursor = scroll = 0;
    armedRemove = false;
    say.clear();
    if (selIndex(c.sim) < 0) sel = 0;
    const int si = selIndex(c.sim);
    if (si >= 0) {
        kmh = kmhFor(c.sim.cars[si], c.defKmh);
        route = routeFor(c.sim.cars[si], c.defRoute);
    }
    pickPointFor = 0;  // (a new popup ends "click the ground")
}

std::vector<Row> Popup::rows(const Env& c) const {
    std::vector<Row> out;
    const Sim& sim = c.sim;
    auto add = [&](Row::Kind k, const std::string& text, const std::string& value, int id, bool enabled = true, int arg = 0) {
        Row r;
        r.kind = k;
        r.text = text;
        r.value = value;
        r.id = id;
        r.enabled = enabled;
        r.arg = arg;
        out.push_back(r);
        return (int)out.size() - 1;
    };
    const int si = selIndex(sim);
    const std::string selTag = si >= 0 ? tag(sim, si, c.owner) : "NONE";

    if (page == PG_PLACES) {
        add(Row::BACK, "BACK", "", ID_BACK);
        if (!c.pad) add(Row::ITEM, "HERE: CLICK THE GROUND NEXT", "", ID_ITEM, true, -1);
        for (int k = 0; k < (int)c.dests.size(); k++) {
            const Dest& d = c.dests[k];
            std::string v;
            if (d.kind == 0 && si >= 0) {
                const V3 me = sim.cars[si].frame().com;
                v = distanceText(std::hypot(d.p.x - me.x, d.p.z - me.z)) + " " + directionText(c.refPos, c.refFacing, d.p);
            }
            add(Row::ITEM, d.name, v, ID_ITEM, true, k);
        }
        return out;
    }
    if (page == PG_FOLLOW) {
        add(Row::BACK, "BACK", "", ID_BACK);
        const int self = kind == VEHICLE ? targetIndex(sim) : si;
        for (int i : byDistance(sim, c.refPos, false, self))
            add(Row::ITEM, listLine(sim, i, c.refPos, c.refFacing, c.owner), "", ID_ITEM, true, i);
        return out;
    }
    if (page == PG_VEHICLES) {
        add(Row::BACK, "BACK", "", ID_BACK);
        for (int i : byDistance(sim, kind == GROUND ? point : c.refPos, true, -1))
            add(Row::ITEM, listLine(sim, i, c.refPos, c.refFacing, c.owner) + (i == si ? "  *" : ""), "", ID_ITEM, true, i);
        return out;
    }

    // ---- the main page ----
    if (kind == VEHICLE) {
        const int ti = targetIndex(sim);
        if (ti < 0) {
            add(Row::HEADER, "THAT VEHICLE IS GONE", "", ID_NONE);
            return out;
        }
        const Car& t = sim.cars[ti];
        add(Row::HEADER, name(sim, ti, c.owner), "", ID_NONE);
        const V3 p = t.frame().com;
        const double d = std::hypot(p.x - c.refPos.x, p.z - c.refPos.z);
        add(Row::INFO, status(sim, ti), ti == sim.player ? "" : distanceText(d) + " " + directionText(c.refPos, c.refFacing, p), ID_NONE);
        if (!mine(t)) {
            add(Row::BUTTON, "FOLLOW THIS", si >= 0 ? selTag : "", ID_FOLLOW_THIS, si >= 0 && si != ti);
            add(Row::INFO, "ANOTHER PLAYER'S VEHICLE", "", ID_NONE);
            return out;
        }
        if (si >= 0 && si != ti) add(Row::BUTTON, "FOLLOW THIS", selTag, ID_FOLLOW_THIS);
        // Where to: a place, or a vehicle.
        std::string dv = "-";
        if (dst == D_PLACE && destIdx >= 0 && destIdx < (int)c.dests.size()) dv = c.dests[destIdx].name;
        else if (dst == D_FOLLOW) {
            const int fi = indexOfUid(sim, followUid);
            dv = fi >= 0 ? tag(sim, fi, c.owner) : "(GONE)";
        }
        add(Row::LINK, "ROUTE TO...", dst == D_PLACE ? dv : "", ID_PLACES);
        add(Row::LINK, "FOLLOW...", dst == D_FOLLOW ? dv : "", ID_FOLLOW);
        Row& sp = out[add(Row::SLIDER, "SPEED", speedText(kmh, c.mph), ID_SPEED)];
        sp.frac = (float)((kmh - 5) / 95);
        add(Row::CHOICE, "STRICTNESS", routeName(route), ID_ROUTE);
        if (dst == D_FOLLOW) add(Row::CHOICE, "FOLLOW GAP", gapName(gap), ID_GAP);
        add(Row::BUTTON, t.pilot.active || t.ai ? "GO THERE INSTEAD" : "GO", dst == D_NONE ? "PICK A PLACE OR VEHICLE" : dv, ID_GO, dst != D_NONE);
        add(Row::BUTTON, "STOP", "", ID_STOP, t.pilot.active || t.ai);
        add(Row::BUTTON, "DRIVE IT", ti == sim.player ? "YOU'RE IN IT" : "", ID_DRIVE, ti != sim.player);
        const int rm = add(Row::BUTTON, armedRemove ? "CLICK AGAIN TO REMOVE" : "REMOVE", ti == sim.player ? "YOU'RE IN IT" : "", ID_REMOVE, canRemove(sim, ti));
        out[rm].danger = true;
        return out;
    }
    // The ground.
    {
        add(Row::HEADER, "GROUND", "", ID_NONE);
        std::string where = directionText(c.refPos, c.refFacing, point);
        add(Row::INFO, distanceText(std::hypot(point.x - c.refPos.x, point.z - c.refPos.z)) + " " + where, "", ID_NONE);
        add(Row::BUTTON, si >= 0 ? "SEND " + selTag + " HERE" : "NO VEHICLE TO SEND", "", ID_SEND_SEL, si >= 0);
        int others = 0;
        for (int i = 0; i < (int)sim.cars.size(); i++) others += mine(sim.cars[i]) && i != sim.player && !sim.cars[i].ai;
        add(Row::BUTTON, "SEND ALL AUTOPILOTS HERE", std::to_string(others) + (others == 1 ? " VEHICLE" : " VEHICLES"), ID_SEND_ALL, others > 0);
        add(Row::LINK, "WHICH VEHICLE...", selTag, ID_PICK_VEHICLE);
        Row& sp = out[add(Row::SLIDER, "SPEED", speedText(kmh, c.mph), ID_SPEED)];
        sp.frac = (float)((kmh - 5) / 95);
        add(Row::CHOICE, "STRICTNESS", routeName(route), ID_ROUTE);
    }
    return out;
}

void Popup::clampCursor(const std::vector<Row>& rs) {
    const int n = (int)rs.size();
    if (n == 0) return;
    cursor = clampi(cursor, 0, n - 1);
    if (page != PG_MAIN) {
        if (cursor < scroll) scroll = cursor;
        if (cursor >= scroll + kVisible) scroll = cursor - kVisible + 1;
        scroll = clampi(scroll, 0, std::max(0, n - kVisible));
    }
}

void Popup::applyToSel(Env& c) {
    const int si = selIndex(c.sim);
    if (si < 0) return;
    Car& s = c.sim.cars[si];
    s.wantKmh = kmh;
    s.wantRoute = route;
    if (s.pilot.active) {
        s.pilot.speed = kmh / 3.6;
        s.pilot.route = route;
    }
    if (s.ai) s.aiCruise = kmh / 3.6;
}

void Popup::adjustSpeed(Env& c, int dir) {
    kmh = clampd(std::round(kmh / 5) * 5 + 5 * dir, 5, 100);
    applyToSel(c);
}

void Popup::go(Env& c) {
    Sim& sim = c.sim;
    const int si = kind == VEHICLE ? targetIndex(sim) : selIndex(sim);
    if (si < 0 || !mine(sim.cars[si])) return;
    sel = sim.cars[si].uid;
    applyToSel(c);
    if (dst == D_PLACE && destIdx >= 0 && destIdx < (int)c.dests.size()) {
        const Dest& d = c.dests[destIdx];
        if (d.kind == 1) say = sendToStation(sim, si, kmh, route);
        else if (d.kind == 4) say = cruiseLoop(sim, si, kmh);
        else say = sendTo(sim, si, d.p, d.name, kmh, route);
    } else if (dst == D_FOLLOW) {
        const int fi = indexOfUid(sim, followUid);
        if (fi < 0) {
            say = "THE VEHICLE TO FOLLOW IS GONE";
            return;
        }
        say = follow(sim, si, fi, kmh, route, c.owner, gapMeters(gap));
    } else {
        return;
    }
    close();
}

void Popup::activate(Env& c, const Row& r, float frac, bool fromKey, int dir) {
    Sim& sim = c.sim;
    if (!r.enabled) return;
    const int si = selIndex(sim);
    switch (r.id) {
        case ID_BACK:
            page = PG_MAIN;
            cursor = scroll = 0;
            return;
        case ID_PLACES:
        case ID_FOLLOW:
        case ID_PICK_VEHICLE:
            page = r.id == ID_PLACES ? PG_PLACES : r.id == ID_FOLLOW ? PG_FOLLOW : PG_VEHICLES;
            cursor = 1;
            scroll = 0;
            return;
        case ID_ITEM: {
            if (page == PG_PLACES) {
                if (r.arg < 0) {  // HERE: the next click on the ground
                    const int ti = kind == VEHICLE ? targetIndex(sim) : si;
                    if (ti < 0) return;
                    applyToSel(c);
                    pickPointFor = sim.cars[ti].uid;
                    say = "CLICK THE GROUND WHERE " + tag(sim, ti, c.owner) + " SHOULD GO (ESC CANCELS)";
                    close();
                    return;
                }
                dst = D_PLACE;
                destIdx = r.arg;
            } else if (page == PG_FOLLOW) {
                if (r.arg < 0 || r.arg >= (int)sim.cars.size()) return;
                dst = D_FOLLOW;
                followUid = sim.cars[r.arg].uid;
            } else if (page == PG_VEHICLES) {
                if (r.arg < 0 || r.arg >= (int)sim.cars.size()) return;
                sel = sim.cars[r.arg].uid;
                const Car& s = sim.cars[r.arg];
                kmh = kmhFor(s, c.defKmh);
                route = routeFor(s, c.defRoute);
            }
            page = PG_MAIN;
            scroll = 0;
            const std::vector<Row> rs = rows(c);
            cursor = 0;
            for (int k = 0; k < (int)rs.size(); k++)
                if (rs[k].id == (page == PG_MAIN && kind == GROUND ? ID_SEND_SEL : ID_GO)) cursor = k;  // the next thing you'd press
            return;
        }
        case ID_SPEED:
            if (fromKey) adjustSpeed(c, dir == 0 ? 1 : dir);
            else {
                kmh = clampd(std::round((5 + 95 * clampd(frac, 0, 1)) / 5) * 5, 5, 100);
                applyToSel(c);
            }
            return;
        case ID_ROUTE:
            route = (route + (dir < 0 ? 2 : 1)) % 3;
            applyToSel(c);
            return;
        case ID_GAP:
            gap = (gap + (dir < 0 ? GAP_COUNT - 1 : 1)) % GAP_COUNT;
            return;
        case ID_GO:
            go(c);
            return;
        case ID_STOP: {
            const int ti = kind == VEHICLE ? targetIndex(sim) : si;
            say = stop(sim, ti);
            close();
            return;
        }
        case ID_DRIVE: {
            const int ti = targetIndex(sim);
            if (ti < 0 || !mine(sim.cars[ti])) return;
            sim.player = ti;
            sim.cars[ti].wake();
            say = "DRIVING " + name(sim, ti, c.owner);
            close();
            return;
        }
        case ID_REMOVE: {
            const int ti = targetIndex(sim);
            if (ti < 0 || !canRemove(sim, ti)) return;
            if (!armedRemove) {
                armedRemove = true;
                return;
            }
            const std::string n = name(sim, ti, c.owner);
            say = removeVehicle(sim, c.job, c.jobOn, ti) ? "REMOVED " + n : "CAN'T REMOVE " + n;
            close();
            return;
        }
        case ID_FOLLOW_THIS: {
            const int ti = targetIndex(sim);
            if (ti < 0 || si < 0 || si == ti) return;
            applyToSel(c);
            say = follow(sim, si, ti, kmh, route, c.owner, gapMeters(gap));
            close();
            return;
        }
        case ID_SEND_SEL: {
            if (si < 0) return;
            applyToSel(c);
            say = sendTo(sim, si, point, "THE SPOT YOU PICKED", kmh, route);
            close();
            return;
        }
        case ID_SEND_ALL: {
            int n = 0;
            std::string last;
            for (int i = 0; i < (int)sim.cars.size(); i++) {
                Car& v = sim.cars[i];
                if (!mine(v) || i == sim.player || v.ai) continue;
                v.wantKmh = kmh;
                v.wantRoute = route;
                last = sendTo(sim, i, point, "THE SPOT YOU PICKED", kmh, route);
                n++;
            }
            say = n == 1 ? last : "AUTOPILOT: " + std::to_string(n) + " VEHICLES SENT";
            close();
            return;
        }
        default:
            return;
    }
}

bool Popup::key(Env& c, Key k) {
    if (!open) return false;
    if (k == K_ESC) {
        if (page != PG_MAIN) {
            page = PG_MAIN;
            cursor = scroll = 0;
        } else {
            close();
        }
        return true;
    }
    std::vector<Row> rs = rows(c);
    if (rs.empty()) {
        close();
        return true;
    }
    auto movable = [&](int i) { return rs[i].kind != Row::HEADER && rs[i].kind != Row::INFO; };
    auto step = [&](int dir) {
        const int n = (int)rs.size();
        for (int tries = 0, i = cursor; tries < n; tries++) {
            i = (i + dir + n) % n;
            if (movable(i)) {
                cursor = i;
                break;
            }
        }
        armedRemove = false;
    };
    clampCursor(rs);
    if (!movable(cursor)) step(1);
    if (k == K_UP) step(-1);
    else if (k == K_DOWN) step(1);
    else {
        const Row r = rs[cursor];
        if (k == K_LEFT || k == K_RIGHT) {
            const int dir = k == K_LEFT ? -1 : 1;
            if (r.kind == Row::SLIDER || r.kind == Row::CHOICE) activate(c, r, 0, true, dir);
            else if (k == K_LEFT && page != PG_MAIN) {
                page = PG_MAIN;
                cursor = scroll = 0;
            }
        } else {  // enter
            activate(c, r, 0.5f, true, 1);
        }
    }
    if (open) clampCursor(rows(c));
    return true;
}

void Popup::click(Env& c, int index, float frac) {
    if (!open) return;
    const std::vector<Row> rs = rows(c);
    if (index < 0 || index >= (int)rs.size()) return;
    const Row r = rs[index];
    if (r.kind == Row::HEADER || r.kind == Row::INFO) return;
    cursor = index;
    activate(c, r, frac, false, 1);
    if (open) clampCursor(rows(c));
}

void Popup::wheel(Env& c, int dir) {
    if (!open || page == PG_MAIN) return;
    const int n = (int)rows(c).size();
    scroll = clampi(scroll + dir, 0, std::max(0, n - kVisible));
    cursor = clampi(cursor, scroll, std::min(n - 1, scroll + kVisible - 1));
}

}  // namespace vtools
