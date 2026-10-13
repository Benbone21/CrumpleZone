// Jobs: the stranded truck, the convoy, scouting; and that a job survives the trip over the
// network as text.
#include <cmath>

#include "../src/jobs.h"
#include "harness.h"

SCENARIO(jobsRecover, "jobs/recover", "A stranded truck is spawned out of fuel with a flat and a broken shaft; parked on a shop pad, the job's done") {
    World& w = ctx.sim.world;
    Car& me = ctx.add(vehicleCatalog()[3], -120, -50, 0);
    ctx.focus(me);
    std::vector<JobPlace> places = {{"OUT THERE", V3(-200, 0, 150)}, {"FAR AWAY", V3(250, 0, -250)}};
    std::vector<Job> jobs = makeJobs(w, places, 7);
    ctx.check("made recovery, convoy and scout jobs", jobs.size() == 4 && jobs[0].kind == JOB_RECOVER && jobs[2].kind == JOB_CONVOY && jobs[3].kind == JOB_SCOUT);
    Job j = jobs[0];
    // Strand it right on the shop pad, to check the finish.
    const Station st = w.stations[0];
    j.target = V3(st.p.x, 0, st.p.z);
    const int k = spawnWreck(ctx.sim, j);
    Car& wreck = ctx.sim.cars[k];
    ctx.check("out of fuel, a flat, a broken shaft", wreck.fuel == 0 && wreck.outOfFuel() && wreck.wheels[1].popped && wreck.wheels[2].shaftBroken);
    bool done = false;
    ctx.run(3, [&] { done |= updateJob(j, ctx.sim, {me.frame().com}, Ctx::kFrame); });
    ctx.check("on the pad: done", done && j.done);
    // Over the network and back.
    j.wreckId = 4242;
    Job back;
    ctx.check("parses", Job::parse(j.text(), back) && back.kind == JOB_RECOVER && back.wreckId == 4242 && back.title == j.title);
    Job scout = jobs[3];
    ctx.check("scout parses with its places", Job::parse(scout.text(), back) && back.spots.size() == scout.spots.size() && back.spotNames[0] == scout.spotNames[0]);
}

SCENARIO(jobsConvoyScout, "jobs/convoy-scout", "Convoy: done only once everyone's there; scout: each place ticks off when any truck gets near") {
    Sim& sim = ctx.sim;
    Job c;
    c.kind = JOB_CONVOY, c.target = V3(100, 0, 100);
    ctx.check("not with one still away", !updateJob(c, sim, {V3(105, 0, 98), V3(0, 0, 0)}, 0.1));
    ctx.check("everyone there", updateJob(c, sim, {V3(105, 0, 98), V3(95, 0, 110)}, 0.1));
    Job s;
    s.kind = JOB_SCOUT;
    s.spots = {V3(0, 0, 0), V3(200, 0, 0)};
    s.spotNames = {"A", "B"};
    s.visited = {false, false};
    ctx.check("first place", !updateJob(s, sim, {V3(5, 0, 5)}, 0.1) && s.visited[0]);
    ctx.check("second place: done", updateJob(s, sim, {V3(190, 0, 3)}, 0.1));
}
