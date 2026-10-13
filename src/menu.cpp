#include "menu.h"


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        char key[64], text[192] = "";
        if (std::sscanf(line, "%63s %191s", key, text) < 1) continue;
        std::string k = key;
        if (k == "name") {
            playerName = text;
            continue;
        }
        if (k == "pilotdest") {
            pilotDest = std::max(0, std::atoi(text));
            continue;
        }
        if (k == "join") {
            joinAddress = text;
            continue;
        }
        const float v = (float)std::atof(text);
        if (k == "mph") mph = v != 0;
        else if (k == "volume") volume = std::min(std::max(v, 0.0f), 1.0f);
        else if (k == "music") music = std::min(std::max(v, 0.0f), 1.0f);
        else if (k == "fov") fovDeg = std::min(std::max(v, 50.0f), 100.0f);
        else if (k == "fullscreen") fullscreen = v != 0;
        else if (k == "vsync") vsync = v != 0;
        else if (k == "fps") showFps = v != 0;
        else if (k == "shadows") gfx.shadows = std::min(std::max((int)v, 0), 3);
        else if (k == "msaa") gfx.msaa = (int)v;
        else if (k == "ao") gfx.ao = v != 0;
        else if (k == "bloom") gfx.bloom = v != 0;
        else if (k == "clouds") gfx.clouds = v != 0;
        else if (k == "scale") gfx.renderScale = std::min(std::max(v, 0.5f), 2.0f);
        else if (k == "grass") gfx.grass = v != 0;
        else if (k == "map") map = std::min(std::max((int)v, 0), MAP_COUNT - 1);
        else if (k == "recovery") realRecovery = v != 0;
        else if (k == "fuel") fuelMode = std::min(std::max((int)v, 0), 2);
        else if (k == "pilotspeed") pilotSpeed = std::min(std::max(v, 5.0f), 100.0f);
        else if (k == "pilotroute") pilotRoute = std::min(std::max((int)v, 0), 2);
        else if (k == "timeofday") timeOfDay = std::min(std::max((int)v, 0), TOD_COUNT - 1);
        else if (k == "daylength") dayMinutes = std::min(std::max(v, 1.0f), 240.0f);
        else if (k == "clock") clockHours = std::fmod(std::max(v, 0.0f), 24.0f);
    }
    std::fclose(f);
    return true;
}

bool Settings::save(const std::string& path) const {
    std::string dir = path.substr(0, path.find_last_of('/'));
    if (!dir.empty()) makeDir(dir);
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "mph %d\nvolume %.2f\nfov %.0f\nfullscreen %d\nvsync %d\nfps %d\n", mph, volume, fovDeg, fullscreen,
                 vsync, showFps);
    std::fprintf(f, "shadows %d\nmsaa %d\nao %d\nbloom %d\nclouds %d\nscale %.2f\ngrass %d\nmap %d\n", gfx.shadows,
                 gfx.msaa, gfx.ao, gfx.bloom, gfx.clouds, gfx.renderScale, gfx.grass, map);
    std::fprintf(f, "recovery %d\nmusic %.2f\nfuel %d\n", realRecovery, music, fuelMode);
    if (!playerName.empty()) std::fprintf(f, "name %s\n", playerName.c_str());
    if (!joinAddress.empty()) std::fprintf(f, "join %s\n", joinAddress.c_str());
    std::fprintf(f, "pilotdest %d\npilotspeed %.0f\npilotroute %d\n", pilotDest, pilotSpeed, pilotRoute);
    std::fprintf(f, "timeofday %d\ndaylength %.0f\nclock %.2f\n", timeOfDay, dayMinutes, clockHours);
    std::fclose(f);
    return true;
}

// ---- Presets ---------------------------------------------------------------------

namespace {
struct Preset {
    int shadows, msaa;
    bool ao, bloom, clouds, grass;
};
const Preset kPresets[4] = {
    {1, 0, false, false, false, false},  // LOW
    {2, 2, false, true, true, true},     // MEDIUM
    {3, 4, true, true, true, true},      // HIGH
    {3, 8, true, true, true, true},      // ULTRA
};
const int kMsaa[4] = {0, 2, 4, 8};
int msaaIndex(int samples) {
    int best = 0;
    for (int k = 0; k < 4; k++)
        if (kMsaa[k] <= samples) best = k;
    return best;
}
}  // namespace

void applyPreset(RenderSettings& g, int preset) {
    const Preset& p = kPresets[std::min(std::max(preset, 0), 3)];
    g.shadows = p.shadows;
    g.msaa = p.msaa;
    g.ao = p.ao;
    g.bloom = p.bloom;
    g.clouds = p.clouds;
    g.grass = p.grass;
}

int presetOf(const RenderSettings& g) {
    for (int k = 0; k < 4; k++) {
        const Preset& p = kPresets[k];
        if (g.shadows == p.shadows && kMsaa[msaaIndex(g.msaa)] == p.msaa && g.ao == p.ao && g.bloom == p.bloom &&
            g.clouds == p.clouds && g.grass == p.grass)
            return k;
    }
    return 4;
}

// ---- Rows ------------------------------------------------------------------------

namespace {
enum Id {
    // Settings page.
    UNITS = 1, VOLUME, FOV, RECOVERY, MUSIC, TIMEOFDAY, FUEL,
    QUALITY = 10, SHADOWS, AA, AO, BLOOM, CLOUDS, GRASS, SCALE,
    FULLSCREEN = 20, VSYNC, FPS,
    // Main page and navigation.
    RESUME_ROW = 100, VEHICLE_ROW, MAP_ROW, GARAGE_ROW, SETTINGS_ROW, CONTROLS_ROW, RESET_ROW, QUIT_ROW, BACK_ROW,
    MULTI_ROW, NAME_ROW, HOST_ROW, ADDRESS_ROW, JOIN_ROW, LEAVE_ROW,
    PILOT_ROW, DEST_ROW, PSPEED_ROW, PROUTE_ROW, PGO_ROW, PSTOP_ROW,
    FREEDRIVE_ROW, LEVELS_ROW, LEVEL_PICK, LEVEL_PLAY, LEVEL_EDIT, EDITOR_ROW, LRESTART_ROW, LQUIT_ROW, MAINMENU_ROW,
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
                                                    : "TEST PAD, CRASH WALL, RAMPS, HILL ROADS";
    if (page == 0 && title) {
        const auto& cat = vehicleCatalog();
        button(FREEDRIVE_ROW, "FREE DRIVE", "THIS MAP, THIS VEHICLE:");
        choice(VEHICLE_ROW, "VEHICLE", cat[vehicle].name, (int)cat.size(), cat[vehicle].blurb);
        choice(MAP_ROW, "MAP", mapName(mapPick), MAP_COUNT, mapHint);
        button(LEVELS_ROW, "LEVELS", std::to_string(levels.size()) + " RACES, CLIMBS AND WORLDS");
        button(MULTI_ROW, "MULTIPLAYER", netActive ? netStatus : "HOST OR JOIN A GAME");
        button(EDITOR_ROW, "WORLD EDITOR", "BUILD YOUR OWN WORLDS AND LEVELS");
        button(GARAGE_ROW, "GARAGE", "ENGINE, SUSPENSION, GEARBOX");
        button(SETTINGS_ROW, "SETTINGS", "UNITS, GRAPHICS, SOUND");
        button(CONTROLS_ROW, "CONTROLS");
        button(QUIT_ROW, "QUIT GAME");
        return r;
    }
    if (page == 4) {
        header(levels.empty() ? "NO LEVELS" : (std::to_string(levels.size()) + " LEVELS").c_str());
        if (!levels.empty()) {
            const int k = std::min(std::max(levelPick, 0), (int)levels.size() - 1);
            choice(LEVEL_PICK, "LEVEL", levels[k].name, (int)levels.size(), levels[k].about);
            for (const std::string& line : levels[k].lines) r.push_back({HEADER, "", "", "", 0}), r.back().label = line, r.back().id = -1;
            button(LEVEL_PLAY, "PLAY", netClient ? "THE HOST PICKS THE LEVEL" : netActive ? "EVERYONE IN THE SESSION PLAYS IT" : "");
            if (!netClient) button(LEVEL_EDIT, "OPEN IN THE EDITOR", "CHANGE IT, OR START YOURS FROM IT");
        }
        button(BACK_ROW, "BACK");
        return r;
    }
    if (page == 0) {
        const auto& cat = vehicleCatalog();
        button(RESUME_ROW, "RESUME");
        if (levelActive) {
            button(LRESTART_ROW, "RESTART LEVEL", "OR R TWICE");
            button(LQUIT_ROW, "QUIT LEVEL", "BACK TO FREE DRIVE");
        }
        choice(VEHICLE_ROW, "VEHICLE", cat[vehicle].name, (int)cat.size(), cat[vehicle].blurb);
        choice(MAP_ROW, "MAP", mapName(mapPick), MAP_COUNT, mapHint);
        button(LEVELS_ROW, "LEVELS", levelActive ? "PICK ANOTHER" : "RACES, CLIMBS AND WORLDS");
        button(GARAGE_ROW, "GARAGE", "ENGINE, SUSPENSION, GEARBOX");
        button(PILOT_ROW, "AUTOPILOT", pilotActive ? pilotStatus : "DRIVE ITSELF SOMEWHERE");
        button(MULTI_ROW, "MULTIPLAYER", netActive ? netStatus : "HOST OR JOIN A GAME");
        button(SETTINGS_ROW, "SETTINGS", "UNITS, GRAPHICS, SOUND");
        button(CONTROLS_ROW, "CONTROLS");
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
        button(PGO_ROW, pilotActive ? "GO THERE INSTEAD" : "GO", "U ALSO STARTS AND STOPS IT");
        if (pilotActive) button(PSTOP_ROW, "STOP AUTOPILOT", "OR JUST STEER");
        button(BACK_ROW, "BACK");
        return r;
    }
    if (page == 2) {
        auto text = [&](int id, const char* label, const std::string& value, const std::string& hint) { r.push_back({TEXT, label, value, hint, id}); };
        header(netStatus.c_str());
        text(NAME_ROW, "YOUR NAME", s.playerName, "TYPE IT; ARROWS MOVE");
        if (!netActive) {
            button(HOST_ROW, "HOST A GAME", "ON THIS MAP, PORT 27015");
            text(ADDRESS_ROW, "HOST ADDRESS", s.joinAddress, "IP OR NAME, :PORT OPTIONAL");
            button(JOIN_ROW, "JOIN");
        } else {
            button(LEAVE_ROW, "LEAVE THE SESSION");
        }
        button(BACK_ROW, "BACK");
        return r;
    }
    static const char* quality[5] = {"LOW", "MEDIUM", "HIGH", "ULTRA", "CUSTOM"};
    static const char* shadows[4] = {"OFF", "LOW", "MEDIUM", "HIGH"};
    static const char* aa[4] = {"OFF", "2X MSAA", "4X MSAA", "8X MSAA"};
    header("GAME");
    choice(UNITS, "SPEED UNITS", s.mph ? "MPH" : "KM/H", 2);
    choice(RECOVERY, "RECOVERY", s.realRecovery ? "REAL" : "ARCADE", 2,
           s.realRecovery ? "DAMAGE STAYS: TOW OR DRIVE TO A REPAIR STATION" : "T FIXES EVERYTHING WHERE YOU ARE");
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
    slider(FOV, "FIELD OF VIEW", std::to_string((int)std::lround(s.fovDeg)) + " DEG", (s.fovDeg - 50) / 50, 26);
    header("GRAPHICS");
    choice(QUALITY, "QUALITY", quality[presetOf(s.gfx)], 4);
    choice(SHADOWS, "SHADOWS", shadows[std::min(std::max(s.gfx.shadows, 0), 3)], 4);
    choice(AA, "ANTI-ALIASING", aa[msaaIndex(s.gfx.msaa)], 4);
    choice(AO, "AMBIENT OCCLUSION", kOffOn[s.gfx.ao], 2);
    choice(BLOOM, "BLOOM", kOffOn[s.gfx.bloom], 2);
    choice(CLOUDS, "CLOUDS", kOffOn[s.gfx.clouds], 2);
    choice(GRASS, "GRASS", kOffOn[s.gfx.grass], 2);
    slider(SCALE, "RENDER SCALE", pct(s.gfx.renderScale), (s.gfx.renderScale - 0.5f) / 1.5f, 16);
    header("DISPLAY");
    choice(FULLSCREEN, "FULLSCREEN", kOffOn[s.fullscreen], 2);
    choice(VSYNC, "VSYNC", kOffOn[s.vsync], 2);
    choice(FPS, "SHOW FPS", kOffOn[s.showFps], 2);
    button(BACK_ROW, "BACK");
    return r;
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

void PauseMenu::show(int currentVehicle, int currentMap) {
    mapPick = currentMap;
    open = true;
    title = false;
    page = 0;
    sel = 0;
    fade = 0;
    dragSlider = -1;
    hits.clear();
    vehicle = std::max(0, currentVehicle);
}

void PauseMenu::moveSel(int dir, const Settings& s) {
    std::vector<Row> r = rows(s);
    const int n = (int)r.size();
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
        case LEVEL_PICK:
        case LEVEL_PLAY: return netClient ? Result{} : Result{PLAY_LEVEL, levelPick};
        case LEVEL_EDIT: return {EDIT_LEVEL, levelPick};
        case EDITOR_ROW: return {EDITOR};
        case LRESTART_ROW: return {LEVEL_RESTART};
        case LQUIT_ROW: return {LEVEL_QUIT};
        case MAINMENU_ROW: return {MAIN_MENU};
        case GARAGE_ROW: return {GARAGE};
        case SETTINGS_ROW: goPage(1, UNITS); return {};
        case MULTI_ROW: goPage(2, netActive ? LEAVE_ROW : HOST_ROW); return {};
        case PILOT_ROW: goPage(3, DEST_ROW); return {};
        case PGO_ROW:
        case DEST_ROW: return {PILOT_GO};
        case PSTOP_ROW: return {PILOT_STOP};
        case CONTROLS_ROW: return {CONTROLS};
        case RESET_ROW: return {RESET};
        case QUIT_ROW: return {QUIT};
        case BACK_ROW: goPage(0, page == 2 ? MULTI_ROW : page == 3 ? PILOT_ROW : page == 4 ? LEVELS_ROW : SETTINGS_ROW); return {};
        case HOST_ROW: return {HOST};
        case JOIN_ROW: return {JOIN};
        case LEAVE_ROW: return {LEAVE};
        case ADDRESS_ROW: return {JOIN};
    }
    if (it.kind == CHOICE) return change(row, 1, s);
    return {};
}

PauseMenu::Result PauseMenu::change(int row, int dir, Settings& s) {
    std::vector<Row> r = rows(s);
    if (row < 0 || row >= (int)r.size()) return {};
    const Row& it = r[row];
    if (it.kind == SLIDER) return setSlider(row, it.frac + (float)dir / (it.count - 1), s);
    if (it.kind != CHOICE) return {};
    auto cycle = [&](int cur) { return (cur + dir + it.count) % it.count; };
    RenderSettings& g = s.gfx;
    switch (it.id) {
        case VEHICLE_ROW: vehicle = cycle(vehicle); return {};
        case MAP_ROW: mapPick = cycle(mapPick); return {};
        case LEVEL_PICK: levelPick = cycle(std::min(std::max(levelPick, 0), it.count - 1)); return {};
        case UNITS: s.mph = !s.mph; break;
        case RECOVERY: s.realRecovery = !s.realRecovery; break;
        case FUEL: s.fuelMode = cycle(std::min(std::max(s.fuelMode, 0), 2)); break;
        case TIMEOFDAY: {
            const int t = cycle(todIndex(s));
            s.timeOfDay = t < 5 ? (int)TOD_CYCLE : t - 4;
            if (t < 5) s.dayMinutes = kDayLengths[t];
            break;
        }
        case DEST_ROW: s.pilotDest = cycle(std::min(std::max(s.pilotDest, 0), it.count - 1)); break;
        case PROUTE_ROW: s.pilotRoute = cycle(std::min(std::max(s.pilotRoute, 0), 2)); break;
        case QUALITY: {
            int cur = presetOf(g);
            applyPreset(g, cur == 4 ? (dir > 0 ? 3 : 0) : cycle(cur));
            break;
        }
        case SHADOWS: g.shadows = cycle(std::min(std::max(g.shadows, 0), 3)); break;
        case AA: g.msaa = kMsaa[cycle(msaaIndex(g.msaa))]; break;
        case AO: g.ao = !g.ao; break;
        case BLOOM: g.bloom = !g.bloom; break;
        case CLOUDS: g.clouds = !g.clouds; break;
        case GRASS: g.grass = !g.grass; break;
        case FULLSCREEN: s.fullscreen = !s.fullscreen; break;
        case VSYNC: s.vsync = !s.vsync; break;
        case FPS: s.showFps = !s.showFps; break;
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
        case FOV: s.fovDeg = 50 + 50 * frac; break;
        case SCALE: s.gfx.renderScale = 0.5f + 1.5f * frac; break;
        case PSPEED_ROW: s.pilotSpeed = 5 + 95 * frac; break;
        default: return {};
    }
    return {CHANGED};
}

PauseMenu::Result PauseMenu::key(int sc, Settings& s) {
    if (!open) return {};
    // Typing into a text field: letters, digits, '.', '-', ':' (shift + ';'); backspace.
    {
        std::vector<Row> r = rows(s);
        if (sel >= 0 && sel < (int)r.size() && r[sel].kind == TEXT) {
            std::string& field = r[sel].id == NAME_ROW ? s.playerName : s.joinAddress;
            char ch = 0;
            if (sc >= SC_A && sc <= SC_A + 25) ch = (char)('A' + (sc - SC_A));
            else if (sc >= SC_1 && sc <= SC_1 + 8) ch = (char)('1' + (sc - SC_1));
            else if (sc == SC_1 + 9) ch = '0';
            else if (sc == 55) ch = '.';
            else if (sc == 45) ch = shift ? '_' : '-';
            else if (sc == 51 && shift) ch = ':';
            if (r[sel].id == ADDRESS_ROW && ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
            if (ch && field.size() < (r[sel].id == NAME_ROW ? 16u : 60u)) {
                field += ch;
                return {CHANGED};
            }
            if (sc == SC_BACKSPACE && !field.empty()) {
                field.pop_back();
                return {CHANGED};
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

void PauseMenu::draw(Renderer& r, const Settings& s, int W, int H, double dt) {
    if (!open) return;
    fade = std::min(1.0f, fade + (float)dt * 7.0f);
    caret += (float)dt;
    const float a = fade;
    const float u = std::max(0.75f, H / 720.0f);
    const float ts = px(1.6f * u), small = px(1.2f * u), title = px((this->title && page == 0 ? 4.0f : 3.0f) * u);
    const float pad = 22 * u, rowH = 8 * ts + 14 * u, headH = 8 * small + 16 * u;
    std::vector<Row> rs = rows(s);

    // Darken the (blurred) scene (the title lets more of the world through).
    r.rect(0, 0, (float)W, (float)H, 0.0f, 0.01f, 0.02f, (this->title ? 0.2f : 0.35f) * a);

    float contentH = 0;
    for (const Row& row : rs)
        contentH += row.kind == HEADER ? headH : rowH + (row.kind == CHOICE && !row.hint.empty() ? 8 * small + 6 * u : 0);
    const float w = std::min((float)W - 40 * u, 640 * u);
    const float h = pad * 2 + 8 * title + 18 * u + contentH + 8 * small + 16 * u;
    const float x = (W - w) / 2, y = std::max(10 * u, (H - h) / 2);
    pill(r, x, y, w, h, 0.03f, 0.035f, 0.045f, 0.9f * a);
    r.rect(x + pad, y + pad + 8 * title + 8 * u, w - 2 * pad, 2 * u, 1.0f, 0.75f, 0.3f, 0.8f * a);
    label(r, x + pad, y + pad, title,
          page == 0 ? (this->title ? "CRUMPLE ZONE" : "PAUSED") : page == 1 ? "SETTINGS" : page == 2 ? "MULTIPLAYER" : page == 3 ? "AUTOPILOT" : "LEVELS",
          1.0f, 0.75f, 0.3f, a);
    if (page == 0) {
        const char* tag = this->title ? "SOFT-BODY DRIVING: CRASH IT, CRAWL IT, RACE IT" : "CRUMPLE ZONE";
        r.text(x + w - pad - r.textWidth(tag, small), y + pad + 8 * title - 8 * small, small, tag, 0.55f, 0.57f, 0.62f, a);
    }

    hits.clear();
    const float valueW = 250 * u, valueX = x + w - pad - valueW;
    float cy = y + pad + 8 * title + 18 * u;
    for (int k = 0; k < (int)rs.size(); k++) {
        const Row& row = rs[k];
        if (row.kind == HEADER) {
            if (row.id == -1)  // an information line (the levels page)
                r.text(x + pad + 6 * u, cy + headH - 8 * small - 6 * u, small, row.label, 0.82f, 0.84f, 0.88f, 0.95f * a);
            else
                r.text(x + pad, cy + headH - 8 * small - 6 * u, small, row.label, 1.0f, 0.75f, 0.3f, 0.85f * a);
            cy += headH;
            continue;
        }
        const bool hasHint = row.kind == CHOICE && !row.hint.empty();
        const float hh = rowH + (hasHint ? 8 * small + 6 * u : 0);
        const bool on = k == sel;
        if (on) {
            r.rect(x + pad * 0.5f, cy, w - pad, hh, 1, 1, 1, 0.07f * a);
            r.rect(x + pad * 0.5f, cy, 3 * u, hh, 1.0f, 0.75f, 0.3f, a);
        }
        const float ty = cy + (rowH - 8 * ts) / 2;
        const float lr = on ? 1.0f : 0.86f, lg = on ? 1.0f : 0.87f, lb = on ? 1.0f : 0.9f;
        r.text(x + pad + 6 * u, ty, ts, row.label, lr, lg, lb, a);
        Hit hit{x + pad * 0.5f, cy, w - pad, hh, valueX, valueW, k};
        if (row.kind == BUTTON && !row.hint.empty())
            r.text(x + w - pad - r.textWidth(row.hint, small), cy + (rowH - 8 * small) / 2, small, row.hint, 0.55f, 0.57f,
                   0.62f, a);
        if (row.kind == CHOICE) {
            r.rect(valueX, cy + 5 * u, valueW, rowH - 10 * u, 0, 0, 0, 0.35f * a);
            const float ar = on ? 1.0f : 0.6f, ag = on ? 0.75f : 0.6f, ab = on ? 0.3f : 0.62f;
            r.text(valueX + 8 * u, ty, ts, "<", ar, ag, ab, a);
            r.text(valueX + valueW - 8 * u - r.textWidth(">", ts), ty, ts, ">", ar, ag, ab, a);
            r.text(valueX + (valueW - r.textWidth(row.value, ts)) / 2, ty, ts, row.value, 1, 1, 1, a);
            if (hasHint) {
                std::string hint = row.hint;
                const float maxW = w - 2 * pad - 12 * u;
                while (!hint.empty() && r.textWidth(hint, small) > maxW) hint.pop_back();
                r.text(x + pad + 6 * u, cy + rowH, small, hint, 0.6f, 0.62f, 0.66f, a);
            }
            if ((row.id == VEHICLE_ROW || row.id == MAP_ROW || row.id == DEST_ROW || row.id == LEVEL_PICK) && on && !this->title) {
                const char* go = row.id == MAP_ROW ? "ENTER TO LOAD" : row.id == DEST_ROW ? "ENTER TO GO" : row.id == LEVEL_PICK ? "ENTER TO PLAY" : "ENTER TO DRIVE";
                r.text(valueX - r.textWidth(go, small) - 12 * u, cy + (rowH - 8 * small) / 2, small, go, 1.0f, 0.75f, 0.3f, a);
            }
        }
        if (row.kind == TEXT) {
            r.rect(valueX, cy + 5 * u, valueW, rowH - 10 * u, 0, 0, 0, 0.5f * a);
            std::string v = row.value;
            while (!v.empty() && r.textWidth(v, ts) > valueW - 20 * u) v.erase(v.begin());
            r.text(valueX + 8 * u, ty, ts, v, 1, 1, 1, a);
            if (on && std::fmod(caret, 1.0f) < 0.55f)
                r.rect(valueX + 9 * u + r.textWidth(v, ts), cy + 9 * u, 2 * u, rowH - 18 * u, 1.0f, 0.75f, 0.3f, a);
            if (on) r.text(valueX - r.textWidth(row.hint, small) - 12 * u, cy + (rowH - 8 * small) / 2, small, row.hint, 0.6f, 0.62f, 0.66f, a);
        }
        if (row.kind == SLIDER) {
            const float textW = 70 * u, barW = valueW - textW - 8 * u, barY = cy + rowH / 2 - 3 * u;
            r.rect(valueX, barY, barW, 6 * u, 1, 1, 1, 0.15f * a);
            r.rect(valueX, barY, barW * row.frac, 6 * u, 1.0f, 0.75f, 0.3f, 0.9f * a);
            r.rect(valueX + barW * row.frac - 3 * u, barY - 5 * u, 6 * u, 16 * u, on ? 1.0f : 0.85f, on ? 1.0f : 0.85f,
                   on ? 1.0f : 0.88f, a);
            r.text(valueX + valueW - r.textWidth(row.value, ts), ty, ts, row.value, 1, 1, 1, a);
            hit.barW = barW;
        }
        hits.push_back(hit);
        cy += hh;
    }
    const char* keys = page == 0 && this->title ? "W S SELECT   A D CHANGE   ENTER OK"
                       : page == 0                ? "W S SELECT   A D CHANGE   ENTER OK   ESC RESUME"
                                 : "W S SELECT   A D CHANGE   ENTER OK   ESC BACK";
    r.text(x + (w - r.textWidth(keys, small)) / 2, y + h - pad - 8 * small + 6 * u, small, keys, 0.55f, 0.57f, 0.62f, a);
}
