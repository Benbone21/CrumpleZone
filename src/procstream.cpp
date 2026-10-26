#include "procstream.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "loadprof.h"
#include "parallel.h"
#include "procworld.h"
#include "props.h"
#include "softbody.h"
#include "world.h"

void ProcStream::reset() {
    if (st_ == WORK && fut_.valid()) fut_.wait();  // (its worker holds nothing of ours; let it finish)
    fut_ = {};
    next_.reset();
    st_ = IDLE;
}

void ProcStream::want(uint64_t seed, int64_t ox, int64_t oz) {
    if (st_ != IDLE && seed == seed_ && ox == ox_ && oz == oz_) return;
    reset();
    seed_ = seed, ox_ = ox, oz_ = oz;
    st_ = WORK;
    fut_ = {};  // (started by tick, which has the renderer)
}

void ProcStream::start(Renderer& renderer, const World* at) {
    const uint64_t seed = seed_;
    const int64_t ox = ox_, oz = oz_;
    const props::HullLibrary* hulls = props::activeRockScans();
    const ScanCover cover = renderer.scanCover();
    const int pq = renderer.objectsQuality();
    t0_ = loadprof::nowMs();
    const Renderer* rp = &renderer;
    fut_ = std::async(std::launch::async, [=] {
        // (A few threads only: the game goes on meanwhile, and its own workers need the rest.)
        const int hw = (int)std::max(1u, std::thread::hardware_concurrency());
        parallelRowsCap = std::max(1, std::min(3, hw / 6));
#ifdef __linux__
        setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 8);  // (and below the game's own threads: the threads it starts inherit it)
#endif
        props::setActiveRockScans(hulls);
        World::procSeed = seed, World::procOriginSet = true, World::procOriginX = ox, World::procOriginZ = oz;
        auto w = std::make_unique<World>(MAP_PROCEDURAL);
        const V3 mid(0, 0, 0);  // (the window was centred near the player)
        auto p = Renderer::prepareWorld(*w, cover, &mid);
        Renderer::prepareObjects(*w, *p, at, pq, cover);  // (the scenery too, and the scans' layout and the places: the swap frame only swaps)
        rp->prepareProps(*w, *p);
        Made m;
        m.places = proc::windowPlaces(*w);
        m.world = std::move(w), m.prep = std::move(p);
        return m;
    });
}

bool ProcStream::tick(Sim& sim, Renderer& renderer, const V3& focus, bool allowed, V3& shift, double sharedNow, double due) {
    const bool timed = due >= 0, late = timed && sharedNow >= due;  // (multiplayer: everyone swaps at `due` on the shared clock)
    World& w = sim.world;
    if (w.map != MAP_PROCEDURAL) {
        if (st_ != IDLE) reset();
        return false;
    }
    if (st_ == IDLE) {
        if (!allowed || !std::isfinite(focus.x) || !std::isfinite(focus.z)) return false;
        if (std::max(std::fabs(focus.x), std::fabs(focus.z)) < proc::kSlide) return false;
        int64_t ox, oz;
        proc::ProcWorld::snapOrigin((double)w.proc.ox + focus.x, (double)w.proc.oz + focus.z, ox, oz);
        want(w.proc.seed, ox, oz);
    }
    if (st_ != UPLOAD) staged_ = false;
    if (st_ == WORK && !fut_.valid()) {
        if (seed_ != w.proc.seed) {  // (a stale request for another world)
            st_ = IDLE;
            return false;
        }
        start(renderer, &sim.world);
    }
    if (st_ == WORK) {
        if (!late && fut_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
        fut_.wait();  // (late: it must go in now, so wait for it here: a hitch on a slow machine, not a world out of step)
        Made made = fut_.get();
        next_ = std::move(made.world);
        places = std::move(made.places);
        renderer.beginUpload(std::move(made.prep));
        lastBuildMs = loadprof::nowMs() - t0_;
        st_ = UPLOAD;
    }
    if (st_ == UPLOAD) {
        // (The terrain, then the scenery, onto the GPU a couple of ms a frame, apart from what's drawn: the swap only swaps.)
        if (!renderer.pumpUpload(late ? 1e9 : 2.0)) return false;
        if (!staged_) {
            if (!renderer.stageWorld(late ? 1e9 : 2.0)) return false;
            staged_ = true;
            if (!late) return false;  // (the swap on a frame of its own, not on the last upload's)
        }
        if (timed && !late) return false;  // (ready: it goes in at the moment the host named)
        const double t0 = loadprof::nowMs();
        shift = V3((double)(w.proc.ox - next_->proc.ox), 0, (double)(w.proc.oz - next_->proc.oz));
        // (The renderer's background streaming, and the autopilot's replans, read the world: done with it before it goes.)
        renderer.waitStreaming();
        // (The window that goes out is freed on a thread of its own: hundreds of MB handed back is ms of the frame.)
        auto old = std::make_shared<World>(std::move(sim.world));
        sim.world = std::move(*next_);
        next_.reset();
        std::thread([old]() mutable { old.reset(); }).detach();
        proc::shiftSim(sim, shift);
        renderer.adoptWorld(sim.world);
        while (!renderer.stepWorld(sim.world)) {}  // (all the scenery this frame: nothing is drawn half there)
        st_ = IDLE;
        lastSwapMs = loadprof::nowMs() - t0;
        worstSwapMs = std::max(worstSwapMs, lastSwapMs);
        swaps++;
        return true;
    }
    return false;
}
