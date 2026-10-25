#include "menu.h"


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gamepad.h"
#include "net.h"
#include "rope.h"
#include "hudkit.h"
#include "platform.h"
#include "sdl_min.h"
#include "vehicles.h"
#include "world.h"

using namespace hud;

// ---- Settings file ----------------------------------------------------------------

std::string Settings::defaultPath() {
    return configDir() + "/settings.txt";
}

bool Settings::load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    gfxChosen = true;  // (a file from before the detection: its graphics are the player's)
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        char key[64], text[192] = "";
        if (std::sscanf(line, "%63s %191s", key, text) < 1) continue;
        std::string k = key;
        if (k == "name") {
            playerName = text;
            continue;
        }
        if (k == "physicsthreads") {
            physicsThreads = (int)std::clamp(std::strtol(text, nullptr, 10), 0L, (long)physicsThreadLimit());
            continue;
        }
        if (k == "pilotdest") {
            pilotDest = std::max(0, std::atoi(text));
            continue;
        }
        if (k == "join") {
            joinAddress = text == std::string("127.0.0.1") ? "" : text;  // (the old default: not something you typed)
            continue;
        }
        if (k == "rendezvous") {
            rendezvous = std::string(text).substr(0, 80);
            continue;
        }
        if (k == "recent") {
            if (recentJoins.size() < 3 && *text) recentJoins.push_back(std::string(text).substr(0, 60));
            continue;
        }
        if (k == "netpassword") {
            netPassword = std::string(text).substr(0, 32);
            continue;
        }
        if (k == "rememberpw") {
            rememberPassword = std::atoi(text) != 0;
            continue;
        }
        if (k == "joinpassword") {  // (the rest of the line)
            const char* p = std::strstr(line, "joinpassword");
            std::string v = p ? p + 12 : "";
            while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(v.begin());
            while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
            joinPassword = v.substr(0, 64);
            continue;
        }
        if (k == "session") {  // (the rest of the line: it can have spaces)
            const char* p = std::strstr(line, "session");
            std::string v = p ? p + 7 : "";
            while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(v.begin());
            while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
            sessionName = v.substr(0, 24);
            continue;
        }
        const float v = (float)std::atof(text);
        if (k == "mph") mph = v != 0;
        else if (k == "winchspeed" && std::isfinite(v)) winchSpeed = std::clamp((double)v, recovery::kWinchSpeedMin, recovery::kWinchSpeedMax);
        else if (k == "volume") volume = std::min(std::max(v, 0.0f), 1.0f);
        else if (k == "music") music = std::min(std::max(v, 0.0f), 1.0f);
        else if (k == "ambience" && std::isfinite(v)) ambience = std::clamp(v, 0.0f, 1.0f);
        else if (k == "fov") fovDeg = std::min(std::max(v, 50.0f), 100.0f);
        else if (k == "fullscreen") fullscreen = v != 0;
        else if (k == "maxplayers") maxPlayers = std::min(std::max((int)v, 2), 8);
        else if (k == "hostport") {  // (out of range, or not a number: the default)
            const int p = net::parsePort(text, net::kMinHostPort);
            hostPort = p > 0 ? p : net::kDefaultPort;
        }
        else if (k == "vsync") vsync = v != 0;
        else if (k == "gfxchosen") gfxChosen = v != 0;
        else if (k == "adaptive") adaptiveQuality = v != 0;
        else if (k == "devhotkeys") devHotkeys = v != 0;
        else if (k == "fps") showFps = v != 0;
        else if (k == "fullhud") fullHud = v != 0;
        else if (k == "steeringassist") steeringAssist = v != 0;
        else if (k == "shadows") gfx.shadows = std::min(std::max((int)v, 0), 3);
        else if (k == "msaa") gfx.msaa = (int)v;
        else if (k == "ao") gfx.ao = v != 0;
        else if (k == "bloom") gfx.bloom = v != 0;
        else if (k == "clouds") gfx.clouds = v != 0;
        else if (k == "scale") gfx.renderScale = std::min(std::max(v, 0.5f), 2.0f);
        else if (k == "drawdistance" && std::isfinite(v)) gfx.drawDistance = v > 0 ? std::clamp(v, 150.0f, 6000.0f) : 0.0f;
        else if (k == "sharpen") gfx.sharpen = v != 0;
        else if (k == "fpstarget") fpsTarget = fpsTargetFromSaved(v);
        else if (k == "grass") gfx.grass = v != 0 ? 2 : 0;  // (before it had levels: on is medium)
        else if (k == "grassq") gfx.grass = std::min(std::max((int)v, 0), 3);
        else if (k == "ground") gfx.ground = std::min(std::max((int)v, 0), 3);
        else if (k == "props") gfx.props = std::min(std::max((int)v, 0), 3);
        else if (k == "map") map = v >= 0 && v < MAP_COUNT ? (int)v : MAP_PROVING_GROUND;  // (a number no map has any more: the proving ground)
        else if (k == "recovery") realRecovery = v != 0;
        else if (k == "career") career = v != 0;
        else if (k == "fuel") fuelMode = std::min(std::max((int)v, 0), 2);
        else if (k == "pilotspeed") pilotSpeed = std::min(std::max(v, 5.0f), 100.0f);
        else if (k == "pilotroute") pilotRoute = std::min(std::max((int)v, 0), 2);
        else if (k == "timeofday") timeOfDay = std::min(std::max((int)v, 0), TOD_COUNT - 1);
        else if (k == "daylength") dayMinutes = std::min(std::max(v, 1.0f), 240.0f);
        else if (k == "clock") clockHours = std::fmod(std::max(v, 0.0f), 24.0f);
        else if (k == "padsteer") padSteering = std::min(std::max((int)v, 0), 2);
        else if (k == "paddead") padDeadzone = std::min(std::max((int)v, 0), 2);
        else if (k == "padrumble") padRumble = std::min(std::max((int)v, 0), 2);
        else if (k == "padinvertx") padInvertLookX = v != 0;
        else if (k == "padinverty") padInvertLookY = v != 0;
        else if (k == "crouchtoggle") crouchToggle = v != 0;
        else if (throttle.loadSetting(k, v)) {}
        else if (keys.loadSetting(k, std::atoi(text))) {}
    }
    std::fclose(f);
    keys.sanitize();
    return true;
}

bool Settings::save(const std::string& path) const {
    std::string dir = path.substr(0, path.find_last_of('/'));
    if (!dir.empty()) makeDir(dir);
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "mph %d\nvolume %.2f\nfov %.0f\nfullscreen %d\nvsync %d\nfps %d\nsteeringassist %d\n", mph, volume,
                 fovDeg, fullscreen, vsync, showFps, steeringAssist);
    std::fprintf(f, "shadows %d\nmsaa %d\nao %d\nbloom %d\nclouds %d\nscale %.2f\ngrassq %d\nmap %d\n", gfx.shadows,
                 gfx.msaa, gfx.ao, gfx.bloom, gfx.clouds, gfx.renderScale, gfx.grass, map);
    std::fprintf(f, "recovery %d\nmusic %.2f\nambience %.2f\nfuel %d\ncareer %d\n", realRecovery, music, ambience, fuelMode, career);
    std::fprintf(f, "ground %d\nprops %d\ndrawdistance %.0f\nsharpen %d\nfpstarget %d\n", gfx.ground, gfx.props, gfx.drawDistance, gfx.sharpen, fpsTarget);
    std::fprintf(f, "physicsthreads %d\nfullhud %d\n", physicsThreads, fullHud);
    std::fprintf(f, "adaptive %d\ndevhotkeys %d\ngfxchosen %d\n", adaptiveQuality, devHotkeys, gfxChosen);
    std::fprintf(f, "winchspeed %.2f\n", winchSpeed);
    if (!playerName.empty()) std::fprintf(f, "name %s\n", playerName.c_str());
    if (!joinAddress.empty()) std::fprintf(f, "join %s\n", joinAddress.c_str());
    if (!sessionName.empty()) std::fprintf(f, "session %s\n", sessionName.c_str());
    if (!rendezvous.empty()) std::fprintf(f, "rendezvous %s\n", rendezvous.c_str());
    for (const std::string& r : recentJoins) std::fprintf(f, "recent %s\n", r.c_str());
    if (!netPassword.empty()) std::fprintf(f, "netpassword %s\n", netPassword.c_str());
    if (rememberPassword) {  // (plain text, like the rest of this file; only when asked)
        std::fprintf(f, "rememberpw 1\n");
        if (!joinPassword.empty()) std::fprintf(f, "joinpassword %s\n", joinPassword.c_str());
    }
    std::fprintf(f, "maxplayers %d\nhostport %d\n", maxPlayers, hostPort >= net::kMinHostPort && hostPort <= net::kMaxPort ? hostPort : net::kDefaultPort);
    std::fprintf(f, "pilotdest %d\npilotspeed %.0f\npilotroute %d\n", pilotDest, pilotSpeed, pilotRoute);
    std::fprintf(f, "timeofday %d\ndaylength %.0f\nclock %.2f\n", timeOfDay, dayMinutes, clockHours);
    std::fprintf(f, "padsteer %d\npaddead %d\npadrumble %d\npadinvertx %d\npadinverty %d\n", padSteering, padDeadzone, padRumble, padInvertLookX, padInvertLookY);
    std::fprintf(f, "crouchtoggle %d\n", crouchToggle ? 1 : 0);
    std::fputs(throttle.saveText().c_str(), f);
    for (int a = 0; a < KA_COUNT; a++) std::fprintf(f, "%s %d\n", settingsName((KeyAction)a), keys.saveValue((KeyAction)a));

    std::fclose(f);
    return true;
}

// ---- Presets: perf.cpp ----

namespace {
const int kMsaa[4] = {0, 2, 4, 8};
int msaaIndex(int samples) {
    int best = 0;
    for (int k = 0; k < 4; k++)
        if (kMsaa[k] <= samples) best = k;
    return best;
}
}  // namespace

// ---- Rows ------------------------------------------------------------------------

namespace {
enum Id {
    // Settings page.
    UNITS = 1, VOLUME, FOV, RECOVERY, MUSIC, TIMEOFDAY, FUEL, AMBIENCE,
    ADAPTIVE = 9, QUALITY = 10, SHADOWS, AA, AO, BLOOM, CLOUDS, GRASS, SCALE, SCENERY, DETECT_ROW = 19,
    FULLSCREEN = 20, VSYNC, FPS, PHYSICSTHREADS, WINCHSPEED, FPSTARGET, FULLHUD,
    PADSTEER = 30, PADDEAD, PADRUMBLE, PADINVERTX, PADINVERTY, THROTTLECAP, THROTTLERESP, THROTTLEPAD, CROUCHMODE,
    // Main page and navigation.
    RESUME_ROW = 100, VEHICLE_ROW, MAP_ROW, GARAGE_ROW, SETTINGS_ROW, CONTROLS_ROW, RESET_ROW, QUIT_ROW, BACK_ROW,
    MULTI_ROW, NAME_ROW, HOST_ROW, ADDRESS_ROW, JOIN_ROW, LEAVE_ROW, FOUND_ROW0, FOUND_ROW5 = FOUND_ROW0 + 5,
    PILOT_ROW, DEST_ROW, PSPEED_ROW, PROUTE_ROW, PGO_ROW, PSTOP_ROW,
    FREEDRIVE_ROW, MODE_ROW, LEVELS_ROW, LEVEL_SHOW, LEVEL_PICK, LEVEL_PLAY, LEVEL_EDIT, EDITOR_ROW, SECTION_ROW, LAB_ROW, LRESTART_ROW, LQUIT_ROW, MAINMENU_ROW, EDITBACK_ROW,
    JOBS_ROW, JOB_PICK, JOB_GO, JOB_STOP_ROW, MEET_ROW, CONTROLLER_ROW,
    SESSION_ROW, HMAP_ROW, HMAX_ROW, CANCEL_ROW, PASSWORD_ROW, PORT_ROW, JPASS_ROW, SHOWPW_ROW, REMEMBER_ROW, RETRY_ROW,
    STEERING, KEYBINDS_ROW, KEYS_RESET, KEY_ROW0, KEY_ROWN = KEY_ROW0 + KA_COUNT,
    DEV_ROW, DEVHOT_ROW, DEVGROUP0, DEVGROUP_N = DEVGROUP0 + devmenu::G_COUNT, DEVITEM0, DEVITEM_N = DEVITEM0 + 40, DEVSEC_ROW, DEVPLACE_ROW,
};
const char* kOffOn[2] = {"OFF", "ON"};

std::string pct(float v) { return std::to_string((int)std::lround(v * 100)) + "%"; }

// TIME OF DAY row: 0..4 the clock running with these day lengths (real minutes), 5..8
// held at MORNING, NOON, EVENING, NIGHT.
const float kDayLengths[5] = {6, 12, 24, 48, 96};
int todIndex(const Settings& s) {
    if (s.timeOfDay > TOD_CYCLE && s.timeOfDay < TOD_COUNT) return 4 + s.timeOfDay;
    int best = 2;
    for (int k = 0; k < 5; k++)
        if (std::fabs(s.dayMinutes - kDayLengths[k]) < std::fabs(s.dayMinutes - kDayLengths[best])) best = k;
    return best;
}
}  // namespace

std::vector<PauseMenu::Row> PauseMenu::rows(const Settings& s) const {
    std::vector<Row> r;
    auto button = [&](int id, const char* label, const std::string& hint = "") { r.push_back({BUTTON, label, "", hint, id}); };
    auto header = [&](const char* label) { r.push_back({HEADER, label, "", "", 0}); };
    auto choice = [&](int id, const char* label, const std::string& value, int count, const std::string& hint = "") {
        r.push_back({CHOICE, label, value, hint, id, count});
    };
    auto slider = [&](int id, const char* label, const std::string& value, float frac, int steps) {
        Row row{SLIDER, label, value, "", id, steps};
        row.frac = frac;
        r.push_back(row);
    };
    const char* mapHint = mapPick == MAP_MOUNTAIN   ? "TRAILHEAD, SUMMIT TRAIL, CRAWL PARK, VALLEY LOOP"
                          : mapPick == MAP_FREEWAY ? "THREE LANES EACH WAY THAT NEVER END: TOP SPEED"
                          : mapPick == MAP_FRONTIER ? "DESERT, RIVERS, LAKES, A RANGE, A RACE CIRCUIT, A RALLY STAGE"
                          : mapPick == MAP_BACKCOUNTRY ? "TRAILS TO CRAWL, MUD, FORD AND EXPLORE WITH BUDDIES"
                                                    : "EVERYTHING IN THE GAME, LAID OUT TO LOOK AT AND TRY";
    if (page == 0 && title) {
        const auto& cat = vehicleCatalog();
        button(FREEDRIVE_ROW, "FREE DRIVE", "THIS MAP, THIS VEHICLE:");
        choice(MODE_ROW, "MODE", s.career ? "CAREER" : "SANDBOX", 2,
               s.career ? "START STOCK: FIND, WIN AND BUY BETTER PARTS" : "EVERY PART ON EVERY VEHICLE, ANY TIME");
        choice(VEHICLE_ROW, "VEHICLE", cat[vehicle].name, (int)listedVehicles().size(), cat[vehicle].blurb);
        choice(MAP_ROW, "MAP", mapName(mapPick), (int)listedMaps().size(), mapHint);
        button(LEVELS_ROW, "LEVELS", std::to_string(levels.size()) + " RACES, CLIMBS AND WORLDS");
        button(MULTI_ROW, "MULTIPLAYER", netActive ? netStatus : "OPEN TO FRIENDS, OR JOIN A GAME");
        button(EDITOR_ROW, "WORLD EDITOR", "BUILD YOUR OWN WORLDS AND LEVELS");
        button(SECTION_ROW, "NEW SECTION", "HAND-BUILD AN OBSTACLE SECTION (DEV)");
        button(LAB_ROW, "MODEL LAB", "PICK, TWEAK OR REDO THE GAME'S MODELS");
        button(GARAGE_ROW, "GARAGE", "ENGINE, SUSPENSION, GEARBOX");
        button(SETTINGS_ROW, "SETTINGS", "UNITS, GRAPHICS, SOUND");
        button(CONTROLS_ROW, "CONTROLS");
        button(QUIT_ROW, "QUIT GAME");
        return r;
    }
    if (page == 8) {  // the dev menu: the groups
        header("TEST AND DEBUG TOOLS");
        choice(DEVHOT_ROW, "DEV HOTKEYS", kOffOn[s.devHotkeys], 2,
               s.devHotkeys ? "ON: DEV KEYS (THE DEV KEYS ARE TOOLS). " + s.keys.name(KA_DEV_TOGGLE) + " TOGGLES" : "OFF: GAMEPLAY KEYS, THE TOOLS ONLY HERE. " + s.keys.name(KA_DEV_TOGGLE) + " TOGGLES");
        for (int g = 0; g < devmenu::G_COUNT; g++) button(DEVGROUP0 + g, devmenu::groupName((devmenu::Group)g), devmenu::groupHint((devmenu::Group)g));
        button(BACK_ROW, devFromPause ? "BACK" : "CLOSE");
        return r;
    }
    if (page == 9) {  // one group
        const std::vector<devmenu::Item> its = devItems(s.keys);
        header(devmenu::groupHint((devmenu::Group)devGroup));
        for (int k = 0; k < (int)its.size() && k < DEVITEM_N - DEVITEM0; k++) button(DEVITEM0 + k, its[k].label.c_str(), its[k].hint);
        if (devGroup == devmenu::G_TELEPORT) {
            if (!devSections.empty()) {
                const int n = (int)devSections.size();
                choice(DEVSEC_ROW, "SECTION", devSections[std::min(std::max(devSection, 0), n - 1)], n, "A D PICKS ONE, ENTER GOES THERE (AT ITS START PAD)");
            }
            if (!devPlaces.empty()) {
                const int n = (int)devPlaces.size();
                choice(DEVPLACE_ROW, "PLACE", devPlaces[std::min(std::max(devPlace, 0), n - 1)], n, "A D PICKS ONE, ENTER GOES THERE");
            }
        }
        button(BACK_ROW, "BACK");
        return r;
    }
    if (page == 6) {
        header(padName.empty() ? "NO CONTROLLER CONNECTED" : ("CONTROLLER: " + padName).c_str());
        choice(PADSTEER, "STEERING", pad::kSteeringNames[std::min(std::max(s.padSteering, 0), 2)], 3,
               "HOW QUICKLY THE STICK TURNS THE WHEELS: SOFT IS GENTLE NEAR THE CENTRE");
        choice(PADDEAD, "STICK DEADZONE", pad::kDeadzoneNames[std::min(std::max(s.padDeadzone, 0), 2)], 3,
               "RAISE IT IF THE CAR DRIFTS WITH YOUR HANDS OFF");
        choice(PADRUMBLE, "RUMBLE", pad::kRumbleNames[std::min(std::max(s.padRumble, 0), 2)], 3, "HITS, LANDINGS, BREAKS, REV LIMITER");
        choice(PADINVERTX, "INVERT LOOK X", kOffOn[s.padInvertLookX], 2, "FLIPS LEFT/RIGHT ON THE RIGHT STICK");
        choice(PADINVERTY, "INVERT LOOK Y", kOffOn[s.padInvertLookY], 2, "FLIPS UP/DOWN ON THE RIGHT STICK");
        header(("THROTTLE LIMIT: " + s.keys.name(KA_THROTTLE_CAP) + " CYCLES 100 50 25 10% WHILE DRIVING").c_str());
        slider(THROTTLECAP, "KEYBOARD THROTTLE", throttle::percent(s.throttle.cap), (s.throttle.cap - throttle::kMinCap) / (1 - throttle::kMinCap), 20);
        static const char* respHint[3] = {"W IS AT THE LIMIT AT ONCE", "W BUILDS UP OVER ABOUT 0.4 S", "W BUILDS UP OVER ABOUT 1.2 S"};
        const int resp = std::min(std::max(s.throttle.response, 0), 2);
        choice(THROTTLERESP, "THROTTLE RESPONSE", throttle::kResponseNames[resp], 3, respHint[resp]);
        choice(THROTTLEPAD, "APPLIES TO PAD", kOffOn[s.throttle.padToo], 2, "THE LIMIT ALSO SCALES THE TRIGGER (OFF: KEYBOARD ONLY)");
        choice(CROUCHMODE, "CROUCH ON FOOT", s.crouchToggle ? "TOGGLE" : "HOLD", 2, "HOLD THE KEY TO STAY LOW, OR PRESS IT TO CROUCH AND AGAIN TO STAND");
        button(KEYBINDS_ROW, "KEYBOARD KEYBINDS", "REMAP EVERY KEY");
        button(BACK_ROW, "BACK");
        return r;
    }
    if (page == 7) {  // the keyboard's remappable keys (the controller has its own fixed layout)
        header(listening >= 0 ? "PRESS THE NEW KEY (SHIFT+KEY FOR A COMBO). ESC CANCELS, BACKSPACE CLEARS" : keyNote.empty() ? "ENTER ON A ROW, THEN PRESS A KEY" : keyNote.c_str());
        for (int g = 0; g < KG_COUNT; g++) {
            header(g == KG_DEV ? "DEV: ONLY IN DEV KEYS MODE (THE FIRST THREE ALWAYS)" : groupLabel((KeyGroup)g));
            for (int a = 0; a < KA_COUNT; a++)
                if (actionGroup((KeyAction)a) == (KeyGroup)g)
                    button(KEY_ROW0 + a, actionLabel((KeyAction)a), listening == a ? "PRESS A KEY..." : s.keys.name((KeyAction)a));
        }
        button(KEYS_RESET, "RESET ALL TO DEFAULTS", s.keys.isDefault() ? "" : "THE GAME'S OWN KEYS");
        button(BACK_ROW, "BACK");
        return r;
    }
    if (page == 4) {
        const std::vector<int> vis = visibleLevels();
        header(levels.empty() ? "NO LEVELS" : vis.empty() ? "NO SECTIONS YET: MAIN MENU > NEW SECTION" : (std::to_string(vis.size()) + (levelSections ? " SECTIONS" : " LEVELS")).c_str());
        choice(LEVEL_SHOW, "SHOW", levelSections ? "SECTIONS" : "ALL", 2, "SECTIONS: THE OBSTACLE SECTIONS YOU BUILT");
        if (!vis.empty()) {
            const int k = shownLevel();
            choice(LEVEL_PICK, "LEVEL", levels[k].name, (int)vis.size(), levels[k].about);
            for (const std::string& line : levels[k].lines) r.push_back({HEADER, "", "", "", 0}), r.back().label = line, r.back().id = -1;
            button(LEVEL_PLAY, "PLAY", netClient ? "THE HOST PICKS THE LEVEL" : netActive ? "EVERYONE IN THE SESSION PLAYS IT" : "");
            if (!netClient) button(LEVEL_EDIT, "OPEN IN THE EDITOR", "CHANGE IT, OR START YOURS FROM IT");
        }
        button(BACK_ROW, "BACK");
        return r;
    }
    if (page == 5) {
        header(jobActive ? jobStatus.c_str() : "JOBS ON THIS MAP");
        if (!jobs.empty()) {
            const int k = std::min(std::max(jobPick, 0), (int)jobs.size() - 1);
            choice(JOB_PICK, "JOB", jobs[k].name, (int)jobs.size(), jobs[k].about);
            for (const std::string& line : jobs[k].lines) r.push_back({HEADER, "", "", "", 0}), r.back().label = line, r.back().id = -1;
            button(JOB_GO, jobActive ? "DO THIS ONE INSTEAD" : "START IT", netClient ? "THE HOST PICKS THE JOB" : netActive ? "EVERYONE IN THE SESSION" : "");
        }
        if (jobActive && !netClient) button(JOB_STOP_ROW, "GIVE UP THE JOB");
        button(BACK_ROW, "BACK");
        return r;
    }
    if (page == 0) {
        const auto& cat = vehicleCatalog();
        button(RESUME_ROW, "RESUME");
        if (editorSuspended) button(EDITBACK_ROW, "BACK TO THE EDITOR", s.keys.name(KA_PLAYTEST_BACK) + ": YOUR EDITS ARE KEPT");
        if (levelActive) {
            button(LRESTART_ROW, "RESTART LEVEL", "OR " + s.keys.name(KA_REPAIR) + " TWICE");
            button(LQUIT_ROW, "QUIT LEVEL", "BACK TO FREE DRIVE");
        }
        choice(VEHICLE_ROW, "VEHICLE", cat[vehicle].name, (int)listedVehicles().size(), cat[vehicle].blurb);
        choice(MAP_ROW, "MAP", mapName(mapPick), (int)listedMaps().size(), mapHint);
        button(LEVELS_ROW, "LEVELS", levelActive ? "PICK ANOTHER" : "RACES, CLIMBS AND WORLDS");
        button(JOBS_ROW, "JOBS", jobActive ? jobStatus : "RECOVERIES, CONVOYS, SCOUTING");
        button(GARAGE_ROW, "GARAGE", "ENGINE, SUSPENSION, GEARBOX");
        button(PILOT_ROW, "AUTOPILOT", pilotActive ? pilotStatus : "DRIVE ITSELF SOMEWHERE");
        button(MULTI_ROW, "MULTIPLAYER", netActive ? netStatus : "OPEN TO FRIENDS, OR JOIN A GAME");
        button(SETTINGS_ROW, "SETTINGS", "UNITS, GRAPHICS, SOUND");
        button(CONTROLS_ROW, "CONTROLS");
        button(DEV_ROW, "DEV MENU", s.keys.name(KA_DEV_MENU) + ": SPAWN, TELEPORT, DEBUG VIEWS");
        button(MAINMENU_ROW, "MAIN MENU");
        button(QUIT_ROW, "QUIT GAME");
        return r;
    }
    if (page == 3) {
        static const char* routes[3] = {"ROADS ONLY", "ROADS AND TRAILS", "ANYWHERE"};
        static const char* routeHints[3] = {"STAYS ON ROADS AND TRAILS", "PREFERS TRACKS, CUTS ACROSS WHEN IT PAYS",
                                            "STRAIGHTEST WAY IT CAN GET THROUGH: GRASS, ROCK, SCREE"};
        header(pilotStatus.c_str());
        const int n = std::max(1, (int)pilotPlaces.size());
        const int d = std::min(std::max(s.pilotDest, 0), n - 1);
        choice(DEST_ROW, "DRIVE TO", pilotPlaces.empty() ? "-" : pilotPlaces[d], n);
        slider(PSPEED_ROW, "SPEED", std::to_string((int)std::lround(s.pilotSpeed)) + " KM/H", (s.pilotSpeed - 5) / 95, 20);
        choice(PROUTE_ROW, "ROUTE", routes[std::min(std::max(s.pilotRoute, 0), 2)], 3, routeHints[std::min(std::max(s.pilotRoute, 0), 2)]);
        button(PGO_ROW, pilotActive ? "GO THERE INSTEAD" : "GO", s.keys.name(KA_AUTOPILOT) + " ALSO STARTS AND STOPS IT");
        if (pilotActive) button(PSTOP_ROW, "STOP AUTOPILOT", "OR JUST STEER");
        button(BACK_ROW, "BACK");
        return r;
    }
    if (page == 2) {
        // Two cards: HOST A GAME on the left, JOIN A GAME on the right (your name above, BACK
        // below); in a session, one card about it; joining, one card saying how it's going.
        auto inCol = [&](int c, size_t from) {
            for (size_t k = from; k < r.size(); k++) r[k].col = c;
        };
        auto info = [&](const std::string& line) { r.push_back({HEADER, line, "", "", -1}); };
        auto text = [&](int id, const char* label, const std::string& value, const std::string& hint, const std::string& ph = "") {
            r.push_back({TEXT, label, value, hint, id});
            r.back().placeholder = ph;
        };
        if (joining) {
            const size_t c1 = r.size();
            if (joinError.empty()) {
                info("CONNECTING TO " + joinLabel);
                button(CANCEL_ROW, "CANCEL");
            } else {
                info("COULDN'T JOIN " + joinLabel);
                info(joinError);
                if (joinNeedsPw) {
                    text(JPASS_ROW, "PASSWORD", showPw ? s.joinPassword : std::string(s.joinPassword.size(), '*'), "TYPE IT", "TYPE THE PASSWORD");
                    button(RETRY_ROW, "RETRY", "JOIN AGAIN WITH THAT PASSWORD");
                    button(CANCEL_ROW, "CANCEL");
                    inCol(1, c1);
                    return r;
                }
                info("IS THE GAME STILL HOSTED? ON ANOTHER NETWORK, THE HOST MAY NEED TO FORWARD PORT " +
                     std::to_string(joinPort > 0 ? joinPort : net::kDefaultPort) + " (TCP AND UDP)");
                button(CANCEL_ROW, "OK");
            }
            inCol(1, c1);
            return r;
        }
        if (netActive) {
            const size_t c1 = r.size();
            for (const std::string& line : sessionLines) info(line);
            if (netHost && !localIps.empty()) {
                std::string ips;
                for (const std::string& ip : localIps) ips += (ips.empty() ? "" : "  OR  ") + ip + (sessionPort > 0 && sessionPort != net::kDefaultPort ? ":" + std::to_string(sessionPort) : "");
                info("FRIENDS ON YOUR NETWORK SEE THIS GAME IN THEIR JOIN LIST");
                info("OR THEY TYPE YOUR ADDRESS: " + ips);
            }
            if (netHost && sessionPort > 0) info("OVER THE INTERNET, FORWARD PORT " + std::to_string(sessionPort) + " (TCP AND UDP)");
            button(MEET_ROW, "MEET UP", "YOUR TRUCK TO A BUDDY'S (AGAIN: THE NEXT ONE)");
            button(LEAVE_ROW, netHost ? "STOP HOSTING" : "LEAVE THE GAME", netHost ? "EVERYONE ELSE IS DROPPED" : "");
            inCol(1, c1);
            button(BACK_ROW, "BACK");
            return r;
        }
        text(NAME_ROW, "YOUR NAME", s.playerName, "TYPE IT", "PLAYER");
        text(PASSWORD_ROW, "HOST PASSWORD", std::string(s.netPassword.size(), '*'), "TYPE IT", "NONE");  // (to host with; joining has its own, on the JOIN card)
        {
            const size_t c1 = r.size();
            const std::string me = s.playerName.empty() ? std::string("PLAYER") : s.playerName;
            text(SESSION_ROW, "GAME NAME", s.sessionName, "TYPE IT", me + "'S GAME");
            choice(HMAP_ROW, "MAP", mapName(mapPick), (int)listedMaps().size());
            choice(HMAX_ROW, "PLAYERS", "UP TO " + std::to_string(std::min(std::max(s.maxPlayers, 2), 8)), 7);
            {
                const std::string shown = portEdited ? portText : std::to_string(s.hostPort);
                text(PORT_ROW, "PORT", shown, "DEFAULT " + std::to_string(net::kDefaultPort), "DEFAULT " + std::to_string(net::kDefaultPort));
            }
            button(HOST_ROW, "START HOSTING");
            if (portEdited && net::parsePort(portText, net::kMinHostPort) < 0) {
                info("PORT: " + std::to_string(net::kMinHostPort) + " TO " + std::to_string(net::kMaxPort));
                info("(DEFAULT " + std::to_string(net::kDefaultPort) + ")");
            } else {
                info("FRIENDS ON YOUR NETWORK WILL SEE IT");
                info("IN THEIR JOIN LIST");
            }
            inCol(1, c1);
        }
        {
            const size_t c2 = r.size();
            if (found.empty()) {
                info("SEARCHING YOUR NETWORK...");
                if (searching > 5) {
                    info("NOTHING YET. IS YOUR FRIEND HOSTING,");
                    info("ON THE SAME WI-FI OR NETWORK AS YOU?");
                    info("WINDOWS: ALLOW CRUMPLE ZONE THROUGH");
                    info("THE FIREWALL ON PRIVATE NETWORKS.");
                }
            }
            for (int k = 0; k < (int)found.size() && k < 6; k++) {
                const FoundGame& g = found[k];
                button(FOUND_ROW0 + k, g.session.c_str(), g.ok ? g.host + "  " + g.map + "  " + g.players : g.why);
                r.back().dim = !g.ok;
            }
            text(ADDRESS_ROW, "ADDRESS", s.joinAddress, "IP OR IP:PORT", "TYPE AN IP:PORT");
            text(JPASS_ROW, "PASSWORD", showPw ? s.joinPassword : std::string(s.joinPassword.size(), '*'), "IF THE GAME HAS ONE", "NONE");
            choice(SHOWPW_ROW, "SHOW PASSWORD", kOffOn[showPw], 2);
            choice(REMEMBER_ROW, "REMEMBER PASSWORD", kOffOn[s.rememberPassword], 2, s.rememberPassword ? "KEPT AS PLAIN TEXT IN SETTINGS.TXT" : "NOT SAVED");
            button(JOIN_ROW, "JOIN THAT ADDRESS");
            inCol(2, c2);
        }
        button(BACK_ROW, "BACK");
        return r;
    }
    static const char* shadows[4] = {"OFF", "LOW", "MEDIUM", "HIGH"};
    static const char* aa[4] = {"OFF", "2X MSAA", "4X MSAA", "8X MSAA"};
    header("GAME");
    choice(UNITS, "SPEED UNITS", s.mph ? "MPH" : "KM/H", 2);
    choice(RECOVERY, "RECOVERY", s.realRecovery ? "REAL" : "ARCADE", 2,
           s.realRecovery ? "DAMAGE STAYS: TOW OR DRIVE TO A REPAIR STATION" : s.keys.name(KA_FLIP) + " FIXES EVERYTHING WHERE YOU ARE");
    choice(STEERING, "STEERING ASSIST", kOffOn[s.steeringAssist], 2,
           s.steeringAssist ? "LIMITS TURNING AT HIGH SPEED (SAFE)" : "FULL LOCK AT ALL SPEEDS (SIMULATION)");
    {
        static const char* fuel[3] = {"OFF", "REAL", "FAST"};
        choice(FUEL, "FUEL", fuel[std::min(std::max(s.fuelMode, 0), 2)], 3);
    }
    {
        // One row (the page is full at 720p): the running clock at a few day lengths, then
        // the fixed times.
        const int t = todIndex(s);
        choice(TIMEOFDAY, "TIME OF DAY",
               t < 5 ? "CYCLE " + std::to_string((int)kDayLengths[t]) + " MIN" : std::string(timeOfDayName(t - 4)), 9);
    }
    slider(VOLUME, "VOLUME", pct(s.volume), s.volume, 21);
    slider(MUSIC, "MUSIC", s.music > 0 ? pct(s.music) : "OFF", s.music, 21);
    slider(AMBIENCE, "AMBIENCE", s.ambience > 0 ? pct(s.ambience) : "OFF", s.ambience, 21);
    slider(FOV, "FIELD OF VIEW", std::to_string((int)std::lround(s.fovDeg)) + " DEG", (s.fovDeg - 50) / 50, 26);
    char speed[32]; std::snprintf(speed, sizeof speed, "%.2gX", s.winchSpeed);
    choice(WINCHSPEED, "WINCH SPEED", speed, 11, "1X: RATED SPEED. 4X: DEFAULT. UP TO 20X. " + s.keys.name(KA_WINCH_SLOWER) + " AND " + s.keys.name(KA_WINCH_FASTER) + " CHANGE IT WHILE DRIVING.");
    header("GRAPHICS");
    choice(QUALITY, "QUALITY", presetName(presetOf(s.gfx)), 5, "POTATO: FOR THE WEAKEST LAPTOPS. HALF RESOLUTION, NO SHADOWS, 300 M OF VIEW");
    button(DETECT_ROW, "DETECT AUTOMATICALLY", "PICK QUALITY FOR THIS COMPUTER: TESTS A FEW SECONDS OF DRIVING");
    choice(ADAPTIVE, "ADAPTIVE QUALITY", kOffOn[s.adaptiveQuality], 2, "AUTO-SCALE GRAPHICS TO MAINTAIN FRAMERATE");
    choice(FPSTARGET, "FPS TARGET", s.fpsTarget > 0 ? std::to_string(s.fpsTarget) + " FPS" : s.fpsTarget < 0 ? std::string("MATCH DISPLAY") : std::string("UNCAPPED"), 7,
           "WHAT ADAPTIVE QUALITY AIMS FOR (NEVER ABOVE 144). WITH VSYNC OFF, ALSO THE FRAME CAP. MATCH DISPLAY: YOUR MONITOR'S REFRESH RATE");
    choice(SHADOWS, "SHADOWS", shadows[std::min(std::max(s.gfx.shadows, 0), 3)], 4);
    choice(AA, "ANTI-ALIASING", aa[msaaIndex(s.gfx.msaa)], 4);
    choice(AO, "AMBIENT OCCLUSION", kOffOn[s.gfx.ao], 2);
    choice(BLOOM, "BLOOM", kOffOn[s.gfx.bloom], 2);
    choice(CLOUDS, "CLOUDS", kOffOn[s.gfx.clouds], 2);
    {
        static const char* grass[4] = {"OFF", "LOW", "MEDIUM", "HIGH"};
        choice(GRASS, "GRASS", grass[std::min(std::max(s.gfx.grass, 0), 3)], 4, "HOW DENSE AND FAR THE GRASS GROWS");
    }
    {
        // Photo-scanned ground materials and props together (OFF: the code-made look).
        static const char* scenery[5] = {"OFF", "LOW", "HIGH", "ULTRA", "CUSTOM"};
        const int sc = s.gfx.ground == s.gfx.props ? std::min(std::max(s.gfx.ground, 0), 3) : 4;
        choice(SCENERY, "SCANNED SCENERY", scenery[sc], 4);
    }
    slider(SCALE, "RENDER SCALE", pct(s.gfx.renderScale), (s.gfx.renderScale - 0.5f) / 1.5f, 16);
    header("DISPLAY");
    choice(FULLSCREEN, "FULLSCREEN", kOffOn[s.fullscreen], 2);
    choice(VSYNC, "VSYNC", kOffOn[s.vsync], 2);
    choice(FPS, "SHOW FPS", kOffOn[s.showFps], 2);
    choice(FULLHUD, "SHOW FULL HUD", s.fullHud ? "ALWAYS" : "CONTEXTUAL", 2, "CONTEXTUAL: FUEL, TERRAIN, DAMAGE AND 4X4 SHOW WHEN THEY MATTER. HOLD THE FULL HUD KEY (CONTROLS) TO SEE ALL.");
    {
        const int available = physicsThreadLimit();
        const int chosen = std::clamp(s.physicsThreads, 0, available);
        const std::string value = chosen == 0 ? "AUTO (" + std::to_string(resolvePhysicsThreads(0)) + ")" :
            std::to_string(chosen) + (chosen == available ? " (ALL)" : "");
        choice(PHYSICSTHREADS, "PHYSICS THREADS", value, available + 1,
               "1: SINGLE THREAD. ALL: USE EVERY CPU THREAD. MORE MAY NOT BE FASTER.");
    }
    choice(DEVHOT_ROW, "DEV HOTKEYS", kOffOn[s.devHotkeys], 2, "TEST KEYS (SPAWNS, BEAM VIEW, EDITOR...). " + s.keys.name(KA_DEV_TOGGLE) + " FLIPS IT. " + s.keys.name(KA_DEV_MENU) + ": THE DEV MENU");
    button(CONTROLLER_ROW, "CONTROLS", "THROTTLE LIMIT, CONTROLLER, KEYBINDS");
    button(BACK_ROW, "BACK");
    return r;
}

std::vector<devmenu::Item> PauseMenu::devItems(const Keybinds& kb) const {
    std::vector<devmenu::Item> r;
    for (const devmenu::Item& it : devmenu::items((devmenu::Group)devGroup, &kb))
        if (it.action.act != devmenu::SCENARIO_CHECKS || devScenario) r.push_back(it);
    return r;
}

void PauseMenu::showDev(int currentVehicle, int currentMap, bool fromPause) {
    show(currentVehicle, currentMap);
    page = 8;
    devFromPause = fromPause;
    sel = 1;  // (row 0 is the header)
}

void PauseMenu::showMultiplayer(int currentVehicle, int currentMap) {
    show(currentVehicle, currentMap);
    page = 2;
    sel = 1;  // (row 0 is the status header)
}

void PauseMenu::showPage(int pg, int currentVehicle, int currentMap) {
    show(currentVehicle, currentMap);
    page = pg;
    sel = 1;
}

void PauseMenu::showTitle(int currentVehicle, int currentMap) {
    show(currentVehicle, currentMap);
    title = true;
}

void PauseMenu::showTitlePage(int pg, int currentVehicle, int currentMap) {
    show(currentVehicle, currentMap);
    title = true;
    page = pg;
    sel = 1;
    if (pg == 2) sel = 1;  // (row 0 is the status header)
}

void PauseMenu::show(int currentVehicle, int currentMap) {
    mapPick = mapListed(currentMap) ? currentMap : nextListedMap(currentMap, 0);
    open = true;
    title = false;
    page = 0;
    listening = -1;
    keyNote.clear();
    portEdited = portFresh = false;
    sel = 0;
    fade = 0;
    dragSlider = -1;
    hits.clear();
    vehicle = std::max(0, currentVehicle);
}

void PauseMenu::moveSel(int dir, const Settings& s) {
    std::vector<Row> r = rows(s);
    const int n = (int)r.size();
    if (page == 2 && n > 0) {
        // Up and down stay in the card you're in (the rows across the top and bottom are in
        // every card's way).
        const int c = sel >= 0 && sel < n ? r[sel].col : 0;
        std::vector<int> order;
        for (int pass = 0; pass < 3; pass++)
            for (int k = 0; k < n; k++) {
                if (r[k].kind == HEADER) continue;
                const bool top = r[k].col == 0 && (k < n / 2), bottom = r[k].col == 0 && !top;
                if ((pass == 0 && top) || (pass == 1 && r[k].col != 0 && (r[k].col == c || c == 0)) || (pass == 2 && bottom)) order.push_back(k);
            }
        // (From the top or bottom row, down/up goes into the HOST card first.)
        if (c == 0) {
            std::vector<int> o2;
            for (int k : order)
                if (r[k].col == 0 || r[k].col == 1) o2.push_back(k);
            if (!o2.empty()) order = o2;
        }
        if (order.empty()) return;
        int at = 0;
        for (int k = 0; k < (int)order.size(); k++)
            if (order[k] == sel) at = k;
        sel = order[(at + dir + (int)order.size()) % (int)order.size()];
        return;
    }
    for (int k = 0; k < n; k++) {
        sel = (sel + dir + n) % n;
        if (r[sel].kind != HEADER) return;
    }
}

PauseMenu::Result PauseMenu::activate(int row, Settings& s) {
    std::vector<Row> r = rows(s);
    if (row < 0 || row >= (int)r.size()) return {};
    const Row& it = r[row];
    auto goPage = [&](int p, int id) {
        page = p;
        portEdited = portFresh = false;
        hits.clear();
        dragSlider = -1;
        std::vector<Row> nr = rows(s);
        sel = 0;
        for (int k = 0; k < (int)nr.size(); k++)
            if (nr[k].id == id) sel = k;
        if (nr[sel].kind == HEADER) moveSel(1, s);
    };
    switch (it.id) {
        case RESUME_ROW: return {RESUME};
        case VEHICLE_ROW:
            if (title) return change(row, 1, s);
            return {DRIVE, vehicle};
        case MAP_ROW:
            if (title) return change(row, 1, s);
            return {LOAD_MAP, mapPick};
        case FREEDRIVE_ROW: return {FREE_DRIVE, vehicle};
        case LEVELS_ROW: goPage(4, LEVEL_PICK); return {};
        case JOBS_ROW: goPage(5, JOB_PICK); return {};
        case JOB_PICK:
        case JOB_GO: return netClient ? Result{} : Result{JOB_START, jobPick};
        case JOB_STOP_ROW: return {JOB_STOP};
        case LEVEL_SHOW: levelSections = !levelSections, levelPick = std::max(0, shownLevel()); return {};
        case LEVEL_PICK:
        case LEVEL_PLAY: return netClient || shownLevel() < 0 ? Result{} : Result{PLAY_LEVEL, shownLevel()};
        case LEVEL_EDIT: return shownLevel() < 0 ? Result{} : Result{EDIT_LEVEL, shownLevel()};
        case EDITOR_ROW: return {EDITOR};
        case SECTION_ROW: return {NEW_SECTION};
        case LAB_ROW: return {MODEL_LAB};
        case LRESTART_ROW: return {LEVEL_RESTART};
        case LQUIT_ROW: return {LEVEL_QUIT};
        case MAINMENU_ROW: return {MAIN_MENU};
        case EDITBACK_ROW: return {EDITOR_BACK};
        case GARAGE_ROW: return {GARAGE};
        case SETTINGS_ROW: goPage(1, UNITS); return {};
        case CONTROLLER_ROW: goPage(6, PADSTEER); return {};
        case MULTI_ROW: goPage(2, joining ? CANCEL_ROW : netActive ? LEAVE_ROW : !found.empty() ? FOUND_ROW0 : HOST_ROW); return {};
        case PILOT_ROW: goPage(3, DEST_ROW); return {};
        case PGO_ROW:
        case DEST_ROW: return {PILOT_GO};
        case PSTOP_ROW: return {PILOT_STOP};
        case CONTROLS_ROW: return {CONTROLS};
        case DEV_ROW: devFromPause = true; goPage(8, DEVGROUP0); return {};
        case DEVHOT_ROW: return change(row, 1, s);
        case DEVSEC_ROW: return {DEV_RUN, devmenu::TELEPORT_SECTION, std::min(std::max(devSection, 0), std::max(0, it.count - 1))};
        case DEVPLACE_ROW: return {DEV_RUN, devmenu::TELEPORT_PLACE, std::min(std::max(devPlace, 0), std::max(0, it.count - 1))};
        case KEYBINDS_ROW: listening = -1, keyNote.clear(); goPage(7, KEY_ROW0); return {};
        case DETECT_ROW: return {DETECT_GFX};
        case KEYS_RESET: s.keys.resetDefaults(); listening = -1, keyNote.clear(); return {CHANGED};
        case RESET_ROW: return {RESET};
        case QUIT_ROW: return {QUIT};
        case BACK_ROW:
            if (page == 9) {
                goPage(8, DEVGROUP0 + devGroup);
                return {};
            }
            if (page == 8) {
                if (!devFromPause) return {RESUME};
                goPage(0, DEV_ROW);
                return {};
            }
            if (page == 6) {
                goPage(1, CONTROLLER_ROW);
                return {};
            }
            if (page == 7) {
                listening = -1, keyNote.clear();
                goPage(6, KEYBINDS_ROW);
                return {};
            }
            goPage(0, page == 2 ? MULTI_ROW : page == 3 ? PILOT_ROW : page == 4 ? LEVELS_ROW : page == 5 ? JOBS_ROW : SETTINGS_ROW); return {};
        case HOST_ROW:
            if (portEdited && net::parsePort(portText, net::kMinHostPort) < 0) {  // (a half-typed port: back to it)
                for (int k = 0; k < (int)r.size(); k++)
                    if (r[k].id == PORT_ROW) sel = k;
                return {};
            }
            portEdited = portFresh = false;
            return {HOST};
        case KEY_ROW0 ... KEY_ROWN - 1: listening = it.id - KEY_ROW0, keyNote.clear(); return {};
        case JOIN_ROW:
        case ADDRESS_ROW:
            if (s.joinAddress.empty()) return {};
            joinPick.clear();
            joinLabel = s.joinAddress;
            return {JOIN};
        case LEAVE_ROW: return {LEAVE};
        case MEET_ROW: return {MEET_UP};
        case CANCEL_ROW: return {CANCEL_JOIN};
        case RETRY_ROW:
            joinPick = joinAddr;
            return {JOIN};
        case JPASS_ROW:
            if (joining) {
                joinPick = joinAddr;
                return {JOIN};
            }
            moveSel(1, s);
            return {};
        case NAME_ROW:
        case PASSWORD_ROW:
        case PORT_ROW:
        case SESSION_ROW: moveSel(1, s); return {};
    }
    if (it.id >= DEVGROUP0 && it.id < DEVGROUP_N) {
        devGroup = it.id - DEVGROUP0;
        goPage(9, DEVITEM0);
        return {};
    }
    if (it.id >= DEVITEM0 && it.id < DEVITEM_N) {
        const std::vector<devmenu::Item> its = devItems(s.keys);
        const int k = it.id - DEVITEM0;
        if (k < (int)its.size()) return {DEV_RUN, (int)its[k].action.act, its[k].action.arg};
        return {};
    }
    if (it.id >= FOUND_ROW0 && it.id <= FOUND_ROW5 && it.id - FOUND_ROW0 < (int)found.size()) {  // a game found on the network
        const FoundGame& g = found[it.id - FOUND_ROW0];
        if (!g.ok) return {};
        joinPick = g.address;
        joinLabel = g.session;
        return {JOIN};
    }
    if (it.kind == CHOICE) return change(row, 1, s);
    return {};
}

std::vector<int> PauseMenu::visibleLevels() const {
    std::vector<int> vis;
    for (int k = 0; k < (int)levels.size(); k++)
        if (!levelSections || levels[k].section) vis.push_back(k);
    return vis;
}

int PauseMenu::shownLevel() const {
    const std::vector<int> vis = visibleLevels();
    if (vis.empty()) return -1;
    return std::find(vis.begin(), vis.end(), levelPick) != vis.end() ? levelPick : vis[0];
}

PauseMenu::Result PauseMenu::change(int row, int dir, Settings& s) {
    std::vector<Row> r = rows(s);
    if (row < 0 || row >= (int)r.size()) return {};
    const Row& it = r[row];
    if (it.id >= QUALITY && it.id <= SCENERY) s.gfxChosen = true;  // (touched the graphics: no first-launch detection over it)
    if (it.kind == SLIDER) return setSlider(row, it.frac + (float)dir / (it.count - 1), s);
    if (it.kind != CHOICE) return {};
    auto cycle = [&](int cur) { return (cur + dir + it.count) % it.count; };
    RenderSettings& g = s.gfx;
    switch (it.id) {
        case VEHICLE_ROW: vehicle = nextListedVehicle(vehicle, dir); return {};
        case MAP_ROW:
        case HMAP_ROW: mapPick = nextListedMap(mapPick, dir); return {};
        case SHOWPW_ROW: showPw = !showPw; return {};
        case REMEMBER_ROW: s.rememberPassword = !s.rememberPassword; return {CHANGED};
        case HMAX_ROW: s.maxPlayers = 2 + cycle(std::min(std::max(s.maxPlayers, 2), 8) - 2); break;
        case LEVEL_SHOW: levelSections = !levelSections, levelPick = std::max(0, shownLevel()); return {};
        case LEVEL_PICK: {  // (among the ones showing)
            const std::vector<int> vis = visibleLevels();
            if (vis.empty()) return {};
            const int at = (int)(std::find(vis.begin(), vis.end(), shownLevel()) - vis.begin());
            levelPick = vis[(at + dir + (int)vis.size()) % (int)vis.size()];
            return {};
        }
        case JOB_PICK: jobPick = cycle(std::min(std::max(jobPick, 0), it.count - 1)); return {};
        case UNITS: s.mph = !s.mph; break;
        case RECOVERY: s.realRecovery = !s.realRecovery; break;
        case MODE_ROW: s.career = !s.career; break;
        case FUEL: s.fuelMode = cycle(std::min(std::max(s.fuelMode, 0), 2)); break;
        case STEERING: s.steeringAssist = !s.steeringAssist; break;
        case TIMEOFDAY: {
            const int t = cycle(todIndex(s));
            s.timeOfDay = t < 5 ? (int)TOD_CYCLE : t - 4;
            if (t < 5) s.dayMinutes = kDayLengths[t];
            break;
        }
        case DEST_ROW: s.pilotDest = cycle(std::min(std::max(s.pilotDest, 0), it.count - 1)); break;
        case PROUTE_ROW: s.pilotRoute = cycle(std::min(std::max(s.pilotRoute, 0), 2)); break;
        case QUALITY: {
            static const int kOrder[5] = {Q_POTATO, Q_LOW, Q_MEDIUM, Q_HIGH, Q_ULTRA};  // (as the row steps through them)
            const int cur = presetOf(g);
            int at = dir > 0 ? 4 : 1;  // (CUSTOM: up to ULTRA, down to LOW)
            if (cur != Q_CUSTOM)
                for (int k = 0; k < 5; k++)
                    if (kOrder[k] == cur) at = (k + (dir > 0 ? 1 : 4)) % 5;
            applyPreset(g, kOrder[at]);
            break;
        }
        case FPSTARGET: {
            int at = 0;
            for (int k = 0; k < 7; k++)
                if (kFpsTargets[k] == s.fpsTarget) at = k;
            s.fpsTarget = kFpsTargets[cycle(at)];
            break;
        }
        case ADAPTIVE: s.adaptiveQuality = !s.adaptiveQuality; break;
        case DEVHOT_ROW: s.devHotkeys = !s.devHotkeys; break;
        case DEVSEC_ROW: devSection = cycle(std::min(std::max(devSection, 0), it.count - 1)); return {};
        case DEVPLACE_ROW: devPlace = cycle(std::min(std::max(devPlace, 0), it.count - 1)); return {};
        case SHADOWS: g.shadows = cycle(std::min(std::max(g.shadows, 0), 3)); break;
        case AA: g.msaa = kMsaa[cycle(msaaIndex(g.msaa))]; break;
        case AO: g.ao = !g.ao; break;
        case BLOOM: g.bloom = !g.bloom; break;
        case CLOUDS: g.clouds = !g.clouds; break;
        case GRASS: g.grass = cycle(std::min(std::max(g.grass, 0), 3)); break;
        case SCENERY: g.ground = g.props = cycle(g.ground == g.props ? std::min(std::max(g.ground, 0), 3) : 2); break;
        case FULLSCREEN: s.fullscreen = !s.fullscreen; break;
        case VSYNC: s.vsync = !s.vsync; break;
        case FPS: s.showFps = !s.showFps; break;
        case FULLHUD: s.fullHud = !s.fullHud; break;
        case PHYSICSTHREADS: s.physicsThreads = cycle(std::clamp(s.physicsThreads, 0, it.count - 1)); break;
        case WINCHSPEED: s.winchSpeed = recovery::adjustWinchSpeed(s.winchSpeed, dir); break;
        case PADSTEER: s.padSteering = cycle(std::min(std::max(s.padSteering, 0), 2)); break;
        case PADDEAD: s.padDeadzone = cycle(std::min(std::max(s.padDeadzone, 0), 2)); break;
        case PADRUMBLE: s.padRumble = cycle(std::min(std::max(s.padRumble, 0), 2)); break;
        case PADINVERTX: s.padInvertLookX = !s.padInvertLookX; break;
        case PADINVERTY: s.padInvertLookY = !s.padInvertLookY; break;
        case THROTTLERESP: s.throttle.response = cycle(std::min(std::max(s.throttle.response, 0), 2)); break;
        case THROTTLEPAD: s.throttle.padToo = !s.throttle.padToo; break;
        case CROUCHMODE: s.crouchToggle = !s.crouchToggle; break;
        default: return {};
    }
    return {CHANGED};
}

PauseMenu::Result PauseMenu::setSlider(int row, float frac, Settings& s) {
    std::vector<Row> r = rows(s);
    if (row < 0 || row >= (int)r.size() || r[row].kind != SLIDER) return {};
    const int steps = r[row].count;
    frac = std::round(std::min(std::max(frac, 0.0f), 1.0f) * (steps - 1)) / (steps - 1);
    switch (r[row].id) {
        case VOLUME: s.volume = frac; break;
        case MUSIC: s.music = frac; break;
        case AMBIENCE: s.ambience = frac; break;
        case FOV: s.fovDeg = 50 + 50 * frac; break;
        case SCALE: s.gfx.renderScale = 0.5f + 1.5f * frac; break;
        case THROTTLECAP: s.throttle.cap = throttle::kMinCap + (1 - throttle::kMinCap) * frac; break;
        case PSPEED_ROW: s.pilotSpeed = 5 + 95 * frac; break;
        default: return {};
    }
    return {CHANGED};
}

void PauseMenu::askPassword(const Settings& s) {
    joinNeedsPw = true;
    const std::vector<Row> r = rows(s);
    for (int k = 0; k < (int)r.size(); k++)
        if (r[k].id == JPASS_ROW) sel = k;
}

bool PauseMenu::textField(int id, Settings& s, std::string*& v, te::Rules& r) {
    switch (id) {
        case NAME_ROW: v = &s.playerName, r = {te::Kind::Name, 16}; return true;
        case SESSION_ROW: v = &s.sessionName, r = {te::Kind::Session, 24}; return true;
        case PASSWORD_ROW: v = &s.netPassword, r = {te::Kind::Password, 32}; return true;
        case JPASS_ROW: v = &s.joinPassword, r = {te::Kind::Password, 64}; return true;
        case ADDRESS_ROW: v = &s.joinAddress, r = {te::Kind::Address, 60}; return true;
        case PORT_ROW: v = &portText, r = {te::Kind::Port, 5}; return true;
    }
    return false;
}

void PauseMenu::focusText(int id, Settings& s) {
    if (tedRow != id) ted.reset(), tedRow = id;
    if (id == PORT_ROW && !portEdited) {
        portText = std::to_string(s.hostPort), portEdited = portFresh = true;
        te::selectAll(portText, ted);  // (what you type or paste goes over the number shown)
    }
}

// Typed or pasted text into the field that has focus (any case and symbol that field takes).
PauseMenu::Result PauseMenu::textInput(const char* utf8, Settings& s) {
    if (!open || !utf8 || ctrl) return {};
    const std::vector<Row> r = rows(s);
    std::string* v;
    te::Rules rl;
    if (sel < 0 || sel >= (int)r.size() || r[sel].kind != TEXT || !textField(r[sel].id, s, v, rl)) return {};
    focusText(r[sel].id, s);
    const bool any = te::insert(*v, ted, utf8, rl);
    if (any && r[sel].id == PORT_ROW) {
        const int p = net::parsePort(portText, net::kMinHostPort);
        if (p > 0) s.hostPort = p;
    }
    return {any ? CHANGED : NONE};
}

PauseMenu::Result PauseMenu::key(int sc, Settings& s) {
    if (!open) return {};
    // The multiplayer page: left and right go between the HOST and JOIN cards (except on a
    // choice, where they change it).
    auto switchCard = [&](int dir) {
        std::vector<Row> r = rows(s);
        if (sel < 0 || sel >= (int)r.size() || r[sel].kind == CHOICE) return false;
        const int c = r[sel].col;
        const int want = c == 1 && dir > 0 ? 2 : c == 2 && dir < 0 ? 1 : 0;
        if (!want) return false;
        int mine = 0;  // how far down its card the selected row is
        for (int k = 0; k < sel; k++) mine += r[k].col == c && r[k].kind != HEADER;
        int best = -1, idx = 0;
        for (int k = 0; k < (int)r.size(); k++) {
            if (r[k].col != want || r[k].kind == HEADER) continue;
            if (best < 0 || idx <= mine) best = k;
            idx++;
        }
        if (best >= 0) sel = best;
        return best >= 0;
    };
    if (page == 2 && joining && (sc == SC_ESCAPE || (sc == SC_BACKSPACE && !joinNeedsPw))) return {CANCEL_JOIN};  // (the password card edits with Backspace)
    if (page == 7 && listening >= 0) {  // the keybinds page waiting for a key: this press is the binding
        const KeyAction a = (KeyAction)listening;
        if (sc == SC_ESCAPE && a != KA_MENU) {  // (Esc is never a binding: it cancels. The MENU row can be put back to Esc with Backspace)
            listening = -1;
            keyNote.clear();
            return {};
        }
        if (sc == SC_ESCAPE || sc == SC_BACKSPACE) {  // (MENU: back to Esc alone; the rest: Backspace clears the binding)
            if (a == KA_MENU) s.keys.bind(a, SC_ESCAPE);
            else s.keys.unbind(a);
            listening = -1;
            keyNote.clear();
            return {CHANGED};
        }
        if (sc >= 224 && sc < 232 && !(sc == SC_LCTRL && a == KA_CROUCH)) return {};  // (a modifier on its own: wait for the key it goes with)
        if (!bindable(sc)) {
            keyNote = "THAT KEY CAN'T BE USED";
            return {};
        }
        if (!s.keys.bind(a, sc, shift)) {
            keyNote = "KEY IN USE: PICK A FREE KEY FOR THE MENU";
            return {};
        }
        listening = -1;
        keyNote.clear();
        return {CHANGED};
    }
    // A text field has focus: the keys edit it (typed characters arrive as text, see textInput).
    {
        std::vector<Row> r = rows(s);
        std::string* field;
        te::Rules rl;
        if (sel >= 0 && sel < (int)r.size() && r[sel].kind == TEXT && textField(r[sel].id, s, field, rl)) {
            const int id = r[sel].id;
            focusText(id, s);
            const bool atEdge = !ted.hasSel() && ((sc == SC_LEFT && te::cursorOf(*field, ted) == 0) || (sc == SC_RIGHT && te::cursorOf(*field, ted) >= field->size()));
            if (atEdge && !shift && !ctrl && switchCard(sc == SC_LEFT ? -1 : 1)) return {};  // (the HOST and JOIN cards: left and right at the end of the text)
            const bool canCopy = id != PASSWORD_ROW && (id != JPASS_ROW || showPw);
            const te::KeyResult kr = te::key(*field, ted, sc, ctrl, shift, rl, canCopy);
            if (kr.handled) {
                if (kr.changed && id == PORT_ROW) {  // (whatever the port field holds becomes the setting once it's a good port)
                    portFresh = false;
                    const int p = net::parsePort(portText, net::kMinHostPort);
                    if (p > 0) s.hostPort = p;
                }
                return {kr.changed ? CHANGED : NONE};
            }
            if (sc == SC_UP) moveSel(-1, s);
            else if (sc == SC_DOWN) moveSel(1, s);
            else if (sc == SC_RETURN || sc == SC_KP_ENTER) return activate(sel, s);
            if (sc != SC_ESCAPE) return {};
        }
    }
    if (sc == SC_ESCAPE) {
        if (page != 0) return activate([&] {
                std::vector<Row> r = rows(s);
                for (int k = 0; k < (int)r.size(); k++)
                    if (r[k].id == BACK_ROW) return k;
                return 0;
            }(), s);
        return title ? Result{} : Result{RESUME};  // (the title: nothing to go back to)
    }
    if (sc == SC_W || sc == SC_UP) moveSel(-1, s);
    else if (sc == SC_S || sc == SC_DOWN) moveSel(1, s);
    else if ((sc == SC_A || sc == SC_LEFT) && page == 2 && switchCard(-1)) return {};
    else if ((sc == SC_D || sc == SC_RIGHT) && page == 2 && switchCard(1)) return {};
    else if (sc == SC_A || sc == SC_LEFT) return change(sel, -1, s);
    else if (sc == SC_D || sc == SC_RIGHT) return change(sel, 1, s);
    else if (sc == SC_RETURN || sc == SC_KP_ENTER || sc == SC_SPACE) return activate(sel, s);
    return {};
}

int PauseMenu::hitAt(float x, float y) const {
    for (const Hit& h : hits)
        if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) return h.row;
    return -1;
}

PauseMenu::Result PauseMenu::mouseMove(float x, float y, bool held, Settings& s) {
    if (!open) return {};
    if (dragSlider >= 0 && held) {
        for (const Hit& h : hits)
            if (h.row == dragSlider) return setSlider(dragSlider, (x - h.barX) / std::max(1.0f, h.barW), s);
        return {};
    }
    int row = hitAt(x, y);
    if (row >= 0) sel = row;
    return {};
}

PauseMenu::Result PauseMenu::mouseDown(float x, float y, Settings& s) {
    if (!open) return {};
    int row = hitAt(x, y);
    if (row < 0) return {};
    sel = row;
    std::vector<Row> r = rows(s);
    const Hit* h = nullptr;
    for (const Hit& hh : hits)
        if (hh.row == row) h = &hh;
    if (!h) return {};
    if (r[row].kind == SLIDER) {
        if (x < h->barX - 4) return {};
        dragSlider = row;
        return setSlider(row, (x - h->barX) / std::max(1.0f, h->barW), s);
    }
    if (r[row].kind == CHOICE) {
        // The value box: "<" on its left third steps back, ">" on its right third forward.
        const float third = h->barW / 3;
        if (x >= h->barX && x < h->barX + third) return change(row, -1, s);
        if (x >= h->barX + 2 * third && x < h->barX + h->barW) return change(row, 1, s);
    }
    return activate(row, s);
}

// ---- Drawing ---------------------------------------------------------------------

// One row in a box from x to x + w, its top at cy: returns its height (and remembers where
// it went, for the mouse).
float PauseMenu::drawRow(Renderer& r, const Row& row, int k, float x, float w, float cy, const RowStyle& st) {
    const float ts = st.ts, small = st.small, u = st.u, rowH = st.rowH, headH = st.headH, a = st.a, pad = 22 * u;
    if (row.kind == HEADER) {
        if (row.id == -1)  // an information line
            r.text(x + pad + 6 * u, cy + headH - 8 * small - 6 * u, small, row.label, 0.82f, 0.84f, 0.88f, 0.95f * a);
        else
            r.text(x + pad, cy + headH - 8 * small - 6 * u, small, row.label, 1.0f, 0.75f, 0.3f, 0.85f * a);
        return headH;
    }
    const float valueW = st.valueW, valueX = x + w - pad - valueW;
    const bool hasHint = row.kind == CHOICE && !row.hint.empty();
    const float hh = rowH + (hasHint ? 8 * small + 6 * u : 0);
    const bool on = k == sel;
    if (on) {
        r.rect(x + pad * 0.5f, cy, w - pad, hh, 1, 1, 1, 0.07f * a);
        r.rect(x + pad * 0.5f, cy, 3 * u, hh, 1.0f, 0.75f, 0.3f, a);
    }
    const float ty = cy + (rowH - 8 * ts) / 2;
    const float dimK = row.dim ? 0.45f : 1.0f;
    const float lr = (on ? 1.0f : 0.86f) * dimK, lg = (on ? 1.0f : 0.87f) * dimK, lb = (on ? 1.0f : 0.9f) * dimK;
    {
        std::string lab = row.label;  // (a long one, a game's name: cut to fit beside its value)
        const float room = (row.kind == BUTTON ? w - 2 * pad - 12 * u - (row.hint.empty() ? 0 : r.textWidth(row.hint, small) + 12 * u)
                                                : valueX - x - pad - 18 * u);
        while (lab.size() > 3 && r.textWidth(lab, ts) > room) lab.pop_back();
        r.text(x + pad + 6 * u, ty, ts, lab, lr, lg, lb, a);
    }
    Hit hit{x + pad * 0.5f, cy, w - pad, hh, valueX, valueW, k};
    if (row.kind == BUTTON && !row.hint.empty())
        r.text(x + w - pad - r.textWidth(row.hint, small), cy + (rowH - 8 * small) / 2, small, row.hint, 0.55f * dimK + (row.dim ? 0.3f : 0), 0.57f * dimK,
               0.62f * dimK, a);
    if (row.kind == CHOICE) {
        r.rect(valueX, cy + 5 * u, valueW, rowH - 10 * u, 0, 0, 0, 0.35f * a);
        const float ar = on ? 1.0f : 0.6f, ag = on ? 0.75f : 0.6f, ab = on ? 0.3f : 0.62f;
        r.text(valueX + 8 * u, ty, ts, "<", ar, ag, ab, a);
        r.text(valueX + valueW - 8 * u - r.textWidth(">", ts), ty, ts, ">", ar, ag, ab, a);
        {  // (a long value: smaller, to fit between the arrows)
            const float vs = r.textWidth(row.value, ts) > valueW - 44 * u ? small : ts;
            std::string v = row.value;
            while (!v.empty() && r.textWidth(v, vs) > valueW - 44 * u) v.pop_back();
            r.text(valueX + (valueW - r.textWidth(v, vs)) / 2, cy + (rowH - 8 * vs) / 2, vs, v, 1, 1, 1, a);
        }
        if (hasHint) {
            std::string hint = row.hint;
            const float maxW = w - 2 * pad - 12 * u;
            while (!hint.empty() && r.textWidth(hint, small) > maxW) hint.pop_back();
            r.text(x + pad + 6 * u, cy + rowH, small, hint, 0.6f, 0.62f, 0.66f, a);
        }
        if ((row.id == VEHICLE_ROW || row.id == MAP_ROW || row.id == DEST_ROW || row.id == LEVEL_PICK || row.id == DEVSEC_ROW || row.id == DEVPLACE_ROW) && on && !this->title) {
            const std::string go = std::string(pad::hint("ENTER", pad::B_A)) + (row.id == MAP_ROW ? " TO LOAD" : row.id == DEST_ROW || row.id == DEVSEC_ROW || row.id == DEVPLACE_ROW ? " TO GO" : row.id == LEVEL_PICK ? " TO PLAY" : " TO DRIVE");
            r.text(valueX - r.textWidth(go, small) - 12 * u, cy + (rowH - 8 * small) / 2, small, go, 1.0f, 0.75f, 0.3f, a);
        }
    }
    if (row.kind == TEXT) {
        r.rect(valueX, cy + 5 * u, valueW, rowH - 10 * u, 0, 0, 0, 0.5f * a);
        if (on) r.rect(valueX, cy + rowH - 7 * u, valueW, 2 * u, 1.0f, 0.75f, 0.3f, 0.6f * a);
        // The window of the text that shows: the cursor stays in it (the front scrolls off, as it did).
        const bool mine = on && tedRow == row.id;
        const size_t cpos = mine ? te::cursorOf(row.value, ted) : row.value.size();
        const float room = valueW - 20 * u;
        size_t off = 0;
        while (off < cpos && r.textWidth(row.value.substr(off, cpos - off), ts) > room) off++;
        std::string v = row.value.substr(off);
        while (!v.empty() && r.textWidth(v, ts) > room) v.pop_back();
        size_t sf, st2;
        if (mine && te::selection(row.value, ted, sf, st2)) {
            sf = std::max(sf, off), st2 = std::min(st2, off + v.size());
            if (st2 > sf) {
                const float x0 = r.textWidth(row.value.substr(off, sf - off), ts), x1 = r.textWidth(row.value.substr(off, st2 - off), ts);
                r.rect(valueX + 8 * u + x0, cy + 9 * u, x1 - x0, rowH - 18 * u, 0.3f, 0.5f, 0.95f, 0.6f * a);
            }
        }
        if (v.empty() && !row.placeholder.empty()) {
            std::string ph = row.placeholder;
            while (!ph.empty() && r.textWidth(ph, ts) > valueW - 20 * u) ph.pop_back();
            r.text(valueX + 8 * u, ty, ts, ph, 0.42f, 0.44f, 0.48f, a);
        } else {
            r.text(valueX + 8 * u, ty, ts, v, 1, 1, 1, a);
        }
        if (on && std::fmod(caret, 1.0f) < 0.55f)
            r.rect(valueX + 9 * u + r.textWidth(row.value.substr(off, cpos - off), ts), cy + 9 * u, 2 * u, rowH - 18 * u, 1.0f, 0.75f, 0.3f, a);
        if (on && valueX - x > r.textWidth(row.label, ts) + r.textWidth(row.hint, small) + 60 * u)
            r.text(valueX - r.textWidth(row.hint, small) - 12 * u, cy + (rowH - 8 * small) / 2, small, row.hint, 0.6f, 0.62f, 0.66f, a);
    }
    if (row.kind == SLIDER) {
        const float textW = 70 * u, barW = valueW - textW - 8 * u, barY = cy + rowH / 2 - 3 * u;
        r.rect(valueX, barY, barW, 6 * u, 1, 1, 1, 0.15f * a);
        r.rect(valueX, barY, barW * row.frac, 6 * u, 1.0f, 0.75f, 0.3f, 0.9f * a);
        r.rect(valueX + barW * row.frac - 3 * u, barY - 5 * u, 6 * u, 16 * u, on ? 1.0f : 0.85f, on ? 1.0f : 0.85f, on ? 1.0f : 0.88f, a);
        r.text(valueX + valueW - r.textWidth(row.value, ts), ty, ts, row.value, 1, 1, 1, a);
        hit.barW = barW;
    }
    if (!row.dim) hits.push_back(hit);
    return hh;
}

// The main menu's own screen: a dark slate background with faint contour lines (a topo map)
// and an amber rule: no world behind it.
void PauseMenu::drawBackdrop(Renderer& r, int W, int H, float a) const {
    (void)a;
    const int bands = 24;
    for (int i = 0; i < bands; i++) {  // a vertical gradient, lighter toward the middle
        const float t = (i + 0.5f) / bands, k = 1.0f - std::fabs(t - 0.45f) * 1.4f;
        r.rect(0, H * (float)i / bands, (float)W, H / (float)bands + 1, 0.035f + 0.03f * k, 0.042f + 0.032f * k, 0.052f + 0.035f * k, 1.0f);
    }
    // Contour lines: a few hills' worth of wobbly rings, drawn as short thin segments.
    const float u = std::max(0.75f, H / 720.0f);
    struct Hill {
        float x, y, r0;
    };
    const Hill hills[3] = {{0.18f, 0.72f, 0.05f}, {0.82f, 0.28f, 0.04f}, {0.62f, 0.95f, 0.08f}};
    for (const Hill& h : hills)
        for (int ring = 0; ring < 14; ring++) {
            const float rad = (h.r0 + ring * 0.045f) * W;
            const int segs = 90;
            float px0 = 0, py0 = 0;
            for (int k = 0; k <= segs; k++) {
                const float ang = k * 6.2831853f / segs;
                const float wob = 1 + 0.10f * std::sin(ang * 3 + ring * 0.7f + h.x * 9) + 0.05f * std::sin(ang * 7 - ring * 1.3f);
                const float px1 = h.x * W + std::cos(ang) * rad * wob, py1 = h.y * H + std::sin(ang) * rad * wob * 0.8f;
                if (k > 0) {
                    const float dx = px1 - px0, dy = py1 - py0, l = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
                    const float nx = -dy / l * 0.6f * u, ny = dx / l * 0.6f * u;
                    const float al = ring % 5 == 0 ? 0.07f : 0.035f;
                    r.tri(px0 + nx, py0 + ny, px1 + nx, py1 + ny, px1 - nx, py1 - ny, 0.75f, 0.8f, 0.85f, al);
                    r.tri(px0 + nx, py0 + ny, px1 - nx, py1 - ny, px0 - nx, py0 - ny, 0.75f, 0.8f, 0.85f, al);
                }
                px0 = px1, py0 = py1;
            }
        }
    r.rect(0, 0, (float)W, 3 * u, 1.0f, 0.75f, 0.3f, 0.9f);
}

void PauseMenu::draw(Renderer& r, const Settings& s, int W, int H, double dt) {
    if (!open) return;
    fade = std::min(1.0f, fade + (float)dt * 7.0f);
    caret += (float)dt;
    spin += (float)dt;
    const float a = fade;
    if (this->title && sceneBehind) r.rect(0, 0, (float)W, (float)H, 0.0f, 0.01f, 0.02f, 0.38f * a);  // the camp shows through, dimmed
    else if (this->title) drawBackdrop(r, W, H, a);
    else r.rect(0, 0, (float)W, (float)H, 0.0f, 0.01f, 0.02f, 0.35f * a);  // darken the (blurred) scene
    if (page == 2) {
        drawMulti(r, s, W, H, a);
        return;
    }
    const std::vector<Row> all = rows(s);
    std::vector<Row> rs = all;
    std::vector<int> idx(all.size());  // (which row of the page each drawn row is)
    for (int k = 0; k < (int)all.size(); k++) idx[k] = k;
    const float u = std::max(0.75f, H / 720.0f);
    const bool titleMain = this->title && page == 0;
    const float ts = px(1.6f * u), small = px(1.2f * u), title = px((titleMain ? 2.0f : 3.0f) * u);
    const float pad = 22 * u;
    // The main menu: the name big above the panel.
    float top = 0;
    if (titleMain) {
        const float big = px(7.0f * u), tag = px(1.6f * u);
        const std::string name = "CRUMPLE ZONE", line = "SOFT-BODY DRIVING: CRASH IT, CRAWL IT, RACE IT";
        top = std::max(24 * u, H * 0.08f);
        label(r, (W - r.textWidth(name, big)) / 2, top, big, name, 1.0f, 0.75f, 0.3f, a);
        r.text((W - r.textWidth(line, tag)) / 2, top + 8 * big + 12 * u, tag, line, 0.62f, 0.64f, 0.68f, a);
        top += 8 * big + 12 * u + 8 * tag + 22 * u;
    }
    if (page == 7) {  // (the keybinds page: more rows than any screen holds. A window of them scrolls with the selection, under its group's header)
        const int capacity = std::max(6, (int)((H - 20 * u - (pad * 2 + 8 * title + 18 * u + 8 * small + 16 * u)) / (8 * ts + 9 * u)));
        const int n = (int)all.size();
        if (n > capacity) {
            scroll = std::min(std::max(scroll, sel - capacity + 2), std::max(0, sel - 1));  // (a row of room above and below)
            scroll = std::min(std::max(scroll, 0), n - capacity);
            std::vector<int> pick;
            int from = scroll;
            if (all[from].kind != HEADER)
                for (int k = from - 1; k >= 0; k--)
                    if (all[k].kind == HEADER) {
                        pick.push_back(k);
                        from++;  // (the header takes a slot)
                        break;
                    }
            for (int k = from; k < n && (int)pick.size() < capacity; k++) pick.push_back(k);
            rs.clear();
            idx = pick;
            for (int k : pick) rs.push_back(all[k]);
        } else scroll = 0;
    }
    // (The settings page is taller than a 720p screen: squeeze the space between rows to fit, keeping the text size.)
    auto pageHeight = [&](float k) {
        float contentH = 0;
        for (const Row& row : rs)
            contentH += row.kind == HEADER ? 8 * small + 16 * u * k : 8 * ts + 14 * u * k + (row.kind == CHOICE && !row.hint.empty() ? 8 * small + 6 * u : 0);
        return pad * 2 + 8 * title + 18 * u + contentH + 8 * small + 16 * u;
    };
    const float avail = H - top - 16 * u;
    const float h0 = pageHeight(0), h1 = pageHeight(1);
    const float squeeze = h1 > avail ? std::min(1.0f, std::max(0.2f, (avail - h0) / (h1 - h0))) : 1.0f;
    const float headH = 8 * small + 16 * u * squeeze;
    float rowH = 8 * ts + 14 * u * squeeze;

    float contentH = 0, h = 0;
    for (int pass = 0; pass < 2; pass++) {
        contentH = 0;
        int n = 0;
        for (const Row& row : rs) {
            contentH += row.kind == HEADER ? headH : rowH + (row.kind == CHOICE && !row.hint.empty() ? 8 * small + 6 * u : 0);
            n += row.kind != HEADER;
        }
        h = pad * 2 + 8 * title + 18 * u + contentH + 8 * small + 16 * u;
        // A long page (settings at 720p) packs its rows closer rather than run off the screen.
        if (pass == 0 && h > avail - 4 * u && n > 0) rowH = std::max(8 * ts + 6 * u, rowH - (h - (avail - 4 * u)) / n);
        else break;
    }
    const float w = std::min((float)W - 40 * u, 640 * u);
    const float x = (W - w) / 2, y = titleMain ? std::max(top, top + (avail - h) * 0.25f) : std::max(10 * u, (H - h) / 2);
    pill(r, x, y, w, h, 0.03f, 0.035f, 0.045f, (this->title ? 0.82f : 0.9f) * a);
    r.rect(x + pad, y + pad + 8 * title + 8 * u, w - 2 * pad, 2 * u, 1.0f, 0.75f, 0.3f, 0.8f * a);
    label(r, x + pad, y + pad, title,
          page == 0 ? (this->title ? "MAIN MENU" : "PAUSED") : page == 1 ? "SETTINGS" : page == 3 ? "AUTOPILOT" : page == 4 ? "LEVELS" : page == 5 ? "JOBS" : page == 7 ? "KEYBINDS" : page == 8 ? "DEV MENU" : page == 9 ? devmenu::groupName((devmenu::Group)devGroup) : "CONTROLS",
          1.0f, 0.75f, 0.3f, a);
    if (page == 0 && !this->title) {
        const char* tag = "CRUMPLE ZONE";
        r.text(x + w - pad - r.textWidth(tag, small), y + pad + 8 * title - 8 * small, small, tag, 0.55f, 0.57f, 0.62f, a);
    }

    hits.clear();
    const RowStyle st{ts, small, u, rowH, headH, a, 250 * u};
    float cy = y + pad + 8 * title + 18 * u;
    for (int k = 0; k < (int)rs.size(); k++) cy += drawRow(r, rs[k], idx[k], x, w, cy, st);
    if (page == 7 && rs.size() < all.size())
        r.text(x + w - pad - r.textWidth("MORE: W S SCROLLS", small), y + pad + 8 * title - 8 * small, small, "MORE: W S SCROLLS", 0.55f, 0.57f, 0.62f, a);
    std::string keys = page == 0 && this->title ? "W S SELECT   A D CHANGE   ENTER OK"
                       : page == 0                ? "W S SELECT   A D CHANGE   ENTER OK   ESC RESUME"
                       : page == 8 && !devFromPause ? "W S SELECT   A D CHANGE   ENTER OK   ESC CLOSE"
                                                  : "W S SELECT   A D CHANGE   ENTER OK   ESC BACK";
    if (pad::ui.last) {  // the controller was the last thing used
        keys = "D-PAD SELECT   LEFT RIGHT CHANGE   " + std::string(pad::hint("", pad::B_A)) + " OK";
        if (!(page == 0 && this->title)) keys += "   " + std::string(pad::hint("", pad::B_B)) + (page == 0 || (page == 8 && !devFromPause) ? " RESUME" : " BACK");
    }
    r.text(x + (w - r.textWidth(keys, small)) / 2, y + h - pad - 8 * small + 6 * u, small, keys, 0.55f, 0.57f, 0.62f, a);
}

// The multiplayer page: YOUR NAME across the top; the HOST A GAME and JOIN A GAME cards side
// by side (stacked on a narrow window); BACK below. In a session, or while joining, one card.
void PauseMenu::drawMulti(Renderer& r, const Settings& s, int W, int H, float a) {
    std::vector<Row> rs = rows(s);
    if (sel < 0 || sel >= (int)rs.size() || rs[sel].kind == HEADER) {
        sel = 0;
        while (sel < (int)rs.size() - 1 && rs[sel].kind == HEADER) sel++;
    }
    const float u = std::max(0.75f, H / 720.0f);
    const float ts = px(1.6f * u), small = px(1.2f * u), big = px(3.0f * u), cardT = px(2.0f * u);
    const float pad = 22 * u;
    const RowStyle base{ts, small, u, 8 * ts + 14 * u, 8 * small + 12 * u, a, 230 * u};
    auto colHeight = [&](int c) {
        float h = 0;
        for (const Row& row : rs)
            if (row.col == c) h += row.kind == HEADER ? base.headH : base.rowH + (row.kind == CHOICE && !row.hint.empty() ? 8 * small + 6 * u : 0);
        return h;
    };
    bool hasJoin = false;
    for (const Row& row : rs) hasJoin |= row.col == 2;
    const bool wide = W >= 900 * u * 0.85f && hasJoin;
    const float totalW = std::min((float)W - 30 * u, hasJoin ? 1180 * u : 700 * u);
    const float gap = 20 * u;
    const float cardW = hasJoin && wide ? (totalW - gap) / 2 : totalW;
    const float cardHead = pad + 8 * cardT + 16 * u;
    const float h1 = cardHead + colHeight(1) + pad, h2 = hasJoin ? cardHead + colHeight(2) + pad : 0;
    const float cardsH = wide ? std::max(h1, h2) : h1 + (hasJoin ? gap + h2 : 0);
    float top0 = 0, bottom0 = 0;
    for (const Row& row : rs)
        if (row.col == 0) (&row < &rs[rs.size() / 2] ? top0 : bottom0) += base.rowH;
    const float headerH = 8 * big + 20 * u;
    const float total = headerH + top0 + (top0 > 0 ? 12 * u : 0) + cardsH + 12 * u + bottom0 + 8 * small + 20 * u;
    const float x0 = (W - totalW) / 2;
    float y = std::max(12 * u, (H - total) / 2);

    hits.clear();
    // Title, and what's going on, on the right.
    label(r, x0, y, big, "MULTIPLAYER", 1.0f, 0.75f, 0.3f, a);
    {
        const std::string st = joining ? (joinError.empty() ? "CONNECTING..." : "COULDN'T JOIN") : netStatus;
        r.text(x0 + totalW - r.textWidth(st, small), y + 8 * big - 8 * small, small, st, 0.6f, 0.62f, 0.66f, a);
    }
    r.rect(x0, y + 8 * big + 8 * u, totalW, 2 * u, 1.0f, 0.75f, 0.3f, 0.8f * a);
    y += headerH;
    // Your name (offline), across the top.
    const int n = (int)rs.size();
    int k = 0;
    if (top0 > 0) {
        const float nameW = std::min(totalW, 620 * u);
        pill(r, x0, y - 4 * u, nameW, top0 + 8 * u, 0.03f, 0.035f, 0.045f, 0.85f * a);  // (the name, and the password)
        for (; k < n && rs[k].col == 0; k++) y += drawRow(r, rs[k], k, x0, nameW, y, base);
        y += 12 * u;
    }
    // The cards.
    auto card = [&](int c, float cx, float cy, float cw, float ch, const char* titleText, const char* sub) {
        pill(r, cx, cy, cw, ch, 0.03f, 0.035f, 0.045f, 0.9f * a);
        r.rect(cx, cy, cw, 3 * u, c == 1 ? 1.0f : 0.35f, c == 1 ? 0.75f : 0.75f, c == 1 ? 0.3f : 1.0f, 0.9f * a);
        label(r, cx + pad, cy + pad, cardT, titleText, 1, 1, 1, a);
        if (sub && r.textWidth(titleText, cardT) + r.textWidth(sub, small) + 24 * u < cw - 2 * pad)  // (only if it fits beside the title)
            r.text(cx + cw - pad - r.textWidth(sub, small), cy + pad + 8 * cardT - 8 * small, small, sub, 0.55f, 0.57f, 0.62f, a);
        float ry = cy + cardHead;
        RowStyle st = base;
        st.valueW = std::min(230 * u, cw * 0.46f);
        for (int i = 0; i < n; i++)
            if (rs[i].col == c) ry += drawRow(r, rs[i], i, cx, cw, ry, st);
    };
    const char* t1 = joining ? (joinError.empty() ? "JOINING" : "NOT CONNECTED") : netActive ? sessionTitle.c_str() : "HOST A GAME";
    const char* s1 = joining || netActive ? nullptr : "YOUR WORLD: OTHERS JOIN YOU";
    if (joining && joinError.empty()) {  // a spinner by the title
        const float cx = x0 + totalW - pad - 10 * u, cy = y + pad + 4 * cardT;
        for (int i = 0; i < 8; i++) {
            const float ang = i * 0.785398f + spin * 6.0f;
            const float al = 0.15f + 0.85f * (float)i / 8;
            r.rect(cx + std::cos(ang) * 10 * u - 2 * u, cy + std::sin(ang) * 10 * u - 2 * u, 4 * u, 4 * u, 1.0f, 0.75f, 0.3f, al * a);
        }
    }
    if (wide) {
        card(1, x0, y, cardW, cardsH, t1, s1);
        if (hasJoin) card(2, x0 + cardW + gap, y, cardW, cardsH, "JOIN A GAME", "PLAY IN SOMEONE ELSE'S WORLD");
    } else {
        card(1, x0, y, cardW, h1, t1, s1);
        if (hasJoin) card(2, x0, y + h1 + gap, cardW, h2, "JOIN A GAME", "PLAY IN SOMEONE ELSE'S WORLD");
    }
    y += cardsH + 12 * u;
    // BACK (and anything else across the bottom).
    for (k = n / 2; k < n; k++)
        if (rs[k].col == 0) {
            const float bw = std::min(totalW, 300 * u);
            pill(r, x0, y - 4 * u, bw, base.rowH + 8 * u, 0.03f, 0.035f, 0.045f, 0.85f * a);
            y += drawRow(r, rs[k], k, x0, bw, y, base);
        }
    std::string keys = joining ? "ESC CANCEL" : "W S SELECT   A D CARD / CHANGE   ENTER OK   ESC BACK";
    if (pad::ui.last)
        keys = joining ? std::string(pad::hint("", pad::B_B)) + " CANCEL"
                       : "D-PAD SELECT   LEFT RIGHT CARD   " + std::string(pad::hint("", pad::B_A)) + " OK   " + std::string(pad::hint("", pad::B_B)) + " BACK";
    r.text(x0 + totalW - r.textWidth(keys, small), y + 4 * u, small, keys, 0.55f, 0.57f, 0.62f, a);
}
