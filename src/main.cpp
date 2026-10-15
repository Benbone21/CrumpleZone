// Crumple Zone: a soft-body driving sandbox.
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <functional>
#include <map>

#include "audio.h"
#include "editor.h"
#include "fx.h"
#include "gl.h"
#include "hudkit.h"
#include "level.h"
#include "levelrun.h"
#include "menu.h"
#include "mods.h"
#include "net.h"
#include "pilotsense.h"
#include "render.h"
#include "sdl_min.h"
#include "softbody.h"
#include "../tests/harness.h"

namespace {

const double kTimeScales[] = {1.0, 0.5, 0.25, 0.1, 0.03};

struct Options {
    int width = 1600, height = 900;
    bool fullscreen = false, audio = true, vsync = true;
    // Scripted capture (for testing): fixed 60 Hz steps, writes PPM screenshots.
    std::string shotPrefix;
    std::set<int> shotFrames;
    std::string script;  // "", "drive", "left"
    int camera = 0;
    int debug = 0;
    int player = 0;
    std::string scenario;  // play a test scenario instead of the sandbox
    bool orbitSet = false, showHelp = false, showGarage = false;
    int mods[3] = {-1, -1, -1};  // --mods E,S,T: fit these to the player's car (testing)
    int menu = -1;                // --menu P: open the pause menu on page P (testing)
    int quality = -1;             // --quality 0-3: graphics preset (testing)
    bool mph = false;             // --mph
    int map = -1;                 // --map N: 0 proving ground, 1 mountain
    double orbitYaw = 0, orbitPitch = 0.2, orbitDist = 7.5;  // degrees, degrees, meters
    bool spawnSet = false;
    int resumeModel = -1;  // --resume-model: the vehicle to be in (after a reload)
    double spawnX = 0, spawnZ = 0, spawnHeading = 0;  // heading in degrees, 0 = +X
    int hostPort = -1;            // --host [PORT]: open a multiplayer session
    std::string join;             // --join HOST[:PORT]
    std::string name;             // --name NAME (multiplayer)
    double timeHours = -1;        // --time H: hold the clock at H hours (0..24), e.g. 21.5
    int lamps = -1;               // --lamps 0-3: headlight switch (LampMode: auto, low, high, off)
    std::string level;            // --level NAME: play the level whose name has NAME in it
    bool editor = false;          // --editor: open the world editor on the map
};

struct CameraRig {
    int mode = 0;  // 0 chase, 1 orbit, 2 cockpit
    double chaseYaw = 0, userYaw = 0, orbitYaw = 0.6, pitch = 0.2, dist = 7.5, idle = 0;
    float fovy = 1.15f;  // radians (settings: FIELD OF VIEW)
    V3 target;
    bool initialized = false;

    View update(const Frame& fr, const World& world, double dt, bool dragging) {
        double carYaw = std::atan2(-fr.fwd.z, fr.fwd.x);
        V3 want = fr.com + V3(0, 0.55, 0);
        if (!initialized) {
            chaseYaw = carYaw;
            target = want;
            initialized = true;
        }
        target += (want - target) * (1 - std::exp(-dt * 14));
        if (std::isfinite(carYaw)) chaseYaw += wrapAngle(carYaw - chaseYaw) * (1 - std::exp(-dt * 3));
        idle = dragging ? 0 : idle + dt;
        if (mode == 0 && idle > 1.5) {
            userYaw *= std::exp(-dt * 2);
            if (pitch < -0.05) pitch += (-0.05 - pitch) * (1 - std::exp(-dt * 2));  // let go: back to looking at the car
        }

        View v;
        v.fovy = fovy;
        if (mode == 2) {
            // (Dragging looks round the cabin: sideways, and up or down from straight ahead.)
            v.eye = fr.com + fr.fwd * 0.1 + fr.up * 0.55 + fr.left * 0.35;
            const double lookUp = clampd(0.2 - pitch, -0.9, 1.1);
            const V3 flat = fr.fwd * std::cos(userYaw) + fr.left * std::sin(userYaw);
            v.target = v.eye + (flat * std::cos(lookUp) + fr.up * std::sin(lookUp)) * 10.0;
            v.up = fr.up;
            v.fovy = fovy * 1.087f;
            return v;
        }
        double yaw = mode == 0 ? chaseYaw + userYaw : orbitYaw;
        // Dragged below level: the camera stays down by the ground behind the car and tilts
        // up instead (to the sky, the trees, the cliff above).
        const double camPitch = std::max(pitch, -0.05), lookUp = std::max(0.0, -0.05 - pitch);
        v.target = target;
        v.eye = target + V3(-std::cos(yaw) * std::cos(camPitch), std::sin(camPitch), std::sin(yaw) * std::cos(camPitch)) * dist;
        if (lookUp > 0) v.target = target + V3(0, std::tan(lookUp) * dist, 0);
        // Pull the camera in front of walls and ramps instead of clipping through them.
        V3 ray = v.eye - v.target;
        double L = len(ray);
        Contact ct;
        for (double t = 0.5; t < L; t += 0.2)
            if (world.contact(v.target + ray * (t / L), 0.3, nullptr, 0, ct)) {
                v.eye = v.target + ray * (std::max(0.5, t - 0.3) / L);
                break;
            }
        v.eye.y = std::max(v.eye.y, world.height(v.eye.x, v.eye.z) + 0.4);
        v.up = V3(0, 1, 0);
        return v;
    }

    void drag(int dx, int dy) {
        if (mode == 1) orbitYaw -= dx * 0.006;
        else userYaw -= dx * 0.006;
        pitch = clampd(pitch + dy * 0.005, -1.0, 1.35);
    }
};

void writePPM(const std::string& path, const std::vector<unsigned char>& px, int w, int h) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) std::fwrite(&px[y * w * 3], 1, w * 3, f);  // GL rows are bottom-up
    std::fclose(f);
}

void fail(const std::string& msg) {
    std::fprintf(stderr, "crumplezone: %s\n", msg.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Crumple Zone", msg.c_str(), nullptr);
}

// Hot reload (Linux): watches the game's own executable. Once a rebuild has landed (the
// file changed, then stopped changing for a moment), the HUD offers F5, which restarts the
// game on the new build in the same map, vehicle and spot.
struct BuildWatch {
    std::string exe;
    long long mtime = 0, lastSize = -1;
    int frames = 0;
    bool ready = false;
    void start() {
#ifndef _WIN32
        char buf[4096];
        const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n <= 0) return;
        buf[n] = 0;
        exe = buf;
        struct stat st;
        if (stat(exe.c_str(), &st) == 0) mtime = (long long)st.st_mtime;
#endif
    }
    // Call once a frame; true once a new build is there.
    bool poll() {
#ifndef _WIN32
        if (exe.empty() || ready || ++frames % 60) return ready;
        struct stat st;
        if (stat(exe.c_str(), &st) != 0) return false;  // (mid-link: not there yet)
        const long long size = (long long)st.st_size;
        if ((long long)st.st_mtime != mtime && size == lastSize && (st.st_mode & S_IXUSR)) ready = true;
        lastSize = size;
#endif
        return ready;
    }
    // Restarts on the new build with these extra arguments (replacing the old ones).
    void exec(int argc, char** argv, const std::vector<std::string>& extra) {
#ifndef _WIN32
        static const std::set<std::string> withValue = {"--map", "--player", "--spawn", "--resume-model", "--scenario", "--script",
                                                        "--shot-prefix", "--shot-frames", "--menu", "--orbit"};
        static const std::set<std::string> flags = {"--garage", "--show-help"};
        std::vector<std::string> args = {exe};
        for (int i = 1; i < argc; i++) {
            const std::string a = argv[i];
            if (withValue.count(a)) i++;
            else if (!flags.count(a)) args.push_back(a);
        }
        args.insert(args.end(), extra.begin(), extra.end());
        std::vector<char*> cargs;
        for (std::string& a : args) cargs.push_back(&a[0]);
        cargs.push_back(nullptr);
        execv(exe.c_str(), cargs.data());
        std::perror("crumplezone: reload failed");
#else
        (void)argc, (void)argv, (void)extra;
#endif
    }
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--no-audio") o.audio = false;
        else if (a == "--no-vsync") o.vsync = false;
        else if (a == "--size") {
            std::string s = next();
            std::sscanf(s.c_str(), "%dx%d", &o.width, &o.height);
        } else if (a == "--shot-prefix") o.shotPrefix = next();
        else if (a == "--shot-frames") {
            std::string s = next();
            for (char* t = std::strtok(&s[0], ","); t; t = std::strtok(nullptr, ",")) o.shotFrames.insert(std::atoi(t));
        } else if (a == "--script") o.script = next();
        else if (a == "--camera") o.camera = std::atoi(next().c_str());
        else if (a == "--debug-view") o.debug = std::atoi(next().c_str());
        else if (a == "--player") o.player = std::atoi(next().c_str());
        else if (a == "--scenario") o.scenario = next();
        else if (a == "--show-help") o.showHelp = true;
        else if (a == "--garage") o.showGarage = true;
        else if (a == "--menu") o.menu = std::atoi(next().c_str());
        else if (a == "--quality") o.quality = std::atoi(next().c_str());
        else if (a == "--mph") o.mph = true;
        else if (a == "--map") o.map = std::atoi(next().c_str());
        else if (a == "--mods") {
            std::string s = next();
            std::sscanf(s.c_str(), "%d,%d,%d", &o.mods[0], &o.mods[1], &o.mods[2]);
        }
        else if (a == "--orbit") {
            std::string s = next();
            o.orbitSet = std::sscanf(s.c_str(), "%lf,%lf,%lf", &o.orbitYaw, &o.orbitPitch, &o.orbitDist) == 3;
        }
        else if (a == "--spawn") {
            std::string s = next();
            o.spawnSet = std::sscanf(s.c_str(), "%lf,%lf,%lf", &o.spawnX, &o.spawnZ, &o.spawnHeading) >= 2;
        }
        else if (a == "--host") {
            o.hostPort = net::kDefaultPort;
            if (i + 1 < argc && std::isdigit((unsigned char)argv[i + 1][0])) o.hostPort = std::atoi(argv[++i]);
        } else if (a == "--join") o.join = next();
        else if (a == "--name") o.name = next();
        else if (a == "--resume-model") o.resumeModel = std::atoi(next().c_str());
        else if (a == "--time") o.timeHours = std::fmod(std::max(0.0, std::atof(next().c_str())), 24.0);
        else if (a == "--lamps") o.lamps = std::atoi(next().c_str());
        else if (a == "--level") o.level = next();
        else if (a == "--editor") o.editor = true;
        else if (a == "--help" || a == "-h") {
            std::puts("usage: crumplezone [--fullscreen] [--size WxH] [--no-audio] [--no-vsync] [--spawn X,Z[,HEADING]] [--scenario NAME]\n"
                      "                   [--map N] [--host [PORT]] [--join HOST[:PORT]] [--name NAME] [--time HOURS] [--level NAME]");
            std::exit(0);
        }
    }
    return o;
}

// Runs a test scenario on a worker thread, one 60 Hz frame at a time, handing the
// simulation back to the render loop after every frame (they never touch it together).
struct Playback {
    const Scenario* scenario = nullptr;
    std::unique_ptr<Ctx> ctx;
    std::thread worker;
    std::mutex m;
    std::condition_variable cv;
    bool simTurn = false, done = false, abort = false;
    double accum = 0;

    void start(const Scenario* sc, Sim& sim) {
        scenario = sc;
        sim.setupEmpty();
        ctx = std::make_unique<Ctx>(sim);
        ctx->abortFlag = &abort;
        ctx->onStep = [this] {
            std::unique_lock<std::mutex> lk(m);
            simTurn = false;
            cv.notify_all();
            cv.wait(lk, [this] { return simTurn || abort; });
        };
        worker = std::thread([this] {
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [this] { return simTurn || abort; });
            }
            if (!abort) scenario->body(*ctx);
            std::lock_guard<std::mutex> lk(m);
            done = true;
            simTurn = false;
            cv.notify_all();
        });
    }
    // Lets the scenario advance one frame; returns false once it has finished.
    bool stepFrame() {
        std::unique_lock<std::mutex> lk(m);
        if (done) return false;
        simTurn = true;
        cv.notify_all();
        cv.wait(lk, [this] { return !simTurn || done; });
        return !done;
    }
    void stop() {
        {
            std::lock_guard<std::mutex> lk(m);
            abort = true;
            cv.notify_all();
        }
        if (worker.joinable()) worker.join();
    }
    bool active() const { return scenario != nullptr; }
    bool passed() const {
        for (const CheckResult& c : ctx->checks)
            if (!c.ok) return false;
        for (const MetricResult& mr : ctx->metrics)
            if (!mr.ok) return false;
        return true;
    }
};

// ---- HUD ------------------------------------------------------------------------
// Minimal by design: a gauge cluster bottom-right, a small vehicle card bottom-left,
// short-lived toasts at the top, and help only when asked for.

struct Toast {
    std::string text;
    double age = 0;
};

// World point to screen pixels (false if behind the camera).
bool project(const View& v, const V3& p, float& sx, float& sy) {
    const V3 fwd = norm(v.target - v.eye), right = norm(cross(fwd, v.up)), up = cross(right, fwd), d = p - v.eye;
    const double z = dot(d, fwd), f = 1.0 / std::tan(v.fovy / 2);
    if (z < 0.3) return false;
    sx = v.width / 2.0f + (float)(dot(d, right) / z * f * v.height / 2);
    sy = v.height / 2.0f - (float)(dot(d, up) / z * f * v.height / 2);
    return true;
}

// Picking a winch anchor (V): everything in reach is marked, the one nearest where the
// camera looks is selected; drag to aim, wheel or Tab for the next, V or a click hooks it.
struct WinchAim {
    bool on = false;
    std::vector<WinchTarget> targets;
    int sel = -1;
    double refresh = 0;
    V3 selAt;
    void pickCentered(const View& v) {
        float best = 1e9f;
        for (int k = 0; k < (int)targets.size(); k++) {
            float sx, sy;
            if (!project(v, targets[k].at, sx, sy)) continue;
            const float d = std::hypot(sx - v.width / 2.0f, sy - v.height * 0.45f) + (targets[k].inArc ? 0 : 4000);
            if (d < best) best = d, sel = k;
        }
        if (sel >= 0) selAt = targets[sel].at;
    }
    void update(const Sim& sim, double dt) {
        if (!on || (refresh -= dt) > 0) return;
        refresh = 0.3;
        targets = sim.winchTargets(sim.player);
        // Keep the same anchor selected as the truck (or the anchor) moves.
        int keep = -1;
        double bd = 2.0;
        for (int k = 0; k < (int)targets.size(); k++)
            if (len(targets[k].at - selAt) < bd) bd = len(targets[k].at - selAt), keep = k;
        sel = keep >= 0 ? keep : targets.empty() ? -1 : std::min(std::max(sel, 0), (int)targets.size() - 1);
        if (sel >= 0) selAt = targets[sel].at;
    }
    void step(int dir) {
        if (targets.empty()) return;
        sel = ((sel < 0 ? 0 : sel + dir) % (int)targets.size() + (int)targets.size()) % (int)targets.size();
        selAt = targets[sel].at;
    }
};

// Where the autopilot can go on this map (the menu's DRIVE TO list).
struct Place {
    std::string name;
    V3 p;
    int kind = 0;  // 0 a spot, 1 nearest repair station, 2 where the camera looks, 3 follow a vehicle, 4 loop road
    int car = -1;
};
std::vector<Place> pilotPlaces(const Sim& sim, const NetSession& net) {
    std::vector<Place> out;
    out.push_back({"NEAREST REPAIR STATION", V3(), 1});
    for (const Station& st : sim.world.stations) out.push_back({st.name, st.p});
    if (sim.world.map == MAP_MOUNTAIN) {
        out.push_back({"TRAILHEAD", sim.world.spawn});
        out.push_back({"SUMMIT LOOKOUT (TRAIL END)", mountainFeature("lookout")});
        out.push_back({"CRAWL PARK", mountainFeature("dome") + V3(0, 0, -34)});
        out.push_back({"WIDOWMAKER SPUR (START)", mountainFeature("spur") - norm(mountainFeature("pinnacle") - mountainFeature("spur")) * 6});
        out.push_back({"BOULDER GULLY (FOOT)", mountainFeature("gully") - norm(mountainFeature("gullytop") - mountainFeature("gully")) * 8});
        out.push_back({"THE LAKE", V3(140, 0, -378)});
    } else if (sim.world.map == MAP_FRONTIER) {
        out.push_back({"PADDOCK", frontierFeature("start")});
        out.push_back({"MESA CLIMB (FOOT)", frontierFeature("mesaclimb") + V3(8, 0, 0)});
        out.push_back({"SLICKROCK BOWL", frontierFeature("bowl")});
        out.push_back({"THE WALL (FOOT)", frontierFeature("wall") - norm(frontierFeature("walltop") - frontierFeature("wall")) * 10});
        out.push_back({"SUMMIT", frontierFeature("summit")});
        out.push_back({"FROZEN TARN", frontierFeature("tarn")});
        out.push_back({"SNAKE RIVER FORD", frontierFeature("ford1")});
        out.push_back({"ECHO LAKE BEACH", frontierFeature("lake") + V3(-70, 0, 30)});
        out.push_back({"MUD BOG", frontierFeature("bog")});
        out.push_back({"MUD DELTA", frontierFeature("delta")});
        out.push_back({"BLACKWATER BAYOU", frontierFeature("bayou:40")});
        out.push_back({"THE WALLOW (EDGE)", frontierFeature("bayou:78")});
        out.push_back({"MUD HILL", frontierFeature("mudhill")});
        out.push_back({"COLD CREEK VALLEY", frontierFeature("snowfield")});
        out.push_back({"COLD CREEK LODGE", frontierFeature("lodge")});
    } else if (sim.world.map == MAP_PROVING_GROUND) {
        out.push_back({"SPAWN", sim.world.spawn});
        out.push_back({"CRASH WALL", V3(95, 0, 0)});
        out.push_back({"JUMP RAMPS", V3(28, 0, -30)});
        out.push_back({"WASHBOARD", V3(-6, 0, -70)});
    }
    out.push_back({"WHERE I'M LOOKING", V3(), 2});
    for (int i = 0; i < (int)sim.cars.size(); i++) {
        const Car& c = sim.cars[i];
        if (i == sim.player || c.isProp()) continue;
        const std::string who = c.remote ? net.ownerName(c.netId) + "'S " + c.spec->name : std::string(c.spec->name);
        out.push_back({"FOLLOW " + who, V3(), 3, i});
    }
    if (!sim.world.loopRoad.empty()) out.push_back({"CRUISE THE LOOP ROAD", V3(), 4});
    return out;
}

// The route ahead on the ground, and what the autopilot is up to: the line it's taking,
// the ones it tried, what it sees ahead and what it's doing about each thing.
void drawPilot(Renderer& r, const Sim& sim, const View& view, float u, int W, int H) {
    const Car& car = sim.cars[sim.player];
    if (!car.pilot.active && !car.ai) return;
    if (car.pilot.active) {
        const Autopilot& ap = car.pilot;
        for (int k = ap.idx; k < std::min((int)ap.path.size(), ap.idx + 90); k += 2) {
            float sx, sy;
            if (project(view, ap.path[k] + V3(0, 0.25, 0), sx, sy)) r.rect(sx - 2.5f * u, sy - 2.5f * u, 5 * u, 5 * u, 0.4f, 0.85f, 1.0f, 0.85f);
        }
        // Lines it tried: faint, red where they hit something (not drawn under the truck).
        const Frame fr = car.frame();
        auto underTruck = [&](const V3& p) { return dot(p - fr.com, fr.fwd) < car.spec->length * 0.5 + 0.3; };
        for (size_t k = 0; k < ap.options.size(); k++)
            for (const V3& p : ap.options[k]) {
                float sx, sy;
                if (underTruck(p) || !project(view, p, sx, sy)) continue;
                if (ap.optionOk[k]) r.rect(sx - 1.5f * u, sy - 1.5f * u, 3 * u, 3 * u, 0.85f, 0.85f, 0.85f, 0.22f);
                else r.rect(sx - 1.5f * u, sy - 1.5f * u, 3 * u, 3 * u, 1.0f, 0.3f, 0.25f, 0.35f);
            }
        // The one it's taking.
        for (const V3& p : ap.line) {
            float sx, sy;
            if (!underTruck(p) && project(view, p, sx, sy)) r.rect(sx - 3 * u, sy - 3 * u, 6 * u, 6 * u, 0.35f, 1.0f, 0.5f, 0.9f);
        }
        // What it sees.
        const float ts = hud::px(1.0f * u);
        for (const PilotSeen& s : ap.seen) {
            float sx, sy;
            if (!project(view, s.at + V3(0, 0.15, 0), sx, sy)) continue;
            static const char* words[] = {"", "OVER", "STRADDLE", "AROUND LEFT", "AROUND RIGHT", "BLOCKED"};
            float cr = 0.8f, cg = 0.8f, cb = 0.8f;
            if (s.verdict == VERDICT_OVER || s.verdict == VERDICT_STRADDLE) cr = 1.0f, cg = 0.8f, cb = 0.25f;
            else if (s.verdict == VERDICT_AROUND_LEFT || s.verdict == VERDICT_AROUND_RIGHT) cr = 0.35f, cg = 0.85f, cb = 1.0f;
            else if (s.verdict == VERDICT_BLOCKING) cr = 1.0f, cg = 0.3f, cb = 0.25f;
            const float b = 7 * u;  // corner brackets
            r.rect(sx - b, sy - b, 3 * u, u, cr, cg, cb, 0.9f);
            r.rect(sx - b, sy - b, u, 3 * u, cr, cg, cb, 0.9f);
            r.rect(sx + b - 3 * u, sy - b, 3 * u, u, cr, cg, cb, 0.9f);
            r.rect(sx + b - u, sy - b, u, 3 * u, cr, cg, cb, 0.9f);
            r.rect(sx - b, sy + b - u, 3 * u, u, cr, cg, cb, 0.9f);
            r.rect(sx - b, sy + b - 3 * u, u, 3 * u, cr, cg, cb, 0.9f);
            r.rect(sx + b - 3 * u, sy + b - u, 3 * u, u, cr, cg, cb, 0.9f);
            r.rect(sx + b - u, sy + b - 3 * u, u, 3 * u, cr, cg, cb, 0.9f);
            char lab[64];
            if (s.kind == SEEN_TREE || s.kind == SEEN_VEHICLE || s.kind == SEEN_WALL) std::snprintf(lab, sizeof lab, "%s", seenName(s.kind));
            else std::snprintf(lab, sizeof lab, "%s %.2fM", seenName(s.kind), s.height);
            r.text(sx + b + 3 * u, sy - b, ts, lab, cr, cg, cb, 0.95f);
            if (s.verdict != VERDICT_PASSING) r.text(sx + b + 3 * u, sy - b + 9 * ts, ts, words[s.verdict], cr, cg, cb, 0.95f);
        }
    }
    char buf[200];
    if (car.ai) std::snprintf(buf, sizeof buf, "AUTOPILOT: CRUISING THE LOOP ROAD   (U OR STEER TO STOP)");
    else std::snprintf(buf, sizeof buf, "AUTOPILOT: %s   %.0f KM/H   %s   (U OR STEER TO STOP)", car.pilot.status.c_str(), car.pilot.speed * 3.6,
                       pilotRouteName(car.pilot.route));
    hud::centeredPill(r, W / 2.0f, H - 140 * u, hud::px(1.4f * u), buf, 0.02f, 0.05f, 0.08f, 0.95f, 0.5f, 0.85f, 1.0f);
    // How it has the truck set up.
    if (car.pilot.active && car.mode()) {
        std::string set = std::string("SETUP: ") + car.mode()->name;
        if (car.lockFront && car.lockRear) set += "  BOTH LOCKERS";
        else if (car.lockRear) set += "  REAR LOCKER";
        else if (car.lockFront) set += "  FRONT LOCKER";
        if (car.swayDisconnected) set += "  SWAY BAR OFF";
        char psi[24];
        std::snprintf(psi, sizeof psi, "  %.0f PSI", car.tirePsi);
        set += psi;
        char climb[48];
        std::snprintf(climb, sizeof climb, "  CLIMBS %.2f M", pilotCanClimb(car, false));
        set += climb;
        hud::centeredPill(r, W / 2.0f, H - 168 * u, hud::px(1.1f * u), set, 0.02f, 0.05f, 0.08f, 0.85f, 0.75f, 0.9f, 0.75f);
    }
}

struct HudState {
    std::vector<Toast> toasts;
    bool newBuild = false;  // the game's been rebuilt since it started: F5 reloads it
    double bestKmh = 0;     // freeway: fastest so far
    // The Frontier's race circuit: time on the lap running (-1: not on one), the last and
    // the best, and which side of the start line you were on.
    double lapTime = -1, lastLap = 0, bestLap = 0, lapSide = 0;
    bool showHelp = false, showFps = false, scenarioDetail = false;
    double confirmReset = 0;  // R just pressed: a second press within this long respawns at the start
    double hintTime = 7.0;  // "press H for controls" fades out after this
    float needle = 0;       // smoothed tach position 0..1
    void toast(const std::string& text) {
        for (Toast& t : toasts)
            if (t.text == text) {
                t.age = 0;
                return;
            }
        toasts.push_back({text, 0});
        if (toasts.size() > 3) toasts.erase(toasts.begin());
    }
    void update(double dt) {
        confirmReset = std::max(0.0, confirmReset - dt);
        hintTime -= dt;
        for (Toast& t : toasts) t.age += dt;
        toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [](const Toast& t) { return t.age > 2.6; }), toasts.end());
    }
};

using namespace hud;

void drawGauges(Renderer& r, const Car& car, HudState& hud, float u, int W, int H, double dt, bool mph) {
    const float R = 92 * u, cx = W - R - 26 * u, cy = H - R - 18 * u;
    const float a0 = 0.75f * (float)M_PI, a1 = 2.25f * (float)M_PI;
    const double redline = car.spec->redline, maxRpm = std::ceil((redline + 600) / 1000) * 1000;
    float target = (float)clampd(car.rpm / maxRpm, 0, 1);
    hud.needle += (target - hud.needle) * (float)std::min(1.0, dt * 14);

    r.arc(cx, cy, 0, R, 0, 2 * (float)M_PI, 0.03f, 0.035f, 0.045f, 0.55f, 48);  // dial face
    r.arc(cx, cy, R - 9 * u, R - 4 * u, a0, a1, 0.35f, 0.37f, 0.4f, 0.55f);      // track
    float red0 = a0 + (a1 - a0) * (float)(redline / maxRpm);
    r.arc(cx, cy, R - 9 * u, R - 4 * u, red0, a1, 0.85f, 0.15f, 0.12f, 0.9f, 12);  // redline zone
    float fillEnd = a0 + (a1 - a0) * hud.needle;
    r.arc(cx, cy, R - 9 * u, R - 4 * u, a0, std::min(fillEnd, red0), 0.98f, 0.62f, 0.18f, 0.95f, 40);
    // Ticks every 1000 rpm, labelled.
    for (int k = 0; k <= (int)(maxRpm / 1000); k++) {
        float a = a0 + (a1 - a0) * (float)(k * 1000 / maxRpm);
        r.arc(cx, cy, R - 16 * u, R - 10 * u, a - 0.012f, a + 0.012f, 0.85f, 0.85f, 0.88f, 0.9f, 1);
        std::string n = std::to_string(k);
        float ts = px(1.1f * u), tx = cx + std::cos(a) * (R - 24 * u) - r.textWidth(n, ts) / 2, ty = cy + std::sin(a) * (R - 24 * u) - 4 * ts;
        r.text(tx, ty, ts, n, 0.75f, 0.76f, 0.8f, 0.9f);
    }
    // Needle.
    float an = a0 + (a1 - a0) * hud.needle, ca = std::cos(an), sa = std::sin(an);
    float nx = -sa * 2.2f * u, ny = ca * 2.2f * u;
    r.tri(cx + nx, cy + ny, cx - nx, cy - ny, cx + ca * (R - 8 * u), cy + sa * (R - 8 * u), 1.0f, 0.3f, 0.2f, 1);
    r.arc(cx, cy, 0, 6 * u, 0, 2 * (float)M_PI, 0.12f, 0.12f, 0.14f, 1, 16);

    // Speed and gear.
    Frame fr = car.frame();
    std::string spd = std::to_string((int)std::lround(len(fr.vel) * (mph ? 2.23694 : 3.6)));
    float ss = px(3.5f * u), sm = px(1.2f * u);
    label(r, cx - r.textWidth(spd, ss) / 2, cy + 12 * u, ss, spd, 1, 1, 1);
    const char* unit = mph ? "MPH" : "KM/H";
    r.text(cx - r.textWidth(unit, sm) / 2, cy + 12 * u + 9 * ss, sm, unit, 0.7f, 0.72f, 0.76f);
    // Gear in a small box in the dial's bottom opening.
    std::string gear = car.engineDead ? "-" : car.gear < 0 ? "R" : std::to_string(car.gear);
    float gs = px(2.2f * u), gw = 12 * gs * 0.5f + 8 * u, gh = 8 * gs + 6 * u, gy = cy + R - gh - 2 * u;
    pill(r, cx - gw / 2, gy, gw, gh, 0.0f, 0.0f, 0.0f, 0.55f);
    r.text(cx - r.textWidth(gear, gs) / 2, gy + 3 * u + gs * 0.5f, gs, gear, 1.0f, 0.72f, 0.25f);
    // Indicator lights above the hub.
    float is = px(1.0f * u), iy = cy - 26 * u;
    std::vector<std::pair<std::string, std::array<float, 3>>> lights;
    if (!car.tractionControl) lights.push_back({"TC OFF", {1.0f, 0.75f, 0.2f}});
    if (car.manualShift) lights.push_back({"MAN", {0.6f, 0.9f, 0.5f}});
    if (const DriveMode* m = car.isProp() ? nullptr : car.mode())
        lights.push_back({m->name, m->low ? std::array<float, 3>{1.0f, 0.75f, 0.2f}
                                   : m->front ? std::array<float, 3>{0.5f, 0.9f, 0.5f} : std::array<float, 3>{0.7f, 0.72f, 0.76f}});
    if (car.lockFront) lights.push_back({"F LOCK", {1.0f, 0.75f, 0.2f}});
    if (car.lockRear) lights.push_back({"R LOCK", {1.0f, 0.75f, 0.2f}});
    if (car.swayDisconnected) lights.push_back({"SWAY", {1.0f, 0.75f, 0.2f}});
    if (car.handbrake > 0.5) lights.push_back({"P", {1.0f, 0.3f, 0.25f}});
    if (car.ai) lights.push_back({"AUTO", {0.4f, 0.85f, 1.0f}});
    // What the tires are on (most of them), and how they're doing in it.
    if (!car.isProp()) {
        int count[SURF_COUNT] = {0};
        double sink = 0, clog = 0;
        int on = 0;
        for (const Wheel& wh : car.wheels)
            if (wh.load > 0) count[wh.surface]++, on++, sink = std::max(sink, (double)wh.sink), clog = std::max(clog, (double)wh.clog);
        if (on > 0) {
            int s = 0;
            for (int k = 1; k < SURF_COUNT; k++)
                if (count[k] > count[s]) s = k;
            std::string txt = std::string(surfaceInfo((Surface)s).name) + "  " + tireTypeName(car.spec->tireType) + " TIRES";
            if (sink > 0.02) txt += "  SUNK " + std::to_string((int)std::lround(sink * 100)) + " CM";
            if (clog > 0.35) txt += "  TREAD PACKED WITH MUD";
            const float* dc = surfaceInfo((Surface)s).dust;
            const float ts2 = px(1.0f * u);
            const float tw = r.textWidth(txt, ts2);
            const float tx = std::min(cx - tw / 2, W - tw - 14 * u);  // (kept on screen)
            pill(r, tx - 6 * u, cy - R - 26 * u, tw + 12 * u, 10 * ts2 + 4 * u, 0.0f, 0.0f, 0.0f, 0.45f);
            r.text(tx, cy - R - 24 * u, ts2, txt, 0.55f + 0.45f * dc[0], 0.55f + 0.45f * dc[1], 0.55f + 0.45f * dc[2], 0.95f);
        }
    }
    float total = 0;
    for (auto& l : lights) total += r.textWidth(l.first, is) + 6 * u;
    float lx = cx - total / 2;
    for (auto& l : lights) {
        r.text(lx, iy, is, l.first, l.second[0], l.second[1], l.second[2]);
        lx += r.textWidth(l.first, is) + 6 * u;
    }
}

// 4x4 schematic beside the gauges (trucks with a transfer case or lockers): wheels lit
// when driven, the prop shaft solid when front and rear are tied together, and a block
// on each axle whose diff is locked.
void drawDriveline(Renderer& r, const Car& car, float u, int W, int H) {
    if (car.isProp() || (car.spec->transfer == XFER_NONE && !car.spec->lockerFront && !car.spec->lockerRear)) return;
    const float R = 92 * u, gaugeX = W - 2 * R - 26 * u;
    const float w = 46 * u, h = 70 * u, x = gaugeX - w - 26 * u, y = H - h - 40 * u;
    pill(r, x - 10 * u, y - 10 * u, w + 20 * u, h + 34 * u, 0.0f, 0.0f, 0.0f, 0.4f);
    const float on[3] = {1.0f, 0.72f, 0.25f}, off[3] = {0.42f, 0.44f, 0.48f}, lockC[3] = {1.0f, 0.3f, 0.22f};
    auto axle = [&](bool front, float ay) {
        bool driven = false;
        for (const Wheel& wh : car.wheels)
            if (wh.front == front) driven |= car.wheelPowered(wh);
        const float* c = driven ? on : off;
        r.rect(x + 8 * u, ay - 1.5f * u, w - 16 * u, 3 * u, c[0], c[1], c[2], 0.9f);
        for (int side = 0; side < 2; side++) r.rect(side ? x + w - 8 * u : x, ay - 9 * u, 8 * u, 18 * u, c[0], c[1], c[2], 1);
        const bool locked = car.diffType(front) == DIFF_LOCKED;
        const float* d = locked ? lockC : c;
        r.rect(x + w / 2 - 5 * u, ay - 5 * u, 10 * u, 10 * u, d[0], d[1], d[2], 1);
        if (!locked) r.rect(x + w / 2 - 3 * u, ay - 3 * u, 6 * u, 6 * u, 0.05f, 0.05f, 0.06f, 1);  // open: hollow
        return driven;
    };
    const float fy = y + 10 * u, ry = y + h - 10 * u;
    const bool fd = axle(true, fy), rd = axle(false, ry);
    // Prop shafts and the transfer case.
    const float* fc = fd ? on : off;
    const float* rc = rd ? on : off;
    const float tcY = y + h * 0.55f;
    for (float yy = fy + 6 * u; yy < tcY - 4 * u; yy += car.centerLocked() ? 1e9f : 5 * u)
        r.rect(x + w / 2 - 1.5f * u, yy, 3 * u, car.centerLocked() ? tcY - 4 * u - yy : 3 * u, fc[0], fc[1], fc[2], 0.9f);
    r.rect(x + w / 2 - 1.5f * u, tcY + 4 * u, 3 * u, ry - 6 * u - tcY - 4 * u, rc[0], rc[1], rc[2], 0.9f);
    const float* tc = car.centerLocked() ? lockC : on;
    r.rect(x + w / 2 - 6 * u, tcY - 4 * u, 12 * u, 8 * u, tc[0], tc[1], tc[2], 1);
    if (const DriveMode* m = car.mode()) {
        const float s = px(1.2f * u);
        r.text(x + w / 2 - r.textWidth(m->name, s) / 2, y + h + 6 * u, s, m->name, 1, 1, 1, 0.95f);
    }
    // Tire pressure (orange when aired down) and the sway bar, above the schematic.
    const float s = px(1.1f * u);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d PSI", (int)std::lround(car.tirePsi));
    const bool aired = car.tirePsi < car.spec->tirePsi - 1;
    r.text(x + w / 2 - r.textWidth(buf, s) / 2, y - 9 * u - 8 * s, s, buf, aired ? 1.0f : 0.8f, aired ? 0.72f : 0.82f, aired ? 0.25f : 0.86f, 0.95f);
    if (car.swayDisconnected) r.text(x + w / 2 - r.textWidth("SWAY OFF", s) / 2, y - 13 * u - 16 * s, s, "SWAY OFF", 1.0f, 0.72f, 0.25f, 0.95f);
}

// Winch line or tow strap in use: line out, pull, and how close it is to its limit.
void drawRecovery(Renderer& r, const Sim& sim, float u) {
    const int ri = sim.ropeOn(sim.player);
    if (ri < 0) return;
    const Rope& rp = sim.ropes[ri];
    const float s = px(1.4f * u), x = 22 * u, y = 14 * u, h = 8 * s * 2 + 22 * u, w = 250 * u;
    pill(r, x, y, w, h, 0.0f, 0.0f, 0.0f, 0.55f);
    char buf[96];
    const char* what = rp.kind == ROPE_STRAP ? "TOW STRAP" : rp.a.car == sim.player ? "WINCH" : "ON A WINCH LINE";
    const char* state = rp.kind != ROPE_WINCH ? "" : rp.winch > 0 ? (rp.shown > rp.stallN * 0.97 ? "  STALLED" : "  REELING IN") : rp.winch < 0 ? "  PAYING OUT" : "  HOLDING";
    std::snprintf(buf, sizeof buf, "%s%s", what, state);
    label(r, x + 8 * u, y + 6 * u, s, buf, 1.0f, 0.75f, 0.3f);
    const double load = rp.shown / rp.breakN;
    std::snprintf(buf, sizeof buf, "%.1f M  %.0f KN", rp.length, rp.shown / 1000);
    r.text(x + 8 * u, y + 12 * u + 8 * s, s, buf, 0.92f, 0.93f, 0.95f);
    const float bx = x + 8 * u + r.textWidth("00.0 M  000 KN", s) + 10 * u, bw = x + w - 10 * u - bx, by = y + 13 * u + 8 * s;
    r.rect(bx, by, bw, 6 * u, 1, 1, 1, 0.2f);
    const float f = (float)clampd(load, 0, 1);
    r.rect(bx, by, bw * f, 6 * u, 0.4f + 0.6f * f, 0.85f - 0.6f * f, 0.25f, 0.95f);
    if (rp.kind == ROPE_WINCH && rp.a.car == sim.player) {
        const float sm = px(1.1f * u);
        char hint[96];
        std::snprintf(hint, sizeof hint, "Z REEL IN   SHIFT+Z PAY OUT   - = SPEED %d%%   V UNHOOK", (int)std::lround(sim.cars[sim.player].winchSpeed * 100));
        r.text(x + 8 * u, y + h + 4 * u, sm, hint, 0.8f, 0.82f, 0.86f, 0.9f);
    }
}

// Repair station status while parked on (or rolling onto) a pad.
void drawStation(Renderer& r, const Sim& sim, float u, int W, int H) {
    const Car& car = sim.cars[sim.player];
    if (car.fieldHelper >= 0 && car.fieldHelper < (int)sim.cars.size()) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "TRAIL REPAIRS FROM THE %s: %d%%  (TIRES, SHAFTS, STEERING, FLOODED ENGINE)", sim.cars[car.fieldHelper].spec->name,
                      (int)std::lround(100 * car.fieldTime / car.fieldRepairSeconds()));
        centeredPill(r, W / 2.0f, H - 70 * u, px(1.5f * u), buf, 0.02f, 0.08f, 0.04f, 0.95f, 0.6f, 1.0f, 0.6f);
        return;
    }
    if (car.station < 0 || car.isProp()) return;
    const Station& st = sim.world.stations[car.station];
    char buf[128];
    if (!car.needsRepair())
        std::snprintf(buf, sizeof buf, "%s: NOTHING TO FIX%s", st.name, car.tirePsi < car.spec->tirePsi - 0.5 ? "  (0: SHOP AIR)" : "");
    else if (car.repairTime <= 0)
        std::snprintf(buf, sizeof buf, "%s: STOP ON THE PAD FOR REPAIRS (%.0f S)", st.name, car.repairSeconds());
    else
        std::snprintf(buf, sizeof buf, "%s: REPAIRING %d%%", st.name, (int)std::lround(100 * car.repairTime / car.repairSeconds()));
    centeredPill(r, W / 2.0f, H - 70 * u, px(1.6f * u), buf, 0.02f, 0.08f, 0.04f, 0.95f, 0.6f, 1.0f, 0.6f);
}

// Compass strip across the top: headings, the nearest repair station, the summit.
void drawCompass(Renderer& r, const Sim& sim, const NetSession& net, const View& view, float u, int W) {
    const Car& car = sim.cars[sim.player];
    const V3 p = car.frame().com;
    const V3 look = norm(V3(view.target.x - view.eye.x, 0, view.target.z - view.eye.z));
    const double heading = std::atan2(look.x, look.z);  // 0 = north (+z), clockwise through east (+x)
    const float w = 360 * u, h = 22 * u, x0 = W / 2.0f - w / 2, y0 = 8 * u, s = px(1.2f * u);
    const double span = M_PI * 0.75;  // degrees shown across the strip
    pill(r, x0, y0, w, h, 0.0f, 0.0f, 0.0f, 0.45f);
    auto place = [&](double bearing) -> float {
        double d = wrapAngle(bearing - heading);
        if (std::fabs(d) > span / 2) return -1;
        return x0 + w / 2 + (float)(d / span) * w;
    };
    static const char* names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    for (int k = 0; k < 8; k++) {
        float x = place(k * M_PI / 4);
        if (x < 0) continue;
        const float ns = k % 2 ? s * 0.85f : s;
        r.text(x - r.textWidth(names[k], ns) / 2, y0 + 5 * u, ns, names[k], k == 0 ? 1.0f : 0.85f, k == 0 ? 0.45f : 0.86f, k == 0 ? 0.35f : 0.9f, 0.95f);
    }
    r.rect(W / 2.0f - 1, y0 + h - 5 * u, 2, 5 * u, 1, 1, 1, 0.9f);
    auto marker = [&](const V3& q, const char* tag, float cr, float cg, float cb) {
        const V3 d = q - p;
        const double dist = std::sqrt(d.x * d.x + d.z * d.z);
        char buf[48];
        std::snprintf(buf, sizeof buf, "%s %s", tag, dist > 950 ? (std::to_string((int)std::lround(dist / 100) / 10.0).substr(0, 3) + " KM").c_str() : (std::to_string((int)std::lround(dist)) + " M").c_str());
        double b = std::atan2(d.x, d.z);
        float x = place(b);
        const float ms = px(1.1f * u);
        if (x < 0) {  // off the strip: pin to the nearer edge
            x = wrapAngle(b - heading) < 0 ? x0 + 4 * u : x0 + w - 4 * u - r.textWidth(buf, ms);
        } else {
            r.tri(x - 4 * u, y0 + h + 1 * u, x + 4 * u, y0 + h + 1 * u, x, y0 + h - 5 * u, cr, cg, cb, 1);
            x -= r.textWidth(buf, ms) / 2;
        }
        label(r, x, y0 + h + 4 * u, ms, buf, cr, cg, cb, 0.95f);
    };
    double sd = 0;
    const int st = sim.nearestStation(p, &sd);
    if (st >= 0 && car.station != st) marker(sim.world.stations[st].p, "REPAIR", 0.55f, 1.0f, 0.55f);
    if (sim.world.map == MAP_MOUNTAIN) marker(mountainFeature("summit"), "SUMMIT", 1.0f, 0.85f, 0.45f);
    if (car.pilot.active) marker(car.pilot.goal, "AUTOPILOT", 0.4f, 0.85f, 1.0f);
    // Your buddies (their first vehicle each).
    std::vector<int> shown;
    for (const Car& c : sim.cars)
        if (c.remote && !c.isProp() && std::find(shown.begin(), shown.end(), NetSession::ownerOf(c.netId)) == shown.end()) {
            shown.push_back(NetSession::ownerOf(c.netId));
            marker(c.frame().com, net.ownerName(c.netId).c_str(), 0.6f, 0.9f, 1.0f);
        }
}

void drawVehicleCard(Renderer& r, const Car& car, float u, int H) {
    float s = px(2.0f * u), sm = px(1.6f * u), x = 22 * u;
    float y = H - 26 * u - 8 * s - 10 * sm - (car.isProp() ? 0 : 8 * sm + 5 * u) - (car.spec->modSummary.empty() ? 0 : 8 * sm + 5 * u);
    label(r, x, y, s, car.spec->name, 1, 1, 1);
    float dmg = (float)clampd(car.damage * 2.0, 0, 1), bw = 150 * u, by = y + 8 * s + 4 * u;
    r.rect(x, by, bw, 4 * u, 1, 1, 1, 0.22f);
    r.rect(x, by, bw * dmg, 4 * u, 0.95f, 0.3f + 0.45f * (1 - dmg), 0.15f, 0.95f);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "DAMAGE %d%%", (int)std::lround(dmg * 100));
    label(r, x, by + 8 * u, sm, buf, 0.85f, 0.87f, 0.9f, 0.95f);
    if (!car.isProp()) {
        int f = (int)std::lround(car.frontShare * 100);
        std::snprintf(buf, sizeof(buf), "%d KG  %d/%d", (int)std::lround(car.mass / 10) * 10, f, 100 - f);
        label(r, x, by + 8 * u + 8 * sm + 5 * u, sm, buf, 0.62f, 0.65f, 0.7f, 0.9f);  // weight, front/rear split
        if (!car.spec->modSummary.empty())
            label(r, x, by + 8 * u + 2 * (8 * sm + 5 * u), sm, car.spec->modSummary, 1.0f, 0.75f, 0.3f, 0.9f);
    }
    // Warning pills stacked above the name.
    std::vector<std::string> warn;
    int detached = 0, popped = 0;
    for (const Wheel& wh : car.wheels) {
        detached += wh.detached;
        popped += wh.popped && !wh.detached;
    }
    if (car.hydrolocked) warn.push_back("ENGINE HYDROLOCKED");
    else if (car.engineDead) warn.push_back("ENGINE DEAD");
    else if (car.engineHealth < 0.65) warn.push_back("ENGINE DAMAGED");
    if (detached) warn.push_back(detached == 1 ? "WHEEL OFF" : "WHEELS OFF");
    bool shaft = false, bentSteer = false, bentArm = false;
    for (const Wheel& wh : car.wheels) {
        if (wh.detached) continue;
        for (int bi : wh.shafts) shaft |= bi >= 0 && car.beams[bi].broken;
        bentSteer |= std::fabs(wh.tieOffset) > 0.004;
        for (int bi : wh.mounts) {
            const Beam& b = car.beams[bi];
            bentArm |= b.type == BEAM_ARM && (b.broken || std::fabs(b.L0 - b.Lorig) > 0.01 * b.Lorig);
        }
    }
    int mountsGone = 0;
    for (int bi : car.engineMounts) mountsGone += car.beams[bi].broken;
    if (shaft) warn.push_back("DRIVESHAFT BROKEN");
    for (const Wheel& wh : car.wheels)
        if (wh.shaftBroken && !wh.detached)
            warn.push_back(std::string(wh.front ? "FRONT " : "REAR ") + (wh.left ? "LEFT" : "RIGHT") + (wh.front && car.spec->frontSusp != SOLID_AXLE ? " CV BROKEN" : " AXLE SHAFT BROKEN"));
    if (mountsGone * 2 >= (int)car.engineMounts.size() && !car.engineMounts.empty()) warn.push_back("ENGINE TORN LOOSE");
    if (bentSteer) warn.push_back("STEERING BENT");
    if (bentArm) warn.push_back("SUSPENSION BENT");
    for (const Panel& p : car.panels)
        if (p.state) warn.push_back(std::string(p.name) + (p.state == 2 ? " OFF" : " OPEN"));
    if (popped) warn.push_back(popped == 1 ? "FLAT TIRE" : "FLAT TIRES");
    float ws = px(1.3f * u), ph = 8 * ws + 6 * u, wy = y - ph - 6 * u;
    for (const std::string& w : warn) {
        float pw = r.textWidth(w, ws) + 12 * u;
        pill(r, x, wy, pw, ph, 0.7f, 0.08f, 0.06f, 0.8f);
        r.text(x + 6 * u, wy + 3 * u, ws, w, 1, 0.92f, 0.88f);
        wy -= ph + 4 * u;
    }
}

// ---- Levels on screen -------------------------------------------------------------------

// Where world point p lands on the screen (drawable pixels); false behind the camera.
bool projectToScreen(const View& v, int W, int H, const V3& p, float& sx, float& sy) {
    const V3 f = norm(v.target - v.eye), rt = norm(cross(f, v.up)), up = cross(rt, f);
    const V3 d = p - v.eye;
    const double z = dot(d, f);
    if (z < 0.3) return false;
    const double k = 1.0 / std::tan(v.fovy / 2);
    sx = (float)(W / 2.0 + dot(d, rt) / z * k * H / 2.0);
    sy = (float)(H / 2.0 - dot(d, up) / z * k * H / 2.0);
    return true;
}

// The level being played: the clock, gates and laps, the medal times, the big banner, the
// next gate in the world, and everyone's progress in multiplayer.
void drawLevelHud(Renderer& r, const LevelRun& run, const Sim& sim, const View& view, int W, int H) {
    const Level& lv = run.level;
    const float u = std::max(0.75f, H / 720.0f), s = px(1.2f * u), big = px(2.4f * u);
    const int n = (int)lv.gates.size();
    // Panel under the compass.
    const float w = 330 * u, x = W / 2.0f - w / 2, y = 52 * u, h = 8 * s * 2 + 8 * big + 30 * u;
    pill(r, x, y, w, h, 0.0f, 0.0f, 0.0f, 0.5f);
    label(r, x + 10 * u, y + 7 * u, s, lv.name, 1.0f, 0.75f, 0.3f, 1.0f);
    const double t = run.state == LevelRun::COUNTDOWN ? 0 : run.state == LevelRun::FINISHED ? run.finishTime : run.clock;
    const std::string clock = formatRaceTime(t);
    r.text(x + 10 * u, y + 12 * u + 8 * s, big, clock, 1, 1, 1, 1);
    char buf[96];
    const int gateNo = std::min(run.gate + 1, std::max(1, n));
    if (lv.mode == LEVEL_RACE) std::snprintf(buf, sizeof buf, "LAP %d/%d   GATE %d/%d", std::min(run.lap + 1, lv.laps), lv.laps, gateNo, n);
    else std::snprintf(buf, sizeof buf, "GATE %d/%d", gateNo, n);
    if (run.state == LevelRun::FINISHED) std::snprintf(buf, sizeof buf, "FINISHED");
    if (run.state == LevelRun::OUT) std::snprintf(buf, sizeof buf, "OUT: %s", run.outWhy.c_str());
    r.text(x + 10 * u, y + 18 * u + 8 * s + 8 * big, s, buf, 0.85f, 0.87f, 0.9f, 1);
    // Medal times down the right.
    static const char* medal[3] = {"GOLD", "SILVER", "BRONZE"};
    static const float mc[3][3] = {{1.0f, 0.82f, 0.3f}, {0.8f, 0.83f, 0.88f}, {0.85f, 0.55f, 0.3f}};
    for (int k = 0; k < 3; k++) {
        if (lv.medals[k] <= 0) continue;
        std::snprintf(buf, sizeof buf, "%s %s", medal[k], formatRaceTime(lv.medals[k]).c_str());
        const bool beaten = t <= lv.medals[k];
        r.text(x + w - 10 * u - r.textWidth(buf, s), y + 7 * u + k * (8 * s + 4 * u), s, buf, mc[k][0], mc[k][1], mc[k][2], beaten ? 1.0f : 0.4f);
    }
    if (run.best > 0) {
        std::snprintf(buf, sizeof buf, "BEST %s", formatRaceTime(run.best).c_str());
        r.text(x + w - 10 * u - r.textWidth(buf, s), y + h - 8 * u - 8 * s, s, buf, 0.6f, 0.9f, 0.6f, 1);
    }
    // The next gate, out in the world.
    if ((run.state == LevelRun::RUNNING || run.state == LevelRun::COUNTDOWN) && n > 0) {
        const Gate& g = lv.gates[run.gate];
        const V3 at(g.p.x, sim.world.height(g.p.x, g.p.z) + 4.5, g.p.z);
        const double dist = len(at - sim.cars[sim.player].frame().com);
        const bool last = run.gate == n - 1 && (lv.mode != LEVEL_RACE || run.lap + 1 >= lv.laps);
        std::snprintf(buf, sizeof buf, "%s %.0f M", last ? "FINISH" : ("GATE " + std::to_string(run.gate + 1)).c_str(), dist);
        float sx, sy;
        const float ms = px(1.3f * u);
        if (projectToScreen(view, W, H, at, sx, sy) && sx > 0 && sx < W && sy > 0 && sy < H) {
            r.tri(sx - 9 * u, sy - 14 * u, sx + 9 * u, sy - 14 * u, sx, sy, 1.0f, 0.75f, 0.3f, 0.95f);
            label(r, sx - r.textWidth(buf, ms) / 2, sy - 30 * u, ms, buf, 1.0f, 0.85f, 0.45f, 1);
        } else {
            // Off screen: an arrow at the bottom edge pointing the way.
            const V3 f = norm(V3(view.target.x - view.eye.x, 0, view.target.z - view.eye.z));
            const V3 d = norm(V3(at.x - view.eye.x, 0, at.z - view.eye.z));
            const double ang = std::atan2(f.x * d.z - f.z * d.x, dot(f, d));  // + = to the right
            const float ax = W / 2.0f + (float)clampd(ang / M_PI, -1, 1) * (W / 2.0f - 60 * u), ay = H - 150 * u;
            const float dir = ang > 0 ? 1.0f : -1.0f;
            r.tri(ax + dir * 14 * u, ay, ax - dir * 6 * u, ay - 10 * u, ax - dir * 6 * u, ay + 10 * u, 1.0f, 0.75f, 0.3f, 0.95f);
            label(r, ax - r.textWidth(buf, ms) / 2, ay + 14 * u, ms, buf, 1.0f, 0.85f, 0.45f, 1);
        }
    }
    // The banner: countdown, GO, laps, the finish.
    if (run.bannerTime > 0 && !run.banner.empty()) {
        const float bs = px((run.banner.size() <= 3 ? 7.0f : 3.6f) * u);
        const float a = (float)std::min(1.0, run.bannerTime * 2);
        const float bw = r.textWidth(run.banner, bs);
        r.text(W / 2.0f - bw / 2 + 3 * u, H * 0.3f + 3 * u, bs, run.banner, 0, 0, 0, 0.6f * a);
        r.text(W / 2.0f - bw / 2, H * 0.3f, bs, run.banner, 1.0f, 0.85f, 0.4f, a);
    }
    if (run.state == LevelRun::FINISHED || run.state == LevelRun::OUT) {
        const char* hint = lv.resets || run.state == LevelRun::OUT ? "R TWICE: TRY AGAIN    ESC: LEVELS" : "R TWICE: TRY AGAIN    ESC: LEVELS";
        r.text(W / 2.0f - r.textWidth(hint, s) / 2, H * 0.3f + 70 * u, s, hint, 0.9f, 0.9f, 0.92f, 0.9f);
    }
    // Everyone (multiplayer).
    if (run.board.size() > 1) {
        std::vector<RunEntry> b = run.board;
        std::stable_sort(b.begin(), b.end(), [](const RunEntry& p, const RunEntry& q) {
            if (p.out != q.out) return !p.out;
            if ((p.time > 0) != (q.time > 0)) return p.time > 0;
            if (p.time > 0) return p.time < q.time;
            return p.gates > q.gates;
        });
        const float bw = 230 * u, bx = W - bw - 12 * u, by = 52 * u, lh = 8 * s + 6 * u;
        pill(r, bx, by, bw, 14 * u + lh * (float)b.size(), 0, 0, 0, 0.5f);
        for (size_t k = 0; k < b.size(); k++) {
            const RunEntry& e = b[k];
            std::string what = e.out ? "OUT" : e.time > 0 ? formatRaceTime(e.time) : std::to_string(e.gates) + "/" + std::to_string(run.totalGates());
            std::snprintf(buf, sizeof buf, "%zu  %s", k + 1, e.name.c_str());
            const float yy = by + 7 * u + lh * k;
            r.text(bx + 10 * u, yy, s, buf, 0.95f, 0.95f, 0.97f, e.out ? 0.5f : 1.0f);
            r.text(bx + bw - 10 * u - r.textWidth(what, s), yy, s, what, 1.0f, 0.8f, 0.4f, e.out ? 0.5f : 1.0f);
        }
    }
}

void drawHelp(Renderer& r, float u, int W, int H, bool mph) {
    const char* keys[][2] = {
        {"W S", "DRIVE / BRAKE / REVERSE"}, {"A D", "STEER"},
        {"SPACE", "HANDBRAKE"},               {"R", "REPAIR IN PLACE (R R: RESPAWN)"},
        {"T", "BACK ON ITS WHEELS"},          {"TAB", "NEXT VEHICLE (DRIVE YOUR BUDDY)"},
        {"V", "WINCH: HOOK / UNHOOK"},        {"Z", "WINCH IN (SHIFT+Z: OUT, - = SPEED)"},
        {"O", "TOW STRAP TO NEARBY TRUCK"},   {"Y", "FRONT SWAY BAR DISCONNECT"},
        {"9 0", "AIR DOWN / UP (STOPPED)"},
        {"1 - 4", "CIVIC JESKO BRONCO 4RUNNER"}, {"G", "GARAGE: ENGINE, SUSPENSION, GEARBOX"},
        {"Q E", "SHIFT DOWN / UP"},           {"M", "MANUAL / AUTOMATIC"},
        {"L", mph ? "4X4: 2H 4H 4L (LOW: BELOW 5 MPH)" : "4X4: 2H 4H 4L (LOW: BELOW 8 KM/H)"},
        {"SHIFT+L", "LIGHTS: AUTO, LOW, HIGH, OFF"},
        {"F B", "FRONT / REAR LOCKER"},       {"I", "SPAWN PARKED VEHICLE"},
        {"K", "SPAWN AI TRAFFIC"},            {"U", "AUTOPILOT (ESC > AUTOPILOT: WHERE)"},
        {"DEL", "REMOVE VEHICLE"},            {"C", "CAMERA"},
        {"MOUSE", "LOOK / ZOOM"},             {"N", "BEAM VIEW"},
        {"[ ]", "SLOW MOTION"},               {"P", "PAUSE"},
        {"X", "TRACTION CONTROL"},            {"F11", "FULLSCREEN"},
        {"F3", "FPS"},                        {"ESC", "MENU: CARS, SETTINGS, QUIT"},
        {"H", "CLOSE"},
    };
    const int n = sizeof(keys) / sizeof(keys[0]), rows = (n + 1) / 2;
    float s = px(1.4f * u), lh = 8 * s + 6 * u, keyW = 0, descW = 0;
    for (auto& k : keys) {
        keyW = std::max(keyW, r.textWidth(k[0], s));
        descW = std::max(descW, r.textWidth(k[1], s));
    }
    float colW = keyW + 14 * u + descW + 28 * u, pad = 18 * u;
    float w = colW * 2 + 2 * pad - 28 * u, h = rows * lh + 8 * s * 1.5f + 3 * pad;
    float x = (W - w) / 2, y = (H - h) / 2;
    pill(r, x, y, w, h, 0.02f, 0.025f, 0.035f, 0.8f);
    label(r, x + pad, y + pad, px(2.0f * u), "CONTROLS", 1.0f, 0.75f, 0.3f);
    for (int k = 0; k < n; k++) {
        float cx = x + pad + (k / rows) * colW, cy = y + pad * 2 + 8 * s * 1.5f + (k % rows) * lh;
        r.text(cx, cy, s, keys[k][0], 1.0f, 0.75f, 0.3f);
        r.text(cx + keyW + 14 * u, cy, s, keys[k][1], 0.9f, 0.91f, 0.93f);
    }
}

void drawScenarioStatus(Renderer& r, const Playback& pb, HudState& hud, float u, int W) {
    const Ctx& c = *pb.ctx;
    int ok = 0, total = 0;
    for (const CheckResult& ch : c.checks) {
        total++;
        ok += ch.ok;
    }
    for (const MetricResult& m : c.metrics) {
        total++;
        ok += m.ok;
    }
    char buf[200];
    if (pb.done)
        std::snprintf(buf, sizeof(buf), "%s  %s  %d/%d   J: DETAILS", pb.scenario->name.c_str(), pb.passed() ? "PASSED" : "FAILED", ok, total);
    else
        std::snprintf(buf, sizeof(buf), "%s  %.1f S  %d/%d   J: DETAILS", pb.scenario->name.c_str(), c.t, ok, total);
    float s = px(1.4f * u);
    bool good = pb.passed();
    centeredPill(r, W / 2.0f, 12 * u, s, buf, pb.done ? (good ? 0.05f : 0.5f) : 0.02f, pb.done ? (good ? 0.35f : 0.05f) : 0.02f,
                 pb.done ? (good ? 0.12f : 0.05f) : 0.03f, 1);
    if (!hud.scenarioDetail) return;
    std::vector<std::pair<std::string, bool>> lines;
    for (const CheckResult& ch : c.checks) lines.push_back({ch.name, ch.ok});
    for (const MetricResult& m : c.metrics) {
        std::snprintf(buf, sizeof(buf), "%s: %.2f %s (%.2f-%.2f)", m.name.c_str(), m.value, m.unit.c_str(), m.lo, m.hi);
        lines.push_back({buf, m.ok});
    }
    float ls = px(1.3f * u), lh = 8 * ls + 4 * u, w = 0;
    for (auto& l : lines) w = std::max(w, r.textWidth(l.first, ls) + 30 * u);
    float x = W / 2.0f - w / 2, y = 34 * u;
    pill(r, x, y, w, lines.size() * lh + 12 * u, 0.02f, 0.025f, 0.035f, 0.7f);
    for (size_t k = 0; k < lines.size(); k++) {
        float ly = y + 6 * u + k * lh;
        r.text(x + 8 * u, ly, ls, lines[k].second ? "+" : "X", lines[k].second ? 0.4f : 1.0f, lines[k].second ? 1.0f : 0.35f, 0.4f);
        r.text(x + 20 * u, ly, ls, lines[k].first, 0.9f, 0.9f, 0.92f);
    }
}

// ---- Garage -------------------------------------------------------------------------
// Pick an engine, suspension and transmission for the car you're in; ENTER fits them
// (the car is rebuilt from the modified spec where it stands).

struct GarageUI {
    bool open = false;
    int vehicle = 0, slot = 0;
    ModSet pick;
};

std::string fmt(const char* f, double a, double b = 0, double c = 0) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), f, a, b, c);
    return buf;
}

void drawGarage(Renderer& r, const GarageUI& g, const Garage& garage, float u, int W, int H) {
    const VehicleSpec& stock = vehicleCatalog()[g.vehicle];
    const VehicleSpec& vs = tunedSpec(g.vehicle, g.pick);
    const float s = px(1.4f * u), big = px(2.0f * u), lh = 8 * s + 6 * u, pad = 18 * u;
    const float w = std::min((float)W - 40 * u, 760 * u), h = pad * 3 + 8 * big * 1.5f + MOD_SLOTS * (lh * 2.4f) + lh * 7.5f;
    const float x = (W - w) / 2, y = (H - h) / 2;
    pill(r, x, y, w, h, 0.02f, 0.025f, 0.035f, 0.88f);
    label(r, x + pad, y + pad, big, std::string("GARAGE  ") + stock.name, 1.0f, 0.75f, 0.3f);
    r.text(x + pad, y + pad + 8 * big + 4 * u, s, stock.blurb, 0.6f, 0.62f, 0.66f);
    float cy = y + pad * 2 + 8 * big * 1.5f;
    for (int k = 0; k < MOD_SLOTS; k++) {
        const auto& opts = modOptions(g.vehicle, k);
        const ModOption& o = opts[g.pick.pick[k]];
        bool sel = k == g.slot, fitted = garage.fitted[g.vehicle].pick[k] == g.pick.pick[k];
        if (sel) r.rect(x + pad * 0.5f, cy - 4 * u, w - pad, lh * 2.2f, 1, 1, 1, 0.07f);
        r.text(x + pad, cy, s, modSlotName(k), 0.6f, 0.62f, 0.66f);
        std::string name = std::string(sel ? "< " : "  ") + o.name + (sel ? " >" : "") + "   " +
                           std::to_string(g.pick.pick[k] + 1) + "/" + std::to_string(opts.size()) + (fitted ? "" : "  *");
        r.text(x + pad + 150 * u, cy, s, name, sel ? 1.0f : 0.9f, sel ? 0.8f : 0.9f, sel ? 0.35f : 0.92f);
        r.text(x + pad + 150 * u, cy + lh, s, o.detail, 0.62f, 0.65f, 0.7f);
        cy += lh * 2.4f;
    }
    // What the combination adds up to.
    double rpmP = 0, rpmT = 0, P = peakPower(vs, &rpmP), T = peakTorque(vs, &rpmT);
    double lift = (vs.layerY[0] - vs.wheelRadius) - (stock.layerY[0] - stock.wheelRadius);  // body vs axle
    double ride = vs.layerY[0] - stock.layerY[0];
    std::vector<std::string> lines = {
        fmt("ENGINE   %.0f HP AT %.0f RPM", P / 745.7, rpmP) + fmt("   %.0f NM AT %.0f RPM", T, rpmT),
        std::string("         ") + vs.engineName + fmt("   REDLINE %.0f", vs.redline),
        std::string("GEARBOX  ") + vs.gearboxName + fmt("   FINAL DRIVE %.2f", vs.finalDrive) +
            (vs.lowRange > 0 ? fmt("   LOW RANGE %.2f", vs.lowRange) : std::string()),
        fmt("TIRES    %.0f MM TALL  %.0f MM WIDE", vs.wheelRadius * 2000, vs.wheelWidth * 1000) +
            fmt("   GRIP %.2f / %.2f", vs.tireGrip, vs.tireGrip * vs.lateralGrip),
        fmt("RIDE     %+.0f MM BODY HEIGHT  %+.0f MM LIFT", ride * 1000, lift * 1000) +
            fmt("   BOUNCE %.2f / %.2f HZ", vs.rideFreqFront, vs.rideFreqRear),
    };
    cy += lh * 0.4f;
    for (const std::string& l : lines) {
        r.text(x + pad, cy, s, l, 0.85f, 0.87f, 0.9f);
        cy += lh;
    }
    r.text(x + pad, y + h - pad - 8 * s, s, "W S CHOOSE PART   A D CHANGE   ENTER FIT AND DRIVE   G CLOSE", 1.0f, 0.75f, 0.3f);
}

void drawWinchAim(Renderer& r, const Sim& sim, const WinchAim& aim, const View& view, float u, int W, int H) {
    if (!aim.on) return;
    const Car& car = sim.cars[sim.player];
    const V3 hook = car.hookPoint(0);
    // The line's reach: a ring on the ground.
    const double reach = recovery::kWinchLine - 1;
    for (int k = 0; k < 120; k++) {
        const double a = k * 2 * M_PI / 120;
        V3 q = hook + V3(std::cos(a), 0, std::sin(a)) * reach;
        q.y = sim.world.height(q.x, q.z) + 0.15;
        float sx, sy;
        if (project(view, q, sx, sy)) r.rect(sx - 2 * u, sy - 2 * u, 4 * u, 4 * u, 1.0f, 0.75f, 0.3f, 0.75f);
    }
    // Every anchor in reach; the selected one gets a reticle and a dotted line to the winch.
    for (int k = 0; k < (int)aim.targets.size(); k++) {
        const WinchTarget& t = aim.targets[k];
        float sx, sy;
        if (!project(view, t.at, sx, sy)) continue;
        const bool on = k == aim.sel;
        const float cr = t.inArc ? 0.55f : 0.5f, cg = t.inArc ? 1.0f : 0.5f, cb = t.inArc ? 0.6f : 0.52f;
        const float d = (on ? 7 : 4) * u;
        r.tri(sx, sy - d, sx + d, sy, sx, sy + d, cr, cg, cb, on ? 1.0f : 0.8f);
        r.tri(sx, sy - d, sx, sy + d, sx - d, sy, cr, cg, cb, on ? 1.0f : 0.8f);
        if (!on) continue;
        const float b = 18 * u, l = 7 * u, th = 2.5f * u;
        for (int cx = -1; cx <= 1; cx += 2)
            for (int cy = -1; cy <= 1; cy += 2) {
                const float x0 = sx + cx * b, y0 = sy + cy * b;
                r.rect(cx < 0 ? x0 : x0 - l, y0 - th / 2, l, th, cr, cg, cb, 1);
                r.rect(x0 - th / 2, cy < 0 ? y0 : y0 - l, th, l, cr, cg, cb, 1);
            }
        for (int i = 1; i < 24; i++) {
            float lx, ly;
            if (project(view, hook + (t.at - hook) * (i / 24.0), lx, ly)) r.rect(lx - 1.5f * u, ly - 1.5f * u, 3 * u, 3 * u, cr, cg, cb, 0.8f);
        }
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s  %.0f M%s", t.what, t.dist, t.inArc ? "" : "  (BEHIND THE WINCH)");
        const float ts = px(1.8f * u);
        label(r, sx - r.textWidth(buf, ts) / 2, sy + b + 6 * u, ts, buf, cr, cg, cb);
    }
    const char* help = aim.targets.empty() ? "NOTHING IN REACH (30 M LINE)   ESC CANCEL"
                                           : "DRAG TO AIM   WHEEL OR TAB: NEXT   V OR CLICK: HOOK   ESC: CANCEL";
    centeredPill(r, W / 2.0f, H - 110 * u, px(1.5f * u), help, 0.02f, 0.02f, 0.03f, 0.95f, 1.0f, 0.75f, 0.3f);
}

// Multiplayer: who's here (top right), and a name tag over each of their vehicles.
void drawPlayers(Renderer& r, const Sim& sim, const NetSession& net, const View& view, float u, int W, int H) {
    if (!net.active()) return;
    const float s = px(1.3f * u), lh = 8 * s + 6 * u;
    float y = 34 * u;
    for (int k = 0; k < net::kMaxPeers; k++) {
        const NetPlayer& p = net.players[k];
        if (!p.active) continue;
        char buf[64];
        if (k == net.self) std::snprintf(buf, sizeof buf, "%s (YOU)%s", p.name.c_str(), k == 0 ? "  HOST" : "");
        else std::snprintf(buf, sizeof buf, "%s  %d MS%s", p.name.c_str(), (int)std::lround(p.ping * 1000), k == 0 ? "  HOST" : "");
        const float w = r.textWidth(buf, s) + 14 * u;
        pill(r, W - w - 10 * u, y, w, lh - 2 * u, 0.0f, 0.0f, 0.0f, 0.45f);
        r.text(W - w - 3 * u, y + 3 * u, s, buf, k == net.self ? 1.0f : 0.6f, k == net.self ? 0.75f : 0.9f, k == net.self ? 0.3f : 1.0f);
        y += lh;
    }
    // Name tags: project a point above each of their vehicles.
    const V3 fwd = norm(view.target - view.eye), right = norm(cross(fwd, view.up)), up = cross(right, fwd);
    const double f = 1.0 / std::tan(view.fovy / 2);
    for (const Car& c : sim.cars) {
        if (!c.remote || c.isProp()) continue;
        const V3 p = c.frame().com + V3(0, 1.9, 0) - view.eye;
        const double z = dot(p, fwd);
        if (z < 1.0 || z > 400) continue;
        const float sx = W / 2.0f + (float)(dot(p, right) / z * f * H / 2), sy = H / 2.0f - (float)(dot(p, up) / z * f * H / 2);
        std::string tag = net.ownerName(c.netId);
        if (c.engineDead) tag += c.hydrolocked ? "  (FLOODED)" : "  (ENGINE DEAD)";
        const float ts = px((z < 40 ? 1.4f : 1.1f) * u);
        const float tw = r.textWidth(tag, ts);
        pill(r, sx - tw / 2 - 6 * u, sy - 4 * u, tw + 12 * u, 8 * ts + 8 * u, 0.0f, 0.0f, 0.0f, 0.5f);
        r.text(sx - tw / 2, sy, ts, tag, 0.6f, 0.9f, 1.0f);
    }
}

void drawHud(Renderer& r, const Sim& sim, const View& view, HudState& hud, const Playback* pb, const NetSession& net, const WinchAim& aim, int W,
             int H, float fps, double dt, bool mph) {
    float u = std::max(0.75f, H / 720.0f);
    const Car& car = sim.cars[sim.player];
    drawGauges(r, car, hud, u, W, H, dt, mph);
    drawDriveline(r, car, u, W, H);
    drawVehicleCard(r, car, u, H);
    drawRecovery(r, sim, u);
    drawStation(r, sim, u, W, H);
    drawPlayers(r, sim, net, view, u, W, H);
    drawWinchAim(r, sim, aim, view, u, W, H);
    drawPilot(r, sim, view, u, W, H);

    // Top-center: compass, scenario status, persistent states, then toasts.
    float y = 12 * u;
    if (!(pb && pb->active()) && !sim.world.stations.empty()) {
        drawCompass(r, sim, net, view, u, W);
        y += 40 * u;
    }
    if (pb && pb->active()) {
        drawScenarioStatus(r, *pb, hud, u, W);
        y += 18 * u;
    }
    char buf[64];
    // The race circuit: a lap starts and ends at the checkered line (leave the circuit and
    // the lap doesn't count).
    if (sim.world.map == MAP_FRONTIER) {
        static V3 lineAt, lineDir;
        if (len(lineDir) < 0.5) {
            const std::vector<V3>& t = frontierTrack();
            double acc = 0;
            for (size_t k = 0; k + 1 < t.size(); k++) {
                acc += std::hypot(t[k + 1].x - t[k].x, t[k + 1].z - t[k].z);
                if (acc >= 90) {
                    lineAt = t[k], lineDir = norm(V3(t[k + 1].x - t[k].x, 0, t[k + 1].z - t[k].z));
                    break;
                }
            }
        }
        const Frame f = car.frame();
        const V3 rel(f.com.x - lineAt.x, 0, f.com.z - lineAt.z);
        const double ahead = dot(rel, lineDir), across = rel.x * lineDir.z - rel.z * lineDir.x;
        double off = 1e9;
        for (const V3& p : frontierTrack()) off = std::min(off, std::hypot(p.x - f.com.x, p.z - f.com.z));
        if (off > 30) hud.lapTime = -1;
        if (std::fabs(across) < 9 && hud.lapSide < 0 && ahead >= 0 && std::fabs(ahead) < 20 && dot(f.vel, lineDir) > 1) {
            if (hud.lapTime > 5) {
                hud.lastLap = hud.lapTime;
                const bool best = hud.bestLap <= 0 || hud.lapTime < hud.bestLap;
                if (best) hud.bestLap = hud.lapTime;
                char t[64];
                std::snprintf(t, sizeof t, "LAP %d:%05.2f%s", (int)(hud.lapTime / 60), std::fmod(hud.lapTime, 60.0), best ? "  NEW BEST" : "");
                hud.toast(t);
            }
            hud.lapTime = 0;
        }
        hud.lapSide = ahead;
        if (hud.lapTime >= 0 && !sim.paused) hud.lapTime += dt * sim.timeScale;
        if (hud.lapTime >= 0 || hud.bestLap > 0) {
            auto mmss = [](double t) {
                char b[24];
                std::snprintf(b, sizeof b, "%d:%05.2f", (int)(t / 60), std::fmod(t, 60.0));
                return std::string(b);
            };
            std::string lap = hud.lapTime >= 0 ? "LAP " + mmss(hud.lapTime) : "CROSS THE LINE TO START A LAP";
            if (hud.lastLap > 0) lap += "    LAST " + mmss(hud.lastLap);
            if (hud.bestLap > 0) lap += "    BEST " + mmss(hud.bestLap);
            centeredPill(r, W / 2.0f, y, px(1.4f * u), lap, 0.02f, 0.03f, 0.05f, 0.9f, 1.0f, 0.9f, 0.6f);
            y += 20 * u;
        }
    }
    // The freeway: how far you've come, and the fastest you've been.
    if (sim.world.wrapPeriod > 0) {
        const Frame f = car.frame();
        const double kmh = len(f.vel) * 3.6;
        hud.bestKmh = std::max(hud.bestKmh, kmh);
        const double trip = std::fabs(f.com.x - sim.wrapShift - sim.world.spawn.x) / 1000;
        char odo[96];
        if (mph) std::snprintf(odo, sizeof odo, "%.2f MI    BEST %.0f MPH", trip * 0.621371, hud.bestKmh * 0.621371);
        else std::snprintf(odo, sizeof odo, "%.2f KM    BEST %.0f KM/H", trip, hud.bestKmh);
        centeredPill(r, W / 2.0f, y, px(1.4f * u), odo, 0.02f, 0.03f, 0.05f, 0.9f, 0.85f, 0.9f, 1.0f);
        y += 20 * u;
    }
    if (hud.newBuild) {
        centeredPill(r, W / 2.0f, y, px(1.5f * u), "NEW BUILD READY: PRESS F5 TO LOAD IT (SAME MAP, VEHICLE AND SPOT)", 0.02f, 0.06f, 0.03f, 1,
                     0.45f, 1.0f, 0.55f);
        y += 22 * u;
    }
    if (sim.paused) {
        centeredPill(r, W / 2.0f, y, px(1.6f * u), "PAUSED", 0.02f, 0.02f, 0.03f, 1, 1.0f, 0.8f, 0.35f);
        y += 22 * u;
    } else if (sim.timeScale < 0.99) {
        std::snprintf(buf, sizeof(buf), "SLOW MOTION %.2fX", sim.timeScale);
        centeredPill(r, W / 2.0f, y, px(1.6f * u), buf, 0.02f, 0.02f, 0.03f, 1, 1.0f, 0.8f, 0.35f);
        y += 20 * u;
    }
    for (const Toast& t : hud.toasts) {
        float a = (float)std::min(1.0, std::min(t.age * 8, (2.6 - t.age) * 2.5));
        centeredPill(r, W / 2.0f, y, px(1.6f * u), t.text, 0.02f, 0.02f, 0.03f, a);
        y += 12 * px(1.6f * u) + 6 * u;
    }
    if (hud.hintTime > 0 && !hud.showHelp) {
        float a = (float)std::min(1.0, hud.hintTime);
        centeredPill(r, W / 2.0f, y, px(1.4f * u), "H: CONTROLS   ESC: MENU", 0.02f, 0.02f, 0.03f, a, 0.9f, 0.9f, 0.92f);
    }
    if (hud.showFps) {
        std::snprintf(buf, sizeof(buf), "%d FPS", (int)std::lround(fps));
        label(r, W - r.textWidth(buf, px(1.3f * u)) - 10 * u, 8 * u, px(1.3f * u), buf, 0.8f, 0.9f, 0.8f);
    }
    if (hud.showHelp) drawHelp(r, u, W, H, mph);
}

}  // namespace

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    const bool scripted = !opt.shotFrames.empty();
    if (scripted) opt.audio = false;
    // Settings from last time (scripted captures use the defaults, so they're repeatable).
    Settings settings;
    const std::string settingsPath = Settings::defaultPath();
    if (!scripted) settings.load(settingsPath);
    if (!opt.name.empty()) settings.playerName = opt.name;
    if (settings.playerName.empty()) {
        const char* user = std::getenv("USER");
        settings.playerName = user && *user ? user : "DRIVER";
        for (char& ch : settings.playerName) ch = (char)std::toupper((unsigned char)ch);
        if (settings.playerName.size() > 16) settings.playerName.resize(16);
    }
    if (opt.quality >= 0) applyPreset(settings.gfx, opt.quality);
    if (opt.mph) settings.mph = true;
    if (opt.map >= 0) settings.map = std::min(opt.map, MAP_COUNT - 1);
    if (!opt.scenario.empty()) settings.map = MAP_PROVING_GROUND;  // the tests are built for it
    if (opt.fullscreen) settings.fullscreen = true;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | (opt.audio ? (uint32_t)SDL_INIT_AUDIO : 0u)) != 0) {
        fail(std::string("SDL_Init failed: ") + SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    uint32_t flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (settings.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_Window* win = SDL_CreateWindow("Crumple Zone", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, opt.width,
                                       opt.height, flags);
    if (!win) {  // retry without multisampling
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        win = SDL_CreateWindow("Crumple Zone", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, opt.width, opt.height,
                               flags);
    }
    if (!win) {
        fail(std::string("could not create window: ") + SDL_GetError());
        return 1;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        fail(std::string("OpenGL 3.3 context failed: ") + SDL_GetError());
        return 1;
    }
    if (const char* missing = gl::load(SDL_GL_GetProcAddress)) {
        fail(std::string("missing OpenGL function ") + missing);
        return 1;
    }
    SDL_GL_SetSwapInterval(settings.vsync && opt.vsync && !scripted ? 1 : 0);
    std::printf("OpenGL: %s / %s\n", (const char*)gl::GetString(GL_VERSION), (const char*)gl::GetString(GL_RENDERER));

    Sim sim;
    if (settings.map != MAP_PROVING_GROUND) sim.world = World(settings.map);
    sim.setup();
    // Your garage: the mods fitted last time go back on the cars.
    Garage garage;
    const std::string garagePath = Garage::defaultPath();
    garage.load(garagePath);
    auto fitGarage = [&]() {
        for (Car& c : sim.cars)
            if (!c.isProp() && c.spec->model >= 0 && !garage.fitted[c.spec->model].stock()) {
                c.spec = &garage.spec(c.spec->model);
                c.respawn(c.spawnPos, c.spawnYaw);
                c.handbrake = 1;
            }
    };
    fitGarage();
    GarageUI garageUI;
    Playback playback;
    if (!opt.scenario.empty()) {
        const Scenario* found = nullptr;
        for (const Scenario& sc : scenarioRegistry())
            if (sc.name == opt.scenario) found = &sc;
        for (const Scenario& sc : scenarioRegistry())
            if (!found && sc.name.find(opt.scenario) != std::string::npos) found = &sc;
        if (!found) {
            std::printf("no scenario matches '%s'. Available:\n", opt.scenario.c_str());
            for (const Scenario& sc : scenarioRegistry()) std::printf("  %s\n", sc.name.c_str());
            return 1;
        }
        std::printf("playing scenario %s\n", found->name.c_str());
        playback.start(found, sim);
    }
    sim.player = std::min(std::max(opt.player, 0), (int)sim.cars.size() - 1);
    if (opt.resumeModel >= 0 && !sim.cars.empty() && sim.cars[sim.player].spec->model != opt.resumeModel)
        sim.swapPlayerVehicle(garage.spec(opt.resumeModel));
    if (opt.spawnSet) {
        Car& c = sim.cars[sim.player];
        c.respawn(V3(opt.spawnX, sim.world.clearY(opt.spawnX, opt.spawnZ) + 0.05, opt.spawnZ), opt.spawnHeading * M_PI / 180);
    }
    Renderer renderer;
    std::string err;
    uint64_t drawnWorld = sim.world.stamp;
    if (!renderer.init(sim.world, err)) {
        fail("renderer init failed:\n" + err);
        return 1;
    }
    Audio audio;
    bool audioOn = opt.audio && audio.start();

    CameraRig cam;
    cam.mode = opt.camera;
    if (opt.orbitSet) {
        cam.mode = 1;
        cam.orbitYaw = opt.orbitYaw * M_PI / 180;
        cam.pitch = opt.orbitPitch * M_PI / 180;
        cam.dist = opt.orbitDist;
    }
    DebugView debug = (DebugView)(opt.debug % DEBUG_VIEW_COUNT);
    bool dragging = false;
    HudState hud;
    if (scripted) hud.hintTime = 0;
    hud.showHelp = opt.showHelp;
    if (opt.mods[0] >= 0 && sim.cars[sim.player].spec->model >= 0) {
        int v = sim.cars[sim.player].spec->model;
        ModSet ms;
        for (int k = 0; k < MOD_SLOTS; k++) ms.pick[k] = std::max(0, opt.mods[k]);
        Car& c = sim.cars[sim.player];
        c.spec = &tunedSpec(v, ms);
        c.respawn(c.spawnPos, c.spawnYaw);
    }
    if (opt.showGarage && sim.cars[sim.player].spec->model >= 0) {
        garageUI.open = true;
        garageUI.vehicle = sim.cars[sim.player].spec->model;
        for (int k = 0; k < MOD_SLOTS; k++) garageUI.pick.pick[k] = sim.cars[sim.player].spec->mods[k];
    }
    // Multiplayer session (see net.h).
    NetSession net;
    WinchAim winchAim;
    View lastView;
    // The pause menu, and putting its settings into effect.
    Effects fx;
    PauseMenu menu;
    bool menuWasPaused = false, garageFromMenu = false;
    float menuBlur = 0;
    double clock = 0;
    // Time of day: the clock (hours) carries on from last time; --time holds it still.
    double dayHours = opt.timeHours >= 0 ? opt.timeHours : settings.clockHours;
    int lampMode = opt.lamps >= 0 && opt.lamps < LAMP_MODES ? opt.lamps : LAMPS_AUTO;  // the player's headlight switch
    auto applySettings = [&]() {
        cam.fovy = (float)(settings.fovDeg * M_PI / 180);
        hud.showFps = settings.showFps;
        bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (fs != settings.fullscreen && !scripted)
            SDL_SetWindowFullscreen(win, settings.fullscreen ? (uint32_t)SDL_WINDOW_FULLSCREEN_DESKTOP : 0u);
        SDL_GL_SetSwapInterval(settings.vsync && opt.vsync && !scripted ? 1 : 0);
        sim.realRecovery = settings.realRecovery;
        sim.steeringAssist = settings.steeringAssist;
    };
    auto saveSettings = [&]() {
        if (!scripted) settings.save(settingsPath);
    };
    applySettings();
    auto openGarage = [&]() {
        const Car& c = sim.cars[sim.player];
        if (c.isProp() || c.spec->model < 0) return false;
        garageUI.open = true;
        garageUI.vehicle = c.spec->model;
        garageUI.slot = 0;
        for (int k = 0; k < MOD_SLOTS; k++) garageUI.pick.pick[k] = c.spec->mods[k];
        hud.showHelp = false;
        return true;
    };
    std::function<void()> refreshLevelsLater;  // (defined below; the menu wants the list when it opens)
    auto openMenu = [&]() {
        if (refreshLevelsLater) refreshLevelsLater();
        menu.show(std::max(0, sim.cars[sim.player].spec->model), sim.world.map);
        menuWasPaused = sim.paused;
        sim.paused = !net.active();  // the others keep driving: no pausing a shared world
        hud.showHelp = false;
    };
    auto closeMenu = [&]() {
        menu.close();
        sim.paused = menuWasPaused;
    };
    bool running = true;
    double wrapSeen = 0;  // sim.wrapShift the camera and effects have caught up with
    BuildWatch watch;
    if (!scripted) watch.start();
    bool reload = false;
    auto shiftDown = [&]() {
        const uint8_t* ks = SDL_GetKeyboardState(nullptr);
        return ks[SC_LSHIFT] || ks[SC_RSHIFT];
    };
    auto confirmWinch = [&]() {
        if (winchAim.sel < 0 || winchAim.sel >= (int)winchAim.targets.size()) {
            winchAim.on = false;
            return;
        }
        const WinchTarget& t = winchAim.targets[winchAim.sel];
        if (!t.inArc) {
            hud.toast("THAT'S BEHIND THE WINCH: TURN TO FACE IT");
            return;
        }
        hud.toast(sim.hookWinchTo(sim.player, t.end));
        winchAim.on = false;
    };
    auto stopPilot = [&]() {
        Car& c = sim.cars[sim.player];
        const bool was = c.pilot.active || c.ai;
        c.pilot.active = false;
        c.pilot.followCar = -1;
        c.ai = false;
        if (c.gear < 0) c.gear = 1;
        if (was) hud.toast("AUTOPILOT OFF");
    };
    auto lookPoint = [&]() {  // where the middle of the screen meets the ground
        const V3 d = norm(lastView.target - lastView.eye);
        for (double t = 2; t < 400; t += 1) {
            const V3 p = lastView.eye + d * t;
            if (p.y < sim.world.height(p.x, p.z)) return p;
        }
        return lastView.target;
    };
    auto startPilot = [&]() {
        Car& c = sim.cars[sim.player];
        if (c.isProp()) return;
        const std::vector<Place> places = pilotPlaces(sim, net);
        const Place& pl = places[std::min(std::max(settings.pilotDest, 0), (int)places.size() - 1)];
        c.pilot.followCar = -1;
        c.ai = false;
        V3 goal = pl.p;
        std::string name = pl.name;
        if (pl.kind == 4) {
            c.pilot.active = false;
            c.ai = true;
            c.aiIndex = -1;
            c.aiCruise = settings.pilotSpeed / 3.6;
            hud.toast("AUTOPILOT: CRUISING THE LOOP ROAD");
            return;
        }
        if (pl.kind == 1) {
            const int st = sim.nearestStation(c.frame().com);
            if (st < 0) {
                hud.toast("NO REPAIR STATION ON THIS MAP");
                return;
            }
            goal = sim.world.stations[st].p;
            name = sim.world.stations[st].name;
        } else if (pl.kind == 2) {
            goal = lookPoint();
            name = "THE SPOT YOU PICKED";
        } else if (pl.kind == 3) {
            c.pilot.followCar = pl.car;
            goal = sim.cars[pl.car].frame().com;
            name = pl.name.substr(7);
        }
        hud.toast(pilotStart(c, sim.world, goal, name, settings.pilotSpeed / 3.6, settings.pilotRoute));
    };
    // Loads a map, keeping the vehicle you were in.
    auto loadMap = [&](int m) {
        const int model = std::max(0, sim.cars[sim.player].spec->model);
        sim.world = World(m);
        sim.setup();
        fitGarage();
        if (model != 0) sim.swapPlayerVehicle(garage.spec(model));
        sim.paused = false;
        renderer.setWorld(sim.world);
        drawnWorld = sim.world.stamp;
        hud.bestKmh = 0;
        wrapSeen = sim.wrapShift = 0;
        fx.clear();
        cam.initialized = false;
    };
    auto removeRemotes = [&]() {
        for (int i = (int)sim.cars.size() - 1; i >= 0; i--)
            if (sim.cars[i].remote) {
                sim.carRemoved(i);
                sim.cars.erase(sim.cars.begin() + i);
                if (sim.player > i) sim.player--;
            }
    };
    auto startHost = [&](int port) {
        if (net.host(port, settings.playerName, sim.world.map)) {
            net.adopt(sim);
            hud.toast("HOSTING ON PORT " + std::to_string(port) + ": FRIENDS JOIN YOUR IP");
            std::printf("hosting %s on port %d\n", mapName(sim.world.map), port);
        } else {
            hud.toast("CAN'T HOST: " + net.error);
            std::printf("can't host: %s\n", net.error.c_str());
        }
    };
    auto startJoin = [&](const std::string& address) {
        if (!net.join(address, settings.playerName)) {
            hud.toast("CAN'T JOIN: " + net.error);
            std::printf("can't join %s: %s\n", address.c_str(), net.error.c_str());
            return;
        }
        if (net.map != sim.world.map) loadMap(net.map);
        net.adopt(sim);
        hud.toast("JOINED " + net.ownerName(0) + "'S GAME ON " + mapName(net.map));
        std::printf("joined %s (player %d)\n", address.c_str(), net.self);
    };
    // ---- Levels: the list for the menu, playing one, and sharing it in multiplayer ----
    LevelRun run;
    std::vector<Level> levelList;
    std::map<std::string, double> bestTimes;  // this session's best finish per level
    auto refreshLevels = [&]() {
        levelList = listLevels();
        menu.levels.clear();
        for (const Level& lv : levelList) {
            PauseMenu::LevelInfo li;
            li.name = lv.name;
            li.about = lv.about;
            std::string l1 = std::string(levelModeName(lv.mode)) + "  -  " + mapName(lv.map);
            if (lv.mode == LEVEL_RACE) l1 += "  -  " + std::to_string(lv.laps) + (lv.laps == 1 ? " LAP" : " LAPS");
            l1 += "  -  " + (lv.vehicle >= 0 && lv.vehicle < (int)vehicleCatalog().size() ? std::string(vehicleCatalog()[lv.vehicle].name) : std::string("ANY VEHICLE"));
            li.lines.push_back(l1);
            std::string rules = lv.resets ? "R REPAIRS ALLOWED" : "NO REPAIRS";
            if (lv.maxDamage > 0) rules += "  -  OUT AT " + std::to_string((int)std::lround(lv.maxDamage)) + "% DAMAGE";
            if (lv.timeLimit > 0) rules += "  -  TIME LIMIT " + formatRaceTime(lv.timeLimit);
            li.lines.push_back(rules);
            if (lv.medals[0] > 0)
                li.lines.push_back("GOLD " + formatRaceTime(lv.medals[0]) + "   SILVER " + formatRaceTime(lv.medals[1]) + "   BRONZE " + formatRaceTime(lv.medals[2]));
            const auto b = bestTimes.find(lv.name);
            li.lines.push_back(b != bestTimes.end() ? "YOUR BEST " + formatRaceTime(b->second) : lv.builtin ? "BY CRUMPLE ZONE" : "YOURS: " + lv.file);
            menu.levels.push_back(li);
        }
        menu.levelPick = std::min(std::max(menu.levelPick, 0), std::max(0, (int)levelList.size() - 1));
    };
    refreshLevelsLater = refreshLevels;
    auto runSlot = [&]() { return net.active() ? std::max(0, net.self) : 0; };
    auto playLevel = [&](const Level& lv) {
        Car& me = sim.cars[sim.player];
        me.pilot.active = false;
        me.ai = false;
        const int model = lv.vehicle >= 0 && lv.vehicle < (int)vehicleCatalog().size() ? lv.vehicle : std::max(0, me.spec->model);
        sim.world = World(lv.map);
        applyLevel(lv, sim.world);
        addGateMarkers(lv, sim.world);
        // Just you on the grid (and the other players, in a session).
        for (int i = (int)sim.cars.size() - 1; i >= 0; i--)
            if (i != sim.player && !sim.cars[i].remote) {
                sim.carRemoved(i);
                sim.cars.erase(sim.cars.begin() + i);
                if (sim.player > i) sim.player--;
            }
        sim.dropRopes(sim.player);
        sim.swapPlayerVehicle(garage.spec(model));
        sim.paused = false;
        renderer.setWorld(sim.world);
        drawnWorld = sim.world.stamp;
        fx.clear();
        cam.initialized = false;
        wrapSeen = sim.wrapShift = 0;
        hud.bestKmh = 0;
        if (lv.timeOfDay >= 0) dayHours = lv.timeOfDay;
        run.level = lv;
        const auto b = bestTimes.find(lv.name);
        run.best = b != bestTimes.end() ? b->second : -1;
        run.setSelf(net.active() ? net.self : -1, net.active() ? settings.playerName : "YOU");
        if (lv.hasJob()) {
            run.begin(sim, runSlot(), 3.0);
        } else {  // a world to drive: just put you at its start
            run.stop();
            V3 p = lv.hasStart ? lv.start : sim.world.spawn;
            double yaw = lv.hasStart ? lv.startYaw : sim.world.spawnYaw;
            if (lv.hasStart) LevelRun::gridSlot(lv, runSlot(), p, yaw);
            Car& c = sim.cars[sim.player];
            c.respawn(V3(p.x, sim.world.clearY(p.x, p.z) + 0.05, p.z), yaw);
            c.setDown(sim.world);
            c.spawnPos = c.frame().com, c.spawnYaw = yaw;
            hud.toast(lv.name + ": FREE DRIVE");
        }
        if (net.role == NetSession::HOST) net.setLevel(saveLevelText(lv));
    };
    auto restartLevel = [&]() {
        if (!run.active()) return;
        if (net.role == NetSession::CLIENT) {  // (the host starts races; on your own: back to the grid)
            run.begin(sim, runSlot(), 3.0);
            return;
        }
        run.begin(sim, runSlot(), 3.0);
        if (net.role == NetSession::HOST) net.sendGame("restart");
    };
    auto quitLevel = [&]() {
        if (!run.active()) return;
        run.stop();
        if (net.role == NetSession::HOST) net.setLevel("");
        hud.toast("FREE DRIVE");
    };
    // ---- The world editor (offline) ----
    Editor editor;
    Level lastEdited;
    bool haveEdited = false;
    auto openEditor = [&](const Level& base) {
        if (net.active()) {
            hud.toast("THE EDITOR IS OFFLINE ONLY: LEAVE THE SESSION FIRST");
            return;
        }
        run.stop();
        stopPilot();
        sim.world = World(base.map);
        editor.open(sim.world, renderer, base);
        drawnWorld = sim.world.stamp;
        sim.paused = true;
        fx.clear();
    };
    auto editorKeyLevel = [&]() {  // what F2 edits: the last edited level, or the one being played, or a new one here
        if (haveEdited && lastEdited.map == sim.world.map) return lastEdited;
        if (run.active()) return run.level;
        Level l;
        l.map = sim.world.map;
        l.name = "UNTITLED";
        return l;
    };
    auto handleMenu = [&](PauseMenu::Result res) {
        switch (res.action) {
            case PauseMenu::NONE: break;
            case PauseMenu::RESUME: closeMenu(); break;
            case PauseMenu::DRIVE:
                closeMenu();
                sim.swapPlayerVehicle(garage.spec(res.vehicle));
                hud.toast(std::string("NOW DRIVING ") + vehicleCatalog()[res.vehicle].name);
                break;
            case PauseMenu::GARAGE:
                closeMenu();
                garageFromMenu = openGarage();
                if (!garageFromMenu) hud.toast("NO GARAGE FOR THIS VEHICLE");
                break;
            case PauseMenu::CONTROLS: closeMenu(); hud.showHelp = true; break;
            case PauseMenu::RESET:
                closeMenu();
                if (sim.realRecovery) {
                    sim.towPlayerHome();
                    hud.toast("TOWED BACK TO THE START (DAMAGE STAYS)");
                } else {
                    sim.respawnPlayer();
                    hud.toast("RESET");
                }
                break;
            case PauseMenu::QUIT: running = false; break;
            case PauseMenu::CHANGED: applySettings(); saveSettings(); break;
            case PauseMenu::PILOT_GO:
                closeMenu();
                saveSettings();
                startPilot();
                break;
            case PauseMenu::PILOT_STOP:
                closeMenu();
                stopPilot();
                break;
            case PauseMenu::HOST:
                closeMenu();
                startHost(net::kDefaultPort);
                if (run.active() && net.role == NetSession::HOST) net.setLevel(saveLevelText(run.level));  // they join into it
                break;
            case PauseMenu::JOIN:
                closeMenu();
                saveSettings();
                startJoin(settings.joinAddress);
                break;
            case PauseMenu::LEAVE:
                closeMenu();
                net.close();
                removeRemotes();
                hud.toast("LEFT THE SESSION");
                break;
            case PauseMenu::FREE_DRIVE:
                closeMenu();
                run.stop();
                if (net.role == NetSession::HOST) net.setLevel("");
                if (menu.mapPick != sim.world.map && net.role != NetSession::CLIENT) {
                    loadMap(menu.mapPick);
                    settings.map = menu.mapPick;
                    saveSettings();
                }
                sim.swapPlayerVehicle(garage.spec(res.vehicle));
                hud.toast(std::string(mapName(sim.world.map)) + ": " + vehicleCatalog()[res.vehicle].name);
                break;
            case PauseMenu::PLAY_LEVEL:
                if (res.vehicle < 0 || res.vehicle >= (int)levelList.size()) break;
                if (net.role == NetSession::CLIENT) {
                    hud.toast("THE HOST PICKS THE LEVEL");
                    break;
                }
                closeMenu();
                playLevel(levelList[res.vehicle]);
                break;
            case PauseMenu::EDIT_LEVEL:
                closeMenu();
                if (res.vehicle >= 0 && res.vehicle < (int)levelList.size()) {
                    Level l = levelList[res.vehicle];
                    if (l.builtin) l.builtin = false, l.file.clear(), l.name += " (MINE)";  // (the game's: edit a copy)
                    openEditor(l);
                }
                break;
            case PauseMenu::EDITOR: {
                const int m = menu.title ? menu.mapPick : sim.world.map;
                closeMenu();
                Level l;
                l.map = m;
                l.name = "UNTITLED";
                openEditor(l);
                break;
            }
            case PauseMenu::LEVEL_RESTART:
                closeMenu();
                restartLevel();
                break;
            case PauseMenu::LEVEL_QUIT:
                closeMenu();
                quitLevel();
                break;
            case PauseMenu::MAIN_MENU:
                run.stop();
                if (net.role == NetSession::HOST) net.setLevel("");
                refreshLevels();
                menu.showTitle(std::max(0, sim.cars[sim.player].spec->model), sim.world.map);
                break;
            case PauseMenu::LOAD_MAP: {
                closeMenu();
                if (net.active()) {
                    hud.toast("LEAVE THE MULTIPLAYER SESSION TO CHANGE MAPS");
                    break;
                }
                loadMap(res.vehicle);
                settings.map = res.vehicle;
                saveSettings();
                hud.toast(std::string("MAP: ") + mapName(res.vehicle));
                break;
            }
        }
    };
    if (opt.menu == 9) {  // (testing: the title screen)
        openMenu();
        menu.showTitle(std::max(0, sim.cars[sim.player].spec->model), sim.world.map);
    } else if (opt.menu >= 2) {
        openMenu();
        menu.showPage(opt.menu, std::max(0, sim.cars[sim.player].spec->model), sim.world.map);
    } else if (opt.menu >= 0) {
        openMenu();
        if (opt.menu == 1) {
            for (int k = 0; k < 6; k++) menu.key(SC_S, settings);  // down to SETTINGS
            menu.key(SC_RETURN, settings);
        }
    }
    if (opt.hostPort > 0) startHost(opt.hostPort);
    else if (!opt.join.empty()) startJoin(opt.join);
    // Launched plainly (no map, spawn, session or test asked for): the title screen first.
    if (opt.editor) {
        Level l;
        l.map = sim.world.map;
        l.name = "UNTITLED";
        openEditor(l);
    } else if (!opt.level.empty()) {
        refreshLevels();
        std::string want = opt.level;
        for (char& ch : want) ch = (char)std::toupper((unsigned char)ch);
        int found = -1;
        for (int k = 0; k < (int)levelList.size() && found < 0; k++)
            if (levelList[k].name.find(want) != std::string::npos) found = k;
        if (found >= 0) playLevel(levelList[found]);
        else std::printf("no level matches '%s'\n", opt.level.c_str());
    } else if (!scripted && opt.scenario.empty() && opt.map < 0 && !opt.spawnSet && opt.hostPort <= 0 && opt.join.empty() && opt.menu < 0 && opt.resumeModel < 0) {
        openMenu();
        menu.showTitle(std::max(0, sim.cars[sim.player].spec->model), sim.world.map);
    }
    // Mouse positions arrive in window coordinates; the HUD is drawn in drawable pixels.
    auto toDrawable = [&](int x, int y, float& dx, float& dy) {
        int ww = 1, wh = 1, dw = 1, dh = 1;
        SDL_GetWindowSize(win, &ww, &wh);
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        dx = x * (float)dw / std::max(1, ww);
        dy = y * (float)dh / std::max(1, wh);
    };

    int timeScaleIdx = 0;
    float fps = 60;
    uint64_t freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    int frame = 0;
    size_t spawnType = 0, trafficType = 0;

    while (running) {
        uint64_t now = SDL_GetPerformanceCounter();
        double dt = scripted ? 1.0 / 60.0 : std::min(0.1, (double)(now - last) / freq);
        last = now;
        if (dt > 0) fps += ((float)(1.0 / dt) - fps) * 0.05f;
        hud.newBuild = watch.poll();
        if (hud.newBuild && std::getenv("CZ_AUTORELOAD")) reload = true, running = false;  // (testing the reload)

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (editor.active && e.type != SDL_QUIT && !menu.open) {
                int ww = 1, wh = 1, dw = 1, dh = 1;
                SDL_GetWindowSize(win, &ww, &wh);
                SDL_GL_GetDrawableSize(win, &dw, &dh);
                editor.handleEvent(e, (float)dw / std::max(1, ww));
                continue;
            }
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_KEYDOWN: {
                    if (menu.open) {  // the menu takes every key (repeats too: hold to slide)
                        const uint8_t* ks = SDL_GetKeyboardState(nullptr);
                        menu.shift = ks[SC_LSHIFT] || ks[SC_RSHIFT];
                        handleMenu(menu.key(evKeyScancode(e), settings));
                        break;
                    }
                    if (evKeyRepeat(e)) break;
                    if (garageUI.open) {
                        const auto sc = evKeyScancode(e);
                        int& pk = garageUI.pick.pick[garageUI.slot];
                        const int n = (int)modOptions(garageUI.vehicle, garageUI.slot).size();
                        if (sc == SC_W || sc == SC_UP) garageUI.slot = (garageUI.slot + MOD_SLOTS - 1) % MOD_SLOTS;
                        else if (sc == SC_S || sc == SC_DOWN) garageUI.slot = (garageUI.slot + 1) % MOD_SLOTS;
                        else if (sc == SC_A || sc == SC_LEFT) pk = (pk + n - 1) % n;
                        else if (sc == SC_D || sc == SC_RIGHT) pk = (pk + 1) % n;
                        else if (sc == SC_RETURN || sc == SC_KP_ENTER) {
                            garage.fitted[garageUI.vehicle] = garageUI.pick;
                            garage.save(garagePath);
                            sim.swapPlayerVehicle(garage.spec(garageUI.vehicle));
                            garageUI.open = false;
                            hud.toast(garageUI.pick.stock() ? "BACK TO STOCK" : "MODS FITTED");
                        } else if (sc == SC_G || sc == SC_ESCAPE) {
                            garageUI.open = false;
                            if (garageFromMenu) openMenu();  // back where you came from
                        }
                        if (!garageUI.open) garageFromMenu = false;
                        break;
                    }
                    if (winchAim.on) {  // picking a winch anchor takes these keys
                        const int sc = evKeyScancode(e);
                        if (sc == SC_ESCAPE || sc == SC_BACKSPACE) {
                            winchAim.on = false;
                            break;
                        }
                        if (sc == SC_TAB) {
                            winchAim.step(shiftDown() ? -1 : 1);
                            break;
                        }
                        if (sc == SC_V || sc == SC_RETURN || sc == SC_KP_ENTER) {
                            confirmWinch();
                            break;
                        }
                    }
                    switch (evKeyScancode(e)) {
                        case SC_ESCAPE:
                            if (hud.showHelp) hud.showHelp = false;
                            else openMenu();
                            break;
                        case SC_R:
                            // R: fixed up where it stands. R twice quickly: back to where it started
                            // (in a level: back to the grid, the attempt starts again).
                            if (hud.confirmReset > 0) {
                                hud.confirmReset = 0;
                                if (run.active()) {
                                    restartLevel();
                                    break;
                                }
                                sim.respawnPlayer();
                                hud.toast("RESPAWNED AT THE START");
                            } else if (!run.repairsAllowed()) {
                                hud.confirmReset = 0.45;
                                hud.toast("NO REPAIRS ON THIS LEVEL: R TWICE TO START AGAIN");
                            } else {
                                hud.confirmReset = 0.45;
                                sim.recoverPlayer();
                                hud.toast("REPAIRED (R TWICE: BACK TO THE START)");
                            }
                            break;
                        case SC_S:
                            sim.steeringAssist = !sim.steeringAssist;
                            settings.steeringAssist = sim.steeringAssist;
                            saveSettings();
                            if (sim.steeringAssist)
                                hud.toast("STEERING ASSIST ON (LIMITED AT HIGH SPEED)");
                            else
                                hud.toast("STEERING ASSIST OFF (FULL LOCK AT ALL SPEEDS)");
                            break;
                        case SC_T:
                            if (sim.realRecovery) {
                                sim.rightPlayer();
                                hud.toast("BACK ON ITS WHEELS - DAMAGE STAYS UNTIL A REPAIR STATION");
                            } else {
                                sim.recoverPlayer();
                                hud.toast("RECOVERED");
                            }
                            break;
                        case SC_MINUS:
                        case SC_EQUALS: {  // winch speed
                            Car& c = sim.cars[sim.player];
                            c.winchSpeed = clampd(std::round((c.winchSpeed + (evKeyScancode(e) == SC_EQUALS ? 0.1 : -0.1)) * 10) / 10, 0.1, 1.0);
                            char t[48];
                            std::snprintf(t, sizeof t, "WINCH SPEED %d%%", (int)std::lround(c.winchSpeed * 100));
                            hud.toast(t);
                            break;
                        }
                        case SC_V: {  // unhook, or start picking an anchor
                            const Car& c = sim.cars[sim.player];
                            if (sim.dropWinch(sim.player)) hud.toast("WINCH LINE UNHOOKED AND SPOOLED");
                            else if (c.isProp() || !(c.spec->extras & EXTRA_WINCH)) hud.toast("NO WINCH ON THIS TRUCK");
                            else {
                                winchAim = WinchAim();
                                winchAim.on = true;
                                winchAim.targets = sim.winchTargets(sim.player);
                                winchAim.refresh = 0.3;
                                winchAim.pickCentered(lastView);
                            }
                            break;
                        }
                        case SC_O: {
                            const std::string m = sim.hookStrap(sim.player);
                            if (!m.empty()) hud.toast(m);
                            break;
                        }
                        case SC_Y: {  // front sway bar disconnect
                            Car& c = sim.cars[sim.player];
                            const int kind = c.isProp() ? 0 : c.spec->swayDisconnect;
                            const double v = len(c.frame().vel);
                            if (kind == 0) hud.toast("NO SWAY BAR DISCONNECT ON THIS CAR");
                            else if (!c.swayDisconnected && kind == 1 && v > 30 / 3.6) hud.toast(settings.mph ? "SLOW BELOW 18 MPH TO DISCONNECT THE SWAY BAR" : "SLOW BELOW 30 KM/H TO DISCONNECT THE SWAY BAR");
                            else if (kind == 2 && v > 0.5) hud.toast("STOP TO PULL THE SWAY BAR PINS");
                            else {
                                c.swayDisconnected = !c.swayDisconnected;
                                hud.toast(c.swayDisconnected ? "FRONT SWAY BAR DISCONNECTED: MORE ARTICULATION, MORE BODY ROLL"
                                                             : "FRONT SWAY BAR CONNECTED");
                            }
                            break;
                        }
                        case SC_TAB:
                            sim.player = sim.nextVehicle(sim.player);
                            sim.cars[sim.player].wake();
                            hud.toast(std::string("DRIVING ") + sim.cars[sim.player].spec->name);
                            break;
                        case SC_C: {
                            static const char* names[] = {"CAMERA: CHASE", "CAMERA: ORBIT", "CAMERA: COCKPIT"};
                            cam.mode = (cam.mode + 1) % 3;
                            hud.toast(names[cam.mode]);
                            break;
                        }
                        case SC_N: {
                            static const char* names[] = {"BEAM VIEW OFF", "BEAM VIEW: X-RAY", "BEAM VIEW: BEAMS ONLY"};
                            debug = (DebugView)((debug + 1) % DEBUG_VIEW_COUNT);
                            hud.toast(names[debug]);
                            break;
                        }
                        case SC_H: hud.showHelp = !hud.showHelp; break;
                        case SC_F2:
                            openEditor(editorKeyLevel());
                            break;
                        case SC_F3:
                            settings.showFps = hud.showFps = !hud.showFps;
                            saveSettings();
                            break;
                        case SC_J: hud.scenarioDetail = !hud.scenarioDetail; break;
                        case SC_F5:  // load the new build (see BuildWatch)
                            if (hud.newBuild) reload = true, running = false;
                            else hud.toast(watch.exe.empty() ? "RELOADING ISN'T AVAILABLE HERE" : "NO NEW BUILD YET");
                            break;
                        case SC_P:
                            if (net.active()) hud.toast("NO PAUSING IN MULTIPLAYER (ESC STILL OPENS THE MENU)");
                            else sim.paused = !sim.paused;
                            break;
                        case SC_X: {
                            Car& c = sim.cars[sim.player];
                            c.tractionControl = !c.tractionControl;
                            hud.toast(c.tractionControl ? "TRACTION CONTROL ON" : "TRACTION CONTROL OFF");
                            break;
                        }
                        case SC_1:
                        case SC_2:
                        case SC_3:
                        case SC_4: {
                            size_t idx = evKeyScancode(e) - SC_1;
                            if (idx < vehicleCatalog().size()) {
                                sim.swapPlayerVehicle(garage.spec((int)idx));
                                hud.toast(std::string("NOW DRIVING ") + vehicleCatalog()[idx].name);
                            }
                            break;
                        }
                        case SC_G: openGarage(); break;
                        case SC_E:
                        case SC_Q: {
                            Car& c = sim.cars[sim.player];
                            if (c.shift(evKeyScancode(e) == SC_E ? 1 : -1) && !c.manualShift) {
                                c.manualShift = true;  // taking over the gears switches to manual
                                hud.toast("MANUAL SHIFTING (M FOR AUTOMATIC)");
                            }
                            break;
                        }
                        case SC_M: {
                            Car& c = sim.cars[sim.player];
                            c.manualShift = !c.manualShift;
                            hud.toast(c.manualShift ? "MANUAL SHIFTING: Q DOWN, E UP" : "AUTOMATIC SHIFTING");
                            break;
                        }
                        case SC_L: {  // transfer case: 2H -> 4A/4H -> 4H/4H LOCK -> 4L -> 2H
                            if (shiftDown()) {  // Shift+L: the headlight switch, AUTO -> LOW -> HIGH -> OFF
                                lampMode = (lampMode + 1) % LAMP_MODES;
                                hud.toast(lampModeName(lampMode));
                                break;
                            }
                            Car& c = sim.cars[sim.player];
                            const auto& modes = driveModes(c.isProp() ? XFER_NONE : c.spec->transfer);
                            if (modes.empty() || !c.mode()) {
                                hud.toast("NO TRANSFER CASE ON THIS CAR (SHIFT+L: LIGHTS)");
                                break;
                            }
                            const int next = (c.driveMode + 1) % (int)modes.size();
                            const DriveMode& m = modes[next];
                            // 2H, 4A and 4H shift on the fly; into or out of low range needs a crawl.
                            if (m.low != c.mode()->low && len(c.frame().vel) > 8 / 3.6) {
                                hud.toast(m.low ? "SLOW DOWN TO SHIFT INTO LOW RANGE" : "SLOW DOWN TO SHIFT OUT OF LOW RANGE");
                                break;
                            }
                            c.setDriveMode(next);
                            hud.toast(std::string(m.name) +
                                      (!m.front ? ": REAR-WHEEL DRIVE" : m.centerLock ? ": FRONT AND REAR LOCKED TOGETHER" : ": ALL-WHEEL DRIVE") +
                                      (m.low ? ", LOW RANGE" : ""));
                            break;
                        }
                        case SC_F:
                        case SC_B: {  // lockers
                            Car& c = sim.cars[sim.player];
                            const bool front = evKeyScancode(e) == SC_F;
                            const char* which = front ? "FRONT" : "REAR";
                            if (c.isProp() || !(front ? c.spec->lockerFront : c.spec->lockerRear)) {
                                hud.toast(std::string("NO ") + which + " LOCKER" + (front && c.spec->transfer != XFER_NONE ? " (GARAGE: LOCKERS)" : ""));
                            } else if (front ? c.lockFront : c.lockRear) {
                                c.setLocker(front, false);
                                hud.toast(std::string(which) + " LOCKER OFF");
                            } else if (front && !c.frontEngaged()) {
                                hud.toast("SHIFT INTO 4WD (L) TO LOCK THE FRONT");
                            } else if (len(c.frame().vel) > 30 / 3.6) {
                                hud.toast("SLOW DOWN TO ENGAGE THE LOCKER");
                            } else {
                                c.setLocker(front, true);
                                hud.toast(std::string(which) + " LOCKER ON");
                            }
                            break;
                        }
                        case SC_I:
                            hud.toast(sim.spawnAhead(garage.spec((int)spawnType))
                                          ? std::string("SPAWNED ") + vehicleCatalog()[spawnType].name
                                          : std::string("VEHICLE LIMIT REACHED"));
                            spawnType = (spawnType + 1) % vehicleCatalog().size();
                            break;
                        case SC_K:
                            hud.toast(sim.spawnTraffic(garage.spec((int)trafficType))
                                          ? std::string("AI ") + vehicleCatalog()[trafficType].name + " ON THE LOOP ROAD"
                                          : std::string("VEHICLE LIMIT REACHED"));
                            trafficType = (trafficType + 1) % vehicleCatalog().size();
                            break;
                        case SC_U: {  // autopilot on (to the place picked in the menu) or off
                            Car& c = sim.cars[sim.player];
                            if (c.pilot.active || c.ai) stopPilot();
                            else startPilot();
                            break;
                        }
                        case SC_DELETE:
                        case SC_BACKSPACE: sim.removePlayerCar(); hud.toast("VEHICLE REMOVED"); break;
                        case SC_LEFTBRACKET:
                            if (net.active()) break;  // everyone shares one clock
                            timeScaleIdx = std::min(timeScaleIdx + 1, 4); sim.timeScale = kTimeScales[timeScaleIdx]; break;
                        case SC_RIGHTBRACKET:
                            if (net.active()) break;
                            timeScaleIdx = std::max(timeScaleIdx - 1, 0); sim.timeScale = kTimeScales[timeScaleIdx]; break;
                        case SC_F11: {
                            settings.fullscreen = !settings.fullscreen;
                            applySettings();
                            saveSettings();
                            break;
                        }
                    }
                    break;
                }
                case SDL_MOUSEMOTION:
                    if (menu.open) {
                        float mx, my;
                        toDrawable(evMotionX(e), evMotionY(e), mx, my);
                        handleMenu(menu.mouseMove(mx, my, evMotionState(e) & 1, settings));
                    } else if (evMotionState(e) & 0x5) {  // left or right button held
                        cam.drag(evMotionXrel(e), evMotionYrel(e));
                        dragging = true;
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (!menu.open && winchAim.on && evButton(e) == 3) winchAim.on = false;  // right click: cancel
                    if (menu.open && evButton(e) == 1) {
                        float mx, my;
                        toDrawable(evButtonX(e), evButtonY(e), mx, my);
                        handleMenu(menu.mouseDown(mx, my, settings));
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (!menu.open && winchAim.on && evButton(e) == 1) {
                        if (dragging) winchAim.pickCentered(lastView);  // aimed by turning the camera
                        else confirmWinch();                            // a click
                    }
                    dragging = false;
                    menu.mouseUp();
                    break;
                case SDL_MOUSEWHEEL:
                    if (!menu.open && winchAim.on) winchAim.step(evWheelY(e) > 0 ? -1 : 1);
                    else if (!menu.open) cam.dist = clampd(cam.dist * std::pow(0.88, evWheelY(e)), 3.0, 60.0);
                    break;
            }
        }

        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        bool up = keys[SC_W] || keys[SC_UP], down = keys[SC_S] || keys[SC_DOWN];
        bool left = keys[SC_A] || keys[SC_LEFT], right = keys[SC_D] || keys[SC_RIGHT], hb = keys[SC_SPACE];
        const bool shiftHeld = keys[SC_LSHIFT] || keys[SC_RSHIFT];
        int winch = keys[SC_Z] ? (shiftHeld ? -1 : 1) : 0;
        int airing = keys[SC_9] ? -1 : keys[SC_0] ? 1 : 0;
        if (garageUI.open || menu.open || editor.active) {  // parked while you work on it
            up = down = left = right = false;
            hb = true;
            winch = airing = 0;
        }
        if (!playback.active() && sim.player >= 0 && sim.player < (int)sim.cars.size()) {  // (a scenario drives its own cars)
            for (Car& c : sim.cars) c.airing = 0;
            sim.cars[sim.player].airing = airing;
            for (int i = 0; i < (int)sim.cars.size(); i++) sim.setWinch(i, i == sim.player ? winch : 0);
            if (airing && len(sim.cars[sim.player].frame().vel) > 0.5) hud.toast("STOP TO LET AIR OUT OR PUMP UP THE TIRES");
        }
        if (scripted) {
            bool donut = opt.script == "donut";
            up = frame > 30 && (opt.script == "drive" || opt.script == "left" || donut);
            left = (opt.script == "left" || donut) && frame > 90;
            hb = donut && frame > 150 && frame < 175;
            down = right = false;
            if (donut) sim.cars[sim.player].tractionControl = false;
            if (opt.script == "pilot" && frame == 20) {  // screenshot: autopilot to the halfway hut
                const std::vector<Place> places = pilotPlaces(sim, net);
                for (int k = 0; k < (int)places.size(); k++)
                    if (places[k].name.find("HALFWAY") != std::string::npos) settings.pilotDest = k;
                startPilot();
            }
            if (opt.script == "pilot") up = down = left = right = false;
            if (opt.script == "aim" && frame == 20) {  // screenshot of picking a winch anchor
                winchAim = WinchAim();
                winchAim.on = true;
                winchAim.targets = sim.winchTargets(sim.player);
                winchAim.pickCentered(lastView);
            }
            if (opt.script == "winch" || opt.script == "strap") {  // screenshots of the recovery gear
                if (frame == 20) std::printf("%s\n", (opt.script == "winch" ? sim.hookWinch(sim.player) : sim.hookStrap(sim.player)).c_str());
                winch = opt.script == "winch" && frame > 20 ? 1 : 0;
                up = opt.script == "strap" && frame > 40;
                sim.setWinch(sim.player, winch);
            }
        }
        double simDt = 0;
        if (playback.active() && !playback.done) {
            // The scenario drives everything; advance it in whole 60 Hz frames.
            if (!sim.paused) playback.accum += dt * sim.timeScale;
            for (int k = 0; k < 4 && playback.accum >= Ctx::kFrame; k++) {
                playback.accum -= Ctx::kFrame;
                if (!playback.stepFrame()) break;
                simDt += Ctx::kFrame;
            }
            if (playback.done && playback.worker.joinable()) playback.worker.join();
        } else {
            const double netNow = (double)(SDL_GetPerformanceCounter()) / freq;
            net.receive(sim, netNow);
            // The host's level (a client loads it as it comes), and the race chatter.
            if (net.levelFresh) {
                net.levelFresh = false;
                Level lv;
                std::string why;
                if (net.levelText.empty()) {
                    if (run.active()) run.stop(), hud.toast("THE HOST ENDED THE LEVEL");
                } else if (parseLevelText(net.levelText, lv, &why)) {
                    playLevel(lv);
                } else {
                    hud.toast("THE HOST'S LEVEL WON'T LOAD: " + why);
                }
            }
            for (const NetSession::GameMsg& m : net.gameIn) {
                if (m.text == "restart") {
                    if (run.active()) run.begin(sim, runSlot(), 3.0);
                } else if (m.player >= 0 && m.player < net::kMaxPeers) {
                    run.onMessage(m.player, net.players[m.player].name, m.text);
                }
            }
            net.gameIn.clear();
            if (!sim.paused) sim.drive(up, down, left, right, hb, dt);
            if (!sim.paused && run.active()) {
                run.update(sim, dt);  // (after the keys: it holds the car on the grid)
                if (run.state == LevelRun::FINISHED && (bestTimes.find(run.level.name) == bestTimes.end() || run.finishTime < bestTimes[run.level.name]))
                    bestTimes[run.level.name] = run.finishTime;
                for (const std::string& m : run.outbox) net.sendGame(m);
                run.outbox.clear();
            }
            simDt = sim.advance(dt, 120) * sim.h;
            if (sim.wrapShift != wrapSeen) {  // the freeway moved everything back a period: so do the camera and the effects
                const V3 d(sim.wrapShift - wrapSeen, 0, 0);
                cam.target += d;
                fx.translate(d);
                wrapSeen = sim.wrapShift;
            }
            for (const std::string& ev : sim.events) hud.toast(ev);
            sim.events.clear();
            for (const std::string& ev : net.events) hud.toast(ev);
            net.events.clear();
        }

        // Reset anything that exploded numerically; track damage and crash energy.
        double impact = 0;
        for (int i = 0; i < (int)sim.cars.size(); i++) {
            Car& c = sim.cars[i];
            if (!c.finite()) {
                sim.dropRopes(i);
                c.respawn(c.spawnPos, c.spawnYaw);
            }
            c.updateStats();
            impact += c.impactEnergy;
            c.impactEnergy = 0;
        }
        if (!(playback.active() && !playback.done)) net.send(sim, (double)(SDL_GetPerformanceCounter()) / freq);
        FxEvents events = fx.update(sim, simDt);

        Car& car = sim.cars[sim.player];
        Frame fr = car.frame();
        if (audioOn) {
            float slip = 0, load = (float)(sim.paused ? 0 : car.throttle * car.tcCut);
            for (const Wheel& wh : car.wheels)
                if (wh.load > 500) slip = std::max(slip, wh.slip);
            audio.setEngine((float)car.rpm * (float)std::max(0.35, sim.timeScale), load);
            const VehicleSpec& vs = *car.spec;
            audio.setVoice(vs.cylinders, (float)vs.displacement, (int)vs.induction, vs.flatPlane, (float)vs.redline);
            audio.setBoost(vs.induction == VehicleSpec::TURBO ? (float)car.boost : 0.0f);
            audio.setTires(sim.paused ? 0 : slip);
            audio.setVolume(settings.volume * (menu.open ? 0.35f : 1.0f));
            audio.setMusic(sim.world.map, settings.music * settings.volume);  // not ducked in the menu
            audio.setSpeed(sim.paused ? 0 : (float)len(fr.vel));
            float amp = (float)clampd(std::log10(1.0 + impact / 50.0) / 3.5, 0, 1);
            if (amp > 0.08) audio.impact(amp);
            if (events.glass) audio.glass(std::min(1.0f, 0.4f + 0.06f * events.glass));
            if (events.tirePops) audio.pop();
        }

        int W, H;
        SDL_GL_GetDrawableSize(win, &W, &H);
        if (editor.active && scripted && opt.script == "editdemo" && frame == 5) {
            // (Screenshots: a few edits in front of the camera, through the editor's core.)
            const View v = editor.view(W, H);
            const V3 f = norm(V3(v.target.x - v.eye.x, 0, v.target.z - v.eye.z)), l(-f.z, 0, f.x);
            const V3 c = V3(v.eye.x, 0, v.eye.z) + f * 45;
            EditCore& ed = editor.core;
            ed.brush(BRUSH_RAISE, c.x + l.x * 12, c.z + l.z * 12, 9, 4.0);
            for (int k = 0; k < (int)paintMaterials().size(); k++) {
                if (paintMaterials()[k].surface == SURF_MUD) ed.paint(c.x, c.z, 7, k, true);
                if (paintMaterials()[k].surface == SURF_SNOW) ed.paint(c.x + l.x * 12, c.z + l.z * 12, 6, k, false);
            }
            for (int k = 0; k < 4; k++) {
                LevelObject o;
                o.kind = OBJ_ROCK, o.p = c - l * (10 + 3 * k) + f * (k % 2) * 3, o.size = V3(1.2, 0.9, 1.0), o.top = 0.8, o.seed = 11 + k, o.col[0] = 1;
                ed.addObject(ed.grounded(o));
            }
            LevelObject t;
            t.kind = OBJ_TREE, t.p = c + f * 12 - l * 4, t.size = V3(0, 11, 0), t.col[0] = 1;
            ed.addObject(ed.grounded(t));
            LevelObject b;
            b.kind = OBJ_BOX, b.p = c + f * 8 + l * 2, b.size = V3(3, 0.3, 2), b.pitch = 0.25, b.col[0] = 0.6f, b.col[1] = 0.58f, b.col[2] = 0.55f;
            ed.addObject(ed.grounded(b));
            const double yaw = std::atan2(-f.z, f.x);
            ed.setStart(c.x - f.x * 25, c.z - f.z * 25, yaw);
            ed.addGate({c - f * 10, yaw, 12});
            ed.addGate({c + f * 25, yaw, 12});
        }
        if (editor.active) {
            editor.update(dt, keys, renderer, W, H);
            drawnWorld = sim.world.stamp;  // (it redraws what it changes itself)
            sim.paused = true;
            if (editor.wantPlay || editor.wantClose) {
                const bool play = editor.wantPlay;
                lastEdited = editor.core.level;
                haveEdited = true;
                editor.close();
                sim.paused = false;
                if (play) playLevel(lastEdited);
                else {
                    // Back to driving, on the world as edited.
                    Level free = lastEdited;
                    free.mode = LEVEL_FREE;
                    playLevel(free);
                }
                hud.toast(play ? "PLAYING " + lastEdited.name + "   (F2: BACK TO THE EDITOR)" : "DRIVING THE EDITED WORLD   (F2: BACK TO THE EDITOR)");
            }
        }
        View view = editor.active ? editor.view(W, H) : cam.update(fr, sim.world, dt, dragging && !menu.open);
        view.width = W;
        view.height = H;
        lastView = view;
        if (winchAim.on) {
            winchAim.update(sim, dt);
            if (dragging) winchAim.pickCentered(view);  // follow the camera while aiming
        }
        clock += dt;
        view.time = (float)clock;
        {
            // Time of day: the clock runs (a whole day in settings.dayMinutes) unless it's
            // held at a fixed time; in multiplayer the host's clock rules.
            double dayRate = 0;  // clock hours per real second
            if (net.role == NetSession::CLIENT && net.dayHours >= 0) {
                if (net.dayFresh) dayHours = net.dayHours, net.dayFresh = false;
                dayRate = net.dayRate;
            } else if (opt.timeHours >= 0) dayHours = opt.timeHours;
            else if (settings.timeOfDay != TOD_CYCLE) dayHours = timeOfDayHours(settings.timeOfDay);
            else dayRate = clockAdvance(1.0, settings.dayMinutes);
            if (!sim.paused) dayHours = std::fmod(dayHours + dayRate * dt * sim.timeScale, 24.0);
            settings.clockHours = (float)dayHours;
            if (net.role == NetSession::HOST) net.dayHours = dayHours, net.dayRate = dayRate;
            renderer.daylight = computeDaylight(dayHours);
            // Headlights: yours on the switch; the AI's, autopilots' and other players' come
            // on at dusk; parked cars have nobody to switch them on.
            renderer.carBeams.assign(sim.cars.size(), 0);
            for (int i = 0; i < (int)sim.cars.size(); i++) {
                const Car& c = sim.cars[i];
                const int mode = i == sim.player ? lampMode : c.ai || c.pilot.active || c.remote ? LAMPS_AUTO : LAMPS_OFF;
                renderer.carBeams[i] = lampBeam(mode, renderer.daylight.night);
            }
        }
        menuBlur +=((menu.open ? 1.0f : 0.0f) - menuBlur) * (float)std::min(1.0, dt * 10);
        if (!menu.open && menuBlur < 0.01f) menuBlur = 0;
        renderer.settings = settings.gfx;
        renderer.settings.blur = menuBlur;
        if (sim.world.stamp != drawnWorld) {  // the map changed (a scenario built its own): redraw it
            renderer.setWorld(sim.world);
            drawnWorld = sim.world.stamp;
        }
        renderer.drawScene(sim, view, debug, &fx);
        hud.update(dt);
        if (editor.active && !menu.open) editor.draw(renderer, W, H, dt);
        else if (!menu.open) drawHud(renderer, sim, view, hud, &playback, net, winchAim, W, H, fps, dt, settings.mph);
        if (!menu.open && !editor.active && run.active()) drawLevelHud(renderer, run, sim, view, W, H);
        if (menu.open) {
            {
                const Car& pc = sim.cars[sim.player];
                menu.pilotPlaces.clear();
                for (const Place& pl : pilotPlaces(sim, net)) menu.pilotPlaces.push_back(pl.name);
                menu.pilotActive = pc.pilot.active || pc.ai;
                menu.pilotStatus = pc.ai ? "CRUISING THE LOOP ROAD" : pc.pilot.active ? "DRIVING: " + pc.pilot.status : "OFF";
            }
            menu.netActive = net.active();
            menu.levelActive = run.active();
            menu.netClient = net.role == NetSession::CLIENT;
            menu.netStatus = !net.active() ? "OFFLINE"
                             : net.role == NetSession::HOST
                                 ? "HOSTING: " + std::to_string(net.playerCount()) + (net.playerCount() == 1 ? " PLAYER" : " PLAYERS")
                                 : "IN " + net.ownerName(0) + "'S GAME: " + std::to_string(net.playerCount()) + " PLAYERS";
        }
        if (garageUI.open) drawGarage(renderer, garageUI, garage, std::max(0.75f, H / 720.0f), W, H);
        menu.draw(renderer, settings, W, H, dt);
        renderer.flushHud(W, H);

        if (scripted && opt.shotFrames.count(frame)) {
            char path[512];
            std::snprintf(path, sizeof(path), "%s_%04d.ppm", opt.shotPrefix.c_str(), frame);
            writePPM(path, renderer.readPixels(W, H), W, H);
            std::printf("wrote %s\n", path);
            if (frame >= *opt.shotFrames.rbegin()) running = false;
        }
        SDL_GL_SwapWindow(win);
        frame++;
    }

    // Where to pick up after a reload.
    std::vector<std::string> resume;
    if (reload && !sim.cars.empty() && !playback.active()) {
        const Car& c = sim.cars[sim.player];
        const Frame f = c.frame();
        char spawn[96];
        std::snprintf(spawn, sizeof spawn, "%.2f,%.2f,%.1f", f.com.x, f.com.z, std::atan2(-f.fwd.z, f.fwd.x) * 180 / M_PI);
        resume = {"--map", std::to_string(sim.world.map), "--player", std::to_string(sim.player), "--spawn", spawn};
        if (c.spec->model >= 0) resume.insert(resume.end(), {"--resume-model", std::to_string(c.spec->model)});
    }
    if (opt.timeHours < 0) saveSettings();  // (the clock carries on next time, and after a reload)
    playback.stop();
    net.close();
    audio.stop();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (reload) {
        std::printf("reloading the new build at %s\n", resume.size() > 5 ? resume[5].c_str() : "?");
        std::fflush(stdout);
        watch.exec(argc, argv, resume);
    }
    return 0;
}
