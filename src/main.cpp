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
#include <stdexcept>
#include <system_error>
#include <string>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <chrono>
#include <csignal>
#include <functional>
#include <map>

#include "audio.h"
#include "biome.h"
#include "jobs.h"
#include "career.h"
#include "interact.h"
#include "walker.h"
#include "coop.h"
#include "editor.h"
#include "fx.h"
#include "gamepad.h"
#include "gl.h"
#include "gpu.h"
#include "hudkit.h"
#include "level.h"
#include "levelrun.h"
#include "menu.h"
#include "mods.h"
#include "net.h"
#include "pilotsense.h"
#include "platform.h"
#include "props.h"
#include "carframe.h"
#include "carparts.h"
#include "handwork.h"
#include "lab.h"
#include "modelkit.h"
#include "render.h"
#include "perf.h"
#include "sdl_min.h"
#include "softbody.h"
#include "../tests/harness.h"

namespace {

const double kTimeScales[] = {1.0, 0.5, 0.25, 0.1, 0.03};

struct Options {
    int physicsThreads = -1;  // -1 saved setting; 0 automatic; otherwise a CPU thread budget
    int width = 1600, height = 900;
    bool fullscreen = false, audio = true, vsync = true;
    bool pad = true;  // --no-pad: don't open game controllers (also CZ_NO_PAD=1)
    // Scripted capture (for testing): fixed 60 Hz steps, writes PPM screenshots.
    std::string shotPrefix;
    std::set<int> shotFrames;
    std::map<int, std::vector<int>> press;  // --press FRAME:SCANCODE,...: tap these keys on those frames (testing the key path)
    std::string script;  // "", "drive", "left"
    int camera = 0;
    int debug = 0;
    int player = 0;
    std::string scenario;  // play a test scenario instead of the sandbox
    bool eyeSet = false;          // --eye X,Z,H,YAW,PITCH: the drone camera parked there (H m above the ground; degrees; yaw 0 looks +X, 90 looks -Z)
    double eyeX = 0, eyeZ = 0, eyeH = 1, eyeYaw = 0, eyePitch = 0;
    bool orbitSet = false, showHelp = false, showGarage = false;
    int mods[5] = {-1, -1, -1, -1, -1};  // --mods E,S,T[,W,R]: fit these to the player's car (testing)
    int menu = -1;                // --menu P: open the pause menu on page P (testing)
    int quality = -1;             // --quality 0-3: graphics preset (testing)
    int ground = -1;              // --ground 0-3: terrain materials (testing; see RenderSettings::ground)
    int props = -1;               // --props 0-3: scanned props (testing; see RenderSettings::props)
    int adaptive = -1;            // --adaptive 0/1: auto-scale quality (default: on)
    bool career = false;          // --career: career mode (a fresh one when scripted)
    bool photo = false;           // --photo: no HUD (screenshots; F10 in the game)
    bool mph = false;             // --mph
    int map = -1;                 // --map N: 0 proving ground, 1 mountain
    double orbitYaw = 0, orbitPitch = 0.2, orbitDist = 7.5;  // degrees, degrees, meters
    bool spawnSet = false;
    int resumeModel = -1;  // --resume-model: the vehicle to be in (after a reload)
    double spawnX = 0, spawnZ = 0, spawnHeading = 0;  // heading in degrees, 0 = +X
    int spawnSection = -1;        // --spawn-section N: start at the Nth obstacle section's entry (0-based, proving ground)
    int hostPort = -1;            // --host [PORT]: open a multiplayer session
    std::string join;             // --join HOST[:PORT]
    std::string name;             // --name NAME (multiplayer)
    std::string password;         // --password PASSWORD: for the game you host, and for the one you join
    double timeHours = -1;        // --time H: hold the clock at H hours (0..24), e.g. 21.5
    int lamps = -1;               // --lamps 0-3: headlight switch (LampMode: auto, low, high, off)
    std::string level;            // --level NAME: play the level whose name has NAME in it
    bool editor = false;          // --editor: open the world editor on the map
    bool newSection = false;      // --new-section: the editor, on a new obstacle section
    bool lab = false;             // --lab: the model lab (lab.h) instead of the game
    std::string labModel;         // --lab-model ID: start on that model
    int labDesign = -1;           // --lab-design N: just that design
};

struct CameraRig {
    int mode = 0;  // 0 chase, 1 orbit, 2 cockpit, 3 drone (flies free; the truck stays put)
    V3 droneEye;
    double droneYaw = 0, dronePitch = -0.3;
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
        // Carried along with the car first, then eased onto it: the easing only smooths the
        // bumps. (Easing alone trailed the car by speed / 14: 3 m at 140 km/h, 8 at 400, so
        // a fast car pulled away from the camera.)
        if (std::isfinite(fr.vel.x + fr.vel.y + fr.vel.z) && len(fr.vel) < 200) target += fr.vel * dt;
        target += (want - target) * (1 - std::exp(-dt * 14));
        if (std::isfinite(carYaw)) chaseYaw += wrapAngle(carYaw - chaseYaw) * (1 - std::exp(-dt * 3));
        idle = dragging ? 0 : idle + dt;
        // (Spun round and round, it comes back the short way, not unwinding every turn.)
        userYaw = wrapAngle(userYaw);
        if (mode == 0 && idle > 1.5) {
            userYaw *= std::exp(-dt * 2);
            if (pitch < -0.05) pitch += (-0.05 - pitch) * (1 - std::exp(-dt * 2));  // let go: back to looking at the car
        }

        View v;
        v.fovy = fovy;
        if (mode == 3) {
            v.fadeNear = 0.5f;  // (the drone sees through what it flies into)
            v.eye = droneEye;
            v.target = droneEye + V3(std::cos(droneYaw) * std::cos(dronePitch), std::sin(dronePitch), -std::sin(droneYaw) * std::cos(dronePitch)) * 10;
            v.up = V3(0, 1, 0);
            return v;
        }
        if (mode == 2) {
            // (Dragging looks round the cabin: sideways, and up or down from straight ahead.)
            v.eye = fr.com + fr.fwd * 0.1 + fr.up * 0.55 + fr.left * 0.35;
            const double lookUp = clampd(0.2 - pitch, -0.9, 1.1);
            const V3 flat = fr.fwd * std::cos(userYaw) + fr.left * std::sin(userYaw);
            v.target = v.eye + (flat * std::cos(lookUp) + fr.up * std::sin(lookUp)) * 10.0;
            v.up = fr.up;
            v.fovy = fovy * 1.087f;
            v.fadeNear = 0.2f;  // (inside the cab: just what the windows are pressed against)
            return v;
        }
        double yaw = mode == 0 ? chaseYaw + userYaw : orbitYaw;
        // Dragged below level: the camera stays down by the ground behind the car and tilts
        // up instead (to the sky, the trees, the cliff above).
        const double camPitch = std::max(pitch, -0.05), lookUp = std::max(0.0, -0.05 - pitch);
        v.target = target;
        v.focus = target, v.hasFocus = true, v.fadeNear = 1;  // (the trees between the camera and the car, and any close to the camera, dissolve)
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
        if (mode == 3) {
            droneYaw -= dx * 0.004;
            dronePitch = clampd(dronePitch - dy * 0.004, -1.5, 1.4);
            return;
        }
        if (mode == 1) orbitYaw -= dx * 0.006;
        else userYaw -= dx * 0.006;
        pitch = clampd(pitch + dy * 0.005, -1.0, 1.35);
    }
    // ---- gamepad: the right stick looks (+ = right / down), R3 held looks back, R3 let go resets ----
    void padLook(double x, double y, bool back, bool reset, double dt) {
        const double kx = std::copysign(std::pow(std::fabs(x), 1.5), x), ky = std::copysign(std::pow(std::fabs(y), 1.5), y);
        if (mode == 3) {
            droneYaw -= kx * 2.4 * dt;
            dronePitch = clampd(dronePitch - ky * 1.4 * dt, -1.5, 1.4);
            return;
        }
        (mode == 1 ? orbitYaw : userYaw) -= kx * 2.4 * dt;
        pitch = clampd(pitch + ky * 1.3 * dt, -1.0, 1.35);
        if (back && mode != 1) userYaw += wrapAngle(M_PI - userYaw) * (1 - std::exp(-dt * 10));
        if (reset) userYaw = 0, pitch = 0.2;
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
        exe = exePath();
        if (exe.empty()) return;
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
                                                        "--shot-prefix", "--shot-frames", "--menu", "--orbit", "--spawn-section", "--eye"};
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
        else if (a == "--physics-threads") {
            const std::string value = next();
            o.physicsThreads = value == "all" ? physicsThreadLimit() :
                value == "auto" ? 0 : (int)std::clamp(std::strtol(value.c_str(), nullptr, 10), 0L, (long)physicsThreadLimit());
        }
        else if (a == "--no-pad") o.pad = false;
        else if (a == "--size") {
            std::string s = next();
            std::sscanf(s.c_str(), "%dx%d", &o.width, &o.height);
        } else if (a == "--shot-prefix") o.shotPrefix = next();
        else if (a == "--shot-frames") {
            std::string s = next();
            for (char* t = std::strtok(&s[0], ","); t; t = std::strtok(nullptr, ",")) o.shotFrames.insert(std::atoi(t));
        } else if (a == "--press") {
            std::string s = next();
            for (char* t = std::strtok(&s[0], ","); t; t = std::strtok(nullptr, ",")) {
                int f = 0, sc = 0;
                if (std::sscanf(t, "%d:%d", &f, &sc) == 2) o.press[f].push_back(sc);
            }
        } else if (a == "--script") o.script = next();
        else if (a == "--camera") o.camera = std::atoi(next().c_str());
        else if (a == "--debug-view") o.debug = std::atoi(next().c_str());
        else if (a == "--player") o.player = std::atoi(next().c_str());
        else if (a == "--scenario") o.scenario = next();
        else if (a == "--show-help") o.showHelp = true;
        else if (a == "--garage") o.showGarage = true;
        else if (a == "--menu") o.menu = std::atoi(next().c_str());
        else if (a == "--quality") o.quality = std::atoi(next().c_str());
        else if (a == "--ground") o.ground = std::atoi(next().c_str());
        else if (a == "--props") o.props = std::atoi(next().c_str());
        else if (a == "--adaptive") o.adaptive = std::atoi(next().c_str());
        else if (a == "--photo") o.photo = true;
        else if (a == "--mph") o.mph = true;
        else if (a == "--career") o.career = true;
        else if (a == "--map") o.map = std::atoi(next().c_str());
        else if (a == "--mods") {
            std::string s = next();
            std::sscanf(s.c_str(), "%d,%d,%d,%d,%d", &o.mods[0], &o.mods[1], &o.mods[2], &o.mods[3], &o.mods[4]);
        }
        else if (a == "--orbit") {
            std::string s = next();
            o.orbitSet = std::sscanf(s.c_str(), "%lf,%lf,%lf", &o.orbitYaw, &o.orbitPitch, &o.orbitDist) == 3;
        }
        else if (a == "--eye") {
            std::string s = next();
            o.eyeSet = std::sscanf(s.c_str(), "%lf,%lf,%lf,%lf,%lf", &o.eyeX, &o.eyeZ, &o.eyeH, &o.eyeYaw, &o.eyePitch) == 5;
        }
        else if (a == "--spawn") {
            std::string s = next();
            o.spawnSet = std::sscanf(s.c_str(), "%lf,%lf,%lf", &o.spawnX, &o.spawnZ, &o.spawnHeading) >= 2;
        }
        else if (a == "--spawn-section") o.spawnSection = std::atoi(next().c_str());
        else if (a == "--host") {
            o.hostPort = net::kDefaultPort;
            if (i + 1 < argc && std::isdigit((unsigned char)argv[i + 1][0])) o.hostPort = std::atoi(argv[++i]);
        } else if (a == "--join") o.join = next();
        else if (a == "--name") o.name = next();
        else if (a == "--password") o.password = next();
        else if (a == "--resume-model") o.resumeModel = std::atoi(next().c_str());
        else if (a == "--time") o.timeHours = std::fmod(std::max(0.0, std::atof(next().c_str())), 24.0);
        else if (a == "--lamps") o.lamps = std::atoi(next().c_str());
        else if (a == "--level") o.level = next();
        else if (a == "--editor") o.editor = true;
        else if (a == "--new-section") o.newSection = true;
        else if (a == "--lab") o.lab = true;
        else if (a == "--lab-model") o.lab = true, o.labModel = next();
        else if (a == "--lab-design") o.lab = true, o.labDesign = std::atoi(next().c_str());
        else if (a == "--help" || a == "-h") {
            std::puts("usage: crumplezone [--fullscreen] [--size WxH] [--no-audio] [--no-vsync] [--no-pad] [--spawn X,Z[,HEADING]] [--spawn-section N] [--scenario NAME]\n"
                      "                   [--map N] [--host [PORT]] [--join HOST[:PORT]] [--name NAME] [--password PASSWORD] [--time HOURS] [--level NAME]\n"
                      "                   [--physics-threads auto|all|N] [--lab [--lab-model ID]]");
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

// Pointer/controller recovery picker. Straps first choose our recovery point,
// then a point on another vehicle; winches choose one reachable anchor.
struct WinchAim {
    bool on = false, pointer = false;
    RopeKind kind = ROPE_WINCH;
    int sourceHook = -1, sel = -1;
    float pointerX = 0, pointerY = 0;
    std::vector<WinchTarget> targets;
    V3 look;
    bool hasLook = false;
    V3 ray(const View& v) const {
        const float x = pointer ? pointerX : v.width * 0.5f, y = pointer ? pointerY : v.height * 0.5f;
        const V3 f = norm(v.target - v.eye), right = norm(cross(f, v.up)), up = cross(right, f);
        const double scale = 2 * std::tan(v.fovy / 2) / std::max(1, v.height);
        return norm(f + right * ((x - v.width * 0.5) * scale) - up * ((y - v.height * 0.5) * scale));
    }
    void update(const Sim& sim, const View& v) {
        if (!on) return;
        const V3 dir = ray(v);
        hasLook = false;
        if (kind == ROPE_WINCH) {
            for (double d = 0.5; d < recovery::kWinchReach + 20; d += 0.5) {
                const V3 q = v.eye + dir * d;
                if (q.y <= sim.world.height(q.x, q.z)) { look = q; hasLook = true; break; }
            }
            targets = sim.winchTargets(sim.player, hasLook ? &look : nullptr);
        } else if (sourceHook < 0) {
            targets.clear();
            for (int h = 0; h < 2; h++) {
                RopeEnd end; end.car = sim.player; end.hook = h;
                targets.push_back({end, sim.cars[sim.player].hookPoint(h), 0, true, h ? "YOUR REAR POINT" : "YOUR FRONT POINT"});
            }
        } else targets = sim.strapTargets(sim.player, sourceHook);
        sel = sim.pickRecoveryTarget(targets, v.eye, dir, 44 * std::tan(v.fovy / 2) / std::max(1, v.height));
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
    } else if (sim.world.map == MAP_BACKCOUNTRY) {
        out.push_back({"CREEK CRAWL (FOOT)", backcountryFeature("creekfoot")});
        out.push_back({"OLD MINE", backcountryFeature("mine")});
        out.push_back({"SLAB POINT LOOKOUT", backcountryFeature("slab")});
        out.push_back({"DEEP CROSSING (SOUTH BANK)", backcountryFeature("deepbank")});
        out.push_back({"ELK RIVER FORD (SOUTH BANK)", backcountryFeature("fordbank")});
        out.push_back({"HEARTBREAK HILL (TOP)", backcountryFeature("hilltop")});
        out.push_back({"THE MUD RUN (START)", backcountryFeature("mudstart")});
        out.push_back({"THE SINK (BOG HOLE)", backcountryFeature("bog3")});
        out.push_back({"SAND WASH", backcountryFeature("washmid")});
        out.push_back({"DUNE BOWL", backcountryFeature("dunering")});
        out.push_back({"SHELF ROAD (START)", backcountryFeature("shelf")});
        out.push_back({"CLIFF OVERLOOK", backcountryFeature("overlook")});
        out.push_back({"WRECK SITE", backcountryFeature("wreck")});
        out.push_back({"SNOW CORNER", backcountryFeature("snowcorner")});
        out.push_back({"FIRE TOWER", backcountryFeature("towerlook")});
        out.push_back({"TRAPPER'S CABIN", backcountryFeature("cabinend")});
    } else if (sim.world.map == MAP_PROVING_GROUND) {
        out.push_back({"SPAWN", sim.world.spawn});
        out.push_back({"CRASH WALL", V3(95, 0, 0)});
        out.push_back({"JUMP RAMPS", V3(28, 0, -30)});
        out.push_back({"WASHBOARD", V3(-6, 0, -70)});
        for (const World::SectionSpot& s : sim.world.sections) out.push_back({std::string(s.name) + " (SECTION)", s.entry});
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
    bool photo = false;     // F10: everything hidden, for pictures
    bool newBuild = false;  // the game's been rebuilt since it started: F5 reloads it
    double bestKmh = 0;     // freeway: fastest so far
    // The Frontier's race circuit: time on the lap running (-1: not on one), the last and
    // the best, and which side of the start line you were on.
    double lapTime = -1, lastLap = 0, bestLap = 0, lapSide = 0;
    bool showHelp = false, showFps = false, scenarioDetail = false;
    std::string gpu;        // the graphics card drawing it (GL_RENDERER), shown with the FPS
    double physicsMs = 0, drawCpuMs = 0;
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
    std::string gear = car.engineDead ? "-" : car.gear < 0 ? "R" : car.gear == 0 ? "N" : std::to_string(car.gear);
    float gs = px(2.2f * u), gw = 12 * gs * 0.5f + 8 * u, gh = 8 * gs + 6 * u, gy = cy + R - gh - 2 * u;
    pill(r, cx - gw / 2, gy, gw, gh, 0.0f, 0.0f, 0.0f, 0.55f);
    r.text(cx - r.textWidth(gear, gs) / 2, gy + 3 * u + gs * 0.5f, gs, gear, 1.0f, 0.72f, 0.25f);
    // Indicator lights above the hub.
    float is = px(1.0f * u), iy = cy - 26 * u;
    std::vector<std::pair<std::string, std::array<float, 3>>> lights;
    if (!car.tractionControl && car.spec->tractionFitted) lights.push_back({"TC OFF", {1.0f, 0.75f, 0.2f}});
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
        const double speed = sim.cars[sim.player].winchSpeed;
        if (pad::ui.last) std::snprintf(hint, sizeof hint, "%s + RT REEL IN   %s + LT PAY OUT   RIGHT STICK UP DOWN: SPEED %.2gX   %s + D-PAD UP: UNHOOK", pad::hint("", pad::B_LB), pad::hint("", pad::B_LB), speed, pad::hint("", pad::B_LB));
        else std::snprintf(hint, sizeof hint, "Z REEL IN   SHIFT+Z PAY OUT   - = SPEED %.2gX   V UNHOOK", speed);
        r.text(x + 8 * u, y + h + 4 * u, sm, hint, 0.8f, 0.82f, 0.86f, 0.9f);
    }
}

// The anchor gear on you (anchors.h), small, above the fuel gauge: only in a truck with a winch.
void drawAnchors(Renderer& r, const Sim& sim, float u, int W, int H) {
    const Car& car = sim.cars[sim.player];
    if (car.isProp() || !(car.spec->extras & EXTRA_WINCH)) return;
    const float s = px(1.1f * u);
    const std::string line = anchors::hudLine(sim.anchorStock);
    const float w = r.textWidth(line, s), x = W - w - 26 * u, y = H - 272 * u;
    pill(r, x - 8 * u, y - 5 * u, w + 16 * u, 8 * s + 10 * u, 0, 0, 0, 0.5f);
    // (The HUD's amber; red for a kind you've run out of.)
    float cx = x;
    for (int g = 0; g < GEAR_COUNT; g++) {
        const std::string part = std::string(anchors::gearShort((AnchorGear)g)) + " " + std::to_string(sim.anchorStock.n[g]) + (g + 1 < GEAR_COUNT ? " " : "");
        const bool none = sim.anchorStock.n[g] <= 0;
        r.text(cx, y, s, part, none ? 0.75f : 1.0f, none ? 0.45f : 0.75f, none ? 0.4f : 0.3f);
        cx += r.textWidth(part, s);
    }
}

// On foot at the open back: everything in the truck (trunk.h), a line each, grouped. In your hands: marked.
void drawInventory(Renderer& r, const Sim& sim, const TrunkStock& own, const Walker& wk, float u, int W, int H) {
    const Car& car = sim.cars[sim.player];
    const std::vector<TrunkItem> items = trunk::contents(car, own, true, TrunkRef(), false);
    struct Row {
        std::string text;
        bool head;
        bool held;
    };
    std::vector<Row> rows;
    static const char* heads[] = {"TOOLS", "REPAIR PARTS", "ANCHOR GEAR", "RECOVERY AND SNOW"};
    for (int grp = 0; grp < 4; grp++) {
        std::vector<Row> g;
        for (const TrunkItem& it : items) {
            const int k = it.ref.kind;
            if ((grp == 0 && k != TK_TOOL) || (grp == 1 && k != TK_PART) || (grp == 2 && k != TK_GEAR) || (grp == 3 && k != TK_CHAINS && k != TK_JACK)) continue;
            g.push_back({trunk::lineFor(it), false, it.ref == wk.hand});
        }
        if (grp == 3 && car.jackPlaced) g.push_back({"HI-LIFT JACK (UNDER THE TRUCK)", false, false});
        if (g.empty()) continue;
        rows.push_back({heads[grp], true, false});
        rows.insert(rows.end(), g.begin(), g.end());
    }
    if (rows.empty()) rows.push_back({"NOTHING IN THE BACK", false, false});
    const float s = px(1.3f * u), lh = 11 * s;
    float w = r.textWidth("TRUCK INVENTORY", px(1.6f * u));
    for (const Row& row : rows) w = std::max(w, r.textWidth(row.text + (row.held ? "  (IN YOUR HAND)" : ""), s));
    w += 40 * u;
    const float h = lh * (float)rows.size() + 34 * u + 10 * s + 40 * u;
    const float x = 30 * u, y = std::max(70.0f * u, (H - h) / 2);
    pill(r, x, y, w, h, 0.02f, 0.03f, 0.04f, 0.78f);
    label(r, x + 16 * u, y + 12 * u, px(1.6f * u), "TRUCK INVENTORY", 1.0f, 0.75f, 0.3f);
    float yy = y + 34 * u + 10 * s;
    for (const Row& row : rows) {
        if (row.head) r.text(x + 16 * u, yy, s, row.text, 0.55f, 0.62f, 0.7f);
        else r.text(x + 26 * u, yy, s, row.text + (row.held ? "  (IN YOUR HAND)" : ""), row.held ? 1.0f : 0.92f, row.held ? 0.75f : 0.93f, row.held ? 0.3f : 0.95f);
        yy += lh;
    }
    (void)W;
    r.text(x + 16 * u, y + h - 24 * u, px(1.1f * u), pad::ui.last ? "Y: CLOSE" : "TAB OR I: CLOSE", 0.65f, 0.68f, 0.72f);
}

// Repair station status while parked on (or rolling onto) a pad.
void drawStation(Renderer& r, const Sim& sim, float u, int W, int H) {
    const Car& car = sim.cars[sim.player];
    if (car.station < 0 && !car.isProp() && car.needsFieldRepair() && len(car.frame().vel) < 0.5) {  // (fixes.h: hands-on)
        centeredPill(r, W / 2.0f, H - 70 * u, px(1.5f * u), "SOMETHING'S BROKEN: GET OUT (J) AND FIX IT, OR GET TO A REPAIR STATION", 0.08f, 0.05f, 0.02f, 0.95f, 1.0f, 0.75f, 0.4f);
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

inline float r_textW(Renderer& r, const std::string& s, float scale) { return r.textWidth(s, scale); }

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
        {"T", "BACK ON ITS WHEELS"},          {"TAB", "NEXT VEHICLE (ON FOOT: TRUCK INVENTORY, I)"},
        {"V", "WINCH: HOOK / UNHOOK"},        {"Z", "WINCH IN (SHIFT+Z: OUT, - = SPEED)"},
        {"O", "TOW STRAP: CLICK YOUR POINT, THEN THEIRS"},   {"Y", "FRONT SWAY BAR DISCONNECT"},
        {"9 0", "AIR DOWN / UP (STOPPED)"},
        {"1 - 5", "BLAZER BRONCO 4RUNNER TACOMA JESKO"}, {"G", "GARAGE: ENGINE, SUSPENSION, GEARBOX"},
        {"Q E", "SHIFT DOWN / UP (R N 1...)"},           {"SHIFT+M", "MANUAL / AUTOMATIC"},
        {"M", "MAP (WAYPOINTS, PINGS)"},      {"ENTER", "CHAT"},
        {"MIDDLE CLICK", "PING: GO HERE (SHIFT: HELP)"},
        {"J", "GET OUT / IN (ON FOOT: V WINCH, F FUEL CAN)"},
        {"L", mph ? "4X4: 2H 4H 4L (LOW: BELOW 5 MPH)" : "4X4: 2H 4H 4L (LOW: BELOW 8 KM/H)"},
        {"SHIFT+L", "LIGHTS: AUTO, LOW, HIGH, OFF"},
        {"F B", "FRONT / REAR LOCKER"},       {"I", "SPAWN PARKED VEHICLE"},
        {"K", "SPAWN AI TRAFFIC"},            {"U", "AUTOPILOT (ESC > AUTOPILOT: WHERE)"},
        {"DEL", "REMOVE VEHICLE"},            {"C", "CAMERA"},
        {"MOUSE", "LOOK / ZOOM"},             {"N", "BEAM VIEW"},
        {"[ ]", "SLOW MOTION"},               {"P", "PAUSE"},
        {"X", "TRACTION CONTROL"},            {"F11", "FULLSCREEN"},
        {"F3", "FPS"},                        {"F10", "HIDE THE HUD (PHOTOS)"},                        {"ESC", "MENU: CARS, SETTINGS, QUIT"},
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

// The controller's button map (the help overlay when the pad was the last thing used).
void drawPadHelp(Renderer& r, float u, int W, int H) {
    const auto rows = pad::helpRows(pad::ui.style);
    const int n = (int)rows.size(), per = (n + 1) / 2;
    float s = px(1.2f * u), lh = 8 * s + 5 * u, keyW[2] = {0, 0}, descW[2] = {0, 0};
    for (int k = 0; k < n; k++) {
        keyW[k / per] = std::max(keyW[k / per], r.textWidth(rows[k].first, s));
        descW[k / per] = std::max(descW[k / per], r.textWidth(rows[k].second, s));
    }
    const float pd = 18 * u, colW0 = keyW[0] + 12 * u + descW[0], colW1 = keyW[1] + 12 * u + descW[1];
    const float w = colW0 + colW1 + 3 * pd, h = per * lh + 8 * s * 1.5f + 3 * pd;
    const float x = (W - w) / 2, y = (H - h) / 2;
    pill(r, x, y, w, h, 0.02f, 0.025f, 0.035f, 0.85f);
    label(r, x + pd, y + pd, px(2.0f * u), "CONTROLLER", 1.0f, 0.75f, 0.3f);
    for (int k = 0; k < n; k++) {
        const int col = k / per;
        const float cx = x + pd + (col ? colW0 + pd : 0), cy = y + pd * 2 + 8 * s * 1.5f + (k % per) * lh;
        r.text(cx, cy, s, rows[k].first, 1.0f, 0.75f, 0.3f);
        r.text(cx + keyW[col] + 12 * u, cy, s, rows[k].second, 0.9f, 0.91f, 0.93f);
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

// In a career: the parts you haven't got are locked (a price if a station sells them), and
// parts are fitted at a repair station (`station`: the one you're at, -1 none).
void drawGarage(Renderer& r, const GarageUI& g, const Garage& garage, const Career& career, int station, const Sim& sim, float u, int W, int H) {
    const VehicleSpec& stock = vehicleCatalog()[g.vehicle];
    const VehicleSpec& vs = tunedSpec(g.vehicle, g.pick);
    const float s = px(1.4f * u), big = px(2.0f * u), lh = 8 * s + 6 * u, pad = 18 * u;
    const float w = std::min((float)W - 40 * u, 760 * u), h = pad * 3 + 8 * big * 1.5f + MOD_SLOTS * (lh * 2.4f) + lh * 7.5f;
    const float x = (W - w) / 2, y = (H - h) / 2;
    pill(r, x, y, w, h, 0.02f, 0.025f, 0.035f, 0.88f);
    label(r, x + pad, y + pad, big, std::string("GARAGE  ") + stock.name, 1.0f, 0.75f, 0.3f);
    if (career.on) {
        const std::string where = station >= 0 ? std::string("AT ") + sim.world.stations[station].name : "FIT PARTS AT A REPAIR STATION";
        const std::string top = "CASH $" + std::to_string(career.cash) + "   " + where;
        r.text(x + w - pad - r.textWidth(top, s), y + pad + 2 * u, s, top, station >= 0 ? 0.55f : 1.0f, station >= 0 ? 0.9f : 0.55f, station >= 0 ? 0.6f : 0.4f);
    }
    r.text(x + pad, y + pad + 8 * big + 4 * u, s, stock.blurb, 0.6f, 0.62f, 0.66f);
    float cy = y + pad * 2 + 8 * big * 1.5f;
    for (int k = 0; k < MOD_SLOTS; k++) {
        const auto& opts = modOptions(g.vehicle, k);
        const ModOption& o = opts[g.pick.pick[k]];
        bool sel = k == g.slot, fitted = garage.fitted[g.vehicle].pick[k] == g.pick.pick[k];
        if (sel) r.rect(x + pad * 0.5f, cy - 4 * u, w - pad, lh * 2.2f, 1, 1, 1, 0.07f);
        r.text(x + pad, cy, s, modSlotName(k), 0.6f, 0.62f, 0.66f);
        const bool have = career.owns(g.vehicle, k, g.pick.pick[k]);
        std::string name = std::string(sel ? "< " : "  ") + o.name + (sel ? " >" : "") + "   " +
                           std::to_string(g.pick.pick[k] + 1) + "/" + std::to_string(opts.size()) + (fitted ? "" : "  *") + (have ? "" : "  LOCKED");
        if (have) r.text(x + pad + 150 * u, cy, s, name, sel ? 1.0f : 0.9f, sel ? 0.8f : 0.9f, sel ? 0.35f : 0.92f);
        else r.text(x + pad + 150 * u, cy, s, name, 0.55f, 0.55f, 0.58f);
        std::string detail = o.detail;
        if (!have) {
            const int price = Career::price(g.vehicle, k, g.pick.pick[k]);
            detail = price > 0 ? "$" + std::to_string(price) + (station >= 0 ? (pad::ui.last ? "  Y: BUY IT" : "  B: BUY IT") : "  AT ANY REPAIR STATION")
                               : "NOT SOLD: FIND IT OUT THERE, OR WIN IT";
        }
        r.text(x + pad + 150 * u, cy + lh, s, detail, have ? 0.62f : 1.0f, have ? 0.65f : 0.7f, have ? 0.7f : 0.35f);
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
    const std::string garageKeys = pad::ui.last ? "D-PAD UP DOWN: CHOOSE PART   LEFT RIGHT: CHANGE   " + std::string(pad::hint("", pad::B_A)) + " FIT AND DRIVE   " +
                                                      (career.on ? std::string(pad::hint("", pad::B_Y)) + " BUY   " : std::string()) + pad::hint("", pad::B_B) + " CLOSE"
                                                : std::string("W S CHOOSE PART   A D CHANGE   ENTER FIT AND DRIVE   ") + (career.on ? "B BUY   " : "") + "G CLOSE";
    r.text(x + pad, y + h - pad - 8 * s, s, garageKeys, 1.0f, 0.75f, 0.3f);
}

void drawWinchAim(Renderer& r, const Sim& sim, const WinchAim& aim, const View& view, float u, int W, int H) {
    if (!aim.on) return;
    const Car& car = sim.cars[sim.player];
    const V3 hook = car.hookPoint(aim.kind == ROPE_STRAP && aim.sourceHook >= 0 ? aim.sourceHook : 0);
    // The line's reach: a ring on the ground.
    const double reach = aim.kind == ROPE_WINCH ? recovery::kWinchReach - 1 : recovery::kStrapReach;
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
        // (A target you can't use stays listed, greyed: the reason is on its label.)
        const float cr = !t.usable ? 0.62f : t.inArc ? 0.55f : 0.5f, cg = !t.usable ? 0.58f : t.inArc ? 1.0f : 0.5f, cb = !t.usable ? 0.58f : t.inArc ? 0.6f : 0.52f;
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
        if (!t.usable) label(r, sx - r.textWidth(t.why, ts) / 2, sy + b + 6 * u + 8 * ts + 4 * u, ts, t.why, 1.0f, 0.55f, 0.4f);
    }
    const char* stage = aim.kind == ROPE_WINCH ? "WINCH: CHOOSE AN ANCHOR" : aim.sourceHook < 0 ? "STRAP: CHOOSE YOUR FRONT OR REAR POINT" : "STRAP: CHOOSE THE OTHER VEHICLE'S POINT (9 M)";
    centeredPill(r, W / 2.0f, H - 139 * u, px(1.5f * u), stage, 0.02f, 0.02f, 0.03f, 0.95f, 1.0f, 0.75f, 0.3f);
    if (!aim.pointer) {
        r.rect(W * 0.5f - 8 * u, H * 0.5f - u, 16 * u, 2 * u, 1, 1, 1, 0.8f);
        r.rect(W * 0.5f - u, H * 0.5f - 8 * u, 2 * u, 16 * u, 1, 1, 1, 0.8f);
    }
    const std::string padHelp = "RIGHT STICK AIMS   " + std::string(pad::hint("", pad::B_A)) + ": SELECT   " + pad::hint("", pad::B_B) + ": CANCEL";
    const char* help = pad::ui.last ? padHelp.c_str() : "CLICK: SELECT   MIDDLE DRAG: LOOK   WHEEL: ZOOM   ESC: CANCEL";
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

// A career's supply caches still to open, within ~250 m: a marker over each and how far.
// (Your cash, top left.)
void drawCaches(Renderer& r, const Career& career, const std::vector<CacheSpot>& caches, const View& view, float u, int W, int H) {
    if (!career.on) return;
    const std::string cash = "CAREER  $" + std::to_string(career.cash);
    const float cs = px(1.4f * u);
    pill(r, 12 * u, 12 * u, r.textWidth(cash, cs) + 16 * u, 8 * cs + 10 * u, 0.02f, 0.025f, 0.035f, 0.75f);
    r.text(20 * u, 17 * u, cs, cash, 0.55f, 0.9f, 0.6f);
    for (const CacheSpot& c : caches) {
        if (career.isOpened(c)) continue;
        const double d = len(c.at - view.eye);
        if (d > 250) continue;
        float sx, sy;
        if (!project(view, c.at + V3(0, 1.4, 0), sx, sy) || sx < 0 || sx > W || sy < 0 || sy > H) continue;
        const float k = d < 40 ? 7 * u : 5 * u;
        r.tri(sx, sy - k, sx + k, sy, sx, sy + k, 0.55f, 0.9f, 0.6f, 0.95f);
        r.tri(sx, sy - k, sx, sy + k, sx - k, sy, 0.55f, 0.9f, 0.6f, 0.95f);
        char buf[64];
        std::snprintf(buf, sizeof buf, "SUPPLY CACHE  %.0f M", d);
        const float ts = px(1.2f * u);
        label(r, sx - r.textWidth(buf, ts) / 2, sy + k + 4 * u, ts, buf, 0.55f, 0.9f, 0.6f);
    }
}

// The names over things nearby (the proving ground's model yard).
void drawTags(Renderer& r, const World& world, const View& view, float u, int W, int H) {
    const float ts = px(1.3f * u);
    for (const World::Tag& t : world.tags) {
        const double d = len(t.p - view.eye);
        if (d > 28) continue;
        float sx, sy;
        if (!project(view, t.p, sx, sy) || sx < 0 || sx > W || sy < 0 || sy > H) continue;
        label(r, sx - r.textWidth(t.text, ts) / 2, sy, ts, t.text, 1.0f, 0.85f, 0.4f);
    }
}

void drawHud(Renderer& r, const Sim& sim, const View& view, HudState& hud, const Playback* pb, const NetSession& net, const WinchAim& aim, int W,
             int H, float fps, double dt, bool mph, const Career& career, const std::vector<CacheSpot>& caches) {
    float u = std::max(0.75f, H / 720.0f);
    const Car& car = sim.cars[sim.player];
    drawGauges(r, car, hud, u, W, H, dt, mph);
    drawDriveline(r, car, u, W, H);
    drawVehicleCard(r, car, u, H);
    drawRecovery(r, sim, u);
    drawAnchors(r, sim, u, W, H);
    drawStation(r, sim, u, W, H);
    drawPlayers(r, sim, net, view, u, W, H);
    drawWinchAim(r, sim, aim, view, u, W, H);
    drawPilot(r, sim, view, u, W, H);
    drawCaches(r, career, caches, view, u, W, H);
    drawTags(r, sim.world, view, u, W, H);

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
        const std::string line = pad::ui.last ? std::string(pad::hint("", pad::B_LB)) + " + " + pad::hint("", pad::B_START) + ": CONTROLS   " + pad::hint("", pad::B_START) + ": MENU"
                                              : "H: CONTROLS   ESC: MENU";
        centeredPill(r, W / 2.0f, y, px(1.4f * u), line, 0.02f, 0.02f, 0.03f, a, 0.9f, 0.9f, 0.92f);
    }
    if (hud.showFps) {
        std::snprintf(buf, sizeof(buf), "%d FPS", (int)std::lround(fps));
        label(r, W - r.textWidth(buf, px(1.3f * u)) - 10 * u, 8 * u, px(1.3f * u), buf, 0.8f, 0.9f, 0.8f);
        // (Which chip draws it: on a laptop with two, the built-in one is far slower.)
        if (!hud.gpu.empty()) r.text(W - r.textWidth(hud.gpu, px(1.0f * u)) - 10 * u, 8 * u + 8 * px(1.3f * u) + 4 * u, px(1.0f * u), hud.gpu, 0.7f, 0.75f, 0.8f);
        std::snprintf(buf, sizeof(buf), "PHYSICS %.1f MS  DRAW CPU %.1f MS", hud.physicsMs, hud.drawCpuMs);
        r.text(W - r.textWidth(buf, px(1.0f * u)) - 10 * u, 8 * u + 8 * px(1.3f * u) + 8 * px(1.0f * u) + 8 * u,
               px(1.0f * u), buf, 0.7f, 0.75f, 0.8f);
    }
    if (hud.showHelp) pad::ui.last ? drawPadHelp(r, u, W, H) : drawHelp(r, u, W, H, mph);
}

}  // namespace

// Laptops with two graphics chips run a program on the slow built-in one unless it asks for
// the fast card: these exports are what the NVIDIA (Optimus) and AMD (PowerXpress) drivers
// look for in the .exe.
#ifdef _WIN32
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char** argv) {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);  // (a multiplayer peer dropping mid-send; macOS has no MSG_NOSIGNAL)
#endif
    Options opt = parseArgs(argc, argv);
    const bool scripted = !opt.shotFrames.empty();
#ifdef _WIN32
    // The GUI executable has no terminal. Keep driver/shader errors where players can find them.
    makeDir(configDir());
    const std::string logPath = configDir() + "/graphics.log";
    if (FILE* log = std::fopen(logPath.c_str(), "w")) {
        std::fclose(log);
        std::freopen(logPath.c_str(), "a", stdout);
        std::freopen(logPath.c_str(), "a", stderr);
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
#endif
    if (scripted) {
        opt.audio = false;
        std::setvbuf(stdout, nullptr, _IONBF, 0);  // Keep packaged-app diagnostics visible during slow CI rendering.
    }
    // Settings from last time (scripted captures use the defaults, so they're repeatable).
    Settings settings;
    const std::string settingsPath = Settings::defaultPath();
    if (!scripted) settings.load(settingsPath);
    if (opt.physicsThreads >= 0) settings.physicsThreads = opt.physicsThreads;
    if (!opt.name.empty()) settings.playerName = opt.name;
    if (settings.playerName.empty()) {
        const char* user = std::getenv("USER");
        settings.playerName = user && *user ? user : "DRIVER";
        for (char& ch : settings.playerName) ch = (char)std::toupper((unsigned char)ch);
        if (settings.playerName.size() > 16) settings.playerName.resize(16);
    }
    if (opt.quality >= 0) applyPreset(settings.gfx, opt.quality);
    if (opt.ground >= 0) settings.gfx.ground = std::min(opt.ground, 3);
    if (opt.props >= 0) settings.gfx.props = std::min(opt.props, 3);
    if (opt.adaptive >= 0) settings.adaptiveQuality = opt.adaptive != 0;
    if (opt.mph) settings.mph = true;
    if (!mapListed(settings.map)) settings.map = MAP_PROVING_GROUND;  // (a map that's been taken off the list)
    World::withShowcase = opt.scenario.empty();  // (the model yard and section park laid out on the proving ground: not under a test's replay)
    if (opt.map >= 0) settings.map = opt.map < MAP_COUNT ? opt.map : MAP_PROVING_GROUND;  // (--map with no such map: the proving ground)
    if (!opt.scenario.empty()) settings.map = MAP_PROVING_GROUND;  // the tests are built for it
    if (opt.lab) settings.map = MAP_PROVING_GROUND;                // (the model lab builds its own studio)
    if (opt.fullscreen) settings.fullscreen = true;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | (opt.audio ? (uint32_t)SDL_INIT_AUDIO : 0u)) != 0) {
        fail(std::string("SDL_Init failed: ") + SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);  // (macOS only makes 3.2+ core contexts forward-compatible)
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    // MSAA belongs to the HDR scene target. The window only receives the full-screen
    // composite and HUD; sampling it again wastes bandwidth and restricts pixel formats.
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);

    uint32_t flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (settings.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_Window* win = SDL_CreateWindow("Crumple Zone", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, opt.width,
                                       opt.height, flags);
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
    const char* glRenderer = (const char*)gl::GetString(GL_RENDERER);
    const char* glVendor = (const char*)gl::GetString(GL_VENDOR);
    // Honor the saved setting on every GPU; vendor names do not establish driver performance.
    SDL_GL_SetSwapInterval(settings.vsync && opt.vsync && !scripted ? 1 : 0);
    std::printf("OpenGL: %s / %s / %s\n", glVendor ? glVendor : "unknown",
                (const char*)gl::GetString(GL_VERSION), glRenderer ? glRenderer : "unknown");
    std::printf("GPU vendor: %s\n", gpuVendorName(detectGpuVendor(glVendor, glRenderer)));
    std::string gpuName;
    if (glRenderer) {
        gpuName = glRenderer;
        for (char& ch : gpuName) ch = (char)std::toupper((unsigned char)ch);  // (the HUD font is capitals)
    }

    // Scanned rock hulls (assets/props), so what you hit is what you see; without the bake,
    // code-made rocks. Scenario playback keeps the code-made rocks the tests ran with.
    if (opt.scenario.empty()) props::setActiveRockScans(&props::sharedHullLibrary());
    Sim sim;
    const int physicsThreads = resolvePhysicsThreads(settings.physicsThreads);
    try {
        sim.setPhysicsThreads(physicsThreads);
        std::printf("Physics: %d threads (small scenes use the main thread)\n", physicsThreads);
    } catch (const std::system_error& error) {
        std::fprintf(stderr, "Physics workers unavailable; using one thread: %s\n", error.what());
    }
    if (settings.map != MAP_PROVING_GROUND) sim.world = World(settings.map);
    sim.lineup = listedVehicles();  // (the overlanders and the Jesko: no Civic, UTV or buggy)
    sim.setup();
    // Your garage: the mods fitted last time go back on the cars. (A career keeps a garage of
    // its own: it starts on factory parts.)
    if (opt.career) settings.career = true;
    Garage garage;
    std::string garagePath = settings.career ? configDir() + "/garage-career.txt" : Garage::defaultPath();
    garage.load(garagePath);
    Career career;  // (career.h: what you own, cash, the caches you've opened)
    career.on = settings.career;
    if (scripted || !career.load(Career::defaultPath())) career.startFresh();
    std::vector<CacheSpot> caches;  // this map's supply caches (a career's)
    int cachesMap = -1;
    AnchorStock anchorSeen;  // the career's anchor gear as the sim last had it
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
    if (opt.spawnSection >= 0) {  // (the obstacle sections' entries, facing along them)
        if (opt.spawnSection < (int)sim.world.sections.size()) {
            const World::SectionSpot& s = sim.world.sections[opt.spawnSection];
            opt.spawnSet = true, opt.spawnX = s.entry.x, opt.spawnZ = s.entry.z, opt.spawnHeading = s.heading * 180 / M_PI;
            std::printf("section %d %s: entry %.1f,%.1f heading %.1f deg\n", opt.spawnSection, s.name.c_str(), s.entry.x, s.entry.z, opt.spawnHeading);
        } else std::printf("no section %d: the proving ground has %zu\n", opt.spawnSection, sim.world.sections.size());
    }
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
    if (opt.lab) {  // the model lab instead of the game (lab.h)
        LabOptions lo;
        lo.select = opt.labModel, lo.variant = opt.labDesign;
        lo.shotPrefix = opt.shotPrefix, lo.shotFrames = opt.shotFrames;
        if (opt.timeHours >= 0) lo.hours = opt.timeHours;
        renderer.settings = settings.gfx;
        const int rc = runModelLab(win, renderer, lo);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return rc;
    }
    // Adaptive quality: auto-scales graphics to maintain smooth framerate
    AdaptiveQuality adaptiveQuality;
    adaptiveQuality.init(settings.gfx, 60.0f);
    RenderSettings adaptiveSettings = settings.gfx;
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
    if (opt.eyeSet) {
        cam.mode = 3;
        cam.droneEye = V3(opt.eyeX, sim.world.height(opt.eyeX, opt.eyeZ) + opt.eyeH, opt.eyeZ);
        cam.droneYaw = opt.eyeYaw * M_PI / 180, cam.dronePitch = opt.eyePitch * M_PI / 180;
    }
    DebugView debug = (DebugView)(opt.debug % DEBUG_VIEW_COUNT);
    bool dragging = false;
    HudState hud;
    hud.gpu = gpuName;
    hud.photo = opt.photo;
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
    net::Finder finder;  // games on the local network, while the multiplayer page is open
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
    double levelHours = -1;  // the playing level's own hour (-1: none)
    int lampMode = opt.lamps >= 0 && opt.lamps < LAMP_MODES ? opt.lamps : LAMPS_AUTO;  // the player's headlight switch
    auto applySettings = [&]() {
        const int threads = resolvePhysicsThreads(settings.physicsThreads);
        if (threads != sim.physicsThreads()) {
            try {
                sim.setPhysicsThreads(threads);
                std::printf("Physics: %d threads of %d available\n", threads, physicsThreadLimit());
            } catch (const std::system_error& error) {
                hud.toast("COULD NOT CHANGE PHYSICS THREAD COUNT");
                std::fprintf(stderr, "Physics threads unchanged: %s\n", error.what());
                settings.physicsThreads = sim.physicsThreads();
            }
        }
        cam.fovy = (float)(settings.fovDeg * M_PI / 180);
        hud.showFps = settings.showFps;
        bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (fs != settings.fullscreen && !scripted)
            SDL_SetWindowFullscreen(win, settings.fullscreen ? (uint32_t)SDL_WINDOW_FULLSCREEN_DESKTOP : 0u);
        SDL_GL_SetSwapInterval(settings.vsync && opt.vsync && !scripted ? 1 : 0);
        sim.realRecovery = settings.realRecovery;
        for (Car& c : sim.cars) if (!c.remote) c.winchSpeed = settings.winchSpeed;
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
    int exitCode = 0;
    double wrapSeen = 0;  // sim.wrapShift the camera and effects have caught up with
    BuildWatch watch;
    if (!scripted) watch.start();
    bool reload = false;
    // ---- gamepad: a pad button press arrives as the key press it stands for (see gamepad.h) ----
    pad::Device padDev;
    pad::Logic padLogic;
    pad::Rumbler rumbler;
    pad::Mode padCtx = pad::Mode::Off;
    bool padShift = false, padRumbling = false;  // (padShift: the key press being handled carries Shift)
    double padRumbleTimer = 0;
    if (opt.pad && !scripted && !std::getenv("CZ_NO_PAD")) padDev.start();
    auto shiftDown = [&]() {
        const uint8_t* ks = SDL_GetKeyboardState(nullptr);
        return padShift || ks[SC_LSHIFT] || ks[SC_RSHIFT];
    };
    auto confirmWinch = [&]() {
        winchAim.update(sim, lastView);
        if (winchAim.sel < 0 || winchAim.sel >= (int)winchAim.targets.size()) {
            hud.toast("POINT AT A RECOVERY POINT OR ANCHOR");
            return;
        }
        const WinchTarget t = winchAim.targets[winchAim.sel];
        if (winchAim.kind == ROPE_STRAP && winchAim.sourceHook < 0) {
            winchAim.sourceHook = t.end.hook;
            winchAim.update(sim, lastView);
            hud.toast("NOW CLICK THE OTHER VEHICLE'S FRONT OR REAR POINT");
            return;
        }
        if (!t.inArc) { hud.toast("THAT'S BEHIND THE WINCH: TURN TO FACE IT"); return; }
        if (winchAim.kind == ROPE_WINCH && !t.usable) { hud.toast(t.why); return; }
        const std::string message = winchAim.kind == ROPE_WINCH ? sim.hookWinchTo(sim.player, t.end)
            : sim.hookStrapTo(sim.player, winchAim.sourceHook, t.end);
        hud.toast(message);
        if (message.rfind("WINCH HOOKED", 0) == 0 || message.rfind("TOW STRAP:", 0) == 0) winchAim.on = false;
    };
    auto startRecoveryPick = [&](RopeKind kind) {
        winchAim = WinchAim(); winchAim.on = true; winchAim.kind = kind;
        winchAim.update(sim, lastView);
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
        if (model != sim.cars[sim.player].spec->model) sim.swapPlayerVehicle(garage.spec(model));
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
        const std::string session = !settings.sessionName.empty() ? settings.sessionName
                                    : (settings.playerName.empty() ? std::string("PLAYER") : settings.playerName) + "'S GAME";
        net.password = !opt.password.empty() ? opt.password : settings.netPassword;
        if (net.host(port, settings.playerName, sim.world.map, settings.maxPlayers, session)) {
            net.adopt(sim);
            hud.toast("HOSTING " + session + " ON " + mapName(sim.world.map) + ": FRIENDS ON YOUR NETWORK SEE IT IN JOIN");
            std::printf("hosting %s on port %d\n", mapName(sim.world.map), port);
        } else {
            hud.toast("CAN'T HOST: " + net.error);
            std::printf("can't host: %s\n", net.error.c_str());
        }
    };
    // ---- multiplayer: joining without freezing (the menu shows a CONNECTING card meanwhile)
    int joinState = 0;  // 0 not joining, 1 connecting, 2 it failed (the card says why)
    auto beginJoin = [&](const std::string& address, const std::string& label) {
        menu.joining = true;
        menu.joinLabel = label.empty() ? address : label;
        menu.joinError.clear();
        net.password = !opt.password.empty() ? opt.password : settings.netPassword;
        if (net.beginJoin(address, settings.playerName)) {
            joinState = 1;
        } else {
            joinState = 2;
            menu.joinError = net.error;
        }
        for (char& ch : menu.joinError) ch = (char)std::toupper((unsigned char)ch);
        std::printf("joining %s\n", address.c_str());
    };
    // ---- end multiplayer joining
    auto startJoin = [&](const std::string& address) {
        net.password = !opt.password.empty() ? opt.password : settings.netPassword;
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
        levelList.erase(std::remove_if(levelList.begin(), levelList.end(), [](const Level& lv) { return !mapListed(lv.map); }), levelList.end());  // (on maps taken off the list)
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
            if (lv.section.on) {  // (a hand-made obstacle section: drive it from the entry to the exit)
                const SectionInfo& sc = lv.section;
                li.section = true;
                li.lines.insert(li.lines.begin(), std::string("SECTION: ") + sectionKindName(sc.kind) + "  -  DIFFICULTY " + std::to_string(sc.difficulty) + " OF 5");
                li.lines.push_back("TESTS " + sectionBitsText(sc.tests, false) + "   KIT " + sectionBitsText(sc.kit, true));
            }
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
        levelHours = lv.timeOfDay;
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
            else p += V3(std::sin(yaw), 0, std::cos(yaw)) * (6.5 * runSlot());  // (each player their own spot, side by side)
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
        levelHours = -1;
        if (net.role == NetSession::HOST) net.setLevel("");
        hud.toast("FREE DRIVE");
    };
    // ---- Playing together: pings, chat, name tags, the map ----
    Coop coop;
    const std::string discoveriesPath = configDir() + "/discovered.txt";
    if (!scripted) coop.loadDiscoveries(discoveriesPath);
    double discoverCheck = 0;
    float mouseX = 0, mouseY = 0;  // (drawable pixels)
    bool mapDrag = false;
    auto myName = [&]() { return net.active() && !settings.playerName.empty() ? settings.playerName : std::string("YOU"); };
    auto groundUnder = [&](float sx, float sy, V3& out) {  // where the screen point meets the ground
        int dw = 1, dh = 1;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        V3 o, d;
        screenRay(lastView, dw, dh, sx, sy, o, d);
        for (double t = 1; t < 600; t += 0.5) {
            const V3 p = o + d * t;
            if (p.y < sim.world.height(p.x, p.z)) {
                out = p;
                return true;
            }
        }
        return false;
    };
    // ---- Jobs: recoveries, convoys, scouting (the host picks; everyone works on it) ----
    std::vector<Job> jobList;
    Job job;
    bool jobOn = false, jobAnnounce = false;
    double jobBanner = 0, jobResend = 0;
    uint32_t jobSeed = 1;
    std::string jobBannerText;
    // A supply cache opened (by you, or by `who` in your session): its part or cash is yours.
    auto openCache = [&](const std::string& id, const std::string& who) {
        for (const CacheSpot& c : caches) {
            if (c.id != id) continue;
            const std::string said = career.openCache(c);
            if (said.empty()) return false;
            for (Placed& pl : sim.world.models)  // its lid comes off
                if (pl.id == "supply_crate" && len(pl.p - c.at) < 0.05) pl.set = {{"open", 1.0}}, renderer.updateObjects(sim.world);
            jobBanner = std::max(jobBanner, 5.0);
            jobBannerText = (who.empty() ? c.id + ": " : who + " OPENED " + c.id + ": ") + said;
            if (!scripted) career.save(Career::defaultPath());
            return true;
        }
        return false;
    };
    auto refreshJobs = [&]() {
        std::vector<JobPlace> places;
        for (const Place& pl : pilotPlaces(sim, net))
            if (pl.kind == 0) places.push_back({pl.name, pl.p});
        jobList = makeJobs(sim.world, places, jobSeed + (uint32_t)sim.world.map * 7919u);
        menu.jobs.clear();
        static const char* kinds[JOB_KIND_COUNT] = {"RECOVERY", "CONVOY", "SCOUTING"};
        for (const Job& j : jobList) {
            PauseMenu::LevelInfo li;
            li.name = j.title;
            li.about = j.about;
            li.lines.push_back(std::string(kinds[j.kind]) + (j.kind == JOB_SCOUT ? ": " + std::to_string(j.spots.size()) + " PLACES" : ": " + j.targetName));
            menu.jobs.push_back(li);
        }
        menu.jobPick = std::min(std::max(menu.jobPick, 0), std::max(0, (int)jobList.size() - 1));
    };
    refreshLevelsLater = [&]() {
        refreshLevels();
        refreshJobs();
    };
    auto startJob = [&](const Job& j) {
        job = j;
        job.done = false;
        job.time = 0;
        job.wreck = -1, job.wreckId = 0;
        if (job.kind == JOB_RECOVER) spawnWreck(sim, job);
        jobOn = true;
        jobAnnounce = net.role == NetSession::HOST;
        hud.toast("JOB: " + job.title);
    };
    // ---- On foot ----
    Walker walker;
    Interact interact;  // on foot: the hold-E jobs (the spare, the wheels)
    bool invOpen = false;  // on foot at the open back: the list of what's in the truck (TAB / I)
    double walkSend = 0, carCamDist = 7.5;
    // On foot you see through your own eyes: the mouse turns your head (no button needed)
    // and the keys walk where you look.
    double footYaw = 0, footPitch = 0;
    bool scriptE = false;  // (--script work-*: E held)
    bool mouseLook = false;
    auto toggleInventory = [&]() {
        if (invOpen) {
            invOpen = false;
            return;
        }
        if (!trunk::kitInReach(sim, walker.p)) hud.toast("OPEN THE BACK OF YOUR TRUCK (HOLD E AT IT) TO SEE WHAT'S IN IT");
        else invOpen = true;
    };
    auto getOut = [&]() {
        Car& c = sim.cars[sim.player];
        if (len(c.frame().vel) > 2.0) {
            hud.toast("STOP FIRST");
            return;
        }
        stopPilot();
        c.handbrake = 1;
        walker.exitCar(sim.world, c);
        carCamDist = cam.dist;
        cam.initialized = false;
        footYaw = walker.yaw;  // looking the way you got out
        footPitch = -0.1;
        hud.toast("ON FOOT: MOUSE LOOK, WASD WALK, SHIFT RUN, SPACE JUMP, V HOOK YOUR WINCH HERE, J GET IN");
    };
    auto getIn = [&]() {
        walker.hand = TrunkRef();  // (a tool in your hand goes back in the truck with you)
        invOpen = false;
        if (walker.carrying) Interact::drop(sim, walker, V3(std::cos(walker.yaw), 0, -std::sin(walker.yaw)));  // (whisked away: it's left there)
        else if (walker.holdingJack) Interact::dropJack(sim, walker);  // (put the jack down)
        const int k = walker.carToEnter(sim);
        if (walker.holdingChain && k >= 0) {  // (a chain in your hands goes in the back with you)
            Car& kc = sim.cars[k];
            if (kc.chains < std::max(kc.spec->snowChains, 4)) kc.chains++;
            walker.holdingChain = false;
        }
        if (k < 0) {
            hud.toast("WALK UP TO A TRUCK TO GET IN");
            return;
        }
        sim.player = k;
        sim.cars[k].handbrake = 0;
        walker.active = false;
        cam.dist = carCamDist;
        cam.initialized = false;
        net.sendGame("inside");
        hud.toast(std::string("IN THE ") + sim.cars[k].spec->name);
    };
    // ---- The world editor (offline) ----
    Editor editor;
    Level lastEdited;
    bool haveEdited = false;
    auto openEditor = [&](const Level& base, bool newSection = false) {
        if (net.active()) {
            hud.toast("THE EDITOR IS OFFLINE ONLY: LEAVE THE SESSION FIRST");
            return;
        }
        run.stop();
        levelHours = -1;
        stopPilot();
        if (newSection) editor.newSection(sim.world, renderer);  // (a fresh proving ground, the canvas cleared)
        else {
            sim.world = World(base.map);
            editor.open(sim.world, renderer, base);
        }
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
            case PauseMenu::CHANGED:
                applySettings();
                saveSettings();
                if (settings.career != career.on) {  // career <-> sandbox: each has its own garage
                    career.on = settings.career;
                    garagePath = career.on ? configDir() + "/garage-career.txt" : Garage::defaultPath();
                    garage = Garage();
                    garage.load(garagePath);
                    const Car& pc = sim.cars[sim.player];
                    if (!pc.isProp() && pc.spec->model >= 0) sim.swapPlayerVehicle(garage.spec(pc.spec->model));
                    cachesMap = -1;  // (the crates go out, or stay out of the way)
                    if (!career.on) sim.anchorStock = AnchorStock();
                    hud.toast(career.on ? "CAREER: FACTORY PARTS. FIND, WIN AND BUY THE REST" : "SANDBOX: EVERY PART ON EVERY VEHICLE");
                }
                break;
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
                saveSettings();
                if (menu.mapPick != sim.world.map) {  // (the HOST card's map)
                    run.stop();
                    levelHours = -1;
                    loadMap(menu.mapPick);
                    settings.map = menu.mapPick;
                    saveSettings();
                }
                closeMenu();
                startHost(net::kDefaultPort);
                if (run.active() && net.role == NetSession::HOST) net.setLevel(saveLevelText(run.level));  // they join into it
                break;
            case PauseMenu::JOIN:  // (a game from the network list, or the typed address)
                saveSettings();
                beginJoin(menu.joinPick.empty() ? settings.joinAddress : menu.joinPick, menu.joinLabel);
                menu.joinPick.clear();
                break;
            case PauseMenu::CANCEL_JOIN:
                if (joinState == 1) net.close();
                joinState = 0;
                menu.joining = false;
                menu.joinError.clear();
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
                levelHours = -1;
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
            case PauseMenu::NEW_SECTION: {
                closeMenu();
                openEditor(Level(), true);
                break;
            }
            case PauseMenu::MODEL_LAB: {  // the model lab (lab.h), then back here with the picks drawn
                SDL_SetRelativeMouseMode(0);
                LabOptions lo;
                runModelLab(win, renderer, lo);
                renderer.setWorld(sim.world);
                drawnWorld = sim.world.stamp;
                break;
            }
            case PauseMenu::EDITOR: {
                const int m = menu.title ? menu.mapPick : sim.world.map;
                closeMenu();
                Level l;
                l.map = m;
                l.name = "UNTITLED";
                openEditor(l);
                break;
            }
            case PauseMenu::MEET_UP: {
                // Your truck parked just behind the next player's (repaired, on the ground).
                static int lastMet = -1;
                closeMenu();
                int pick = -1;
                for (int k = 1; k <= net::kMaxPeers && pick < 0; k++) {
                    const int pid = (lastMet + k + net::kMaxPeers) % net::kMaxPeers;
                    if (pid != net.self && net.players[pid].active) pick = pid;
                }
                const Car* theirs = nullptr;
                for (const Car& c : sim.cars)
                    if (c.remote && !c.isProp() && NetSession::ownerOf(c.netId) == pick) theirs = &c;
                if (pick < 0 || !theirs) {
                    hud.toast("NOBODY TO MEET UP WITH");
                    break;
                }
                lastMet = pick;
                const Frame f = theirs->frame();
                const V3 back = norm(V3(f.fwd.x, 0, f.fwd.z));
                const V3 at = f.com - back * (theirs->spec->length / 2 + 6);
                if (walker.active) getIn();
                Car& me = sim.cars[sim.player];
                sim.dropRopes(sim.player);
                me.respawn(V3(at.x, sim.world.clearY(at.x, at.z) + 0.1, at.z), std::atan2(-back.z, back.x));
                me.setDown(sim.world);
                cam.initialized = false;
                hud.toast("MET UP WITH " + net.players[pick].name);
                break;
            }
            case PauseMenu::JOB_START:
                if (res.vehicle < 0 || res.vehicle >= (int)jobList.size()) break;
                if (net.role == NetSession::CLIENT) {
                    hud.toast("THE HOST PICKS THE JOB");
                    break;
                }
                closeMenu();
                startJob(jobList[res.vehicle]);
                break;
            case PauseMenu::JOB_STOP:
                closeMenu();
                jobOn = false;
                if (net.role == NetSession::HOST) net.sendGame("jobstop");
                hud.toast("JOB ABANDONED");
                break;
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
                levelHours = -1;
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
                levelHours = -1;
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
    } else if (opt.menu >= 1) {  // (1: the settings page)
        openMenu();
        menu.showPage(opt.menu, std::max(0, sim.cars[sim.player].spec->model), sim.world.map);
    } else if (opt.menu >= 0) {
        openMenu();
    }
    if (opt.hostPort > 0) startHost(opt.hostPort);
    else if (!opt.join.empty()) startJoin(opt.join);
    // Launched plainly (no map, spawn, session or test asked for): the title screen first.
    if (opt.newSection) {
        openEditor(Level(), true);
    } else if (opt.editor) {
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
    double benchDraw = 0;
    size_t spawnType = 0, trafficType = 0;

    while (running) {
        uint64_t now = SDL_GetPerformanceCounter();
        const double frameTimeMs = 1000.0 * (double)(now - last) / freq;
        double dt = scripted ? 1.0 / 60.0 : std::min(0.1, frameTimeMs / 1000.0);
        last = now;
        if (dt > 0) fps += ((float)(1.0 / dt) - fps) * 0.05f;
        hud.newBuild = watch.poll();
        if (hud.newBuild && std::getenv("CZ_AUTORELOAD")) reload = true, running = false;  // (testing the reload)

        // ---- gamepad: what the pad is doing this frame (its presses are fed in as keys below) ----
        padCtx = editor.active || coop.typing || playback.active() || !padDev.connected() ? pad::Mode::Off
                 : menu.open || garageUI.open                                             ? pad::Mode::Menu
                 : coop.mapOpen                                                           ? pad::Mode::Map
                 : winchAim.on                                                            ? pad::Mode::Aim
                                                                                          : pad::Mode::Drive;
        padLogic.tune = {settings.padSteering, settings.padDeadzone, settings.padRumble};
        padLogic.update(padDev.read((SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) != 0), padCtx, dt);
        size_t padTap = 0;
        bool fromPad = false;
        auto nextEvent = [&](SDL_Event& ev) {
            fromPad = false;
            padShift = false;
            if (padTap < padLogic.taps.size()) {
                const pad::Tap& t = padLogic.taps[padTap++];
                std::memset(&ev, 0, sizeof ev);
                ev.type = SDL_KEYDOWN;
                std::memcpy(ev.padding + 16, &t.scancode, 4);
                padShift = t.shift;
                fromPad = true;
                return true;
            }
            return SDL_PollEvent(&ev) != 0;
        };

        if (!opt.press.empty()) {  // (testing: real key events through SDL's queue, a press and its release in one frame)
            static bool menuWas = false;
            if (menu.open != menuWas) std::printf("press-test: frame %d menu %s\n", frame, menu.open ? "OPEN" : "CLOSED");
            menuWas = menu.open;
            const auto it = opt.press.find(frame);
            if (it != opt.press.end())
                for (int sc : it->second)
                    for (int down = 1; down >= 0; down--) {
                        SDL_Event ke;
                        std::memset(&ke, 0, sizeof ke);
                        ke.type = down ? SDL_KEYDOWN : SDL_KEYUP;
                        ke.padding[12] = (uint8_t)down;
                        std::memcpy(ke.padding + 16, &sc, 4);
                        SDL_PushEvent(&ke);
                    }
        }
        SDL_Event e;
        while (nextEvent(e)) {
            {  // ---- gamepad: hot-plug and buttons; the keyboard and mouse take the HUD hints back ----
                std::string padToast;
                if (!fromPad && padDev.handleEvent(e, padToast)) {
                    if (!padToast.empty()) hud.toast(padToast), std::fprintf(stderr, "crumplezone: %s\n", padToast.c_str());
                    menu.padName = padDev.name();
                    continue;
                }
                if (!fromPad && (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL)) pad::ui.last = false;
            }
            if (!editor.active && !menu.open && coop.typing && e.type == SDL_KEYDOWN) {
                coop.typeKey(evKeyScancode(e), shiftDown(), myName(), net);
                continue;
            }
            if (!editor.active && !menu.open && coop.mapOpen && e.type != SDL_QUIT) {
                // The map has the mouse (and Esc / M close it).
                float mx, my;
                if (e.type == SDL_KEYDOWN) {
                    const int sc = evKeyScancode(e);
                    if ((sc == SC_ESCAPE || (sc == SC_M && !shiftDown())) && !evKeyRepeat(e)) {
                        coop.mapOpen = false;
                        continue;
                    }
                } else if (e.type == SDL_MOUSEWHEEL) {
                    coop.mapMouse(mouseX, mouseY, evWheelY(e), false, 0, 0);
                    continue;
                } else if (e.type == SDL_MOUSEMOTION) {
                    toDrawable(evMotionX(e), evMotionY(e), mx, my);
                    const float dx = mx - mouseX, dy = my - mouseY;
                    mouseX = mx, mouseY = my;
                    if (evMotionState(e) & 1) {
                        if (std::fabs(dx) + std::fabs(dy) > 0) mapDrag = true;
                        coop.mapMouse(mx, my, 0, true, dx, dy);
                    }
                    continue;
                } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                    toDrawable(evButtonX(e), evButtonY(e), mx, my);
                    mouseX = mx, mouseY = my;
                    if (evButton(e) == 1) mapDrag = false;
                    V3 p;
                    if (evButton(e) == 3 && coop.mapPoint(mx, my, p)) {
                        p.y = sim.world.height(p.x, p.z);
                        coop.ping(shiftDown() ? 1 : 0, p, myName(), net);
                    }
                    continue;
                } else if (e.type == SDL_MOUSEBUTTONUP) {
                    toDrawable(evButtonX(e), evButtonY(e), mx, my);
                    V3 p;
                    if (evButton(e) == 1 && !mapDrag && coop.mapPoint(mx, my, p)) {
                        p.y = sim.world.height(p.x, p.z);
                        coop.haveWaypoint = !(coop.haveWaypoint && len(V3(p.x - coop.waypoint.x, 0, p.z - coop.waypoint.z)) < 15);
                        coop.waypoint = p;
                        hud.toast(coop.haveWaypoint ? "WAYPOINT SET" : "WAYPOINT CLEARED");
                    }
                    mapDrag = false;
                    continue;
                }
            }
            if (e.type == SDL_MOUSEMOTION) toDrawable(evMotionX(e), evMotionY(e), mouseX, mouseY);
            if (!editor.active && !menu.open && e.type == SDL_MOUSEBUTTONDOWN && evButton(e) == 2) {
                // Middle click: ping where the mouse points (Shift: HELP, at your truck).
                toDrawable(evButtonX(e), evButtonY(e), mouseX, mouseY);
                V3 p;
                if (shiftDown()) coop.ping(1, sim.cars[sim.player].frame().com, myName(), net);
                else if (groundUnder(mouseX, mouseY, p)) coop.ping(0, p, myName(), net);
                continue;
            }
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
                    // The key as the in-game switch knows it: the player's keyboard bindings turned back into
                    // the default keys the switch is written against (a gamepad press already is one).
                    const int gsc = settings.keys.gameKey(evKeyScancode(e), fromPad);
                    if (garageUI.open) {
                        const auto sc = evKeyScancode(e);
                        int& pk = garageUI.pick.pick[garageUI.slot];
                        const int n = (int)modOptions(garageUI.vehicle, garageUI.slot).size();
                        if (sc == SC_W || sc == SC_UP) garageUI.slot = (garageUI.slot + MOD_SLOTS - 1) % MOD_SLOTS;
                        else if (sc == SC_S || sc == SC_DOWN) garageUI.slot = (garageUI.slot + 1) % MOD_SLOTS;
                        else if (sc == SC_A || sc == SC_LEFT) pk = (pk + n - 1) % n;
                        else if (sc == SC_D || sc == SC_RIGHT) pk = (pk + 1) % n;
                        else if (sc == SC_B && career.on) {  // buy the part shown, at a station
                            const int at = sim.stationAt(sim.cars[sim.player].frame().com);
                            std::string why;
                            if (at < 0) hud.toast("PARTS ARE SOLD AT THE REPAIR STATIONS");
                            else if (career.buy(garageUI.vehicle, garageUI.slot, pk, why)) {
                                if (!scripted) career.save(Career::defaultPath());
                                hud.toast("BOUGHT " + modOptions(garageUI.vehicle, garageUI.slot)[pk].name + ": $" + std::to_string(career.cash) + " LEFT");
                            } else hud.toast(why);
                        } else if ((sc == SC_RETURN || sc == SC_KP_ENTER) && career.on && sim.stationAt(sim.cars[sim.player].frame().com) < 0 &&
                                   !(garageUI.pick.pick[0] == garage.fitted[garageUI.vehicle].pick[0] && garageUI.pick.pick[1] == garage.fitted[garageUI.vehicle].pick[1] &&
                                     garageUI.pick.pick[2] == garage.fitted[garageUI.vehicle].pick[2] && garageUI.pick.pick[3] == garage.fitted[garageUI.vehicle].pick[3] &&
                                     garageUI.pick.pick[4] == garage.fitted[garageUI.vehicle].pick[4])) {
                            hud.toast("DRIVE TO A REPAIR STATION TO FIT PARTS");
                        } else if ((sc == SC_RETURN || sc == SC_KP_ENTER) && career.on &&
                                   [&] {
                                       for (int k = 0; k < MOD_SLOTS; k++)
                                           if (!career.owns(garageUI.vehicle, k, garageUI.pick.pick[k])) return true;
                                       return false;
                                   }()) {
                            int k = 0;
                            while (k < MOD_SLOTS && career.owns(garageUI.vehicle, k, garageUI.pick.pick[k])) k++;
                            hud.toast(std::string("LOCKED: ") + modOptions(garageUI.vehicle, k)[garageUI.pick.pick[k]].name);
                        } else if (sc == SC_RETURN || sc == SC_KP_ENTER) {
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
                    if (winchAim.on) {  // recovery picker owns keyboard confirmation/cancel
                        const int sc = gsc;
                        if (sc == SC_BACKSPACE && winchAim.kind == ROPE_STRAP && winchAim.sourceHook >= 0) {
                            winchAim.sourceHook = -1;
                            winchAim.update(sim, lastView);
                            break;
                        }
                        if (sc == SC_ESCAPE || sc == SC_BACKSPACE) {
                            winchAim.on = false;
                            break;
                        }
                        if (fromPad) winchAim.pointer = false;
                        if (sc == SC_V || sc == SC_O || sc == SC_RETURN || sc == SC_KP_ENTER) confirmWinch();
                        break;
                    }
                    if ((evKeyScancode(e) == SC_RETURN || evKeyScancode(e) == SC_KP_ENTER) && !winchAim.on) {
                        coop.typing = true;
                        coop.draft.clear();
                        break;
                    }
                    switch (gsc) {
                        case SC_ESCAPE:  // (the pause menu: Esc on the keyboard, Start on the pad, the MENU key if rebound)
                            if (hud.showHelp) hud.showHelp = false;
                            else openMenu();
                            break;
                        case SC_F4:  // steering assist (rebindable)
                            sim.steeringAssist = !sim.steeringAssist;
                            settings.steeringAssist = sim.steeringAssist;
                            saveSettings();
                            hud.toast(sim.steeringAssist ? "STEERING ASSIST ON (LIMITED AT HIGH SPEED)"
                                                         : "STEERING ASSIST OFF (FULL LOCK AT ALL SPEEDS)");
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
                            settings.winchSpeed = recovery::adjustWinchSpeed(settings.winchSpeed, gsc == SC_EQUALS ? 1 : -1);
                            for (Car& car : sim.cars) if (!car.remote) car.winchSpeed = settings.winchSpeed;
                            char t[64];
                            std::snprintf(t, sizeof t, "WINCH SPEED %.2gX (UP TO %.2f M/S)", c.winchSpeed, recovery::kWinchSpeed * c.winchSpeed);
                            hud.toast(t); saveSettings();
                            break;
                        }
                        case SC_V: {
                            const Car& c = sim.cars[sim.player];
                            if (sim.dropWinch(sim.player)) hud.toast("WINCH LINE UNHOOKED AND SPOOLED");
                            else if (c.isProp() || !(c.spec->extras & EXTRA_WINCH)) hud.toast("NO WINCH ON THIS TRUCK");
                            else startRecoveryPick(ROPE_WINCH);
                            break;
                        }
                        case SC_O: {
                            if (sim.dropStrap(sim.player)) hud.toast("TOW STRAP UNHOOKED");
                            else if (!sim.cars[sim.player].isProp()) startRecoveryPick(ROPE_STRAP);
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
                            if (walker.active) {  // (on foot: the truck's inventory)
                                toggleInventory();
                                break;
                            }
                            sim.player = sim.nextVehicle(sim.player);
                            sim.cars[sim.player].wake();
                            hud.toast(std::string("DRIVING ") + sim.cars[sim.player].spec->name);
                            break;
                        case SC_C: {
                            static const char* names[] = {"CAMERA: CHASE", "CAMERA: ORBIT", "CAMERA: COCKPIT",
                                                          "CAMERA: DRONE (WASD FLY, Q E DOWN UP, DRAG LOOK, SHIFT FAST; F10 HIDES THE HUD)"};
                            cam.mode = (cam.mode + 1) % 4;
                            if (cam.mode == 3) {  // the drone takes off from where the camera was
                                cam.droneEye = lastView.eye;
                                const V3 d = norm(lastView.target - lastView.eye);
                                cam.droneYaw = std::atan2(-d.z, d.x);
                                cam.dronePitch = std::asin(clampd(d.y, -1, 1));
                            }
                            hud.toast(names[cam.mode]);
                            break;
                        }
                        case SC_F10:
                            hud.photo = !hud.photo;
                            break;
                        case SC_N: {
                            static const char* names[] = {"BEAM VIEW OFF", "BEAM VIEW: X-RAY", "BEAM VIEW: BEAMS ONLY", "BEAM VIEW: PARTS"};
                            debug = (DebugView)((debug + 1) % DEBUG_VIEW_COUNT);
                            hud.toast(names[debug]);
                            break;
                        }
                        case SC_H: hud.showHelp = !hud.showHelp; break;
                        case SC_F2:
                            openEditor(editorKeyLevel());
                            break;
                        case SC_J:
                            if (playback.active()) {  // (a scenario: J opens its checks list)
                                hud.scenarioDetail = !hud.scenarioDetail;
                                break;
                            }
                            if (walker.active && walker.carrying) hud.toast("PUT THE WHEEL DOWN FIRST (G)");
                            else if (walker.active) getIn();
                            else getOut();
                            break;
                        case SC_F3:
                            settings.showFps = hud.showFps = !hud.showFps;
                            saveSettings();
                            break;
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
                            hud.toast(!c.spec->tractionFitted ? "NO TRACTION CONTROL ON THIS CAR"
                                      : c.tractionControl ? "TRACTION CONTROL ON" : "TRACTION CONTROL OFF");
                            break;
                        }
                        case SC_1:
                        case SC_2:
                        case SC_3:
                        case SC_4:
                        case SC_5:
                        case SC_6:
                        case SC_7: {
                            const size_t k = gsc - SC_1;  // (the listed vehicles, in order: 1 the Blazer)
                            if (k < listedVehicles().size()) {
                                const int idx = listedVehicles()[k];
                                sim.swapPlayerVehicle(garage.spec(idx));
                                hud.toast(std::string("NOW DRIVING ") + vehicleCatalog()[idx].name);
                            }
                            break;
                        }
                        case SC_G:
                            if (walker.active) {
                                if (walker.carrying) Interact::drop(sim, walker, lastView.target - lastView.eye);  // (on foot: put down what you carry)
                                else if (walker.holdingJack) Interact::dropJack(sim, walker);  // (put the jack down)
                                else if (walker.hand.any()) hud.toast("PUT IT BACK IN THE TRUCK: HOLD E AT THE OPEN BACK");
                            }
                            else openGarage();
                            break;
                        case SC_E:
                        case SC_Q: {
                            if (cam.mode == 3 || walker.active) break;  // (flying the drone / on foot: Q E aren't the gears)
                            Car& c = sim.cars[sim.player];
                            if (c.shift(gsc == SC_E ? 1 : -1) && !c.manualShift) {
                                c.manualShift = true;  // taking over the gears switches to manual
                                hud.toast("MANUAL SHIFTING (SHIFT+M FOR AUTOMATIC)");
                            }
                            break;
                        }
                        case SC_M: {
                            if (!shiftDown()) {  // M: the map (Shift+M: the gearbox)
                                coop.mapOpen = !coop.mapOpen;
                                break;
                            }
                            Car& c = sim.cars[sim.player];
                            c.manualShift = !c.manualShift;
                            if (!c.manualShift && c.gear == 0) c.gear = 1;
                            hud.toast(c.manualShift ? "MANUAL: Q/E SELECT R N 1... W GAS, S BRAKE" : "AUTOMATIC SHIFTING");
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
                        case SC_B: {  // lockers (on foot, F: the fuel can)
                            if (walker.active && gsc == SC_F) {
                                {  // pour your truck's spare can into the nearest vehicle
                                    Car& mine = sim.cars[sim.player];
                                    if (mine.jerry <= 0.5) {
                                        hud.toast(mine.spec->jerryCan > 0 ? "YOUR SPARE CAN IS EMPTY (FILL IT AT A SHOP)" : "YOUR VEHICLE HAS NO SPARE CAN");
                                        break;
                                    }
                                    int best = -1;
                                    double bd = 3.5;
                                    for (int i = 0; i < (int)sim.cars.size(); i++) {
                                        const Car& o = sim.cars[i];
                                        if (o.isProp()) continue;
                                        const Frame of = o.frame();
                                        const V3 d = walker.p + V3(0, 1, 0) - of.com;
                                        const double dd = std::hypot(std::max(0.0, std::fabs(dot(d, of.fwd)) - o.spec->length / 2), std::max(0.0, std::fabs(dot(d, of.left)) - o.spec->width / 2));
                                        if (dd < bd) bd = dd, best = i;
                                    }
                                    if (best < 0) {
                                        hud.toast("WALK UP TO THE TRUCK THAT NEEDS FUEL");
                                        break;
                                    }
                                    Car& t = sim.cars[best];
                                    const double liters = mine.jerry;
                                    mine.jerry = 0;
                                    if (t.remote) {
                                        char b[64];
                                        std::snprintf(b, sizeof b, "fuel %u %.1f", t.netId, liters);
                                        net.sendGame(b);
                                        hud.toast("POURED " + std::to_string((int)liters) + " L INTO " + net.ownerName(t.netId) + "'S TANK");
                                    } else {
                                        if (t.fuel < 0) t.fuel = t.spec->fuelCapacity;
                                        t.fuel = std::min(t.spec->fuelCapacity, t.fuel + liters);
                                        hud.toast("POURED " + std::to_string((int)liters) + " L INTO THE " + t.spec->name);
                                    }
                                    break;
                                }
                            }
                            Car& c = sim.cars[sim.player];
                            const bool front = gsc == SC_F;
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
                            if (walker.active) {
                                toggleInventory();
                                break;
                            }
                            hud.toast(sim.spawnAhead(garage.spec(listedVehicles()[spawnType]))
                                          ? std::string("SPAWNED ") + vehicleCatalog()[listedVehicles()[spawnType]].name
                                          : std::string("VEHICLE LIMIT REACHED"));
                            spawnType = (spawnType + 1) % listedVehicles().size();
                            break;
                        case SC_K:
                            hud.toast(sim.spawnTraffic(garage.spec(listedVehicles()[trafficType]))
                                          ? std::string("AI ") + vehicleCatalog()[listedVehicles()[trafficType]].name + " ON THE LOOP ROAD"
                                          : std::string("VEHICLE LIMIT REACHED"));
                            trafficType = (trafficType + 1) % listedVehicles().size();
                            break;
                        case SC_U: {  // autopilot on (to the place picked in the menu) or off
                            Car& c = sim.cars[sim.player];
                            if (c.pilot.active || c.ai) stopPilot();
                            else startPilot();
                            break;
                        }
                        case SC_DELETE:
                        case SC_BACKSPACE:
                            hud.toast(sim.removeCar(sim.player) ? "VEHICLE REMOVED" : "KEEP ONE LOCAL VEHICLE");
                            break;
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
                    } else if (winchAim.on) {
                        if (evMotionState(e) & 2) {
                            if (walker.active) {
                                footYaw -= evMotionXrel(e) * 0.0035;
                                footPitch = clampd(footPitch - evMotionYrel(e) * 0.0035, -1.45, 1.45);
                            } else cam.drag(evMotionXrel(e), evMotionYrel(e));
                        }
                        else { toDrawable(evMotionX(e), evMotionY(e), winchAim.pointerX, winchAim.pointerY); winchAim.pointer = true; }
                    } else if (walker.active && !editor.active && !winchAim.on) {  // on foot: the mouse turns your head
                        footYaw -= evMotionXrel(e) * 0.0035;
                        footPitch = clampd(footPitch - evMotionYrel(e) * 0.0035, -1.45, 1.45);
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
                        toDrawable(evButtonX(e), evButtonY(e), winchAim.pointerX, winchAim.pointerY);
                        winchAim.pointer = true;
                        confirmWinch();
                    }
                    dragging = false;
                    menu.mouseUp();
                    break;
                case SDL_MOUSEWHEEL:
                    if (!menu.open) cam.dist = clampd(cam.dist * std::pow(0.88, evWheelY(e)), 3.0, 60.0);
                    break;
            }
        }

        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        const Keybinds& kb = settings.keys;
        bool up = keys[kb.key(KA_THROTTLE)] || keys[SC_UP], down = keys[kb.key(KA_BRAKE)] || keys[SC_DOWN];
        bool left = keys[kb.key(KA_STEER_LEFT)] || keys[SC_LEFT], right = keys[kb.key(KA_STEER_RIGHT)] || keys[SC_RIGHT];
        bool hb = keys[kb.key(KA_HANDBRAKE)];
        const bool shiftHeld = keys[SC_LSHIFT] || keys[SC_RSHIFT];
        int winch = keys[SC_Z] ? (shiftHeld ? -1 : 1) : 0;
        int airing = keys[SC_9] ? -1 : keys[SC_0] ? 1 : 0;
        if (cam.mode == 3 && !menu.open && !editor.active && !coop.typing && !coop.mapOpen) {
            // The drone: the keys fly the camera; the truck waits on its handbrake.
            const double speed = (8 + std::max(0.0, cam.droneEye.y - sim.world.height(cam.droneEye.x, cam.droneEye.z)) * 0.6) * (shiftHeld ? 3 : 1);
            const V3 f(std::cos(cam.droneYaw), 0, -std::sin(cam.droneYaw)), l(-f.z, 0, f.x);
            V3 m;
            if (up) m += f;
            if (down) m -= f;
            if (left) m += l;
            if (right) m -= l;
            if (keys[SC_E]) m.y += 1;
            if (keys[SC_Q]) m.y -= 1;
            if (padCtx == pad::Mode::Drive) m += f * (double)padLogic.in.walkForward + l * (double)padLogic.in.walkTurn;  // (gamepad: left stick flies)
            if (len(m) > 0) cam.droneEye += norm(m) * (speed * dt);
            cam.droneEye.y = std::max(cam.droneEye.y, sim.world.height(cam.droneEye.x, cam.droneEye.z) + 0.5);
            up = down = left = right = false;
            hb = true;
        }
        if (garageUI.open || menu.open || editor.active || coop.typing || coop.mapOpen) {  // parked while you work on it
            up = down = left = right = false;
            hb = true;
            winch = airing = 0;
        }
        // ---- gamepad: the held controls (the steering and pedals go on the car after Sim::drive) ----
        const bool padDrives = padCtx == pad::Mode::Drive && !walker.active && cam.mode != 3;
        if (padCtx == pad::Mode::Drive) {
            const pad::Intent& pin = padLogic.in;
            hb = hb || pin.handbrake;
            if (pin.winch) winch = pin.winch;
            if (pin.airing) airing = pin.airing;
            if (walker.active) {  // on foot: the left stick walks, A jumps
                up = up || pin.walkForward > 0.4f, down = down || pin.walkForward < -0.4f;
                left = left || pin.walkTurn > 0.4f, right = right || pin.walkTurn < -0.4f;
            } else if (cam.mode != 3 && sim.player >= 0 && sim.player < (int)sim.cars.size() && sim.cars[sim.player].pilot.active) {
                up = up || pin.throttle > 0.3, down = down || pin.brake > 0.3;  // (taking the wheel switches the autopilot off)
                left = left || pin.steerTarget > 0.3, right = right || pin.steerTarget < -0.3;
            }
        }
        if (!playback.active() && sim.player >= 0 && sim.player < (int)sim.cars.size()) {  // (a scenario drives its own cars)
            for (Car& c : sim.cars) { c.airing = 0; if (!c.remote) c.winchSpeed = settings.winchSpeed; }
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
                startRecoveryPick(ROPE_WINCH);
            }
            if (opt.script.rfind("work-", 0) == 0) {  // screenshots of hands-on work: a wheel off, a chain on, a CV axle in
                Car& c = sim.cars[sim.player];
                const int k = std::getenv("CZ_WORK_WHEEL") ? std::atoi(std::getenv("CZ_WORK_WHEEL")) : opt.script == "work-chain" ? 0 : 2;  // (CZ_WORK_WHEEL: which corner)
                if (frame == 20) c.handbrake = 1, getOut();
                if (frame == 30 && k < (int)c.wheels.size()) {
                    const Wheel& wh = c.wheels[k];
                    const V3 hub = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5, out = norm(c.nodes[wh.outer].p - c.nodes[wh.inner].p);
                    if (opt.script == "work-chain") walker.holdingChain = true;
                    else if (std::getenv("CZ_WORK_PLACE")) walker.holdingJack = true;  // (the jack still in your arms: the placing job)
                    else c.placeJack(sim.world, k), c.wake(), c.jackWorked = true, c.jackHeight = std::getenv("CZ_JACK_H") ? std::atof(std::getenv("CZ_JACK_H")) : 0.12;
                    if (opt.script == "work-fix") c.wheels[k].shaftBroken = true;
                    const V3 aim = opt.script == "work-fix" ? hub + (c.frame().com - hub) * 0.25 : std::getenv("CZ_WORK_PLACE") || opt.script == "work-jack" ? Interact::jackAim(sim, c, k) : hub;
                    walker.p = hub + out * 1.0 - c.frame().fwd * 0.3;
                    walker.p.y = sim.world.height(walker.p.x, walker.p.z);
                    const V3 d = norm(aim - (walker.p + V3(0, 1.62, 0)));
                    footYaw = std::atan2(-d.z, d.x), footPitch = std::asin(d.y);
                }
                scriptE = frame > 70;
            }
            if (opt.script == "damage") {  // screenshots of broken parts: CZ_BREAK=cv:fl,tie:fr,bend:fl,shaft:f,spring:rl,leaf:rr,flood (corner = front/rear + left/right),
                // CZ_STAND=fwd,left (m from the car's middle, default 2.6,1.9) and CZ_LOOKAT=fwd,left,up (m, default the front axle at hub height)
                Car& c = sim.cars[sim.player];
                const Frame fr0 = c.frame();
                const V3 fl = norm(V3(fr0.fwd.x, 0, fr0.fwd.z)), lf = norm(V3(fr0.left.x, 0, fr0.left.z));
                const bool orbit = std::getenv("CZ_ORBIT") != nullptr;  // (the breaking only: look with --orbit)
                if (frame == 20) c.handbrake = 1;
                if (frame == 20 && !orbit) getOut();
                if (frame == 24) {
                    auto corner = [&](const std::string& t) {
                        for (int k = 0; k < (int)c.wheels.size(); k++)
                            if (c.wheels[k].front == (t[0] == 'f') && c.wheels[k].left == (t[1] == 'l')) return k;
                        return -1;
                    };
                    std::string list = std::getenv("CZ_BREAK") ? std::getenv("CZ_BREAK") : "cv:fl";
                    for (size_t at = 0; at < list.size();) {
                        size_t e = list.find(',', at);
                        if (e == std::string::npos) e = list.size();
                        const std::string it = list.substr(at, e - at);
                        at = e + 1;
                        const size_t colon = it.find(':');
                        const std::string what = it.substr(0, colon), arg = colon == std::string::npos ? "" : it.substr(colon + 1);
                        const int k = arg.size() >= 2 ? corner(arg) : -1;
                        if (what == "flood") c.hydrolocked = true;
                        else if (what == "dead") c.engineDead = true;
                        else if (what == "shaft") {
                            for (Axle& ax : c.axles)
                                if (ax.propShaft >= 0 && ax.front == (arg == "f")) c.beams[ax.propShaft].broken = true;
                        } else if (k >= 0) {
                            Wheel& wh = c.wheels[k];
                            if (what == "cv") {
                                wh.shaftBroken = true;
                                for (int bi : wh.shafts) c.beams[bi].broken = true;
                            } else if (what == "tie") wh.tieOffset = 0.05;
                            else if (what == "tiebreak" && wh.tieRod >= 0) c.beams[wh.tieRod].broken = true;
                            else if (what == "spring" && wh.spring >= 0) c.beams[wh.spring].broken = true;
                            else
                                for (const SuspLink& l : c.links) {
                                    if (l.wheel != k) continue;
                                    Beam& b = c.beams[l.beam];
                                    if (what == "bend" && (l.kind == LINK_LOWER_ARM || l.kind == LINK_TRAILING)) b.L0 = b.Lorig * 0.95;
                                    if (what == "armbreak" && (l.kind == LINK_LOWER_ARM || l.kind == LINK_TRAILING)) b.broken = true;
                                    if (what == "leaf" && l.kind == LINK_LEAF) b.broken = true;
                                }
                        }
                    }
                    double st[3] = {2.6, 1.9, 1.0}, la[3] = {c.spec->wheelbase * 0.5, 0.8, 0.3};
                    if (const char* e = std::getenv("CZ_STAND")) std::sscanf(e, "%lf,%lf,%lf", st, st + 1, st + 2);  // (the 3rd, the drone camera's height, with CZ_ORBIT)
                    if (const char* e = std::getenv("CZ_LOOKAT")) std::sscanf(e, "%lf,%lf,%lf", la, la + 1, la + 2);
                    if (orbit) {  // the drone camera there, looking at the same spot
                        const V3 e = fr0.com + fl * st[0] + lf * st[1];
                        const V3 at = fr0.com + fl * la[0] + lf * la[1];
                        const V3 eye(e.x, sim.world.height(e.x, e.z) + st[2], e.z);
                        const V3 d = norm(V3(at.x, sim.world.height(at.x, at.z) + la[2], at.z) - eye);
                        cam.mode = 3, cam.droneEye = eye, cam.droneYaw = std::atan2(-d.z, d.x), cam.dronePitch = std::asin(d.y);
                    } else {
                        walker.p = fr0.com + fl * st[0] + lf * st[1];
                        walker.p.y = sim.world.height(walker.p.x, walker.p.z);
                        walker.v = V3();
                        const V3 at = fr0.com + fl * la[0] + lf * la[1];
                        const V3 d = norm(V3(at.x, sim.world.height(at.x, at.z) + la[2], at.z) - (walker.p + V3(0, 1.62, 0)));
                        footYaw = std::atan2(-d.z, d.x), footPitch = std::asin(d.y);
                    }
                }
            }
            if (opt.script == "trunk") {  // screenshots of the cargo: the gate, what rides in the back, taking things out, the list
                Car& c = sim.cars[sim.player];
                const Frame fr = c.frame();
                const V3 fl = norm(V3(fr.fwd.x, 0, fr.fwd.z));
                auto stand = [&](double behind) {  // behind the truck, centred
                    walker.p = fr.com - fl * (c.spec->length * 0.5 + behind);
                    walker.p.y = sim.world.height(walker.p.x, walker.p.z);
                    walker.v = V3();
                };
                auto look = [&](const V3& at) {
                    const V3 d = norm(at - (walker.p + V3(0, 1.62, 0)));
                    footYaw = std::atan2(-d.z, d.x), footPitch = std::asin(d.y);
                };
                auto itemAim = [&](TrunkRef r, V3& at) {  // where an item of the back lies
                    const TrunkStock st{career.kit(), sim.anchorStock};
                    const std::vector<TrunkItem> items = trunk::wholeLoad(c, st, true, walker.hand, walker.holdingJack, walker.holdingChain);
                    const std::vector<TrunkSlot> slots = trunk::layout(c, items);
                    for (int i = 0; i < (int)items.size(); i++)
                        if (items[i].ref == r) return trunk::aimAt(c, fr, items, slots, i, at);
                    return false;
                };
                V3 at;
                if (frame == 20) c.handbrake = 1, getOut();
                scriptE = false;
                if (frame >= 24 && frame < 140) {  // the closed back, then hold E: it opens
                    if (frame == 24) stand(1.9);
                    look(trunk::gateAt(c));
                    scriptE = frame > 40 && c.gateShut();
                } else if (frame >= 140 && frame < 175) {  // the open back, and a step closer to look in
                    double behind = 1.7, rise = 0, aside = 0;  // (CZ_TRUNKCAM=behind,rise,aside: a look from another spot)
                    if (const char* e = std::getenv("CZ_TRUNKCAM")) std::sscanf(e, "%lf,%lf,%lf", &behind, &rise, &aside);
                    if (frame == 140 || rise != 0 || aside != 0) stand(behind), walker.p += V3(-fl.z, 0, fl.x) * aside, walker.p.y += rise;
                    look(trunk::cargoCenter(c));
                } else if (frame >= 175 && frame < 270) {  // the hi-lift jack: hold E, and it's in your hands
                    if (itemAim({TK_JACK, 0}, at) || walker.holdingJack) look(walker.holdingJack ? trunk::cargoCenter(c) : at);
                    scriptE = frame > 180 && !walker.holdingJack;
                } else if (frame >= 270 && frame < 330) {  // ...and back it goes
                    look(trunk::cargoCenter(c));
                    scriptE = frame > 275 && walker.holdingJack;
                } else if (frame >= 330 && frame < 420) {  // a ground stake
                    if (itemAim({TK_GEAR, GEAR_STAKE}, at)) look(at);
                    scriptE = frame > 335 && !walker.hand.any();
                } else if (frame == 420) {
                    invOpen = true;
                }
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
            if (joinState == 1) {  // ---- multiplayer: joining, a step a frame
                const int st = net.pollJoin();
                if (st > 0) {
                    joinState = 0;
                    menu.joining = false;
                    closeMenu();
                    run.stop();
                    if (net.map != sim.world.map) loadMap(net.map);
                    net.adopt(sim);
                    hud.toast("JOINED " + net.ownerName(0) + "'S GAME ON " + mapName(net.map));
                    std::printf("joined (player %d)\n", net.self);
                } else if (st < 0) {
                    joinState = 2;
                    menu.joinError = net.error;
                    for (char& ch : menu.joinError) ch = (char)std::toupper((unsigned char)ch);
                    std::printf("can't join: %s\n", net.error.c_str());
                }
            }
            if (!net.joining()) net.receive(sim, netNow);
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
                if (m.text.rfind("job|", 0) == 0) {  // the host's job (again, for latecomers)
                    Job j;
                    if (Job::parse(m.text, j) && (!jobOn || j.title != job.title || job.wreckId != j.wreckId)) {
                        job = j;
                        jobOn = true;
                        hud.toast("JOB: " + job.title);
                    }
                    continue;
                }
                if (m.text == "jobstop") {
                    jobOn = false;
                    continue;
                }
                if (m.text.rfind("cache ", 0) == 0) {  // a buddy opened a supply cache: it counts for you too
                    const size_t sp = m.text.find(' ', 6);
                    if (career.on && sp != std::string::npos && std::atoi(m.text.c_str() + 6) == sim.world.map) {
                        const std::string who = m.player >= 0 && m.player < net::kMaxPeers ? net.players[m.player].name : std::string("A BUDDY");
                        openCache(m.text.substr(sp + 1), who);
                    }
                    continue;
                }
                if (m.text == "jobdone") {
                    if (jobOn && !job.done) {
                        job.done = true;
                        jobBanner = 6;
                        jobBannerText = "JOB DONE: " + job.title;
                        if (career.on) {  // (everyone on the job is paid)
                            jobBannerText += "   " + career.jobDone();
                            if (!scripted) career.save(Career::defaultPath());
                        }
                    }
                    continue;
                }
                if (m.text.rfind("fuel ", 0) == 0) {  // a buddy poured their can into one of mine
                    unsigned id = 0;
                    double liters = 0;
                    if (std::sscanf(m.text.c_str(), "fuel %u %lf", &id, &liters) == 2 && std::isfinite(liters) && liters > 0 && liters <= 60)  // (a can holds a few liters: not whatever a buddy typed)
                        for (Car& c : sim.cars)
                            if (c.netId == id && !c.remote) {
                                if (c.fuel < 0) c.fuel = c.spec->fuelCapacity;
                                c.fuel = std::min(c.spec->fuelCapacity, c.fuel + liters);
                                hud.toast(net.players[std::min(std::max(m.player, 0), net::kMaxPeers - 1)].name + " POURED IN " + std::to_string((int)liters) + " L OF FUEL");
                            }
                    continue;
                }
                if (m.player >= 0 && m.player < net::kMaxPeers && coop.onMessage(net.players[m.player].name, m.text)) continue;
                if (m.text == "restart") {
                    if (run.active()) run.begin(sim, runSlot(), 3.0);
                } else if (m.player >= 0 && m.player < net::kMaxPeers) {
                    run.onMessage(m.player, net.players[m.player].name, m.text);
                }
            }
            net.gameIn.clear();
            for (Car& c : sim.cars)
                if (!c.remote) c.fuelUse = settings.fuelMode == 0 ? 0.0 : settings.fuelMode == 1 ? 1.0 : 4.0;
            if (walker.active && !sim.paused) {
                // On foot: the keys walk you; the truck sits on its handbrake.
                const V3 vf = norm(V3(lastView.target.x - lastView.eye.x, 0, lastView.target.z - lastView.eye.z));
                const double viewYaw = std::atan2(-vf.z, vf.x);
                walker.step(sim.world, sim, dt, (up ? 1 : 0) - (down ? 1 : 0), (left ? 1 : 0) - (right ? 1 : 0), shiftHeld, hb, viewYaw);
                {  // hands on: hold E (pad: X) for the job in front of you
                    const uint8_t* ks = SDL_GetKeyboardState(nullptr);
                    const bool holdE = (!menu.open && ks[SC_E]) || (padCtx == pad::Mode::Drive && padLogic.in.interact) || scriptE;
                    interact.spots.clear();  // (a career's supply caches in reach: E opens one)
                    if (career.on) {
                        for (const CacheSpot& c : caches)
                            if (!career.isOpened(c) && len(c.at - walker.p) < 6) interact.spots.push_back({c.id, c.at, "OPEN THE SUPPLY CACHE"});
                        // A station's rack: tools and repair parts for sale.
                        for (int s = 0; s < (int)sim.world.stations.size(); s++) {
                            if (len(sim.world.stations[s].p - walker.p) > 14) continue;
                            for (int i = 0; i < (int)stationRack().size(); i++) {
                                const ShopItem& it = stationRack()[i];
                                const std::string name = it.key.substr(it.key.find(':') + 1);
                                std::string prompt = "BUY " + name + "  $" + std::to_string(it.price);
                                if (it.key.rfind("TOOL:", 0) == 0 && career.owned.count(it.key)) prompt = name + ": YOU HAVE IT";
                                else if (career.stock.count(it.key)) prompt += "  (YOU HAVE " + std::to_string(career.stock.at(it.key)) + ")";
                                interact.spots.push_back({"SHOP:" + std::to_string(i), rackSpot(sim.world, s, i) - V3(0, 0.3, 0), prompt});
                            }
                        }
                    }
                    {  // your kit rides in the truck's back (trunk.h): usable in your hand, or at its open back
                        const Kit have = career.kit();
                        TrunkRef& hd = walker.hand;
                        if ((hd.kind == TK_TOOL && !have.has((Tool)hd.id)) || (hd.kind == TK_PART && !have.hasPart((RepairPart)hd.id)) ||
                            (hd.kind == TK_GEAR && sim.anchorStock.n[hd.id] <= 0))
                            hd = TrunkRef();  // (used up, or lost)
                        interact.own = TrunkStock{have, sim.anchorStock};
                        // (--script work-*: the old way, everything on you: those shots are of the work, not the trunk)
                        const bool onYou = scripted && opt.script.rfind("work-", 0) == 0;
                        interact.trunkOn = !onYou;
                        const bool reach = onYou || trunk::kitInReach(sim, walker.p);
                        interact.kit = onYou ? have : trunk::reachKit(have, reach, hd, walker.holdingJack);
                        if (invOpen && !reach) invOpen = false;  // (walked off, or shut it)
                        if (padCtx == pad::Mode::Drive && padLogic.in.inventory) toggleInventory();
                    }
                    interact.hover = PartHit();  // the car part under the crosshair (carparts.h), nearest car part wins
                    if (cam.mode != 3) {
                        const V3 look = norm(lastView.target - lastView.eye);
                        for (int ci = 0; ci < (int)sim.cars.size(); ci++) {
                            PartHit h;
                            if (pickCarPart(sim.cars[ci], lastView.eye, look, 4.0, debug != DEBUG_PARTS, h) && (!interact.hover.ok || h.t < interact.hover.t)) h.car = ci, interact.hover = h;
                        }
                    }
                    const std::string said = interact.update(sim, walker, lastView.eye, lastView.target - lastView.eye, holdE, dt);
                    if (!said.empty()) hud.toast(said);
                    if (interact.usedPart >= 0) {  // a repair used a part
                        if (career.on) {
                            career.usePart((RepairPart)interact.usedPart);
                            if (!scripted) career.save(Career::defaultPath());
                        }
                        interact.usedPart = -1;
                    }
                    if (interact.openedSpot.rfind("SHOP:", 0) == 0) {  // bought off a rack
                        const int i = std::atoi(interact.openedSpot.c_str() + 5);
                        std::string why;
                        if (i >= 0 && i < (int)stationRack().size()) {
                            const ShopItem& it = stationRack()[i];
                            if (career.buyGear(it, why)) {
                                hud.toast("BOUGHT " + it.key.substr(it.key.find(':') + 1) + "  ($" + std::to_string(career.cash) + " LEFT)");
                                if (!scripted) career.save(Career::defaultPath());
                            } else hud.toast(why);
                        }
                        interact.openedSpot.clear();
                    }
                    if (!interact.openedSpot.empty()) {
                        if (openCache(interact.openedSpot, "") && net.active()) net.sendGame("cache " + std::to_string(sim.world.map) + " " + interact.openedSpot);
                        interact.openedSpot.clear();
                    }
                    if (padCtx == pad::Mode::Drive && padLogic.in.drop) {
                        if (walker.carrying) Interact::drop(sim, walker, lastView.target - lastView.eye);
                        else if (walker.holdingJack) Interact::dropJack(sim, walker);
                        else if (walker.hand.any()) hud.toast("PUT IT BACK IN THE TRUCK: HOLD " + std::string("X") + " AT THE OPEN BACK");
                    }
                }
                up = down = left = right = false;
                hb = true;
                walkSend -= dt;
                if (net.active() && walkSend <= 0) {
                    walkSend = 0.1;
                    char b[96];
                    std::snprintf(b, sizeof b, "walk %.2f %.2f %.2f %.3f %.2f", walker.p.x, walker.p.y, walker.p.z, walker.yaw, walker.stride);
                    net.sendGame(b);
                }
            }
            if (!sim.paused) sim.drive(up, down, left, right, hb, dt);
            {  // ---- gamepad: analog steering and pedals on top of the keyboard path ----
                Car& pc = sim.cars[sim.player];
                if (padDrives && !sim.paused && !pc.ai && !pc.pilot.active) padLogic.apply(pc, dt);
                else padLogic.steerState = 0;
            }
            if (jobOn && !sim.paused) {
                // Where everyone is: you (on foot or in the truck), and each other player's
                // nearest truck (or them on foot).
                std::vector<V3> players = {walker.active ? walker.p : sim.cars[sim.player].frame().com};
                if (net.active())
                    for (int pid = 0; pid < net::kMaxPeers; pid++) {
                        if (pid == net.self || !net.players[pid].active) continue;
                        double best = 1e18;
                        V3 at;
                        for (const Car& c : sim.cars)
                            if (c.remote && !c.isProp() && NetSession::ownerOf(c.netId) == pid && c.netId != job.wreckId) {
                                const V3 q = c.frame().com;
                                const double d = std::hypot(q.x - job.target.x, q.z - job.target.z);
                                if (d < best) best = d, at = q;
                            }
                        for (const RemoteWalker& rw : coop.walkers)
                            if (rw.who == net.players[pid].name && std::hypot(rw.p.x - job.target.x, rw.p.z - job.target.z) < best) at = rw.p, best = 0;
                        if (best < 1e17) players.push_back(at);
                    }
                const bool authority = net.role != NetSession::CLIENT;
                if (updateJob(job, sim, players, dt) && authority) {
                    jobBanner = 6;
                    char b[96];
                    std::snprintf(b, sizeof b, " IN %d:%02d", (int)job.time / 60, (int)job.time % 60);
                    jobBannerText = "JOB DONE: " + job.title + b;
                    if (career.on) {
                        jobBannerText += "   " + career.jobDone();
                        if (!scripted) career.save(Career::defaultPath());
                    }
                    if (net.role == NetSession::HOST) net.sendGame("jobdone");
                    jobSeed++;
                } else if (!authority) {
                    job.done = job.done && jobBanner > 0;  // (a client waits for the host's word)
                }
                if (net.role == NetSession::HOST) {
                    jobResend -= dt;
                    if (job.kind == JOB_RECOVER && job.wreck >= 0 && job.wreck < (int)sim.cars.size() && !job.wreckId) job.wreckId = sim.cars[job.wreck].netId;
                    if ((jobAnnounce && (job.kind != JOB_RECOVER || job.wreckId)) || jobResend <= 0) {
                        if (job.kind != JOB_RECOVER || job.wreckId) net.sendGame(job.text());
                        jobAnnounce = false;
                        jobResend = 5;
                    }
                }
                if (job.done && jobBanner <= 0) jobOn = false;
            }
            jobBanner = std::max(0.0, jobBanner - dt);
            {  // Anchor gear. A career's stock is the truth (bought, found): the sim's counts follow it, and what the winch
               // uses goes back into it. Sandbox is the sim's own set, refilled at a station.
                if (!career.on) sim.anchorStock.refills = true;
                else if (sim.anchorStock.refills || career.anchorStock() != anchorSeen) {  // (a new sim has refills set)
                    sim.anchorStock = anchorSeen = career.anchorStock();
                } else if (sim.anchorStock != anchorSeen) {
                    career.setAnchorStock(sim.anchorStock);
                    anchorSeen = sim.anchorStock;
                    if (!scripted) career.save(Career::defaultPath());
                }
            }
            // Exploring: places found (you, in the truck or on foot).
            discoverCheck -= dt;
            if (discoverCheck <= 0 && !playback.active()) {
                discoverCheck = 0.5;
                // A career's supply caches: a crate at each (put back when the map's rebuilt).
                auto crateAt = [&](const V3& p) {
                    for (const Box& b : sim.world.boxes)
                        if (len(b.c - (p + V3(0, 0.33, 0))) < 0.05) return true;
                    return false;
                };
                // ...and each station's rack of tools and parts, laid out beside the shed.
                if (career.on && !sim.world.stations.empty()) {
                    bool shown = false;  // (a marker in the world's models: gone when the map's rebuilt)
                    for (const Placed& pl : sim.world.models) shown |= pl.id == "(station racks)";
                    if (!shown) {
                        Placed mark;
                        mark.id = "(station racks)";
                        sim.world.models.push_back(mark);
                        for (int s = 0; s < (int)sim.world.stations.size(); s++)
                            for (int i = 0; i < (int)stationRack().size(); i++)
                                if (!stationRack()[i].model.empty())
                                    sim.world.placeModel(stationRack()[i].model, rackSpot(sim.world, s, i), sim.world.stations[s].yaw + M_PI / 2);
                        renderer.updateObjects(sim.world);
                    }
                }
                if (!career.on) caches.clear(), cachesMap = -1;
                else if (cachesMap != sim.world.map || (!caches.empty() && !crateAt(caches[0].at))) {
                    caches = cacheSpots(sim.world);
                    cachesMap = sim.world.map;
                    for (const CacheSpot& c : caches)
                        if (!crateAt(c.at)) {
                            const double yaw = (double)(std::hash<std::string>()(c.id) % 628) / 100;
                            sim.world.addBox(c.at + V3(0, 0.33, 0), V3(0.55, 0.33, 0.38), yaw, 0, 0, V3(0.29, 0.34, 0.19));
                            if (mk::findModel("supply_crate")) {  // drawn as the picked crate (the box stays its collider)
                                sim.world.boxes.back().hidden = true;
                                Placed pl;
                                pl.id = "supply_crate", pl.p = c.at, pl.yaw = yaw;
                                pl.set = {{"open", career.isOpened(c) ? 1.0 : 0.0}};
                                sim.world.models.push_back(pl);
                            }
                        }
                    if (!caches.empty()) sim.world.buildGrid();
                }
                std::vector<std::pair<std::string, V3>> places;
                for (const Place& pl : pilotPlaces(sim, net))
                    if (pl.kind == 0) places.push_back({pl.name, pl.p});
                coop.mapNow = sim.world.map;
                const V3 me = walker.active ? walker.p : sim.cars[sim.player].frame().com;
                for (const std::string& name : coop.discover(sim.world.map, me, places)) {
                    jobBanner = std::max(jobBanner, 4.0);
                    jobBannerText = "DISCOVERED: " + name;
                    if (net.active()) coop.say("FOUND " + name, myName(), net);
                    if (!scripted) coop.saveDiscoveries(discoveriesPath);
                }
            }
            if (!sim.paused && run.active()) {
                run.update(sim, dt);  // (after the keys: it holds the car on the grid)
                // A career: medals pay (each once a level), a level's first gold brings a part.
                static bool medalPaid = false;
                if (run.state != LevelRun::FINISHED) medalPaid = false;
                else if (!medalPaid) {
                    medalPaid = true;
                    const int m = run.medalFor(run.finishTime);  // (0 gold .. 2 bronze, -1 none)
                    const std::string paid = career.on && m >= 0 ? career.medal(run.level.name, 3 - m) : "";
                    if (!paid.empty()) {
                        jobBanner = std::max(jobBanner, 6.0);
                        jobBannerText = run.level.name + ": " + paid;
                        if (!scripted) career.save(Career::defaultPath());
                    }
                }
                if (run.state == LevelRun::FINISHED && (bestTimes.find(run.level.name) == bestTimes.end() || run.finishTime < bestTimes[run.level.name]))
                    bestTimes[run.level.name] = run.finishTime;
                for (const std::string& m : run.outbox) net.sendGame(m);
                run.outbox.clear();
            }
            const uint64_t physicsStart = SDL_GetPerformanceCounter();
            simDt = sim.advance(dt, 120) * sim.h;
            const double physicsMs = (double)(SDL_GetPerformanceCounter() - physicsStart) * 1000 / freq;
            hud.physicsMs += (physicsMs - hud.physicsMs) * 0.05;
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
            {  // ---- gamepad rumble: only while the player is on the pad (the last thing they used) ----
                const bool want = pad::ui.last && padDev.connected() && padCtx == pad::Mode::Drive && !walker.active && !sim.paused && settings.padRumble > 0 &&
                                  (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) && sim.player >= 0 && sim.player < (int)sim.cars.size();
                if (want) {
                    const pad::RumbleOut ro = rumbler.update(sim.cars[sim.player], settings.padRumble, dt);
                    if ((padRumbleTimer -= dt) <= 0) {
                        padRumbleTimer = 0.1;
                        padDev.rumble(ro.low, ro.high);
                        padRumbling = ro.low > 0.01f || ro.high > 0.01f;
                    }
                } else if (padRumbling) {
                    padDev.rumble(0, 0);
                    padRumbling = false;
                    rumbler.reset();
                }
            }
        }

        // Reset anything that exploded numerically; track damage and crash energy.
        double impact = 0;
        for (int i = 0; i < (int)sim.cars.size(); i++) {
            Car& c = sim.cars[i];
            if (!c.finite()) {
                sim.dropRopes(i);
                c.respawn(c.spawnPos, c.spawnYaw);
            }
            if (!c.remote) c.updateStats();
            impact += c.impactEnergy;
            c.impactEnergy = 0;
        }
        for (int i = 0; i < (int)sim.cars.size(); i++) {
            Car& c = sim.cars[i];
            if (!c.remote) c.lampMode = (uint8_t)(i == sim.player ? lampMode : c.ai || c.pilot.active ? LAMPS_AUTO : LAMPS_OFF);
        }
        if (!(playback.active() && !playback.done) && !net.joining()) net.send(sim, (double)(SDL_GetPerformanceCounter()) / freq);
        FxEvents events = fx.update(sim, simDt);

        Car& car = sim.cars[sim.player];
        Frame fr = car.frame();
        if (audioOn) {
            float slip = 0, load = (float)(sim.paused ? 0 : car.throttle * car.tcCut);
            for (const Wheel& wh : car.wheels)
                if (wh.load > 500) slip = std::max(slip, wh.slip);
            audio.setEngine((float)(car.engineDead ? 0 : car.rpm) * (float)std::max(0.35, sim.timeScale), load);
            const VehicleSpec& vs = *car.spec;
            audio.setVoice(vs.cylinders, (float)vs.displacement, (int)vs.induction, vs.flatPlane, (float)vs.redline, voiceFor(vs));
            audio.setRecording(engineSamples::forVehicle(vs));
            audio.setBoost(vs.induction == VehicleSpec::TURBO ? (float)car.boost : 0.0f);
            audio.setTires(sim.paused ? 0 : slip);
            audio.setVolume(settings.volume, menu.open ? 0.35f : 1.0f);
            {  // the music follows the country under you (steadied, so a border doesn't flip it back and forth)
                static BiomeTracker biomes;
                const V3 at = walker.active ? walker.p : fr.com;
                biomes.update(sim.world, at.x, at.z, dt);
                audio.setMusic(sim.world.map, settings.music, biomes.current);  // master volume applies to the whole mix
            }
            audio.setSpeed(sim.paused ? 0 : (float)len(fr.vel));
            float amp = (float)clampd(std::log10(1.0 + impact / 50.0) / 3.5, 0, 1);
            if (amp > 0.08) audio.impact(amp);
            if (events.glass) audio.glass(std::min(1.0f, 0.4f + 0.06f * events.glass));
            if (events.tirePops) audio.pop();
            {  // ---- snow chains snapping (driven too fast on them)
                static int chainsSeen = 0;
                Car& pc = sim.cars[sim.player];
                if (pc.chainsSnapped > chainsSeen) hud.toast("A SNOW CHAIN SNAPPED: KEEP UNDER 50 KM/H ON CHAINS");
                chainsSeen = pc.chainsSnapped;
            }
            // Tire air hiss while inflating/deflating
            const Car& pcar = sim.cars[sim.player];
            const bool airing = !sim.paused && pcar.airing != 0 && len(pcar.frame().vel) < 0.5 &&
                                (pcar.airing < 0 ? pcar.tirePsi > 12 : pcar.tirePsi < 40);
            audio.setHiss(airing ? 0.6f : 0.0f);
        }

        int W, H;
        SDL_GL_GetDrawableSize(win, &W, &H);
        if (scripted && opt.script == "job" && frame == 10) {  // (screenshots: a recovery job under way)
            refreshJobs();
            if (!jobList.empty()) startJob(jobList[0]);
        }
        if (scripted && opt.script == "walk") {  // (screenshots: out of the truck and walking off)
            if (frame == 20) getOut();
            if (frame > 30) up = true;
        }
        if (scripted && opt.script == "map" && frame == 5) {  // (screenshots: the map, a ping and a waypoint)
            const V3 c = sim.cars[sim.player].frame().com;
            coop.ping(1, c + V3(120, 0, 80), "BUDDY", net);
            coop.haveWaypoint = true;
            coop.waypoint = c + V3(-150, 0, 200);
            coop.mapOpen = opt.camera != 5;
            coop.say("ON MY WAY, HANG TIGHT", "YOU", net);
        }
        if (editor.active && scripted && opt.script.rfind("editsection", 0) == 0 && frame == 3) {
            // (Screenshots: the section editor: a fresh canvas, its page, a stamped rock wall, a slab ledge and the slope readout.)
            EditCore& ed = editor.core;
            const SectionInfo c = ed.level.section;
            const V3 f(std::cos(c.heading), 0, -std::sin(c.heading)), e = ed.level.start;
            if (opt.script == "editsection-save") {  // (a saved section to play: XDG_CONFIG_HOME says where)
                ed.settings("demo", [](Level& l) {
                    l.name = "ROCK WALL V4";
                    l.section.kind = SK_LEDGES, l.section.difficulty = 4;
                    l.section.tests = ST_SKILL | ST_COMMIT, l.section.kit = SKIT_4L | SKIT_LOCKERS;
                });
                ed.stampSection(SEC_ROCK_WALL, 3);
                std::string err;
                std::printf("saved: %d %s | %s\n", (int)ed.save(userLevelFolder() + "/rock_wall_v4.czl", &err), err.c_str(), ed.saveNote.c_str());
            } else if (opt.script == "editsection-meta") {
                editor.setTool(Editor::SECTION);
                ed.settings("demo", [](Level& l) {
                    l.name = "ROCK WALL V4";
                    l.section.kind = SK_LEDGES, l.section.difficulty = 4;
                    l.section.tests = ST_SKILL | ST_COMMIT, l.section.kit = SKIT_4L | SKIT_LOCKERS | SKIT_AIRDOWN;
                });
                ed.stampSection(SEC_ROCK_WALL, 3);
            } else if (opt.script == "editsection-wall") {
                editor.setTool(Editor::OBJECTS);
                ed.stampSection(SEC_ROCK_WALL, 3);
                editor.setCamera(e + f * 28 + V3(0, 9, 0), c.heading, -0.38);
            } else if (opt.script == "editsection-ledge") {
                editor.setTool(Editor::OBJECTS);
                editor.setLedge(2, 3.0);
                const V3 l(-f.z, 0, f.x);
                editor.placeLedge(e + f * 30 + l * -6, e + f * 30 + l * 6);
                editor.placeLedge(e + f * 40 + l * -6, e + f * 40 + l * 6);
                editor.setLedge(4, 3.0);
                editor.placeLedge(e + f * 52 + l * -6, e + f * 52 + l * 6);
                editor.setCamera(e + f * 8 + V3(0, 7, 0), c.heading, -0.3);
                editor.setMouse(W * 0.5f, H * 0.62f);
            }
        }
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
        if (walker.active) {  // the camera follows you on foot
            fr = Frame();
            fr.com = walker.p + V3(0, 1.1, 0);
            fr.fwd = V3(std::cos(walker.yaw), 0, -std::sin(walker.yaw));
            fr.up = V3(0, 1, 0);
            fr.left = cross(fr.up, fr.fwd);
            fr.vel = walker.v;
        }
        // ---- gamepad: the right stick turns the camera; on the map the sticks pan and the triggers zoom ----
        bool padLooking = false;
        if (padCtx == pad::Mode::Drive || padCtx == pad::Mode::Aim) {
            const pad::Intent& pin = padLogic.in;
            padLooking = pin.lookX != 0 || pin.lookY != 0 || pin.lookBack;
            if (walker.active && winchAim.on) {
                footYaw -= pin.lookX * 2.6 * dt;
                footPitch = clampd(footPitch + pin.lookY * 1.8 * dt, -1.45, 1.45);
            } else if (padLooking || pin.resetView) cam.padLook(pin.lookX, pin.lookY, pin.lookBack, pin.resetView, dt);
            if (padLooking && winchAim.on) winchAim.pointer = false;
        } else if (padCtx == pad::Mode::Map) {
            const pad::Intent& pin = padLogic.in;
            int dw = 1, dh = 1;
            SDL_GL_GetDrawableSize(win, &dw, &dh);
            if (pin.mapPanX != 0 || pin.mapPanY != 0) coop.mapMouse(dw / 2.0f, dh / 2.0f, 0, true, -pin.mapPanX * 600.0f * (float)dt, -pin.mapPanY * 600.0f * (float)dt);
            if (pin.mapZoom) coop.mapMouse(dw / 2.0f, dh / 2.0f, pin.mapZoom, false, 0, 0);
            V3 wp;
            if (pin.mapSelect && coop.mapPoint(dw / 2.0f, dh / 2.0f, wp)) {  // (A: the waypoint at the centre, as a click would)
                wp.y = sim.world.height(wp.x, wp.z);
                coop.haveWaypoint = !(coop.haveWaypoint && len(V3(wp.x - coop.waypoint.x, 0, wp.z - coop.waypoint.z)) < 15);
                coop.waypoint = wp;
                hud.toast(coop.haveWaypoint ? "WAYPOINT SET" : "WAYPOINT CLEARED");
            }
        }
        if (walker.active && padCtx == pad::Mode::Drive && cam.mode != 3) {  // (gamepad: the right stick looks around on foot)
            footYaw -= padLogic.in.lookX * 2.6 * dt;
            footPitch = clampd(footPitch + padLogic.in.lookY * 1.8 * dt, -1.45, 1.45);
        }
        View view = editor.active ? editor.view(W, H) : cam.update(fr, sim.world, dt, (dragging || padLooking) && !menu.open);
        if (walker.active && !editor.active && cam.mode != 3) {
            // First person: eyes 1.62 m up, a little bob as you walk.
            const double bob = walker.grounded ? 0.025 * std::sin(walker.stride * 2) * std::min(1.0, len(walker.v) / 1.5) : 0.0;
            view.eye = walker.p + V3(0, 1.62 + bob, 0);
            view.target = view.eye + V3(std::cos(footYaw) * std::cos(footPitch), std::sin(footPitch), -std::sin(footYaw) * std::cos(footPitch)) * 10.0;
            view.up = V3(0, 1, 0);
            view.fovy = cam.fovy;
            // (--script work-*: CZ_WORK_CAM=out,up,side puts the eye out / up / along from the corner's hub, looking at it: the work seen from outside)
            if (const char* wc = std::getenv("CZ_WORK_CAM"); wc && opt.script.rfind("work-", 0) == 0 && sim.player >= 0) {
                double e[3] = {1, 0.3, 0};
                std::sscanf(wc, "%lf,%lf,%lf", e, e + 1, e + 2);
                const Car& wc0 = sim.cars[sim.player];
                const int k = std::getenv("CZ_WORK_WHEEL") ? std::atoi(std::getenv("CZ_WORK_WHEEL")) : opt.script == "work-chain" ? 0 : 2;
                if (k < (int)wc0.wheels.size()) {
                    const Wheel& wh = wc0.wheels[k];
                    const V3 hub = (wc0.nodes[wh.inner].p + wc0.nodes[wh.outer].p) * 0.5, out = norm(wc0.nodes[wh.outer].p - wc0.nodes[wh.inner].p);
                    const V3 side = norm(cross(V3(0, 1, 0), out));
                    view.eye = hub + out * e[0] + V3(0, e[1], 0) + side * e[2];
                    view.target = hub;
                }
            }
        }
        // A wheel in your arms: held low in front of you, its face toward you.
        renderer.carried.on = walker.active && walker.carrying && !editor.active && cam.mode != 3;
        if (renderer.carried.on) {
            const V3 fwd = norm(V3(std::cos(footYaw), 0, -std::sin(footYaw)));
            const WheelItem& it = *walker.carrying;
            renderer.carried.item = it;
            renderer.carried.c = view.eye + fwd * (0.45 + it.width * 0.5) - V3(0, 0.95 - it.R * 0.4, 0);
            renderer.carried.axis = -fwd;
            renderer.carried.up = V3(0, 1, 0);
        }
        {
            // The mouse is yours to look with on foot; anywhere else it's a pointer.
            const bool want = walker.active && !menu.open && !editor.active && !winchAim.on && cam.mode != 3;
            if (want != mouseLook) {
                SDL_SetRelativeMouseMode(want ? 1 : 0);
                mouseLook = want;
            }
        }
        view.width = W;
        view.height = H;
        if (audioOn) audio.setNearbyEngines(sim, view.eye, norm(cross(view.target - view.eye, view.up)),
                                           (float)std::max(0.35, sim.timeScale));
        if (audioOn) audio.setEnvironment(sim.world.map, (float)dayHours, settings.ambience,
                                         !walker.active && cam.mode == 2 ? 0.3f : 1.0f);
        lastView = view;
        if (winchAim.on) winchAim.update(sim, view);
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
            else if (editor.active && editor.core.level.timeOfDay >= 0) dayHours = editor.core.level.timeOfDay;  // the editor shows the level's light
            else if (levelHours >= 0 && settings.timeOfDay != TOD_CYCLE) dayHours = levelHours;  // a level's hour holds over a fixed setting (with the cycle it starts there and runs)
            else if (settings.timeOfDay != TOD_CYCLE) dayHours = timeOfDayHours(settings.timeOfDay);
            else dayRate = clockAdvance(1.0, settings.dayMinutes);
            if (!sim.paused) dayHours = std::fmod(dayHours + dayRate * dt * sim.timeScale, 24.0);
            settings.clockHours = (float)dayHours;
            if (net.role == NetSession::HOST) net.dayHours = dayHours, net.dayRate = dayRate;
            renderer.daylight = computeDaylight(dayHours);
            // Each owner's switch is carried reliably, including OFF and high beams.
            renderer.carBeams.assign(sim.cars.size(), 0);
            for (int i = 0; i < (int)sim.cars.size(); i++) {
                const Car& c = sim.cars[i];
                renderer.carBeams[i] = lampBeam(c.lampMode, renderer.daylight.night);
            }
        }
        menuBlur +=((menu.open ? 1.0f : 0.0f) - menuBlur) * (float)std::min(1.0, dt * 10);
        if (!menu.open && menuBlur < 0.01f) menuBlur = 0;
        // Keep saved/menu choices separate from temporary runtime reductions.
        // Scripted captures stay deterministic unless adaptation is explicitly requested.
        if (settings.adaptiveQuality && !menu.open && !garageUI.open && !editor.active && (!scripted || opt.adaptive == 1)) {
            adaptiveQuality.evaluate((float)frameTimeMs, adaptiveSettings);
        } else {
            adaptiveQuality.reset(settings.gfx);
            adaptiveSettings = settings.gfx;
        }
        renderer.settings = adaptiveSettings;
        renderer.settings.blur = menuBlur;
        if (sim.world.stamp != drawnWorld) {  // the map changed (a scenario built its own): redraw it
            renderer.setWorld(sim.world);
            drawnWorld = sim.world.stamp;
        }
        renderer.figures.clear();
        // (Your own figure isn't drawn: you're looking out of its eyes.)
        if (walker.active && (editor.active || cam.mode == 3)) renderer.figures.push_back({walker.p, walker.yaw, V3(0.95, 0.5, 0.12), walker.stride});
        for (const RemoteWalker& rw : coop.walkers) renderer.figures.push_back({rw.p, rw.yaw, V3(0.2, 0.6, 0.95), rw.stride});
        // A jack under a wheel: on the ground under its axle or arm, handle out, raised as far as it's been worked.
        renderer.frameModels.clear();
        if (mk::findModel("jack"))
            for (const Car& c : sim.cars)
                if (c.jackPlaced && c.jackWheel >= 0 && c.jackWheel < (int)c.wheels.size()) renderer.frameModels.push_back(Interact::jackModel(sim, c, c.jackWheel));
        // What rides in the back (trunk.h): your load laid out in the cargo area of a truck with its back
        // open (a pickup's bed always), and a pickup's gate.
        const TrunkStock ownStock{career.kit(), sim.anchorStock};
        for (int i = 0; i < (int)sim.cars.size(); i++) {
            const Car& c = sim.cars[i];
            if (c.isProp() || c.nodes.empty() || !c.hasGate() || len(c.nodes[0].p - view.eye) > 30) continue;
            trunk::models(sim, i, ownStock, walker.active ? walker.hand : TrunkRef(), walker.active && walker.holdingJack, walker.active && walker.holdingChain, renderer.frameModels);
        }
        // What rides in the back of a truck with no cargo area to open: the chains in their bag, the jack (while they're there).
        for (const Car& c : sim.cars) {
            if (c.isProp() || !c.spec->wheelLoss || c.hasGate()) continue;
            const Frame fr = c.frame();
            auto stowed = [&](const char* id, const V3& at) {
                if (!mk::findModel(id)) return;
                Placed pl;
                pl.id = id, pl.p = at - fr.up * 0.12, pl.oriented = true;
                pl.ax[0] = fr.fwd, pl.ax[1] = fr.up, pl.ax[2] = fr.left;
                renderer.frameModels.push_back(pl);
            };
            if (c.chains > 0) stowed("chain_bundle", c.stowAt(Car::STOW_CHAINS));
            if (c.hasJack && !c.jackPlaced && !walker.holdingJack) stowed("jack", c.stowAt(Car::STOW_JACK));
        }
        // What's under the car: the engine, gearbox, diffs, shafts, arms and springs, as models on the
        // physics nodes (carparts.h). A car's parts are ~80k vertices: the nearest four get them all (within
        // 20 m), the nearest eight the big ones (to 40 m).
        {
            std::vector<std::pair<double, const Car*>> near;
            for (const Car& c : sim.cars) {
                if (c.isProp() || c.nodes.empty()) continue;
                const double away = len(c.nodes[0].p - view.eye);
                if (away < 40) near.push_back({away, &c});
            }
            std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (size_t i = 0; i < near.size() && i < 8; i++) {
                const int level = i < 4 && near[i].first < 20 ? PARTS_ALL : PARTS_BIG;
                carPartModels(*near[i].second, renderer.frameModels, level);
                carFrameModels(*near[i].second, renderer.frameModels, level);  // (a ladder frame's rails, crossmembers, brackets and jack pads: carframe.h)
            }
        }
        // The work in your hands (first person): tools, the hand, a part going in; a wheel or
        // chain being moved (handwork.h).
        renderer.xrayRange = walker.active && !editor.active && cam.mode != 3 ? 7.0 : 0.0;  // (on foot: the hurt parts glow through the body)
        renderer.xrayEye = view.eye;
        renderer.wheelWork = Renderer::WheelWork();
        if (walker.active && !editor.active && cam.mode != 3) {
            const Handwork hw = handwork(sim, walker, interact, view.eye, view.target - view.eye, clock);
            renderer.frameModels.insert(renderer.frameModels.end(), hw.models.begin(), hw.models.end());
            renderer.wheelWork = hw.wheel;
            if (hw.carriedPose && renderer.carried.on) renderer.carried.c = hw.carriedC, renderer.carried.axis = hw.carriedAxis;
        }
        const uint64_t drawStart = SDL_GetPerformanceCounter();
        if (menu.open && menu.title) {  // the main menu is its own screen: no world behind it
            gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
            gl::Viewport(0, 0, W, H);
            gl::ClearColor(0.04f, 0.05f, 0.06f, 1);
            gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        } else {
            try {
                renderer.drawScene(sim, view, debug, &fx);
            } catch (const std::runtime_error& error) {
                fail(error.what());
                exitCode = 1;
                break;
            }
        }
        const double drawCpuMs = (double)(SDL_GetPerformanceCounter() - drawStart) * 1000 / freq;
        hud.drawCpuMs += (drawCpuMs - hud.drawCpuMs) * 0.05;
        if (scripted && std::getenv("CZ_BENCH")) {  // (the scene's frame time: wait for the GPU to finish it)
            unsigned char px[4];
            gl::ReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            benchDraw = (double)(SDL_GetPerformanceCounter() - drawStart) / freq;
        }
        hud.update(dt);
        if (editor.active && !menu.open) editor.draw(renderer, W, H, dt);
        else if (!menu.open && !hud.photo) drawHud(renderer, sim, view, hud, &playback, net, winchAim, W, H, fps, dt, settings.mph, career, caches);
        const bool showHud = !menu.open && !editor.active && !hud.photo;
        if (showHud && walker.active && cam.mode != 3) {  // on foot: a crosshair, the job in front of you, what you carry
            const float u = std::max(0.75f, H / 720.0f), s = px(1.4f * u);
            renderer.rect(W / 2.0f - 2 * u, H / 2.0f - 2 * u, 4 * u, 4 * u, 1, 1, 1, 0.8f);
            const bool padNow = pad::ui.last && padDev.connected();
            if (interact.current.kind != IA_NONE) {
                const std::string line = std::string(padNow ? "HOLD X  " : "HOLD E  ") + interact.current.prompt;
                centeredPill(renderer, W / 2.0f, H / 2.0f + 40 * u, s, line, 0, 0, 0, 0.6f, 1.0f, 0.85f, 0.45f);
                if (interact.progress > 0) {
                    const float bw = 220 * u, bx = W / 2.0f - bw / 2, by = H / 2.0f + 40 * u + 8 * s + 14 * u;
                    renderer.rect(bx, by, bw, 5 * u, 0, 0, 0, 0.6f);
                    renderer.rect(bx, by, bw * (float)interact.share(), 5 * u, 1.0f, 0.75f, 0.3f, 0.95f);
                }
            }
            if (interact.hover.ok && !walker.carrying) {  // the part under the crosshair: its name and how it is, and the repair that goes with it
                const PartHit& ph = interact.hover;
                const bool bad = ph.tag.state == PS_BROKEN, hurt = ph.tag.state != PS_OK;
                const float cr = bad ? 1.0f : hurt ? 1.0f : 0.85f, cg = bad ? 0.42f : hurt ? 0.7f : 0.95f, cb = bad ? 0.32f : hurt ? 0.3f : 0.85f;
                const float ls = px(1.8f * u);
                float y = H / 2.0f - 44 * u - 12 * ls;
                std::string hint;
                if (!(interact.current.kind == IA_FIX && partMatchesFix(ph.tag, interact.current.fix)) && ph.car >= 0 && ph.car < (int)sim.cars.size())
                    for (const Fix& f : fixesFor(sim.cars[ph.car]))
                        if (partMatchesFix(ph.tag, f)) {
                            const std::string miss = missingFor(f, interact.kit, f.wheel >= 0 && Interact::cornerLifted(sim, sim.cars[ph.car], f.wheel));
                            hint = std::string(len(ph.at - (walker.p + V3(0, 1.0, 0))) > 2.5 ? "GET CLOSER, THEN HOLD E: " : "HOLD E: ") + f.what + (miss.empty() ? "" : " (" + miss + ")");
                            break;
                        }
                if (!hint.empty()) {
                    centeredPill(renderer, W / 2.0f, y, s, hint, 0, 0, 0, 0.9f, 1.0f, 0.85f, 0.45f);
                    y -= 15 * s;
                }
                centeredPill(renderer, W / 2.0f, y, ls, ph.tag.label(), 0, 0, 0, 1.0f, cr, cg, cb);
            }
            if (walker.hand.any() && !walker.carrying) {
                const std::string line = "IN YOUR HAND: " + trunk::name(walker.hand) + (padNow ? "   (PUT IT BACK AT THE OPEN BACK: X)" : "   (PUT IT BACK AT THE OPEN BACK: E)");
                centeredPill(renderer, W / 2.0f, H - 110 * u, px(1.2f * u), line, 0, 0, 0, 0.55f, 0.9f, 0.92f, 0.95f);
            }
            if (invOpen) drawInventory(renderer, sim, TrunkStock{career.kit(), sim.anchorStock}, walker, u, W, H);
            if (walker.carrying) {
                const std::string line = "CARRYING A " + walker.carrying->sizeName() + (walker.carrying->tireOnly ? "" : " WHEEL") +
                                         (walker.carrying->popped ? " (FLAT)" : "") + (padNow ? "   B: PUT IT DOWN" : "   G: PUT IT DOWN");
                centeredPill(renderer, W / 2.0f, H - 110 * u, px(1.2f * u), line, 0, 0, 0, 0.55f, 0.9f, 0.92f, 0.95f);
            }
        }
        if (showHud && run.active()) drawLevelHud(renderer, run, sim, view, W, H);
        if (showHud && jobOn) {  // the job: what to do next, and where
            const float u = std::max(0.75f, H / 720.0f), s = px(1.2f * u);
            const V3 me = walker.active ? walker.p : sim.cars[sim.player].frame().com;
            const std::string status = jobStatus(job, sim, me);
            const float w = std::max(r_textW(renderer, job.title, s), r_textW(renderer, status, s)) + 24 * u;
            pill(renderer, 12 * u, 12 * u, w, 2 * 8 * s + 22 * u, 0, 0, 0, 0.55f);
            label(renderer, 22 * u, 18 * u, s, job.title, 1.0f, 0.62f, 0.25f, 1);
            renderer.text(22 * u, 24 * u + 8 * s, s, status, 0.92f, 0.93f, 0.95f, 1);
            V3 at;
            std::string what;
            static const float jc[3] = {1.0f, 0.62f, 0.25f};
            if (jobMarker(job, sim, me, at, what)) coop.drawMarker(renderer, sim, view, W, H, at, what, jc);
        }
        if (showHud && jobBanner > 0)
            centeredPill(renderer, W / 2.0f, H * 0.28f, px(2.2f * std::max(0.75f, H / 720.0f)), jobBannerText, 0, 0, 0, (float)std::min(1.0, jobBanner), 1.0f, 0.75f, 0.35f);
        if (showHud && settings.fuelMode > 0) {  // the fuel gauge
            const Car& c = sim.cars[sim.player];
            if (!c.isProp() && c.fuel >= 0 && c.spec->fuelCapacity > 0) {
                const float u = std::max(0.75f, H / 720.0f), s = px(1.2f * u);
                const float f = (float)(c.fuel / c.spec->fuelCapacity), w = 150 * u, x = W - w - 26 * u, y = H - 245 * u;
                pill(renderer, x - 8 * u, y - 6 * u, w + 16 * u, 8 * s + 22 * u, 0, 0, 0, 0.5f);
                char b[64];
                std::snprintf(b, sizeof b, "FUEL %.0f L%s", c.fuel, c.jerry > 0.5 ? ("   CAN " + std::to_string((int)std::lround(c.jerry)) + " L").c_str() : "");
                renderer.text(x, y, s, b, f < 0.15f ? 1.0f : 0.9f, f < 0.15f ? 0.4f : 0.9f, f < 0.15f ? 0.3f : 0.92f, 1);
                renderer.rect(x, y + 8 * s + 4 * u, w, 5 * u, 1, 1, 1, 0.15f);
                renderer.rect(x, y + 8 * s + 4 * u, w * f, 5 * u, f < 0.15f ? 1.0f : 0.95f, f < 0.15f ? 0.35f : 0.75f, 0.25f, 0.95f);
                if (c.outOfFuel()) centeredPill(renderer, W / 2.0f, H * 0.62f, px(1.6f * u), "OUT OF FUEL: A BUDDY'S SPARE CAN (F ON FOOT) OR A TOW TO A SHOP", 0, 0, 0, 1, 1, 0.5f, 0.4f);
            }
        }
        coop.update(dt);
        if (showHud || (coop.mapOpen && !menu.open && !editor.active)) {
            if (showHud) coop.drawWorld(renderer, sim, net, view, W, H);
            if (coop.mapOpen) {
                std::vector<std::pair<std::string, V3>> places;
                for (const Place& pl : pilotPlaces(sim, net))
                    if (pl.kind == 0) places.push_back({pl.name, pl.p});
                coop.drawMap(renderer, sim, net, W, H, places);
            }
            coop.drawChat(renderer, W, H);
        }
        if (menu.open) {
            {
                const Car& pc = sim.cars[sim.player];
                menu.pilotPlaces.clear();
                for (const Place& pl : pilotPlaces(sim, net)) menu.pilotPlaces.push_back(pl.name);
                menu.pilotActive = pc.pilot.active || pc.ai;
                menu.pilotStatus = pc.ai ? "CRUISING THE LOOP ROAD" : pc.pilot.active ? "DRIVING: " + pc.pilot.status : "OFF";
            }
            menu.netActive = net.active() && !net.joining();
            menu.levelActive = run.active();
            menu.jobActive = jobOn && !job.done;
            menu.jobStatus = jobOn ? jobStatus(job, sim, sim.cars[sim.player].frame().com) : "";
            menu.netClient = net.role == NetSession::CLIENT;
            menu.netHost = net.role == NetSession::HOST;
            menu.netStatus = !menu.netActive ? "OFFLINE"
                             : net.role == NetSession::HOST
                                 ? "HOSTING: " + std::to_string(net.playerCount()) + (net.playerCount() == 1 ? " PLAYER" : " PLAYERS")
                                 : "IN " + net.ownerName(0) + "'S GAME: " + std::to_string(net.playerCount()) + " PLAYERS";
            // ---- multiplayer page: the session's card, the network list, this machine's addresses
            if (menu.pageNow() == 2) {
                static double ipsAt = -10;
                const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                if (wall - ipsAt > 3) {
                    ipsAt = wall;
                    menu.localIps.clear();
                    for (const net::LocalAddress& la : net::localAddresses()) menu.localIps.push_back(la.ip);
                }
                if (menu.netActive) {
                    std::string title = net.role == NetSession::HOST ? "YOU'RE HOSTING " + net.sessionName : "IN " + net.ownerName(0) + "'S GAME";
                    for (char& ch : title) ch = (char)std::toupper((unsigned char)ch);
                    menu.sessionTitle = title;
                    menu.sessionLines.clear();
                    menu.sessionLines.push_back(std::string(mapName(net.map)) + ": " + std::to_string(net.playerCount()) + " OF " +
                                                std::to_string(net.role == NetSession::HOST ? net.maxPlayers : net::kMaxPeers) + " PLAYERS");
                    for (int k = 0; k < net::kMaxPeers; k++) {
                        const NetPlayer& p = net.players[k];
                        if (!p.active) continue;
                        std::string line = "  " + (p.name.empty() ? std::string("PLAYER") : p.name) + (k == net.self ? "  (YOU)" : "");
                        line += k == 0 ? "  HOST" : "  " + std::to_string((int)std::lround(p.ping * 1000)) + " MS";
                        for (char& ch : line) ch = (char)std::toupper((unsigned char)ch);
                        menu.sessionLines.push_back(line);
                    }
                }
            }
            if (menu.pageNow() == 2 && !net.active() && !menu.joining) {
                finder.poll(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
                menu.searching += dt;
                menu.found.clear();
                for (const net::FoundHost& fh : finder.hosts) {
                    PauseMenu::FoundGame g;
                    g.host = fh.name.empty() ? "A PLAYER" : fh.name;
                    g.session = fh.session.empty() ? g.host + "'S GAME" : fh.session;
                    for (std::string* t : {&g.host, &g.session})
                        for (char& ch : *t) ch = (char)std::toupper((unsigned char)ch);
                    g.map = mapName(fh.map);
                    g.players = std::to_string(fh.players) + "/" + std::to_string(fh.maxPlayers) + (fh.locked ? "  PASSWORD" : "");
                    g.address = fh.address;
                    g.ok = fh.joinable();
                    g.why = fh.version != net::kVersion ? "A DIFFERENT VERSION OF THE GAME" : "FULL: " + std::to_string(fh.players) + "/" + std::to_string(fh.maxPlayers);
                    menu.found.push_back(g);
                }
            } else if (!menu.found.empty() || menu.pageNow() != 2 || menu.searching > 0) {
                finder.close();
                menu.found.clear();
                menu.searching = 0;
            }
            // ---- end multiplayer page
        }
        if (garageUI.open) drawGarage(renderer, garageUI, garage, career, sim.stationAt(sim.cars[sim.player].frame().com), sim, std::max(0.75f, H / 720.0f), W, H);
        menu.draw(renderer, settings, W, H, dt);
        renderer.flushHud(W, H);

        // CZ_BENCH=1 (with --shot-frames): average frame time, the GPU finished (one pixel read back).
        static const bool bench = std::getenv("CZ_BENCH") != nullptr;
        static double benchSum = 0;
        static int benchN = 0;
        if (bench && scripted && frame >= 40) benchSum += benchDraw, benchN++;
        if (scripted && opt.shotFrames.count(frame)) {
            if (bench && benchN) {
                std::printf("bench: scene %.2f ms/frame over %d frames\n", 1000.0 * benchSum / benchN, benchN);
                const auto& geometry = renderer.geometryStats();
                std::printf("bench: terrain %u/%u triangles, %u/%u visible chunks; scenery %u vertices, shadows %u vertices\n",
                            geometry.terrainTriangles, geometry.terrainFullTriangles,
                            geometry.terrainVisibleChunks, geometry.terrainChunks,
                            geometry.sceneryVertices, geometry.shadowSceneryVertices);
                std::printf("bench: adaptive %d, scale %.2f, shadows %d, MSAA %d, AO %d, ground %d, props %d\n",
                            settings.adaptiveQuality, renderer.settings.renderScale, renderer.settings.shadows,
                            renderer.settings.msaa, renderer.settings.ao, renderer.settings.ground, renderer.settings.props);
                const PropStats& ps = renderer.propStats();
                std::printf("bench: props %.2f ms GPU (timer queries), %.2f ms CPU (%.2f ms culling/LODs); main %d draws, %d instances, %d billboards, %d scatter, %.0fk tris; "
                            "shadows %d draws, %d instances, %.0fk tris\n",
                            ps.gpuMs, ps.cpuMs, ps.selectMs, ps.draws, ps.instances, ps.billboards, ps.scatter, ps.tris / 1000.0, ps.shadowDraws, ps.shadowInstances,
                            ps.shadowTris / 1000.0);
            }
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
    padDev.close();  // (before SDL goes: stops any rumble)
    SDL_Quit();
    if (reload) {
        std::printf("reloading the new build at %s\n", resume.size() > 5 ? resume[5].c_str() : "?");
        std::fflush(stdout);
        watch.exec(argc, argv, resume);
    }
    return exitCode;
}
