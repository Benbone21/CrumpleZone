#include "jobs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace {

uint32_t nextRand(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

const Car* wreckCar(const Job& job, const Sim& sim) {
    if (job.wreckId)
        for (const Car& c : sim.cars)
            if (c.netId == job.wreckId) return &c;
    if (job.wreck >= 0 && job.wreck < (int)sim.cars.size()) return &sim.cars[job.wreck];
    return nullptr;
}

std::string dist(double d) {
    char b[32];
    if (d > 950) std::snprintf(b, sizeof b, "%.1f KM", d / 1000);
    else std::snprintf(b, sizeof b, "%.0f M", d);
    return b;
}

std::string clean(std::string s) {
    for (char& c : s)
        if (c == '|') c = '/';
    return s;
}

}  // namespace

std::string Job::text() const {
    std::ostringstream o;
    o << "job|" << kind << '|' << clean(title) << '|' << clean(about) << '|' << target.x << '|' << target.y << '|' << target.z << '|' << clean(targetName) << '|'
      << vehicle << '|' << wreckId << '|' << spots.size();
    for (size_t k = 0; k < spots.size(); k++) o << '|' << spots[k].x << '|' << spots[k].z << '|' << clean(spotNames[k]);
    return o.str();
}

bool Job::parse(const std::string& s, Job& out) {
    std::vector<std::string> f;
    std::string cur;
    for (char c : s) {
        if (c == '|') f.push_back(cur), cur.clear();
        else cur += c;
    }
    f.push_back(cur);
    if (f.size() < 11 || f[0] != "job") return false;
    Job j;
    j.kind = std::atoi(f[1].c_str());
    j.title = f[2], j.about = f[3];
    j.target = V3(std::atof(f[4].c_str()), std::atof(f[5].c_str()), std::atof(f[6].c_str()));
    j.targetName = f[7];
    j.vehicle = std::atoi(f[8].c_str());
    j.wreckId = (uint32_t)std::strtoul(f[9].c_str(), nullptr, 10);
    const int n = std::atoi(f[10].c_str());
    if ((int)f.size() < 11 + n * 3) return false;
    for (int k = 0; k < n; k++) {
        j.spots.push_back(V3(std::atof(f[11 + k * 3].c_str()), 0, std::atof(f[12 + k * 3].c_str())));
        j.spotNames.push_back(f[13 + k * 3]);
    }
    j.visited.assign(j.spots.size(), false);
    out = j;
    return true;
}

std::vector<Job> makeJobs(const World& w, const std::vector<JobPlace>& places, uint32_t seed) {
    std::vector<Job> out;
    if (places.empty()) return out;
    uint32_t s = seed * 2654435761u + 0x9e3779b9u;
    if (!s) s = 1;
    // Places far from the shops are the ones worth a job.
    auto shopDist = [&](const V3& p) {
        double best = 1e9;
        for (const Station& st : w.stations) best = std::min(best, std::hypot(st.p.x - p.x, st.p.z - p.z));
        return best;
    };
    std::vector<JobPlace> far;
    for (const JobPlace& p : places)
        if (shopDist(p.p) > 90) far.push_back(p);
    if (far.empty()) far = places;
    static const char* trucks[2] = {"BRONCO", "4RUNNER"};
    for (int k = 0; k < 2; k++) {
        const JobPlace& p = far[nextRand(s) % far.size()];
        Job j;
        j.kind = JOB_RECOVER;
        j.vehicle = 2 + (int)(nextRand(s) % 2);
        j.title = std::string("STRANDED ") + trucks[j.vehicle - 2] + " AT " + p.name;
        j.about = "OUT OF FUEL, A FLAT, A SNAPPED SHAFT. GET IT TO ANY REPAIR SHOP: STRAP IT, WINCH IT, CAN OF FUEL";
        // A little off the spot (it slid off the line).
        const double a = (nextRand(s) % 628) / 100.0;
        j.target = p.p + V3(std::cos(a) * 6, 0, std::sin(a) * 6);
        j.target.y = w.height(j.target.x, j.target.z);
        j.targetName = p.name;
        out.push_back(j);
    }
    {
        const JobPlace& p = far[nextRand(s) % far.size()];
        Job j;
        j.kind = JOB_CONVOY;
        j.title = "CONVOY TO " + p.name;
        j.about = "EVERYONE'S TRUCK WITHIN 30 M OF IT. WAIT FOR EACH OTHER, PULL EACH OTHER OUT";
        j.target = p.p;
        j.targetName = p.name;
        out.push_back(j);
    }
    {
        Job j;
        j.kind = JOB_SCOUT;
        j.title = "SCOUT THE MAP";
        j.about = "GET A TRUCK TO EACH OF THESE PLACES (SPLIT UP OR GO TOGETHER)";
        std::vector<JobPlace> pool = far;
        for (int k = 0; k < 4 && !pool.empty(); k++) {
            const size_t i = nextRand(s) % pool.size();
            j.spots.push_back(pool[i].p);
            j.spotNames.push_back(pool[i].name);
            pool.erase(pool.begin() + i);
        }
        j.visited.assign(j.spots.size(), false);
        if (!j.spots.empty()) out.push_back(j);
    }
    return out;
}

int spawnWreck(Sim& sim, Job& job) {
    const auto& cat = vehicleCatalog();
    const VehicleSpec& spec = cat[std::min(std::max(job.vehicle, 0), (int)cat.size() - 1)];
    const double yaw = std::fmod(job.target.x * 0.37 + job.target.z * 0.11, 2 * M_PI);
    sim.addCar(spec, V3(job.target.x, sim.world.clearY(job.target.x, job.target.z) + 0.1, job.target.z), yaw, V3(0.55, 0.42, 0.25));
    const int i = (int)sim.cars.size() - 1;
    Car& c = sim.cars[i];
    c.setDown(sim.world);
    c.fuel = 0;
    c.fuelUse = 1;
    c.handbrake = 1;
    if (c.wheels.size() > 1) c.wheels[1].popped = true;
    if (c.wheels.size() > 2) c.wheels[2].shaftBroken = true;
    job.wreck = i;
    return i;
}

bool updateJob(Job& job, const Sim& sim, const std::vector<V3>& players, double dt) {
    if (job.done) return false;
    job.time += dt;
    bool now = false;
    switch (job.kind) {
        case JOB_RECOVER: {
            const Car* c = wreckCar(job, sim);
            if (!c) break;
            const Frame f = c->frame();
            for (const Station& st : sim.world.stations)
                if (std::hypot(f.com.x - st.p.x, f.com.z - st.p.z) < st.r && len(f.vel) < 0.5) now = true;
            break;
        }
        case JOB_CONVOY: {
            if (players.empty()) break;
            now = true;
            for (const V3& p : players)
                if (std::hypot(p.x - job.target.x, p.z - job.target.z) > 30) now = false;
            break;
        }
        case JOB_SCOUT: {
            bool all = !job.spots.empty();
            for (size_t k = 0; k < job.spots.size(); k++) {
                for (const V3& p : players)
                    if (std::hypot(p.x - job.spots[k].x, p.z - job.spots[k].z) < 20) job.visited[k] = true;
                all = all && job.visited[k];
            }
            now = all;
            break;
        }
    }
    if (now) job.done = true;
    return now;
}

std::string jobStatus(const Job& job, const Sim& sim, const V3& me) {
    if (job.done) return "DONE";
    switch (job.kind) {
        case JOB_RECOVER: {
            const Car* c = wreckCar(job, sim);
            if (!c) return "THE TRUCK IS GONE";
            const Frame f = c->frame();
            const double toIt = len(f.com - me);
            if (toIt > 25) return "GET TO THE " + std::string(c->spec->name) + ": " + dist(toIt);
            int best = sim.nearestStation(f.com);
            if (best < 0) return "NO SHOP ON THIS MAP";
            return "GET IT TO A SHOP: " + dist(std::hypot(f.com.x - sim.world.stations[best].p.x, f.com.z - sim.world.stations[best].p.z));
        }
        case JOB_CONVOY: return "EVERYONE TO " + job.targetName + ": " + dist(std::hypot(me.x - job.target.x, me.z - job.target.z));
        case JOB_SCOUT: {
            int n = 0;
            for (bool v : job.visited) n += v;
            return std::to_string(n) + " OF " + std::to_string(job.spots.size()) + " PLACES SCOUTED";
        }
    }
    return "";
}

bool jobMarker(const Job& job, const Sim& sim, const V3& me, V3& at, std::string& label) {
    if (job.done) return false;
    switch (job.kind) {
        case JOB_RECOVER: {
            const Car* c = wreckCar(job, sim);
            if (!c) return false;
            const Frame f = c->frame();
            if (len(f.com - me) > 25) {
                at = f.com, label = std::string("STRANDED ") + c->spec->name;
                return true;
            }
            const int st = sim.nearestStation(f.com);
            if (st < 0) return false;
            at = sim.world.stations[st].p, label = sim.world.stations[st].name;
            return true;
        }
        case JOB_CONVOY: at = job.target, label = job.targetName; return true;
        case JOB_SCOUT: {
            double best = 1e18;
            for (size_t k = 0; k < job.spots.size(); k++) {
                if (job.visited[k]) continue;
                const double d = std::hypot(job.spots[k].x - me.x, job.spots[k].z - me.z);
                if (d < best) best = d, at = job.spots[k], label = job.spotNames[k];
            }
            return best < 1e17;
        }
    }
    return false;
}
