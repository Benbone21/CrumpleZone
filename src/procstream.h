// The procedural world's window sliding with you (procworld.h): when the focus (you, or the middle of the group) gets
// proc::kSlide from the window's middle, the next window round it is built on a worker thread (World + the renderer's prepared
// terrain, as a map switch does), uploaded a few ms a frame, and swapped in on one frame with everything the sim holds moved by
// the exact whole-metre offset. The game moves what it holds itself (the camera, effects, you on foot) by the offset returned.
#pragma once
#include <cstdint>
#include <future>
#include <memory>

#include "mathx.h"
#include "render.h"

class Sim;
class World;

class ProcStream {
public:
    // Call every frame on MAP_PROCEDURAL. `focus`: where the window should follow (local). `allowed`: whether it may start a new
    // window (multiplayer: only the host decides; a guest is told, see want()). Returns true on the frame a new window went in, with
    // `shift` the offset everything moved by (new local = old local + shift).
    // In multiplayer `due` (shared clock) is when every machine swaps: not before, and at it whatever it takes (-1: as soon as ready).
    bool tick(Sim& sim, Renderer& renderer, const V3& focus, bool allowed, V3& shift, double sharedNow = -1, double due = -1);
    // A window round this origin (global, a chunk corner) is wanted (a guest told by the host; a test). Built as tick() goes.
    void want(uint64_t seed, int64_t ox, int64_t oz);
    bool busy() const { return st_ != IDLE; }
    void reset();  // (a new map: anything in flight is dropped)
    // The target origin of the window being made (valid while busy()).
    int64_t pendingX() const { return ox_; }
    int64_t pendingZ() const { return oz_; }
    // What the last ones cost (for the F3 screen and the bench): the worker's build, and the swap frame's work on the game thread.
    double lastBuildMs = 0, lastSwapMs = 0, worstSwapMs = 0;
    int swaps = 0;

private:
    enum State { IDLE, WORK, UPLOAD } st_ = IDLE;
    std::future<std::pair<std::unique_ptr<World>, std::shared_ptr<Renderer::Prepared>>> fut_;
    std::unique_ptr<World> next_;
    uint64_t seed_ = 0;
    int64_t ox_ = 0, oz_ = 0;
    double t0_ = 0;
    void start(Renderer& renderer, const World* at);  // (at: where the window will live once swapped in: its scenery is keyed by it)
};
