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
        if (k == "procseed") {  // (a 64-bit number: not through a float)
            procSeed = std::strtoull(text, nullptr, 10);
            continue;
        }
        const float v = (float)std::atof(text);
        if (k == "mph") mph = v != 0;
        else if (k == "winchspeed" && std::isfinite(v)) winchSpeed = std::clamp((double)v, recovery::kWinchSpeedMin, recovery::kWinchSpeedMax);
        else if (k == "volume") volume = std::min(std::max(v, 0.0f), 1.0f);
        else if (k == "music") music = std::min(std::max(v, 0.0f), 1.0f);
        else if (k == "vehiclevolume" && std::isfinite(v)) vehicleVolume = std::clamp(v, 0.0f, 1.0f);
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
    std::fprintf(f, "recovery %d\nmusic %.2f\nambience %.2f\nvehiclevolume %.2f\nfuel %d\ncareer %d\n", realRecovery, music, ambience, vehicleVolume, fuelMode, career);
    std::fprintf(f, "ground %d\nprops %d\ndrawdistance %.0f\nsharpen %d\nfpstarget %d\n", gfx.ground, gfx.props, gfx.drawDistance, gfx.sharpen, fpsTarget);
    std::fprintf(f, "physicsthreads %d\nfullhud %d\n", physicsThreads, fullHud);
    std::fprintf(f, "adaptive %d\ndevhotkeys %d\ngfxchosen %d\n", adaptiveQuality, devHotkeys, gfxChosen);
    std::fprintf(f, "winchspeed %.2f\n", winchSpeed);
    if (procSeed) std::fprintf(f, "procseed %llu\n", procSeed);
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

#include "menuids.h"

namespace {
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
    const char* mapHint = mapPick == MAP_MOUNTAIN   ? "START AT THE MOUNTAIN: SUMMIT TRAIL, CRAWL PARK, VALLEY LOOP"
                          : mapPick == MAP_FREEWAY ? "THREE LANES EACH WAY THAT NEVER END: TOP SPEED"
                          : mapPick == MAP_FRONTIER ? "START AT THE FRONTIER: DESERT, RIVERS, A RANGE, A RACE CIRCUIT"
                          : mapPick == MAP_BACKCOUNTRY ? "8 KM: TRAILS, RIVERS AND PLACES, THE MOUNTAIN, FRONTIER AND OPEN COUNTRY ROUND IT"
                          : mapPick == MAP_WILDS ? "START IN THE OPEN COUNTRY: ROOM TO TRY ANYTHING"
                          : mapPick == MAP_PROCEDURAL ? "A BRAND-NEW ENDLESS WORLD EVERY TIME: DRIVE AS FAR AS YOU LIKE"
                                                    : "EVERYTHING IN THE GAME, LAID OUT TO LOOK AT AND TRY";
    if (page == 0 && title) {
        const auto& cat = vehicleCatalog();
        button(FREEDRIVE_ROW, "FREE DRIVE", "THIS MAP, THIS VEHICLE:");
        choice(MODE_ROW, "MODE", s.career ? "CAREER" : "SANDBOX", 2,
               s.career ? "START STOCK: FIND, WIN AND BUY BETTER PARTS" : "EVERY PART ON EVERY VEHICLE, ANY TIME");
        choice(VEHICLE_ROW, "VEHICLE", cat[vehicle].name, (int)listedVehicles().size(), cat[vehicle].blurb);
        choice(MAP_ROW, "MAP", mapPickName(mapPick), (int)listedMaps().size(), mapHint);
        button(LEVELS_ROW, "LEVELS", std::to_string(levels.size()) + " RACES, CLIMBS AND WORLDS");
        button(MULTI_ROW, "MULTIPLAYER", netActive ? netStatus : "OPEN TO FRIENDS, OR JOIN A GAME");
        button(EDITOR_ROW, "WORLD EDITOR", "BUILD YOUR OWN WORLDS AND LEVELS");
        button(SECTION_ROW, "NEW SECTION", "HAND-BUILD AN OBSTACLE SECTION (DEV)");
        button(LAB_ROW, "MODEL LAB", "PICK, TWEAK OR REDO THE GAME'S MODELS");
        button(GARAGE_ROW, "GARAGE", "BUY AND FIT PARTS, SEE THEM UP CLOSE");
        button(SETTINGS_ROW, "SETTINGS", "UNITS, GRAPHICS, SOUND");
        button(CONTROLS_ROW, "CONTROLS");
        button(QUIT_ROW, "QUIT GAME");
        return r;
    }
    if (page == 1) return rowsFor(tab, s);
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
    if (page == 0) {  // the pause menu
        button(RESUME_ROW, "RESUME");
        if (editorSuspended) button(EDITBACK_ROW, "BACK TO THE EDITOR", s.keys.name(KA_PLAYTEST_BACK) + ": YOUR EDITS ARE KEPT");
        if (levelActive) {
            button(LRESTART_ROW, "RESTART LEVEL", "OR " + s.keys.name(KA_REPAIR) + " TWICE");
            button(LQUIT_ROW, "QUIT LEVEL", "BACK TO FREE DRIVE");
        }
        button(MULTI_ROW, "MULTIPLAYER", netActive ? netStatus : "OPEN TO FRIENDS, OR JOIN A GAME");
        button(SETTINGS_ROW, "SETTINGS", "GRAPHICS, CONTROLS, AUDIO, GENERAL, DEV");
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
        button(PGO_ROW, pilotActive ? "GO THERE INSTEAD" : "GO", s.keys.name(KA_AUTOPILOT) + " STARTS AND STOPS IT, " + s.keys.name(KA_AUTOPILOT_PICK) + " OPENS THIS PAGE");
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
            choice(HMAP_ROW, "MAP", mapPickName(mapPick), (int)listedMaps().size());
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
    return r;
}

const char* PauseMenu::tabName(int t) {
    static const char* n[T_COUNT] = {"GRAPHICS", "CONTROLS", "AUDIO", "GENERAL", "DEV"};
    return t >= 0 && t < T_COUNT ? n[t] : "";
}

// One tab of the SETTINGS page.
std::vector<PauseMenu::Row> PauseMenu::rowsFor(int tabIndex, const Settings& s) const {
    std::vector<Row> r;
    auto button = [&](int id, const char* label, const std::string& hint = "") { r.push_back({BUTTON, label, "", hint, id}); };
    auto header = [&](const std::string& label) { r.push_back({HEADER, label, "", "", 0}); };
    auto choice = [&](int id, const char* label, const std::string& value, int count, const std::string& hint = "") {
        r.push_back({CHOICE, label, value, hint, id, count});
    };
    auto slider = [&](int id, const char* label, const std::string& value, float frac, int steps) {
        Row row{SLIDER, label, value, "", id, steps};
        row.frac = frac;
        r.push_back(row);
    };
    static const char* shadows[4] = {"OFF", "LOW", "MEDIUM", "HIGH"};
    static const char* aa[4] = {"OFF", "2X MSAA", "4X MSAA", "8X MSAA"};
    if (tabIndex == T_GRAPHICS) {
        header("QUALITY");
        choice(QUALITY, "QUALITY", presetName(presetOf(s.gfx)), 5, "POTATO: FOR THE WEAKEST LAPTOPS. HALF RESOLUTION, NO SHADOWS, 300 M OF VIEW");
        button(DETECT_ROW, "DETECT AUTOMATICALLY", "TESTS A FEW SECONDS OF DRIVING");
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
        {
            const int available = physicsThreadLimit();
            const int chosen = std::clamp(s.physicsThreads, 0, available);
            const std::string value = chosen == 0 ? "AUTO (" + std::to_string(resolvePhysicsThreads(0)) + ")" :
                std::to_string(chosen) + (chosen == available ? " (ALL)" : "");
            choice(PHYSICSTHREADS, "PHYSICS THREADS", value, available + 1,
                   "1: SINGLE THREAD. ALL: USE EVERY CPU THREAD. MORE MAY NOT BE FASTER.");
        }
        button(BACK_ROW, "BACK");
        return r;
    }
    if (tabIndex == T_CONTROLS) {
        header(padName.empty() ? "NO CONTROLLER CONNECTED" : ("CONTROLLER: " + padName));
        choice(PADSTEER, "PAD STEERING", pad::kSteeringNames[std::min(std::max(s.padSteering, 0), 2)], 3,
               "HOW QUICKLY THE STICK TURNS THE WHEELS: SOFT IS GENTLE NEAR THE CENTRE");
        choice(PADDEAD, "STICK DEADZONE", pad::kDeadzoneNames[std::min(std::max(s.padDeadzone, 0), 2)], 3,
               "RAISE IT IF THE CAR DRIFTS WITH YOUR HANDS OFF");
        choice(PADRUMBLE, "RUMBLE", pad::kRumbleNames[std::min(std::max(s.padRumble, 0), 2)], 3, "HITS, LANDINGS, BREAKS, REV LIMITER");
        choice(PADINVERTX, "INVERT LOOK X", kOffOn[s.padInvertLookX], 2, "FLIPS LEFT/RIGHT ON THE RIGHT STICK");
        choice(PADINVERTY, "INVERT LOOK Y", kOffOn[s.padInvertLookY], 2, "FLIPS UP/DOWN ON THE RIGHT STICK");
        header("STEERING AND THROTTLE");
        choice(STEERING, "STEERING ASSIST", kOffOn[s.steeringAssist], 2,
               s.steeringAssist ? "LIMITS TURNING AT HIGH SPEED (SAFE)" : "FULL LOCK AT ALL SPEEDS (SIMULATION)");
        header(("THROTTLE LIMIT: " + s.keys.name(KA_THROTTLE_CAP) + " CYCLES 100 50 25 10% WHILE DRIVING"));
        slider(THROTTLECAP, "KEYBOARD THROTTLE", throttle::percent(s.throttle.cap), (s.throttle.cap - throttle::kMinCap) / (1 - throttle::kMinCap), 20);
        static const char* respHint[3] = {"W IS AT THE LIMIT AT ONCE", "W BUILDS UP OVER ABOUT 0.4 S", "W BUILDS UP OVER ABOUT 1.2 S"};
        const int resp = std::min(std::max(s.throttle.response, 0), 2);
        choice(THROTTLERESP, "THROTTLE RESPONSE", throttle::kResponseNames[resp], 3, respHint[resp]);
        choice(THROTTLEPAD, "APPLIES TO PAD", kOffOn[s.throttle.padToo], 2, "THE LIMIT ALSO SCALES THE TRIGGER (OFF: KEYBOARD ONLY)");
        choice(CROUCHMODE, "CROUCH ON FOOT", s.crouchToggle ? "TOGGLE" : "HOLD", 2, "HOLD THE KEY TO STAY LOW, OR PRESS IT TO CROUCH AND AGAIN TO STAND");
        header(listening >= 0 ? "PRESS THE NEW KEY (SHIFT+KEY FOR A COMBO). ESC CANCELS, BACKSPACE CLEARS" : keyNote.empty() ? "KEYBOARD KEYS: ENTER ON A ROW, THEN PRESS A KEY" : keyNote);
        button(CONTROLS_ROW, "SHOW THE CONTROLS OVERLAY", s.keys.name(KA_HELP) + " IN THE GAME");
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
    if (tabIndex == T_AUDIO) {
        header("VOLUME");
        slider(VOLUME, "VOLUME", pct(s.volume), s.volume, 21);
        slider(VEHICLEVOL, "VEHICLE VOLUME", s.vehicleVolume > 0 ? pct(s.vehicleVolume) : "OFF", s.vehicleVolume, 21);
        slider(MUSIC, "MUSIC", s.music > 0 ? pct(s.music) : "OFF", s.music, 21);
        slider(AMBIENCE, "AMBIENCE", s.ambience > 0 ? pct(s.ambience) : "OFF", s.ambience, 21);
        button(BACK_ROW, "BACK");
        return r;
    }
    if (tabIndex == T_DEV) {  // the dev menu (devmenu.h): the groups, then one group's rows
        if (!devInGroup) {
            header("TEST AND DEBUG TOOLS");
            choice(DEVHOT_ROW, "DEV HOTKEYS", kOffOn[s.devHotkeys], 2,
                   s.devHotkeys ? "ON: DEV KEYS (THE DEV KEYS ARE TOOLS). " + s.keys.name(KA_DEV_TOGGLE) + " TOGGLES" : "OFF: GAMEPLAY KEYS, THE TOOLS ONLY HERE. " + s.keys.name(KA_DEV_TOGGLE) + " TOGGLES");
            for (int g = 0; g < devmenu::G_COUNT; g++) button(DEVGROUP0 + g, devmenu::groupName((devmenu::Group)g), devmenu::groupHint((devmenu::Group)g));
            button(BACK_ROW, "BACK");
            return r;
        }
        const std::vector<devmenu::Item> its = devItems(s.keys);
        header(std::string(devmenu::groupName((devmenu::Group)devGroup)) + ": " + devmenu::groupHint((devmenu::Group)devGroup));
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
    // GENERAL
    header("GAME");
    choice(UNITS, "SPEED UNITS", s.mph ? "MPH" : "KM/H", 2);
    choice(MODE_ROW, "MODE", s.career ? "CAREER" : "SANDBOX", 2,
           s.career ? "START STOCK: FIND, WIN AND BUY BETTER PARTS (SWITCH FROM THE MAIN MENU)" : "EVERY PART ON EVERY VEHICLE, ANY TIME");
    choice(RECOVERY, "RECOVERY", s.realRecovery ? "REAL" : "ARCADE", 2,
           s.realRecovery ? "DAMAGE STAYS: TOW OR DRIVE TO A REPAIR STATION" : s.keys.name(KA_FLIP) + " FIXES EVERYTHING WHERE YOU ARE");
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
    char speed[32]; std::snprintf(speed, sizeof speed, "%.2gX", s.winchSpeed);
    choice(WINCHSPEED, "WINCH SPEED", speed, 11, "1X: RATED SPEED. 4X: DEFAULT. UP TO 20X. " + s.keys.name(KA_WINCH_SLOWER) + " AND " + s.keys.name(KA_WINCH_FASTER) + " CHANGE IT WHILE DRIVING.");
    header("HUD AND CAMERA");
    choice(FULLHUD, "SHOW FULL HUD", s.fullHud ? "ALWAYS" : "CONTEXTUAL", 2, "CONTEXTUAL: FUEL, TERRAIN, DAMAGE AND 4X4 SHOW WHEN THEY MATTER. HOLD THE FULL HUD KEY (CONTROLS) TO SEE ALL.");
    slider(FOV, "FIELD OF VIEW", std::to_string((int)std::lround(s.fovDeg)) + " DEG", (s.fovDeg - 50) / 50, 26);
    button(BACK_ROW, "BACK");
    return r;
}

std::vector<devmenu::Item> PauseMenu::devItems(const Keybinds& kb) const {
    std::vector<devmenu::Item> r;
    for (const devmenu::Item& it : devmenu::items((devmenu::Group)devGroup, &kb))
        if (it.action.act != devmenu::SCENARIO_CHECKS || devScenario) r.push_back(it);
    return r;
}

void PauseMenu::setTab(int t) {
    t = std::min(std::max(t, 0), tabCount() - 1);
    if (t == tab && page == 1) return;
    tab = t;
    listening = -1;
    keyNote.clear();
    devInGroup = false;
    scroll = 0;
    dragSlider = -1;
    hits.clear();
    sel = 1;  // (every tab starts with a header)
}

void PauseMenu::showSettings(int t, int currentVehicle, int currentMap, bool fromPause) {
    show(currentVehicle, currentMap);
    page = 1;
    settingsFromPause = fromPause;
    standalone = false;
    tab = -1;
    setTab(t);
}

void PauseMenu::showDev(int currentVehicle, int currentMap, bool fromPause) {
    if (open && page == 1 && !title) {  // (already in Settings: the DEV tab, where the menu came from stays)
        setTab(T_DEV);
        return;
    }
    showSettings(T_DEV, currentVehicle, currentMap, fromPause);
}

void PauseMenu::showMultiplayer(int currentVehicle, int currentMap) {
    show(currentVehicle, currentMap);
    page = 2;
    sel = 1;  // (row 0 is the status header)
}

void PauseMenu::showPage(int pg, int currentVehicle, int currentMap) {
    if (pg == 1 || pg == 6 || pg == 7 || pg == 8 || pg == 9) {  // (the settings, or one of the pages that are its tabs now)
        showSettings(pg == 1 ? T_GRAPHICS : pg <= 7 ? T_CONTROLS : T_DEV, currentVehicle, currentMap, false);
        standalone = true;
        return;
    }
    show(currentVehicle, currentMap);
    page = pg;
    sel = 1;
    standalone = true;
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
    if (pg == 1) setTab(T_GRAPHICS);
}

void PauseMenu::show(int currentVehicle, int currentMap) {
    mapPick = mapListed(currentMap) ? currentMap : nextListedMap(currentMap, 0);
    open = true;
    title = false;
    page = 0;
    listening = -1;
    keyNote.clear();
    portEdited = portFresh = false;
    standalone = settingsFromPause = devInGroup = false;
    tab = T_GRAPHICS;
    scroll = 0;
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
        case MAP_ROW: return change(row, 1, s);  // (the old title page's pickers)
        case FREEDRIVE_ROW: return {FREE_DRIVE, vehicle};
        case LEVELS_ROW: goPage(4, LEVEL_PICK); return {};
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
        case SETTINGS_ROW:
            page = 1, tab = -1, settingsFromPause = !title;
            hits.clear();
            setTab(T_GRAPHICS);
            return {};
        case MULTI_ROW: goPage(2, joining ? CANCEL_ROW : netActive ? LEAVE_ROW : !found.empty() ? FOUND_ROW0 : HOST_ROW); return {};
        case PGO_ROW:
        case DEST_ROW: return {PILOT_GO};
        case PSTOP_ROW: return {PILOT_STOP};
        case CONTROLS_ROW: return {CONTROLS};
        case DEVHOT_ROW: return change(row, 1, s);
        case DEVSEC_ROW: return {DEV_RUN, devmenu::TELEPORT_SECTION, std::min(std::max(devSection, 0), std::max(0, it.count - 1))};
        case DEVPLACE_ROW: return {DEV_RUN, devmenu::TELEPORT_PLACE, std::min(std::max(devPlace, 0), std::max(0, it.count - 1))};
        case DETECT_ROW: return {DETECT_GFX};
        case KEYS_RESET: s.keys.resetDefaults(); listening = -1, keyNote.clear(); return {CHANGED};
        case RESET_ROW: return {RESET};
        case QUIT_ROW: return {QUIT};
        case BACK_ROW:
            if (page == 1 && tab == T_DEV && devInGroup) {  // (one group's rows: back to the groups)
                devInGroup = false, scroll = 0, hits.clear();
                const std::vector<Row> nr = rows(s);
                sel = 1;
                for (int k = 0; k < (int)nr.size(); k++)
                    if (nr[k].id == DEVGROUP0 + devGroup) sel = k;
                return {};
            }
            if (page == 1) {
                listening = -1, keyNote.clear();
                if (title || settingsFromPause) goPage(0, SETTINGS_ROW);
                else return {RESUME};
                return {};
            }
            if (standalone && !title) return {RESUME};  // (opened by its key: back out to the game)
            goPage(0, page == 2 ? MULTI_ROW : page == 4 ? LEVELS_ROW : SETTINGS_ROW);
            return {};
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
        devInGroup = true, scroll = 0, hits.clear();
        sel = 1;
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
        case VEHICLEVOL: s.vehicleVolume = frac; break;
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
    if (page == 1 && tab == T_CONTROLS && listening >= 0) {  // the keybinds waiting for a key: this press is the binding
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
    if (page == 1 && (sc == SC_Q || sc == SC_E)) {  // the tabs (the pad's shoulder buttons arrive as these keys)
        stepTab(sc == SC_E ? 1 : -1);
        return {};
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
    if (page == 1) {  // the tab strip
        for (int t = 0; t < tabCount(); t++) {
            const Rect c = tabRect(t, s);
            if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) {
                setTab(t);
                return {};
            }
        }
    }
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

// ---- The SETTINGS page's layout and what the tests look at ---------------------------

PauseMenu::Geom PauseMenu::settingsGeom(int W, int H, const Settings& s) const {
    Geom g{};
    const float u = std::max(0.75f, H / 720.0f);
    const float ts = hud::px(1.6f * u), small = hud::px(1.2f * u), titleS = hud::px(3.0f * u), pad = 22 * u;
    g.u = u;
    g.w = std::min((float)W - 40 * u, 640 * u);
    g.x = ((float)W - g.w) / 2;
    const float headerBlock = pad + 8 * titleS + 18 * u;
    g.tabH = 8 * ts + 12 * u;
    const float footer = 8 * small + 16 * u + pad;
    float need = 0;  // (the same panel for every tab: as tall as the tallest of the short ones)
    for (int t : {(int)T_GRAPHICS, (int)T_AUDIO, (int)T_GENERAL}) {
        float c = 0;
        for (const Row& row : rowsFor(t, s)) c += row.kind == HEADER ? 8 * small + 16 * u : 8 * ts + 14 * u + (row.kind == CHOICE && !row.hint.empty() ? 8 * small + 6 * u : 0);
        need = std::max(need, c);
    }
    g.h = std::min(headerBlock + g.tabH + 8 * u + need + footer, (float)H - 20 * u);
    g.y = std::max(10 * u, ((float)H - g.h) / 2);
    g.tabY = g.y + headerBlock;
    g.rowsY = g.tabY + g.tabH + 8 * u;
    g.rowsH = g.y + g.h - footer - g.rowsY;
    return g;
}

PauseMenu::Rect PauseMenu::tabRect(int t, const Settings& s) const {
    const Geom g = settingsGeom(viewW, viewH, s);
    const float pad = 22 * g.u, cw = (g.w - 2 * pad) / (float)std::max(1, tabCount());
    return {g.x + pad + cw * (float)t, g.tabY, cw, g.tabH};
}

std::vector<std::string> PauseMenu::labels(const Settings& s) const {
    std::vector<std::string> out;
    for (const Row& row : rows(s))
        if (row.kind != HEADER) out.push_back(row.label);
    return out;
}

std::string PauseMenu::selectedLabel(const Settings& s) const {
    const std::vector<Row> r = rows(s);
    return sel >= 0 && sel < (int)r.size() ? r[sel].label : std::string();
}

bool PauseMenu::select(const Settings& s, const std::string& label) {
    const std::vector<Row> r = rows(s);
    for (int k = 0; k < (int)r.size(); k++)
        if (r[k].kind != HEADER && r[k].label == label) {
            sel = k;
            return true;
        }
    return false;
}
