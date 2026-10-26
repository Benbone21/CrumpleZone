#include "procstream.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

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
    fut_ = std::async(std::launch::async, [=] {
        // (A few threads only: the game goes on meanwhile, and its own workers need the rest.)
        const int hw = (int)std::max(1u, std::thread::hardware_concurrency());
        parallelRowsCap = std::max(1, std::min(6, hw / 3));
        props::setActiveRockScans(hulls);
        World::procSeed = seed, World::procOriginSet = true, World::procOriginX = ox, World::procOriginZ = oz;
        auto w = std::make_unique<World>(MAP_PROCEDURAL);
        auto p = Renderer::prepareWorld(*w, cover);
        Renderer::prepareObjects(*w, *p, at, pq, cover);  // (the scenery too: the swap frame only uploads it)
        return std::make_pair(std::move(w), std::move(p));
    });
}

bool ProcStream::tick(Sim& sim, Renderer& renderer, const V3& focus, bool allowed, V3& shift) {
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
    if (st_ == WORK && !fut_.valid()) {
        if (seed_ != w.proc.seed) {  // (a stale request for another world)
            st_ = IDLE;
            return false;
        }
        start(renderer, &sim.world);
    }
    if (st_ == WORK) {
        if (fut_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
        auto made = fut_.get();
        next_ = std::move(made.first);
        renderer.beginUpload(std::move(made.second));
        lastBuildMs = loadprof::nowMs() - t0_;
        st_ = UPLOAD;
    }
    if (st_ == UPLOAD) {
        if (!renderer.pumpUpload(3.0)) return false;
        const double t0 = loadprof::nowMs();
        shift = V3((double)(w.proc.ox - next_->proc.ox), 0, (double)(w.proc.oz - next_->proc.oz));
        sim.world = std::move(*next_);
        next_.reset();
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
