// Car paint (paint.h): the PAINT page in the garage shop, saving with the garage (old files load with factory colours),
// and everyone in a session seeing each other's colours, also when one is changed after joining.
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

#include "../src/garageshop.h"
#include "../src/net.h"
#include "harness.h"

using namespace shop;

namespace {

int vehicleIndex(const char* id) {
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        if (std::string(vehicleCatalog()[v].id) == id) return v;
    return -1;
}
int paintCat() {
    for (int i = 0; i < (int)categories().size(); i++)
        if (categories()[i].group == G_PAINT) return i;
    return -1;
}
std::string tmpFile(const char* name) { return std::string("/tmp/cz_carpaint_") + std::to_string(getpid()) + "_" + name; }

CarPaint painted(int r, int g, int b, int ar, int ag, int ab, int finish) {
    CarPaint p;
    p.setBody(r / 255.0, g / 255.0, b / 255.0);
    p.setAccent(ar / 255.0, ag / 255.0, ab / 255.0);
    p.finish = (uint8_t)finish;
    return p;
}

}  // namespace

SCENARIO(paintCodecs, "paint/codecs", "Colours survive the file text and the wire exactly; HSV converts both ways; there are 16 distinct presets") {
    const CarPaint p = painted(12, 200, 99, 255, 0, 17, PF_METALLIC);
    CarPaint q;
    ctx.check("text round trip", paintFromText(paintToText(p).substr(0, 6), paintToText(p).substr(7, 6), PF_METALLIC, q) && q == p);
    uint8_t wire[kPaintWire];
    paintToWire(p, wire);
    CarPaint w;
    ctx.check("wire round trip", paintFromWire(wire, w) && w == p);
    CarPaint f;
    paintToWire(f, wire);
    ctx.check("factory is factory on the wire", paintFromWire(wire, w) && w.factory());
    wire[0] = 3 << 2;
    ctx.check("an unknown finish is refused", !paintFromWire(wire, w));
    ctx.check("junk text is refused", !paintFromText("zzzzzz", "-", 0, q) && !paintFromText("12345", "-", 0, q));
    double worst = 0;
    for (int r = 0; r < 256; r += 15)
        for (int g = 0; g < 256; g += 15)
            for (int b = 0; b < 256; b += 15) {
                double h, s, v, R, G, B;
                rgbToHsv(r / 255.0, g / 255.0, b / 255.0, h, s, v);
                hsvToRgb(h, s, v, R, G, B);
                worst = std::max({worst, std::fabs(R - r / 255.0), std::fabs(G - g / 255.0), std::fabs(B - b / 255.0)});
            }
    ctx.check("HSV round trip", worst < 1e-9);
    const auto& pr = paintPresets();
    ctx.check("16 presets", pr.size() == 16);
    bool distinct = true;
    for (size_t i = 0; i < pr.size(); i++)
        for (size_t j = i + 1; j < pr.size(); j++) distinct &= !(pr[i].rgb[0] == pr[j].rgb[0] && pr[i].rgb[1] == pr[j].rgb[1] && pr[i].rgb[2] == pr[j].rgb[2]);
    ctx.check("all different", distinct);
}

SCENARIO(paintGarageFile, "paint/garage-file", "Paint saves per vehicle with the garage (sandbox and career files); a garage file from before paint loads with factory colours; mods are untouched; older builds can read the new file") {
    const int a = vehicleIndex("bronco"), b = vehicleIndex("4runner");
    for (const char* name : {"garage.txt", "garage-career.txt"}) {
        const std::string path = tmpFile(name);
        Garage g;
        g.fitted[a].pick[MOD_ENGINE] = 1;
        g.paint[a] = painted(196, 30, 36, 20, 20, 22, PF_MATTE);
        CarPaint bodyOnly;
        bodyOnly.setBody(0.1, 0.5, 0.9);
        g.paint[b] = bodyOnly;
        ctx.check(std::string(name) + ": saves", g.save(path));
        Garage h;
        ctx.check(std::string(name) + ": loads", h.load(path));
        ctx.check(std::string(name) + ": paint is back", h.paint[a] == g.paint[a] && h.paint[b] == g.paint[b]);
        ctx.check(std::string(name) + ": unpainted vehicles stay factory", h.paint[0].factory() && h.paint[vehicleIndex("civic")].factory());
        ctx.check(std::string(name) + ": the mods are too", h.fitted[a].pick[MOD_ENGINE] == g.fitted[a].pick[MOD_ENGINE]);
        // An older build's parser: "id n n n [n n]" per line; the paint lines must not parse as a vehicle.
        FILE* f = std::fopen(path.c_str(), "r");
        char line[256];
        int paintLines = 0;
        bool oldParses = false;
        while (f && std::fgets(line, sizeof line, f)) {
            char id[64];
            int p[5];
            if (std::strncmp(line, "paint ", 6) == 0) {
                paintLines++;
                oldParses |= std::sscanf(line, "%63s %d %d %d %d %d", id, &p[0], &p[1], &p[2], &p[3], &p[4]) >= 4;
            }
        }
        if (f) std::fclose(f);
        ctx.check(std::string(name) + ": an older build skips the paint lines", paintLines == 2 && !oldParses);
        std::remove(path.c_str());
    }
    // A file from before paint.
    const std::string old = tmpFile("old.txt");
    FILE* f = std::fopen(old.c_str(), "w");
    std::fprintf(f, "bronco 1 0 0 0 0\n4runner 0 0 0\ncivic 0 0 0 0 0\n");
    std::fclose(f);
    Garage g;
    ctx.check("old file loads", g.load(old) && g.fitted[a].pick[MOD_ENGINE] == 1);
    bool factory = true;
    for (const CarPaint& p : g.paint) factory &= p.factory();
    ctx.check("with factory colours everywhere", factory);
    // Junk paint lines are ignored, not fatal.
    f = std::fopen(old.c_str(), "a");
    std::fprintf(f, "paint bronco nothex - 0\npaint nosuchcar ff0000 - 1\npaint bronco\n");
    std::fclose(f);
    Garage j;
    ctx.check("junk paint lines change nothing", j.load(old) && j.paint[a].factory());
    std::remove(old.c_str());
}

SCENARIO(paintShop, "paint/shop", "The PAINT page: pick a preset, change hue, saturation and brightness, the accent, the finish and the factory reset by keyboard and by mouse; the truck wears it live, and it is free in a career") {
    const int v = vehicleIndex("bronco");
    ctx.add(vehicleCatalog()[v], -60, -50, 0);
    ctx.sim.player = 0;
    ctx.run(0.3);
    for (bool careerOn : {false, true}) {
        const std::string tag = careerOn ? "career: " : "sandbox: ";
        Career c;
        c.on = careerOn;
        if (careerOn) c.startFresh(), c.cash = 0;
        Shop s;
        s.begin(v, ModSet(), c, careerOn, true);
        s.setCat(paintCat());
        ctx.check(tag + "PAINT is a category", paintCat() >= 0 && s.category().group == G_PAINT);
        s.key(K_ENTER, c);
        ctx.check(tag + "four rows", s.list.size() == PAINT_ROWS && s.paintRow() == PR_BODY && s.item() == nullptr);
        Result r = s.key(K_ENTER, c);
        ctx.check(tag + "ENTER on the body opens the picker", s.focus == F_PICK && !r.painted);
        r = s.key(K_RIGHT, c);
        ctx.check(tag + "right: the first preset", r.painted && s.paint.bodySet && paintPresetOf(s.paint.body) == 0 && s.shown().stock());
        r = s.key(K_RIGHT, c);
        ctx.check(tag + "right again: the next", paintPresetOf(s.paint.body) == 1);
        r = s.key(K_LEFT, c);
        ctx.check(tag + "left: back", paintPresetOf(s.paint.body) == 0);
        s.key(K_DOWN, c);
        const uint8_t before[3] = {s.paint.body[0], s.paint.body[1], s.paint.body[2]};
        for (int k = 0; k < 6; k++) r = s.key(K_RIGHT, c);
        ctx.check(tag + "hue moves the colour", s.ctl == PC_HUE && r.painted && (s.paint.body[0] != before[0] || s.paint.body[1] != before[1] || s.paint.body[2] != before[2]) && paintPresetOf(s.paint.body) < 0);
        s.key(K_DOWN, c);
        for (int k = 0; k < 10; k++) s.key(K_LEFT, c);
        s.key(K_DOWN, c);
        for (int k = 0; k < 10; k++) s.key(K_LEFT, c);
        ctx.check(tag + "saturation and brightness down: dark and dull", s.hsv[1] < 0.7 && s.hsv[2] < 0.7);
        s.key(K_ESC, c);
        ctx.check(tag + "ESC leaves the picker, not the shop", s.focus == F_ITEMS && s.open);
        // Accent.
        s.key(K_DOWN, c);
        ctx.check(tag + "accent row", s.paintRow() == PR_ACCENT);
        s.key(K_ENTER, c);
        s.key(K_RIGHT, c);
        ctx.check(tag + "accent set, body kept", s.paint.accentSet && s.paint.bodySet);
        s.key(K_ENTER, c);
        // Finish.
        s.key(K_DOWN, c);
        ctx.check(tag + "finish row", s.paintRow() == PR_FINISH);
        s.key(K_ENTER, c);
        ctx.check(tag + "ENTER: matte", s.paint.finish == PF_MATTE);
        s.key(K_RIGHT, c);
        ctx.check(tag + "right: metallic", s.paint.finish == PF_METALLIC);
        s.key(K_RIGHT, c);
        ctx.check(tag + "right: gloss again", s.paint.finish == PF_GLOSS);
        s.key(K_ENTER, c);
        ctx.check(tag + "finish listed", s.list[PR_FINISH].detail == "MATTE");
        // The truck wears it live.
        const CarPaint mine = s.paint;
        liftSync(ctx.sim, s);
        Car& car = ctx.sim.cars[ctx.sim.player];
        ctx.check(tag + "the truck wears it", car.paint == mine && std::fabs(car.color.x - mine.body[0] / 255.0) < 1e-9);
        ctx.check(tag + "it costs nothing", c.cash == 0 && c.owned == (careerOn ? [&] { Career f; f.startFresh(); return f.owned; }() : c.owned));
        // Factory.
        s.key(K_DOWN, c);
        ctx.check(tag + "factory row", s.paintRow() == PR_FACTORY);
        r = s.key(K_ENTER, c);
        ctx.check(tag + "factory paint back", r.painted && s.paint.factory());
        const V3 base = car.baseColor;
        liftSync(ctx.sim, s);
        ctx.check(tag + "the truck is factory again", car.paint.factory() && len(car.color - base) < 1e-9);
        // Mouse: a swatch, a slider drag.
        s.setSel(PR_BODY);
        s.hits.clear();
        s.hits.push_back({100, 100, 20, 16, 2, 5});
        s.hits.push_back({100, 200, 200, 14, 3, 0});
        s.sliders[0] = {100, 200};
        r = s.pointer(110, 108, true, true);
        ctx.check(tag + "click a swatch", r.painted && s.focus == F_PICK && paintPresetOf(s.paint.body) == 5);
        r = s.pointer(200, 206, true, true);
        ctx.check(tag + "press on the hue slider", s.dragCtl == 0 && std::fabs(s.hsv[0] - 0.5) < 1e-6);
        r = s.pointer(250, 206, false, true);
        ctx.check(tag + "drag it", std::fabs(s.hsv[0] - 0.75) < 1e-6 && r.painted);
        s.pointer(0, 0, false, false);
        ctx.check(tag + "let go", s.dragCtl < 0);
        s.key(K_R, c);
        ctx.check(tag + "R: this colour back to the factory's", !s.paint.bodySet);
    }
}

SCENARIO(paintNet, "paint/net", "Host and guest see each other's paint (body, accent, finish) on the other's truck; a repaint after joining arrives; a factory truck stays factory; a late rebuild keeps the paint") {
    const CarPaint hostPaint = painted(196, 30, 36, 240, 190, 20, PF_METALLIC);
    const CarPaint guestPaint = painted(30, 70, 190, 236, 232, 220, PF_MATTE);
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    hostCar.setPaint(hostPaint);
    NetSession host;
    const int port = 27600 + (int)(getpid() % 300) + 7;
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    Sim sim2;
    sim2.world = World(MAP_PROVING_GROUND);
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[3], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
    sim2.player = 0;
    sim2.cars[0].setPaint(guestPaint);
    NetSession client;
    bool joined = false, joinDone = false;
    std::thread joiner([&] {
        joined = client.join("127.0.0.1:" + std::to_string(port), "BUDDY", 3.0);
        joinDone = true;
    });
    for (int k = 0; k < 400 && !joinDone; k++) {
        host.receive(ctx.sim, ctx.t);
        usleep(5000);
    }
    joiner.join();
    ctx.check("client is let in", joined, client.error);
    if (!joined) return;
    client.adopt(sim2);
    auto frameBoth = [&] {
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        client.receive(sim2, ctx.t);
        ctx.sim.advance(Ctx::kFrame, 1000);
        sim2.advance(Ctx::kFrame, 1000);
        client.send(sim2, ctx.t);
        usleep(300);
    };
    auto remoteOf = [](Sim& s) -> Car* {
        for (Car& c : s.cars)
            if (c.remote) return &c;
        return nullptr;
    };
    ctx.run(1.5, frameBoth);
    Car* onClient = remoteOf(sim2);
    Car* onHost = remoteOf(ctx.sim);
    ctx.check("both see the other", onClient && onHost);
    if (!onClient || !onHost) return;
    ctx.check("guest sees the host's paint", onClient->paint == hostPaint);
    ctx.check("and its colour is drawn in it", std::fabs(onClient->color.x - 196 / 255.0) < 1e-6 && std::fabs(onClient->color.z - 36 / 255.0) < 1e-6);
    ctx.check("host sees the guest's paint", onHost->paint == guestPaint && std::fabs(onHost->color.z - 190 / 255.0) < 1e-6);
    // A repaint after joining.
    const CarPaint again = painted(18, 18, 20, 255, 255, 255, PF_GLOSS);
    hostCar.setPaint(again);
    ctx.run(1.0, frameBoth);
    onClient = remoteOf(sim2);
    ctx.check("a repaint reaches the guest", onClient && onClient->paint == again && std::fabs(onClient->color.x - 18 / 255.0) < 1e-6);
    sim2.cars[0].setPaint(CarPaint());
    ctx.run(1.0, frameBoth);
    onHost = remoteOf(ctx.sim);
    ctx.check("the guest going back to factory reaches the host", onHost && onHost->paint.factory() && !onHost->paint.bodySet);
    // A respawn (a rebuild, resent as a spawn) keeps the paint on both sides.
    hostCar.setPaint(hostPaint);
    ctx.run(0.5, frameBoth);
    hostCar.respawn(hostCar.spawnPos, hostCar.spawnYaw);
    ctx.run(1.0, frameBoth);
    onClient = remoteOf(sim2);
    ctx.check("a respawn keeps the paint", hostCar.paint == hostPaint && onClient && onClient->paint == hostPaint);
}
