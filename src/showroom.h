// The showroom garage, game side: ShowroomCore's studio on screen (its world swapped in behind a black fade), the
// turntable camera, the panels and their input. It never fits: on FIT the game runs its own fit path (main.cpp).
// One frame: handleEvent for each event, pad, update (builds, evicts, fades: never while drawing), then draw.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gamepad.h"
#include "mods.h"
#include "showroomcore.h"

class Renderer;
struct Career;
struct View;

struct ShowroomResult {
    enum Act { NONE, FIT, CLOSED } act = NONE;
    ModSet mods;
    bool recal = false;  // FIT: recalibrate the speedometer to the tires fitted (C), as the old garage did
};

class Showroom {
public:
    Showroom(Renderer& r, std::function<void()> restoreGameWorld);  // restore: renderer.setWorld(sim.world) + drawnWorld
    ~Showroom();  // (closes and lets go of every spec: run it while the GL context is still up)
    void open(int vehicle, const ModSet& fitted, const V3& color, bool career);
    bool isOpen() const;          // from open() until the closing fade has cleared
    bool drawsScene() const;      // true while the studio is the world on screen (game skips drawScene/HUD)
    void handleEvent(const SDL_Event& e);
    void pad(const pad::Raw& raw, double dt);
    // Call every frame, closed too: after closing it lets go of the preview's specs as their builds finish.
    ShowroomResult update(double dt, Career& career, bool atStation);
    void draw(int W, int H);      // studio scene (when drawsScene) + panels + fade overlay, queued on the HUD (the caller flushes)
    const Car* previewCar() const;
    void requestClose();          // ESC/G/B, and after FIT

private:
    enum Phase { CLOSED, OPENING, OPEN, CLOSING };
    struct Hit {
        float x, y, w, h;
        std::function<void()> click;
    };
    Renderer& r;
    std::function<void()> restore;
    std::unique_ptr<ShowroomCore> core;
    ShowroomFade fade;
    Phase phase = CLOSED;
    bool scene = false, career_ = false, recal = false, fitWanted = false, closeWanted = false;
    int slot = 0, listScroll = 0;
    bool follow = true;  // keep the pick in view (after a key moved it; the wheel scrolls freely)
    // Camera: yaw from the car's +x, pitch up from level, zoom times the distance (1.25 car lengths).
    double yaw = 0, pitch = 0, zoom = 1, idle = 0, clock = 0;
    struct Ease {
        double t = -1, yaw0 = 0, pitch0 = 0, zoom0 = 1, yaw1 = 0, pitch1 = 0, zoom1 = 1;
    } ease;  // t < 0: none
    bool orbiting = false;
    float mouseX = 0, mouseY = 0;
    uint32_t padPrev = 0;
    float rev = 0;  // RT (phase 3: revs the preview's engine)
    std::vector<Hit> hits;
    std::string toast;
    double toastT = 0;
    int W = 1280, H = 720;
    const Career* careerNow = nullptr;  // (from the last update, for drawing)
    bool atStation = false;
    VehicleSpec detail;  // the pick's spec, for the right panel's lines (the preview's may still be building)
    ModSet detailOf;
    int detailVehicle = -1;

    void setSlot(int s, bool now = false);
    void step(int d);
    void touched();
    void say(const std::string& s) { toast = s, toastT = 2.5; }
    void camera(View& v) const;
    void drawLeft(const ShowroomLayout& l);
    void drawRight(const ShowroomLayout& l);
    void drawBottom(const ShowroomLayout& l);
    void drawOverCar(const ShowroomLayout& l);
};
