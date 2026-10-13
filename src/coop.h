// Playing together: pings ("go here", "help, I'm stuck"), your own waypoint, text chat, name
// tags over your buddies' trucks, and the big map (M) with everyone on it.
//
// What the others need to see goes out as short text over the session's chatter channel
// (NetSession::sendGame): "ping KIND X Z", "chat TEXT". Offline, pings and the map still work
// for you.
#pragma once
#include <string>
#include <vector>

#include "net.h"
#include "render.h"
#include "softbody.h"

struct Ping {
    int kind = 0;          // 0 GO HERE, 1 HELP (stuck), 2 LOOK
    V3 p;
    std::string who;
    double age = 0;
};

struct RemoteWalker {
    std::string who;
    V3 p;
    double yaw = 0, stride = 0, age = 0;
};

struct ChatLine {
    std::string who, text;
    double age = 0;
};

class Coop {
public:
    std::vector<Ping> pings;
    std::vector<ChatLine> chat;
    std::vector<RemoteWalker> walkers;  // buddies on foot
    bool haveWaypoint = false;
    V3 waypoint;
    // Typing a chat line.
    bool typing = false;
    std::string draft;
    // The map.
    bool mapOpen = false;

    void update(double dt);
    // A chatter line from the session; true if it was ours (a ping or chat).
    bool onMessage(const std::string& who, const std::string& text);
    // Drops a ping (and tells the others).
    void ping(int kind, const V3& p, const std::string& me, NetSession& net);
    void say(const std::string& text, const std::string& me, NetSession& net);
    // Chat typing: a key while typing (true: used). Enter sends.
    bool typeKey(int scancode, bool shift, const std::string& me, NetSession& net);

    // The map: rebuilt for each world (colors from its ground, water and paint).
    void buildMap(const World& w);
    void mapMouse(float x, float y, int wheel, bool drag, float dx, float dy);
    // A click on the map: the world point under it (false: off the map).
    bool mapPoint(float x, float y, V3& out) const;

    // Drawing (HUD pixels): name tags, pings and waypoint in the world; the chat box; the map.
    void drawWorld(Renderer& r, const Sim& sim, const NetSession& net, const View& view, int W, int H) const;
    void drawChat(Renderer& r, int W, int H) const;
    // A marker in the world (or an arrow at the screen's edge pointing to it).
    void drawMarker(Renderer& r, const Sim& sim, const View& view, int W, int H, const V3& at, const std::string& text, const float col[3], double age = 0) const;
    void drawMap(Renderer& r, const Sim& sim, const NetSession& net, int W, int H, const std::vector<std::pair<std::string, V3>>& places);

    // Exploring: the places on each map you've found (kept between sessions).
    std::vector<std::string> discovered;  // "MAP:NAME"
    bool isDiscovered(int map, const std::string& name) const;
    // Marks places within 30 m of `p` found; returns the newly found names.
    std::vector<std::string> discover(int map, const V3& p, const std::vector<std::pair<std::string, V3>>& places);
    void loadDiscoveries(const std::string& path);
    void saveDiscoveries(const std::string& path) const;
    int mapNow = 0;  // (set by the game: which map the places are on)

private:
    int mapN = 0;
    double mapCell = 8;  // m per map pixel
    std::vector<float> mapRgb;
    double mapCx = 0, mapCz = 0, mapZoom = 1;  // view: center (world), zoom (1 = the whole map fits)
    float mapX = 0, mapY = 0, mapS = 1;        // where the map was last drawn (HUD px, px per meter)
    int mapWorld = -1;
};

// Where world point p lands on the screen; false behind the camera.
bool worldToScreen(const View& v, int W, int H, const V3& p, float& sx, float& sy);
// The ray from the camera through screen point (sx, sy).
void screenRay(const View& v, int W, int H, float sx, float sy, V3& o, V3& d);
