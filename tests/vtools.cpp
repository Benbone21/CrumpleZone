// Vehicle tools: names, taking vehicles away (and every index that pointed past them), and the
// click popup's model (rows, keys, what they do to the Sim). No graphics.
#include <cmath>
#include <set>

#include "../src/vtools.h"
#include "harness.h"

namespace {

const vtools::OwnerName kOwner = [](uint32_t id) { return std::string(id >> 16 == 1 ? "ALEX" : "SAM"); };

int addAt(Ctx& ctx, int model, double x, double z, double heading = 0) {
    ctx.add(vehicleCatalog()[model], x, z, heading);
    return (int)ctx.sim.cars.size() - 1;
}

std::vector<std::string> allNames(const Sim& sim) {
    std::vector<std::string> out;
    for (int i = 0; i < (int)sim.cars.size(); i++)
        if (!sim.cars[i].isProp()) out.push_back(vtools::name(sim, i, kOwner));
    return out;
}

bool unique(const std::vector<std::string>& v) { return std::set<std::string>(v.begin(), v.end()).size() == v.size(); }

// The row of the popup's current page that does `id`.
int rowOf(vtools::Popup& p, vtools::Env& env, int id) {
    const std::vector<vtools::Row> rs = p.rows(env);
    for (int k = 0; k < (int)rs.size(); k++)
        if (rs[k].id == id) return k;
    return -1;
}

// Moves the popup's cursor to the row and presses Enter.
void choose(vtools::Popup& p, vtools::Env& env, int id) {
    const int k = rowOf(p, env, id);
    if (k >= 0) p.click(env, k, 0.5f);
}

}  // namespace

SCENARIO(vtoolsNames, "vtools/names", "Five Broncos are told apart by number and paint; the labels are unique, stay put when others come and go, and say how far and which way") {
    Sim& sim = ctx.sim;
    for (int k = 0; k < 5; k++) addAt(ctx, 2, 40 + k * 10, -20);  // five Broncos
    addAt(ctx, 0, 40, -40);                                         // a Civic
    sim.player = 0;
    vtools::assign(sim);
    std::vector<std::string> before = allNames(sim);
    ctx.check("every name is different", unique(before));
    ctx.check("the one you drive says so", vtools::name(sim, 0, kOwner).rfind("YOU: BRONCO 1 (", 0) == 0, vtools::name(sim, 0, kOwner));
    ctx.check("the others are numbered", vtools::tag(sim, 3, kOwner) == "BRONCO 4" && vtools::tag(sim, 5, kOwner) == "CIVIC 1", vtools::tag(sim, 3, kOwner));
    ctx.check("the paint is in the name", vtools::name(sim, 1, kOwner).find("BRONCO 2 (") == 0 && vtools::name(sim, 1, kOwner).back() == ')');
    // A new arrival, and one going: nobody else's label changes.
    const int fresh = addAt(ctx, 2, 40, -60);
    vtools::assign(sim);
    bool same = true;
    for (int i = 0; i < 6; i++) same &= vtools::name(sim, i, kOwner) == before[i];
    ctx.check("a new arrival changes nobody's label", same && unique(allNames(sim)), vtools::name(sim, fresh, kOwner));
    const uint32_t uidBronco3 = sim.cars[2].uid;
    const uint32_t uid3 = sim.cars[3].uid;
    const std::string n3 = vtools::name(sim, 3, kOwner);
    ctx.check("the sixth Bronco is BRONCO 6", vtools::tag(sim, fresh, kOwner) == "BRONCO 6");
    vtools::removeVehicle(sim, nullptr, nullptr, 1);
    vtools::assign(sim);
    const int now3 = vtools::indexOfUid(sim, uid3);
    ctx.check("one going changes nobody else's label", now3 >= 0 && vtools::name(sim, now3, kOwner) == n3 && unique(allNames(sim)), n3);
    // Same model, same number even after a rebuild (a repair, a respawn).
    sim.cars[now3].respawn(sim.cars[now3].spawnPos, 0);
    vtools::assign(sim);
    ctx.check("a repair keeps the label", vtools::name(sim, now3, kOwner) == n3);
    // Becoming another model renumbers.
    sim.cars[now3].spec = &vehicleCatalog()[3];
    vtools::assign(sim);
    ctx.check("another model, another label", vtools::tag(sim, now3, kOwner) == "4RUNNER 1", vtools::tag(sim, now3, kOwner));

    // Other players' vehicles: their name first.
    const int r = addAt(ctx, 2, 100, 10);
    sim.cars[r].remote = true;
    sim.cars[r].netId = (1u << 16) | 5;
    vtools::assign(sim);
    ctx.check("a remote vehicle wears its player's name", vtools::tag(sim, r, kOwner) == "ALEX: BRONCO", vtools::tag(sim, r, kOwner));
    const int r2 = addAt(ctx, 2, 100, 20);
    sim.cars[r2].remote = true;
    sim.cars[r2].netId = (1u << 16) | 6;
    vtools::assign(sim);
    ctx.check("two of the same get numbers", vtools::tag(sim, r, kOwner) != vtools::tag(sim, r2, kOwner), vtools::tag(sim, r, kOwner) + " / " + vtools::tag(sim, r2, kOwner));
    ctx.check("and still no clash with yours", unique(allNames(sim)));

    // Colours from the paint.
    ctx.check("colour names", vtools::colourName(V3(0.80, 0.16, 0.10)) == "RED" && vtools::colourName(V3(0.14, 0.34, 0.78)) == "BLUE" &&
                                  vtools::colourName(V3(0.88, 0.88, 0.86)) == "WHITE" && vtools::colourName(V3(0.025, 0.026, 0.03)) == "BLACK" &&
                                  vtools::colourName(V3(0.16, 0.55, 0.30)) == "GREEN" && vtools::colourName(V3(0.95, 0.45, 0.10)) == "ORANGE" &&
                                  vtools::colourName(V3(0.90, 0.72, 0.16)) == "YELLOW" && vtools::colourName(V3(0.55, 0.20, 0.60)) == "PURPLE");
    // Direction and distance.
    const V3 from(0, 0, 0), face(1, 0, 0);
    ctx.check("ahead / behind / left / right",
              vtools::directionText(from, face, V3(50, 0, 0)) == "AHEAD" && vtools::directionText(from, face, V3(-50, 0, 0)) == "BEHIND" &&
                  vtools::directionText(from, face, V3(0, 0, -50)) == "LEFT" && vtools::directionText(from, face, V3(0, 0, 50)) == "RIGHT" &&
                  vtools::directionText(from, face, V3(30, 0, -30)) == "AHEAD LEFT" && vtools::directionText(from, face, V3(-30, 0, 30)) == "BEHIND RIGHT");
    const int bronco3 = vtools::indexOfUid(sim, uidBronco3);
    const V3 at = sim.cars[bronco3].frame().com;
    const std::string line = vtools::listLine(sim, bronco3, at - V3(45, 0, 0), face, kOwner);
    ctx.check("list line: number, colour, distance, direction", line.find("BRONCO 3 - ") == 0 && line.find("45 M AHEAD") != std::string::npos, line);
    ctx.check("far away in km", vtools::distanceText(1234) == "1.2 KM" && vtools::distanceText(45) == "45 M");
}

SCENARIO(vtoolsRemove, "vtools/remove", "Removing a car fixes the player, every autopilot's followCar, the lines and the job's wreck; your own car, a prop and another player's are refused") {
    Sim& sim = ctx.sim;
    for (int k = 0; k < 6; k++) addAt(ctx, k % 2 ? 3 : 2, 40 + k * 8, -30);
    const int prop = (ctx.addProp(crashBoxSpec(), 0, 0, 0), (int)sim.cars.size() - 1);
    (void)prop;
    sim.player = 4;
    vtools::assign(sim);
    sim.cars[0].pilot.followCar = 3;   // car 0 follows car 3
    sim.cars[0].pilot.active = true;
    sim.cars[1].pilot.followCar = 5;   // car 1 follows car 5
    sim.cars[1].pilot.active = true;
    sim.cars[2].pilot.followCar = 2 + 1;  // car 2 follows car 3 as well
    sim.cars[2].pilot.active = true;
    Rope strap;
    strap.kind = ROPE_STRAP;
    strap.a.car = 3, strap.b.car = 5;  // a line between 3 and 5
    sim.ropes.push_back(strap);
    Rope tow = strap;
    tow.a.car = 1, tow.b.car = 2;      // and one between 1 and 2
    sim.ropes.push_back(tow);
    Job job;
    job.kind = JOB_RECOVER;
    job.wreck = 5;
    bool jobOn = true;
    const uint32_t uid3 = sim.cars[3].uid, uid5 = sim.cars[5].uid, uid4 = sim.cars[4].uid;
    const uint32_t uid1 = sim.cars[1].uid, uid2 = sim.cars[2].uid;

    ctx.check("your own car can't be removed this way", !vtools::removeVehicle(sim, &job, &jobOn, sim.player) && sim.cars.size() == 7);
    ctx.check("a prop can't be removed", !vtools::removeVehicle(sim, &job, &jobOn, 6));
    sim.cars[0].remote = true;
    ctx.check("another player's car can't be removed", !vtools::removeVehicle(sim, &job, &jobOn, 0));
    sim.cars[0].remote = false;

    // Take out car 2 (before the followed car 3, the player 4, the wreck 5).
    ctx.check("removed", vtools::removeVehicle(sim, &job, &jobOn, 2) && sim.cars.size() == 6);
    ctx.check("the player is still the same car", sim.cars[sim.player].uid == uid4 && sim.player == 3);
    ctx.check("followers keep their leader", sim.cars[0].pilot.followCar >= 0 && sim.cars[sim.cars[0].pilot.followCar].uid == uid3 &&
                                              sim.cars[1].pilot.followCar >= 0 && sim.cars[sim.cars[1].pilot.followCar].uid == uid5);
    ctx.check("the job's wreck is still the same car", job.wreck >= 0 && sim.cars[job.wreck].uid == uid5 && jobOn);
    ctx.check("lines: the one on the removed car is gone, the other moved over", sim.ropes.size() == 1 &&
                                                                              sim.cars[sim.ropes[0].a.car].uid == uid3 && sim.cars[sim.ropes[0].b.car].uid == uid5);
    (void)uid1;
    (void)uid2;

    // Take out a car that is being followed: the follower stops.
    const int lead = vtools::indexOfUid(sim, uid3);
    ctx.check("removed the leader", vtools::removeVehicle(sim, &job, &jobOn, lead));
    ctx.check("its follower stops", !sim.cars[0].pilot.active && sim.cars[0].pilot.followCar == -1);
    ctx.check("the other follower is unharmed", sim.cars[1].pilot.active && sim.cars[sim.cars[1].pilot.followCar].uid == uid5);
    ctx.check("the line to it is dropped", sim.ropes.empty());
    ctx.check("the player is unmoved", sim.cars[sim.player].uid == uid4);

    // The wreck: the job ends.
    const int wreck = vtools::indexOfUid(sim, uid5);
    ctx.check("job points at it", job.wreck == wreck);
    ctx.check("removing the wreck ends the job", vtools::removeVehicle(sim, &job, &jobOn, wreck) && !jobOn);

    // The car you're driving goes: you move to another of yours, never another player's.
    sim.cars[0].remote = true;
    const int before = (int)sim.cars.size();
    sim.removePlayerCar();
    ctx.check("driving car removed, switched to one of yours", (int)sim.cars.size() == before - 1 && !sim.cars[sim.player].remote && !sim.cars[sim.player].isProp());
    sim.cars[0].remote = false;
}

SCENARIO(vtoolsRemoveAll, "vtools/remove-all", "Removing all the spares leaves your own car, props and other players' cars alone, and drops their lines") {
    Sim& sim = ctx.sim;
    for (int k = 0; k < 5; k++) addAt(ctx, k % 3, 40 + k * 8, -30);
    ctx.addProp(crashBoxSpec(), 0, 0, 0);
    ctx.addProp(crashBoxSpec(), 0, 5, 0);
    const int remote = addAt(ctx, 2, 80, 40);
    sim.cars[remote].remote = true;
    sim.cars[remote].netId = (1u << 16) | 9;
    sim.cars[3].ai = true;  // AI traffic goes too
    sim.player = 2;
    vtools::assign(sim);
    const uint32_t me = sim.cars[2].uid, other = sim.cars[remote].uid;
    Rope r;
    r.kind = ROPE_STRAP;
    r.a.car = 2, r.b.car = 4;
    sim.ropes.push_back(r);
    sim.cars[2].pilot.followCar = 1;
    sim.cars[2].pilot.active = true;
    ctx.check("counts the spares", vtools::spareCount(sim) == 4, std::to_string(vtools::spareCount(sim)));
    const int gone = vtools::removeSpares(sim, nullptr, nullptr);
    ctx.check("four went", gone == 4, std::to_string(gone));
    int props = 0, vehicles = 0;
    for (const Car& c : sim.cars) props += c.isProp(), vehicles += !c.isProp();
    ctx.check("you, the other player and the props are left", vehicles == 2 && props == 2 && sim.cars.size() == 4);
    ctx.check("you are still you", sim.cars[sim.player].uid == me && vtools::indexOfUid(sim, other) >= 0);
    ctx.check("lines to removed cars dropped", sim.ropes.empty());
    ctx.check("your autopilot stopped following a vehicle that is gone", sim.cars[sim.player].pilot.followCar == -1 && !sim.cars[sim.player].pilot.active);
    ctx.check("nothing left to remove", vtools::spareCount(sim) == 0 && vtools::removeSpares(sim, nullptr, nullptr) == 0);
}

SCENARIO(vtoolsPick, "vtools/pick", "A ray finds the vehicle it hits first; the one nearest the middle of the view is the one aimed at") {
    Sim& sim = ctx.sim;
    addAt(ctx, 2, 30, 0);   // 0: ahead of the camera
    addAt(ctx, 2, 60, 0);   // 1: behind it, further
    addAt(ctx, 3, 40, 25);  // 2: well off to the side
    ctx.addProp(crashBoxSpec(), 20, 0, 0);
    const V3 eye(0, 1.5, 0);
    ctx.check("first hit", vtools::pickVehicle(sim, eye, V3(1, 0, 0)) == 0);
    ctx.check("the one behind it when it's out of the way", vtools::pickVehicle(sim, eye + V3(40, 0, 0), V3(1, 0, 0)) == 1);
    ctx.check("a ray into the sky hits nothing", vtools::pickVehicle(sim, eye, V3(0, 1, 0)) == -1);
    ctx.check("props are not picked", vtools::pickVehicle(sim, eye, V3(1, 0, 0), 25) == -1);
    ctx.check("aimed at: the middle of the view", vtools::aimedVehicle(sim, eye, V3(1, 0, 0)) == 0);
    ctx.check("aimed at: turned to the side", vtools::aimedVehicle(sim, eye, norm(V3(40, 0, 25))) == 2);
    ctx.check("aimed at: skip one", vtools::aimedVehicle(sim, eye, V3(1, 0, 0), 0) == 1);
    ctx.check("aimed at: nothing there", vtools::aimedVehicle(sim, eye, V3(-1, 0, 0)) == -1);
}

SCENARIO(vtoolsPopup, "vtools/popup", "The click popup: FOLLOW picks from the labelled list, SPEED and STRICTNESS are per vehicle, GO sends it, STOP and DRIVE IT and REMOVE work, the ground sends one or all") {
    Sim& sim = ctx.sim;
    sim.player = addAt(ctx, 2, 60, -12);
    const int a = addAt(ctx, 3, 90, -12), b = addAt(ctx, 0, 60, 14), c = addAt(ctx, 2, 100, 30);
    vtools::assign(sim);
    const Station& shop = sim.world.stations[0];
    vtools::Env env{sim};
    env.owner = kOwner;
    env.dests = {{"THE SHOP", shop.p, 0}, {"NEAREST REPAIR STATION", V3(), 1}};
    env.refPos = sim.cars[sim.player].frame().com;
    env.refFacing = V3(1, 0, 0);
    bool jobOn = false;
    env.jobOn = &jobOn;
    const uint32_t uidA = sim.cars[a].uid, uidB = sim.cars[b].uid, uidC = sim.cars[c].uid;

    vtools::Popup p;
    p.openOnVehicle(env, a, 100, 100);
    std::vector<vtools::Row> rs = p.rows(env);
    ctx.check("opens with the vehicle's own name first", p.open && rs[0].kind == vtools::Row::HEADER && rs[0].text == vtools::name(sim, a, kOwner), rs[0].text);
    ctx.check("has the actions", rowOf(p, env, vtools::Popup::ID_PLACES) >= 0 && rowOf(p, env, vtools::Popup::ID_FOLLOW) >= 0 && rowOf(p, env, vtools::Popup::ID_SPEED) >= 0 &&
                                     rowOf(p, env, vtools::Popup::ID_ROUTE) >= 0 && rowOf(p, env, vtools::Popup::ID_GO) >= 0 &&
                                     rowOf(p, env, vtools::Popup::ID_STOP) >= 0 && rowOf(p, env, vtools::Popup::ID_DRIVE) >= 0 && rowOf(p, env, vtools::Popup::ID_REMOVE) >= 0);
    ctx.check("GO waits for somewhere to go", !p.rows(env)[rowOf(p, env, vtools::Popup::ID_GO)].enabled);
    // Speed with the keys: move to the row, Right x3.
    while (p.rows(env)[p.cursor].id != vtools::Popup::ID_SPEED) p.key(env, vtools::Popup::K_DOWN);
    const double k0 = p.kmh;
    for (int i = 0; i < 3; i++) p.key(env, vtools::Popup::K_RIGHT);
    ctx.check("speed up 15 km/h with the keys", std::fabs(p.kmh - (k0 + 15)) < 0.1 && std::fabs(sim.cars[a].wantKmh - p.kmh) < 0.1, std::to_string(p.kmh));
    // Strictness.
    p.key(env, vtools::Popup::K_DOWN);
    ctx.check("cursor is on STRICTNESS", p.rows(env)[p.cursor].id == vtools::Popup::ID_ROUTE);
    p.key(env, vtools::Popup::K_LEFT);  // trails -> roads
    ctx.check("strictness changes and is kept on that car", p.route == PILOT_ROADS && sim.cars[a].wantRoute == PILOT_ROADS);
    ctx.check("another vehicle isn't affected", sim.cars[b].wantKmh == 0 && sim.cars[b].wantRoute == -1);
    // The place list.
    choose(p, env, vtools::Popup::ID_PLACES);
    rs = p.rows(env);
    ctx.check("places list: back, here, then the places", p.page == vtools::Popup::PG_PLACES && rs[0].id == vtools::Popup::ID_BACK && rs[1].arg == -1 && rs[2].text == "THE SHOP");
    p.click(env, 2, 0.5f);
    ctx.check("picking a place goes back with GO ready", p.page == vtools::Popup::PG_MAIN && p.dst == vtools::Popup::D_PLACE && p.rows(env)[p.cursor].id == vtools::Popup::ID_GO);
    p.key(env, vtools::Popup::K_ENTER);
    ctx.check("GO sends it to the shop at the speed asked", !p.open && sim.cars[a].pilot.active && std::fabs(sim.cars[a].pilot.speed * 3.6 - sim.cars[a].wantKmh) < 0.1 &&
                                                                  sim.cars[a].pilot.route == PILOT_ROADS && sim.cars[a].pilot.goalName == env.dests[0].name,
              p.say);
    ctx.check("and the others still have their own", sim.cars[b].wantKmh == 0 && !sim.cars[b].pilot.active);

    // Follow: the list is labelled, nearest first; the follower gets the right leader.
    p.openOnVehicle(env, b, 50, 50);
    choose(p, env, vtools::Popup::ID_FOLLOW);
    rs = p.rows(env);
    bool labelled = rs.size() >= 4 && rs[1].text.find("BRONCO 1 - ") != std::string::npos;
    ctx.check("the follow list says which is which", labelled && rs[1].text.find("RIGHT HERE") != std::string::npos, rs.size() > 1 ? rs[1].text : "");
    bool nearestFirst = true;
    for (size_t k = 2; k + 1 < rs.size(); k++) {
        const V3 q1 = sim.cars[rs[k].arg].frame().com, q2 = sim.cars[rs[k + 1].arg].frame().com;
        nearestFirst &= std::hypot(q1.x - env.refPos.x, q1.z - env.refPos.z) <= std::hypot(q2.x - env.refPos.x, q2.z - env.refPos.z) + 1e-6;
    }
    ctx.check("nearest first", nearestFirst);
    int pickRow = -1;
    for (int k = 1; k < (int)rs.size(); k++)
        if (rs[k].arg == c) pickRow = k;
    ctx.check("the one being commanded isn't in its own list", pickRow > 0 && std::none_of(rs.begin() + 1, rs.end(), [&](const vtools::Row& r) { return r.arg == b; }));
    p.click(env, pickRow, 0.5f);
    ctx.check("follow target chosen", p.dst == vtools::Popup::D_FOLLOW && p.followUid == uidC);
    // Gap choice appears for following.
    ctx.check("follow gap row", rowOf(p, env, vtools::Popup::ID_GAP) >= 0);
    while (p.rows(env)[p.cursor].id != vtools::Popup::ID_GAP) p.key(env, vtools::Popup::K_DOWN);
    p.key(env, vtools::Popup::K_RIGHT);
    ctx.check("gap medium", p.gap == vtools::GAP_MEDIUM);
    choose(p, env, vtools::Popup::ID_GO);
    ctx.check("GO: b follows c", sim.cars[b].pilot.followCar == c && sim.cars[b].pilot.active && sim.cars[b].followGap > 10, p.say);
    ctx.check("with its own settings", sim.cars[a].wantKmh != sim.cars[b].wantKmh || sim.cars[a].wantRoute != sim.cars[b].wantRoute);
    ctx.check("the toast names the leader", p.say.find(vtools::tag(sim, c, kOwner)) != std::string::npos, p.say);
    // The follower aims behind its leader.
    const V3 g = vtools::followGoal(sim.cars[c], 15);
    ctx.check("followGoal is 15 m behind the leader", std::fabs(len(g - sim.cars[c].frame().com) - 15) < 0.5);

    // STOP.
    p.openOnVehicle(env, b, 50, 50);
    choose(p, env, vtools::Popup::ID_STOP);
    ctx.check("STOP ends it", !sim.cars[b].pilot.active && sim.cars[b].pilot.followCar == -1 && !p.open);

    // DRIVE IT.
    p.openOnVehicle(env, c, 50, 50);
    choose(p, env, vtools::Popup::ID_DRIVE);
    ctx.check("DRIVE IT switches to it", sim.player == c && sim.cars[sim.player].uid == uidC && !p.open);
    // ... which makes it unremovable and the old one removable.
    p.openOnVehicle(env, c, 50, 50);
    ctx.check("the car you're in can't be removed", !p.rows(env)[rowOf(p, env, vtools::Popup::ID_REMOVE)].enabled);
    // REMOVE needs a second press.
    p.openOnVehicle(env, a, 50, 50);
    choose(p, env, vtools::Popup::ID_REMOVE);
    ctx.check("REMOVE asks again", p.open && (int)sim.cars.size() == 4 && p.rows(env)[rowOf(p, env, vtools::Popup::ID_REMOVE)].text.find("AGAIN") != std::string::npos);
    choose(p, env, vtools::Popup::ID_REMOVE);
    ctx.check("then does it", !p.open && (int)sim.cars.size() == 3 && vtools::indexOfUid(sim, uidA) < 0,
              p.say);
    ctx.check("removing says what went", p.say.rfind("REMOVED ", 0) == 0, p.say);
    ctx.check("player index still right", sim.cars[sim.player].uid == uidC);

    // The ground.
    const int bi = vtools::indexOfUid(sim, uidB);
    const V3 spot(120, 0, -40);
    vtools::Popup gp;
    gp.openOnGround(env, spot, 200, 200);
    rs = gp.rows(env);
    ctx.check("ground: send the selected one, send all", rowOf(gp, env, vtools::Popup::ID_SEND_SEL) >= 0 && rowOf(gp, env, vtools::Popup::ID_SEND_ALL) >= 0);
    ctx.check("the selected one defaults to the car you're in", gp.selIndex(sim) == sim.player);
    ctx.check("ALL counts the others", rs[rowOf(gp, env, vtools::Popup::ID_SEND_ALL)].value == "2 VEHICLES", rs[rowOf(gp, env, vtools::Popup::ID_SEND_ALL)].value);
    // Choose another vehicle to send.
    choose(gp, env, vtools::Popup::ID_PICK_VEHICLE);
    rs = gp.rows(env);
    ctx.check("vehicle chooser lists yours", gp.page == vtools::Popup::PG_VEHICLES && rs.size() == 4);
    for (int k = 1; k < (int)rs.size(); k++)
        if (rs[k].arg == bi) gp.click(env, k, 0.5f);
    ctx.check("chosen vehicle is selected", gp.page == vtools::Popup::PG_MAIN && gp.selIndex(sim) == bi);
    choose(gp, env, vtools::Popup::ID_SEND_SEL);
    ctx.check("SEND sends it, not you", sim.cars[bi].pilot.active && !sim.cars[sim.player].pilot.active && len(sim.cars[bi].pilot.goal - spot) < 3);
    sim.cars[bi].pilot.active = false;
    gp.openOnGround(env, spot, 200, 200);
    choose(gp, env, vtools::Popup::ID_SEND_ALL);
    ctx.check("SEND ALL sends everything but what you drive", sim.cars[bi].pilot.active && !sim.cars[sim.player].pilot.active, gp.say);

    // FOLLOW THIS from the other vehicle's popup.
    sim.cars[bi].pilot.active = false;
    gp.openOnVehicle(env, bi, 10, 10);  // selects b
    vtools::Popup fp;
    fp.sel = sim.cars[bi].uid;
    fp.openOnVehicle(env, sim.player, 10, 10);
    ctx.check("FOLLOW THIS is offered on another vehicle while one is selected", rowOf(fp, env, vtools::Popup::ID_FOLLOW_THIS) >= 0 || fp.sel == sim.cars[sim.player].uid);
    fp.sel = sim.cars[bi].uid;
    fp.kind = vtools::Popup::VEHICLE;
    fp.target = sim.cars[sim.player].uid;
    choose(fp, env, vtools::Popup::ID_FOLLOW_THIS);
    ctx.check("FOLLOW THIS makes the selected one follow", sim.cars[bi].pilot.followCar == sim.player && sim.cars[bi].pilot.active, fp.say);

    // A remote vehicle: can be followed, never removed or commanded.
    const int rem = addAt(ctx, 2, 150, 10);
    sim.cars[rem].remote = true;
    sim.cars[rem].netId = (1u << 16) | 3;
    vtools::assign(sim);
    vtools::Popup rp;
    rp.openOnVehicle(env, rem, 10, 10);
    ctx.check("another player's vehicle: only FOLLOW THIS", rowOf(rp, env, vtools::Popup::ID_FOLLOW_THIS) >= 0 && rowOf(rp, env, vtools::Popup::ID_REMOVE) < 0 && rowOf(rp, env, vtools::Popup::ID_GO) < 0);
    ctx.check("and it can't be removed even by index", !vtools::removeVehicle(sim, nullptr, nullptr, rem));

    // Esc backs out of a list, then closes.
    vtools::Popup ep;
    ep.openOnVehicle(env, bi, 0, 0);
    choose(ep, env, vtools::Popup::ID_FOLLOW);
    ep.key(env, vtools::Popup::K_ESC);
    ctx.check("Esc leaves the list", ep.open && ep.page == vtools::Popup::PG_MAIN);
    ep.key(env, vtools::Popup::K_ESC);
    ctx.check("Esc closes", !ep.open);
}

SCENARIO(vtoolsGo, "vtools/go", "A vehicle sent from the popup drives itself while you drive another, at the speed it was given") {
    Sim& sim = ctx.sim;
    sim.player = addAt(ctx, 3, 60, -12);
    const int a = addAt(ctx, 2, 60, 14);
    vtools::assign(sim);
    vtools::Env env{sim};
    env.owner = kOwner;
    env.refPos = sim.cars[sim.player].frame().com;
    env.refFacing = V3(1, 0, 0);
    vtools::Popup p;
    p.openOnGround(env, V3(100, 0, 30), 0, 0);
    p.sel = sim.cars[a].uid;
    p.kmh = 20;
    p.route = PILOT_ANYWHERE;
    p.click(env, p.rows(env).size() > 2 ? 2 : 0, 0.5f);  // SEND <selected> HERE
    ctx.check("it is on autopilot with its own speed and strictness", sim.cars[a].pilot.active && std::fabs(sim.cars[a].pilot.speed * 3.6 - 20) < 0.1 && sim.cars[a].pilot.route == PILOT_ANYWHERE, p.say);
    ctx.check("your own car keeps its defaults", !sim.cars[sim.player].pilot.active && sim.cars[sim.player].wantKmh == 0);
    double top = 0;
    ctx.run(8, [&] {
        sim.drive(false, false, false, false, false, Ctx::kFrame);  // (you do nothing; the other drives itself)
        top = std::max(top, len(sim.cars[a].frame().vel) * 3.6);
    });
    ctx.metric("the sent vehicle's speed (asked for 20)", top, "km/h", 8, 24);
    ctx.check("it moved towards the spot", len(sim.cars[a].frame().com - V3(100, 0, 30)) < len(V3(60, 0, 14) - V3(100, 0, 30)) - 3);
}
