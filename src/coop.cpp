#include "coop.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "hudkit.h"
#include "sdl_min.h"

using namespace hud;

namespace {
const char* kPingNames[3] = {"GO HERE", "HELP", "LOOK"};
const float kPingCol[3][3] = {{0.35f, 0.85f, 1.0f}, {1.0f, 0.35f, 0.3f}, {1.0f, 0.85f, 0.35f}};
constexpr double kPingLife = 45, kChatLife = 20;

std::string distText(double d) {
    char b[32];
    if (d > 950) std::snprintf(b, sizeof b, "%.1f KM", d / 1000);
    else std::snprintf(b, sizeof b, "%.0f M", d);
    return b;
}
}  // namespace

bool worldToScreen(const View& v, int W, int H, const V3& p, float& sx, float& sy) {
    const V3 f = norm(v.target - v.eye), rt = norm(cross(f, v.up)), up = cross(rt, f);
    const V3 d = p - v.eye;
    const double z = dot(d, f);
    if (z < 0.3) return false;
    const double k = 1.0 / std::tan(v.fovy / 2);
    sx = (float)(W / 2.0 + dot(d, rt) / z * k * H / 2.0);
    sy = (float)(H / 2.0 - dot(d, up) / z * k * H / 2.0);
    return true;
}

void screenRay(const View& v, int W, int H, float sx, float sy, V3& o, V3& d) {
    const V3 f = norm(v.target - v.eye), rt = norm(cross(f, v.up)), up = cross(rt, f);
    const double k = std::tan(v.fovy / 2);
    o = v.eye;
    d = norm(f + rt * ((sx - W / 2.0) / (H / 2.0) * k) + up * ((H / 2.0 - sy) / (H / 2.0) * k));
}

// ---- Messages ------------------------------------------------------------------------------

void Coop::update(double dt) {
    for (Ping& p : pings) p.age += dt;
    pings.erase(std::remove_if(pings.begin(), pings.end(), [](const Ping& p) { return p.age > kPingLife; }), pings.end());
    for (ChatLine& c : chat) c.age += dt;
    for (RemoteWalker& w : walkers) w.age += dt;
    walkers.erase(std::remove_if(walkers.begin(), walkers.end(), [](const RemoteWalker& w) { return w.age > 2.0; }), walkers.end());
    if (chat.size() > 40) chat.erase(chat.begin(), chat.begin() + (chat.size() - 40));
}

bool Coop::onMessage(const std::string& who, const std::string& text) {
    std::istringstream in(text);
    std::string tag;
    in >> tag;
    if (tag == "ping") {
        Ping p;
        in >> p.kind >> p.p.x >> p.p.y >> p.p.z;
        p.kind = std::min(std::max(p.kind, 0), 2);
        p.who = who;
        // One ping per player: a new one replaces their old.
        pings.erase(std::remove_if(pings.begin(), pings.end(), [&](const Ping& q) { return q.who == who; }), pings.end());
        pings.push_back(p);
        chat.push_back({who, std::string("PINGED: ") + kPingNames[p.kind]});
        return true;
    }
    if (tag == "walk" || tag == "inside") {
        auto it = std::find_if(walkers.begin(), walkers.end(), [&](const RemoteWalker& w) { return w.who == who; });
        if (tag == "inside") {
            if (it != walkers.end()) walkers.erase(it);
            return true;
        }
        if (it == walkers.end()) walkers.push_back(RemoteWalker{who}), it = walkers.end() - 1;
        in >> it->p.x >> it->p.y >> it->p.z >> it->yaw >> it->stride;
        it->age = 0;
        return true;
    }
    if (tag == "chat") {
        std::string rest;
        std::getline(in, rest);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        chat.push_back({who, rest});
        return true;
    }
    return false;
}

void Coop::ping(int kind, const V3& p, const std::string& me, NetSession& net) {
    char b[96];
    std::snprintf(b, sizeof b, "ping %d %.1f %.1f %.1f", kind, p.x, p.y, p.z);
    onMessage(me, b);
    net.sendGame(b);
}

void Coop::say(const std::string& text, const std::string& me, NetSession& net) {
    if (text.empty()) return;
    chat.push_back({me, text});
    net.sendGame("chat " + text.substr(0, 200));
}

bool Coop::typeKey(int sc, bool shift, const std::string& me, NetSession& net) {
    if (!typing) return false;
    char ch = 0;
    if (sc >= SC_A && sc <= SC_A + 25) ch = (char)('A' + (sc - SC_A));
    else if (sc >= SC_1 && sc <= SC_1 + 8) ch = shift && sc == SC_1 ? '!' : (char)('1' + (sc - SC_1));
    else if (sc == SC_0) ch = '0';
    else if (sc == SC_SPACE) ch = ' ';
    else if (sc == SC_MINUS) ch = '-';
    else if (sc == 55) ch = '.';
    else if (sc == 54) ch = ',';
    else if (sc == 56) ch = shift ? '?' : '/';
    else if (sc == 52) ch = '\'';
    if (ch && draft.size() < 120) draft += ch;
    else if (sc == SC_BACKSPACE && !draft.empty()) draft.pop_back();
    else if (sc == SC_RETURN || sc == SC_KP_ENTER) {
        say(draft, me, net);
        draft.clear();
        typing = false;
    } else if (sc == SC_ESCAPE) {
        draft.clear();
        typing = false;
    }
    return true;
}

// ---- The map -------------------------------------------------------------------------------

void Coop::buildMap(const World& w) {
    mapWorld = (int)(w.stamp & 0x7fffffff);
    const double half = World::kHalfSize - 20;
    mapCell = 6;
    mapN = (int)(2 * half / mapCell);
    mapRgb.assign((size_t)mapN * mapN * 3, 0);
    for (int j = 0; j < mapN; j++)
        for (int i = 0; i < mapN; i++) {
            const double x = -half + (i + 0.5) * mapCell, z = -half + (j + 0.5) * mapCell;
            const double h = w.height(x, z);
            const Surface s = w.surfaceAt(x, z);
            const uint8_t paint = w.roadAt(x, z).paint;
            float c[3];
            // Ground by surface (paint where there's a track drawn on it).
            switch (s) {
                case SURF_PAVED: c[0] = 0.55f, c[1] = 0.55f, c[2] = 0.56f; break;
                case SURF_WET: c[0] = 0.35f, c[1] = 0.37f, c[2] = 0.4f; break;
                case SURF_DIRT: c[0] = 0.6f, c[1] = 0.46f, c[2] = 0.3f; break;
                case SURF_ROCK: c[0] = 0.55f, c[1] = 0.5f, c[2] = 0.46f; break;
                case SURF_SCREE: c[0] = 0.62f, c[1] = 0.6f, c[2] = 0.56f; break;
                case SURF_GRAVEL: c[0] = 0.66f, c[1] = 0.63f, c[2] = 0.58f; break;
                case SURF_SAND: c[0] = 0.86f, c[1] = 0.74f, c[2] = 0.52f; break;
                case SURF_MUD: c[0] = 0.36f, c[1] = 0.27f, c[2] = 0.18f; break;
                case SURF_SNOW: c[0] = 0.93f, c[1] = 0.95f, c[2] = 0.98f; break;
                case SURF_ICE: c[0] = 0.72f, c[1] = 0.86f, c[2] = 0.95f; break;
                default: c[0] = 0.33f, c[1] = 0.47f, c[2] = 0.24f; break;
            }
            if (paint == PAINT_TRACK || paint == PAINT_FREEWAY || paint == PAINT_ROAD) c[0] = 0.2f, c[1] = 0.2f, c[2] = 0.21f;
            else if (paint == PAINT_TRAIL) c[0] = 0.72f, c[1] = 0.55f, c[2] = 0.33f;
            else if (paint == PAINT_REDROCK) c[0] = 0.72f, c[1] = 0.4f, c[2] = 0.27f;
            // Hillshade (light from the north-west) and a little height tint.
            const double e = mapCell;
            const double dx = w.height(x + e, z) - w.height(x - e, z), dz = w.height(x, z + e) - w.height(x, z - e);
            const double shade = clampd(1.0 + (-dx * 0.7 + dz * 0.7) / (2 * e) * 1.4, 0.45, 1.35);
            for (float& v : c) v = (float)clampd(v * shade * (0.92 + 0.0006 * h), 0, 1);
            if (w.waterLevel(x, z) > h + 0.05) {
                const double depth = w.waterLevel(x, z) - h;
                const float k = (float)clampd(depth / 2.5, 0.25, 1.0);
                c[0] = c[0] * (1 - k) + 0.15f * k, c[1] = c[1] * (1 - k) + 0.35f * k, c[2] = c[2] * (1 - k) + 0.55f * k;
            }
            float* o = &mapRgb[((size_t)j * mapN + i) * 3];
            o[0] = c[0], o[1] = c[1], o[2] = c[2];
        }
    mapZoom = 1;
    mapCx = mapCz = 0;
}

void Coop::mapMouse(float x, float y, int wheel, bool drag, float dx, float dy) {
    if (wheel) {
        // Zoom about the point under the mouse.
        V3 before;
        const bool on = mapPoint(x, y, before);
        mapZoom = clampd(mapZoom * std::pow(1.25, wheel), 1.0, 12.0);
        if (on) {
            mapS = mapS * (float)std::pow(1.25, wheel);
            V3 after;
            if (mapPoint(x, y, after)) mapCx += before.x - after.x, mapCz += before.z - after.z;
        }
    }
    if (drag && mapS > 0) mapCx -= dx / mapS, mapCz += dy / mapS;
    const double half = World::kHalfSize;
    mapCx = clampd(mapCx, -half, half), mapCz = clampd(mapCz, -half, half);
}

bool Coop::mapPoint(float x, float y, V3& out) const {
    if (mapS <= 0) return false;
    out = V3(mapCx + (x - mapX) / mapS, 0, mapCz - (y - mapY) / mapS);
    return std::fabs(out.x) < World::kHalfSize && std::fabs(out.z) < World::kHalfSize;
}

// ---- Drawing -------------------------------------------------------------------------------

void Coop::drawWorld(Renderer& r, const Sim& sim, const NetSession& net, const View& view, int W, int H) const {
    const float u = std::max(0.75f, H / 720.0f), s = px(1.2f * u);
    const V3 me = sim.cars[sim.player].frame().com;
    // Buddies: name and distance over each other player's truck.
    if (net.active())
        for (const Car& c : sim.cars) {
            if (!c.remote || c.isProp()) continue;
            const V3 p = c.frame().com + V3(0, 2.4, 0);
            float sx, sy;
            if (!worldToScreen(view, W, H, p, sx, sy)) continue;
            const std::string t = net.ownerName(c.netId) + "  " + distText(len(c.frame().com - me));
            label(r, sx - r.textWidth(t, s) / 2, sy - 8 * s, s, t, 0.6f, 0.95f, 0.7f, 0.95f);
        }
    for (const RemoteWalker& wk : walkers) {
        float sx, sy;
        if (worldToScreen(view, W, H, wk.p + V3(0, 2.2, 0), sx, sy)) {
            const std::string t = wk.who + " (ON FOOT)  " + distText(len(wk.p - me));
            label(r, sx - r.textWidth(t, s) / 2, sy - 8 * s, s, t, 0.6f, 0.95f, 0.7f, 0.95f);
        }
    }
    // Pings and the waypoint: a marker in the world, or an arrow at the screen's edge.
    auto marker = [&](const V3& at, const std::string& t, const float* col, double age) { drawMarker(r, sim, view, W, H, at, t, col, age); };
    for (const Ping& p : pings) marker(p.p, p.who + ": " + kPingNames[p.kind], kPingCol[p.kind], p.age);
    if (haveWaypoint) {
        static const float wc[3] = {0.95f, 0.6f, 1.0f};
        marker(waypoint, "WAYPOINT", wc, 0);
    }
}

void Coop::drawMarker(Renderer& r, const Sim& sim, const View& view, int W, int H, const V3& at, const std::string& t, const float col[3], double age) const {
    const float u = std::max(0.75f, H / 720.0f);
    const V3 me = sim.cars[sim.player].frame().com;
    const V3 p(at.x, sim.world.height(at.x, at.z) + 3, at.z);
    const std::string text = t + "  " + distText(len(V3(at.x, me.y, at.z) - me));
    const float a = (float)clampd((kPingLife - age) / 5, 0.2, 1.0);
    float sx, sy;
    const float ms = px(1.3f * u);
    if (worldToScreen(view, W, H, p, sx, sy) && sx > 0 && sx < W && sy > 0 && sy < H) {
        const float pulse = 1 + 0.15f * (float)std::sin(age * 6);
        r.tri(sx - 10 * u * pulse, sy - 16 * u * pulse, sx + 10 * u * pulse, sy - 16 * u * pulse, sx, sy, col[0], col[1], col[2], a);
        label(r, sx - r.textWidth(text, ms) / 2, sy - 34 * u, ms, text, col[0], col[1], col[2], a);
    } else {
        const V3 f = norm(V3(view.target.x - view.eye.x, 0, view.target.z - view.eye.z));
        const V3 d = norm(V3(p.x - view.eye.x, 0, p.z - view.eye.z));
        const double ang = std::atan2(f.x * d.z - f.z * d.x, dot(f, d));
        const float ax = W / 2.0f + (float)clampd(ang / M_PI, -1, 1) * (W / 2.0f - 70 * u), ay = 150 * u;
        const float dir = ang > 0 ? 1.0f : -1.0f;
        r.tri(ax + dir * 14 * u, ay, ax - dir * 6 * u, ay - 10 * u, ax - dir * 6 * u, ay + 10 * u, col[0], col[1], col[2], a);
        label(r, ax - r.textWidth(text, ms) / 2, ay + 14 * u, ms, text, col[0], col[1], col[2], a);
    }
}

void Coop::drawChat(Renderer& r, int W, int H) const {
    const float u = std::max(0.75f, H / 720.0f), s = px(1.2f * u), lh = 8 * s + 5 * u;
    const float x = 14 * u;
    float y = H - 250 * u;
    int shown = 0;
    for (int k = (int)chat.size() - 1; k >= 0 && shown < 7; k--) {
        const ChatLine& c = chat[k];
        if (c.age > kChatLife && !typing) continue;
        shown++;
    }
    y -= lh * shown;
    for (int k = (int)chat.size() - shown; k < (int)chat.size(); k++) {
        if (k < 0) continue;
        const ChatLine& c = chat[k];
        const float a = typing ? 1.0f : (float)clampd((kChatLife - c.age) / 3, 0, 1);
        if (a <= 0) continue;
        const std::string who = c.who + ": ";
        r.rect(x - 4 * u, y - 2 * u, r.textWidth(who + c.text, s) + 8 * u, lh, 0, 0, 0, 0.45f * a);
        r.text(x, y, s, who, 0.6f, 0.95f, 0.7f, a);
        r.text(x + r.textWidth(who, s), y, s, c.text, 0.95f, 0.95f, 0.97f, a);
        y += lh;
    }
    if (typing) {
        const std::string t = "SAY: " + draft + "_";
        pill(r, x - 6 * u, y + 2 * u, std::max(300 * u, r.textWidth(t, s) + 16 * u), lh + 6 * u, 0, 0, 0, 0.7f);
        r.text(x, y + 5 * u, s, t, 1.0f, 0.9f, 0.6f, 1);
    }
}

void Coop::drawMap(Renderer& r, const Sim& sim, const NetSession& net, int W, int H, const std::vector<std::pair<std::string, V3>>& places) {
    if (mapWorld != (int)(sim.world.stamp & 0x7fffffff) || mapN == 0) buildMap(sim.world);
    const float u = std::max(0.75f, H / 720.0f), s = px(1.2f * u);
    r.rect(0, 0, (float)W, (float)H, 0.02f, 0.03f, 0.04f, 0.92f);
    // Fit the whole map in the window at zoom 1.
    const float side = std::min((float)W - 40 * u, (float)H - 90 * u);
    const double half = World::kHalfSize - 20;
    mapS = (float)(side / (2 * half) * mapZoom);
    mapX = W / 2.0f, mapY = H / 2.0f + 10 * u;
    auto toMap = [&](double x, double z, float& sx, float& sy) {
        sx = mapX + (float)((x - mapCx) * mapS), sy = mapY - (float)((z - mapCz) * mapS);  // north (+z) up
    };
    // The ground (one rect per map pixel on screen; skip what's off screen).
    const float cellPx = (float)(mapCell * mapS) + 0.6f;
    for (int j = 0; j < mapN; j++) {
        const double z = -half + (j + 0.5) * mapCell;
        float sx, sy;
        toMap(-half, z, sx, sy);
        if (sy < -cellPx || sy > H) continue;
        for (int i = 0; i < mapN; i++) {
            const double x = -half + (i + 0.5) * mapCell;
            toMap(x, z, sx, sy);
            if (sx < -cellPx || sx > W) continue;
            const float* c = &mapRgb[((size_t)j * mapN + i) * 3];
            r.rect(sx - cellPx / 2, sy - cellPx / 2, cellPx, cellPx, c[0], c[1], c[2], 1);
        }
    }
    auto pin = [&](const V3& p, const std::string& t, float cr, float cg, float cb, float size) {
        float sx, sy;
        toMap(p.x, p.z, sx, sy);
        if (sx < 0 || sx > W || sy < 0 || sy > H) return;
        r.rect(sx - size / 2, sy - size / 2, size, size, 0, 0, 0, 0.8f);
        r.rect(sx - size / 2 + 1.5f * u, sy - size / 2 + 1.5f * u, size - 3 * u, size - 3 * u, cr, cg, cb, 1);
        if (!t.empty()) label(r, sx + size, sy - 4 * s, s, t, cr, cg, cb, 1);
    };
    for (const Station& st : sim.world.stations) pin(st.p, st.name, 0.55f, 1.0f, 0.55f, 9 * u);
    for (const auto& pl : places) pin(pl.second, pl.first, 1.0f, 0.9f, 0.6f, 7 * u);
    for (const Ping& p : pings) pin(p.p, p.who + ": " + kPingNames[p.kind], kPingCol[p.kind][0], kPingCol[p.kind][1], kPingCol[p.kind][2], 12 * u);
    if (haveWaypoint) pin(waypoint, "WAYPOINT", 0.95f, 0.6f, 1.0f, 12 * u);
    // Everyone's trucks: an arrow the way it points, and a name.
    for (int i = 0; i < (int)sim.cars.size(); i++) {
        const Car& c = sim.cars[i];
        if (c.isProp()) continue;
        const Frame f = c.frame();
        float sx, sy;
        toMap(f.com.x, f.com.z, sx, sy);
        const V3 d = norm(V3(f.fwd.x, 0, f.fwd.z)), l(-d.z, 0, d.x);
        const float a = 11 * u, b = 6 * u;
        const bool mine = i == sim.player;
        const float cr = mine ? 1.0f : c.remote ? 0.5f : 0.85f, cg = mine ? 0.8f : c.remote ? 1.0f : 0.85f, cb = mine ? 0.3f : c.remote ? 0.6f : 0.85f;
        r.tri(sx + (float)d.x * a, sy - (float)d.z * a, sx - (float)d.x * b + (float)l.x * b, sy + (float)d.z * b - (float)l.z * b,
              sx - (float)d.x * b - (float)l.x * b, sy + (float)d.z * b + (float)l.z * b, cr, cg, cb, 1);
        if (mine) label(r, sx + 12 * u, sy - 4 * s, s, "YOU", cr, cg, cb, 1);
        else if (c.remote) label(r, sx + 12 * u, sy - 4 * s, s, net.ownerName(c.netId), cr, cg, cb, 1);
    }
    // Title and keys.
    label(r, 20 * u, 14 * u, px(2.2f * u), "MAP", 1.0f, 0.75f, 0.3f, 1);
    const char* keys = "WHEEL ZOOM   DRAG PAN   LEFT CLICK: WAYPOINT   RIGHT CLICK: PING GO HERE   SHIFT+RIGHT: PING HELP   M CLOSE";
    r.text(W / 2.0f - r.textWidth(keys, s) / 2, H - 24 * u, s, keys, 0.75f, 0.77f, 0.8f, 1);
}
