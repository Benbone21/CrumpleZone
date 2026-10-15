// Recovery and trail upkeep: winch lines and tow straps, airing tires down and up, the
// sway-bar disconnect, putting a wreck back on its wheels, and repair stations.
#include <algorithm>
#include <cstdio>
#include <string>

#include "softbody.h"

// ---- Recovery points --------------------------------------------------------

V3 Car::hookPoint(int e) const {
    const std::vector<int>& h = hooks[e];
    if (h.empty()) return frame().com;
    V3 p, r;
    for (int i : h) {
        p += nodes[i].p;
        r += restLocal[i];
    }
    p = p / (double)h.size();
    r = r / (double)h.size();
    return p + chassis.rot.rotate(hookRest[e] - r);
}

V3 Car::hookVel(int e) const {
    const std::vector<int>& h = hooks[e];
    if (h.empty()) return frame().vel;
    V3 v;
    double m = 0;
    for (int i : h) {
        v += nodes[i].v * nodes[i].m;
        m += nodes[i].m;
    }
    return v / m;
}

void Car::pullHook(int e, const V3& F, const V3& at) {
    // The recovery point bolts to the frame, and the frame spreads a line's pull through
    // the whole truck: most of it goes in as a force and a moment on the chassis (as if
    // it were rigid), the rest locally at the hook. Fed in only at the hook, 40 kN would
    // fold the bumper it's mounted behind.
    const std::vector<int>& h = hooks[e];
    const std::vector<int>& all = chassis.nodes;
    const double local = all.empty() ? 1.0 : 0.2;
    double m = 0;
    for (int i : h) m += nodes[i].m;
    for (int i : h) nodes[i].f += F * (local * nodes[i].m / m);
    if (all.empty()) return;
    double M = 0;
    V3 com;
    for (int i : all) {
        M += nodes[i].m;
        com += nodes[i].p * nodes[i].m;
    }
    com = com / M;
    const V3 Fg = F * (1 - local);
    for (int i : all) nodes[i].f += Fg * (nodes[i].m / M);
    applyTorque(all, cross(at - com, Fg));
}

// ---- Putting a truck back on its wheels -------------------------------------

void Car::rotateRigid(const V3& axis, double ang) {
    const V3 o = frame().com;
    const Quat q = Quat::axisAngle(norm(axis), ang);
    for (Node& n : nodes) {
        n.p = o + q.rotate(n.p - o);
        n.v = V3();
    }
    if (!chassis.nodes.empty()) {
        chassis.rot = q * chassis.rot;
        chassis.rot.normalize();
        for (ChassisFrame& rf : railFrame) rf.rot = q * rf.rot, rf.rot.normalize();
    }
    for (Wheel& wh : wheels) wh.omega = 0, wh.stickX = wh.stickY = 0;
    wake();
    updateBounds();
}

void Car::placeUpright(const World& w, double x, double z, double yaw) {
    // Rotate the whole truck as it is (dents and all) so it sits level facing `yaw`,
    // then lower it onto whatever is under it.
    const Frame fr = frame();
    const Quat want = Quat::axisAngle(V3(0, 1, 0), yaw);
    // Rotation taking the current attitude to the wanted one.
    Quat cur = chassis.nodes.empty() ? Quat::axisAngle(V3(0, 1, 0), std::atan2(-fr.fwd.z, fr.fwd.x)) : chassis.rot;
    Quat inv{cur.w, -cur.x, -cur.y, -cur.z};
    Quat q = want * inv;
    q.normalize();
    double lowest = 1e9;
    for (Node& n : nodes) {
        n.p = q.rotate(n.p - fr.com);
        n.v = V3();
        n.f = V3();
        lowest = std::min(lowest, n.p.y);
    }
    const V3 shift(x, w.height(x, z) + 0.5 - lowest, z);
    for (Node& n : nodes) n.p += shift;
    if (!chassis.nodes.empty()) {
        chassis.rot = q * chassis.rot;
        chassis.rot.normalize();
        for (ChassisFrame& rf : railFrame) rf.rot = q * rf.rot, rf.rot.normalize();
    }
    setDown(w);  // onto the slope under it
    for (Wheel& wh : wheels) wh.omega = 0, wh.stickX = wh.stickY = 0;
    throttle = brake = 0;
    handbrake = 1;
    wake();
    updateBounds();
}

void Car::setDown(const World& w) {
    // Tilt the truck to the ground under its wheels (a plane through the four contact
    // spots), then lower or raise it until nothing is buried: built level on a steep
    // slope, the uphill tires would start inside the hill and get rammed up through the
    // suspension into the body.
    if (wheels.size() < 4) return;
    auto ground = [&](double x, double z) { return std::max(w.height(x, z), w.clearY(x, z)); };
    auto hub = [&](const Wheel& wh) { return (nodes[wh.inner].p + nodes[wh.outer].p) * 0.5; };
    V3 fl, fr, rl, rr;
    for (const Wheel& wh : wheels) {
        V3 p = hub(wh);
        p.y = ground(p.x, p.z);
        (wh.front ? (wh.left ? fl : fr) : (wh.left ? rl : rr)) = p;
    }
    V3 n = norm(cross(fl - rr, fr - rl));
    if (n.y < 0) n = -n;
    if (n.y < std::cos(35 * M_PI / 180)) n = norm(V3(n.x, 0, n.z) * std::sin(35 * M_PI / 180) / std::max(1e-6, std::hypot(n.x, n.z)) + V3(0, std::cos(35 * M_PI / 180), 0));
    const V3 up = frame().up, axis = cross(up, n);
    const double s = len(axis);
    if (s > 1e-6) rotateRigid(axis / s, std::atan2(s, dot(up, n)));
    // Every tire's lowest point just clear of the ground, and the body too.
    double dy = -1e9;
    for (const Wheel& wh : wheels) {
        const V3 bottom = hub(wh) - n * wh.R;
        dy = std::max(dy, ground(bottom.x, bottom.z) + 0.02 - bottom.y);
    }
    for (int i = 0; i < bodyNodes; i++) dy = std::max(dy, w.height(nodes[i].p.x, nodes[i].p.z) + 0.02 - nodes[i].p.y);
    for (Node& nd : nodes) nd.p.y += dy;
    updateBounds();
}

// ---- Repairs ------------------------------------------------------------------

bool Car::needsRepair() const {
    if (isProp()) return false;
    if (damage > 0.0005 || engineDead || hydrolocked || engineHealth < 0.98) return true;
    for (const Wheel& wh : wheels)
        if (wh.popped || wh.detached || wh.shaftBroken || std::fabs(wh.tieOffset) > 0.002) return true;
    for (const Panel& p : panels)
        if (p.state > 0) return true;
    for (const Tri& t : tris)
        if (t.shattered) return true;
    return false;
}

double Car::repairSeconds() const {
    double t = 4 + 30 * damage;
    for (const Wheel& wh : wheels) t += 5 * wh.popped + 8 * wh.shaftBroken + 12 * wh.detached + 4 * (std::fabs(wh.tieOffset) > 0.002);
    for (const Panel& p : panels) t += p.state == 2 ? 6 : p.state == 1 ? 3 : 0;
    if (engineDead || hydrolocked) t += 12;
    else t += (1 - engineHealth) * 15;
    return t;
}

bool Car::needsFieldRepair() const {
    if (isProp()) return false;
    if (hydrolocked) return true;
    for (const Wheel& wh : wheels) {
        if (wh.detached) continue;
        if (wh.popped || wh.shaftBroken || std::fabs(wh.tieOffset) > 0.002) return true;
        for (int bi : wh.shafts)
            if (bi >= 0 && beams[bi].broken) return true;
    }
    for (const Beam& b : beams)
        if (!b.broken && (b.type == BEAM_ARM || b.type == BEAM_TIEROD) && std::fabs(b.L0 - b.Lorig) > 0.005 * b.Lorig) return true;
    for (const Axle& ax : axles)
        if (std::fabs(ax.arbBend) > 0.005) return true;
    return false;
}

double Car::fieldRepairSeconds() const {
    double t = 6;
    for (const Wheel& wh : wheels) {
        if (wh.detached) continue;
        t += 12 * wh.popped + 20 * wh.shaftBroken + 8 * (std::fabs(wh.tieOffset) > 0.002);
        for (int bi : wh.shafts) t += bi >= 0 && beams[bi].broken ? 25 : 0;
    }
    for (const Beam& b : beams)
        if (!b.broken && (b.type == BEAM_ARM || b.type == BEAM_TIEROD) && std::fabs(b.L0 - b.Lorig) > 0.005 * b.Lorig) t += 3;
    if (hydrolocked) t += 30;  // pull the plugs, crank the water out
    return t;
}

void Car::fieldRepair() {
    // Consume parts from the repair kit. If a part is missing, the damage stays.
    for (Wheel& wh : wheels) {
        if (wh.detached) continue;
        if (wh.popped) {
            if (repairKit.spareTires > 0) {
                wh.popped = false;
                repairKit.spareTires--;
            }
        }
        if (wh.shaftBroken) {
            if (repairKit.cvJoints > 0) {
                wh.shaftBroken = false;
                repairKit.cvJoints--;
            }
        }
        if (std::fabs(wh.tieOffset) > 0.002) {
            if (repairKit.tieRods > 0) {
                wh.tieOffset = 0;
                repairKit.tieRods--;
            }
        }
        wh.shaftLoad = 0;
    }
    // Bent links bolted straight; torn drive shafts replaced if the ends are still there.
    for (Beam& b : beams) {
        if (b.broken && b.type == BEAM_SHAFT) {
            const double L = len(nodes[b.b].p - nodes[b.a].p);
            if (L < b.Lorig * 1.2 && L > b.Lorig * 0.6) {
                if (repairKit.cvJoints > 0) {
                    b.broken = false;
                    repairKit.cvJoints--;
                }
            }
            continue;
        }
        if (!b.broken && (b.type == BEAM_ARM || b.type == BEAM_TIEROD) && std::fabs(b.L0 - b.Lorig) > 0.005 * b.Lorig) {
            if (b.type == BEAM_ARM) {
                if (repairKit.suspensionLinks > 0) {
                    b.L0 = b.Lorig;
                    repairKit.suspensionLinks--;
                }
            } else {
                if (repairKit.tieRods > 0) {
                    b.L0 = b.Lorig;
                    repairKit.tieRods--;
                }
            }
        }
    }
    for (Axle& ax : axles) {
        if (std::fabs(ax.arbBend) > 0.005) {
            if (repairKit.suspensionLinks > 0) {
                ax.arbBend = 0;
                repairKit.suspensionLinks--;
            }
        }
    }
    if (hydrolocked) {
        hydrolocked = false;
        engineDead = engineHealth < 0.12;
    }
    tirePsi = std::max(tirePsi, spec->tirePsi * 0.8);
    wake();
}

void Sim::repairCar(int i) {
    Car& c = cars[i];
    const Frame fr = c.frame();
    const V3 sp = c.spawnPos;
    const double sy = c.spawnYaw;
    double yaw = std::atan2(-fr.fwd.z, fr.fwd.x);
    if (!std::isfinite(yaw)) yaw = 0;
    dropRopes(i);
    c.respawn(V3(fr.com.x, world.clearY(fr.com.x, fr.com.z) + 0.05, fr.com.z), yaw);
    c.setDown(world);
    c.spawnPos = sp;
    c.spawnYaw = sy;
    c.handbrake = 1;
    // Repair station replenishes the field kit
    c.repairKit = Car::RepairKit();
}

int Sim::stationAt(const V3& p) const {
    for (int s = 0; s < (int)world.stations.size(); s++) {
        const Station& st = world.stations[s];
        const double dx = p.x - st.p.x, dz = p.z - st.p.z;
        if (dx * dx + dz * dz < st.r * st.r && std::fabs(p.y - st.p.y) < 4) return s;
    }
    return -1;
}

int Sim::nearestStation(const V3& p, double* dist) const {
    int best = -1;
    double bd = 1e18;
    for (int s = 0; s < (int)world.stations.size(); s++) {
        const double d = len(world.stations[s].p - p);
        if (d < bd) bd = d, best = s;
    }
    if (dist) *dist = bd;
    return best;
}

// ---- Lines --------------------------------------------------------------------

namespace {

V3 endPoint(const std::vector<Car>& cars, const RopeEnd& e) {
    return e.kind == ANCHOR_CAR ? cars[e.car].hookPoint(e.hook) : e.p;
}
V3 endVel(const std::vector<Car>& cars, const RopeEnd& e) {
    return e.kind == ANCHOR_CAR ? cars[e.car].hookVel(e.hook) : V3();
}

const char* anchorName(AnchorKind k) {
    return k == ANCHOR_TREE ? "TREE" : k == ANCHOR_ROCK ? "BOULDER" : k == ANCHOR_STAKE ? "GROUND ANCHOR" : "VEHICLE";
}

}  // namespace

int Sim::ropeOn(int car, int kind) const {
    for (int r = 0; r < (int)ropes.size(); r++) {
        const Rope& rp = ropes[r];
        if (kind >= 0 && rp.kind != kind) continue;
        if ((rp.a.kind == ANCHOR_CAR && rp.a.car == car) || (rp.b.kind == ANCHOR_CAR && rp.b.car == car)) return r;
    }
    return -1;
}

void Sim::dropRopes(int car) {
    ropes.erase(std::remove_if(ropes.begin(), ropes.end(),
                               [&](const Rope& r) {
                                   return (r.a.kind == ANCHOR_CAR && r.a.car == car) || (r.b.kind == ANCHOR_CAR && r.b.car == car);
                               }),
                ropes.end());
}

void Sim::carRemoved(int car) {
    dropRopes(car);
    for (Rope& r : ropes) {
        if (r.a.kind == ANCHOR_CAR && r.a.car > car) r.a.car--;
        if (r.b.kind == ANCHOR_CAR && r.b.car > car) r.b.car--;
    }
    // Autopilots following a vehicle: the one that left stops following, the rest keep their leader's new index.
    for (int i = 0; i < (int)cars.size(); i++) {
        Autopilot& ap = cars[i].pilot;
        if (ap.followCar < 0) continue;
        if (ap.followCar == car) {
            ap.followCar = -1;
            if (ap.active) {
                ap.active = false;
                if (i == player) events.push_back("AUTOPILOT: THE VEHICLE IT WAS FOLLOWING IS GONE");
            }
        } else if (ap.followCar > car) {
            ap.followCar--;
        }
    }
}

std::vector<WinchTarget> Sim::winchTargets(int ci, const V3* look) const {
    std::vector<WinchTarget> out;
    const Car& c = cars[ci];
    if (c.isProp() || !(c.spec->extras & EXTRA_WINCH)) return out;
    const V3 p = c.hookPoint(0);
    const Frame fr = c.frame();
    const V3 fwd = norm(V3(fr.fwd.x, 0, fr.fwd.z));
    // (The drum's line plus the extension from the recovery bag.)
    const double reach = recovery::kWinchReach - 1;
    auto consider = [&](const RopeEnd& e, const char* what) {
        const V3 q = endPoint(cars, e);
        const V3 d = q - p;
        const double dist = len(d);
        if (dist < 1.5 || dist > reach) return;
        WinchTarget t;
        t.end = e;
        t.at = q;
        t.dist = dist;
        // The line runs out through the fairlead: straight ahead to ~75 degrees either side.
        t.inArc = dot(norm(V3(d.x, 0, d.z)), fwd) > 0.26;
        t.what = what;
        out.push_back(t);
    };
    for (const Pole& pl : world.poles) {
        if (pl.r < 0.15) continue;  // fence and sign posts would pull out
        const double dx = pl.x - p.x, dz = pl.z - p.z;
        if (dx * dx + dz * dz > reach * reach) continue;
        RopeEnd e;
        e.kind = ANCHOR_TREE;
        const double y = std::min(pl.y1 - 0.2, std::max(pl.y0 + 0.4, world.height(pl.x, pl.z) + 0.5));
        const V3 toward = norm(V3(p.x - pl.x, 0, p.z - pl.z));
        e.p = V3(pl.x, y, pl.z) + toward * pl.r;
        consider(e, pl.r > 0.2 ? "TREE" : "STUMP");
    }
    for (const Rock& rk : world.rocks) {
        if (rk.r < 0.45) continue;  // too small: it would just roll toward you
        if (len(rk.c - p) > reach + rk.r) continue;
        RopeEnd e;
        e.kind = ANCHOR_ROCK;
        const V3 toward = norm(p - rk.c);
        e.p = rk.c + toward * (rk.r * 0.75);
        e.p.y = std::max(e.p.y, world.height(e.p.x, e.p.z) + 0.2);
        consider(e, "BOULDER");
    }
    for (int o = 0; o < (int)cars.size(); o++) {
        if (o == ci || cars[o].isProp()) continue;
        for (int h = 0; h < 2; h++) {
            RopeEnd e;
            e.kind = ANCHOR_CAR;
            e.car = o;
            e.hook = h;
            consider(e, h ? "TRUCK (REAR)" : "TRUCK (FRONT)");
        }
    }
    // Anywhere else: a buried anchor in loose ground (a spare tire or a pull-pal dug in), or
    // a bolt drilled into bare rock (or a stake in the verge beside a road). One straight
    // ahead, and one wherever the driver is looking or standing.
    auto ground = [&](const V3& q) {
        if (world.waterLevel(q.x, q.z) > world.height(q.x, q.z) + 0.3) return;  // not under water
        const Surface s = world.surfaceAt(q.x, q.z);
        RopeEnd e;
        e.kind = ANCHOR_STAKE;
        e.p = V3(q.x, world.height(q.x, q.z) + 0.1, q.z);
        consider(e, s == SURF_ROCK || s == SURF_PAVED || s == SURF_WET ? "ROCK ANCHOR" : "GROUND ANCHOR");
    };
    ground(p + fwd * 14);
    if (look) ground(*look);
    std::sort(out.begin(), out.end(), [](const WinchTarget& x, const WinchTarget& y) { return x.dist < y.dist; });
    return out;
}

bool Sim::dropWinch(int ci) {
    for (int r = 0; r < (int)ropes.size(); r++)
        if (ropes[r].kind == ROPE_WINCH && ropes[r].a.car == ci) {
            ropes.erase(ropes.begin() + r);
            return true;
        }
    return false;
}

std::string Sim::hookWinch(int ci) {
    Car& c = cars[ci];
    if (c.isProp() || !(c.spec->extras & EXTRA_WINCH)) return "NO WINCH ON THIS TRUCK";
    if (dropWinch(ci)) return "WINCH LINE UNHOOKED AND SPOOLED";
    // No choice made: the best anchor ahead (close, and as straight as possible), with a
    // buried anchor only when there's nothing else.
    const Frame fr = c.frame();
    const V3 fwd = norm(V3(fr.fwd.x, 0, fr.fwd.z)), p = c.hookPoint(0);
    const WinchTarget* best = nullptr;
    double bestScore = 1e18;
    const std::vector<WinchTarget> ts = winchTargets(ci);
    for (const WinchTarget& t : ts) {
        if (!t.inArc) continue;
        const double ahead = dot(norm(V3(t.at.x - p.x, 0, t.at.z - p.z)), fwd);
        const double score = t.dist * (2.2 - ahead) + (t.end.kind == ANCHOR_STAKE ? 1e6 : 0);
        if (score < bestScore) bestScore = score, best = &t;
    }
    if (!best) return "NOTHING TO WINCH TO AHEAD";
    return hookWinchTo(ci, best->end);
}

std::string Sim::hookWinchTo(int ci, const RopeEnd& best) {
    if (ci < 0 || ci >= (int)cars.size()) return "VEHICLE NO LONGER AVAILABLE";
    Car& c = cars[ci];
    if (c.isProp() || !(c.spec->extras & EXTRA_WINCH)) return "NO WINCH ON THIS TRUCK";
    if (best.kind == ANCHOR_CAR && (best.car < 0 || best.car >= (int)cars.size() || best.car == ci ||
                                  cars[best.car].isProp() || best.hook < 0 || best.hook > 1)) return "INVALID RECOVERY POINT";
    const V3 dest = endPoint(cars, best), source = c.hookPoint(0);
    const V3 delta = dest - source;
    if (!std::isfinite(len(delta)) || len(delta) < 1.5 || len(delta) > recovery::kWinchReach) return "ANCHOR OUT OF LINE REACH";
    const V3 fwd = c.frame().fwd;
    if (dot(norm(V3(delta.x, 0, delta.z)), norm(V3(fwd.x, 0, fwd.z))) <= 0.26) return "THAT'S BEHIND THE WINCH: TURN TO FACE IT";
    dropWinch(ci);
    const V3 p = c.hookPoint(0);
    Rope r;
    r.kind = ROPE_WINCH;
    r.a.kind = ANCHOR_CAR;
    r.a.car = ci;
    r.a.hook = 0;
    r.b = best;
    r.maxLen = recovery::kWinchReach;
    r.length = std::min(r.maxLen, len(endPoint(cars, r.b) - p) + 0.4);
    r.k = 2.5e5;
    r.c = 3000;
    r.breakN = recovery::kWinchBreak;
    r.stallN = c.spec->winchPull > 0 ? c.spec->winchPull : recovery::kWinchStall;
    ropes.push_back(r);
    if (best.kind == ANCHOR_CAR) cars[best.car].wake();
    char buf[96];
    std::snprintf(buf, sizeof buf, "WINCH HOOKED TO %s %.0f M AWAY  (Z REEL IN, SHIFT+Z PAY OUT)", anchorName(best.kind), r.length);
    return buf;
}

bool Sim::dropStrap(int ci) {
    for (int r = 0; r < (int)ropes.size(); r++)
        if (ropes[r].kind == ROPE_STRAP && ((ropes[r].a.kind == ANCHOR_CAR && ropes[r].a.car == ci) || (ropes[r].b.kind == ANCHOR_CAR && ropes[r].b.car == ci))) {
            ropes.erase(ropes.begin() + r);
            return true;
        }
    return false;
}

int Sim::pickRecoveryTarget(const std::vector<WinchTarget>& targets, const V3& eye, const V3& ray, double cone) const {
    int pick = -1;
    double best = 1e18;
    const V3 dir = norm(ray);
    for (int k = 0; k < (int)targets.size(); k++) {
        const WinchTarget& t = targets[k];
        const V3 to = t.at - eye;
        const double depth = dot(to, dir);
        if (depth < 0.3) continue;
        const double miss = len(to - dir * depth);
        bool bodyHit = false;
        double hit = depth;
        if (t.end.kind == ANCHOR_CAR && t.end.car >= 0 && t.end.car < (int)cars.size()) {
            // Clicking the vehicle body snaps to the nearer front/rear recovery point.
            // Use its oriented bounds, so a roof or door click works as well as a marker.
            const Car& car = cars[t.end.car];
            const Frame fr = car.frame();
            const V3 axes[] = {fr.fwd, fr.up, fr.left};
            const double extents[] = {car.spec->length * 0.5, 0.9, car.spec->width * 0.5};
            double lo = 0.3, hi = 1e9;
            for (int a = 0; a < 3; a++) {
                const double origin = dot(eye - fr.com, axes[a]), slope = dot(dir, axes[a]);
                if (std::fabs(slope) < 1e-8) { if (std::fabs(origin) > extents[a]) hi = -1; }
                else {
                    double x = (-extents[a] - origin) / slope, y = (extents[a] - origin) / slope;
                    if (x > y) std::swap(x, y);
                    lo = std::max(lo, x); hi = std::min(hi, y);
                }
            }
            bodyHit = lo <= hi;
            hit = lo;
        }
        if (!bodyHit && miss > 0.18 + depth * cone) continue;
        // Real anchors beat the ground below them. Select a point under the cursor,
        // never a distant marker merely because it's the closest marker on screen.
        double score = miss / depth + (t.end.kind == ANCHOR_STAKE ? 100 : 0);
        if (bodyHit) score = -10 + hit * 0.001 + len(t.at - (eye + dir * hit)) * 0.0001;
        if (score < best) best = score, pick = k;
    }
    return pick;
}

std::vector<WinchTarget> Sim::strapTargets(int ci, int hook) const {
    std::vector<WinchTarget> out;
    if (ci < 0 || ci >= (int)cars.size() || cars[ci].isProp() || hook < 0 || hook > 1) return out;
    for (int o = 0; o < (int)cars.size(); o++) {
        if (o == ci || cars[o].isProp()) continue;
        for (int h = 0; h < 2; h++) {
            const V3 at = cars[o].hookPoint(h);
            const double dist = len(at - cars[ci].hookPoint(hook));
            if (dist > recovery::kStrapReach) continue;
            RopeEnd end; end.car = o; end.hook = h;
            out.push_back({end, at, dist, true, h ? "VEHICLE (REAR)" : "VEHICLE (FRONT)"});
        }
    }
    return out;
}

std::string Sim::hookStrap(int ci) {
    if (dropStrap(ci)) return "TOW STRAP UNHOOKED";
    Car& c = cars[ci];
    if (c.isProp()) return "";
    double best = recovery::kStrapReach;
    int bo = -1, bh = 0, mh = 0;
    for (int o = 0; o < (int)cars.size(); o++) {
        if (o == ci || cars[o].isProp()) continue;
        for (int h = 0; h < 2; h++)
            for (int m = 0; m < 2; m++) {
                const double d = len(cars[o].hookPoint(h) - c.hookPoint(m));
                if (d < best) best = d, bo = o, bh = h, mh = m;
            }
    }
    if (bo < 0) return "NO VEHICLE WITHIN STRAP REACH (9 M)";
    RopeEnd target; target.car = bo; target.hook = bh;
    return hookStrapTo(ci, mh, target);
}

std::string Sim::hookStrapTo(int ci, int mh, const RopeEnd& target) {
    if (ci < 0 || ci >= (int)cars.size() || cars[ci].isProp() || mh < 0 || mh > 1 || target.kind != ANCHOR_CAR ||
        target.car < 0 || target.car >= (int)cars.size() || target.car == ci || cars[target.car].isProp() ||
        target.hook < 0 || target.hook > 1) return "INVALID RECOVERY POINT";
    const int bo = target.car, bh = target.hook;
    if (len(cars[ci].hookPoint(mh) - cars[bo].hookPoint(bh)) > recovery::kStrapReach) return "THAT POINT IS BEYOND STRAP REACH (9 M)";
    dropStrap(ci);
    Rope r;
    r.kind = ROPE_STRAP;
    r.a.kind = ANCHOR_CAR;
    r.a.car = ci;
    r.a.hook = mh;
    r.b.kind = ANCHOR_CAR;
    r.b.car = bo;
    r.b.hook = bh;
    r.maxLen = r.length = recovery::kStrapLen;
    // A kinetic strap stretches ~20% near its working load: it snatches, it doesn't jerk.
    r.k = 60000 / (0.2 * recovery::kStrapLen);
    r.c = 2500;
    r.breakN = recovery::kStrapBreak;
    ropes.push_back(r);
    cars[bo].wake();
    return std::string("TOW STRAP: YOUR ") + (mh ? "REAR" : "FRONT") + " TO " + (cars[bo].spec ? cars[bo].spec->name : "VEHICLE") + (bh ? " REAR" : " FRONT");
}

void Sim::setWinch(int ci, int dir) {
    for (Rope& r : ropes)
        if (r.kind == ROPE_WINCH && r.a.car == ci) r.winch = dir;
}

void Sim::ropeForces() {
    for (size_t ri = 0; ri < ropes.size();) {
        Rope& r = ropes[ri];
        const V3 pa = endPoint(cars, r.a), pb = endPoint(cars, r.b);
        const V3 d = pb - pa;
        const double L = len(d);
        // Winch drum: reels in at a speed that falls off with load and stalls at its
        // rated pull; pays out freely.
        if (r.kind == ROPE_WINCH && !cars[r.a.car].remote) {  // the winch's owner works the drum
            // (The remote's speed control: a share of the drum's full speed.)
            const double sp = clampd(cars[r.a.car].winchSpeed, recovery::kWinchSpeedMin, recovery::kWinchSpeedMax);
            if (r.winch > 0) r.length -= recovery::kWinchSpeed * sp * std::max(0.0, 1 - r.tension / r.stallN) * h;
            else if (r.winch < 0) r.length += 0.25 * sp * h;
            r.length = clampd(r.length, 0.6, r.maxLen);
        }
        r.tension = 0;
        if (L > r.length && L > 1e-6) {
            const V3 dir = d / L;
            const double rate = dot(endVel(cars, r.b) - endVel(cars, r.a), dir);
            r.tension = std::max(0.0, r.k * (L - r.length) + r.c * rate);
            const V3 F = dir * r.tension;
            // (Another player's truck feels the line on their own machine.)
            if (r.a.kind == ANCHOR_CAR && !cars[r.a.car].remote) {
                cars[r.a.car].pullHook(r.a.hook, F, pa);
                if (cars[r.a.car].sleeping) cars[r.a.car].wake();
            }
            if (r.b.kind == ANCHOR_CAR && !cars[r.b.car].remote) {
                cars[r.b.car].pullHook(r.b.hook, -F, pb);
                if (cars[r.b.car].sleeping) cars[r.b.car].wake();
            }
        }
        r.shown += (r.tension - r.shown) * std::min(1.0, h / 0.15);
        r.overload = r.tension > r.breakN ? r.overload + h : 0;
        if (r.overload > 0.004) {
            events.push_back(r.kind == ROPE_WINCH ? "WINCH LINE SNAPPED" : "TOW STRAP SNAPPED");
            ropes.erase(ropes.begin() + ri);
            continue;
        }
        ri++;
    }
}

// ---- Per frame ----------------------------------------------------------------

void Sim::updateTrail(double dt) {
    for (int i = 0; i < (int)cars.size(); i++) {
        Car& c = cars[i];
        if (c.isProp() || c.remote) continue;
        const double speed = len(c.frame().vel);
        // Tires: let air out at the valves (5 psi/s), pump up with the onboard
        // compressor (2 psi/s), or shop air at a repair station (8 psi/s).
        const int st = stationAt(c.frame().com);
        if (c.airing != 0 && speed < 0.5) {
            const double rate = c.airing < 0 ? 5.0 : st >= 0 ? 8.0 : 2.0;
            c.tirePsi = clampd(c.tirePsi + c.airing * rate * dt, 12, 40);
        }
        // The shop's pump: fills the tank (and the spare can) while you're on its pad.
        if (st >= 0 && speed < 0.5 && c.fuel >= 0) {
            c.fuel = std::min(c.spec->fuelCapacity, c.fuel + 3.0 * dt);
            if (c.jerry >= 0) c.jerry = std::min(c.spec->jerryCan, c.jerry + 3.0 * dt);
        }
        // An electronic sway-bar disconnect hooks itself back up past ~30 km/h.
        if (c.swayDisconnected && c.spec->swayDisconnect == 1 && speed > 30 / 3.6) {
            c.swayDisconnected = false;
            if (i == player) events.push_back("FRONT SWAY BAR RECONNECTED (OVER 30 KM/H)");
        }
        // Trail repairs: stopped beside another vehicle (anyone's), its crew and kit fix
        // what can be fixed out here.
        c.fieldHelper = -1;
        if (st < 0 && speed < 0.5 && c.needsFieldRepair()) {
            const V3 me = c.frame().com;
            double best = 7.5;
            for (int o = 0; o < (int)cars.size(); o++) {
                if (o == i || cars[o].isProp()) continue;
                const Frame of = cars[o].frame();
                const double d = len(of.com - me);
                if (d < best && len(of.vel) < 0.8) best = d, c.fieldHelper = o;
            }
        }
        if (c.fieldHelper >= 0) {
            // The helper vehicle's kit is used for the repairs
            Car& helper = cars[c.fieldHelper];
            if (!helper.repairKit.hasSpareParts()) {
                c.fieldTime = 0;
                if (i == player) events.push_back("HELPER TRUCK HAS NO SPARE PARTS LEFT");
                continue;
            }
            c.fieldTime += dt;
            if (c.fieldTime >= c.fieldRepairSeconds()) {
                c.fieldRepair();
                c.fieldTime = 0;
                if (i == player) {
                    if (c.needsFieldRepair()) {
                        events.push_back("TRAIL REPAIRS DONE (SOME PARTS MISSING, DAMAGE REMAINS)");
                    } else if (c.needsRepair()) {
                        events.push_back("TRAIL REPAIRS DONE (BODYWORK STILL NEEDS A STATION)");
                    } else {
                        events.push_back("TRAIL REPAIRS DONE");
                    }
                }
            }
        } else {
            c.fieldTime = 0;
        }
        // Repair stations: park on the pad and wait.
        c.station = st;
        if (st >= 0 && speed < 0.5 && c.needsRepair()) {
            c.repairTime += dt;
            if (c.repairTime >= c.repairSeconds()) {
                repairCar(i);
                cars[i].station = st;
                events.push_back(std::string(cars[i].spec->name) + " REPAIRED AT " + world.stations[st].name);
            }
        } else if (speed > 1.0 || st < 0 || !c.needsRepair()) {
            c.repairTime = 0;
        }
    }
}
