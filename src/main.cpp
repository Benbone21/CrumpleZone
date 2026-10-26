// Crumple Zone: a soft-body driving sandbox.
#include "texenc.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include "loadprof.h"
#include "watchdog.h"
#include "benchreport.h"
#include "meshcache.h"
#include "carvisual.h"
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
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <sstream>
#include <map>

#include "allocstat.h"
#include "audio.h"
#include "toolsounds.h"
#include "horn.h"
#include "battery.h"
#include "repairsync.h"
#include "worldsound.h"
#include "biome.h"
#include "jobs.h"
#include "career.h"
#include "garageshop.h"
#include "garageshopdraw.h"
#include "interact.h"
#include "prompts.h"
#include "invdraw.h"
#include "walker.h"
#include "carentry.h"
#include "buildinfo.h"
#include "coop.h"
#include "places.h"
#include "debugscreen.h"
#include "editor.h"
#include "fx.h"
#include "gamepad.h"
#include "gl.h"
#include "gpu.h"
#include "hudcontext.h"
#include "hudlayout.h"
#include "hudkit.h"
#include "level.h"
#include "levelrun.h"
#include "mpmenu.h"
#include "mpmenudraw.h"
#include "rendezvous.h"
#include "laptimer.h"
#include "sectionfile.h"
#include "menu.h"
#include "mainmenu.h"
#include "mainmenudraw.h"
#include "menuscene.h"
#include "menucamp.h"
#include "camhold.h"
#include "throttle.h"
#include "tiretemp.h"
#include "mods.h"
#include "net.h"
#include "pilotsense.h"
#include "people.h"
#include "platform.h"
#include "props.h"
#include "carframe.h"
#include "carparts.h"
#include "handwork.h"
#include "lab.h"
#include "modelkit.h"
#include "objshape.h"
#include "render.h"
#include "perf.h"
#include "frameprof.h"
#include "simasync.h"
#include "sdl_min.h"
#include "softbody.h"
#include "../tests/harness.h"

namespace {

const double kTimeScales[] = {1.0, 0.5, 0.25, 0.1, 0.03};

// The player's bindings, for the hints that name a key (they show the CURRENT binding, never a fixed letter).
const Keybinds* gKeys = nullptr;
std::string K(KeyAction a) { return gKeys ? gKeys->name(a) : keyName(defaultKey(a)); }

struct Options {
    int physicsThreads = -1;  // -1 saved setting; 0 automatic; otherwise a CPU thread budget
    int width = 1600, height = 900;
    bool fullscreen = false, audio = true, vsync = true;
    bool trafficLod = true;  // --no-traffic-lod: far AI traffic runs the full soft-body sim too
    bool simAsync = true;  // --no-sim-async: step the physics on the frame thread, not alongside the GPU wait
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
    std::string clipboard;        // --clipboard TEXT: put this on the system clipboard first (testing paste); a --press scancode over 1000 is that key with Ctrl held
    int menu = -1;                // --menu P: open the pause menu on page P (testing)
    int mainMenu = -1;            // --main-menu P: the new main menu with its camp scene, on page P (0 front, 1 map, 2 vehicle, 3 dev); a plain launch shows it too
    std::string campDump;         // --menu-camp-dump FILE: write the main menu camp the game ships (made by code) there and quit
    bool menuCareer = false;      // --main-menu-career: its map and vehicle pages are CAREER's
    int quality = -1;             // --quality 0-3: graphics preset (testing)
    int ground = -1;              // --ground 0-3: terrain materials (testing; see RenderSettings::ground)
    int props = -1;               // --props 0-3: scanned props (testing; see RenderSettings::props)
    int grass = -1;               // --grass 0-3: the grass level (testing; see RenderSettings::grass)
    int adaptive = -1;            // --adaptive 0/1: auto-scale quality (default: on)
    bool career = false;          // --career: career mode (a fresh one when scripted)
    bool photo = false;           // --photo: no HUD (screenshots; F10 in the game)
    int debugScreen = 0;          // --debug-screen [graph]: F3 (and Shift+F3) already pressed
    int bench = 0;                // --bench N: fly a lap round the start for N frames (after a warm-up), print the frame times, quit
    int benchWarm = 60;           // --bench N,WARM: frames to warm up first (default 60)
    bool benchSuite = false;      // --bench-suite [N[,WARM]]: Frontier, Open Country and Boulder Gully at POTATO..HIGH, one launch, a report file and a shot per run
    std::string benchOut;         // --bench-out FILE: where the bench report goes (default: configDir()/bench/<date-time>-<map>-<quality>-<WxH>.txt)
    bool benchSync = false;       // --bench-sync: wait for the GPU at every stage (each stage's whole cost, no overlap)
    bool mph = false;             // --mph
    bool fullHud = false;         // --full-hud: SHOW FULL HUD on
    double throttleCap = -1;      // --throttle PCT[,RESPONSE[,PAD]]: the throttle limit (testing)
    int throttleResp = 0, throttlePad = 0;
    double eyeFly = 0;            // --eye-fly V (dev, with --eye): the drone flies on ahead at V m/s, its height over the ground kept (the streaming test)
    double mapHalf = 0;           // --map-half M (dev): every map built M m each way from its middle (big-map tests: docs/plans/big-maps.md)
    int map = -1;                 // --map N: 0 proving ground, 1 mountain
    double orbitYaw = 0, orbitPitch = 0.2, orbitDist = 7.5;  // degrees, degrees, meters
    bool spawnSet = false;
    bool loadTour = false;  // --load-tour: switch maps, swap and spawn vehicles on a timer, report the worst frame of each
    std::string resumeEdit;  // --resume-edit NAME: reopen that level's recovery copy in the editor (after a reload with unsaved edits)
    int resumeModel = -1;  // --resume-model: the vehicle to be in (after a reload)
    double spawnX = 0, spawnZ = 0, spawnHeading = 0;  // heading in degrees, 0 = +X
    int spawnSection = -1;        // --spawn-section N: start at the Nth obstacle section's entry (0-based, proving ground)
    int hostPort = -1;            // --host [PORT]: open a multiplayer session (0: on the port in the settings)
    std::string join;             // --join HOST[:PORT]
    std::string joinCode;         // --join-code CODE: join a friend's game with its code
    std::string rendezvous;       // --rendezvous HOST[:PORT]: the join-code server (else settings.txt's, else the game's own)
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
    camhold::Hold hold;

    View update(const Frame& fr, const World& world, double dt, bool dragging, bool driving = true) {
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
        // (Spun round and round, it comes back the short way, not unwinding every turn.) It only
        // comes back while the car is being driven: parked or crawling, the view stays as set.
        userYaw = wrapAngle(userYaw);
        hold.step(dt, driving, idle > camhold::kIdle, mode == 0 || mode == 2, mode == 0, userYaw, pitch, -0.05);

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
        static const std::set<std::string> withValue = {"--map", "--player", "--spawn", "--resume-model", "--resume-edit", "--scenario", "--script",
                                                        "--shot-prefix", "--shot-frames", "--clipboard", "--menu", "--orbit", "--spawn-section", "--eye"};
        static const std::set<std::string> flags = {"--garage", "--show-help", "--debug-screen", "--debug-graph", "--full-hud"};
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
        else if (a == "--no-sim-async") o.simAsync = false;
        else if (a == "--no-traffic-lod") o.trafficLod = false;
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
        else if (a == "--clipboard") o.clipboard = next();
        else if (a == "--menu") o.menu = std::atoi(next().c_str());
        else if (a == "--main-menu") o.mainMenu = std::atoi(next().c_str());
        else if (a == "--main-menu-career") o.menuCareer = true;
        else if (a == "--menu-camp-dump") o.campDump = next();
        else if (a == "--quality") {  // 0-3 LOW..ULTRA, 4 or potato: POTATO
            const std::string q = next();
            o.quality = q == "potato" ? (int)Q_POTATO : std::atoi(q.c_str());
        }
        else if (a == "--ground") o.ground = std::atoi(next().c_str());
        else if (a == "--props") o.props = std::atoi(next().c_str());
        else if (a == "--grass") o.grass = std::atoi(next().c_str());
        else if (a == "--adaptive") o.adaptive = std::atoi(next().c_str());
        else if (a == "--photo") o.photo = true;
        else if (a == "--debug-screen") o.debugScreen = 1;
        else if (a == "--bench") {  // N[,WARM]
            const std::string v = next();
            int n = 0, w = -1;
            std::sscanf(v.c_str(), "%d,%d", &n, &w);
            o.bench = std::max(1, n), o.benchWarm = w >= 0 ? w : o.benchWarm;
        }
        else if (a == "--bench-sync") o.benchSync = true;
        else if (a == "--bench-out") o.benchOut = next();
        else if (a == "--bench-suite") {  // [N[,WARM]]: default 600 frames after 120
            o.benchSuite = true;
            int n = 600, w = 120;
            if (i + 1 < argc && std::isdigit((unsigned char)argv[i + 1][0])) std::sscanf(argv[++i], "%d,%d", &n, &w);
            o.bench = std::max(1, n), o.benchWarm = std::max(0, w);
        }
        else if (a == "--debug-graph") o.debugScreen = 2;
        else if (a == "--mph") o.mph = true;
        else if (a == "--full-hud") o.fullHud = true;
        else if (a == "--throttle") {
            std::string v = next();
            std::sscanf(v.c_str(), "%lf,%d,%d", &o.throttleCap, &o.throttleResp, &o.throttlePad);
        }
        else if (a == "--career") o.career = true;
        else if (a == "--map") o.map = std::atoi(next().c_str());
        else if (a == "--map-half") o.mapHalf = std::atof(next().c_str());
        else if (a == "--eye-fly") o.eyeFly = std::atof(next().c_str());
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
            o.hostPort = 0;
            if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0) {  // (a port, for this run only)
                o.hostPort = net::parsePort(argv[i + 1], net::kMinHostPort);
                if (o.hostPort < 0) {
                    std::fprintf(stderr, "bad --host port \"%s\": a number from %d to %d (default: the PORT in the Multiplayer menu, %d)\n", argv[i + 1],
                                 net::kMinHostPort, net::kMaxPort, net::kDefaultPort);
                    std::exit(2);
                }
                i++;
            }
        } else if (a == "--join") {
            o.join = next();
            std::string host, why;
            int port = 0;
            if (!net::parseAddress(o.join, host, port, &why)) {
                std::fprintf(stderr, "bad --join address: %s (use HOST or HOST:PORT)\n", why.c_str());
                std::exit(2);
            }
        }
        else if (a == "--join-code") o.joinCode = next();
        else if (a == "--rendezvous") o.rendezvous = next();
        else if (a == "--name") o.name = next();
        else if (a == "--password") o.password = next();
        else if (a == "--resume-edit") o.resumeEdit = next();
        else if (a == "--resume-model") o.resumeModel = std::atoi(next().c_str());
        else if (a == "--time") o.timeHours = std::fmod(std::max(0.0, std::atof(next().c_str())), 24.0);
        else if (a == "--lamps") o.lamps = std::atoi(next().c_str());
        else if (a == "--level") o.level = next();
        else if (a == "--editor") o.editor = true;
        else if (a == "--new-section") o.newSection = true;
        else if (a == "--lab") o.lab = true;
        else if (a == "--load-tour") o.loadTour = true;
        else if (a == "--lab-model") o.lab = true, o.labModel = next();
        else if (a == "--lab-design") o.lab = true, o.labDesign = std::atoi(next().c_str());
        else if (a == "--help" || a == "-h") {
            std::puts("usage: crumplezone [--fullscreen] [--size WxH] [--no-audio] [--no-vsync] [--no-pad] [--spawn X,Z[,HEADING]] [--spawn-section N] [--scenario NAME]\n"
                      "                   [--map N] [--host [PORT]] [--join HOST[:PORT]] [--name NAME] [--password PASSWORD] [--time HOURS] [--level NAME]\n"
                      "                   [--physics-threads auto|all|N] [--lab [--lab-model ID]] [--quality 0-3] [--bench FRAMES [--bench-sync] [--bench-out FILE]] [--bench-suite [FRAMES[,WARM]]]");
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
        // The places of the big map (places.h): the ones not already listed here (within 40 m of one that is).
        for (const MapPlace& mp : mapPlaces(MAP_BACKCOUNTRY)) {
            if (mp.kind == PK_CACHE || mp.kind == PK_PHOTO_SPOT || mp.kind == PK_ABANDONED_VEHICLE) continue;
            bool have = false;
            for (const Place& q : out) have |= q.kind == 0 && std::hypot(q.p.x - mp.p.x, q.p.z - mp.p.z) < 40;
            if (!have) out.push_back({mp.name, V3(mp.p.x, sim.world.height(mp.p.x, mp.p.z), mp.p.z)});
        }
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
    bool devHotkeys = false;  // (the setting: which key the NEW BUILD pill names)
    double bestKmh = 0;     // freeway: fastest so far
    // The Frontier's race circuit: time on the lap running (-1: not on one), the last and
    // the best, and which side of the start line you were on.
    double lapTime = -1, lastLap = 0, bestLap = 0, lapSide = 0;
    // The proving ground's lap timer (laptimer.h): the lap running, the last and best (0: none), the checkpoints passed.
    bool lapOn = false;
    double plLap = 0, plLast = 0, plBest = 0;
    int plNext = 0;
    bool showHelp = false, showFps = false, scenarioDetail = false;
    bool debugOn = false;   // F3: the debug screen is up (it has the FPS, and the players list, in it)
    std::string gpu;        // the graphics card drawing it (GL_RENDERER), shown with the FPS
    double physicsMs = 0, drawCpuMs = 0;
    double confirmReset = 0;  // R just pressed: a second press within this long respawns at the start
    double hintTime = 7.0;  // "press H for controls" fades out after this
    float needle = 0;       // smoothed tach position 0..1
    hudlayout::TopLeftState tl;  // which top-left panels are up this frame (set before drawHud): they stack, not overlap
    bool levelOn = false;        // the level run's clock panel is up under the compass (the toasts start below it)
    float throttleCap = 1;  // the throttle limit (settings): shown by the gear when it's under 100%
    float blink = 0;        // the shift light's clock
    hudctx::Context ctx;    // which pieces show now (hudcontext.h): drawn at ctx.alpha(...)
    bool nearTruck = false, trunkOpen = false, airing = false, fuelGauge = false;  // (set before the context updates)
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

// Draws everything queued while it lives at the alpha of one contextual element.
struct HudFade {
    Renderer& r;
    float old;
    HudFade(Renderer& rr, const HudState& hud, int e) : r(rr), old(rr.hudAlpha) { r.hudAlpha = old * hud.ctx.alpha(e); }
    ~HudFade() { r.hudAlpha = old; }
};

// What most of the tires are on (-1: none touching), and the worst sink and tread clog.
int dominantSurface(const Car& car, double* sinkOut = nullptr, double* clogOut = nullptr) {
    int count[SURF_COUNT] = {0};
    double sink = 0, clog = 0;
    int on = 0;
    for (const Wheel& wh : car.wheels)
        if (wh.load > 0) count[wh.surface]++, on++, sink = std::max(sink, (double)wh.sink), clog = std::max(clog, (double)wh.clog);
    if (sinkOut) *sinkOut = sink;
    if (clogOut) *clogOut = clog;
    if (on == 0) return -1;
    int s = 0;
    for (int k = 1; k < SURF_COUNT; k++)
        if (count[k] > count[s]) s = k;
    return s;
}

// The contextual HUD's inputs for this frame, and the step.
void updateHudContext(HudState& hud, const Sim& sim, bool fuelOn, float dt) {
    const Car& car = sim.cars[sim.player];
    hudctx::Sample s;
    s.carId = sim.player;
    s.speed = (float)len(car.frame().vel);
    if (fuelOn && !car.isProp() && car.fuel >= 0 && car.spec->fuelCapacity > 0) {
        s.fuelFrac = (float)(car.fuel / car.spec->fuelCapacity);
        s.fuelL = (float)car.fuel, s.jerryL = (float)car.jerry, s.outOfFuel = car.outOfFuel();
    }
    double sink = 0, clog = 0;
    s.surface = car.isProp() ? -1 : dominantSurface(car, &sink, &clog);
    s.sunk = sink > 0.02, s.clogged = clog > 0.35;
    s.nearTruck = hud.nearTruck, s.trunkOpen = hud.trunkOpen, s.ropeOn = sim.ropeOn(sim.player) >= 0;
    if (!car.isProp() && (car.spec->extras & EXTRA_WINCH))
        for (int g = 0; g < GEAR_COUNT; g++) s.anchorSig = s.anchorSig * 31 + sim.anchorStock.n[g] + 1;
    s.damage = (float)clampd(car.damage * 2.0, 0, 1);
    s.psi = (float)car.tirePsi, s.streetPsi = (float)car.spec->tirePsi, s.airing = hud.airing;
    s.hasDriveline = !car.isProp() && (car.spec->transfer != XFER_NONE || car.spec->lockerFront || car.spec->lockerRear);
    if (const DriveMode* m = car.isProp() ? nullptr : car.mode()) s.mode = (int)(m - driveModes(car.spec->transfer).data()) + 1, s.lowRange = m->low;
    s.lapOn = hud.lapOn;
    if (!car.isProp())
        for (const Wheel& wh : car.wheels) {
            if (!wh.mounted || wh.debeaded) continue;
            const int tread = car.treadType(wh), band = tireTempBand(tread, wh.tempC);
            if (band > 0 || (band < 0 && s.tireBand == 0)) s.tireBand = band;
            s.tireLoss = std::max(s.tireLoss, (float)(1 - tireTempGrip(tread, wh.tempC)));
        }
    s.tireFrac = car.isProp() || !car.blowoutsOn ? 0.0f : (float)car.tireSpeedFrac();
    s.lockF = car.lockFront, s.lockR = car.lockRear, s.lockC = car.centerLocked(), s.sway = car.swayDisconnected;
    hud.ctx.update(s, dt);
}

void drawGauges(Renderer& r, const Car& car, HudState& hud, float u, int W, int H, double dt, bool mph) {
    const float R = hudlayout::kDialR * u, cx = W - R - 26 * u, cy = H - R - 18 * u;
    const float a0 = 0.75f * (float)M_PI, a1 = 2.25f * (float)M_PI;
    const double redline = car.spec->redline, maxRpm = std::ceil((redline + 600) / 1000) * 1000;
    float target = (float)clampd(car.rpm / maxRpm, 0, 1);
    hud.needle += (target - hud.needle) * (float)std::min(1.0, dt * 14);

    r.arc(cx, cy, 0, R, 0, 2 * (float)M_PI, 0.03f, 0.035f, 0.045f, 0.55f, 48);  // dial face
    r.arc(cx, cy, R - 9 * u, R - 4 * u, a0, a1, 0.35f, 0.37f, 0.4f, 0.55f);      // track
    float red0 = a0 + (a1 - a0) * (float)(redline / maxRpm);
    r.arc(cx, cy, R - 12 * u, R - 2 * u, red0, a1, 0.85f, 0.15f, 0.12f, 0.9f, 12);  // redline zone (wide: it's the one to see coming)
    hud.blink += (float)dt;
    if (car.manualShift && !car.engineDead && car.rpm >= redline * 0.94 && std::fmod(hud.blink, 0.2f) < 0.1f)  // shift light: the rim flashes near redline
        r.arc(cx, cy, R - 1 * u, R + 3 * u, 0, 2 * (float)M_PI, 1.0f, 0.25f, 0.15f, 0.95f, 48);
    float fillEnd = a0 + (a1 - a0) * hud.needle;
    r.arc(cx, cy, R - 9 * u, R - 4 * u, a0, std::min(fillEnd, red0), 0.98f, 0.62f, 0.18f, 0.95f, 40);
    // Ticks every 1000 rpm, labelled.
    for (int k = 0; k <= (int)(maxRpm / 1000); k++) {
        float a = a0 + (a1 - a0) * (float)(k * 1000 / maxRpm);
        r.arc(cx, cy, R - 16 * u, R - 10 * u, a - 0.012f, a + 0.012f, 0.85f, 0.85f, 0.88f, 0.9f, 1);
        if (u < 1 && k % 2) continue;  // (small dial: every other label)
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
    std::string spd = std::to_string(Car::speedoDisplay(car.speedoMs(), mph));
    float ss = px(4.5f * u), sm = px(1.2f * u);  // (speed is what's read most: big, 999 still fits)
    label(r, cx - r.textWidth(spd, ss) / 2, cy + 8 * u, ss, spd, 1, 1, 1);
    const char* unit = mph ? "MPH" : "KM/H";
    r.text(cx - r.textWidth(unit, sm) / 2, cy + 8 * u + 8 * ss + 3 * u, sm, unit, 0.78f, 0.8f, 0.84f);
    // Gear in a small box in the dial's bottom opening.
    std::string gear = (car.engineDead || car.engineOff) ? "-" : car.gear < 0 ? "R" : car.gear == 0 ? "N" : std::to_string(car.gear);
    float gs = px(2.2f * u), gw = 12 * gs * 0.5f + 8 * u, gh = 8 * gs + 6 * u, gy = cy + R - gh - 2 * u;
    pill(r, cx - gw / 2, gy, gw, gh, 0.0f, 0.0f, 0.0f, 0.55f);
    r.text(cx - r.textWidth(gear, gs) / 2, gy + 3 * u + gs * 0.5f, gs, gear, 1.0f, 0.72f, 0.25f);
    if (const DriveMode* m = car.isProp() ? nullptr : car.mode()) {  // the drive mode, a chip beside the gear
        const float cs = px(1.3f * u), cw = r.textWidth(m->name, cs) + 10 * u, ch = 8 * cs + 6 * u;
        const std::array<float, 3> mc = m->low ? std::array<float, 3>{1.0f, 0.75f, 0.2f} : m->front ? std::array<float, 3>{0.5f, 0.9f, 0.5f} : std::array<float, 3>{0.75f, 0.77f, 0.8f};
        const float chx = cx - gw / 2 - 6 * u - cw, chy = gy + (gh - ch) / 2;
        pill(r, chx, chy, cw, ch, mc[0] * 0.25f, mc[1] * 0.25f, mc[2] * 0.25f, 0.8f);
        r.text(chx + 5 * u, chy + 3 * u, cs, m->name, mc[0], mc[1], mc[2]);
    }
    if (!car.isProp() && hud.ctx.alpha(hudctx::E_TIRETEMP) > 0) {  // the tires' temperature: four marks beside the gear, laid out as they sit
        const HudFade fade(r, hud, hudctx::E_TIRETEMP);
        const float tw = 5 * u, th = std::max(3 * u, (gh - 8 * u) / 2), bw = 2 * tw + 12 * u, bx = cx + gw / 2 + 6 * u;
        pill(r, bx, gy, bw, gh, 0.0f, 0.0f, 0.0f, 0.55f);
        for (const Wheel& wh : car.wheels) {
            const int band = tireTempBand(car.treadType(wh), wh.tempC);
            const float c[3] = {band < 0 ? 0.35f : band > 0 ? 1.0f : 0.4f, band < 0 ? 0.62f : band > 0 ? 0.3f : 0.88f, band < 0 ? 1.0f : band > 0 ? 0.22f : 0.45f};
            const bool gone = !wh.mounted || wh.debeaded;
            r.rect(wh.left ? bx + 4 * u : bx + bw - 4 * u - tw, wh.front ? gy + 3 * u : gy + gh - 3 * u - th, tw, th,
                   gone ? 0.3f : c[0], gone ? 0.3f : c[1], gone ? 0.32f : c[2], 0.95f);
        }
    }
    if (hud.throttleCap < 0.995f && !car.isProp()) {  // the throttle limit, under the gear box
        const std::string txt = "THROTTLE " + throttle::percent(hud.throttleCap);
        const float ts2 = px(1.0f * u);
        const float tw2 = r.textWidth(txt, ts2);
        pill(r, cx - tw2 / 2 - 4 * u, gy + gh + 1 * u, tw2 + 8 * u, 8 * ts2 + 4 * u, 0.0f, 0.0f, 0.0f, 0.5f);
        r.text(cx - tw2 / 2, gy + gh + 3 * u, ts2, txt, 1.0f, 0.75f, 0.2f, 0.9f);
    }
    // Indicator lights above the hub.
    float is = px(1.0f * u), iy = cy - 26 * u;
    std::vector<std::pair<std::string, std::array<float, 3>>> lights;
    if (!car.tractionControl && car.spec->tractionFitted) lights.push_back({"TC OFF", {1.0f, 0.75f, 0.2f}});
    if (car.manualShift) lights.push_back({"MAN", {0.6f, 0.9f, 0.5f}});
    if (car.lockFront) lights.push_back({"F LOCK", {1.0f, 0.75f, 0.2f}});
    if (car.lockRear) lights.push_back({"R LOCK", {1.0f, 0.75f, 0.2f}});
    if (car.swayDisconnected) lights.push_back({"SWAY", {1.0f, 0.75f, 0.2f}});
    if (car.handbrake > 0.5 || car.parkBrake) lights.push_back({"P", {1.0f, 0.3f, 0.25f}});
    if (car.parkBrake && car.throttle > 0.3 && len(car.frame().vel) > 1.0) lights.push_back({"PARKING BRAKE ON", {1.0f, 0.2f, 0.15f}});
    if (car.ai) lights.push_back({"AUTO", {0.4f, 0.85f, 1.0f}});
    // What the tires are on (most of them), and how they're doing in it.
    if (!car.isProp()) {
        double sink = 0, clog = 0;
        const int s = dominantSurface(car, &sink, &clog);
        if (s >= 0) {
            const HudFade fade(r, hud, hudctx::E_TERRAIN);
            std::string txt = std::string(surfaceInfo((Surface)s).name) + "  " + tireTypeName(car.spec->tireType) + " TIRES";
            if (sink > 0.02) txt += "  SUNK " + std::to_string((int)std::lround(sink * 100)) + " CM";
            if (clog > 0.35) txt += "  TREAD PACKED WITH MUD";
            const float* dc = surfaceInfo((Surface)s).dust;
            const hudlayout::Column col = hudlayout::rightColumn(u, W, H, (float)txt.size(), 0, 0);
            const float ts2 = col.tagScale;
            txt = txt.substr(0, (size_t)hudlayout::fitChars(ts2, col.tag.w - 12 * u));  // (a line that's still too long at scale 1 is clipped)
            pill(r, col.tag.x, col.tag.y, col.tag.w, col.tag.h, 0.0f, 0.0f, 0.0f, 0.45f);
            r.text(col.tag.x + 6 * u, col.tag.y + 2 * u, ts2, txt, 0.55f + 0.45f * dc[0], 0.55f + 0.45f * dc[1], 0.55f + 0.45f * dc[2], 0.95f);
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
void drawDriveline(Renderer& r, const Car& car, const HudState& hud, float u, int W, int H) {
    if (car.isProp() || (car.spec->transfer == XFER_NONE && !car.spec->lockerFront && !car.spec->lockerRear)) return;
    const float R = hudlayout::kDialR * u, gaugeX = W - 2 * R - 26 * u;
    const float w = 46 * u, h = 70 * u, x = gaugeX - w - 26 * u, y = H - h - 40 * u;
    HudFade fade(r, hud, hudctx::E_DRIVELINE);
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
    r.hudAlpha = fade.old * hud.ctx.alpha(hudctx::E_PRESSURE);
    const float s = px(1.1f * u);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d PSI", (int)std::lround(car.tirePsi));
    const bool aired = car.tirePsi < car.spec->tirePsi - 1;
    r.text(x + w / 2 - r.textWidth(buf, s) / 2, y - 9 * u - 8 * s, s, buf, aired ? 1.0f : 0.8f, aired ? 0.72f : 0.82f, aired ? 0.25f : 0.86f, 0.95f);
    if (car.swayDisconnected) r.text(x + w / 2 - r.textWidth("SWAY OFF", s) / 2, y - 13 * u - 16 * s, s, "SWAY OFF", 1.0f, 0.72f, 0.25f, 0.95f);
}

// Winch line or tow strap in use: line out, pull, and how close it is to its limit.
void drawRecovery(Renderer& r, const Sim& sim, const HudState& hud, float u, int W) {
    const int ri = sim.ropeOn(sim.player);
    if (ri < 0) return;
    const Rope& rp = sim.ropes[ri];
    const float s = px(1.4f * u), x = 22 * u, y = hudlayout::topLeftY(hud.tl, hudlayout::TL_WINCH, u), h = 8 * s * 2 + 22 * u, w = std::max(250 * u, 18 * u + std::max(hudlayout::textW(27, s), hudlayout::textW(14, s) + 70 * u));  // (wide enough for the longest state and a bar)
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
        char hint[200];
        const double speed = sim.cars[sim.player].winchSpeed;
        if (pad::ui.last) std::snprintf(hint, sizeof hint, "%s + RT REEL IN   %s + LT PAY OUT   RIGHT STICK UP DOWN: SPEED %.2gX   ON FOOT, HOLD X AT AN END: UNHOOK", pad::hint("", pad::B_LB), pad::hint("", pad::B_LB), speed);
        else std::snprintf(hint, sizeof hint, "%s REEL IN   %s PAY OUT   %s %s SPEED %.2gX   ON FOOT, HOLD %s AT AN END: UNHOOK", K(KA_WINCH_IN).c_str(), K(KA_WINCH_OUT).c_str(), K(KA_WINCH_SLOWER).c_str(), K(KA_WINCH_FASTER).c_str(), speed, K(KA_INTERACT).c_str());
        // (The key hints stay left of the compass: past that, they break at a gap onto a second line.)
        std::string h1 = hint, h2;
        const int fit = hudlayout::fitChars(sm, hudlayout::topLeftMaxW(u, W) - 18 * u);
        if ((int)h1.size() > fit) {
            size_t cut = h1.rfind("   ", (size_t)fit);
            if (cut != std::string::npos && cut > 0) h2 = h1.substr(cut + 3), h1 = h1.substr(0, cut);
        }
        r.text(x + 8 * u, y + h + 4 * u, sm, h1, 0.8f, 0.82f, 0.86f, 0.9f);
        if (!h2.empty()) r.text(x + 8 * u, y + h + 6 * u + 8 * sm, sm, h2, 0.8f, 0.82f, 0.86f, 0.9f);
    }
}

// The anchor gear on you (anchors.h), small, above the fuel gauge: only in a truck with a winch.
void drawAnchors(Renderer& r, const Sim& sim, const HudState& hud, float u, int W, int H) {  // (the column above the dial: hudlayout.h)
    const Car& car = sim.cars[sim.player];
    if (car.isProp() || !(car.spec->extras & EXTRA_WINCH)) return;
    const std::string line = anchors::hudLine(sim.anchorStock);
    const hudlayout::Column col = hudlayout::rightColumn(u, W, H, 0, 0, (float)line.size());
    const float s = col.anchorScale, x = col.anchors.x + 8 * u, y = col.anchors.y + 5 * u;
    const HudFade fade(r, hud, hudctx::E_ANCHORS);
    pill(r, col.anchors.x, col.anchors.y, col.anchors.w, col.anchors.h, 0, 0, 0, 0.5f);
    // (The HUD's amber; red for a kind you've run out of.)
    float cx = x;
    for (int g = 0; g < GEAR_COUNT; g++) {
        const std::string part = std::string(anchors::gearShort((AnchorGear)g)) + " " + std::to_string(sim.anchorStock.n[g]) + (g + 1 < GEAR_COUNT ? " " : "");
        const bool none = sim.anchorStock.n[g] <= 0;
        r.text(cx, y, s, part, none ? 0.75f : 1.0f, none ? 0.45f : 0.75f, none ? 0.4f : 0.3f);
        cx += r.textWidth(part, s);
    }
}

// A bottom-centre prompt in its slot (hudlayout::prompt): shrinks, then clips, to the room it has.
void drawPrompt(Renderer& r, float u, int W, int H, int slot, const std::string& text, float br, float bg, float bb, float ba, float tr, float tg, float tb) {
    float s = 1;
    const hudlayout::Rect q = hudlayout::prompt(u, W, H, slot, (float)text.size(), &s);
    const int fit = hudlayout::fitChars(s, q.w - 14 * s);
    centeredPill(r, W / 2.0f, q.y, s, text.substr(0, (size_t)fit), br, bg, bb, ba, tr, tg, tb);
}

// Repair station status while parked on (or rolling onto) a pad.
void drawStation(Renderer& r, const Sim& sim, float u, int W, int H) {
    const Car& car = sim.cars[sim.player];
    if (car.station < 0 && !car.isProp() && car.needsFieldRepair() && len(car.frame().vel) < 0.5) {  // (fixes.h: hands-on)
        drawPrompt(r, u, W, H, 0, "SOMETHING'S BROKEN: GET OUT (" + K(KA_GET_IN_OUT) + ") AND FIX IT, OR GET TO A REPAIR STATION", 0.08f, 0.05f, 0.02f, 0.95f, 1.0f, 0.75f, 0.4f);
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
    drawPrompt(r, u, W, H, 0, buf, 0.02f, 0.08f, 0.04f, 0.95f, 0.6f, 1.0f, 0.6f);
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

void drawVehicleCard(Renderer& r, const Car& car, const HudState& hud, float u, int W, int H) {
    const bool schematic = !car.isProp() && (car.spec->transfer != XFER_NONE || car.spec->lockerFront || car.spec->lockerRear);  // (drawDriveline's)
    float s = px(2.0f * u), sm = hudlayout::cardScale(u, (float)car.spec->modSummary.size(), W, H, schematic), x = 22 * u;
    float y = H - 26 * u - 8 * s - 10 * sm - (car.isProp() ? 0 : 8 * sm + 5 * u) - (car.spec->modSummary.empty() ? 0 : 8 * sm + 5 * u);
    label(r, x, y, s, car.spec->name, 1, 1, 1);
    float dmg = (float)clampd(car.damage * 2.0, 0, 1), bw = 150 * u, by = y + 8 * s + 4 * u;
    char buf[32];
    {  // (the damage bar and its percentage: after a hit, or when it's bad)
        const HudFade fade(r, hud, hudctx::E_DAMAGE);
        r.rect(x, by, bw, 4 * u, 1, 1, 1, 0.22f);
        r.rect(x, by, bw * dmg, 4 * u, 0.95f, 0.3f + 0.45f * (1 - dmg), 0.15f, 0.95f);
        std::snprintf(buf, sizeof(buf), "DAMAGE %d%%", (int)std::lround(dmg * 100));
        label(r, x, by + 8 * u, sm, buf, 0.85f, 0.87f, 0.9f, 0.95f);
    }
    if (!car.isProp()) {
        const HudFade fade(r, hud, hudctx::E_CARINFO);  // (weight and mods: when stopped)
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
    if (battery::flat(car) && !battery::running(car)) warn.push_back("BATTERY FLAT: NEEDS A JUMP");  // the dash battery light
    else if (car.battery < battery::kLow) warn.push_back("BATTERY LOW");
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

bool projectToScreen(const View& v, int W, int H, const V3& p, float& sx, float& sy);

// ---- On-foot prompts (prompts.h) ---------------------------------------------------------------

// A key cap: the key (or pad button) in a lit box, then what it does. `lit`: can be done now. Returns the width.
float drawKeyLine(Renderer& r, float cx, float y, float s, const std::string& key, const std::string& what, bool lit, bool measureOnly = false) {
    const float capW = r.textWidth(key, s) + 10 * s, gap = 6 * s, whatW = r.textWidth(what, s), pad = 7 * s, h = 12 * s;
    const float w = capW + gap + whatW + 2 * pad;
    if (measureOnly) return w;
    const float x = cx - w / 2;
    pill(r, x, y, w, h, 0, 0, 0, 0.62f);
    pill(r, x + pad * 0.6f, y + 1.5f * s, capW, h - 3 * s, lit ? 1.0f : 0.45f, lit ? 0.78f : 0.4f, lit ? 0.3f : 0.38f, 0.95f);
    r.text(x + pad * 0.6f + 5 * s, y + 2.7f * s, s, key, 0.08f, 0.06f, 0.02f, 1);
    r.text(x + pad * 0.6f + capW + gap, y + 2.2f * s, s, what, lit ? 1.0f : 0.8f, lit ? 0.9f : 0.78f, lit ? 0.6f : 0.75f, 1);
    return w;
}

// The key (or the pad's button) the marker's job is done with.
std::string promptKeyName(const prompts::Marker& m, bool padNow, const std::string& holdWord) {
    if (m.key == prompts::KEY_GET_IN) return padNow ? "LB+Y" : K(KA_GET_IN_OUT);
    return (m.hold ? holdWord : std::string()) + (padNow ? "X" : K(KA_INTERACT));
}

// The markers (a small diamond at each spot there is something to do; the target's brighter, with corner brackets round
// the part) and the prompt near the crosshair: the key, what it does, and why not if it can't be done. Only on foot.
void drawFocus(Renderer& r, const prompts::Focus& fo, const Interact& ia, const Sim& sim, const View& view, const Walker& wk, int W, int H, bool padNow) {
    const float u = std::max(0.75f, H / 720.0f);
    const double k = 1.0 / std::tan(view.fovy / 2);
    const V3 chest = wk.p + V3(0, 1.0, 0);
    for (size_t i = 0; i < fo.markers.size(); i++) {
        const prompts::Marker& m = fo.markers[i];
        float sx, sy;
        if (!projectToScreen(view, W, H, m.at, sx, sy) || sx < 0 || sx > W || sy < 0 || sy > H) continue;
        if (m.car >= 0 && m.car < (int)sim.cars.size()) {  // (the far side of a car is behind its body)
            const Car& c = sim.cars[m.car];
            V3 a = m.at - c.frame().com, b = view.eye - c.frame().com;
            a.y = b.y = 0;
            if (len(a) > 0.3 && len(b) > c.spec->width * 0.6 && dot(norm(a), norm(b)) < -0.3 && !m.current) continue;
        }
        const double dist = len(m.at - chest);
        const float prox = (float)clampd(1.25 - dist / 4.0, 0.3, 1.0);  // 1 up close, dim by 5 m
        float cr = 1.0f, cg = 0.82f, cb = 0.35f;
        if (m.blocked) cr = 0.75f, cg = 0.6f, cb = 0.5f;
        if (m.key == prompts::KEY_GET_IN) cr = 0.55f, cg = 0.85f, cb = 1.0f;
        const float a = m.current ? 1.0f : prox * (m.far ? 0.55f : 0.9f);
        const float rr = (m.current ? 8.0f : 5.5f) * u;
        r.tri(sx, sy - rr * 1.35f, sx + rr * 1.35f, sy, sx, sy + rr * 1.35f, 0, 0, 0, 0.5f * a);
        r.tri(sx, sy - rr * 1.35f, sx - rr * 1.35f, sy, sx, sy + rr * 1.35f, 0, 0, 0, 0.5f * a);
        r.tri(sx, sy - rr, sx + rr, sy, sx, sy + rr, cr, cg, cb, a);
        r.tri(sx, sy - rr, sx - rr, sy, sx, sy + rr, cr, cg, cb, a);
        if (m.current) {  // brackets round the part
            const double z = std::max(0.5, dot(m.at - view.eye, norm(view.target - view.eye)));
            const float hb = (float)clampd(m.radius / z * k * H / 2.0, 16 * u, 90 * u), bl = hb * 0.5f, th = std::max(2.0f, 2.0f * u);
            for (int sxn = -1; sxn <= 1; sxn += 2)
                for (int syn = -1; syn <= 1; syn += 2) {
                    const float bx = sx + sxn * hb, by = sy + syn * hb;
                    r.rect(sxn < 0 ? bx : bx - bl, syn < 0 ? by : by - th, bl, th, cr, cg, cb, 0.95f);
                    r.rect(sxn < 0 ? bx : bx - th, syn < 0 ? by : by - bl, th, bl, cr, cg, cb, 0.95f);
                }
        }
    }
    const float s = px(1.4f * u), y = H / 2.0f + 40 * u;
    if (const prompts::Marker* m = fo.current()) {
        const bool tap = !m->hold && m->key == prompts::KEY_INTERACT;
        const float ps = tap ? px(1.8f * u) : s;  // (E at the back is the way in to everything: say it big)
        drawKeyLine(r, W / 2.0f, y, ps, promptKeyName(*m, padNow, "HOLD "), m->label, !m->blocked);
        float by = y + 12 * ps + 4 * u;
        if (!m->reason.empty()) {
            centeredPill(r, W / 2.0f, by, s, m->reason, 0.12f, 0.03f, 0.02f, 0.9f, 1.0f, 0.55f, 0.4f);
            by += 12 * s + 4 * u;
        }
        if (ia.progress > 0 && m->kind != IA_NONE) {
            const float bw = 220 * u, bx = W / 2.0f - bw / 2;
            r.rect(bx, by + 4 * u, bw, 5 * u, 0, 0, 0, 0.6f);
            r.rect(bx, by + 4 * u, bw * (float)ia.share(), 5 * u, 1.0f, 0.75f, 0.3f, 0.95f);
        }
    } else if (!fo.hint.empty()) {
        centeredPill(r, W / 2.0f, y, px(1.5f * u), fo.hint, 0, 0, 0, 0.75f, 0.85f, 0.88f, 0.9f);
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

// The engine job's spray and drip (fixes.h): water out of the plug holes while it's cranked over, the fouled oil off the drain plug.
static void engineJobFx(const Sim& sim, Effects& fx, const Interact& ia, const Handwork& hw, double dt) {
    const InteractOption& o = ia.current;
    if (ia.progress <= 0 || o.kind != IA_FIX || o.blocked || o.car < 0 || o.car >= (int)sim.cars.size() || !hw.hasWork) return;
    const RepairStep::Act act = engineStepAct(o.fix.kind, o.step);
    if (act == RepairStep::ACT_CRANK) fx.engineJob(1, hw.workAt, sim.cars[o.car].frame().fwd, dt);
    else if (act == RepairStep::ACT_DRAIN && ia.share() > 0.35) fx.engineJob(2, hw.workAt, sim.cars[o.car].frame().fwd, dt);
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
        const std::string hint = K(KA_REPAIR) + " TWICE: TRY AGAIN    " + K(KA_MENU) + ": LEVELS";
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
    // (The keys are the current bindings: what the player set under Settings > Controls > Keyboard keybinds.)
    const std::string sp = " ";
    const std::vector<std::pair<std::string, std::string>> keys = {
        {K(KA_THROTTLE) + sp + K(KA_BRAKE), "DRIVE / BRAKE / REVERSE"}, {K(KA_STEER_LEFT) + sp + K(KA_STEER_RIGHT), "STEER"},
        {K(KA_HANDBRAKE), "HANDBRAKE"},                                  {K(KA_REPAIR), "REPAIR IN PLACE (TWICE: RESPAWN)"},
        {K(KA_FLIP), "BACK ON ITS WHEELS"},                              {K(KA_NEXT_VEHICLE), "NEXT VEHICLE (ON FOOT: TRUCK INVENTORY, " + K(KA_INVENTORY) + ")"},
        {"HOLD " + K(KA_INTERACT), "ON FOOT: GRAB THE WINCH LINE, WALK IT OUT, HOOK IT (SAME TO UNHOOK)"},                           {K(KA_WINCH_IN) + " / " + K(KA_WINCH_OUT), "WINCH IN / OUT (" + K(KA_WINCH_SLOWER) + " " + K(KA_WINCH_FASTER) + " = SPEED)"},
        {K(KA_STRAP), "TOW STRAP: GET OUT, HOLD " + K(KA_INTERACT) + " AT A POINT"},       {K(KA_SWAY), "FRONT SWAY BAR DISCONNECT"},
        {K(KA_AIR_DOWN) + sp + K(KA_AIR_UP), "AIR DOWN / UP (STOPPED)"}, {K(KA_THROTTLE_CAP), "THROTTLE LIMIT: 100 50 25 10% (FOR CRAWLING)"},
        {K(KA_GARAGE), "GARAGE: BUY AND FIT PARTS"},
        {K(KA_GEAR_DOWN) + sp + K(KA_GEAR_UP), "SHIFT DOWN / UP (R N 1...)"}, {K(KA_MANUAL), "MANUAL / AUTOMATIC"},
        {K(KA_MAP), "MAP (WAYPOINTS, PINGS)"},                           {K(KA_CHAT), "CHAT"},
        {"MIDDLE CLICK", "PING: GO HERE (SHIFT: HELP)"},
        {K(KA_CROUCH), "ON FOOT: CROUCH (LOWER, SLOWER, LOOK UNDER THE TRUCK; REPAIRS STILL WORK)"},
        {K(KA_GET_IN_OUT), "GET OUT / IN (ON FOOT: " + K(KA_INTERACT) + " WINCH LINE, " + K(KA_FUEL_CAN) + " FUEL CAN)"},
        {K(KA_TRANSFER), mph ? "4X4: 2H 4H 4L (LOW: BELOW 5 MPH)" : "4X4: 2H 4H 4L (LOW: BELOW 8 KM/H)"},
        {K(KA_LIGHTS), "LIGHTS: AUTO, LOW, HIGH, OFF"},
        {K(KA_LOCK_FRONT) + sp + K(KA_LOCK_REAR), "FRONT / REAR LOCKER"}, {K(KA_AUTOPILOT), "AUTOPILOT (ESC > AUTOPILOT: WHERE)"},
        {K(KA_CAMERA), "CAMERA"},                                        {"MOUSE", "LOOK / ZOOM"},
        {K(KA_PAUSE), "PAUSE"},                                          {K(KA_PARK_BRAKE), "PARKING BRAKE"},
        {K(KA_TRACTION), "TRACTION CONTROL"},                            {K(KA_HORN), "HORN (HOLD)"},
        {K(KA_FULLSCREEN), "FULLSCREEN"},                                {K(KA_PHOTO), "HIDE THE HUD (PHOTOS)"},
        {K(KA_HUD_ALL), "HOLD: SHOW THE FULL HUD"},
        {K(KA_DEBUG_INFO), "DEBUG SCREEN (SHIFT: GRAPH)"},
        {K(KA_DEV_MENU), "DEV MENU: SPAWN, TELEPORT, DEBUG"},            {K(KA_MENU), "MENU: CARS, SETTINGS, QUIT"},
        {K(KA_HELP), "CLOSE"},
    };
    const int n = (int)keys.size(), rows = (n + 1) / 2;
    float s = px(1.4f * u), lh = 8 * s + 6 * u, keyW = 0, descW = 0;
    for (auto& k : keys) {
        keyW = std::max(keyW, r.textWidth(k.first, s));
        descW = std::max(descW, r.textWidth(k.second, s));
    }
    float colW = keyW + 14 * u + descW + 28 * u, pad = 18 * u;
    float w = colW * 2 + 2 * pad - 28 * u, h = rows * lh + 8 * s * 1.5f + 3 * pad;
    float x = (W - w) / 2, y = (H - h) / 2;
    pill(r, x, y, w, h, 0.02f, 0.025f, 0.035f, 0.8f);
    label(r, x + pad, y + pad, px(2.0f * u), "CONTROLS", 1.0f, 0.75f, 0.3f);
    for (int k = 0; k < n; k++) {
        float cx = x + pad + (k / rows) * colW, cy = y + pad * 2 + 8 * s * 1.5f + (k % rows) * lh;
        r.text(cx, cy, s, keys[k].first, 1.0f, 0.75f, 0.3f);
        r.text(cx + keyW + 14 * u, cy, s, keys[k].second, 0.9f, 0.91f, 0.93f);
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
        std::snprintf(buf, sizeof(buf), "%s  %s  %d/%d   F12: DETAILS", pb.scenario->name.c_str(), pb.passed() ? "PASSED" : "FAILED", ok, total);
    else
        std::snprintf(buf, sizeof(buf), "%s  %.1f S  %d/%d   F12: DETAILS", pb.scenario->name.c_str(), c.t, ok, total);
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

// Multiplayer: who's here (top right), and a name tag over each of their vehicles.
void drawPlayers(Renderer& r, const Sim& sim, const NetSession& net, const View& view, float u, int W, int H, bool fpsOn) {
    if (!net.active()) return;
    const float s = px(1.3f * u), lh = 8 * s + 6 * u;
    float y = hudlayout::playersList(u, W, 1, 0, fpsOn).y;  // (under the FPS block when that is up)
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
void drawCaches(Renderer& r, const Career& career, const HudState& hud, const std::vector<CacheSpot>& caches, const View& view, float u, int W, int H) {
    if (!career.on) return;
    const std::string cash = "CAREER  $" + std::to_string(career.cash);
    const float cs = px(1.4f * u);
    const float cy = hudlayout::topLeftY(hud.tl, hudlayout::TL_CASH, u);
    pill(r, 12 * u, cy, r.textWidth(cash, cs) + 16 * u, 8 * cs + 10 * u, 0.02f, 0.025f, 0.035f, 0.75f);
    r.text(20 * u, cy + 5 * u, cs, cash, 0.55f, 0.9f, 0.6f);
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

void drawHud(Renderer& r, const Sim& sim, const View& view, HudState& hud, const Playback* pb, const NetSession& net, int W,
             int H, float fps, double dt, bool mph, const Career& career, const std::vector<CacheSpot>& caches) {
    float u = std::max(0.75f, H / 720.0f);
    const Car& car = sim.cars[sim.player];
    drawGauges(r, car, hud, u, W, H, dt, mph);
    drawDriveline(r, car, hud, u, W, H);
    drawVehicleCard(r, car, hud, u, W, H);
    drawRecovery(r, sim, hud, u, W);
    drawAnchors(r, sim, hud, u, W, H);
    drawStation(r, sim, u, W, H);
    if (!hud.debugOn) drawPlayers(r, sim, net, view, u, W, H, hud.showFps);  // (F3 lists them itself)
    drawPilot(r, sim, view, u, W, H);
    drawCaches(r, career, hud, caches, view, u, W, H);
    drawTags(r, sim.world, view, u, W, H);

    // Top-center: compass, scenario status, persistent states, then toasts.
    float y = 12 * u;
    if (hud.levelOn) y = hudlayout::levelPanel(u, W).b() + 6 * u;  // (the clock panel sits right under the compass)
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
    // The proving ground's loop: the lap timer (shown while a lap runs, and a few seconds after).
    if (sim.world.map == MAP_PROVING_GROUND && World::withShowcase && hud.ctx.alpha(hudctx::E_LAPS) > 0.01f) {
        const HudFade fade(r, hud, hudctx::E_LAPS);
        auto mmss = [](double t) {
            char b[24];
            std::snprintf(b, sizeof b, "%d:%05.2f", (int)(t / 60), std::fmod(t, 60.0));
            return std::string(b);
        };
        std::string lap = hud.lapOn ? "LAP " + mmss(hud.plLap) + "  " + std::to_string(hud.plNext) + "/" + std::to_string(LapTimer::kChecks) : "CROSS THE LINE TO START A LAP";
        if (hud.plLast > 0) lap += "    LAST " + mmss(hud.plLast);
        if (hud.plBest > 0) lap += "    BEST " + mmss(hud.plBest);
        centeredPill(r, W / 2.0f, y, px(1.4f * u), lap, 0.02f, 0.03f, 0.05f, 0.9f, 1.0f, 0.9f, 0.6f);
        y += 20 * u;
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
        centeredPill(r, W / 2.0f, y, px(1.5f * u), "NEW BUILD READY: PRESS " + K(KA_DEV_RELOAD) + " TO LOAD IT (SAME MAP, VEHICLE AND SPOT)", 0.02f, 0.06f, 0.03f, 1,
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
                                              : K(KA_HELP) + ": CONTROLS   " + K(KA_MENU) + ": MENU";
        centeredPill(r, W / 2.0f, y, px(1.4f * u), line, 0.02f, 0.02f, 0.03f, a, 0.9f, 0.9f, 0.92f);
    }
    if (hud.showFps && !hud.debugOn) {
        std::snprintf(buf, sizeof(buf), "%d FPS", (int)std::lround(fps));
        const hudlayout::Rect fb = hudlayout::fpsBlock(u, W, 33);  // (right aligned in its block: hudlayout.h)
        const float s1 = px(1.3f * u), s2 = px(1.0f * u);
        label(r, fb.r() - r.textWidth(buf, s1), fb.y, s1, buf, 0.8f, 0.9f, 0.8f);
        // (Which chip draws it: on a laptop with two, the built-in one is far slower.)
        if (!hud.gpu.empty()) r.text(fb.r() - r.textWidth(hud.gpu, s2), fb.y + 8 * s1 + 4 * u, s2, hud.gpu, 0.7f, 0.75f, 0.8f);
        std::snprintf(buf, sizeof(buf), "PHYSICS %.1f MS  DRAW CPU %.1f MS", hud.physicsMs, hud.drawCpuMs);
        r.text(fb.r() - r.textWidth(buf, s2), fb.y + 8 * s1 + 8 * s2 + 8 * u, s2, buf, 0.7f, 0.75f, 0.8f);
    }
    if (hud.devHotkeys) {  // the key mode: 1-7, I, K, N... are the dev tools (` flips back)
        const float ds = px(1.2f * u);
        centeredPill(r, 10 * u + (r.textWidth("DEV KEYS", ds) + 14 * ds) / 2, 8 * u, ds, "DEV KEYS", 0.35f, 0.18f, 0.0f, 1, 1.0f, 0.75f, 0.3f);
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

// SIGTERM / SIGINT. SDL's own handler only queues an SDL_QUIT, which a hung main loop never reads (so `timeout` never killed it).
// Ours: normal play sets a flag the loop checks (clean quit), a second signal exits at once; a scripted run exits at the first.
static volatile std::sig_atomic_t g_termSignals = 0;
static bool g_termScripted = false;
static void onTermSignal(int sig) {
    if (g_termScripted || g_termSignals++ >= 1) _exit(128 + sig);
}

int main(int argc, char** argv) {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);  // (a multiplayer peer dropping mid-send; macOS has no MSG_NOSIGNAL)
#endif
    Options opt = parseArgs(argc, argv);
    if (!opt.campDump.empty()) {
        const bool ok = menuscene::dump(opt.campDump);
        std::printf("%s %s\n", ok ? "wrote" : "could not write", opt.campDump.c_str());
        return ok ? 0 : 1;
    }
    const bool scripted = !opt.shotFrames.empty() || opt.bench > 0 || opt.loadTour;  // (the default settings, no vsync, a fixed 60 Hz step)
    // Unattended runs get a hang watchdog: no frame for 60 s (240 s while loading), or the whole run over CZ_WATCHDOG_TOTAL s (default 3600).
    // (CZ_WATCHDOG_STALL=S shortens the stall limit; CZ_TEST_STALL_FRAME=N makes the loop sleep for ever at frame N: the tests' hooks.)
    // The new main menu (mainmenu.h) with its camp scene behind it: a plain launch (nothing asked for), or --main-menu. Scripted and test runs never build it
    // (CZ_NO_MENU_SCENE=1 turns it off for a plain launch too).
    const bool wantCamp = (opt.mainMenu >= 0 && !opt.lab) ||
                          (!scripted && !std::getenv("CZ_NO_MENU_SCENE") && opt.scenario.empty() && opt.script.empty() && opt.map < 0 && !opt.spawnSet && opt.spawnSection < 0 &&
                           opt.hostPort < 0 && opt.join.empty() && opt.menu < 0 && opt.resumeModel < 0 && opt.level.empty() && !opt.editor && !opt.newSection && opt.resumeEdit.empty() &&
                           !opt.lab && !opt.loadTour);
    const bool unattended = scripted || !opt.scenario.empty() || !opt.script.empty();
    g_termScripted = unattended;
    std::unique_ptr<wd::FrameWatchdog> watchdog;
    if (unattended) {
        const char* es = std::getenv("CZ_WATCHDOG_STALL");
        const char* et = std::getenv("CZ_WATCHDOG_TOTAL");
        const double stall = es ? std::atof(es) : 60.0;
        watchdog.reset(new wd::FrameWatchdog(stall, et ? std::atof(et) : 3600.0, std::max(240.0, stall)));
    }
    const char* stallEnv = std::getenv("CZ_TEST_STALL_FRAME");
    const int testStallFrame = stallEnv ? std::atoi(stallEnv) : -1;
    blockOnTemplates = !opt.shotFrames.empty() || opt.bench > 0;  // (screenshots and the bench show every car; the tour wants the real, waiting behaviour)
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
    const bool settingsLoaded = !scripted && settings.load(settingsPath);
    if (opt.physicsThreads >= 0) settings.physicsThreads = opt.physicsThreads;
    if (!opt.name.empty()) settings.playerName = opt.name;
    if (settings.playerName.empty()) {
        const char* user = std::getenv("USER");
        settings.playerName = user && *user ? user : "DRIVER";
        for (char& ch : settings.playerName) ch = (char)std::toupper((unsigned char)ch);
        if (settings.playerName.size() > 16) settings.playerName.resize(16);
    }
    if (opt.quality >= 0) applyPreset(settings.gfx, opt.quality);
    const bool autoDetect = !scripted && opt.quality < 0 && shouldAutoDetect(settingsLoaded, settings.gfxChosen);
    if (opt.ground >= 0) settings.gfx.ground = std::min(opt.ground, 3);
    if (opt.props >= 0) settings.gfx.props = std::min(opt.props, 3);
    if (opt.grass >= 0) settings.gfx.grass = std::min(opt.grass, 3);
    if (opt.adaptive >= 0) settings.adaptiveQuality = opt.adaptive != 0;
    if (opt.mph) settings.mph = true;
    if (opt.fullHud) settings.fullHud = true;
    if (opt.throttleCap > 0) {
        settings.throttle.loadSetting("throttlecap", opt.throttleCap), settings.throttle.loadSetting("throttleresp", opt.throttleResp);
        settings.throttle.loadSetting("throttlepad", opt.throttlePad);
    }
    if (!mapListed(settings.map)) settings.map = MAP_PROVING_GROUND;  // (a map that's been taken off the list)
    if (opt.mapHalf >= 100 && opt.mapHalf <= 8192) World::halfSizeOverride = std::floor(opt.mapHalf);
    World::withShowcase = opt.scenario.empty();  // (the model yard and section park laid out on the proving ground: not under a test's replay)
    if (opt.map >= 0) settings.map = opt.map < MAP_COUNT ? opt.map : MAP_PROVING_GROUND;  // (--map with no such map: the proving ground)
    if (!opt.scenario.empty()) settings.map = MAP_PROVING_GROUND;  // the tests are built for it
    if (opt.lab) settings.map = MAP_PROVING_GROUND;                // (the model lab builds its own studio)
    if (opt.fullscreen) settings.fullscreen = true;

    {  // (the system clipboard for every text field: Ctrl+V pastes, Ctrl+C copies)
        te::clipboardGetHook = [] {
            char* t = SDL_GetClipboardText();
            std::string s = t ? t : "";
            if (t) SDL_free(t);
            return s;
        };
        te::clipboardSetHook = [](const std::string& s) { SDL_SetClipboardText(s.c_str()); };
    }
    static bool testCtrl = false;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | (opt.audio ? (uint32_t)SDL_INIT_AUDIO : 0u)) != 0) {
        fail(std::string("SDL_Init failed: ") + SDL_GetError());
        return 1;
    }
    std::signal(SIGTERM, onTermSignal);  // (after SDL_Init, which installs its own)
    std::signal(SIGINT, onTermSignal);
    if (!opt.clipboard.empty()) SDL_SetClipboardText(opt.clipboard.c_str());
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
    {  // (compiled shaders kept between launches; scripted runs only with CZ_SHADER_CACHE=DIR, so they never write to the config)
        const char* dir = std::getenv("CZ_SHADER_CACHE");
        setShaderCacheDir(dir ? dir : scripted || std::getenv("CZ_NO_SHADER_CACHE") ? "" : configDir() + "/shadercache");
    }
    const char* glRenderer = (const char*)gl::GetString(GL_RENDERER);
    const char* glVendor = (const char*)gl::GetString(GL_VENDOR);
    HardwareInfo hardware;  // (for the first-launch guess and DETECT AUTOMATICALLY)
    hardware.vendor = glVendor ? glVendor : "", hardware.renderer = glRenderer ? glRenderer : "";
    queryHardware(hardware);
    {
        GLint kb[4] = {-1, 0, 0, 0};
        if (hardware.vendor.find("NVIDIA") != std::string::npos) gl::GetIntegerv(0x9047 /* GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX */, kb);
        if (kb[0] > 0 && hardware.vendor.find("NVIDIA") != std::string::npos) hardware.vramMb = kb[0] / 1024;
    }
    std::printf("hardware: %d cores, %ld MB RAM, VRAM %ld MB, guess %s\n", hardware.cores, hardware.ramMb, hardware.vramMb,
                presetName(guessPreset(hardware)));
    if (autoDetect) {
        const int guess = guessPreset(hardware);
        applyPreset(settings.gfx, guess);
        if (guess != Q_POTATO) settings.gfx.renderScale = firstLaunchRenderScale(hardware, guess);
    }
    // Honor the saved setting on every GPU; vendor names do not establish driver performance. VSync asks for adaptive
    // sync first (a frame that misses the refresh shows at once, torn, instead of waiting a whole refresh and halving
    // the frame rate), plain vsync where the driver hasn't got it.
    auto setVsync = [&](bool on) {
        if (!on) SDL_GL_SetSwapInterval(0);
        else if (SDL_GL_SetSwapInterval(-1) != 0) SDL_GL_SetSwapInterval(1);
    };
    setVsync(settings.vsync && opt.vsync && !scripted);
    std::printf("Swap interval: %d\n", SDL_GL_GetSwapInterval());
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
    // The sim starts on the proving ground. When another map or the menu's camp replaces it straight away, it's built bare: the
    // model yard and the section park (seconds of work on a build's first run) aren't laid out only to be thrown away.
    const bool showcaseOn = World::withShowcase;
    World::withShowcase = showcaseOn && !wantCamp && settings.map == MAP_PROVING_GROUND;
    Sim sim;
    World::withShowcase = showcaseOn;
    const int physicsThreads = resolvePhysicsThreads(settings.physicsThreads);
    try {
        sim.setPhysicsThreads(physicsThreads);
        sim.trafficLod = opt.trafficLod;
        std::printf("Physics: %d threads (small scenes use the main thread)\n", physicsThreads);
    } catch (const std::system_error& error) {
        std::fprintf(stderr, "Physics workers unavailable; using one thread: %s\n", error.what());
    }
    Level campLevel;  // the main menu's camp (menuscene.h): the player's file, the game's, or made by code
    V3 campFire;
    if (wantCamp) {  // (the menu's camp instead of the saved map: choosing a map loads it)
        campLevel = menuscene::load();
        menucamp::fireOf(campLevel, campFire);
        sim.world = menuscene::buildWorld(campLevel);
    } else if (settings.map != MAP_PROVING_GROUND) sim.world = World(settings.map);
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
    bool previewMenu = false;  // the menu is shown as a preview from the camp editor (Esc goes back to it)
    bool campWorld = false;  // sim.world is the main menu's camp (a map load, a level or the editor ends it)
    // Three of the listed vehicles parked round the camp's fire: the player's car becomes the first (the lineup's others are dropped).
    auto parkCamp = [&]() {
        sim.clearWorldLeftovers(sim.player);
        const std::vector<int>& lv = listedVehicles();
        menucamp::Meta meta = menucamp::metaOf(campLevel);
        if (meta.vehicles == 0) meta.vehicles = 1, meta.veh[0].x = campFire.x + 4, meta.veh[0].z = campFire.z - 2;  // (no stand-ins left in the level: one parked by the fire)
        const V3 paint[3] = {V3(0.55, 0.12, 0.08), V3(0.16, 0.3, 0.2), V3(0.5, 0.2, 0.1)};
        sim.swapPlayerVehicle(garage.spec(lv[meta.veh[0].slot]));
        for (int k = 0; k < meta.vehicles; k++) {
            const menucamp::Vehicle& v = meta.veh[k];
            const double yaw = v.yawDeg * M_PI / 180;
            if (k > 0) sim.addCar(garage.spec(lv[v.slot]), V3(v.x, 0, v.z), yaw, paint[k]);
            Car& c = sim.cars[k == 0 ? sim.player : (int)sim.cars.size() - 1];
            c.respawn(V3(v.x, sim.world.clearY(v.x, v.z) + 0.05, v.z), yaw);
            c.setDown(sim.world);
            c.spawnPos = c.frame().com, c.spawnYaw = yaw;
            c.handbrake = 1;
        }
        sim.paused = false;  // (a moment of physics settles them on their springs; then they stand still)
        for (int i = 0; i < 100; i++) sim.advance(1.0 / 60, 4);
        for (Car& c : sim.cars) c.handbrake = 1;
        sim.paused = true;
        campWorld = true;
    };
    fitGarage();
    if (wantCamp) parkCamp();
    // The lineup's drawing templates are made on worker threads while the window's shaders build (the first frame waits for yours).
    for (const Car& c : sim.cars)
        if (!c.isProp()) prebuildVisual(*c.spec, c.color);
    shop::Shop garageShop;     // the garage (garageshop.h)
    std::string shopToast;     // what the last press in it said, shown in it
    double shopToastT = 0;
    bool shopDrag = false, shopRecal = false;
    View shopView;             // the garage camera, eased toward where it wants to be
    double shopBlend = 0;      // 0: the chase view it came from .. 1: the garage view
    Playback playback;
    throttle::Pedal throttlePedal;
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
            opt.spawnSet = true, opt.spawnX = s.spawn.x, opt.spawnZ = s.spawn.z, opt.spawnHeading = s.spawnYaw * 180 / M_PI;  // (on its pad, a little before the entry)
            std::printf("section %d %s: entry %.1f,%.1f start %.1f,%.1f heading %.1f deg\n", opt.spawnSection, s.name.c_str(), s.entry.x, s.entry.z, opt.spawnX, opt.spawnZ, opt.spawnHeading);
        } else std::printf("no section %d: the proving ground has %zu\n", opt.spawnSection, sim.world.sections.size());
    }
    if (opt.spawnSet) {
        Car& c = sim.cars[sim.player];
        const V3 home = c.spawnPos;
        const double homeYaw = c.spawnYaw;
        c.respawn(V3(opt.spawnX, sim.world.clearY(opt.spawnX, opt.spawnZ) + 0.05, opt.spawnZ), opt.spawnHeading * M_PI / 180);
        // (--spawn only places you: a hot reload passes where you were, and home must stay the map's spawn pad. A
        // section start is a real start, so there it becomes home.)
        if (opt.spawnSection < 0) c.spawnPos = home, c.spawnYaw = homeYaw;
    }
    Renderer renderer;
    std::string err;
    uint64_t drawnWorld = sim.world.stamp;
    const uint64_t initStart = SDL_GetPerformanceCounter();
    if (!renderer.init(sim.world, err)) {
        fail("renderer init failed:\n" + err);
        return 1;
    }
    renderer.settings = settings.gfx;
    renderer.preload(sim.world);
    std::printf("renderer: ready in %.0f ms (%d shader programs compiled, %d from the cache)\n",
                (SDL_GetPerformanceCounter() - initStart) * 1000.0 / SDL_GetPerformanceFrequency(), gShaderCacheStats.compiled, gShaderCacheStats.hits);
    // (a scenario has no cars until its first frame: nothing to wait for, and nobody would ever make it)
    if (!sim.cars.empty() && sim.player >= 0 && sim.player < (int)sim.cars.size()) {
        prebuildVisual(*sim.cars[sim.player].spec, sim.cars[sim.player].color);  // (asked for, in case nothing has yet)
        while (!visualReady(*sim.cars[sim.player].spec)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
    LapTimer lapTimer;  // (best laps by vehicle: configDir()/laps.txt)
    lapTimer.savePath = LapTimer::defaultPath();
    lapTimer.load(lapTimer.savePath);
    uint64_t lapStamp = ~0ull;
    DebugScreen dbg;  // F3
    const std::string buildText = buildDescription();
    std::string peerAddress;  // the game joined, for the debug screen
    double dbgSimSeconds = 0, dbgMsSum = 0;  // (the screen's own CPU time, for CZ_BENCH)
    int dbgMsN = 0;
    int dbgSubsteps = 0;
    hud.gpu = gpuName;
    hud.photo = opt.photo;
    if (scripted) hud.hintTime = 0;
    hud.showHelp = opt.showHelp;
    if (opt.debugScreen) dbg.toggle(opt.debugScreen == 2);
    if (opt.mods[0] >= 0 && sim.cars[sim.player].spec->model >= 0) {
        int v = sim.cars[sim.player].spec->model;
        ModSet ms;
        for (int k = 0; k < MOD_SLOTS; k++) ms.pick[k] = std::max(0, opt.mods[k]);
        Car& c = sim.cars[sim.player];
        c.spec = &tunedSpec(v, ms);
        c.respawn(c.spawnPos, c.spawnYaw);
    }
    // Multiplayer session (see net.h).
    NetSession net;
    net.build = buildId();  // (told to the others: another build can handle differently)
    net::Finder finder;  // games on the local network, while the multiplayer page is open
    View lastView;
    // The pause menu, and putting its settings into effect.
    Effects fx;
    PauseMenu menu;
    mainmenu::Nav mainNav;        // the new main menu's state (mainmenu.h)
    mpmenu::Nav mpNav;            // the multiplayer pages (mpmenu.h): the PauseMenu's page 2, in the main menu's style
    mpmenu::View mpView;
    bool mpWasShown = false, joinByCode = false;
    std::string joinWhat;         // the last join: a code or an address (RETRY, RECENT)
    bool classicTitle = false;    // the old title page is up (DEV > CLASSIC MENU, or --menu 9) instead of the new one
    float mainFade = 0;           // the new menu fading in
    // The new main menu is the title screen's page 0 over the camp (the settings, multiplayer and levels pages are still the PauseMenu's).
    auto mainMenuShown = [&]() { return menu.open && menu.title && menu.pageNow() == 0 && campWorld && !classicTitle; };
    auto mpShown = [&]() { return menu.open && menu.pageNow() == 2 && !(menu.title && classicTitle); };
    auto campBehind = [&]() { return campWorld && menu.open && menu.title; };
    bool menuWasPaused = false, garageFromMenu = false;
    float menuBlur = 0;
    double clock = 0;
    horn::Sender hornSender;  // your horn, as told to the others (horn.h)
    horn::Remote hornRemote;  // theirs
    repairsync::Sender repairOut;  // what you hold and the work you do on foot, the jacks under yours, as told to the others (repairsync.h)
    repairsync::Remote repairIn;   // theirs
    // Time of day: the clock (hours) carries on from last time; --time holds it still.
    double dayHours = opt.timeHours >= 0 ? opt.timeHours : settings.clockHours;
    double levelHours = -1;  // the playing level's own hour (-1: none)
    bool dayFromHost = false;  // in someone else's game: the sky is theirs (and our own clock waits in settings)
    int lampMode = opt.lamps >= 0 && opt.lamps < LAMP_MODES ? opt.lamps : LAMPS_AUTO;  // the player's headlight switch
    int displayHz = 0;  // the window's display refresh rate (0 unknown), for FPS TARGET: MATCH DISPLAY
    auto fpsCap = [&]() { return resolveFpsTarget(settings.fpsTarget, displayHz); };
    auto applySettings = [&]() {
        { SDL_DisplayMode dm = {}; displayHz = SDL_GetCurrentDisplayMode(std::max(SDL_GetWindowDisplayIndex(win), 0), &dm) == 0 ? dm.refresh_rate : 0; }

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
        setVsync(settings.vsync && opt.vsync && !scripted);
        adaptiveQuality.init(settings.gfx, adaptiveAim(fpsCap()));
        sim.realRecovery = settings.realRecovery;
        for (Car& c : sim.cars) if (!c.remote) c.winchSpeed = settings.winchSpeed;
        sim.steeringAssist = settings.steeringAssist;
    };
    auto saveSettings = [&]() {
        if (!scripted) settings.save(settingsPath);
    };
    // Graphics detection (perf.h): the hardware guess is on already (first launch) or applied here (the button); the probe then
    // measures a few seconds of the running game and steps the preset toward the FPS target.
    QualityProbe probe;
    bool probePending = autoDetect;  // (waits for the world to be drawing)
    auto startProbe = [&]() {
        probe.start(guessPreset(hardware), adaptiveAim(fpsCap()));
        applyPreset(settings.gfx, probe.preset());
        applySettings();
        hud.toast(std::string("DETECTING GRAPHICS: ") + presetName(probe.preset()) + "?");
    };
    applySettings();
    gKeys = &settings.keys;
    // The garage: in a career only at a repair station; in sandbox anywhere. False (and says why) if it didn't open.
    auto openGarage = [&]() {
        const Car& c = sim.cars[sim.player];
        if (c.isProp() || c.spec->model < 0) {
            hud.toast("NO GARAGE FOR THIS VEHICLE");
            return false;
        }
        const int at = sim.stationAt(c.frame().com);
        if (career.on && at < 0) {
            hud.toast("THE GARAGE IS AT THE REPAIR STATIONS: DRIVE ONTO ONE'S PAD");
            return false;
        }
        garageShop.begin(c.spec->model, garage.fitted[c.spec->model], career, career.on, at >= 0);
        shopToast.clear();
        shopDrag = false;
        shopBlend = 0;
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
    // What a press in the garage did: say it there, save what changed, and back to the pause menu if it came from there.
    auto shopResult = [&](const shop::Result& r) {
        if (!r.toast.empty()) shopToast = r.toast, shopToastT = 3.0;
        if (r.saveCareer && !scripted) career.save(Career::defaultPath());
        if (r.fitted) {
            garage.fitted[garageShop.vehicle] = garageShop.fitted;
            if (!scripted) garage.save(garagePath);
            shopRecal = garageShop.recal;
        }
        if (r.close) {
            shopDrag = false;
            if (garageFromMenu) openMenu();  // back where you came from
            garageFromMenu = false;
        }
    };
    if (opt.showGarage && scripted && std::getenv("CZ_SHOP_STATION") && !sim.world.stations.empty()) {  // (screenshots: onto the first repair station's pad)
        const Station& st = sim.world.stations[0];
        sim.teleportPlayer(st.p.x, st.p.z, st.yaw);
    }
    if (opt.showGarage && openGarage()) {
        // (Screenshots: CZ_SHOP=CAT[,PART[,try]] opens on a category, a part in it, and tries it on the truck; CZ_SHOP_SPIN=rad turns the model.)
        if (const char* sv = std::getenv("CZ_SHOP")) {
            int c = 0, p = -1;
            char t[16] = "";
            std::sscanf(sv, "%d,%d,%15s", &c, &p, t);
            garageShop.setCat(c);
            if (p >= 0) garageShop.focus = shop::F_ITEMS, garageShop.setSel(p);
            garageShop.previewing = p >= 0 && std::string(t) == "try";
        }
        if (const char* sp = std::getenv("CZ_SHOP_SPIN")) garageShop.spin = std::atof(sp), garageShop.idle = -1e9;
    }
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
    float padRumbleSent[2] = {0, 0};  // (the last rumble command, so a pulse goes out the frame it changes)
    if (opt.pad && !scripted && !std::getenv("CZ_NO_PAD")) padDev.start();
    int shiftForce = -1;  // a key press resolved to an action: whether its default is a Shift combo (Shift+L: the lights), whatever Shift is doing
    auto shiftDown = [&]() {
        const uint8_t* ks = SDL_GetKeyboardState(nullptr);
        if (shiftForce >= 0) return shiftForce != 0;
        return padShift || ks[SC_LSHIFT] || ks[SC_RSHIFT];
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
        if (c.isProp() || c.remote) return;
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
    // A vehicle about to appear (a spawn, a swap) has its drawing template built on a worker first: `act` runs the frame it is ready
    // (the game goes on meanwhile), so nothing hitches and nothing pops in half-made.
    std::vector<std::pair<const VehicleSpec*, std::function<void()>>> afterVisual;
    auto whenVisualReady = [&](const VehicleSpec& vs, std::function<void()> act) {
        if (visualReady(vs) || blockOnTemplates) return act();
        prebuildVisual(vs, sim.cars[sim.player].color);
        afterVisual.push_back({&vs, std::move(act)});
    };
    // A new world went in (a map, a level, the editor, the camp): what the game keeps of the last one goes with it (a job, being on
    // foot, the crates). Set below, where that state lives. worldCareer: the mode (the save) this world's vehicles were put out in.
    std::function<void()> worldChanged;
    bool worldCareer = career.on;
    // Loads a map, keeping the vehicle you were in.
    auto loadMap = [&](int m) {
        World::useLocalEdits = net.role != NetSession::CLIENT;  // (a guest builds the host's: its edited sections came with the welcome, World::setSessionSections)
        const int model = std::max(0, sim.cars[sim.player].spec->model);
        campWorld = false;
        sim.world = World(m);
        sim.setup();
        if (worldChanged) worldChanged();
        fitGarage();
        if (model != sim.cars[sim.player].spec->model) sim.swapPlayerVehicle(garage.spec(model));
        sim.paused = false;
        renderer.setWorld(sim.world);
        drawnWorld = sim.world.stamp;
        hud.bestKmh = 0;
        dbgSimSeconds = 0;
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
    auto startHost = [&](int port, bool quietFail = false) {
        const std::string session = !settings.sessionName.empty() ? settings.sessionName
                                    : (settings.playerName.empty() ? std::string("PLAYER") : settings.playerName) + "'S GAME";
        net.password = !opt.password.empty() ? opt.password : settings.netPassword;
        if (net.host(port, settings.playerName, sim.world.map, settings.maxPlayers, session)) {
            net.setSections(sim.world.editedSections);  // (your edited park sections go to whoever joins: the same world for all)
            net.adopt(sim);
            hud.toast("HOSTING " + session + " ON " + mapName(sim.world.map) + ": FRIENDS ON YOUR NETWORK SEE IT IN JOIN");
            std::printf("hosting %s on port %d\n", mapName(sim.world.map), port);
        } else {
            if (quietFail && net.portInUse) return;
            if (net.portInUse) net.error = "PORT " + std::to_string(port) + " IS IN USE: PICK ANOTHER IN THE MULTIPLAYER MENU";
            hud.toast("CAN'T HOST: " + net.error);
            std::printf("can't host: %s\n", net.error.c_str());
        }
    };
    // ---- multiplayer: joining without freezing (the menu shows a CONNECTING card meanwhile)
    int joinState = 0;  // 0 not joining, 1 connecting, 2 it failed (the card says why)
    // It failed: the card says why; over a missing or wrong password it asks for the password right there.
    auto joinFailed = [&]() {
        joinState = 2;
        menu.joinError = net.passwordIssue == NetSession::PW_NEEDED ? "THIS GAME NEEDS A PASSWORD" : net.passwordIssue == NetSession::PW_WRONG ? "WRONG PASSWORD" : net.error;
        for (char& ch : menu.joinError) ch = (char)std::toupper((unsigned char)ch);
        menu.joinNeedsPw = net.passwordIssue != NetSession::PW_OK;
        if (menu.joinNeedsPw) menu.askPassword(settings);
    };
    auto beginJoin = [&](const std::string& address, const std::string& label) {
        menu.joining = true;
        menu.joinLabel = label.empty() ? address : label;
        menu.joinAddr = address;
        menu.joinError.clear();
        menu.joinNeedsPw = false;
        {
            std::string h;
            if (!net::parseAddress(address, h, menu.joinPort)) menu.joinPort = net::kDefaultPort;  // (for the failed card's port help)
        }
        settings.joinAddress = address;  // (the last server, whether typed or picked from the list)
        joinByCode = false;
        joinWhat = address;
        net.password = net::joinPassword(opt.password, settings.joinPassword);
        peerAddress = address;
        if (net.beginJoin(address, settings.playerName)) {
            joinState = 1;
        } else {
            joinFailed();
        }
        std::printf("joining %s\n", address.c_str());
    };
    auto rendezvousAddress = [&]() { return !opt.rendezvous.empty() ? opt.rendezvous : !settings.rendezvous.empty() ? settings.rendezvous : std::string(net::kDefaultRendezvous); };
    auto beginCodeJoin = [&](const std::string& code) {
        menu.joining = true;
        menu.joinLabel = "CODE " + rv::formatCode(rv::normalizeCode(code));
        menu.joinError.clear();
        menu.joinNeedsPw = false;
        net.password = net::joinPassword(opt.password, settings.joinPassword);
        net.rendezvous = rendezvousAddress();
        joinByCode = true;
        joinWhat = rv::formatCode(rv::normalizeCode(code));
        if (net.beginJoinCode(code, settings.playerName)) joinState = 1;
        else joinFailed();
        std::printf("joining with code %s\n", joinWhat.c_str());
    };
    auto rememberJoin = [&](const std::string& what) {  // (RECENT on the multiplayer page)
        if (what.empty()) return;
        auto& r = settings.recentJoins;
        r.erase(std::remove(r.begin(), r.end(), what), r.end());
        r.insert(r.begin(), what);
        if (r.size() > 3) r.resize(3);
        saveSettings();
    };
    // ---- end multiplayer joining
    auto startJoin = [&](const std::string& address) {
        net.password = net::joinPassword(opt.password, settings.joinPassword);
        peerAddress = address;
        if (!net.join(address, settings.playerName)) {
            hud.toast("CAN'T JOIN: " + net.error);
            std::printf("can't join %s: %s\n", address.c_str(), net.error.c_str());
            return;
        }
        World::setSessionSections(net.sections);
        if (net.map != sim.world.map || sim.world.parkEdited || !net.sections.empty() || campWorld) loadMap(net.map);
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
        levelList.erase(std::remove_if(levelList.begin(), levelList.end(), [](const Level& lv) { return !mapForLevels(lv.map); }), levelList.end());  // (on maps taken off the list)
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
    bool editorSuspended = false;  // the editor is waiting while its level is tried out (F1 comes back to it)
    auto playLevel = [&](const Level& lv, const EditCore::PlaySpot* spot = nullptr) {
        editorSuspended = false;
        Car& me = sim.cars[sim.player];
        me.pilot.active = false;
        me.ai = false;
        const int model = lv.vehicle >= 0 && lv.vehicle < (int)vehicleCatalog().size() ? lv.vehicle : std::max(0, me.spec->model);
        campWorld = false;
        sim.world = levelWorld(lv);
        applyLevel(lv, sim.world);
        addGateMarkers(lv, sim.world);
        // Just you on the grid (and the other players, in a session): nothing of the last world's.
        sim.clearWorldLeftovers(sim.player, net.active());
        if (worldChanged) worldChanged();
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
            if (spot && spot->here) p = spot->p, yaw = spot->yaw;  // (a playtest from where the editor's camera looked)
            else if (lv.hasStart) LevelRun::gridSlot(lv, runSlot(), p, yaw);
            else if (!sim.world.spawnSlot(runSlot(), p, yaw)) p += V3(std::sin(yaw), 0, std::cos(yaw)) * (6.5 * runSlot());  // (each player their own place on the pad)
            Car& c = sim.cars[sim.player];
            c.respawn(V3(p.x, sim.world.clearY(p.x, p.z) + 0.05, p.z), yaw);
            c.setDown(sim.world);
            c.spawnPos = c.frame().com, c.spawnYaw = yaw;
            hud.toast(spot && spot->here ? lv.name + ": FROM HERE" : lv.name + ": FREE DRIVE");
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
    // The spots that count as finding something on this map: its table of places (places.h) and the autopilot's named spots, except the
    // ones the table already has (within 40 m of one).
    auto discoverySpots = [&]() {
        std::vector<DiscoverSpot> spots = discoverSpots(sim.world.map);
        const size_t fromTable = spots.size();
        for (const Place& pl : pilotPlaces(sim, net)) {
            if (pl.kind != 0) continue;
            bool have = false;
            for (size_t k = 0; k < fromTable; k++) have |= std::hypot(spots[k].p.x - pl.p.x, spots[k].p.z - pl.p.z) < 40;
            if (!have) spots.push_back({pl.name, pl.p, 30, -1});
        }
        return spots;
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
    prompts::Prompter prompter;  // ...and where they are and what to say (markers, the prompt line, the hint)
    prompts::Focus focus;
    InvMenu inv;           // on foot at the back of your truck: its inventory (E, or TAB / I) (invmenu.h)
    InvLayout invLay;      // ...as last drawn (the mouse and keys work from it)
    double walkSend = 0, carCamDist = 7.5;
    // On foot you see through your own eyes: the mouse turns your head (no button needed)
    // and the keys walk where you look.
    double footYaw = 0, footPitch = 0;
    // Getting in or out: the view glides from where it was (camFrom) to the new one over half a second.
    double camBlend = 1, peopleMs = 0;
    View camFrom;
    double workCrouch = 0;  // 0..1: crouched and leaning in to the work you're doing on foot (eased)
    bool crouchLatch = false, crouchWas = false;  // the crouch key in toggle mode / last frame's press
    V3 crouchAt, crouchFrom, fpLean, fpWork;  // ...at that, and how far the eyes moved for it
    double crouchReach = 0.85;
    V3 workHand;            // where your right hand was on the work last frame
    bool workHandOn = false;
    bool scriptE = false;  // (--script work-*: E held)
    bool mouseLook = false;
    // Footsteps (recorded, audio.h): one per half stride, by what's underfoot -- paving, sand / snow, or gravel (dirt, grass, rock, mud, ...).
    auto stepSfx = [](Surface su) { return su == SURF_PAVED ? SFX_STEP_PAVED : su == SURF_SAND || su == SURF_SNOW ? SFX_STEP_SAND : SFX_STEP_GRAVEL; };
    auto stepped = [](double before, double after) { return std::floor(after / 3.14159265) != std::floor(before / 3.14159265); };
    auto closeInventory = [&]() {
        if (!inv.open) return;
        inv.close();
        if (audioOn) audio.play(SFX_TOOLBOX_SHUT, 0.8f);
        interact.waitRelease = true;  // (E, if it's still down, is let go before it opens the menu again)
    };
    auto toggleInventory = [&]() {
        if (inv.open) {
            closeInventory();
            return;
        }
        if (!trunk::kitInReach(sim, walker.p)) hud.toast("WALK UP TO THE BACK OF YOUR TRUCK AND PRESS " + K(KA_INTERACT));
        else inv.show(sim.player), audioOn ? audio.play(SFX_TOOLBOX_OPEN, 0.7f) : void();
    };
    // A choice in the menu (a slot): the same jobs as taking from the bed.
    auto inventoryChoose = [&](int what) {
        bool closed = false;
        const std::string said = inv.choose(interact, sim, walker, TrunkStock{career.kit(), sim.anchorStock}, what, closed);
        if (!said.empty()) hud.toast(said);
        if (closed) closeInventory();
    };
    // Getting into other vehicles (carentry.h): riding along in a buddy's (sim.player is theirs
    // meanwhile, and nothing of yours is driven), or waiting for the one you asked for to be handed over.
    uint32_t rideId = 0;    // the one you ride along in (its netId; 0: not riding)
    V3 rideAt;              // where it was last (if it goes, you're set down there)
    uint32_t enterAsk = 0;  // the one you asked for (its netId) to get into when it's yours
    auto carById = [&](uint32_t id) {
        for (int i = 0; i < (int)sim.cars.size(); i++)
            if (id && sim.cars[i].netId == id) return i;
        return -1;
    };
    auto backToMine = [&]() {  // sim.player back on one of mine, the nearest (if any is left)
        if (sim.player >= 0 && sim.player < (int)sim.cars.size() && !sim.cars[sim.player].remote) return;
        int best = -1;
        double bd = 1e18;
        for (int i = 0; i < (int)sim.cars.size(); i++)
            if (!sim.cars[i].remote && !sim.cars[i].isProp() && len(sim.cars[i].frame().com - walker.p) < bd) bd = len(sim.cars[i].frame().com - walker.p), best = i;
        if (best >= 0) sim.player = best;
    };
    auto getOut = [&]() {
        Car& c = sim.cars[sim.player];
        const bool riding = rideId != 0;
        if (len(c.frame().vel) > (riding ? 3.0 : 2.0)) {
            hud.toast(riding ? "ASK THEM TO STOP FIRST" : "STOP FIRST");
            return;
        }
        if (!riding) {
            stopPilot();
            c.handbrake = 1;
            c.parkBrake = true;
            c.engineOff = true, c.crank = 0;  // (key out: the starter turns it over when you get back in; battery.h)
        }
        camFrom = lastView, camBlend = 0;
        walker.exitCar(sim.world, c, riding);
        if (audioOn) audio.play(SFX_DOOR_OPEN, 0.8f), audio.play(SFX_DOOR_SHUT, 0.8f, 0, 1.1f);  // (the door opens as you step out, and you push it shut)
        if (riding) {
            rideId = 0;
            backToMine();
        }
        carCamDist = cam.dist;
        cam.initialized = false;
        footYaw = walker.yaw;  // looking the way you got out
        footPitch = -0.1;
        hud.toast("ON FOOT: MOUSE LOOK, WASD WALK, SHIFT RUN, SPACE JUMP, HOLD E AT THE BUMPER TO GRAB THE WINCH LINE, J GET IN");
    };
    // In: `target`, else the nearest (`any`: anyone's, the J key and E at a door; else only yours).
    auto getIn = [&](int target = -1, bool any = false) {
        const int k = target >= 0 ? target : any ? carentry::nearest(sim, walker.p) : walker.carToEnter(sim);
        const carentry::Verdict v = carentry::verdict(sim, k);
        if (v == carentry::WAIT) {
            hud.toast("WAIT FOR IT TO STOP");
            return;
        }
        if (v == carentry::TAKE) {  // someone else's with nobody in it: asked of its owner (the host settles who's first)
            if (enterAsk) hud.toast("GETTING IN...");
            else if (net.requestTake(sim, k)) enterAsk = net.asking(), hud.toast("GETTING INTO " + net.ownerName(sim.cars[k].netId) + "'S " + sim.cars[k].spec->name + "...");
            else hud.toast("CAN'T GET INTO THAT ONE NOW");
            return;
        }
        if (sim.handLine.on()) {  // (the line in your hands spools back onto its drum)
            sim.dropHandLine();
            hud.toast("LINE SPOOLED BACK");
        }
        walker.hand = TrunkRef();  // (a tool in your hand goes back in the truck with you)
        inv.close();
        if (walker.carrying) Interact::drop(sim, walker, V3(std::cos(walker.yaw), 0, -std::sin(walker.yaw)));  // (whisked away: it's left there)
        else if (walker.holdingJack) Interact::dropJack(sim, walker);  // (put the jack down)
        if (v == carentry::RIDE) {  // a buddy's they're driving: along for the ride (their car's on the screen, yours stays parked)
            rideId = sim.cars[k].netId, rideAt = sim.cars[k].frame().com;
            sim.player = k;
            camFrom = lastView, camBlend = 0;
            walker.active = false;
            if (audioOn) audio.play(SFX_DOOR_OPEN, 0.8f), audio.play(SFX_DOOR_SHUT, 0.9f, 0, 0.7f);
            cam.dist = carCamDist;
            cam.initialized = false;
            net.sendGame("ride " + std::to_string(rideId));
            hud.toast("RIDING ALONG WITH " + net.ownerName(rideId) + " (" + K(KA_GET_IN_OUT) + " GETS OUT)");
            return;
        }
        if (walker.holdingChain && k >= 0 && v == carentry::DRIVE) {  // (a chain in your hands goes in the back with you)
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
        if (sim.cars[k].ai || sim.cars[k].pilot.active) {  // (traffic, stopped: it's yours to drive now, no AI)
            sim.cars[k].ai = false;
            sim.cars[k].pilot.active = false;
            sim.cars[k].pilot.followCar = -1;
            if (sim.cars[k].gear < 0) sim.cars[k].gear = 1;
        }
        if (audioOn) audio.play(SFX_DOOR_OPEN, 0.8f), audio.play(SFX_DOOR_SHUT, 0.9f, 0, 0.7f);  // (opens as you get in, and you pull it to behind you)
        camFrom = lastView, camBlend = 0;
        walker.active = false, crouchLatch = false;
        cam.dist = carCamDist;
        cam.initialized = false;
        net.sendGame("inside");
        hud.toast(std::string("IN THE ") + sim.cars[k].spec->name);
    };
    // ---- The world editor (offline) ----
    Editor editor;
    // A map switch without a frozen frame: the world and the terrain's data are made on a worker, uploaded a few ms a frame, the
    // screen fades to black meanwhile, the scenery is built a stage a frame, and it fades back in. (Hosting, joining and the
    // editor load in one go, as before.)
    struct MapLoad {
        enum State { IDLE, WORK, UPLOAD, STEPS, FADEIN } st = IDLE;
        std::future<std::pair<std::unique_ptr<World>, std::shared_ptr<Renderer::Prepared>>> fut;
        std::unique_ptr<World> world;
        int map = 0;
        float fade = 0;  // how black the screen is
        int warm = 0;    // frames held black once built: the first draws of the new buffers and textures (80 ms and more) land here, not in the fade
        std::function<void()> then;
        bool active() const { return st != IDLE; }
    } mapLoad;
    auto requestMap = [&](int m, std::function<void()> then) {
        if (mapLoad.active()) return;
        if (!opt.shotFrames.empty() || !opt.script.empty() || editor.active || net.active()) {
            loadMap(m);
            if (then) then();
            return;
        }
        World::useLocalEdits = net.role != NetSession::CLIENT;
        const bool showcase = World::withShowcase;
        const int wilds = (int)World::wildsKind;
        const double halfOverride = World::halfSizeOverride;
        const props::HullLibrary* hulls = props::activeRockScans();
        const ScanCover cover = renderer.scanCover();
        mapLoad.map = m, mapLoad.then = std::move(then), mapLoad.st = MapLoad::WORK;
        mapLoad.fut = std::async(std::launch::async, [=] {
            World::withShowcase = showcase, World::wildsKind = wilds, World::halfSizeOverride = halfOverride;
            props::setActiveRockScans(hulls);
            auto w = std::make_unique<World>(m);
            auto p = Renderer::prepareWorld(*w, cover);
            return std::make_pair(std::move(w), std::move(p));
        });
    };
    auto mapLoadTick = [&](double dtSec) {
        MapLoad& L = mapLoad;
        if (!L.active()) return;
        const float step = (float)(std::min(dtSec, 0.05) / 0.3);
        if (L.st != MapLoad::STEPS && L.st != MapLoad::FADEIN) L.fade = std::min(1.0f, L.fade + step);
        if (L.st == MapLoad::WORK && L.fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto made = L.fut.get();
            L.world = std::move(made.first);
            renderer.beginUpload(std::move(made.second));
            L.st = MapLoad::UPLOAD;
        }
        if (L.st == MapLoad::UPLOAD && renderer.pumpUpload(3.0) && L.fade >= 1) {
            LOADPROF("mapload.swap (sim)");
            const int model = std::max(0, sim.cars[sim.player].spec->model);
            campWorld = false;
            sim.world = std::move(*L.world);
            L.world.reset();
            sim.setup();
            if (worldChanged) worldChanged();
            fitGarage();
            if (model != sim.cars[sim.player].spec->model) sim.swapPlayerVehicle(garage.spec(model));
            sim.paused = false;
            renderer.adoptWorld(sim.world);
            drawnWorld = sim.world.stamp;
            hud.bestKmh = 0;
            dbgSimSeconds = 0;
            wrapSeen = sim.wrapShift = 0;
            fx.clear();
            cam.initialized = false;
            L.st = MapLoad::STEPS;
        } else if (L.st == MapLoad::STEPS) {
            if (renderer.stepWorld(sim.world) && ++L.warm > 8) L.st = MapLoad::FADEIN, L.warm = 0;
        } else if (L.st == MapLoad::FADEIN) {
            L.fade -= step * 0.8f;
            if (L.fade <= 0) {
                L.fade = 0, L.st = MapLoad::IDLE;
                if (L.then) std::exchange(L.then, nullptr)();
            }
        }
    };
    if (!scripted) editor.autosaveDir = userLevelFolder() + "/autosave";  // (recovery copies; never from the scripted runs)
    Level lastEdited;
    bool haveEdited = false;
    auto openEditor = [&](const Level& base, bool newSection = false, bool atPlayer = false) {
        if (net.active()) {
            hud.toast("THE EDITOR IS OFFLINE ONLY: LEAVE THE SESSION FIRST");
            return;
        }
        run.stop();
        levelHours = -1;
        stopPilot();
        if (newSection) editor.newSection(sim.world, renderer);  // (a fresh open country, the canvas cleared)
        else {
            campWorld = false;
            sim.world = levelWorld(base);
            editor.open(sim.world, renderer, base);
            if (atPlayer) {  // (F2 from driving: the camera starts above and behind you, looking the way you face)
                const Frame pf = sim.cars[sim.player].frame();
                if (walker.active) editor.lookFrom(walker.p, walker.yaw);
                else editor.lookFrom(pf.com, std::atan2(-pf.fwd.z, pf.fwd.x));
            }
        }
        sim.clearWorldLeftovers(sim.player);  // (offline: just you; the last world's vehicles don't wait in this one)
        if (worldChanged) worldChanged();
        drawnWorld = sim.world.stamp;
        sim.paused = true;
        fx.clear();
    };
    // Back to the editor after a playtest: its camera, undo history and unsaved edits as they were (nothing reloaded from disk).
    auto returnToEditor = [&]() -> bool {
        if (!editorSuspended || !editor.started() || editor.core.level.map != sim.world.map || net.active()) return false;
        run.stop();
        levelHours = -1;
        stopPilot();
        campWorld = false;
        sim.world = levelWorld(editor.core.level);
        sim.clearWorldLeftovers(sim.player);
        if (worldChanged) worldChanged();
        editor.resume(sim.world, renderer);
        drawnWorld = sim.world.stamp;
        sim.paused = true;
        fx.clear();
        editorSuspended = false;
        return true;
    };
    auto editorKeyLevel = [&]() {  // what F2 edits: the last edited level, or the one being played, or a new one here
        if (haveEdited && lastEdited.map == sim.world.map) return lastEdited;
        if (run.active()) return run.level;
        Level l;
        l.map = sim.world.map;
        l.name = "UNTITLED";
        return l;
    };
    // Back to the main menu's camp: its world and the three parked vehicles replace what was up (offline only).
    auto enterCamp = [&]() {
        run.stop();
        levelHours = -1;
        menucamp::fireOf(campLevel, campFire);
        sim.world = menuscene::buildWorld(campLevel);
        sim.setup();
        if (worldChanged) worldChanged();
        fitGarage();
        parkCamp();
        for (const Car& c : sim.cars)
            if (!c.isProp()) prebuildVisual(*c.spec, c.color);
        renderer.setWorld(sim.world);
        drawnWorld = sim.world.stamp;
        fx.clear();
        cam.initialized = false;
        wrapSeen = sim.wrapShift = 0;
        hud.bestKmh = 0;
    };
    std::function<void(const devmenu::Action&)> runDev;  // the dev tools, one place for the menu and the hotkeys (set below, where its state lives)
    // Menu sounds (quiet; the master volume applies): a click when something's activated, a softer, higher tick when the selection moves.
    auto uiClick = [&]() { if (audioOn) audio.play(SFX_UI_CLICK, 0.35f); };
    auto uiHover = [&]() { if (audioOn) audio.play(SFX_UI_HOVER, 0.16f); };
    auto activationKey = [](int sc) { return sc == 40 || sc == 88 || sc == 44; };  // (Enter, keypad Enter, Space: the pad's A arrives as Enter)
    worldChanged = [&]() {
        worldCareer = career.on;
        if (jobOn && net.role == NetSession::HOST) net.sendGame("jobstop");
        jobOn = false;  // (its wreck and its places were the last world's)
        cachesMap = -1;
        if (walker.active) {  // (back in your seat: the sim has you there)
            walker.active = false, crouchLatch = false;
            cam.dist = carCamDist;
        }
        releaseFreedMemory();  // (the last world's terrain and scenery, freed on worker threads, back to the system)
    };
    auto handleMenu = [&](PauseMenu::Result res) {
        switch (res.action) {
            case PauseMenu::NONE: break;
            case PauseMenu::DEV_RUN: {
                const devmenu::Action da{(devmenu::Act)res.vehicle, res.arg};
                if (da.act != devmenu::MODEL_LAB) closeMenu();  // (the lab comes back to the menu)
                runDev(da);
                break;
            }
            case PauseMenu::RESUME: closeMenu(); break;
            case PauseMenu::DRIVE:
                closeMenu();
                sim.swapPlayerVehicle(garage.spec(res.vehicle));
                hud.toast(std::string("NOW DRIVING ") + vehicleCatalog()[res.vehicle].name);
                break;
            case PauseMenu::GARAGE:
                closeMenu();
                garageFromMenu = openGarage();
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
            case PauseMenu::DETECT_GFX:
                saveSettings();
                if (!menu.title) closeMenu();  // (the game runs while it measures)
                startProbe();
                break;
            case PauseMenu::CHANGED:
                if (probe.active() && presetOf(settings.gfx) != probe.preset()) probe.cancel(), probePending = false;  // (you picked one yourself)
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
                if (menu.mapPick != sim.world.map || campWorld) {  // (the HOST card's map)
                    run.stop();
                    levelHours = -1;
                    loadMap(menu.mapPick);
                    settings.map = menu.mapPick;
                    saveSettings();
                }
                closeMenu();
                startHost(settings.hostPort);
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
            case PauseMenu::FREE_DRIVE: {
                closeMenu();
                run.stop();
                levelHours = -1;
                if (net.role == NetSession::HOST) net.setLevel("");
                const bool staleEdits = menu.mapPick == MAP_PROVING_GROUND && sim.world.map == MAP_PROVING_GROUND && !net.active() &&
                                        sim.world.editStamp != levelFilesStamp();  // (a level saved since the proving ground was built)
                const bool otherSave = worldCareer != career.on && !net.active();  // (career <-> sandbox: the other mode's vehicles don't come along)
                if ((menu.mapPick != sim.world.map || staleEdits || campWorld || otherSave) && net.role != NetSession::CLIENT) {
                    requestMap(menu.mapPick, nullptr);
                    settings.map = menu.mapPick;
                    saveSettings();
                }
                sim.swapPlayerVehicle(garage.spec(res.vehicle));
                hud.toast(std::string(mapName(sim.world.map)) + ": " + vehicleCatalog()[res.vehicle].name);
                break;
            }
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
            case PauseMenu::EDITOR_BACK:
                closeMenu();
                if (!returnToEditor()) hud.toast("NOTHING TO GO BACK TO");
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
                if (sim.cars[sim.player].remote) {  // (riding along, or yours went to someone: none of yours to bring)
                    hud.toast(rideId ? "GET OUT FIRST (" + K(KA_GET_IN_OUT) + ")" : "GET INTO ONE OF YOURS FIRST");
                    break;
                }
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
                if (wantCamp && !net.active() && !editor.active && !mapLoad.active()) enterCamp();
                menu.showTitle(std::max(0, sim.cars[sim.player].spec->model), campWorld ? settings.map : sim.world.map);
                classicTitle = false;
                if (campWorld) mainNav.open(menu.mapPick, std::max(0, sim.cars[sim.player].spec->model));
                break;
            case PauseMenu::LOAD_MAP: {
                closeMenu();
                if (net.active()) {
                    hud.toast("LEAVE THE MULTIPLAYER SESSION TO CHANGE MAPS");
                    break;
                }
                requestMap(res.vehicle, nullptr);
                levelHours = -1;
                settings.map = res.vehicle;
                saveSettings();
                hud.toast(std::string("MAP: ") + mapName(res.vehicle));
                break;
            }
        }
    };
    // ---- The multiplayer pages (mpmenu.h): the PauseMenu's page 2, drawn and driven in the main menu's style ----
    auto upperText = [](std::string s) {
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    };
    auto mpLoadFields = [&]() {  // settings -> the pages' fields
        mpmenu::Fields& f = mpNav.f;
        f.name = settings.playerName;
        f.address = settings.joinAddress;
        f.password = settings.joinPassword;
        f.hostPassword = settings.netPassword;
        f.session = settings.sessionName;
        f.port = std::to_string(settings.hostPort);
        f.maxPlayers = net.role == NetSession::HOST ? net.maxPlayers : settings.maxPlayers;
        f.passwordOn = net.role == NetSession::HOST ? !net.password.empty() : !settings.netPassword.empty();
        f.map = 0;
        const std::vector<int>& maps = listedMaps();
        for (int i = 0; i < (int)maps.size(); i++)
            if (maps[i] == menu.mapPick) f.map = i;
    };
    auto mpSaveFields = [&]() {  // the pages' fields -> settings
        const mpmenu::Fields& f = mpNav.f;
        if (!f.name.empty()) settings.playerName = f.name;
        settings.joinAddress = f.address;
        settings.joinPassword = f.password;
        settings.netPassword = f.hostPassword;
        settings.sessionName = f.session;
        const int p = net::parsePort(f.port, net::kMinHostPort);
        if (p > 0) settings.hostPort = p;
        settings.maxPlayers = f.maxPlayers;
        const std::vector<int>& maps = listedMaps();
        if (!maps.empty()) menu.mapPick = maps[std::min(std::max(f.map, 0), (int)maps.size() - 1)];
        saveSettings();
    };
    auto mpFillView = [&]() {
        mpmenu::View& v = mpView;
        v.inGame = !menu.title;
        v.role = net.role == NetSession::HOST ? 1 : net.role == NetSession::CLIENT && !net.joining() ? 2 : 0;
        v.codesAvailable = !rendezvousAddress().empty();
        v.code = net.joinCode;
        v.codeState = (int)net.codeState;
        v.codeError = net.codeError;
        v.players.clear();
        for (int k = 0; k < net::kMaxPeers && v.role; k++) {
            const NetPlayer& p = net.players[k];
            if (!p.active) continue;
            mpmenu::View::Player vp;
            vp.id = k, vp.name = p.name.empty() ? "PLAYER" : p.name, vp.pingMs = (int)std::lround(p.ping * 1000);
            vp.link = k == net.self && net.role == NetSession::CLIENT ? (int)net.myLink() : p.link;
            vp.self = k == net.self;
            v.players.push_back(vp);
        }
        v.sessionTitle = net.role == NetSession::HOST ? net.sessionName : net.ownerName(0) + "'S GAME";
        v.mapName = v.role ? mapName(net.map) : "";
        v.maxPlayersNow = net.maxPlayers;
        v.lan.clear();
        for (const PauseMenu::FoundGame& g : menu.found) v.lan.push_back({g.session, g.ok ? g.host + "  " + g.map + "  " + g.players : g.why, g.address, g.ok});
        v.searching = menu.searching;
        v.localIps = menu.localIps;
        v.hostPort = net.port() ? net.port() : settings.hostPort;
        v.joining = menu.joining;
        v.byCode = joinByCode;
        v.joinStep = (int)net.joinStep();
        v.joinFail = (int)net.joinFail;
        v.failPlayers = net.failPlayers, v.failMax = net.failMax;
        v.joinLabel = menu.joinLabel;
        v.joinError = menu.joinError;
        v.needPassword = menu.joinNeedsPw;
        v.mapNames.clear();
        for (int mp : listedMaps()) v.mapNames.push_back(mapName(mp));
        v.recent = settings.recentJoins;
    };
    auto mpSync = [&]() {  // (the page just came up: from the main menu, HOME; in the game, FRIENDS)
        mpFillView();
        const bool shown = mpShown();
        if (shown && !mpWasShown) {
            mpLoadFields();
            mpNav.open(menu.title ? mpmenu::P_HOME : mpmenu::P_FRIENDS, mpView);
            if (menu.joining) mpNav.showJoining();
        }
        mpWasShown = shown;
    };
    auto openToFriends = [&]() {
        if (net.role == NetSession::CLIENT) return;
        if (net.role != NetSession::HOST) {  // (this world as it is: no reload, the port in the settings or the next free one)
            for (int k = 0; k < 10 && !net.active(); k++) startHost(settings.hostPort + k, k < 9);
            if (!net.active()) return;
            if (run.active()) net.setLevel(saveLevelText(run.level));  // (they join into the level being played)
        }
        net.password = mpNav.f.passwordOn ? mpNav.f.hostPassword : std::string();
        net.maxPlayers = std::max(net.playerCount(), std::min(std::max(mpNav.f.maxPlayers, 2), net::kMaxPeers));
        net.rendezvous = rendezvousAddress();
        if (!net.openToFriends()) hud.toast("NO JOIN CODE: " + upperText(net.codeError));
    };
    auto handleMp = [&](const mpmenu::Result& r) {
        using namespace mpmenu;
        const int model = std::max(0, sim.cars[sim.player].spec->model);
        switch (r.act) {
            case A_NONE: break;
            case A_CHANGED: mpSaveFields(); break;
            case A_SETTINGS_LIVE:
                mpSaveFields();
                if (net.role == NetSession::HOST) {
                    net.password = mpNav.f.passwordOn ? mpNav.f.hostPassword : std::string();
                    net.maxPlayers = std::max(net.playerCount(), std::min(std::max(mpNav.f.maxPlayers, 2), net::kMaxPeers));
                }
                break;
            case A_BACK:
                if (menu.title) menu.showTitle(model, sim.world.map);
                else menu.show(model, sim.world.map);
                break;
            case A_JOIN_CODE:
                mpSaveFields();
                mpNav.showJoining();
                beginCodeJoin(r.text);
                break;
            case A_JOIN_ADDRESS:
            case A_JOIN_FOUND:
                mpSaveFields();
                mpNav.showJoining();
                beginJoin(r.text, r.label);
                break;
            case A_RETRY:
                mpSaveFields();
                mpNav.showJoining();
                if (joinByCode) beginCodeJoin(joinWhat);
                else beginJoin(joinWhat, menu.joinLabel);
                break;
            case A_CANCEL_JOIN:
                handleMenu(PauseMenu::Result{PauseMenu::CANCEL_JOIN});
                break;
            case A_HOST: {
                mpSaveFields();
                handleMenu(PauseMenu::Result{PauseMenu::HOST});
                if (net.role == NetSession::HOST && mpNav.f.alsoCode && !rendezvousAddress().empty()) {
                    net.rendezvous = rendezvousAddress();
                    net.openToFriends();
                }
                break;
            }
            case A_OPEN_FRIENDS: openToFriends(); break;
            case A_CLOSE_FRIENDS:
                net.closeToFriends();
                hud.toast("CLOSED TO FRIENDS: THE CODE NO LONGER WORKS");
                break;
            case A_COPY_CODE: hud.toast("JOIN CODE " + rv::formatCode(net.joinCode) + " COPIED"); break;
            case A_STOP_HOSTING:
            case A_LEAVE: handleMenu(PauseMenu::Result{PauseMenu::LEAVE}); break;
            case A_KICK:
                if (r.arg > 0 && r.arg < net::kMaxPeers) hud.toast(upperText(net.players[r.arg].name) + " REMOVED");
                net.kick(r.arg);
                break;
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
    if (opt.hostPort >= 0) startHost(opt.hostPort > 0 ? opt.hostPort : settings.hostPort);
    else if (!opt.join.empty()) startJoin(opt.join);
    else if (!opt.joinCode.empty()) beginCodeJoin(opt.joinCode);
    // Launched plainly (no map, spawn, session or test asked for): the title screen first.
    bool resumedEdit = false;
    if (!opt.resumeEdit.empty() && !scripted) {  // (a reload with unsaved edits: the recovery copy it just wrote)
        Level l;
        if (loadLevelFile(editor.autosaveDir + "/" + levelFileName(opt.resumeEdit) + ".czl", l)) {
            l.file = userLevelFolder() + "/" + levelFileName(l.name) + ".czl";
            l.builtin = false;
            openEditor(l);
            editor.markRecovered(l);
            resumedEdit = true;
        }
    }
    if (resumedEdit) {
    } else if (opt.newSection) {
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
    } else if (wantCamp && campWorld) {  // the new main menu over its camp
        openMenu();
        const int model = std::max(0, sim.cars[sim.player].spec->model);
        menu.showTitle(model, settings.map);
        mainNav.open(menu.mapPick, model);
        mainNav.career = opt.menuCareer || (opt.mainMenu < 0 && settings.career);
        if (opt.mainMenu >= 1) {  // (--main-menu P: straight onto a page, for the screenshots)
            if (opt.mainMenu <= 2) mainNav.choose(opt.menuCareer ? mainmenu::F_CAREER : mainmenu::F_SANDBOX);
            if (opt.mainMenu == 2) mainNav.choose(mainNav.mapIdx);
            if (opt.mainMenu == 3) mainNav.choose(mainmenu::F_DEV);
        }
    } else if (!scripted && opt.scenario.empty() && opt.map < 0 && !opt.spawnSet && opt.hostPort < 0 && opt.join.empty() && opt.menu < 0 && opt.resumeModel < 0) {
        openMenu();
        menu.showTitle(std::max(0, sim.cars[sim.player].spec->model), sim.world.map);
    }
    // What the new main menu asked for (mainmenu.h).
    auto handleMain = [&](const mainmenu::Result& r) {
        using namespace mainmenu;
        const int model = std::max(0, sim.cars[sim.player].spec->model);
        switch (r.act) {
            case A_NONE: break;
            case A_START: {
                settings.career = r.career;
                handleMenu(PauseMenu::Result{PauseMenu::CHANGED});  // (career <-> sandbox: its own garage)
                menu.mapPick = r.map;
                menu.vehicle = r.vehicle;
                handleMenu(PauseMenu::Result{PauseMenu::FREE_DRIVE, r.vehicle});
                break;
            }
            case A_MULTIPLAYER: menu.showTitlePage(2, model, settings.map); menu.mapPick = mainNav.map(); break;
            case A_SETTINGS: menu.showTitlePage(1, model, settings.map); menu.mapPick = mainNav.map(); break;
            case A_QUIT: running = false; break;
            case A_DEV_EDITOR: menu.mapPick = mainNav.map(); handleMenu(PauseMenu::Result{PauseMenu::EDITOR}); break;
            case A_DEV_SECTION: handleMenu(PauseMenu::Result{PauseMenu::NEW_SECTION}); break;
            case A_DEV_LAB: handleMenu(PauseMenu::Result{PauseMenu::MODEL_LAB}); break;
            case A_DEV_MENUSCENE: {  // (the world editor on the camp: it saves to the player's own file, never the game's)
                Level l = campLevel;
                l.builtin = false, l.file = menuscene::userPath();
                closeMenu();
                openEditor(l);
                break;
            }
            case A_DEV_LEVELS:
                if (refreshLevelsLater) refreshLevelsLater();
                menu.showTitlePage(4, model, settings.map);
                menu.mapPick = mainNav.map();
                break;
            case A_DEV_TOOLS: {  // (the tools act on a real map: load the last one, then open the dev menu on it)
                const int m = mainNav.map();
                closeMenu();
                settings.map = m;
                saveSettings();
                requestMap(m, [&, model] {
                    openMenu();
                    menu.showDev(model, sim.world.map, false);
                });
                break;
            }
            case A_DEV_HOTKEYS:
                settings.devHotkeys = !settings.devHotkeys;
                saveSettings();
                break;
            case A_DEV_DEBUG: runDev(devmenu::Action{devmenu::FPS, 0}); break;
            case A_DEV_CLASSIC: classicTitle = true; break;
        }
    };
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
    double benchDraw = 0, benchPhysics = 0;
    int benchPhysicsN = 0;
    Renderer::GeometryStats benchCars;  // (CZ_BENCH: the vehicles' counts, summed over the frames measured)
    int benchCarN = 0;
    auto printCarBench = [&] {  // (CZ_BENCH or --bench: what the vehicles cost; --script cars puts CZ_CARS of them in view)
        if (!benchCarN) return;
        int awake = 0, vehicles = 0;
        for (const Car& c : sim.cars)
            if (!c.isProp()) vehicles++, awake += !c.sleeping && !c.remote;
        const double n = benchCarN;
        std::printf("bench: cars %d (%d awake), physics %.2f ms/frame; car meshes %.2f ms CPU (skin, bound parts, running gear, parts models), other frame models %.2f ms; "
                    "main %.1f draws %.1fk tris, shadows %.1f draws %.1fk tris; %.2f meshes rebuilt a frame\n",
                    vehicles, awake, benchPhysicsN ? benchPhysics / benchPhysicsN : 0.0, benchCars.carPrepMs / n, benchCars.carPartsMs / n,
                    benchCars.carDraws / n, benchCars.carTris / n / 1000, benchCars.carShadowDraws / n, benchCars.carShadowTris / n / 1000, benchCars.carRebuilds / n);
        const unsigned* k = benchCars.carKindTris;
        std::printf("bench: car tris (main, k) skin %.1f glass %.1f bound parts %.1f running gear %.1f wheels %.1f parts models %.1f\n",
                    k[0] / n / 1000, k[1] / n / 1000, k[2] / n / 1000, k[3] / n / 1000, k[4] / n / 1000, k[5] / n / 1000);
    };
    size_t spawnType = 0, trafficType = 0;
    // The dev tools (devmenu.h): what the old test keys did, now also what the dev menu's entries do.
    runDev = [&](const devmenu::Action& da) {
        using namespace devmenu;
        if (net.active() && offlineOnly(da.act)) {
            hud.toast("OFFLINE ONLY: LEAVE THE SESSION FIRST");
            return;
        }
        const std::vector<int>& listed = listedVehicles();
        // (static, not [&]: the whenVisualReady callbacks below run after this call has returned)
        static const auto vehicleName = [](int k) { return std::string(vehicleCatalog()[listedVehicles()[k]].name); };
        auto toSpot = [&](double x, double z, const std::string& what) {
            if (walker.active) getIn();
            if (walker.active) return;  // (nobody to drive there)
            const Frame f = sim.cars[sim.player].frame();
            sim.teleportPlayer(x, z, std::atan2(-f.fwd.z, f.fwd.x));
            cam.initialized = false;
            hud.toast("AT " + what);
        };
        switch (da.act) {
            case NONE: break;
            case SWAP_VEHICLE:
                if (da.arg >= 0 && da.arg < (int)listed.size()) {  // (the listed vehicles, in order: 1 the Blazer)
                    const int k = da.arg;
                    whenVisualReady(garage.spec(listed[k]), [&, k] {
                        sim.swapPlayerVehicle(garage.spec(listedVehicles()[k]));
                        hud.toast("NOW DRIVING " + vehicleName(k));
                    });
                }
                break;
            case SPAWN_PARKED: {
                const size_t k = da.arg >= 0 ? (size_t)da.arg : spawnType;
                if (k >= listed.size()) break;
                whenVisualReady(garage.spec(listed[k]), [&, k] {
                    hud.toast(sim.spawnAhead(garage.spec(listedVehicles()[k])) ? "SPAWNED " + vehicleName((int)k) : std::string("VEHICLE LIMIT REACHED"));
                });
                if (da.arg < 0) spawnType = (spawnType + 1) % listed.size();
                break;
            }
            case SPAWN_TRAFFIC:
                {
                    const size_t k = trafficType;
                    whenVisualReady(garage.spec(listed[k]), [&, k] {
                        hud.toast(sim.spawnTraffic(garage.spec(listedVehicles()[k])) ? "AI " + vehicleName((int)k) + " ON THE LOOP ROAD"
                                                                            : std::string("VEHICLE LIMIT REACHED"));
                    });
                }
                trafficType = (trafficType + 1) % listed.size();
                break;
            case REMOVE_VEHICLE: hud.toast(sim.removeCar(sim.player) ? "VEHICLE REMOVED" : "KEEP ONE LOCAL VEHICLE"); break;
            case TELEPORT_SPAWN:
                if (walker.active) getIn();
                if (walker.active) break;
                sim.towPlayerHome();
                cam.initialized = false;
                hud.toast("AT THE MAP SPAWN");
                break;
            case TELEPORT_SECTION:
                if (da.arg >= 0 && da.arg < (int)sim.world.sections.size()) {
                    const World::SectionSpot& sp = sim.world.sections[da.arg];
                    if (walker.active) getIn();
                    if (walker.active) break;
                    sim.teleportPlayer(sp.spawn.x, sp.spawn.z, sp.spawnYaw);
                    cam.initialized = false;
                    hud.toast("AT " + sp.name);
                }
                break;
            case TELEPORT_PLACE: {
                int k = 0;
                for (const Place& pl : pilotPlaces(sim, net)) {
                    if (pl.kind != 0) continue;
                    if (k++ == da.arg) {
                        toSpot(pl.p.x, pl.p.z, pl.name);
                        break;
                    }
                }
                break;
            }
            case TIME_OF_DAY:
                if (net.role == NetSession::CLIENT) {
                    hud.toast("THE HOST'S CLOCK RULES IN MULTIPLAYER");
                    break;
                }
                settings.timeOfDay = std::min(std::max(da.arg, 0), (int)TOD_COUNT - 1);
                saveSettings();
                hud.toast(da.arg == TOD_CYCLE ? "THE CLOCK RUNS" : std::string("TIME: ") + timeOfDayName(settings.timeOfDay));
                break;
            case TIME_SLOWER:
                if (net.active()) break;  // everyone shares one clock
                timeScaleIdx = std::min(timeScaleIdx + 1, 4); sim.timeScale = kTimeScales[timeScaleIdx];
                break;
            case TIME_FASTER:
                if (net.active()) break;
                timeScaleIdx = std::max(timeScaleIdx - 1, 0); sim.timeScale = kTimeScales[timeScaleIdx];
                break;
            case RESET_WORLD:
                run.stop();
                levelHours = -1;
                requestMap(sim.world.map, nullptr);
                hud.toast("WORLD RESET");
                break;
            case REPAIR: sim.recoverPlayer(); hud.toast("REPAIRED"); break;
            case RESPAWN: sim.respawnPlayer(); hud.toast("RESPAWNED AT THE START"); break;
            case FLIP_UPRIGHT: sim.rightPlayer(); hud.toast("BACK ON ITS WHEELS"); break;
            case REFUEL: {
                Car& c = sim.cars[sim.player];
                if (c.isProp()) break;
                c.fuel = c.spec->fuelCapacity;
                if (c.spec->jerryCan > 0) c.jerry = c.spec->jerryCan;
                hud.toast("REFUELED");
                break;
            }
            case BEAM_VIEW: {
                static const char* names[] = {"BEAM VIEW OFF", "BEAM VIEW: X-RAY", "BEAM VIEW: BEAMS ONLY", "BEAM VIEW: PARTS"};
                debug = da.arg < 0 ? (DebugView)((debug + 1) % DEBUG_VIEW_COUNT) : (DebugView)std::min(da.arg, (int)DEBUG_VIEW_COUNT - 1);
                hud.toast(names[debug]);
                break;
            }
            case FPS: dbg.toggle(shiftDown()); break;  // the F3 debug screen (Shift: its frame graph); the small FPS counter stays in the settings
            case EDITOR:  // free driving: opens the editor here (in a playtest, F1 is the way back)
                if (editorSuspended && !editor.active) hud.toast(K(KA_PLAYTEST_BACK) + ": BACK TO THE EDITOR");
                else openEditor(editorKeyLevel(), false, true);
                break;
            case MODEL_LAB: handleMenu(PauseMenu::Result{PauseMenu::MODEL_LAB}); break;
            case NEW_SECTION: openEditor(Level(), true); break;
            case RELOAD:  // load the new build (see BuildWatch)
                if (editorSuspended) hud.toast("NO RELOADING DURING A PLAYTEST: " + K(KA_PLAYTEST_BACK) + " BACK TO THE EDITOR FIRST");  // (a reload would lose the editor and its unsaved edits)
                else if (hud.newBuild && editor.started() && !editor.autosaveNow()) hud.toast("RELOAD CANCELLED: COULDN'T SAVE A RECOVERY COPY OF YOUR EDITS (CTRL+S)");  // (a reload never costs edits)
                else if (hud.newBuild) reload = true, running = false;
                else hud.toast(watch.exe.empty() ? "RELOADING ISN'T AVAILABLE HERE" : "NO NEW BUILD YET");
                break;
            case SCENARIO_CHECKS:
                if (playback.active()) hud.scenarioDetail = !hud.scenarioDetail;
                break;
            case EXPORT_KEYBINDS: {  // the saved layout as the new defaults (keybinds.h): the text to paste over kInfo's first two fields
                makeDir("build");
                const std::string text = settings.keys.exportDefaults();
                FILE* f = std::fopen("build/keybinds-defaults.txt", "w");
                if (f) std::fputs(text.c_str(), f), std::fclose(f);
                std::fputs(text.c_str(), stdout);
                hud.toast(f ? "KEYBINDS WRITTEN TO BUILD/KEYBINDS-DEFAULTS.TXT" : "COULDN'T WRITE BUILD/KEYBINDS-DEFAULTS.TXT");
                break;
            }
        }
    };

    // CZ_ALLOC=N (with --shot-frames; N: the frames to warm up, default 60): heap allocations per frame on this thread afterwards; CZ_ALLOC_TRACE=1 adds the top call sites.
    const bool allocWatch = std::getenv("CZ_ALLOC") != nullptr;
    const int allocWarm = allocWatch && std::atoi(std::getenv("CZ_ALLOC")) > 1 ? std::atoi(std::getenv("CZ_ALLOC")) : 60;
    std::vector<uint64_t> allocFrames;
    uint64_t allocLast = 0, allocAllLast = 0, allocAllSum = 0;
    // --load-tour: every 45 frames do the next thing that used to hitch (a map switch, a vehicle swap, a spawn) and report the
    // worst frame since the last one.
    loadprof::Frames tourFrames, startFrames;
    int tourStep = -1, tourNext = 60;
    std::vector<std::string> tourLog;
    auto tourTick = [&](double ms) {
        if (!opt.loadTour) return;
        if (mapLoad.active() && frame >= tourNext) tourNext = frame + 1;  // (a map switch in progress: the next step waits)
        if (frame < 60) startFrames.add(ms);
        else tourFrames.add(ms);
        if (frame == 0) loadprof::report("startup (to the first frame)");
        if (frame < tourNext) return;
        const std::vector<int>& listed = listedVehicles();
        static const int maps[] = {MAP_FREEWAY, MAP_MOUNTAIN, MAP_FRONTIER, MAP_BACKCOUNTRY, MAP_WILDS, MAP_PROVING_GROUND};
        const int nMaps = (int)(sizeof maps / sizeof maps[0]), nVeh = (int)listed.size();
        if (tourStep >= 0) {
            char line[160];
            const std::string what = tourStep < nMaps ? std::string("load map ") + mapName(maps[tourStep])
                                     : tourStep < nMaps + nVeh ? "swap to vehicle " + std::to_string(tourStep - nMaps)
                                                               : "spawn vehicle " + std::to_string(tourStep - nMaps - nVeh);
            std::snprintf(line, sizeof line, "tour: %-28s worst frame %7.1f ms  mean %6.2f ms", what.c_str(), tourFrames.worst, tourFrames.mean());
            tourLog.push_back(line);
            std::printf("%s\n", line);
            loadprof::report(what.c_str());
        } else {
            std::printf("tour: startup frames 0-59: worst %.1f ms, mean %.2f ms\n", startFrames.worst, startFrames.mean());
        }
        tourFrames.reset();
        tourStep++;
        tourNext = frame + 45;
        if (tourStep < nMaps) requestMap(maps[tourStep], nullptr);
        else if (tourStep < nMaps + nVeh) {
            const VehicleSpec& vs = garage.spec(listed[tourStep - nMaps]);
            whenVisualReady(vs, [&, v = &vs] { sim.swapPlayerVehicle(*v); });
        } else if (tourStep < nMaps + 2 * nVeh) {
            const VehicleSpec& vs = garage.spec(listed[tourStep - nMaps - nVeh]);
            whenVisualReady(vs, [&, v = &vs] { sim.spawnAhead(*v); });
        }
        else if (false) {}
        else {
            std::printf("tour done\n");
            running = false;
        }
    };
    // CZ_FRAMELOG=1 with --bench: a histogram, and every frame over 1.5x the median with its stages (CPU/GPU ms).
    struct FrameRec { int bf; double ms, cpu[FP_COUNT], gpu[FP_COUNT]; };
    std::vector<FrameRec> frameRecs;
    auto printFrameLog = [&] {
        if (frameRecs.empty()) return;
        std::vector<double> t;
        for (auto& f : frameRecs) t.push_back(f.ms);
        std::sort(t.begin(), t.end());
        const double med = t[t.size() / 2];
        static const double edges[] = {2.0, 2.25, 2.5, 2.75, 3.0, 3.5, 4, 5, 6, 8, 12, 1e9};
        int h[12] = {};
        for (double v : t) { int k = 0; while (v >= edges[k]) k++; h[k]++; }
        std::printf("framelog: median %.3f ms; histogram (ms): <2:%d", med, h[0]);
        for (int k = 1; k < 12; k++) std::printf(" <%g:%d", edges[k], h[k]);
        std::printf("\n");
        int n = 0, last = -100;
        for (auto& f : frameRecs) if (f.ms > med * 1.5) {
            std::printf("framelog: f%d (+%d) %.2f ms |", f.bf, f.bf - last, f.ms);
            for (int i = 0; i < FP_COUNT; i++) if (f.cpu[i] > 0.15 || f.gpu[i] > 0.15) std::printf(" %s %.2f/%.2f", framePassName((FramePass)i), f.cpu[i], f.gpu[i]);
            std::printf("\n");
            last = f.bf;
            if (++n >= 400) break;
        }
        frameRecs.clear();
    };
    // --bench: a lap round where the player starts, the same every run.
    const int benchWarm = opt.benchWarm;
    BenchStats benchStats;
    const bool offscreenBench = [] { const char* d = std::getenv("SDL_VIDEODRIVER"); return d && std::string(d) == "offscreen"; }();
    V3 benchCentre = sim.cars.empty() ? V3() : sim.cars[sim.player].frame().com;
    if (opt.bench) std::printf("bench: %d frames after %d to warm up, %dx%d, quality %d%s\n", opt.bench, benchWarm, opt.width, opt.height,
                               presetOf(settings.gfx), opt.benchSync ? ", GPU synced per stage" : "");
    // The bench report file (the Windows build has no console): the machine, the settings, the numbers. --bench-suite: a run after another.
    BenchSuite suite;
    BenchStats& benchSt = opt.benchSuite ? suite.stats() : benchStats;
    MachineInfo machine;
    const std::string benchStamp = benchTimestamp();
    int benchBase = 0, suitePhase = 0;  // (the frame the run started at; suite: 0 measuring, 1 F3 shot due, 2 run over)
    bool suiteDbgOn = false, suiteSummaryDone = false;
    if (opt.bench) {
        machine.gpu = glRenderer ? glRenderer : "", machine.vendor = glVendor ? glVendor : "";
        const char* glv = (const char*)gl::GetString(GL_VERSION);
        const char* sl = (const char*)gl::GetString(0x8B8C /* GL_SHADING_LANGUAGE_VERSION */);
        machine.glVersion = glv ? glv : "", machine.glsl = sl ? sl : "";
        const char* vd = SDL_GetCurrentVideoDriver();
        machine.videoDriver = vd ? vd : "";
        queryMachine(machine);
        const int di = std::max(0, SDL_GetWindowDisplayIndex(win));
        SDL_DisplayMode dm;
        if (SDL_GetCurrentDisplayMode(di, &dm) == 0) {
            machine.refreshHz = dm.refresh_rate;
            const char* dn = SDL_GetDisplayName(di);
            machine.display = std::string(dn ? dn : "display") + " " + std::to_string(dm.w) + "x" + std::to_string(dm.h);
        }
    }
    auto benchRunInfo = [&](const std::string& mapTitle, const char* place) {
        BenchRunInfo ri;
        ri.map = mapTitle, ri.place = place, ri.preset = presetName(presetOf(settings.gfx));
        ri.width = opt.width, ri.height = opt.height, ri.frames = opt.bench, ri.warm = benchWarm, ri.synced = opt.benchSync;
        SDL_GL_GetDrawableSize(win, &ri.drawW, &ri.drawH);
        char b[400];
        const RenderSettings& g = renderer.settings;
        std::snprintf(b, sizeof b, "scale %.2f, shadows %d, MSAA %d, AO %d, bloom %d, grass %d, ground %d, props %d, draw distance %.0f, vsync %d, adaptive %d, physics threads %d",
                      g.renderScale, g.shadows, g.msaa, (int)g.ao, (int)g.bloom, g.grass, g.ground, g.props, g.drawDistance, SDL_GL_GetSwapInterval(),
                      (int)(settings.adaptiveQuality && opt.adaptive == 1), sim.physicsThreads());
        ri.settings = b;
        return ri;
    };
    auto benchPathStem = [&]() {  // (no extension)
        if (!opt.benchOut.empty()) {
            const size_t dot = opt.benchOut.find_last_of('.'), sep = opt.benchOut.find_last_of("/\\");
            return dot != std::string::npos && (sep == std::string::npos || dot > sep) ? opt.benchOut.substr(0, dot) : opt.benchOut;
        }
        return benchDir() + "/" + (opt.benchSuite ? "suite-" + benchStamp + "-" + std::to_string(opt.width) + "x" + std::to_string(opt.height)
                                                  : benchStem(benchStamp, mapName(sim.world.map), presetName(presetOf(settings.gfx)), opt.width, opt.height));
    };
    auto writeSuiteSummary = [&]() {
        if (suiteSummaryDone || suite.done().empty()) return;
        suiteSummaryDone = true;
        const std::string path = !opt.benchOut.empty() ? opt.benchOut : benchPathStem() + ".txt";
        if (writeTextFile(path, suite.summary(machine, benchStamp, opt.width, opt.height, opt.bench, benchWarm))) std::printf("bench: suite summary written to %s\n", path.c_str());
        else std::printf("bench: could not write %s\n", path.c_str());
    };
    // The suite's next run: its map (the car at the foot of Boulder Gully), its preset, a fresh BenchStats and lap.
    auto startNextRun = [&]() -> bool {
        if (!suite.next()) return false;
        const BenchSuite::Entry& e = suite.current();
        if (sim.world.map != e.map) loadMap(e.map);
        if (e.spawn) sim.cars[sim.player].respawn(V3(e.x, sim.world.clearY(e.x, e.z) + 0.05, e.z), e.heading * M_PI / 180);
        applyPreset(settings.gfx, e.preset);
        benchCentre = sim.cars[sim.player].frame().com;
        benchBase = frame, suitePhase = 0;
        std::printf("bench: suite run %d of %d: %s%s%s, %s\n", suite.index() + 1, suite.size(), mapName(e.map), *e.place ? " / " : "", e.place, presetName(e.preset));
        return true;
    };

    // The physics for the next frame runs during this one's GPU wait (the swap), when nothing else touches the sim;
    // the next frame owes only what's left of its time. Not for scripted captures, nor net play or the editor.
    SimAsync simAsync;
    double simCredit = 0;  // time already simulated ahead (s)
    int simAsyncSteps = 0;
    const bool asyncAllowed = opt.simAsync && opt.shotFrames.empty() && !opt.loadTour && opt.script.empty();

    while (running) {
        if (g_termSignals) break;  // (SIGTERM / SIGINT in normal play: quit cleanly)
        if (watchdog) watchdog->frame(frame);
        if (frame == testStallFrame) for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
        uint64_t now = SDL_GetPerformanceCounter();
        const double frameTimeMs = 1000.0 * (double)(now - last) / freq;
        mapLoadTick(frameTimeMs / 1000.0);
        tourTick(frameTimeMs);
        if (opt.benchSuite && (!suite.begun() || suitePhase == 2) && !startNextRun()) {
            writeSuiteSummary();
            running = false;
            break;
        }
        const int bf = frame - benchBase;  // (frames into the bench run)
        if (frame == 90) std::thread([] { meshcache::flush(); }).detach();  // (what the first seconds generated, for the next run: off the main thread)
        for (size_t i = 0; i < afterVisual.size();)
            if (visualReady(*afterVisual[i].first)) {
                auto act = std::move(afterVisual[i].second);
                afterVisual.erase(afterVisual.begin() + i);
                act();
            } else i++;
        // ---- frame timing (frameprof.h): the stage times F3 and --bench show ----
        frameProfile().beginFrame();
        if (opt.bench && bf > benchWarm + opt.bench && (!opt.benchSuite || suitePhase == 0)) {
            std::string title = std::string(mapName(sim.world.map)) + (run.active() ? " / " + run.level.name : std::string()) + ", " +
                                std::to_string(opt.width) + "x" + std::to_string(opt.height) + ", quality " + std::to_string(presetOf(settings.gfx));
            char extra[160];
            std::snprintf(extra, sizeof extra, " (scale %.2f, shadows %d, MSAA %d, AO %d, bloom %d, grass %d, ground %d, props %d)%s",
                          renderer.settings.renderScale, renderer.settings.shadows, renderer.settings.msaa, renderer.settings.ao, renderer.settings.bloom,
                          renderer.settings.grass, renderer.settings.ground, renderer.settings.props, opt.benchSync ? " synced" : "");
            std::fputs(BenchStats::report(benchSt.result(), title + extra).c_str(), stdout);
            printFrameLog();
            printCarBench();
            if (opt.benchSuite) {  // (filed, then one F3 shot of this run, then the next)
                suite.record(mapName(sim.world.map), presetName(presetOf(settings.gfx)));
                if (!dbg.on) dbg.toggle(false), suiteDbgOn = true;
                suitePhase = 1;
            } else {
                const BenchRunInfo ri = benchRunInfo(mapName(sim.world.map) + (run.active() ? " / " + run.level.name : std::string()), "");
                const std::string path = !opt.benchOut.empty() ? opt.benchOut : benchPathStem() + ".txt";
                if (writeTextFile(path, benchFileText(machine, ri, benchSt.result()))) std::printf("bench: report written to %s\n", path.c_str());
                else std::printf("bench: could not write %s\n", path.c_str());
                running = false;
                break;
            }
        }
        {  // (spikes go to the log, with the stages that ran long: stdout, graphics.log on Windows)
            static double typical = 16.7, logged = -1;
            static int spikesLogged = 0;
            const double wall = (double)now / freq;
            if (frame > 10 && frameSpike(frameTimeMs, typical) && (opt.bench || wall - logged > 0.5) && spikesLogged < 2000) {
                char line[200];
                describeSpike(line, sizeof line, frame - 1, frameTimeMs, typical, frameProfile().last);
                std::printf("%s\n", line);
                logged = wall, spikesLogged++;
            }
            if (frameTimeMs < 250) typical = frame <= 10 ? frameTimeMs : typical + (std::min(frameTimeMs, typical * 3) - typical) * 0.05;
        }
        if (opt.bench && bf > benchWarm && bf <= benchWarm + opt.bench && std::getenv("CZ_FRAMELOG")) {  // (every measured frame's stages, for the slow-frame report)
            FrameRec fr;
            fr.bf = bf, fr.ms = frameTimeMs;
            std::copy(frameProfile().last, frameProfile().last + FP_COUNT, fr.cpu);
            for (int i = 0; i < FP_COUNT; i++) fr.gpu[i] = frameProfile().gpuValid ? frameProfile().lastGpu[i] : -1;
            frameRecs.push_back(fr);
        }
        if (opt.bench && bf > benchWarm && bf <= benchWarm + opt.bench) benchSt.add(frameTimeMs, frameProfile().last, frameProfile().gpuValid ? frameProfile().lastGpu : nullptr);
        renderer.gpuTiming = dbg.on || opt.bench;
        renderer.syncTiming = opt.benchSync;
        renderer.gpuFrameBegin();
        renderer.passMark(FP_GAME);
        double dt = scripted ? 1.0 / 60.0 : std::min(0.1, frameTimeMs / 1000.0);
        last = now;
        if (dt > 0) fps += ((float)(1.0 / dt) - fps) * 0.05f;
        hud.devHotkeys = settings.devHotkeys;
        hud.newBuild = watch.poll() && !editorSuspended;  // (not during a playtest: the editor would be lost)
        if (hud.newBuild && std::getenv("CZ_AUTORELOAD")) reload = true, running = false;  // (testing the reload)

        // ---- gamepad: what the pad is doing this frame (its presses are fed in as keys below) ----
        padCtx = editor.active || coop.typing || playback.active() || !padDev.connected() ? pad::Mode::Off
                 : menu.open || garageShop.open || inv.open                                 ? pad::Mode::Menu
                 : coop.mapOpen                                                           ? pad::Mode::Map
                                                                                          : pad::Mode::Drive;
        padLogic.tune = {settings.padSteering, settings.padDeadzone, settings.padRumble, settings.throttle.padScale()};
        padLogic.onFoot = walker.active;
        padLogic.update(padDev.read((SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) != 0), padCtx, dt);
        size_t padTap = 0;
        bool fromPad = false;
        testCtrl = false;
        {  // (text fields take typed text through SDL's text input; the model lab has its own)
            static bool textOn = false;
            const bool want = menu.open || coop.typing || (editor.active && editor.wantsText());
            if (want) SDL_StartTextInput();
            else if (textOn) SDL_StopTextInput();
            textOn = want;
        }
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
                for (int sc0 : it->second) {
                    const int sc = sc0 % 1000;
                    if (sc0 >= 1000) testCtrl = true;  // (Ctrl held for this press: SDL's keyboard state isn't fed by pushed events)
                    for (int down = 1; down >= 0; down--) {
                        SDL_Event ke;
                        std::memset(&ke, 0, sizeof ke);
                        ke.type = down ? SDL_KEYDOWN : SDL_KEYUP;
                        ke.padding[12] = (uint8_t)down;
                        std::memcpy(ke.padding + 16, &sc, 4);
                        SDL_PushEvent(&ke);
                    }
                }
        }
        SDL_Event e;
        while (nextEvent(e)) {
            shiftForce = -1;
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
                const uint8_t* ks = SDL_GetKeyboardState(nullptr);
                coop.typeKey(evKeyScancode(e), shiftDown(), ks[224] || ks[228] || testCtrl, myName(), net);
                continue;
            }
            if (!editor.active && !menu.open && coop.typing && e.type == SDL_TEXTINPUT) {
                coop.typeText(evText(e).c_str());
                continue;
            }
            if (!editor.active && !menu.open && coop.mapOpen && e.type != SDL_QUIT) {
                // The map has the mouse (and Esc / M close it).
                float mx, my;
                if (e.type == SDL_KEYDOWN) {
                    const int sc = evKeyScancode(e);
                    if ((sc == SC_ESCAPE || settings.keys.actionFor(sc, shiftDown(), KCTX_ANY, false) == KA_MAP) && !evKeyRepeat(e)) {
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
            if (garageShop.open && !menu.open && !editor.active &&
                (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEWHEEL)) {
                // The garage has the mouse: rows to point at and click, the model to drag round and zoom.
                float mx = mouseX, my = mouseY;
                int col = -1, row = -1;
                if (e.type == SDL_MOUSEMOTION) {
                    toDrawable(evMotionX(e), evMotionY(e), mx, my);
                    const float dx = mx - mouseX, dy = my - mouseY;
                    mouseX = mx, mouseY = my;
                    if (shopDrag && (evMotionState(e) & 1)) garageShop.turn(dx * 0.012, dy * 0.006);
                    else if (garageShop.hitAt(mx, my, col, row)) garageShop.hover(col, row);
                } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                    toDrawable(evButtonX(e), evButtonY(e), mx, my);
                    mouseX = mx, mouseY = my;
                    if (evButton(e) == 1 && garageShop.hitAt(mx, my, col, row)) shopResult(garageShop.click(col, row, career));
                    else if (evButton(e) == 1 && mx > garageShop.turntableX) shopDrag = true;
                    else if (evButton(e) == 3) shopResult(garageShop.back());
                } else if (e.type == SDL_MOUSEBUTTONUP) {
                    shopDrag = false;
                } else if (evWheelY(e) != 0) {
                    if (mouseX > garageShop.turntableX) garageShop.zoomBy(evWheelY(e) > 0 ? 1 / 1.12 : 1.12);
                    else garageShop.move(evWheelY(e) > 0 ? -1 : 1);
                }
                continue;
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
                case SDL_TEXTINPUT:
                    if (mpShown()) {
                        mpSync();
                        handleMp(mpNav.text(evText(e), mpView));
                    } else if (menu.open) {
                        const uint8_t* ks = SDL_GetKeyboardState(nullptr);
                        menu.ctrl = ks[224] || ks[228] || testCtrl;
                        handleMenu(menu.textInput(evText(e).c_str(), settings));
                    }
                    break;
                case SDL_KEYDOWN: {
                    if (settings.keys.matches(KA_DEV_MENU, evKeyScancode(e), shiftDown()) && !fromPad && !evKeyRepeat(e) && !inv.open && !garageShop.open && !menu.title &&
                        !menu.listeningForKey()) {  // the dev menu (devmenu.h): F12 opens and closes it, whatever DEV HOTKEYS says
                        if (menu.devOpen()) closeMenu();
                        else {
                            const bool fromPause = menu.onPausePage();
                            if (!menu.open) openMenu();
                            menu.showDev(std::max(0, sim.cars[sim.player].spec->model), sim.world.map, fromPause);
                        }
                        break;
                    }
                    if (settings.keys.matches(KA_DEV_TOGGLE, evKeyScancode(e), shiftDown()) && !fromPad && !evKeyRepeat(e) && !menu.open && !inv.open && !garageShop.open) {
                        settings.devHotkeys = !settings.devHotkeys;  // DEV KEYS <-> GAMEPLAY KEYS (devmenu.h has the table)
                        saveSettings();
                        hud.toast(settings.devHotkeys ? "DEV KEYS: THE DEV KEYS ARE TOOLS (" + K(KA_DEV_TOGGLE) + " FOR GAMEPLAY KEYS)" : "GAMEPLAY KEYS (" + K(KA_DEV_TOGGLE) + " FOR DEV KEYS)");
                        break;
                    }
                    if (mainMenuShown()) {  // the new main menu (mainmenu.h)
                        if (previewMenu && evKeyScancode(e) == SC_ESCAPE && mainNav.page == mainmenu::P_FRONT) {  // (the camp editor's preview: back to the editor)
                            previewMenu = false;
                            closeMenu();
                            if (!returnToEditor()) hud.toast("CAN'T GO BACK TO THE EDITOR");
                            break;
                        }
                        {
                            const int before = mainNav.sel, pageBefore = mainNav.page;
                            const mainmenu::Result r = mainNav.key(evKeyScancode(e));
                            if (activationKey(evKeyScancode(e)) || r.act != mainmenu::A_NONE || mainNav.page != pageBefore) uiClick();
                            else if (mainNav.sel != before) uiHover();
                            handleMain(r);
                        }
                        break;
                    }
                    if (menu.open && menu.title && classicTitle && campWorld && menu.pageNow() == 0 && evKeyScancode(e) == SC_ESCAPE) {  // (the old title page: Esc is back to the new one)
                        classicTitle = false;
                        break;
                    }
                    if (mpShown()) {  // the multiplayer pages (mpmenu.h) take every key
                        mpSync();
                        const uint8_t* ks = SDL_GetKeyboardState(nullptr);
                        const bool ctrl = ks[224] || ks[228] || testCtrl, shift = ks[SC_LSHIFT] || ks[SC_RSHIFT];
                        const int before = mpNav.sel, pageBefore = mpNav.page;
                        const mpmenu::Result r = mpNav.key(evKeyScancode(e), ctrl, shift, mpView);
                        if (activationKey(evKeyScancode(e)) || mpNav.page != pageBefore) uiClick();
                        else if (mpNav.sel != before) uiHover();
                        handleMp(r);
                        break;
                    }
                    if (menu.open) {  // the menu takes every key (repeats too: hold to slide)
                        const uint8_t* ks = SDL_GetKeyboardState(nullptr);
                        menu.shift = ks[SC_LSHIFT] || ks[SC_RSHIFT];
                        menu.ctrl = ks[224] || ks[228] || testCtrl;
                        {
                            const int before = menu.selected();
                            const PauseMenu::Result r = menu.key(evKeyScancode(e), settings);
                            if (activationKey(evKeyScancode(e))) uiClick();
                            else if (menu.selected() != before) uiHover();
                            handleMenu(r);
                        }
                        break;
                    }
                    if (inv.open) {  // the inventory takes every key
                        const int k = evKeyScancode(e);  // (the physical key: WASD are held actions in the game's own switch)
                        if (k == SC_W || k == SC_UP) inv.move(0, -1);
                        else if (k == SC_S || k == SC_DOWN) inv.move(0, 1);
                        else if (k == SC_A || k == SC_LEFT) inv.move(-1, 0);
                        else if (k == SC_D || k == SC_RIGHT) inv.move(1, 0);
                        else if (evKeyRepeat(e)) {
                        } else if (k == SC_ESCAPE || k == SC_TAB || k == SC_Q || settings.keys.matches(KA_INVENTORY, k, false) || settings.keys.matches(KA_NEXT_VEHICLE, k, false)) closeInventory();
                        else if (k == SC_E || k == SC_RETURN || k == SC_KP_ENTER) {
                            if (inv.sel != INV_NONE) inventoryChoose(inv.sel);
                        }
                        break;
                    }
                    if (evKeyRepeat(e)) break;
                    // The key as the in-game switch knows it: the player's keyboard bindings turned back into
                    // the default keys the switch is written against (a gamepad press already is one).
                    const unsigned kctx = (walker.active ? KCTX_FOOT : KCTX_DRIVE) | (playback.active() ? KCTX_SCENARIO : 0u);
                    const Keybinds::Press kh = settings.keys.resolve(evKeyScancode(e), shiftDown(), fromPad, kctx, settings.devHotkeys);
                    const int gsc = kh.key;
                    if (!fromPad && kh.action >= 0) shiftForce = kh.shift ? 1 : 0;  // (the switch asks shiftDown(): the action's own answer)
                    if (garageShop.open) {
                        // (Its own keys, as pressed; a rebound garage key closes it too.)
                        const int sc = kh.action == KA_GARAGE ? (int)shop::K_G : (int)evKeyScancode(e);
                        shopResult(garageShop.key(sc, career));
                        break;
                    }
                    if (kh.action == KA_CHAT) {
                        coop.typing = true;
                        coop.draft.clear(), coop.draftEd.reset();
                        break;
                    }
                    if (kh.action >= 0 && isDevAction((KeyAction)kh.action) &&  // (the dev tools: DEV KEYS mode only, resolve left them out otherwise; the dev menu has them all)
                        devmenu::dispatchKey(settings.keys, evKeyScancode(e), shiftDown(), settings.devHotkeys, kctx,
                                             [](void* fn, const devmenu::Action& a) { (*static_cast<std::function<void(const devmenu::Action&)>*>(fn))(a); }, &runDev))
                        break;
                    switch (gsc) {
                        case SC_ESCAPE:  // (the pause menu: Esc on the keyboard, Start on the pad, the MENU key if rebound)
                            if (hud.showHelp) hud.showHelp = false;
                            else openMenu();
                            break;
                        case SC_8:  // the throttle limit: 100 50 25 10 % (rebindable)
                            settings.throttle.cyclePreset();
                            saveSettings();
                            hud.toast("THROTTLE " + throttle::percent(settings.throttle.cap));
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
                                hud.toast("NO REPAIRS ON THIS LEVEL: " + K(KA_REPAIR) + " TWICE TO START AGAIN");
                            } else {
                                hud.confirmReset = 0.45;
                                sim.recoverPlayer();
                                hud.toast("REPAIRED (" + K(KA_REPAIR) + " TWICE: BACK TO THE START)");
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
                        case SC_V:  // (the hook-up is done on foot: grab the line at the bumper, walk it to the anchor, hold E)
                        case SC_O: {
                            const bool winchKey = gsc == SC_V;
                            hud.toast(walker.active ? (sim.handLine.on() ? "WALK THE LINE TO THE ANCHOR AND HOLD " + K(KA_INTERACT) + " TO HOOK IT (" + K(KA_PUT_DOWN) + " LETS GO)"
                                                                         : "HOLD " + K(KA_INTERACT) + " AT THE " + (winchKey ? "WINCH FAIRLEAD" : "RECOVERY POINT") + " TO GRAB THE LINE")
                                                    : sim.seatHook(sim.player, winchKey ? ROPE_WINCH : ROPE_STRAP));
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
                        case SC_H: hud.showHelp = !hud.showHelp; break;
                        case SC_F3: runDev({devmenu::FPS, 0}); break;  // the debug info screen (always on)
                        case SC_F5: runDev({devmenu::RELOAD, 0}); break;  // load the new build (always on)
                        case SC_F1:  // leaves a playtest for the editor
                            if (editorSuspended && !editor.active && !returnToEditor()) hud.toast("CAN'T GO BACK TO THE EDITOR HERE");
                            break;
                        case SC_J:
                            if (playback.active()) break;  // (a scenario: J is its checks list, a dev key)
                            if (walker.active && walker.carrying) hud.toast("PUT THE WHEEL DOWN FIRST (" + K(KA_PUT_DOWN) + ")");
                            else if (walker.active) getIn(interact.current.kind == IA_ENTER_CAR || interact.current.kind == IA_RIDE_ALONG ? interact.current.car : -1, true);  // (the door the prompt is on, else the nearest)
                            else getOut();
                            break;
                        case SC_P: {
                            Car& c = sim.cars[sim.player];
                            c.parkBrake = !c.parkBrake;
                            hud.toast(c.parkBrake ? "PARKING BRAKE ON" : "PARKING BRAKE OFF");
                            break;
                        }
                        case SC_F6:
                            if (net.active()) hud.toast("NO PAUSING IN MULTIPLAYER (" + K(KA_MENU) + " STILL OPENS THE MENU)");
                            else sim.paused = !sim.paused;
                            break;
                        case SC_X: {
                            Car& c = sim.cars[sim.player];
                            c.tractionControl = !c.tractionControl;
                            hud.toast(!c.spec->tractionFitted ? "NO TRACTION CONTROL ON THIS CAR"
                                      : c.tractionControl ? "TRACTION CONTROL ON" : "TRACTION CONTROL OFF");
                            break;
                        }
                        case SC_G:
                            if (walker.active) {
                                if (sim.handLine.on()) { sim.dropHandLine(); hud.toast("LET GO OF THE LINE: IT SPOOLS BACK"); }
                                else if (walker.carrying) Interact::drop(sim, walker, lastView.target - lastView.eye);  // (on foot: put down what you carry)
                                else if (walker.holdingJack) Interact::dropJack(sim, walker);  // (put the jack down)
                                else if (walker.hand.any()) hud.toast("PUT IT BACK IN THE TRUCK: PRESS " + K(KA_INTERACT) + " AT ITS BACK");
                            }
                            else openGarage();
                            break;
                        case SC_E:
                        case SC_Q: {
                            if (cam.mode == 3 || walker.active) break;  // (flying the drone / on foot: Q E aren't the gears)
                            Car& c = sim.cars[sim.player];
                            if (c.shift(gsc == SC_E ? 1 : -1) && !c.manualShift) {
                                c.manualShift = true;  // taking over the gears switches to manual
                                hud.toast("MANUAL SHIFTING (" + K(KA_MANUAL) + " FOR AUTOMATIC)");
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
                                hud.toast("NO TRANSFER CASE ON THIS CAR (" + K(KA_LIGHTS) + ": LIGHTS)");
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
                                hud.toast("SHIFT INTO 4WD (" + K(KA_TRANSFER) + ") TO LOCK THE FRONT");
                            } else if (len(c.frame().vel) > 30 / 3.6) {
                                hud.toast("SLOW DOWN TO ENGAGE THE LOCKER");
                            } else {
                                c.setLocker(front, true);
                                hud.toast(std::string(which) + " LOCKER ON");
                            }
                            break;
                        }
                        case SC_I:  // (driving, I is a dev key: spawn a parked vehicle)
                            if (walker.active) toggleInventory();
                            break;
                        case SC_U: {  // autopilot on (to the place picked in the menu) or off
                            Car& c = sim.cars[sim.player];
                            if (c.pilot.active || c.ai) stopPilot();
                            else startPilot();
                            break;
                        }
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
                    if (inv.open) {  // (a pointer: hovering a card chooses it)
                        float mx, my;
                        toDrawable(evMotionX(e), evMotionY(e), mx, my);
                        const int h = inv.hit(invLay, mx, my);
                        if (h != INV_NONE) inv.sel = h;
                    } else if (mainMenuShown()) {
                        float mx, my;
                        toDrawable(evMotionX(e), evMotionY(e), mx, my);
                        const int h = mainNav.hitAt(mx, my);
                        if (h >= 0) {
                            const int before = mainNav.sel;
                            mainNav.setSel(h);
                            if (mainNav.sel != before) uiHover();
                        }
                    } else if (mpShown()) {
                        float mx, my;
                        toDrawable(evMotionX(e), evMotionY(e), mx, my);
                        mpSync();
                        const int h = mpNav.hitAt(mx, my);
                        if (h >= 0) {
                            const int before = mpNav.sel;
                            mpNav.setSel(h, mpView);
                            if (mpNav.sel != before) uiHover();
                        }
                    } else if (menu.open) {
                        float mx, my;
                        toDrawable(evMotionX(e), evMotionY(e), mx, my);
                        {
                            const int before = menu.selected();
                            const PauseMenu::Result r = menu.mouseMove(mx, my, evMotionState(e) & 1, settings);
                            if (menu.selected() != before) uiHover();
                            handleMenu(r);
                        }
                    } else if (walker.active && !editor.active) {  // on foot: the mouse turns your head
                        footYaw -= evMotionXrel(e) * 0.0035;
                        footPitch = clampd(footPitch - evMotionYrel(e) * 0.0035, -1.45, 1.45);
                    } else if (evMotionState(e) & 0x5) {  // left or right button held
                        cam.drag(evMotionXrel(e), evMotionYrel(e));
                        dragging = true;
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (inv.open) {
                        float mx, my;
                        toDrawable(evButtonX(e), evButtonY(e), mx, my);
                        const int h = inv.hit(invLay, mx, my);
                        if (evButton(e) == 1 && h != INV_NONE) inv.sel = h, inventoryChoose(h);
                        else if (evButton(e) == 3 || (evButton(e) == 1 && !invLay.panel.has(mx, my))) closeInventory();
                        break;
                    }
                    if (mainMenuShown()) {
                        float mx, my;
                        toDrawable(evButtonX(e), evButtonY(e), mx, my);
                        const int h = mainNav.hitAt(mx, my);
                        if (evButton(e) == 1 && h >= 0) uiClick(), handleMain(mainNav.choose(h));
                        else if (evButton(e) == 3) handleMain(mainNav.back());
                        break;
                    }
                    if (mpShown()) {
                        float mx, my;
                        toDrawable(evButtonX(e), evButtonY(e), mx, my);
                        mpSync();
                        const int h = mpNav.hitAt(mx, my);
                        if (evButton(e) == 1 && h >= 0) uiClick(), handleMp(mpNav.choose(h, mpView));
                        else if (evButton(e) == 3) handleMp(mpNav.back(mpView));
                        break;
                    }
                    if (menu.open && evButton(e) == 1) {
                        float mx, my;
                        toDrawable(evButtonX(e), evButtonY(e), mx, my);
                        if (menu.overRow(mx, my)) uiClick();
                        handleMenu(menu.mouseDown(mx, my, settings));
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    dragging = false;
                    menu.mouseUp();
                    break;
                case SDL_MOUSEWHEEL:
                    if (!menu.open) cam.dist = clampd(cam.dist * std::pow(0.88, evWheelY(e)), 3.0, 60.0);
                    else if (mainMenuShown()) {
                        if (evWheelY(e) != 0 && mainNav.page != mainmenu::P_FRONT) mainNav.move(evWheelY(e) > 0 ? -1 : 1);
                    } else if (mpShown()) {
                        if (evWheelY(e) != 0) mpSync(), mpNav.move(evWheelY(e) > 0 ? -1 : 1, mpView);
                    } else if (evWheelY(e) != 0) menu.wheel(evWheelY(e) > 0 ? -1 : 1, settings);
                    break;
            }
        }

        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        const Keybinds& kb = settings.keys;
        const bool shiftHeld = keys[SC_LSHIFT] || keys[SC_RSHIFT];
        bool up = kb.held(KA_THROTTLE, keys, shiftHeld) || keys[SC_UP], down = kb.held(KA_BRAKE, keys, shiftHeld) || keys[SC_DOWN];
        bool left = kb.held(KA_STEER_LEFT, keys, shiftHeld) || keys[SC_LEFT], right = kb.held(KA_STEER_RIGHT, keys, shiftHeld) || keys[SC_RIGHT];
        bool hb = kb.held(KA_HANDBRAKE, keys, shiftHeld);
        int winch = kb.held(KA_WINCH_OUT, keys, shiftHeld) ? -1 : kb.held(KA_WINCH_IN, keys, shiftHeld) ? 1 : 0;
        int airing = kb.held(KA_AIR_DOWN, keys, shiftHeld) ? -1 : kb.held(KA_AIR_UP, keys, shiftHeld) ? 1 : 0;
        if (cam.mode == 3 && !menu.open && !editor.active && !coop.typing && !coop.mapOpen) {
            // The drone: the keys fly the camera; the truck waits on its handbrake.
            const double speed = (8 + std::max(0.0, cam.droneEye.y - sim.world.height(cam.droneEye.x, cam.droneEye.z)) * 0.6) * (shiftHeld ? 3 : 1);
            const V3 f(std::cos(cam.droneYaw), 0, -std::sin(cam.droneYaw)), l(f.z, 0, -f.x);
            V3 m;
            if (up) m += f;
            if (down) m -= f;
            if (left) m += l;
            if (right) m -= l;
            if (kb.held(KA_GEAR_UP, keys, shiftHeld)) m.y += 1;
            if (kb.held(KA_GEAR_DOWN, keys, shiftHeld)) m.y -= 1;
            if (padCtx == pad::Mode::Drive) m += f * (double)padLogic.in.walkForward + l * (double)padLogic.in.walkTurn;  // (gamepad: left stick flies)
            if (len(m) > 0) cam.droneEye += norm(m) * (speed * dt);
            if (opt.eyeFly != 0 && opt.eyeSet) {
                const double over = cam.droneEye.y - sim.world.height(cam.droneEye.x, cam.droneEye.z);
                cam.droneEye += f * (opt.eyeFly * dt);
                cam.droneEye.y = sim.world.height(cam.droneEye.x, cam.droneEye.z) + over;
            }
            cam.droneEye.y = std::max(cam.droneEye.y, sim.world.height(cam.droneEye.x, cam.droneEye.z) + 0.5);
            up = down = left = right = false;
            hb = true;
        }
        if (garageShop.open || menu.open || editor.active || coop.typing || coop.mapOpen) {  // parked while you work on it
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
            hud.airing = airing != 0;
            for (int i = 0; i < (int)sim.cars.size(); i++) sim.setWinch(i, i == sim.player ? winch : 0);
            if (airing && len(sim.cars[sim.player].frame().vel) > 0.5) hud.toast("STOP TO LET AIR OUT OR PUMP UP THE TIRES");
        }
        if (scripted) {
            bool donut = opt.script == "donut";
            up = frame > 30 && (opt.script == "drive" || opt.script == "left" || donut || (opt.script == "hud" && !std::getenv("CZ_HUD_STILL")) || opt.script == "walk" || opt.script == "line");  // (walk, line: on foot, see below)
            if (opt.script == "hud") {  // screenshots of the contextual HUD: CZ_HUD_STILL=1 (stay put), CZ_HUD_FUEL=0.15 (tank fraction from frame 5), CZ_HUD_HIT=F (a knock at frame F)
                Car& c = sim.cars[sim.player];
                if (frame == 5 && std::getenv("CZ_HUD_FUEL") && c.spec->fuelCapacity > 0) c.fuel = c.spec->fuelCapacity * std::atof(std::getenv("CZ_HUD_FUEL"));
                if (std::getenv("CZ_HUD_HIT") && frame == std::atoi(std::getenv("CZ_HUD_HIT")))
                    for (size_t k = 0; k < c.beams.size(); k += 3)  // (bent beams: what damage is measured from)
                        if (c.beams[k].type != BEAM_MOUNT) c.beams[k].L0 = c.beams[k].Lorig * 1.04;
            }
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
            if (opt.script == "trafficlod") {  // screenshots of far traffic as proxies and the handover: one AI car, you 600 m up the road facing it
                up = down = left = right = false;
                if (frame == 5) sim.spawnTraffic(*sim.cars[sim.player].spec);
                if (frame == 8 && sim.cars.size() > 1) {
                    const auto& road = sim.world.loopRoad;
                    const int n = (int)road.size();
                    int j = sim.cars.back().aiIndex;
                    double acc = 0;
                    while (acc < 600) acc += len(road[(j + 1) % n] - road[j]), j = (j + 1) % n;
                    const V3 t = norm(V3(road[(j + 1) % n].x - road[j].x, 0, road[(j + 1) % n].z - road[j].z));
                    const V3 p = road[j] - V3(-t.z, 0, t.x) * 2.6;  // (the oncoming lane)
                    sim.teleportPlayer(p.x, p.z, std::atan2(t.z, -t.x));
                }
                if (frame > 8 && frame % 60 == 0 && sim.cars.size() > 1)
                    std::printf("trafficlod: frame %d, %d proxies, car %.0f m away, %.1f m/s, %s\n", frame, sim.proxyCount(), len(sim.cars.back().frame().com - sim.cars[sim.player].frame().com),
                                len(sim.cars.back().frame().vel), sim.cars.back().proxy ? "PROXY" : "full sim");
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
                    if (opt.script == "work-fix" || opt.script == "work-repair") c.wheels[k].shaftBroken = true;
                    if (opt.script == "work-repair") c.jackHeight = 0.3, walker.hand = TrunkRef{TK_TOOL, TOOL_HAMMER};  // (up, the wheel comes off below)
                    const V3 aim = opt.script == "work-fix" || opt.script == "work-repair" ? hub + (c.frame().com - hub) * 0.25 : std::getenv("CZ_WORK_PLACE") || opt.script == "work-jack" ? Interact::jackAim(sim, c, k) : hub;
                    walker.p = hub + out * 1.0 - c.frame().fwd * 0.3;
                    walker.p.y = sim.world.height(walker.p.x, walker.p.z);
                    const V3 d = norm(aim - (walker.p + V3(0, 1.62, 0)));
                    footYaw = std::atan2(-d.z, d.x), footPitch = std::asin(d.y);
                }
                // work-repair: a CV axle job half done (the corner up, the wheel off and laid down, the axle nut off): the next step's prompt and the checklist (CZ_WORK_E: and the work)
                if (opt.script == "work-repair" && frame >= 40 && frame < 70 && k < (int)c.wheels.size() && c.wheels[k].mounted) {
                    WheelItem it;
                    if (sim.removeWheel(sim.player, k, it)) {
                        const Wheel& wh = c.wheels[k];
                        const V3 hub = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5, out = norm(c.nodes[wh.outer].p - c.nodes[wh.inner].p);
                        sim.dropWheel(it, hub + out * 0.9 + c.frame().fwd * 1.1, c.frame().fwd);
                    }
                    if (!c.wheels[k].mounted && c.fieldJobs.empty()) {
                        Car::FieldJob j;
                        j.kind = Fix::CV_AXLE, j.wheel = k, j.done = 1;
                        c.fieldJobs.push_back(j);
                    }
                }
                scriptE = opt.script == "work-repair" ? frame > 70 && std::getenv("CZ_WORK_E") : frame > 70;
            }
            if (opt.script == "prompts") {  // screenshots of the on-foot prompts: CZ_PROMPT=wheel|door|trunk|near (what the crosshair is near), CZ_PROMPT_OFF=m (how far off the middle of it)
                Car& c = sim.cars[sim.player];
                const std::string what = std::getenv("CZ_PROMPT") ? std::getenv("CZ_PROMPT") : "wheel";
                const double off = std::getenv("CZ_PROMPT_OFF") ? std::atof(std::getenv("CZ_PROMPT_OFF")) : 0.25;
                if (frame == 20) c.handbrake = 1, getOut();
                if (frame == 30) {
                    const Frame fr = c.frame();
                    const V3 fwd = norm(V3(fr.fwd.x, 0, fr.fwd.z)), left(fwd.z, 0, -fwd.x);
                    V3 stand, aim;
                    if (what == "door") aim = prompts::doorAt(c), stand = aim + left * 1.9 - fwd * 0.5;
                    else if (what == "trunk") aim = trunk::gateAt(c) + V3(0, off, 0), stand = trunk::gateAt(c) - fwd * 1.9 + left * 0.3;
                    else if (what == "near") stand = fr.com + left * (c.spec->width / 2 + 2.2) - fwd * 1.0, aim = stand + left * 5.0 + fwd * 3.0;
                    else {
                        const int k = std::getenv("CZ_WORK_WHEEL") ? std::atoi(std::getenv("CZ_WORK_WHEEL")) : 2;
                        const Wheel& wh = c.wheels[k];
                        const V3 hub = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5, out = norm(c.nodes[wh.outer].p - c.nodes[wh.inner].p);
                        aim = hub + fwd * off, stand = hub + out * 1.9 - fwd * 0.6;
                    }
                    walker.p = V3(stand.x, sim.world.height(stand.x, stand.z), stand.z);
                    const V3 d = norm(aim - (walker.p + V3(0, 1.62, 0)));
                    footYaw = std::atan2(-d.z, d.x), footPitch = std::asin(d.y);
                }
            }
            if (opt.script == "damage") {  // screenshots of broken parts: CZ_BREAK=cv:fl,tie:fr,bend:fl,shaft:f,spring:rl,leaf:rr,flood,hood (corner = front/rear + left/right),
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
                        else if (what == "hood") c.setHood(true);  // (nothing broken: the hood up, for the engine bay)
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
            if (opt.script == "trunk" || opt.script == "inventory") {  // screenshots of the back of your truck (shut, E prompt) and of its inventory menu
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
                auto envInt = [](const char* name, int def) { return std::getenv(name) ? std::atoi(std::getenv(name)) : def; };
                scriptE = false;
                {  // E at the back (CZ_INV_TAKE=slot: take it and open it again; CZ_INV_SEL=slot: the one chosen; CZ_INV_PAD=1: the pad's hints)
                    if (frame == 20) c.handbrake = 1, getOut();
                    if (frame == 24) stand(std::getenv("CZ_INV_BEHIND") ? std::atof(std::getenv("CZ_INV_BEHIND")) : 1.9), look(trunk::gateAt(c));
                    if (frame >= 24 && !inv.open && frame < 400) look(trunk::gateAt(c));
                    scriptE = (frame == 44 || frame == 120) && !inv.open;
                    if (frame == 90 && inv.open && envInt("CZ_INV_TAKE", -1) >= 0) inventoryChoose(envInt("CZ_INV_TAKE", 0));
                    if (frame >= 100 && inv.open && std::getenv("CZ_INV_SEL")) inv.sel = envInt("CZ_INV_SEL", 0);
                    if (frame == 100 && std::getenv("CZ_INV_PAD")) pad::ui.last = true;
                    if (frame == 200 && std::getenv("CZ_INV_CLOSE")) closeInventory();
                }
            }
            if (opt.script == "cars" && frame == 2) {  // (CZ_BENCH: CZ_CARS vehicles in view, models mixed, in rows ahead of yours; CZ_CARS_AWAKE=1 keeps them simulated)
                const char* env = std::getenv("CZ_CARS");
                const int want = std::max(1, env ? std::atoi(env) : 8);
                static const int kMix[] = {7, 2, 3, 6, 1};
                const auto& cat = vehicleCatalog();
                const Frame fr = sim.cars[sim.player].frame();
                V3 fwd = norm(V3(fr.fwd.x, 0, fr.fwd.z)), side(-fwd.z, 0, fwd.x);
                for (int k = 1; k < want; k++) {
                    const int row = (k - 1) / 4, col = (k - 1) % 4;
                    const V3 p = fr.com + fwd * (9.0 + 8.0 * row) + side * ((col - 1.5) * 4.2);
                    const int m = kMix[(k - 1) % 5];
                    if (m >= (int)cat.size()) continue;
                    sim.addCar(cat[m], V3(p.x, sim.world.clearY(p.x, p.z) + 0.05, p.z), std::atan2(-fwd.z, fwd.x) + 0.35 * (col - 1.5), sim.nextColor());
                    sim.cars.back().handbrake = 1;
                }
            }
            if (opt.script == "cars" && std::getenv("CZ_CARS_AWAKE"))
                for (Car& c : sim.cars) c.wake();
            if (opt.script == "winch" || opt.script == "winchout" || opt.script == "strap") {  // screenshots of the recovery gear
                if (frame == 20) std::printf("%s\n", (opt.script == "strap" ? sim.hookStrap(sim.player) : sim.hookWinch(sim.player)).c_str());
                winch = frame <= 20 ? 0 : opt.script == "winch" ? 1 : opt.script == "winchout" ? -1 : 0;  // (winchout: paying out, the slack line)
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
                    World::setSessionSections(net.sections);
                    if (net.map != sim.world.map || sim.world.parkEdited || !net.sections.empty() || campWorld) loadMap(net.map);
                    net.adopt(sim);
                    hud.toast("JOINED " + net.ownerName(0) + "'S GAME ON " + mapName(net.map) + (net.myLink() == NetSession::LINK_RELAYED ? " (THROUGH THE RELAY)" : ""));
                    std::printf("joined (player %d)\n", net.self);
                    rememberJoin(joinWhat);
                } else if (st < 0) {
                    joinFailed();
                    std::printf("can't join: %s\n", net.error.c_str());
                }
            }
            if (!net.joining()) net.receive(sim, netNow);
            // Vehicles that changed hands (NetSession::requestTake): the one you asked for, yours someone took, the ride you're on.
            for (const NetSession::CarEvent& e : net.carEvents) {
                if (rideId && e.oldId == rideId) rideId = e.newId;
                if (job.wreckId && e.oldId == job.wreckId) job.wreckId = e.newId;
                const int i = carById(e.newId);
                const std::string what = i >= 0 ? std::string(sim.cars[i].spec->name) : std::string("VEHICLE");
                if (e.kind == NetSession::CarEvent::GOT && e.asked && e.oldId == enterAsk) {
                    enterAsk = 0;
                    if (walker.active && i >= 0 && len(sim.cars[i].frame().com - walker.p) < 10) getIn(i, true);
                    else hud.toast("THE " + what + " IS YOURS NOW");
                } else if (e.kind == NetSession::CarEvent::GOT) {
                    hud.toast("THE " + what + " LEFT BEHIND IS YOURS NOW (PARKED)");
                } else if (e.kind == NetSession::CarEvent::REFUSED && e.oldId == enterAsk) {
                    enterAsk = 0;
                    hud.toast("CAN'T GET IN: " + e.why);
                } else if (e.kind == NetSession::CarEvent::GAVE) {
                    hud.toast(net.ownerName(e.newId) + " TOOK YOUR " + what);
                    if (walker.active) backToMine();  // (on foot by it: the nearest of yours is "your truck" now)
                }
            }
            net.carEvents.clear();
            if (enterAsk && net.asking() != enterAsk) enterAsk = 0;  // (the session's gone)
            if (rideId) {  // riding along: the view stays on their vehicle; if it's gone, you're set down where it was
                const int ri = carById(rideId);
                if (ri < 0) {
                    rideId = 0;
                    walker.p = V3(rideAt.x + 2.0, sim.world.height(rideAt.x + 2.0, rideAt.z) + 0.02, rideAt.z);
                    walker.v = V3();
                    walker.active = true, walker.grounded = false;
                    footYaw = walker.yaw, footPitch = -0.1;
                    cam.initialized = false;
                    backToMine();
                    hud.toast("THE RIDE'S OVER: THEY'RE GONE");
                } else {
                    sim.player = ri;
                    rideAt = sim.cars[ri].frame().com;
                    if (!sim.cars[ri].remote) rideId = 0;  // (it's yours now: you're driving)
                }
            } else if (!walker.active && sim.player < (int)sim.cars.size() && sim.cars[sim.player].remote) {  // (in one that isn't yours, not riding: out beside it)
                walker.exitCar(sim.world, sim.cars[sim.player]);
                footYaw = walker.yaw, footPitch = -0.1;
                cam.initialized = false;
                backToMine();
            }
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
                if (bool hornState = false; horn::parse(m.text, hornState)) {  // a buddy's horn goes down or up (horn.h)
                    hornRemote.hear(m.player, hornState, clock);
                    continue;
                }
                if (m.player >= 0 && m.player < net::kMaxPeers && repairIn.onMessage(sim, m.player, net.players[m.player].name, m.text, clock)) continue;  // a buddy's work on foot, jack, dropped part, repair
                if (m.text == "jobstop") {
                    jobOn = false;
                    continue;
                }
                if (m.text.rfind("found ", 0) == 0) {  // a buddy found a place: it counts for you too
                    std::string name;
                    if (takeFoundMessage(coop.discovered, sim.world.map, discoverySpots(), m.text, name)) {
                        jobBanner = std::max(jobBanner, 4.0);
                        jobBannerText = "DISCOVERED: " + name;
                        if (!scripted) coop.saveDiscoveries(discoveriesPath);
                    }
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
                if (m.text.rfind("ride ", 0) == 0 && m.player >= 0 && m.player < net::kMaxPeers) {  // a buddy got in with someone: not on foot
                    const int ri = carById((uint32_t)std::strtoul(m.text.c_str() + 5, nullptr, 10));
                    if (ri >= 0 && !sim.cars[ri].remote) hud.toast(net.players[m.player].name + " IS RIDING ALONG WITH YOU");
                    coop.onMessage(net.players[m.player].name, "inside");
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
                const bool still = inv.open;  // (the menu is up: you stand and look at it)
                {  // crouch: hold the key (or L3), or press it to toggle (Settings > Controls)
                    const uint8_t* cks = SDL_GetKeyboardState(nullptr);
                    const bool down = !menu.open && !inv.open && (settings.keys.held(KA_CROUCH, cks, cks[SC_LSHIFT] || cks[SC_RSHIFT]) || (padCtx == pad::Mode::Drive && padLogic.in.crouchHeld));
                    if (settings.crouchToggle) {
                        if (down && !crouchWas) crouchLatch = !crouchLatch;
                        walker.crouching = crouchLatch;
                    } else walker.crouching = down;
                    crouchWas = down;
                    if (std::getenv("CZ_CROUCH") && scripted) walker.crouching = true;  // (screenshots)
                }
                const double strideBefore = walker.stride;
                walker.step(sim.world, sim, dt, still ? 0 : (up ? 1 : 0) - (down ? 1 : 0), still ? 0 : (left ? 1 : 0) - (right ? 1 : 0), (shiftHeld || (scripted && std::getenv("CZ_RUN"))) && !still, hb && !still, viewYaw);  // (CZ_RUN: screenshots running)
                if (audioOn && walker.grounded && walker.inWater < 0.2 && stepped(strideBefore, walker.stride))  // (own steps: on the ground, out of the water)
                    audio.play(stepSfx(sim.world.surfaceAt(walker.p.x, walker.p.z)), (walker.crouching ? 0.12f : 0.25f) * (len(V3(walker.v.x, 0, walker.v.z)) > 3.5 ? 1.6f : 1.0f), 0, 0, 0.95f + 0.1f * (float)(walker.stride - std::floor(walker.stride)));
                {  // hands on: hold E (pad: X) for the job in front of you
                    const uint8_t* ks = SDL_GetKeyboardState(nullptr);
                    const bool holdE = (!menu.open && !inv.open && settings.keys.held(KA_INTERACT, ks, ks[SC_LSHIFT] || ks[SC_RSHIFT])) || (padCtx == pad::Mode::Drive && padLogic.in.interact) || scriptE;
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
                        if (inv.open && (inv.car < 0 || inv.car >= (int)sim.cars.size() || len(walker.p - trunk::gateAt(sim.cars[std::max(inv.car, 0)])) > trunk::kReach)) closeInventory();  // (walked off)
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
                    if (sim.handLine.on()) {  // (the hook in your right hand, low in front; hooking up, it goes to the anchor)
                        const double hy = cam.mode != 3 ? footYaw : walker.yaw;
                        const V3 hf(std::cos(hy), 0, -std::sin(hy)), hr = cross(hf, V3(0, 1, 0));
                        V3 hold = walker.p + V3(0, 1.06, 0) + hf * 0.4 + hr * 0.2;
                        const InteractOption& io = interact.current;
                        if (interact.progress > 0 && io.kind == IA_HOOK_LINE && std::isfinite(io.at.x)) hold = hold + (io.at - hold) * smoothstep(0, 1, std::min(1.0, interact.share() * 1.4));
                        sim.handLine.hand = hold;
                    }
                    if (sim.handCable.on()) {  // (the cables' free end in your hand while you walk them to the other battery; battery.h)
                        if (!(walker.hand.kind == TK_TOOL && walker.hand.id == TOOL_CABLES)) sim.handCable = Sim::HandCable();
                        else {
                            const double hy = cam.mode != 3 ? footYaw : walker.yaw;
                            const V3 hf(std::cos(hy), 0, -std::sin(hy)), hr = cross(hf, V3(0, 1, 0));
                            V3 hold = walker.p + V3(0, 1.0, 0) + hf * 0.4 + hr * 0.2;
                            const InteractOption& io = interact.current;
                            if (interact.progress > 0 && io.kind == IA_CLAMP_CABLE && std::isfinite(io.at.x)) hold = hold + (io.at - hold) * smoothstep(0, 1, std::min(1.0, interact.share() * 1.4));
                            sim.handCable.hand = hold;
                        }
                    }
                    const std::string said = interact.update(sim, walker, lastView.eye - fpWork, lastView.target - lastView.eye, holdE, dt);  // (from where you stand, not where you've leaned in to)
                    focus = prompter.update(sim, walker, interact, interact.nearby, lastView.eye - fpWork, lastView.target - lastView.eye);
                    if (!said.empty()) hud.toast(said);
                    if (interact.enterCar >= 0) {  // E at a door
                        const int k = interact.enterCar;
                        interact.enterCar = -1;
                        getIn(k, true);
                    }
                    if (interact.openedInventory >= 0) {  // E at the back
                        inv.show(interact.openedInventory);
                        if (audioOn) audio.play(SFX_TOOLBOX_OPEN, 0.7f);
                        interact.openedInventory = -1;
                    }
                    if (inv.open) inv.refresh(sim, interact.own, walker);
                    {  // a repair step done (a row of the checklist ticked, or the job gone with its last step): the part clunks home
                        static std::string clTitle;
                        static int clDone = 0;
                        int done = 0;
                        for (bool d : interact.checklist.done) done += d;
                        if (audioOn && (interact.usedPart >= 0 || (interact.checklist.any() && interact.checklist.title == clTitle && done > clDone))) audio.play(SFX_CLUNK, 0.7f);
                        clTitle = interact.checklist.title, clDone = done;
                    }
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
                        if (sim.handLine.on()) { sim.dropHandLine(); hud.toast("LET GO OF THE LINE: IT SPOOLS BACK"); }
                        else if (walker.carrying) Interact::drop(sim, walker, lastView.target - lastView.eye);
                        else if (walker.holdingJack) Interact::dropJack(sim, walker);
                        else if (walker.hand.any()) hud.toast("PUT IT BACK IN THE TRUCK: PRESS " + K(KA_INTERACT) + " AT ITS BACK");
                    }
                }
                up = down = left = right = false;
                hb = true;
                walkSend -= dt;
                if (net.active() && walkSend <= 0 && walker.active) {
                    walkSend = 0.1;
                    char b[96];
                    std::snprintf(b, sizeof b, "walk %.2f %.2f %.2f %.3f %.2f %.2f", walker.p.x, walker.p.y, walker.p.z, walker.yaw, walker.stride, walker.crouchAmount());  // (the sixth field, crouch 0..1, is new: older builds read the first five)
                    net.sendGame(b);
                }
            }
            // Your work on foot and the jacks under yours, told to the others; theirs eased and forgotten when unheard (repairsync.h).
            repairOut.update(sim, walker.active ? &walker : nullptr, interact, clock, net.active(), [&](const std::string& t) { net.sendGame(t); });
            repairIn.update(sim, dt, clock);
            sim.lodWatch.clear();  // (where traffic counts someone: on foot, and the camera)
            if (walker.active) sim.lodWatch.push_back(walker.p);
            sim.lodWatch.push_back(lastView.eye);
            if (!sim.paused) sim.drive(up, down, left, right, hb, dt);
            {  // ---- the throttle limit: W gives the cap, built up over the response time (throttle.h; the pad is shaped in padLogic.apply) ----
                Car& kc = sim.cars[sim.player];
                if (!sim.paused) {
                    if (!playback.active() && !kc.ai && !kc.remote && !kc.pilot.active) kc.throttle = throttlePedal.step(kc.throttle > 0.5, settings.throttle, dt);
                    else throttlePedal.reset();
                }
                hud.throttleCap = settings.throttle.cap;
            }
            {  // ---- gamepad: analog steering and pedals on top of the keyboard path ----
                Car& pc = sim.cars[sim.player];
                if (padDrives && !sim.paused && !pc.ai && !pc.pilot.active && !pc.remote) padLogic.apply(pc, dt, sim.steeringAssist);
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
                coop.mapNow = sim.world.map;
                const V3 me = walker.active ? walker.p : sim.cars[sim.player].frame().com;
                for (const std::string& name : coop.discover(sim.world.map, me, discoverySpots())) {
                    jobBanner = std::max(jobBanner, 4.0);
                    jobBannerText = "DISCOVERED: " + name;
                    if (net.active()) {
                        coop.say("FOUND " + name, myName(), net);
                        net.sendGame(foundMessage(sim.world.map, name));  // (the others have found it too)
                    }
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
            renderer.passMark(FP_SIM);
            // Catching up is capped: a hitch (a frame far over the usual) isn't run all at once, which would make the next frame
            // a hitch too, and so on; it's let go, like a pause. Ordinary slow frames run in real time down to 20 fps (at most
            // 50 ms of physics a frame); below that the game slows rather than spiral.
            sim.playerAway = walker.active;
            static double usualDt = 1.0 / 60;
            double owed = simCatchUp(dt, usualDt) - simCredit;
            simCredit = owed < 0 ? -owed : 0;
            const bool bgNow = !scripted && !(SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS);
            const int maxSteps = bgNow ? std::max(simMaxSteps(sim.h), (int)(0.075 / sim.h + 0.5)) : simMaxSteps(sim.h);  // (15 fps in the background: still real time)
            static double msPerStep = -1;  // (what a physics step costs here lately)
            int stepsNow;
            if (net.active() && !sim.paused) {  // (multiplayer: on the shared clock, however slow the frames; frameprof.h simNetSteps)
                const double frameS = scripted ? dt : std::min(1.0, frameTimeMs / 1000.0);
                stepsNow = sim.advance(frameS, std::max(maxSteps, simNetSteps(sim.h, sim.owed() + frameS, msPerStep)), kNetOwedMax);
                simCredit = 0;
            } else {
                stepsNow = sim.advance(std::max(0.0, owed), maxSteps);
            }
            simDt = (simAsyncSteps + stepsNow) * sim.h;
            simAsyncSteps = 0;
            renderer.passMark(FP_GAME);
            const double physicsMs = (double)(SDL_GetPerformanceCounter() - physicsStart) * 1000 / freq;
            if (stepsNow > 0) msPerStep = msPerStep < 0 ? physicsMs / stepsNow : msPerStep + (physicsMs / stepsNow - msPerStep) * 0.1;
            hud.physicsMs += (physicsMs - hud.physicsMs) * 0.05;
            if (scripted && frame >= (opt.bench ? benchWarm + 1 : 40)) benchPhysics += physicsMs, benchPhysicsN++;
            if (sim.wrapShift != wrapSeen) {  // the freeway moved everything back a period: so do the camera and the effects
                const V3 d(sim.wrapShift - wrapSeen, 0, 0);
                cam.target += d;
                fx.translate(d);
                wrapSeen = sim.wrapShift;
            }
            for (const std::string& ev : sim.events) hud.toast(ev);
            sim.events.clear();
            for (const std::string& ev : net.events) hud.toast(ev), std::printf("net: %s\n", ev.c_str());
            net.events.clear();
        }

        // Reset anything that exploded numerically; track damage and crash energy.
        double impact = 0, playerImpact = 0;
        for (int i = 0; i < (int)sim.cars.size(); i++) {
            Car& c = sim.cars[i];
            if (!c.finite()) {
                sim.dropRopes(i);
                c.respawn(c.spawnPos, c.spawnYaw);
            }
            if (!c.remote) c.updateStats();
            impact += c.impactEnergy;
            if (i == sim.player) playerImpact = c.impactEnergy;
            c.impactEnergy = 0;
        }
        for (int i = 0; i < (int)sim.cars.size(); i++) {
            Car& c = sim.cars[i];
            if (!c.remote) c.lampMode = (uint8_t)(i == sim.player ? lampMode : c.ai || c.pilot.active ? LAMPS_AUTO : LAMPS_OFF);
        }
        {  // ---- gamepad rumble: only while the player is on the pad (the last thing they used); pulses, so sent as they change ----
            const bool want = pad::ui.last && padDev.connected() && padCtx == pad::Mode::Drive && !walker.active && !sim.paused && settings.padRumble > 0 &&
                              (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) && sim.player >= 0 && sim.player < (int)sim.cars.size();
            if (want) {
                const pad::RumbleOut ro = rumbler.update(sim.cars[sim.player], settings.padRumble, dt, playerImpact);
                const bool on = ro.low > 0.01f || ro.high > 0.01f;
                padRumbleTimer -= dt;
                if (on != padRumbling || (on && (padRumbleTimer <= 0 || std::fabs(ro.low - padRumbleSent[0]) > 0.04f || std::fabs(ro.high - padRumbleSent[1]) > 0.04f))) {
                    padRumbleTimer = 0.1;  // (a pulse is cut short on the pad itself after 160 ms, so a lost frame can't leave it buzzing)
                    padDev.rumble(ro.low, ro.high);
                    padRumbleSent[0] = ro.low, padRumbleSent[1] = ro.high;
                    padRumbling = on;
                }
            } else {
                if (padRumbling) padDev.rumble(0, 0);
                padRumbling = false;
                rumbler.reset();
            }
        }
        {  // the starter and the dead battery's click (battery.h)
            static battery::Sounds batSounds;
            if (audioOn) batSounds.update(sim, simDt, [&](Sfx what, const V3&, float gain, float delay, float pitch) { audio.play(what, gain, 0, delay, pitch); });
            if (!walker.active) sim.handCable = Sim::HandCable();
        }
        for (int i = 0; i < (int)sim.cars.size(); i++)  // (who's in the driver's seat: a buddy getting in either takes it over or rides along)
            if (!sim.cars[i].remote) sim.cars[i].occupied = i == sim.player && !walker.active;
        if (!(playback.active() && !playback.done) && !net.joining()) net.send(sim, (double)(SDL_GetPerformanceCounter()) / freq);
        dbgSimSeconds += simDt, dbgSubsteps = (int)std::lround(simDt / sim.h);
        fx.setLimits(markLimits(presetOf(settings.gfx)));
        FxEvents events = fx.update(sim, campBehind() ? dt : simDt);
        if (campBehind()) {  // (the camp's fire: the sim is paused behind the menu, so the effects run on the real frame time)
            const V3 f = campFire;
            fx.campfire(V3(f.x, sim.world.height(f.x, f.z), f.z), dt, V3(0.32, 0, -0.12));
        }

        Car& car = sim.cars[sim.player];
        Frame fr = car.frame();
        if (audioOn) {
            float slip = 0, load = (float)(sim.paused ? 0 : car.throttle * car.tcCut);
            for (const Wheel& wh : car.wheels)
                if (wh.load > 500) slip = std::max(slip, wh.slip);
            audio.setEngine((float)(car.engineDead || car.engineOff || campWorld ? 0 : car.rpm) * (float)std::max(0.35, sim.timeScale), load);
            const VehicleSpec& vs = *car.spec;
            audio.setVoice(vs.cylinders, (float)vs.displacement, (int)vs.induction, vs.flatPlane, (float)vs.redline, voiceFor(vs));
            audio.setRecording(engineSamples::forVehicle(vs));
            audio.setBoost(vs.induction == VehicleSpec::TURBO ? (float)car.boost : 0.0f);
            audio.setTires(sim.paused ? 0 : slip);
            audio.setVolume(settings.volume, campBehind() ? 0.85f : menu.open ? 0.35f : 1.0f);
            audio.setVehicleVolume(settings.vehicleVolume);
            audio.setCampfire(campBehind() ? 0.85f : 0.0f, 0.25f, 0.1f);  // (the fire sits right of the middle of the picture, the stream runs across the foreground)
            {  // the music follows the country under you (steadied, so a border doesn't flip it back and forth)
                static BiomeTracker biomes;
                const V3 at = walker.active ? walker.p : fr.com;
                biomes.update(sim.world, at.x, at.z, dt);
                audio.setMusic(soundMap(sim.world.map), settings.music, biomes.current);  // master volume applies to the whole mix
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
            const bool recordedAir = audio.setAiring(airing ? pcar.airing : 0);  // (the compressor and the valve's hiss; the synthesized hiss only when they're missing)
            audio.setHiss(airing && !recordedAir ? 0.6f : 0.0f);
        }

        int W, H;
        SDL_GL_GetDrawableSize(win, &W, &H);
#ifdef __APPLE__
        {  // Retina: the scene renders at logical pixels (the OpenGL layer is a slow path at 2x)
            int lw = 0, lh = 0;
            SDL_GetWindowSize(win, &lw, &lh);
            renderer.dpiFactor = W > 0 && lw > 0 && lw < W ? (float)lw / (float)W : 1.0f;
        }
#endif
        if (scripted && opt.script == "job" && frame == 10) {  // (screenshots: a recovery job under way)
            refreshJobs();
            if (!jobList.empty()) startJob(jobList[0]);
        }
        if (scripted && (opt.script == "walk" || opt.script == "stand" || opt.script == "line")) {  // (screenshots: out of the truck and walking off, or standing by it;
            if (frame == 20) getOut();                                                                // line: walking the winch line out)
            if (frame == 25 && opt.script == "line") std::printf("%s\n", sim.takeLine(sim.player, ROPE_WINCH, 0, walker.p + V3(0, 1, 0)).c_str());
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
        if (editor.active && scripted && opt.script == "editbrush") {
            // (Screenshots: the brush cursor at one cell and at 30 m, a patterned one, what the pattern brushes leave, and draining water.)
            EditCore& ed = editor.core;
            const V3 c(-200, 0, 200);
            auto cam = [&](double back, double up, double ahead) { editor.setCamera(c + V3(-back, 0, 0) + V3(0, sim.world.height(c.x, c.z) + up, 0), 0, -std::atan2(up, ahead)); };
            auto find = [&](int surface) {
                for (int k = 0; k < (int)paintMaterials().size(); k++)
                    if (paintMaterials()[k].surface == surface) return k;
                return 0;
            };
            if (frame == 2) {
                editor.setTool(Editor::SCULPT);
                cam(10, 9, 12);
                editor.setMouse(W * 0.5f, H * 0.55f);
                editor.setBrush(SHAPE_ROUND, FALL_SMOOTH, STYLE_SOLID, 0.5);
            }
            if (frame == 8) editor.setBrush(SHAPE_ROUND, FALL_SMOOTH, STYLE_SOLID, 3);
            if (frame == 14) cam(25, 28, 30), editor.setBrush(SHAPE_ROUND, FALL_SMOOTH, STYLE_SOLID, 24);
            if (frame == 20) editor.setBrush(SHAPE_SQUARE, FALL_HARD, STYLE_SPLATTER, 24, 0.55);
            if (frame == 26) {  // (what the pattern brushes leave)
                editor.setTool(Editor::PAINT);
                cam(30, 32, 34);
                const double ox[6] = {-30, 0, 30, -30, 0, 30}, oz[6] = {-18, -18, -18, 18, 18, 18};
                const int mats[6] = {find(SURF_MUD), find(SURF_SAND), find(SURF_GRAVEL), find(SURF_SNOW), find(SURF_ROCK), find(SURF_MUD)};
                const BrushStyle st[6] = {STYLE_NOISE, STYLE_SPLATTER, STYLE_STRIPES, STYLE_SPECKLE, STYLE_SOLID, STYLE_SPLATTER};
                const BrushShape sh[6] = {SHAPE_ROUND, SHAPE_ROUND, SHAPE_SQUARE, SHAPE_ROUND, SHAPE_DIAMOND, SHAPE_SQUARE};
                for (int k = 0; k < 6; k++) {
                    Brush b;
                    b.shape = sh[k], b.style = st[k], b.radius = 13, b.density = k == 5 ? 0.35 : 0.55, b.fall = k == 4 ? FALL_HARD : FALL_SMOOTH, b.seed = 5 + k * 3;
                    b.angle = 0.6;
                    ed.paint(b, c.x + 30 + ox[k], c.z + oz[k], mats[k]);
                    if (k == 5) {  // (a second, lighter coat over the splatter: opacity)
                        b.style = STYLE_SOLID, b.strength = 0.3, b.radius = 8, b.seed = 99, b.fall = FALL_HARD;
                        ed.paint(b, c.x + 30 + ox[k], c.z + oz[k], find(SURF_ICE));
                    }
                }
                editor.setMaterial(find(SURF_GRAVEL));
                editor.setBrush(SHAPE_ROUND, FALL_SMOOTH, STYLE_NOISE, 10, 0.5, 0.8);
                editor.setMouse(W * 0.5f, H * 0.2f);
            }
            if (frame == 32) {  // (a pool, half of it drained, and the drain brush up)
                editor.setTool(Editor::WATER);
                cam(30, 32, 34);
                ed.brush(BRUSH_LOWER, c.x + 25, c.z, 14, 2.5);
                ed.flood(c.x + 25, c.z, 1.2, 40);
                Brush b;
                b.radius = 7, b.fall = FALL_HARD, b.style = STYLE_SPLATTER, b.density = 0.6;
                ed.dryBrush(b, c.x + 25, c.z + 2);
                editor.setWaterMode(1);
                editor.setBrush(SHAPE_ROUND, FALL_HARD, STYLE_SPLATTER, 9, 0.6);
                editor.setMouse(W * 0.5f, H * 0.5f);
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
        if (scripted && opt.script == "edobj") {
            // (Screenshots of the object tools: CZ_EDOBJ=palette | ghost | xform | group | playtest | back; CZ_PAL names the palette entry.)
            const std::string mode = std::getenv("CZ_EDOBJ") ? std::getenv("CZ_EDOBJ") : "palette";
            EditCore& ed = editor.core;
            auto look = [&](double dist, double h) {  // the camera over the middle of the scene
                const V3 c = editor.core.level.hasStart ? editor.core.level.start : sim.world.spawn;
                editor.setCamera(V3(c.x, c.y, c.z) + V3(-dist, h, 0), 0, -0.45);
            };
            if (editor.active && frame == 3) {
                editor.setTool(Editor::OBJECTS);
                if (std::getenv("CZ_PAL")) editor.setPalette(std::max(0, editor.findPalette(std::getenv("CZ_PAL"))));
                look(std::getenv("CZ_DIST") ? std::atof(std::getenv("CZ_DIST")) : 26, std::getenv("CZ_HIGH") ? std::atof(std::getenv("CZ_HIGH")) : 14);
                editor.setMouse(W * 0.5f, H * 0.58f);
                if (mode == "paint") editor.setTool(Editor::PAINT);
            }
            if (editor.active && frame == 5 && (mode == "xform" || mode == "group" || mode == "playtest" || mode == "back")) {
                const V3 c = (ed.level.hasStart ? ed.level.start : sim.world.spawn);
                auto put = [&](int pal, double dx, double dz, std::vector<int>& ks) {
                    editor.setPalette(pal);
                    ks.push_back(ed.addObject(editor.makeForScript(V3(c.x + dx, 0, c.z + dz))));
                };
                std::vector<int> ks;
                put(editor.findPalette(std::getenv("CZ_PAL") ? std::getenv("CZ_PAL") : "MOSS 1 A"), 4, -4, ks);
                put(editor.findPalette("PINE"), 6, 8, ks);
                put(editor.findPalette("LOG"), 2, 3, ks);
                put(editor.findPalette("CABIN"), 14, 6, ks);
                put(editor.findPalette("RAMP"), 8, -10, ks);
                editor.setPointer(true);
                editor.selectObjects({ks[0]});
                if (mode == "xform") {
                    editor.applyKey(SC_R, false, false);  // tip
                    editor.applyKey(SC_R, false, false);
                    editor.applyKey(SC_C, false, false);  // roll
                    editor.applyKey(SC_E, true, false);   // turn
                    editor.applyKey(SC_T, false, false);  // longer
                    editor.applyKey(SC_T, false, false);
                    editor.applyKey(SC_Y, false, false);  // taller
                    editor.applyKey(SC_EQUALS, false, false);
                    editor.applyKey(SC_X, false, false);  // raise
                }
                if (mode == "group") editor.selectObjects(ks);
                if (mode == "playtest" || mode == "back") {
                    ed.setStart(c.x - 20, c.z, 0);
                    editor.setCamera(V3(c.x + 30, sim.world.height(c.x + 30, c.z + 30) + 6, c.z + 30), 2.3, -0.2);
                }
            }
            if (editor.active && frame == 5 && (mode == "hover" || mode == "delete")) {
                const V3 c = (ed.level.hasStart ? ed.level.start : sim.world.spawn);
                std::vector<int> ks;
                for (const char* nm : {"MOSS 1 A", "PINE", "LOG", "CABIN", "RAMP"}) {
                    editor.setPalette(editor.findPalette(nm));
                    const double dx = std::string(nm) == "CABIN" ? 14 : std::string(nm) == "RAMP" ? 8 : std::string(nm) == "PINE" ? 6 : std::string(nm) == "LOG" ? 2 : 4;
                    const double dz = std::string(nm) == "CABIN" ? 6 : std::string(nm) == "RAMP" ? -10 : std::string(nm) == "PINE" ? 8 : std::string(nm) == "LOG" ? 3 : -4;
                    ks.push_back(ed.addObject(editor.makeForScript(V3(c.x + dx, 0, c.z + dz))));
                }
                editor.setPointer(true);
                if (mode == "hover") editor.setMouse(W * 0.595f, H * 0.30f);
                else editor.selectObjects({ks[4], ks[3]});
            }
            if (editor.active && frame == 8 && mode == "delete") editor.applyKey(SC_DELETE);
            if (editor.active && frame == 5 && mode == "stack") {
                const V3 c = (ed.level.hasStart ? ed.level.start : sim.world.spawn);
                editor.setPalette(editor.findPalette("BOULDER"));
                for (int k = 0; k < 6; k++) editor.applyKey(SC_EQUALS);  // (a big one to stack on)
                const LevelObject big = editor.makeForScript(V3(c.x + 7, 0, c.z - 3));
                ed.addObject(big);
                editor.setPalette(editor.findPalette(std::getenv("CZ_PAL") ? std::getenv("CZ_PAL") : "ROCK"));
            }
            if (editor.active && frame == 9 && mode == "stack") {
                const V3 c = (ed.level.hasStart ? ed.level.start : sim.world.spawn);
                const Rock& r = sim.world.rocks.back();
                float sx, sy;
                if (editor.project(r.c + V3(0, r.r * 0.55, 0), sx, sy, W, H)) editor.setMouse(sx, sy);
                (void)c;
            }
            if (editor.active && frame == 5 && (mode == "save" || mode == "ask")) {
                ed.brush(BRUSH_RAISE, -10, 10, 6, 1.0);
                if (mode == "save") editor.applyKey(SC_S, false, true);
                else editor.applyKey(SC_ESCAPE);
            }
            if (editor.active && frame == 12 && (mode == "playtest" || mode == "back")) editor.requestPlay(true);
            if (!editor.active && editorSuspended && frame == 50 && mode == "back") returnToEditor();
        }
        if (scripted && opt.script == "edqa") {
            // (QA driver: CZ_EDQA names a file of commands, one per frame, run with --script edqa --shot-frames 99999 (`shot` takes one, `quit` ends). For checking the
            // editor the way a user does it, offscreen: menu_edit NAME / menu_real_edit N / menu_new / menu_section / menu_back / f2 / play_level NAME, tool N, ptr, pal NAME,
            // cam / camat x z dist high yawdeg / camobj k, mouse fx fy / mousepx dx dy / mouseobj k / mousebase kind idx / mousepoint x y z, down, up, wheel, key NAME [shift] [ctrl],
            // hold [shift] [ctrl] [space|alt] (right button: down 3 / up 3), mousehandle x|y|z [ring] (the gizmo), drag (what it's doing), gates, keys letters, save / saveas / undo / redo / play [start], and the dumps: state, world, objs [a [b]], rocksnear x z r, pals, models, levels, carpos, autosaves.)
            static std::vector<std::string> cmds;
            static size_t ci = 0;
            static int waitN = 0;
            static float qx = 0, qy = 0;
            static bool loaded = false;
            if (!loaded) {
                loaded = true;
                std::ifstream f(std::getenv("CZ_EDQA") ? std::getenv("CZ_EDQA") : "/dev/null");
                for (std::string ln; std::getline(f, ln);)
                    if (!ln.empty() && ln[0] != '#') cmds.push_back(ln);
            }
            auto keyCode = [](const std::string& n) -> int {
                if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') return 4 + (n[0] - 'a');
                if (n.size() == 1 && n[0] >= '1' && n[0] <= '9') return 30 + (n[0] - '1');
                if (n == "0") return 39;
                static const std::map<std::string, int> m = {{"return", 40}, {"esc", 41}, {"backspace", 42}, {"tab", 43}, {"space", 44}, {"minus", 45}, {"equals", 46}, {"lbracket", 47}, {"rbracket", 48},
                    {"comma", 54}, {"period", 55}, {"f1", 58}, {"f2", 59}, {"f3", 60}, {"f5", 62}, {"f6", 63}, {"pgup", 75}, {"delete", 76}, {"pgdn", 78}, {"right", 79}, {"left", 80}, {"down", 81}, {"up", 82}};
                const auto it = m.find(n);
                return it == m.end() ? std::atoi(n.c_str()) : it->second;
            };
            auto mouseEv = [&](uint32_t type, int button, float x, float y) {
                SDL_Event e;
                std::memset(&e, 0, sizeof(e));
                e.type = type;
                int32_t xi = (int32_t)x, yi = (int32_t)y;
                if (type == SDL_MOUSEMOTION) std::memcpy(e.padding + 20, &xi, 4), std::memcpy(e.padding + 24, &yi, 4);
                else e.padding[16] = (uint8_t)button, std::memcpy(e.padding + 20, &xi, 4), std::memcpy(e.padding + 24, &yi, 4);
                editor.handleEvent(e, 1.0f);
            };
            auto objPoint = [&](int k, V3& at) {  // the middle of what level object k made in the world
                const auto parts = editor.core.partsOf(k);
                if (parts.empty()) return false;
                const World& w = sim.world;
                const auto pr = parts[0];
                if (pr.first == 0) { double hi = -1e9; for (const V3& v : w.rocks[pr.second].v) hi = std::max(hi, v.y); at = w.rocks[pr.second].c; at.y = at.y * 0.3 + hi * 0.7; }
                else if (pr.first == 1) at = w.trees[pr.second].base + V3(0, w.trees[pr.second].height * 0.4, 0);
                else if (pr.first == 2) at = (w.logs[pr.second].a + w.logs[pr.second].b) * 0.5;
                else if (pr.first == 3) at = w.boxes[pr.second].c;
                else if (pr.first == 4) at = w.fixtures[pr.second].a + V3(0, 1, 0);
                else if (pr.first == 5) at = w.models[pr.second].p + V3(0, 1, 0);
                else at = w.bushes[pr.second].p + V3(0, 0.5, 0);
                return true;
            };
            if (waitN > 0) waitN--;
            else if (ci < cmds.size()) {
                std::istringstream is(cmds[ci++]);
                std::string c;
                is >> c;
                std::vector<std::string> a;
                for (std::string t; is >> t;) a.push_back(t);
                auto num = [&](size_t i, double d = 0) { return i < a.size() ? std::atof(a[i].c_str()) : d; };
                auto rest = [&](size_t from) { std::string r; for (size_t i = from; i < a.size(); i++) r += (i > from ? " " : "") + a[i]; return r; };
                EditCore& ed = editor.core;
                std::printf("[edqa %d] %s   (last frame %.0f ms)\n", frame, cmds[ci - 1].c_str(), frameTimeMs);
                if (c == "wait") waitN = (int)num(0);
                else if (c == "quit") running = false;
                else if (c == "shot") opt.shotFrames.insert(frame + 1);
                else if (c == "state") std::printf("  %s\n", editor.active ? editor.debugState().c_str() : "(editor not active)");
                else if (c == "world") std::printf("  world: rocks %zu trees %zu logs %zu boxes %zu fixtures %zu models %zu bushes %zu map %d paused %d\n", sim.world.rocks.size(), sim.world.trees.size(), sim.world.logs.size(), sim.world.boxes.size(), sim.world.fixtures.size(), sim.world.models.size(), sim.world.bushes.size(), sim.world.map, (int)sim.paused);
                else if (c == "levels") {
                    refreshLevels();
                    for (size_t k = 0; k < levelList.size(); k++) std::printf("  level %zu: %s%s map %d objs %zu\n", k, levelList[k].name.c_str(), levelList[k].builtin ? " (builtin)" : "", levelList[k].map, levelList[k].objects.size());
                } else if (c == "menu_edit") {  // the LEVELS list -> EDIT: by name substring, as the menu's result
                    refreshLevels();
                    std::string want = rest(0);
                    for (char& ch : want) ch = (char)std::toupper((unsigned char)ch);
                    int found = -1;
                    for (int k = 0; k < (int)levelList.size() && found < 0; k++)
                        if (levelList[k].name.find(want) != std::string::npos) found = k;
                    std::printf("  menu edit %d\n", found);
                    if (found >= 0) {
                        PauseMenu::Result r;
                        r.action = PauseMenu::EDIT_LEVEL, r.vehicle = found;
                        handleMenu(r);
                    }
                } else if (c == "menu_new") { PauseMenu::Result r; r.action = PauseMenu::EDITOR; handleMenu(r); }
                else if (c == "menu_section") { PauseMenu::Result r; r.action = PauseMenu::NEW_SECTION; handleMenu(r); }
                else if (c == "menu_back") { PauseMenu::Result r; r.action = PauseMenu::EDITOR_BACK; handleMenu(r); }
                else if (c == "f1") returnToEditor();
                else if (c == "f2") { if (!returnToEditor()) openEditor(editorKeyLevel(), false, true); }
                else if (c == "play_level") { refreshLevels(); std::string want = rest(0); for (char& ch : want) ch = (char)std::toupper((unsigned char)ch); for (int k = 0; k < (int)levelList.size(); k++) if (levelList[k].name.find(want) != std::string::npos) { playLevel(levelList[k]); break; } }
                else if (c == "tool") editor.setTool((Editor::Tool)std::max(0, (int)num(0) - 1));
                else if (c == "ptr") editor.setPointer(num(0, 1) != 0);
                else if (c == "gates") { std::printf("  start %d (%.1f %.1f) yaw %.2f\n", (int)ed.level.hasStart, ed.level.start.x, ed.level.start.z, ed.level.startYaw); for (size_t k = 0; k < ed.level.gates.size(); k++) std::printf("  gate %zu (%.1f %.1f) yaw %.2f width %.1f\n", k + 1, ed.level.gates[k].p.x, ed.level.gates[k].p.z, ed.level.gates[k].yaw, ed.level.gates[k].width); }
                else if (c == "hold") { bool sh = false, ct = false, al = false; for (const std::string& t : a) sh |= t == "shift", ct |= t == "ctrl", al |= t == "alt" || t == "space"; editor.holdMods(sh, ct, al); }  // keep Shift / Ctrl / the Y key down for what follows (`hold` alone lets go)
                else if (c == "pal") editor.setPalette(std::max(0, editor.findPalette(rest(0))));
                else if (c == "cam") editor.setCamera(V3(num(0), num(1), num(2)), num(3) * M_PI / 180, num(4) * M_PI / 180);
                else if (c == "camat") {  // camera `dist` back from a ground point, `high` up, looking at it: camat x z dist high yawdeg
                    const double yw = num(4) * M_PI / 180, dist = num(2), hi = num(3);
                    const V3 g(num(0), sim.world.height(num(0), num(1)), num(1));
                    editor.setCamera(g + V3(-std::cos(yw) * dist, hi, std::sin(yw) * dist), yw, -std::atan2(hi, dist));
                } else if (c == "camobj") {  // camera looking at level object k from `dist` behind and `high` up (facing yaw deg)
                    V3 at;
                    if (objPoint((int)num(0), at)) {
                        const double yw = num(3) * M_PI / 180, dist = num(1), hi = num(2);
                        editor.setCamera(at + V3(-std::cos(yw) * dist, hi, std::sin(yw) * dist), yw, -std::atan2(hi, dist));
                    } else std::printf("  no parts for object %d\n", (int)num(0));
                } else if (c == "mouse") { qx = (float)(num(0) * W), qy = (float)(num(1) * H); mouseEv(SDL_MOUSEMOTION, 0, qx, qy); }
                else if (c == "mousepx") { qx += (float)num(0), qy += (float)num(1); mouseEv(SDL_MOUSEMOTION, 0, qx, qy); }
                else if (c == "down") mouseEv(SDL_MOUSEBUTTONDOWN, a.empty() ? 1 : (int)num(0), qx, qy);
                else if (c == "up") mouseEv(SDL_MOUSEBUTTONUP, a.empty() ? 1 : (int)num(0), qx, qy);
                else if (c == "mouseobj" || c == "mousebase" || c == "mousepoint") {  // aim the mouse: at level object k, at a world part (kind idx), or a world point
                    V3 at;
                    bool ok = false;
                    if (c == "mouseobj") ok = objPoint((int)num(0), at);
                    else if (c == "mousepoint") at = V3(num(0), num(1), num(2)), ok = true;
                    else {
                        const World& w = sim.world;
                        const int k = (int)num(0), i = (int)num(1);
                        ok = true;
                        if (k == 0 && i < (int)w.rocks.size()) { double hi = -1e9; for (const V3& v : w.rocks[i].v) hi = std::max(hi, v.y); at = w.rocks[i].c; at.y = at.y * 0.3 + hi * 0.7; }
                        else if (k == 1 && i < (int)w.trees.size()) at = w.trees[i].base + V3(0, w.trees[i].height * 0.4, 0);
                        else if (k == 2 && i < (int)w.logs.size()) at = (w.logs[i].a + w.logs[i].b) * 0.5;
                        else if (k == 3 && i < (int)w.boxes.size()) at = w.boxes[i].c;
                        else if (k == 4 && i < (int)w.fixtures.size()) at = w.fixtures[i].a + V3(0, 1, 0);
                        else ok = false;
                    }
                    float sx, sy;
                    if (ok && editor.project(at, sx, sy)) qx = sx, qy = sy, mouseEv(SDL_MOUSEMOTION, 0, qx, qy), std::printf("  mouse at %.0f,%.0f for (%.1f %.1f %.1f)\n", sx, sy, at.x, at.y, at.z);
                    else std::printf("  can't aim (off screen or no such part)\n");
                } else if (c == "mousehandle") {  // aim the mouse at a gizmo handle: mousehandle x|y|z [ring]
                    const int h = (a.empty() ? 1 : a[0] == "y" ? 2 : a[0] == "z" ? 3 : 1) + (a.size() > 1 && a[1] == "ring" ? 3 : 0);
                    float sx, sy;
                    if (editor.handlePoint(h, sx, sy)) qx = sx, qy = sy, mouseEv(SDL_MOUSEMOTION, 0, qx, qy), std::printf("  mouse at %.0f,%.0f on handle %d\n", sx, sy, h);
                    else std::printf("  no such handle showing\n");
                } else if (c == "drag") {
                    float gx = 0, gy = 0;
                    const bool g = editor.grabPoint(gx, gy);
                    std::printf("  drag: %s  mouse %.0f,%.0f%s\n", editor.dragStatus().c_str(), qx, qy, g ? (" grabbed point " + std::to_string((int)std::lround(gx)) + "," + std::to_string((int)std::lround(gy))).c_str() : "");
                } else if (c == "key") {
                    bool sh = false, ct = false;
                    for (size_t i = 1; i < a.size(); i++) sh |= a[i] == "shift", ct |= a[i] == "ctrl";
                    editor.applyKey(keyCode(a.empty() ? "" : a[0]), sh, ct);
                } else if (c == "keys") {  // a string of letters typed one per call (names, prompts)
                    for (char ch : rest(0)) editor.applyKey(ch == '_' ? 44 : ch >= 'a' && ch <= 'z' ? 4 + (ch - 'a') : ch >= '1' && ch <= '9' ? 30 + (ch - '1') : ch == '0' ? 39 : 0);
                } else if (c == "wheel") { SDL_Event e; std::memset(&e, 0, sizeof(e)); e.type = SDL_MOUSEWHEEL; int32_t dy = (int32_t)num(0); std::memcpy(e.padding + 20, &dy, 4); editor.handleEvent(e, 1.0f); }
                else if (c == "objs") {
                    for (size_t k = a.empty() ? 0 : (size_t)num(0); k < (a.empty() ? ed.level.objects.size() : std::min(ed.level.objects.size(), (size_t)num(1, num(0)) + 1)); k++) {
                        const LevelObject& o = ed.level.objects[k];
                        std::printf("  obj %zu kind %d p(%.2f %.2f %.2f) p2(%.2f %.2f %.2f) size(%.2f %.2f %.2f) yaw %.2f pitch %.2f roll %.2f top %.2f lift %.2f sub %d\n", k, o.kind, o.p.x, o.p.y, o.p.z, o.p2.x, o.p2.y, o.p2.z, o.size.x, o.size.y, o.size.z, o.yaw, o.pitch, o.roll, o.top, o.lift, o.sub);
                        V3 at;
                        const auto parts = ed.partsOf((int)k);
                        for (const auto& pr : parts) {
                            if (pr.first == 0) { const Rock& r = sim.world.rocks[pr.second]; double lo = 1e9, hi = -1e9; for (const V3& v : r.v) lo = std::min(lo, v.y), hi = std::max(hi, v.y); std::printf("    world rock %d c(%.2f %.2f %.2f) r %.2f y %.2f..%.2f ground %.2f\n", pr.second, r.c.x, r.c.y, r.c.z, r.r, lo, hi, sim.world.height(r.c.x, r.c.z)); }
                            else if (pr.first == 3) { const Box& b = sim.world.boxes[pr.second]; std::printf("    world box %d c(%.2f %.2f %.2f) half(%.2f %.2f %.2f)\n", pr.second, b.c.x, b.c.y, b.c.z, b.h.x, b.h.y, b.h.z); }
                            else if (pr.first == 2) { const Log& l = sim.world.logs[pr.second]; std::printf("    world log %d a(%.2f %.2f %.2f) b(%.2f %.2f %.2f) r %.2f\n", pr.second, l.a.x, l.a.y, l.a.z, l.b.x, l.b.y, l.b.z, l.r); }
                            else if (pr.first == 1) { const Tree& t = sim.world.trees[pr.second]; std::printf("    world tree %d base(%.2f %.2f %.2f) h %.2f ground %.2f\n", pr.second, t.base.x, t.base.y, t.base.z, t.height, sim.world.height(t.base.x, t.base.z)); }
                            else std::printf("    world part %d:%d\n", pr.first, pr.second);
                        }
                        (void)at;
                    }
                } else if (c == "menu_real_edit") {  // the real menu: LEVELS page, pick a level by index, DOWN to OPEN IN THE EDITOR, RETURN
                    refreshLevels();
                    openMenu();
                    menu.showPage(4, 0, sim.world.map);
                    menu.levelPick = (int)num(0);
                    for (int i = 0; i < 3; i++) handleMenu(menu.key(81, settings));
                    handleMenu(menu.key(40, settings));
                    std::printf("  menu open %d editor active %d\n", (int)menu.open, (int)editor.active);
                } else if (c == "models") {  // the world's placed models by id, how many of each
                    std::map<std::string, int> ids;
                    for (const Placed& pl : sim.world.models) ids[pl.id]++;
                    for (const auto& kv : ids) std::printf("  model %s x%d\n", kv.first.c_str(), kv.second);
                    std::map<int, int> fk;
                    for (const Prop& f : sim.world.fixtures) fk[(int)f.kind]++;
                    for (const auto& kv : fk) std::printf("  fixture kind %d x%d\n", kv.first, kv.second);
                } else if (c == "autosavedir") {
                    editor.autosaveDir = userLevelFolder() + "/autosave";
                } else if (c == "autosavenow") {
                    editor.autosaveNow();
                } else if (c == "pals") {
                    for (int k = 0; k < editor.paletteSize(); k++) std::printf("  pal %d %s\n", k, editor.paletteName(k));
                } else if (c == "rocksnear") {  // base rocks near a ground point: rocksnear x z r
                    int n = 0;
                    for (size_t i = 0; i < sim.world.rocks.size() && n < 12; i++) {
                        const Rock& r = sim.world.rocks[i];
                        if (std::hypot(r.c.x - num(0), r.c.z - num(1)) < num(2)) std::printf("  rock %zu c(%.1f %.1f %.1f) r %.2f\n", i, r.c.x, r.c.y, r.c.z, r.r), n++;
                    }
                } else if (c == "save") { editor.applyKey(keyCode("s"), false, true); }
                else if (c == "selveh") {  // (the camp's K-th parked vehicle stand-in, selected: selveh K)
                    int n = 0;
                    for (int i = 0; i < (int)editor.core.level.objects.size(); i++)
                        if (editor.core.level.objects[i].name == menucamp::kVehicleModel && n++ == (int)num(0)) editor.selectObjects({i});
                }
                else if (c == "menupreview") editor.wantPreview = true;  // (the MENU tool's PREVIEW button)
                else if (c == "menuexit") editor.wantClose = true;
                else if (c == "saveas") { editor.applyKey(keyCode("s"), true, true); }
                else if (c == "undo") { editor.applyKey(keyCode("z"), false, true); }
                else if (c == "redo") { editor.applyKey(keyCode("y"), false, true); }
                else if (c == "play") editor.requestPlay(a.empty() || a[0] != "start");
                else if (c == "place") {  // place the palette entry at a ground point (as a click would); place x z
                    ed.addObject(editor.makeForScript(V3(num(0), 0, num(1))));
                } else if (c == "carpos") {
                    const Car& pc = sim.cars[sim.player];
                    std::printf("  car at %.1f %.1f %.1f editorActive %d suspended %d\n", pc.frame().com.x, pc.frame().com.y, pc.frame().com.z, (int)editor.active, (int)editorSuspended);
                } else if (c == "autosaves") {
                    int n = 0;
                    std::error_code ec;
                    for (const auto& de : std::filesystem::directory_iterator(userLevelFolder() + "/autosave", ec)) std::printf("  autosave %s %lld bytes\n", de.path().c_str(), (long long)de.file_size()), n++;
                    std::printf("  %d autosave files\n", n);
                } else std::printf("  ?? unknown command\n");
            }
        }
        if (editor.active) {
            editor.update(dt, keys, renderer, W, H);
            if (editor.takeSaved()) refreshLevels();  // (a level just saved is on the menu's list at once)
            drawnWorld = sim.world.stamp;  // (it redraws what it changes itself)
            sim.paused = true;
            if (editor.wantResetCamp) {  // (MENU tool: the game's own scene back; the saved file is forgotten, the editor reopens on it)
                editor.wantResetCamp = false;
                menuscene::resetUser();
                Level l = menuscene::load();
                l.builtin = false, l.file = menuscene::userPath();
                editor.close();
                openEditor(l);
                hud.toast("THE GAME'S MENU SCENE IS BACK. EDIT IT, OR EXIT TO KEEP IT");
            } else if (editor.wantPreview) {  // (MENU tool: the menu as it will look, from the camp as edited so far; Esc comes back here)
                editor.wantPreview = false;
                editor.autosaveNow();
                lastEdited = editor.core.level;
                haveEdited = true;
                campLevel = editor.core.level;
                editor.close();
                editorSuspended = true;
                previewMenu = true;
                enterCamp();
                openMenu();
                menu.showTitle(std::max(0, sim.cars[sim.player].spec->model), settings.map);
                mainNav.open(menu.mapPick, std::max(0, sim.cars[sim.player].spec->model));
                classicTitle = false;
            } else if (editor.wantClose && menucamp::isCamp(editor.core.level)) {  // (leaving the camp: back to the menu, on the camp as edited or as saved)
                const bool discard = editor.discarded;
                if (!discard) editor.autosaveNow();
                campLevel = discard ? editor.reverted : editor.core.level;
                editor.close();
                editorSuspended = false;
                enterCamp();
                openMenu();
                menu.showTitle(std::max(0, sim.cars[sim.player].spec->model), settings.map);
                mainNav.open(menu.mapPick, std::max(0, sim.cars[sim.player].spec->model));
                classicTitle = false;
            } else if (editor.wantPlay || editor.wantClose) {
                const bool play = editor.wantPlay, here = editor.playHere;
                EditCore::PlaySpot spot = editor.spot;
                if (!play) spot = editor.core.playSpot(true, editor.viewPoint(), editor.heading());  // (leaving: you're where you were looking, unless there's a start)
                const bool discard = !play && editor.discarded;  // (DISCARD: the saved level, not the edits)
                if (!discard) editor.autosaveNow();
                lastEdited = discard ? editor.reverted : editor.core.level;
                haveEdited = true;
                editor.close();  // (the editor keeps its state: F1 comes back to it)
                sim.paused = false;
                if (discard || !play) {
                    // Back to driving: on the world as edited, or on the level as last saved when the edits were discarded.
                    Level free = lastEdited;
                    free.mode = LEVEL_FREE;
                    playLevel(free, free.hasStart ? nullptr : &spot);
                } else {
                    playLevel(editor.core.levelToPlay(here), here ? &spot : nullptr);
                }
                editorSuspended = !discard;  // (discarded: nothing to come back to, F2 opens the saved level fresh)
                hud.toast(discard ? "EDITS DISCARDED: DRIVING " + lastEdited.name : play ? (here ? "TESTING FROM HERE" : "TESTING " + lastEdited.name) : "DRIVING THE EDITED WORLD");
            }
        }
        if (walker.active) {  // the camera follows you on foot
            fr = Frame();
            fr.com = walker.p + V3(0, 1.1 - 0.4 * walker.crouchAmount(), 0);
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
            // Invert axes per settings (default: stick up = look up, stick right = look right).
            // The gamepad's raw values are inverted (stick down = positive), so flip Y by default.
            float lx = pin.lookX, ly = -pin.lookY;
            if (settings.padInvertLookX) lx = -lx;
            if (settings.padInvertLookY) ly = -ly;
            if (padLooking || pin.resetView) cam.padLook(lx, ly, pin.lookBack, pin.resetView, dt);
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
            float lx = padLogic.in.lookX, ly = -padLogic.in.lookY;
            if (settings.padInvertLookX) lx = -lx;
            if (settings.padInvertLookY) ly = -ly;
            footYaw -= lx * 2.6 * dt;
            footPitch = clampd(footPitch + ly * 1.8 * dt, -1.45, 1.45);
        }
        if (opt.bench) {  // (the lap: 30 m round the start, 2.5 m up, once over the warm-up and the run)
            auto ground = [](const void* w, double x, double z) { return ((const World*)w)->height(x, z); };
            const BenchPose bp = benchPose(benchCentre.x, benchCentre.z, (double)bf / (benchWarm + opt.bench), 30, 2.5, ground, &sim.world);
            cam.mode = 3, cam.droneEye = V3(bp.x, bp.y, bp.z), cam.droneYaw = bp.yaw, cam.dronePitch = bp.pitch;
        }
        if (scripted) {  // (screenshots of the first-person body: CZ_FOOT_PITCH=rad looks up/down on foot, CZ_CAB_LOOK=yaw,pitch round the cabin)
            if (const char* fp = std::getenv("CZ_FOOT_PITCH"); fp && walker.active) footPitch = std::atof(fp);
            if (const char* fy = std::getenv("CZ_FOOT_YAW"); fy && walker.active) footYaw = walker.yaw + std::atof(fy);  // (relative to the way you got out)
            if (const char* cl = std::getenv("CZ_CAB_LOOK"); cl && !walker.active) {
                double y = 0, p = 0;
                std::sscanf(cl, "%lf,%lf", &y, &p);
                cam.userYaw = y, cam.pitch = p, cam.idle = 0;
            }
        }
        // ---- the garage (garageshop.h): the truck wearing what it shows, the part on its plinth, the garage camera
        bool shopModel = false;
        shop::Display shopDisp;
        V3 shopBase, shopLo, shopHi, shopMid;
        static bool shopRestore = false;  // closed: the fitted parts going back on (once their visual is built)
        auto shopReady = [&](const VehicleSpec& vs) {
            if (visualReady(vs) || blockOnTemplates) return true;
            prebuildVisual(vs, sim.cars[sim.player].color);
            return false;
        };
        if (garageShop.open) {
            shopRestore = true;
            garageShop.tick(dt);
            if (padCtx == pad::Mode::Menu && (padLogic.in.lookX != 0 || padLogic.in.lookY != 0)) garageShop.turn(padLogic.in.lookX * dt * 2.6, padLogic.in.lookY * dt * 1.2);  // (gamepad: the right stick turns the model)
            shopToastT = std::max(0.0, shopToastT - dt);
            shopBlend = std::min(1.0, shopBlend + dt * 1.25);
            shop::liftSync(sim, garageShop, shopReady);
            Car& pc = sim.cars[sim.player];
            if (shopRecal && pc.spec == &tunedSpec(garageShop.vehicle, garageShop.fitted)) pc.recalibrateSpeedo(), shopRecal = false;
            if (const shop::Item* it = garageShop.previewing ? nullptr : garageShop.item()) {  // (trying it on: it's on the truck, not the plinth)
                shopDisp = shop::displayFor(*it, garageShop.vehicle, garageShop.fitted);
                shop::modelBounds(shopDisp, shopLo, shopHi);
                shopBase = shop::plinthSpot(pc.frame(), *pc.spec);
                shopBase.y = sim.world.height(shopBase.x, shopBase.z);
                if (shopDisp.onDrum) {
                    shop::Display drum;
                    drum.id = "oil_drum";
                    V3 dl, dh;
                    shop::modelBounds(drum, dl, dh);
                    shopBase.y += dh.y;
                }
                shopMid = shopBase + V3(0, (shopDisp.standUp ? shopHi.z - shopLo.z : shopHi.y - shopLo.y) * shopDisp.scale * 0.5, 0);
                shopModel = true;
            }
        } else if (shopRestore) {
            shop::liftSync(sim, garageShop, shopReady);
            const Car& pc = sim.cars[sim.player];
            if (pc.isProp() || pc.spec->model != garageShop.vehicle || pc.spec == &tunedSpec(garageShop.vehicle, garageShop.fitted)) shopRestore = false;
            if (shopRecal && !shopRestore) sim.cars[sim.player].recalibrateSpeedo(), shopRecal = false;
        }
        View view = editor.active ? editor.view(W, H) : campBehind() ? menuscene::camera(campLevel, sim.world, clock) : cam.update(fr, sim.world, dt, (dragging || padLooking) && !menu.open, camhold::driving(len(fr.vel), sim.paused ? 0.0 : sim.cars[sim.player].throttle));
        if (!walker.active && !editor.active && cam.mode == 2 && sim.player >= 0 && sim.player < (int)sim.cars.size()) {
            // The cockpit view from the driver's eyes (people.h sits them on the seat): the seat's hip point, up and a little ahead.
            const Car& pc = sim.cars[sim.player];
            people::Seat seat;
            if (const VisualTemplate* t = tryVisualTemplate(pc); t && people::seatOf(pc, *t, seat)) {
                const V3 eye = rideId ? carentry::acrossCab(pc, people::seatEye(seat)) : people::seatEye(seat);  // (riding along: the passenger's seat)
                const V3 d = eye - view.eye;
                view.eye += d, view.target += d;
            }
        }
        if (garageShop.open && !editor.active) {  // the garage camera, eased in from the chase view
            const Car& pc = sim.cars[sim.player];
            const shop::Cam gc = shop::camera(garageShop, pc.frame(), *pc.spec, shopModel ? shopMid : V3(), shopModel ? shopDisp.size : 1.0, clock);
            if (shopBlend <= dt * 1.25 + 1e-9) shopView = view, shopView.eye = gc.eye, shopView.target = gc.target;
            const double k = 1 - std::exp(-dt * 4.0);
            shopView.eye += (gc.eye - shopView.eye) * k;
            shopView.target += (gc.target - shopView.target) * k;
            shopView.eye.y = std::max(shopView.eye.y, sim.world.height(shopView.eye.x, shopView.eye.z) + 0.25);
            const double b = shopBlend * shopBlend * (3 - 2 * shopBlend);
            view.eye = view.eye + (shopView.eye - view.eye) * b;
            view.target = view.target + (shopView.target - view.target) * b;
            view.up = V3(0, 1, 0);
            view.fovy = (float)(view.fovy + (0.78 - view.fovy) * b);
            view.hasFocus = false;
        }
        fpLean = V3(), fpWork = V3();
        if (walker.active && !editor.active && cam.mode != 3) {
            // First person: eyes 1.62 m up, a little bob as you walk (the hips' own: people.h).
            const double bob = walker.grounded ? 0.016 * std::sin(walker.stride * 2) * std::min(1.0, len(walker.v) / 1.5) : 0.0;
            view.eye = walker.p + V3(0, walker.eyeHeight() + bob, 0);
            // Working on something low or out of reach (holding E): crouch and lean in to it, so your hands are on the work.
            const InteractOption& io = interact.current;
            const bool atWork = interact.progress > 0 && io.kind != IA_NONE && !io.blocked && io.kind != IA_RAISE_JACK && io.kind != IA_LOWER_JACK;
            workCrouch += ((atWork ? 1.0 : 0.0) - workCrouch) * std::min(1.0, dt * 6);
            if (workCrouch > 0.001 && std::isfinite(io.at.x)) {
                // Lean so the right shoulder comes to within a forearm and a bit of where that hand's working (last frame's: the
                // tool's grip), else to arm's length of the job.
                const V3 F(std::cos(footYaw), 0, -std::sin(footYaw)), Rt = cross(F, V3(0, 1, 0));
                const V3 shoulder0 = view.eye + Rt * 0.19 - V3(0, 0.2, 0) - F * 0.05;
                if (atWork) crouchAt = workHandOn ? workHand : io.at, crouchFrom = workHandOn ? shoulder0 : view.eye, crouchReach = workHandOn ? 0.53 : 0.85;
                const V3 to = crouchAt - crouchFrom;
                const double dist = len(to), a = dist > crouchReach ? (1 - crouchReach / dist) : 0.0;
                V3 lean = to * a;
                lean.y = std::max(lean.y, walker.p.y + 0.95 - view.eye.y);  // (no lower than a crouch)
                lean.y = std::min(lean.y, 0.0);
                view.eye += lean * smoothstep(0, 1, workCrouch);
            }
            fpLean = view.eye - (walker.p + V3(0, Walker::kEyeStand + bob, 0));  // (from the standing eyes: the body follows)
            fpWork = view.eye - (walker.p + V3(0, walker.eyeHeight() + bob, 0));  // (from the eyes as crouched: the lean in to work)
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
        if (camBlend < 1) {  // (getting in or out: from the old view to the new, eased)
            camBlend = editor.active || cam.mode == 3 ? 1.0 : std::min(1.0, camBlend + dt / 0.5);
            const double k = smoothstep(0, 1, camBlend);
            const V3 d0 = norm(camFrom.target - camFrom.eye), d1 = norm(view.target - view.eye);
            V3 d = d0 + (d1 - d0) * k;
            d = len(d) > 1e-3 ? norm(d) : d1;
            view.eye = camFrom.eye + (view.eye - camFrom.eye) * k;
            view.target = view.eye + d * 10.0;
            view.up = norm(camFrom.up + (view.up - camFrom.up) * k);
            view.fadeNear = std::max(view.fadeNear, 0.2f);
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
            const bool want = walker.active && !menu.open && !inv.open && !editor.active && cam.mode != 3;
            if (want != mouseLook) {
                SDL_SetRelativeMouseMode(want ? 1 : 0);
                mouseLook = want;
            }
        }
        view.width = W;
        view.height = H;
        if (audioOn) audio.setNearbyEngines(sim, view.eye, norm(cross(view.target - view.eye, view.up)),
                                           (float)std::max(0.35, sim.timeScale));
        {  // the horn (held): yours at your car, each buddy's at theirs (sent as "horn 1" / "horn 0" chatter, horn.h)
            const uint8_t* ks = SDL_GetKeyboardState(nullptr);
            const bool shiftDown = ks[SC_LSHIFT] || ks[SC_RSHIFT];
            const bool hornHeld = sim.player >= 0 && sim.player < (int)sim.cars.size() && !walker.active && !garageShop.open && !menu.open && !editor.active && !coop.typing && !coop.mapOpen &&
                                  (settings.keys.held(KA_HORN, ks, shiftDown) || (padCtx == pad::Mode::Drive && padLogic.in.horn));
            if (net.active() && hornSender.update(hornHeld, clock)) net.sendGame(horn::message(hornHeld));
            else if (!net.active()) hornSender.on = hornHeld;
            if (audioOn) {
                const V3 hr = norm(cross(view.target - view.eye, view.up));
                bool slotUsed[Audio::kHorns] = {};
                if (sim.player >= 0 && sim.player < (int)sim.cars.size()) {
                    const Car& pc = sim.cars[sim.player];
                    audio.setHornAt(0, hornHeld, pc.frame().com, view.eye, hr, horn::pitchFor(pc.mass));
                    slotUsed[0] = true;
                }
                int slot = 1;
                for (const Car& c : sim.cars) {  // (a remote car whose owner is honking: the first, if they own several)
                    const int owner = NetSession::ownerOf(c.netId);
                    if (!c.remote || slot >= Audio::kHorns || !hornRemote.sounding(owner, clock) || !c.occupied) continue;
                    audio.setHornAt(slot, true, c.frame().com, view.eye, hr, horn::pitchFor(c.mass));
                    slotUsed[slot++] = true;
                }
                for (int k = 0; k < Audio::kHorns; k++)
                    if (!slotUsed[k]) audio.setHorn(k, false, 0, 0, 1);
            }
        }
        if (audioOn) audio.setWinches(sim, view.eye, norm(cross(view.target - view.eye, view.up)), simDt);
        if (audioOn) audio.setEnvironment(soundMap(sim.world.map), (float)dayHours, settings.ambience,
                                         !walker.active && cam.mode == 2 ? 0.3f : 1.0f);
        if (audioOn) {  // the repair shops' din, the rivers and brooks, the forest (worldsound.h)
            static WorldSounds worldSounds;
            static BiomeTracker worldBiome;
            WorldSounds::Voice voices[WorldSounds::kVoices];
            if (!campBehind()) {
                worldSounds.prepare(sim.world);
                worldSounds.update(view.eye, norm(cross(view.target - view.eye, view.up)), voices);
                voices[WorldSounds::kVoices - 1] = WorldSounds::forest(worldBiome.update(sim.world, view.eye.x, view.eye.z, dt), (float)dayHours);
            }
            for (int v = 0; v < WorldSounds::kVoices; v++) audio.setWorldVoice(v, voices[v].loop, voices[v].gain, voices[v].pan);
        }
        lastView = view;
        clock += dt;
        view.time = (float)clock;
        {
            // Time of day: the clock runs (a whole day in settings.dayMinutes) unless it's
            // held at a fixed time; in multiplayer the host's (or the server's) clock rules,
            // and ours, and the setting, come back when we leave.
            static bool campWas = false;
            double dayRate = 0;  // clock hours per real second
            const bool fromHost = net.role == NetSession::CLIENT && net.dayHours >= 0;
            if (dayFromHost && !fromHost) dayHours = settings.clockHours;
            dayFromHost = fromHost;
            if (fromHost) {
                dayHours = net.dayHours;  // (run on to this moment by the session: everyone's sky agrees)
                net.dayFresh = false;
            } else if (opt.timeHours >= 0) dayHours = opt.timeHours;
            else if (campWorld) dayHours = campLevel.timeOfDay >= 0 ? campLevel.timeOfDay : menuscene::kHours;  // (the menu's camp: golden hour, whatever the clock says)
            else if (editor.active && editor.core.level.timeOfDay >= 0) dayHours = editor.core.level.timeOfDay;  // the editor shows the level's light
            else if (levelHours >= 0 && settings.timeOfDay != TOD_CYCLE) dayHours = levelHours;  // a level's hour holds over a fixed setting (with the cycle it starts there and runs)
            else if (settings.timeOfDay != TOD_CYCLE) dayHours = timeOfDayHours(settings.timeOfDay);
            else dayRate = clockAdvance(1.0, settings.dayMinutes);
            if (!sim.paused) dayHours = std::fmod(dayHours + dayRate * dt * sim.timeScale, 24.0);
            if (!fromHost && !campWorld) settings.clockHours = (float)dayHours;
            if (campWas && !campWorld) dayHours = settings.clockHours;  // (left the camp: the clock goes on from where it was)
            campWas = campWorld;
            if (net.role == NetSession::HOST) net.dayHours = dayHours, net.dayRate = sim.paused ? 0 : dayRate * sim.timeScale;
            {  // (the park sections whoever joins next builds: this world's, whenever it's rebuilt)
                static uint64_t sectionsOf = 0;
                if (net.role == NetSession::HOST && sectionsOf != sim.world.stamp) net.setSections(sim.world.editedSections), sectionsOf = sim.world.stamp;
            }
            renderer.daylight = campWorld ? menuscene::daylight(campLevel) : computeDaylight(dayHours);
            sim.dayHours = dayHours;  // (the air cools at night: the tires feel it)
            // Each owner's switch is carried reliably, including OFF and high beams.
            renderer.carBeams.assign(sim.cars.size(), 0);
            for (int i = 0; i < (int)sim.cars.size(); i++) {
                const Car& c = sim.cars[i];
                renderer.carBeams[i] = lampBeam(c.lampMode, renderer.daylight.night);
            }
        }
        menuBlur += ((menu.open ? (campBehind() ? (mainMenuShown() ? 0.0f : 0.45f) : 1.0f) : 0.0f) - menuBlur) * (float)std::min(1.0, dt * 10);
        if (!menu.open && menuBlur < 0.01f) menuBlur = 0;
        // Keep saved/menu choices separate from temporary runtime reductions.
        // Scripted captures stay deterministic unless adaptation is explicitly requested.
        if (probePending && frame > 90 && !probe.active()) probePending = false, startProbe();
        if (probe.active() && !garageShop.open && !editor.active) {
            const QualityProbe::Step step = probe.feed((float)frameTimeMs);
            if (step != QualityProbe::NONE) {
                applyPreset(settings.gfx, probe.preset());
                applySettings();
                if (step == QualityProbe::DONE) {
                    settings.gfxChosen = true;
                    saveSettings();
                    std::printf("graphics detected: %s\n", presetName(probe.preset()));
                    hud.toast(std::string("GRAPHICS: ") + presetName(probe.preset()) + ". CHANGE IT IN SETTINGS");
                }
            }
        }
        const bool inBackground = !scripted && !(SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS);  // (another window has the focus: throttled to 15 fps below)
        if (inBackground) {
            // (frames are slow on purpose: nothing for adaptive quality to judge)
        } else if (settings.adaptiveQuality && !probe.active() && !menu.open && !garageShop.open && !editor.active && (!scripted || opt.adaptive == 1)) {
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
        renderer.figures.clear();  // (people, you and your buddies included, are posed below: renderer.people)
        // A jack under a wheel: on the ground under its axle or arm, handle out, raised as far as it's been worked.
        renderer.frameModels.clear();
        if (editor.active) renderer.frameModels.insert(renderer.frameModels.end(), editor.ghostModels.begin(), editor.ghostModels.end());
        if (shopModel) {  // the garage's turntable: the part turning (on an oil drum when it's small), its middle on the axis
            if (shopDisp.onDrum) {
                Placed drum;
                drum.id = "oil_drum";
                drum.p = V3(shopBase.x, sim.world.height(shopBase.x, shopBase.z), shopBase.z);
                renderer.frameModels.push_back(drum);
            }
            Placed pl;
            pl.id = shopDisp.id;
            pl.variant = shopDisp.variant;
            pl.set = shopDisp.set;
            pl.scale = V3(shopDisp.scale, shopDisp.scale, shopDisp.scale);
            pl.yaw = garageShop.spin;
            pl.sunlit = true;
            const V3 mid = (shopLo + shopHi) * (0.5 * shopDisp.scale);
            const double cy = std::cos(pl.yaw), sy = std::sin(pl.yaw);
            if (shopDisp.standUp) {  // (on its tread: the model's up is across, its left is down)
                const V3 F(cy, 0, -sy), Lf(sy, 0, cy), D(0, -1, 0);
                pl.oriented = true, pl.ax[0] = F, pl.ax[1] = Lf, pl.ax[2] = D;
                pl.p = shopMid - (F * mid.x + Lf * mid.y + D * mid.z);
            } else {
                pl.p = shopBase - V3(mid.x * cy + mid.z * sy, 0, -mid.x * sy + mid.z * cy) + V3(0, -shopLo.y * shopDisp.scale, 0);
            }
            renderer.frameModels.push_back(pl);
        }
        if (mk::findModel("jack"))
            for (const Car& c : sim.cars)
                if (c.jackPlaced && c.jackWheel >= 0 && c.jackWheel < (int)c.wheels.size()) renderer.frameModels.push_back(Interact::jackModel(sim, c, c.jackWheel));
        for (const Sim::DroppedPart& d : sim.droppedParts) {  // broken parts taken off in a repair, left on the ground
            Placed pl;
            pl.id = repairPartModel((RepairPart)d.part), pl.p = d.p, pl.yaw = d.yaw;
            renderer.frameModels.push_back(pl);
        }
        // What rides on each vehicle, as models (renderer.carModels: one mesh per car, kept, placed on the GPU and culled
        // with the car). A pickup's tailgate (trunk.h), shut; what rides in the back of a truck with no cargo area to
        // open (the chains in their bag, the jack while it's there); and what's under it: the engine, gearbox, diffs,
        // shafts, arms and springs, as models on the physics nodes (carparts.h). A car's parts are ~80k vertices: the
        // nearest four get them all (within 20 m), the nearest eight the big ones (to 40 m).
        {
            static std::vector<std::vector<Placed>> on;
            on.resize(sim.cars.size());
            for (auto& v : on) recyclePlaced(v);  // (last frame's placements go back to be filled again: no garbage a frame)
            for (int i = 0; i < (int)sim.cars.size(); i++) {
                const Car& c = sim.cars[i];
                if (c.isProp() || c.nodes.empty()) continue;
                const double away = len(c.nodes[0].p - view.eye);
                if (c.bedGate() && away < 30) trunk::gateModels(sim, i, on[i]);  // (its size is the bed's at rest: the mesh is kept)
                if (c.spec->wheelLoss && !c.hasGate() && away < 80) {
                    const Frame fr = c.frame();
                    auto stowed = [&](const char* id, const V3& at) {
                        if (!mk::findModel(id)) return;
                        Placed pl;
                        pl.id = id, pl.p = at - fr.up * 0.12, pl.oriented = true;
                        pl.ax[0] = fr.fwd, pl.ax[1] = fr.up, pl.ax[2] = fr.left;
                        on[i].push_back(pl);
                    };
                    if (c.chains > 0) stowed("chain_bundle", c.stowAt(Car::STOW_CHAINS));
                    if (c.hasJack && !c.jackPlaced && !walker.holdingJack) stowed("jack", c.stowAt(Car::STOW_JACK));
                }
            }
            std::vector<std::pair<double, int>> near;
            for (int i = 0; i < (int)sim.cars.size(); i++) {
                const Car& c = sim.cars[i];
                if (c.isProp() || c.nodes.empty()) continue;
                const double away = len(c.nodes[0].p - view.eye);
                if (away < 40) near.push_back({away, i});
            }
            std::sort(near.begin(), near.end());
            for (size_t i = 0; i < near.size() && i < 8; i++) {
                const int level = i < 4 && near[i].first < 20 ? PARTS_ALL : PARTS_BIG;
                const int ci = near[i].second;
                carPartModels(sim.cars[ci], on[ci], level);
                carFrameModels(sim.cars[ci], on[ci], level);  // (a ladder frame's rails, crossmembers, brackets and jack pads: carframe.h)
            }
            size_t n = 0;
            for (int i = 0; i < (int)on.size(); i++) {
                if (on[i].empty()) continue;
                if (renderer.carModels.size() <= n) renderer.carModels.resize(n + 1);
                renderer.carModels[n].car = i;
                renderer.carModels[n].parts.swap(on[i]);
                n++;
            }
            renderer.carModels.resize(n);
        }
        // The work in your hands (first person): tools, the hand, a part going in; a wheel or
        // chain being moved (handwork.h).
        renderer.xrayRange = walker.active && !editor.active && cam.mode != 3 ? 7.0 : 0.0;  // (on foot: the hurt parts glow through the body)
        renderer.xrayEye = view.eye;
        renderer.wheelWork = Renderer::WheelWork();
        Handwork::Hold fpHands[2];
        bool fpHideTorso = false;
        if (walker.active && !editor.active && cam.mode != 3) {
            const Handwork hw = handwork(sim, walker, interact, view.eye, view.target - view.eye, clock);
            fpHands[0] = hw.hands[0], fpHands[1] = hw.hands[1];
            fpHideTorso = hw.hideTorso;
            workHandOn = hw.hands[1].on, workHand = hw.hands[1].p;
            {  // the tools' and jack's sounds, placed at the work (toolsounds.h): wrench ticks and nut squeals, hammer, jack, pry bar, tire irons, the wheel
                static ToolSounds toolSounds;
                if (audioOn) toolSounds.update(audio, interact, hw, view.eye, norm(cross(view.target - view.eye, view.up)));
            }
            engineJobFx(sim, fx, interact, hw, dt);
            renderer.frameModels.insert(renderer.frameModels.end(), hw.models.begin(), hw.models.end());
            renderer.wheelWork = hw.wheel;
            if (hw.carriedPose && renderer.carried.on) renderer.carried.c = hw.carriedC, renderer.carried.axis = hw.carriedAxis;
        }
        // People (people.h): you (first person on foot: your body, arms and hands on what you hold; seen from outside:
        // all of you) and your buddies on foot. Nobody's drawn in a seat: the cabs show their steering wheels, turning.
        {
            const uint64_t t0 = SDL_GetPerformanceCounter();
            renderer.people.v.clear();
            people::lastTris = 0;
            static people::Mover myMove;
            static std::map<std::string, people::Mover> buddyMove;
            const bool inGlide = camBlend < 1;
            if (walker.active) {
                people::Gait g;
                g.p = walker.p, g.yaw = walker.yaw, g.vel = walker.v, g.stride = walker.stride;
                g.grounded = walker.grounded || walker.inWater > 1.2;
                g.crouch = walker.crouchAmount();  // (first person takes it from the eyes, see fpLean)
                const people::Look me = people::lookFor(V3(0.56, 0.37, 0.2), 7);
                if (!editor.active && cam.mode != 3) {
                    people::Pose p = people::firstPerson(g, myMove, dt, footYaw, footPitch, view.eye, fpLean);
                    p.noTorso = fpHideTorso;
                    for (int s = 0; s < 2; s++)
                        if (fpHands[s].on) people::gripAt(p, s, fpHands[s].p, fpHands[s].axis, fpHands[s].grip);
                    if (renderer.carried.on && !fpHands[0].on && !fpHands[1].on) {  // a wheel in your arms: a hand either side of the tread
                        const V3 side = norm(cross(renderer.carried.axis, V3(0, 1, 0)));
                        const double R = renderer.carried.item.R;
                        for (int s = 0; s < 2; s++)
                            people::gripAt(p, s, renderer.carried.c + side * ((s == 0 ? 1 : -1) * R * 0.93) + V3(0, R * 0.25, 0), V3(0, 1, 0), 0.8);
                    }
                    if (!inGlide) people::build(p, me, renderer.people);  // (getting out: the view's still gliding from the seat)
                } else {
                    g.lookPitch = footPitch;
                    people::Pose p = people::walking(g, myMove, dt);
                    p.detail = 2;
                    people::build(p, me, renderer.people);
                }
            }
            const V3 viewDir = norm(view.target - view.eye), viewRight = norm(cross(viewDir, view.up));
            auto inView = [&](const V3& at, double d) { return d < 5 || dot(at - view.eye, viewDir) > 0.2 * d; };  // (well behind you: not drawn)
            renderer.carriedOthers.clear();
            auto buddy = [&](const std::string& who, const V3& at, double yaw, double stride, const V3* vel, double crouch = 0) {
                people::Mover& mv = buddyMove[who];
                uint32_t h = 2166136261u;
                for (char c : who) h = (h ^ (uint8_t)c) * 16777619u;
                mv.seed = h;
                people::Gait g;
                g.p = at, g.yaw = yaw, g.stride = stride, g.lookAround = true, g.crouch = crouch;
                g.grounded = at.y - sim.world.height(at.x, at.z) < 0.15;  // (off the ground: jumping)
                {  // their steps, from where they are (a new walker's first stride is only noted)
                    static std::map<std::string, double> lastStride;
                    const auto it = lastStride.find(who);
                    if (audioOn && it != lastStride.end() && g.grounded && std::fabs(stride - it->second) < 3 && stepped(it->second, stride))
                        audio.playAt(stepSfx(sim.world.surfaceAt(at.x, at.z)), at, view.eye, viewRight, crouch > 0.5 ? 0.12f : 0.25f);
                    lastStride[who] = stride;
                }
                if (vel) g.vel = *vel;
                else g.estimate = true;
                // What they hold and the work they're doing (repairsync.h): turned to it, down on their heels for a low job, the
                // tools in their hands and the sounds of them.
                Handwork rh;
                bool rhOn = false;
                V3 rEye = at + V3(0, Walker::kEyeStand, 0);
                repairsync::Remote::Player* rp = repairIn.find(who);
                if (rp) {
                    const repairsync::Work& rw = rp->work;
                    if (rw.working()) {
                        const V3 dw = rw.job.at - at;
                        if (dw.x * dw.x + dw.z * dw.z > 0.01) g.yaw = std::atan2(-dw.z, dw.x);
                        if (rw.job.kind != IA_RAISE_JACK && rw.job.kind != IA_LOWER_JACK && rw.job.at.y < at.y + 1.1) g.crouch = std::max(g.crouch, 1.0);
                    }
                    rhOn = repairIn.handwork(sim, *rp, at, g.yaw, g.crouch, clock, rh, &rEye);
                    if (rhOn) engineJobFx(sim, fx, rp->ia, rh, dt);
                    if (audioOn)
                        repairIn.sounds(*rp, rh, [&](Sfx s, const V3& p, float gain, float delay, float pitch) { audio.playAt(s, p, view.eye, viewRight, gain, delay, pitch); }, view.eye);
                }
                people::Pose p = people::walking(g, mv, dt);  // (posed even out of sight: their feet stay where they were)
                const double d = len(at - view.eye);
                if (d > 120 || !inView(at + V3(0, 1, 0), d)) return;
                p.detail = d < 8 ? 2 : d < 30 ? 1 : 0;
                if (rhOn) {
                    for (int s = 0; s < 2; s++)
                        if (rh.hands[s].on) people::gripAt(p, s, rh.hands[s].p, rh.hands[s].axis, rh.hands[s].grip), p.stretch = true;
                    if (d < 60) renderer.frameModels.insert(renderer.frameModels.end(), rh.models.begin(), rh.models.end());
                    if (rh.wheel.car >= 0 && renderer.wheelWork.car < 0) renderer.wheelWork = rh.wheel;
                    if (rp->work.carrying) {  // a wheel in their arms (onto the hub as it goes on), a hand either side of the tread
                        Renderer::Carried cw;
                        cw.on = true, cw.item = rp->work.carry, cw.up = V3(0, 1, 0);
                        const V3 fwd(std::cos(g.yaw), 0, -std::sin(g.yaw));
                        cw.c = rEye + fwd * (0.45 + cw.item.width * 0.5) - V3(0, 0.95 - cw.item.R * 0.4, 0), cw.axis = -fwd;
                        if (rh.carriedPose) cw.c = rh.carriedC, cw.axis = rh.carriedAxis;
                        renderer.carriedOthers.push_back(cw);
                        if (!rh.hands[0].on && !rh.hands[1].on) {
                            const V3 side = norm(cross(cw.axis, V3(0, 1, 0)));
                            for (int s = 0; s < 2; s++) people::gripAt(p, s, cw.c + side * ((s == 0 ? 1 : -1) * cw.item.R * 0.93) + V3(0, cw.item.R * 0.25, 0), V3(0, 1, 0), 0.8);
                        }
                    }
                }
                people::build(p, people::lookFor(h), renderer.people);  // (each their own clothes and colours)
            };
            for (const RemoteWalker& rw : coop.walkers) buddy(rw.who, rw.p, rw.yaw, rw.stride, nullptr, rw.crouch);
            if (const char* fg = scripted ? std::getenv("CZ_FIGURE") : nullptr) {  // (screenshots: a buddy on foot, CZ_FIGURE=m ahead of you, walking toward you[,side,speed[,turn rad/s[,crouch 0..1[,yaw off toward you, rad[,count in a row]]]]]])
                double d = 3, side = 0, sp = 1.7, turn = 0, figCrouch = 0, yawOff = 0, count = 1;
                std::sscanf(fg, "%lf,%lf,%lf,%lf,%lf,%lf,%lf", &d, &side, &sp, &turn, &figCrouch, &yawOff, &count);
                const V3 f = norm(V3(view.target.x - view.eye.x, 0, view.target.z - view.eye.z)), l(f.z, 0, -f.x);
                for (int k = 0; k < std::max(1, (int)count); k++) {  // (several: side by side, 0.9 m apart, each their own look)
                    V3 at = view.eye + f * d + l * (side + (k - (count - 1) * 0.5) * 0.9);
                    at.y = sim.world.height(at.x, at.z);
                    const double yaw = std::atan2(f.z, -f.x) + yawOff + turn * clock;
                    const V3 vel = V3(std::cos(yaw), 0, -std::sin(yaw)) * sp;
                    buddy(k == 0 ? std::string("CZ_FIGURE") : "CZ_FIGURE" + std::to_string(k), at, yaw, clock * sp * 3.2, &vel, figCrouch);
                }
            }
            // Every near cab's steering wheel (it turns with the steering).
            for (int i = 0; i < (int)sim.cars.size(); i++) {
                const Car& c = sim.cars[i];
                if (c.isProp() || c.nodes.empty()) continue;
                const double away = len(c.nodes[0].p - view.eye);
                if (away > 45 || !inView(c.nodes[0].p, away)) continue;
                const VisualTemplate* t = tryVisualTemplate(c);
                people::Seat seat;
                if (t && people::seatOf(c, *t, seat)) people::steeringWheel(seat, people::wheelTurnOf(c), renderer.people);
            }
            peopleMs += ((double)(SDL_GetPerformanceCounter() - t0) * 1000 / freq - peopleMs) * 0.05;
            if (scripted && std::getenv("CZ_PEOPLE_STATS") && frame % 30 == 0)
                std::printf("people: %d tris, %.3f ms\n", people::lastTris, peopleMs);
        }
        const uint64_t drawStart = SDL_GetPerformanceCounter();
        if (campBehind()) {  // (the camp's fire lights what is near it; its flicker is the strength)
            const V3 f = campFire;
            renderer.fireLight.pos = V3(f.x, sim.world.height(f.x, f.z) + 0.7, f.z);
            renderer.fireLight.strength = 260.0f * menuscene::flicker(clock);
        } else renderer.fireLight.strength = 0;
        if (menu.open && menu.title && !campWorld) {  // the old title page is its own screen: no world behind it
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
        renderer.passMark(FP_UI);
        const double drawCpuMs = (double)(SDL_GetPerformanceCounter() - drawStart) * 1000 / freq;
        hud.drawCpuMs += (drawCpuMs - hud.drawCpuMs) * 0.05;
        if (scripted && std::getenv("CZ_BENCH")) {  // (the scene's frame time: wait for the GPU to finish it)
            unsigned char px[4];
            gl::ReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            benchDraw = (double)(SDL_GetPerformanceCounter() - drawStart) / freq;
        }
        {  // (which top-left panels are up: they stack from the top, see hudlayout.h)
            hud.tl.devKeys = hud.devHotkeys;
            hud.tl.on[hudlayout::TL_PLAYTEST] = editorSuspended && !editor.active && !hud.photo;
            hud.tl.on[hudlayout::TL_CASH] = career.on;
            hud.tl.on[hudlayout::TL_JOB] = jobOn;
            hud.tl.on[hudlayout::TL_WINCH] = sim.ropeOn(sim.player) >= 0;
            hud.levelOn = run.active();
            const Car& pc = sim.cars[sim.player];
            hud.nearTruck = walker.active && len(walker.p - pc.frame().com) < 8;
            hud.trunkOpen = inv.open;
            hud.ctx.settingFull = settings.fullHud;
            hud.ctx.holdFull = settings.keys.held(KA_HUD_ALL, SDL_GetKeyboardState(nullptr), shiftDown());
            {  // (the proving ground's lap timer: only on the loop, driving, outside a level)
                const Car& lc = sim.cars[sim.player];
                if (lapStamp != sim.world.stamp) lapStamp = sim.world.stamp, lapTimer.setLoop(sim.world);
                lapTimer.setVehicle(lc.spec->name);
                const bool timing = sim.world.map == MAP_PROVING_GROUND && World::withShowcase && !run.active() && !walker.active && !sim.paused && !net.active();
                if (timing) lapTimer.update(lc.frame().com, dt * sim.timeScale);
                else lapTimer.reset();
                using E = LapTimer::Event;
                if (lapTimer.event == E::STARTED) hud.ctx.trigger(hudctx::E_LAPS, hudctx::kHoldSecs);
                else if (lapTimer.event == E::LAP || lapTimer.event == E::BEST) {
                    hud.toast("LAP " + formatRaceTime(lapTimer.last) + (lapTimer.event == E::BEST ? "  NEW BEST" : ""));
                    hud.ctx.trigger(hudctx::E_LAPS, hudctx::kHoldSecs);
                } else if (lapTimer.event == E::VOIDED) {
                    hud.toast("LAP VOID: STAY ON THE ROAD, PASS EVERY CHECKPOINT");
                    hud.ctx.trigger(hudctx::E_LAPS, hudctx::kHoldSecs);
                }
                hud.lapOn = lapTimer.running, hud.plLap = lapTimer.lap, hud.plLast = lapTimer.last, hud.plBest = lapTimer.best(), hud.plNext = lapTimer.next;
            }
            updateHudContext(hud, sim, settings.fuelMode > 0, (float)dt);
        }
        hud.update(dt);
        if (editor.active && !menu.open) editor.draw(renderer, W, H, dt);
        else if (!menu.open && !hud.photo && !garageShop.open) drawHud(renderer, sim, view, hud, &playback, net, W, H, fps, dt, settings.mph, career, caches);
        const bool showHud = !menu.open && !editor.active && !hud.photo && !garageShop.open;
        hud.debugOn = dbg.on;
        if (dbg.on && showHud) {  // F3: gathered and painted only while it's up
            if (dbg.tick(frameTimeMs, dt)) {
                const Car& pc = sim.cars[sim.player];
                const V3 at = walker.active ? walker.p : pc.frame().com;
                dbg.biome = biomeAt(sim.world, at.x, at.z);
                const std::vector<Place> spots = pilotPlaces(sim, net);
                double best = 1e18;
                dbg.place[0] = 0, dbg.placeDist = 0;
                for (const Place& pl : spots) {
                    if (pl.kind != 0) continue;
                    const double d = std::hypot(pl.p.x - at.x, pl.p.z - at.z);
                    if (d < best) best = d, dbg.placeDist = d, std::snprintf(dbg.place, sizeof dbg.place, "%s", pl.name.c_str());
                }
            }
            DebugInputs di;
            di.sim = &sim, di.walker = &walker, di.run = &run, di.net = &net, di.peer = peerAddress.c_str();
            di.camMode = cam.mode, di.mph = settings.mph, di.playtest = editorSuspended, di.throttleCap = hud.throttleCap;
            di.dayHours = dayHours, di.simSeconds = dbgSimSeconds, di.frame = dbg.history.stats();
            di.physicsMs = hud.physicsMs, di.drawCpuMs = hud.drawCpuMs, di.substeps = dbgSubsteps;
            di.profile = &frameProfile();
            di.physicsThreads = sim.physicsThreads(), di.physicsThreadLimit = physicsThreadLimit();
            di.width = W, di.height = H, di.gpu = hud.gpu.c_str(), di.build = buildText.c_str();
            const Renderer::GeometryStats& gs = renderer.geometryStats();
            di.terrainTris = gs.terrainTriangles, di.terrainFullTris = gs.terrainFullTriangles;
            di.terrainChunks = gs.terrainChunks, di.terrainVisible = gs.terrainVisibleChunks;
            di.sceneryVerts = gs.sceneryVertices, di.shadowVerts = gs.shadowSceneryVertices;
            const PropStats& dps = renderer.propStats();
            di.propDraws = dps.draws, di.propInstances = dps.instances, di.propBillboards = dps.billboards, di.propTris = dps.tris, di.propGpuMs = dps.gpuMs;
            di.renderScale = renderer.settings.renderScale, di.shadows = renderer.settings.shadows, di.msaa = renderer.settings.msaa;
            dbg.readSlow(di, dt);
            const bool corner = jobOn || career.on || editorSuspended;  // (their panels sit in the top left)
            const uint64_t dbgStart = SDL_GetPerformanceCounter();
            dbg.draw(renderer, di, W, H, corner ? 60 * std::max(0.75f, H / 720.0f) : 0);
            if (frame >= 40) dbgMsSum += (double)(SDL_GetPerformanceCounter() - dbgStart) * 1000 / freq, dbgMsN++;  // (CZ_BENCH prints it)
        }
        if (showHud && walker.active && cam.mode != 3) {  // on foot: a crosshair, the job in front of you, what you carry
            const float u = std::max(0.75f, H / 720.0f), s = px(1.4f * u);
            renderer.rect(W / 2.0f - 2 * u, H / 2.0f - 2 * u, 4 * u, 4 * u, 1, 1, 1, 0.8f);
            const bool padNow = pad::ui.last && padDev.connected();
            if (!inv.open && !std::getenv("CZ_OLDHUD")) drawFocus(renderer, focus, interact, sim, view, walker, W, H, padNow);
            else if (interact.current.kind != IA_NONE && !inv.open) {
                const bool tap = interact.current.hold <= 0 && !interact.current.continuous;  // (E opens a menu: no holding)
                const std::string line = std::string(padNow ? (tap ? "X  " : "HOLD X  ") : (tap ? K(KA_INTERACT) + "  " : "HOLD " + K(KA_INTERACT) + "  ")) + interact.current.prompt;
                const float ps = tap ? px(1.8f * u) : s;
                centeredPill(renderer, W / 2.0f, H / 2.0f + 40 * u, ps, line, 0, 0, 0, 0.6f, 1.0f, 0.85f, 0.45f);
                if (interact.progress > 0) {
                    const float bw = 220 * u, bx = W / 2.0f - bw / 2, by = H / 2.0f + 40 * u + 8 * s + 14 * u;
                    renderer.rect(bx, by, bw, 5 * u, 0, 0, 0, 0.6f);
                    renderer.rect(bx, by, bw * (float)interact.share(), 5 * u, 1.0f, 0.75f, 0.3f, 0.95f);
                }
            }
            if (interact.hover.ok && !walker.carrying && !inv.open) {  // the part under the crosshair: its name and how it is, and the repair that goes with it
                const PartHit& ph = interact.hover;
                const bool bad = ph.tag.state == PS_BROKEN, hurt = ph.tag.state != PS_OK;
                const float cr = bad ? 1.0f : hurt ? 1.0f : 0.85f, cg = bad ? 0.42f : hurt ? 0.7f : 0.95f, cb = bad ? 0.32f : hurt ? 0.3f : 0.85f;
                const float ls = px(1.8f * u);
                float y = H / 2.0f - 44 * u - 12 * ls;
                std::string hint;
                const bool onIt = (interact.current.kind == IA_FIX || (interact.current.kind == IA_REMOVE_WHEEL && interact.current.step >= 0)) && partMatchesFix(ph.tag, interact.current.fix);
                if (!onIt && ph.car >= 0 && ph.car < (int)sim.cars.size())
                    for (const Fix& f : fixesFor(sim.cars[ph.car]))
                        if (partMatchesFix(ph.tag, f)) {  // (its next step, as the job would put it)
                            const InteractOption fo = Interact::fixOption(sim, walker, ph.car, f, interact.kit, interact.trunkOn ? &interact.own : nullptr);
                            if (fo.kind == IA_NONE) break;
                            const std::string far = len(ph.at - (walker.p + V3(0, 1.0, 0))) > 2.5 ? "GET CLOSER: " : "";
                            hint = far + (fo.blocked ? fo.prompt : "HOLD " + K(KA_INTERACT) + ": " + fo.prompt);
                            break;
                        }
                if (!hint.empty()) {
                    centeredPill(renderer, W / 2.0f, y, s, hint, 0, 0, 0, 0.9f, 1.0f, 0.85f, 0.45f);
                    y -= 15 * s;
                }
                centeredPill(renderer, W / 2.0f, y, ls, ph.tag.label(), 0, 0, 0, 1.0f, cr, cg, cb);
            }
            if (interact.checklist.any() && !inv.open) {  // the repair under way: its steps, in order, top right
                const RepairChecklist& cl = interact.checklist;
                const float cs = px(1.3f * u), lh = 8 * cs + 5 * u;
                float bw = r_textW(renderer, cl.title, cs);
                for (const std::string& row : cl.rows) bw = std::max(bw, r_textW(renderer, "[X] " + row, cs));
                bw += 28 * u;
                const float bx = W - bw - 14 * u, by = H * 0.2f;
                pill(renderer, bx, by, bw, 16 * u + lh * (float)(cl.rows.size() + 1), 0, 0, 0, 0.55f);
                renderer.text(bx + 14 * u, by + 8 * u, cs, cl.title, 1.0f, 0.62f, 0.25f, 1);
                for (size_t i = 0; i < cl.rows.size(); i++) {
                    const bool d = cl.done[i], nx = (int)i == cl.next;
                    const float yy = by + 8 * u + lh * (float)(i + 1);
                    const float cr = d ? 0.55f : nx ? 1.0f : 0.8f, cg = d ? 0.85f : nx ? 0.85f : 0.8f, cb = d ? 0.55f : nx ? 0.45f : 0.82f;
                    renderer.text(bx + 14 * u, yy, cs, std::string(d ? "[X] " : "[ ] ") + cl.rows[i], cr, cg, cb, d ? 0.75f : 1.0f);
                }
            }
            if (walker.hand.any() && !walker.carrying && !inv.open) {
                const std::string line = "IN YOUR HAND: " + trunk::name(walker.hand) + (padNow ? "   (X AT THE BACK OF YOUR TRUCK: PUT IT BACK)" : "   (" + K(KA_INTERACT) + " AT THE BACK OF YOUR TRUCK: PUT IT BACK)");
                drawPrompt(renderer, u, W, H, 0, line, 0, 0, 0, 0.55f, 0.9f, 0.92f, 0.95f);
            }
            if (sim.handLine.on() && sim.handLine.car < (int)sim.cars.size()) {
                const Sim::HandLine& hl = sim.handLine;
                const double out = len(hl.hand - sim.cars[hl.car].hookPoint(hl.hook)), most = sim.handLineReach(hl) + 3.2;
                char lb[160];
                std::snprintf(lb, sizeof lb, "%s: %.0f OF %.0f M OUT   WALK IT TO AN ANCHOR, HOLD %s TO HOOK   %s: LET GO", hl.kind == ROPE_WINCH ? "WINCH LINE" : "TOW STRAP", out, most,
                              padNow ? "X" : K(KA_INTERACT).c_str(), padNow ? "B" : K(KA_PUT_DOWN).c_str());
                drawPrompt(renderer, u, W, H, 0, lb, 0, 0, 0, 0.55f, 0.9f, 0.92f, 0.95f);
            }
            if (walker.carrying) {
                const std::string line = "CARRYING A " + walker.carrying->sizeName() + (walker.carrying->tireOnly ? "" : " WHEEL") +
                                         (walker.carrying->popped ? " (FLAT)" : "") + (padNow ? "   B: PUT IT DOWN" : "   G: PUT IT DOWN");
                drawPrompt(renderer, u, W, H, 0, line, 0, 0, 0, 0.55f, 0.9f, 0.92f, 0.95f);
            }
        }
        if (showHud && run.active()) drawLevelHud(renderer, run, sim, view, W, H);
        if (editorSuspended && !editor.active && !hud.photo) {  // a playtest: the way back, always on screen
            const float u = std::max(0.75f, H / 720.0f);
            const std::string t = "PLAYTEST   " + K(KA_PLAYTEST_BACK) + ": BACK TO THE EDITOR";
            const float s = hudlayout::fitScale((float)t.size(), 1.5f * u, hudlayout::topLeftMaxW(u, W) - 28 * u), py = hudlayout::topLeftY(hud.tl, hudlayout::TL_PLAYTEST, u);
            const float w = r_textW(renderer, t, s) + 28 * u;
            pill(renderer, 12 * u, py, w, 8 * s + 18 * u, 0.30f, 0.17f, 0.02f, 0.92f);
            label(renderer, 26 * u, py + 9 * u, s, t, 1.0f, 0.88f, 0.45f, 1);
        }
        if (showHud && jobOn) {  // the job: what to do next, and where
            const float u = std::max(0.75f, H / 720.0f);
            const V3 me = walker.active ? walker.p : sim.cars[sim.player].frame().com;
            const std::string status = jobStatus(job, sim, me);
            const float s = hudlayout::fitScale((float)std::max(job.title.size(), status.size()), 1.2f * u, hudlayout::topLeftMaxW(u, W) - 24 * u);
            const float w = std::max(r_textW(renderer, job.title, s), r_textW(renderer, status, s)) + 24 * u;
            const float jy = hudlayout::topLeftY(hud.tl, hudlayout::TL_JOB, u);
            pill(renderer, 12 * u, jy, w, 2 * 8 * s + 22 * u, 0, 0, 0, 0.55f);
            label(renderer, 22 * u, jy + 6 * u, s, job.title, 1.0f, 0.62f, 0.25f, 1);
            renderer.text(22 * u, jy + 12 * u + 8 * s, s, status, 0.92f, 0.93f, 0.95f, 1);
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
                const float u = std::max(0.75f, H / 720.0f);
                char b[64];
                std::snprintf(b, sizeof b, "FUEL %.0f L%s", c.fuel, c.jerry > 0.5 ? ("   CAN " + std::to_string((int)std::lround(c.jerry)) + " L").c_str() : "");
                const hudlayout::Column col = hudlayout::rightColumn(u, W, H, 0, (float)std::strlen(b), 0);  // (its own slot above the terrain tag)
                const float s = col.fuelScale, f = (float)(c.fuel / c.spec->fuelCapacity), w = col.fuelBarW, x = col.fuel.x + 8 * u, y = col.fuel.y + 6 * u;
                const HudFade fade(renderer, hud, hudctx::E_FUEL);
                pill(renderer, col.fuel.x, col.fuel.y, col.fuel.w, col.fuel.h, 0, 0, 0, 0.5f);
                renderer.text(x, y, s, b, f < 0.15f ? 1.0f : 0.9f, f < 0.15f ? 0.4f : 0.9f, f < 0.15f ? 0.3f : 0.92f, 1);
                renderer.rect(x, y + 8 * s + 4 * u, w, 5 * u, 1, 1, 1, 0.15f);
                renderer.rect(x, y + 8 * s + 4 * u, w * f, 5 * u, f < 0.15f ? 1.0f : 0.95f, f < 0.15f ? 0.35f : 0.75f, 0.25f, 0.95f);
                renderer.hudAlpha = fade.old;  // (OUT OF FUEL is a fault: always up)
                if (c.outOfFuel()) centeredPill(renderer, W / 2.0f, H * 0.62f, px(1.6f * u), "OUT OF FUEL: A BUDDY'S SPARE CAN (F ON FOOT) OR A TOW TO A SHOP", 0, 0, 0, 1, 1, 0.5f, 0.4f);
            }
        }
        if (showHud && hud.ctx.alpha(hudctx::E_TIRESPEED) > 0.01f) {  // over a tire's speed rating: it can blow out
            const HudFade fade(renderer, hud, hudctx::E_TIRESPEED);
            centeredPill(renderer, W / 2.0f, H * 0.72f, px(1.3f * std::max(0.75f, H / 720.0f)), "TIRES OVER SPEED RATING", 0, 0, 0, 0.6f, 1, 0.72f, 0.25f);
        }
        {  // a tire blew from being driven too fast
            static int blownSeen = 0;
            const Car& pc = sim.cars[sim.player];
            if (pc.blowouts > blownSeen) hud.toast("TIRE BLEW OUT: OVER ITS SPEED RATING");
            blownSeen = pc.blowouts;
        }
        coop.update(dt);
        if (showHud || (coop.mapOpen && !menu.open && !editor.active)) {
            if (showHud) coop.drawWorld(renderer, sim, net, view, W, H);
            if (coop.mapOpen) {
                coop.drawMap(renderer, sim, net, W, H, discoverySpots());
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
            if (menu.devOpen()) {  // (the dev menu's TELEPORT lists)
                menu.devScenario = playback.active();
                menu.devSections.clear();
                for (const World::SectionSpot& sp : sim.world.sections) menu.devSections.push_back(sp.name);
                menu.devPlaces.clear();
                for (const Place& pl : pilotPlaces(sim, net))
                    if (pl.kind == 0) menu.devPlaces.push_back(pl.name);
            }
            menu.netActive = net.active() && !net.joining();
            menu.levelActive = run.active();
            menu.editorSuspended = editorSuspended;
            menu.jobActive = jobOn && !job.done;
            menu.jobStatus = jobOn ? jobStatus(job, sim, sim.cars[sim.player].frame().com) : "";
            menu.netClient = net.role == NetSession::CLIENT;
            menu.netHost = net.role == NetSession::HOST;
            menu.sessionPort = menu.netHost ? net.port() : 0;
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
        if (inv.open && walker.active && !menu.open && !hud.photo) {  // the truck's inventory (over the rest of the HUD)
            const float u = std::max(0.75f, H / 720.0f);
            const bool padNow = pad::ui.last && padDev.connected();
            invLay = inv.layout(W, H, u, padNow);
            drawInvMenu(renderer, inv, invLay, W, H, padNow);
        }
        if (garageShop.open && !menu.open) {
            const int at = sim.stationAt(sim.cars[sim.player].frame().com);
            const std::string toast = shopToastT > 0 ? shopToast : std::string();
            shop::draw(renderer, garageShop, career, at >= 0 ? sim.world.stations[at].name : "", W, H, (float)std::min(1.0, shopBlend * 1.6), pad::ui.last && padDev.connected(),
                       sim.cars[sim.player].spec != &tunedSpec(garageShop.vehicle, garageShop.shown()), toast, (float)std::min(1.0, shopToastT));
        }
        if (mapLoad.fade > 0.001f) {  // a map switch: black, with its name once it's dark enough to read
            renderer.rect(0, 0, (float)W, (float)H, 0, 0, 0, std::min(1.0f, mapLoad.fade));
            if (mapLoad.fade > 0.6f) {
                const float s = std::max(1.0f, H / 360.0f);
                const std::string t = std::string("LOADING ") + mapName(mapLoad.map);
                renderer.text(W * 0.5f - renderer.textWidth(t, s) * 0.5f, H * 0.5f, s, t, 0.85f, 0.85f, 0.85f, std::min(1.0f, (mapLoad.fade - 0.6f) * 4));
            }
        }
        menu.sceneBehind = campBehind();
        if (!campWorld) previewMenu = false;
        if (mainMenuShown()) {
            mainFade = std::min(1.0f, mainFade + (float)dt * 3.0f);
            mainNav.devHotkeys = settings.devHotkeys, mainNav.debugOn = dbg.on;
            mainmenu::draw(renderer, mainNav, W, H, mainFade, clock, pad::ui.last);
            if (previewMenu) {
                const float us = std::max(0.75f, H / 720.0f), ps = hud::px(1.6f * us);
                const std::string t = "PREVIEW OF THE MENU SCENE   ESC: BACK TO THE EDITOR";
                renderer.rect(W - renderer.textWidth(t, ps) - 28 * us, 10 * us, renderer.textWidth(t, ps) + 20 * us, 8 * ps + 14 * us, 0, 0, 0, 0.6f);
                renderer.text(W - renderer.textWidth(t, ps) - 18 * us, 17 * us, ps, t, 0.4f, 0.9f, 1.0f, 1.0f);
            }
        } else if (mpShown()) {
            mpSync();
            mainFade = std::min(1.0f, mainFade + (float)dt * 3.0f);
            mpmenu::draw(renderer, mpNav, mpView, W, H, mainFade, clock, pad::ui.last, !campBehind());
        } else {
            mainFade = 0;
            menu.draw(renderer, settings, W, H, dt);
        }
        if (!mpShown()) mpWasShown = false;
        renderer.flushHud(W, H);

        // CZ_BENCH=1 (with --shot-frames): average frame time, the GPU finished (one pixel read back).
        static const bool bench = std::getenv("CZ_BENCH") != nullptr;
        static double benchSum = 0;
        static int benchN = 0;
        static double benchGrass = 0;
        if (bench && scripted && frame >= 40) benchSum += benchDraw, benchGrass += renderer.grassGpuMs(), benchN++;
        if ((bench && scripted && frame >= 40) || (opt.bench && frame > benchWarm)) {
            benchCarN++;
            const auto& g = renderer.geometryStats();
            benchCars.carDraws += g.carDraws, benchCars.carTris += g.carTris, benchCars.carShadowDraws += g.carShadowDraws;
            benchCars.carShadowTris += g.carShadowTris, benchCars.carPrepMs += g.carPrepMs, benchCars.carPartsMs += g.carPartsMs;
            benchCars.carRebuilds += g.carRebuilds;
            for (int k = 0; k < 6; k++) benchCars.carKindTris[k] += g.carKindTris[k];
        }
        if (scripted && opt.shotFrames.count(frame)) {
            if (bench && benchN) {
                std::printf("bench: scene %.2f ms/frame over %d frames\n", 1000.0 * benchSum / benchN, benchN);
                std::printf("bench: grass %.2f ms GPU (timer query)\n", benchGrass / benchN);
                const auto& geometry = renderer.geometryStats();
                std::printf("bench: terrain %u/%u triangles, %u/%u visible chunks; scenery %u vertices, shadows %u vertices\n",
                            geometry.terrainTriangles, geometry.terrainFullTriangles,
                            geometry.terrainVisibleChunks, geometry.terrainChunks,
                            geometry.sceneryVertices, geometry.shadowSceneryVertices);
                std::printf("bench: occlusion %u of %u boxes hidden behind the ground (terrain chunks, scenery, trees); %u terrain chunks of the in-frustum ones\n", geometry.occlusionHidden, geometry.occlusionTests, geometry.terrainOccluded);
                std::printf("bench: adaptive %d, scale %.2f, shadows %d, MSAA %d, AO %d, ground %d, props %d, grass %d\n",
                            settings.adaptiveQuality, renderer.settings.renderScale, renderer.settings.shadows,
                            renderer.settings.msaa, renderer.settings.ao, renderer.settings.ground, renderer.settings.props, renderer.settings.grass);
                printCarBench();
                if (dbgMsN) std::printf("bench: debug screen %.3f ms/frame CPU (gather + queue) over %d frames\n", dbgMsSum / dbgMsN, dbgMsN);
                const VegStats& vs = renderer.vegStats();
                auto pass = [](const VegStats::Pass& p) { return std::to_string(p.draws) + " draws " + std::to_string(p.tris / 1000) + "k tris"; };
                std::printf("bench: vegetation main: trees %s (%d), bushes %s (%d), reeds and logs %s, grass %s (%d tufts); shadows: trees %s, bushes %s, reeds and logs %s\n",
                            pass(vs.trees).c_str(), vs.trees.inst, pass(vs.bushes).c_str(), vs.bushes.inst, pass(vs.logs).c_str(), pass(vs.grass).c_str(), vs.grass.inst,
                            pass(vs.treesShadow).c_str(), pass(vs.bushesShadow).c_str(), pass(vs.logsShadow).c_str());
                const PropStats& ps = renderer.propStats();
                std::printf("bench: props %.2f ms GPU (timer queries), %.2f ms CPU (%.2f ms culling/LODs); main %d draws, %d instances, %d billboards, %d scatter, %.0fk tris; "
                            "shadows %d draws, %d instances, %.0fk tris\n",
                            ps.gpuMs, ps.cpuMs, ps.selectMs, ps.draws, ps.instances, ps.billboards, ps.scatter, ps.tris / 1000.0, ps.shadowDraws, ps.shadowInstances,
                            ps.shadowTris / 1000.0);
                std::printf("bench: models main %u draws, %.0fk tris; shadows %u draws, %.0fk tris\n", geometry.modelDraws, geometry.modelVertices / 3000.0,
                            geometry.shadowModelDraws, geometry.shadowModelVertices / 3000.0);
                if (std::getenv("CZ_TEXSTATS")) {  // every texture held: format, size, mips, VRAM (texenc.h)
                    std::string table;
                    const std::vector<std::string> bad = texenc::textureReport(&table);
                    std::printf("texstats:\n%s", table.c_str());
                    for (const std::string& n : bad) std::printf("texstats: %s has no full mip chain\n", n.c_str());
                }
            }
            char path[512];
            std::snprintf(path, sizeof(path), "%s_%04d.ppm", opt.shotPrefix.c_str(), frame);
            writePPM(path, renderer.readPixels(W, H), W, H);
            std::printf("wrote %s\n", path);
            if (frame >= *opt.shotFrames.rbegin()) running = false;
        }
        if (opt.benchSuite && suitePhase == 1 && bf >= benchWarm + opt.bench + 2) {  // the run's F3 screen, beside the summary
            const BenchSuite::Done& d = suite.done().back();
            const std::string shot = benchPathStem() + "-" + benchSlug(d.mapName + (*d.e.place ? std::string("-") + d.e.place : "") + "-" + d.presetName) + ".bmp";
            std::printf(writeBMP(shot, renderer.readPixels(W, H), W, H) ? "bench: wrote %s\n" : "bench: could not write %s\n", shot.c_str());
            if (suiteDbgOn) dbg.toggle(false), suiteDbgOn = false;
            suitePhase = 2;
        }
        renderer.passMark(FP_SWAP);
        bool simKicked = false;
        if (asyncAllowed && !sim.paused && !net.active() && !editor.active && !playback.active() && dt > 0) {
            // (Scripted benches run a fixed step, so the guess is exact; otherwise a little under the last frame's, never over.)
            const double ahead = scripted ? dt : std::min(0.9 * dt, 0.03);
            simAsync.kick(sim, ahead, simMaxSteps(sim.h));
            simCredit += ahead;
            simKicked = true;
        }
        if (opt.bench && offscreenBench) gl::Finish();  // (offscreen the swap doesn't wait for the GPU, so the bench waits; with a real window it must not: a Finish here made CPU and GPU run one after the other)
        SDL_GL_SwapWindow(win);
        if (allocWatch) {
            const uint64_t n = allocstat::count(), all = allocstat::countAll();
            if (frame == allocWarm && std::getenv("CZ_ALLOC_TRACE")) allocstat::trace(true);
            if (frame > allocWarm) allocFrames.push_back(n - allocLast), allocAllSum += all - allocAllLast;
            allocLast = n, allocAllLast = all;
        }
        if (const int cap = inBackground ? 15 : campBehind() ? 30 : fpsCap(); !scripted && cap > 0 && (inBackground || campBehind() || SDL_GL_GetSwapInterval() == 0)) {  // FPS TARGET with VSync off: the frame cap (15 fps whenever the window is in the background, to spare the machine; 30 on the main menu's camp, which has nothing to do faster)
            const uint64_t until = now + freq / (uint64_t)cap;
            for (uint64_t t = SDL_GetPerformanceCounter(); t < until; t = SDL_GetPerformanceCounter()) {
                const double leftMs = (double)(until - t) * 1000 / freq;
                if (leftMs > 2) SDL_Delay((uint32_t)(leftMs - 1.5));  // (sleep most of it: SDL asks Windows for 1 ms timer ticks)
            }
        }
        if (simKicked) simAsyncSteps += simAsync.join();
        frame++;
    }
    writeSuiteSummary();  // (the window closed part-way: what was done)
    if (allocWatch && !allocFrames.empty()) {
        std::vector<uint64_t> v = allocFrames;
        std::sort(v.begin(), v.end());
        uint64_t sum = 0;
        for (uint64_t x : v) sum += x;
        std::printf("allocs: %zu frames, main thread %.1f/frame (median %llu, p95 %llu, max %llu), all threads %.1f/frame\n", v.size(), (double)sum / v.size(),
                    (unsigned long long)v[v.size() / 2], (unsigned long long)v[v.size() * 95 / 100], (unsigned long long)v.back(), (double)allocAllSum / v.size());
        allocstat::dump(stdout, 40);
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
    if (reload && editor.started() && editor.core.unsaved()) resume.insert(resume.end(), {"--resume-edit", editor.core.level.name});  // (back in the editor on the recovery copy)
    if (opt.timeHours < 0) saveSettings();  // (the clock carries on next time, and after a reload)
    if (editor.started()) editor.autosaveNow();  // (closing the game with unsaved edits, in the editor or while testing them: a recovery copy)
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
