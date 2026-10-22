#include "showroomcore.h"

#include <algorithm>
#include <cmath>

#include "hudlayout.h"
#include "studio.h"

ShowroomCore::ShowroomCore(VisualHooks h) : hooks(std::move(h)) {
    sim.setupEmpty();
    sim.world = studioWorld();
}

void ShowroomCore::open(int vehicle, const ModSet& fitted, const V3& paint) {
    if (isOpen_) close();
    vehicle_ = vehicle, color = paint;
    for (int k = 0; k < MOD_SLOTS; k++) fitted_.pick[k] = std::clamp(fitted.pick[k], 0, (int)modOptions(vehicle, k).size() - 1);
    pick_ = fitted_;
    lastSlot = -1, failed_ = false, neighboursDone = false;
    debouncing = true, held = kDebounce;  // what's fitted is built at once
    isOpen_ = true;
}

void ShowroomCore::close() {
    removePreview();
    for (auto& s : specs) evicting.push_back(std::move(s));
    specs.clear();
    want = nullptr;
    isOpen_ = debouncing = false;
}

void ShowroomCore::setPick(int slot, int option) {
    if (slot < 0 || slot >= MOD_SLOTS) return;
    pick_.pick[slot] = std::clamp(option, 0, (int)modOptions(vehicle_, slot).size() - 1);
    lastSlot = slot, failed_ = false, neighboursDone = false;
    debouncing = true, held = 0;
    want = nullptr;  // (an older pick still building stays held until it's built: see trim)
}

const Car* ShowroomCore::preview() const { return sim.cars.empty() ? nullptr : &sim.cars[0]; }
bool ShowroomCore::fitting() const { return want && want != shown; }
int ShowroomCore::liveSpecs() const { return (int)(specs.size() + evicting.size()); }

// The held spec for these mods, moved to the front (most recent); with `build`, made and prebuilt if there's none.
VehicleSpec* ShowroomCore::specFor(const ModSet& m, bool build) {
    for (auto it = specs.begin(); it != specs.end(); ++it)
        if (std::equal(m.pick, m.pick + MOD_SLOTS, (*it)->mods)) {
            specs.splice(specs.begin(), specs, it);
            return specs.front().get();
        }
    if (!build) return nullptr;
    auto s = std::make_unique<VehicleSpec>();
    applyMods(vehicle_, m, *s);
    hooks.prebuild(*s, color);
    specs.push_front(std::move(s));
    return specs.front().get();
}

void ShowroomCore::removePreview() {
    if (!sim.cars.empty()) {
        sim.carRemoved(0);
        sim.cars.erase(sim.cars.begin());
    }
    shown = nullptr;
}

// Least recently used first, past kMaxSpecs: never the preview's own, the pick's, or one still building.
void ShowroomCore::trim() {
    while ((int)specs.size() > kMaxSpecs) {
        auto it = specs.end();
        while (it != specs.begin()) {
            --it;
            if (it->get() != shown && it->get() != want && hooks.ready(**it)) break;
            if (it == specs.begin()) return;  // nothing can go yet
        }
        evicting.push_back(std::move(*it));
        specs.erase(it);
    }
}

void ShowroomCore::update(double dt) {
    if (isOpen_) {
        if (debouncing && (held += dt) >= kDebounce) {
            debouncing = false;
            want = specFor(pick_, true);
            waited = 0;
        }
        if (want && want != shown) {
            if (hooks.ready(*want)) {
                double yaw = 0;
                if (!sim.cars.empty()) {
                    const Frame f = sim.cars[0].frame();
                    yaw = std::atan2(-f.fwd.z, f.fwd.x);
                }
                removePreview();
                sim.addCar(*want, V3(0, kDrop, 0), yaw, color);
                sim.cars[0].gear = 0, sim.cars[0].handbrake = 1;
                shown = want;
            } else if ((waited += dt) >= kBuildTimeout) {
                failed_ = true;
            }
        }
        // Then the options either side in the slot last changed, so stepping through them is instant.
        if (shown && shown == want && !neighboursDone) {
            neighboursDone = true;
            if (lastSlot >= 0) {
                int building = 0;
                for (const auto& s : specs) building += !hooks.ready(*s);
                for (int d : {-1, 1}) {
                    ModSet m = pick_;
                    m.pick[lastSlot] += d;
                    if (m.pick[lastSlot] < 0 || m.pick[lastSlot] >= (int)modOptions(vehicle_, lastSlot).size() || specFor(m, false)) continue;
                    if (building >= kMaxInFlight) {
                        neighboursDone = false;  // try again next frame
                        continue;
                    }
                    specFor(m, true);
                    building++;
                }
            }
        }
        trim();
        if (!sim.cars.empty()) {
            const Car& c = sim.cars[0];
            bool blown = !c.finite();
            for (const Node& n : c.nodes) blown = blown || len(n.p) > kUnstable;
            if (blown) {
                failed_ = true;
                removePreview();
                want = nullptr;  // not again until the next pick
            }
        }
        sim.advance(dt, 64);
    }
    for (auto it = evicting.begin(); it != evicting.end();)
        if (hooks.evict(**it)) it = evicting.erase(it);
        else ++it;
}

// The left panel (vehicle and part lists) 26% of the width, the right (stats) 24%, each with a floor so their
// text fits on small screens; a 40u bar along the bottom; the car gets the middle.
ShowroomLayout showroomLayout(int W, int H) {
    ShowroomLayout l;
    l.u = hudlayout::uiScale(H);
    const float bar = 40 * l.u, top = H - bar;
    const float lw = std::max(0.26f * W, 280 * l.u), rw = std::max(0.24f * W, 260 * l.u);
    l.left = {0, 0, lw, top};
    l.right = {W - rw, 0, rw, top};
    l.bottom = {0, top, (float)W, bar};
    l.car = {lw, 0, W - lw - rw, top};
    l.listRows = (int)std::floor((l.left.h - 60 * l.u) / (18 * l.u));  // 60u of title and header, 18u a row
    return l;
}

bool ShowroomFade::start() {
    if (on) return false;
    on = true, black = false, t = 0;
    return true;
}

bool ShowroomFade::update(double dt) {
    if (!on) return false;
    t += dt;
    if (!black && t >= kTime) {
        black = true, t = kTime;  // fully black on the swap frame
        return true;
    }
    if (t >= 2 * kTime) on = false;
    return false;
}

float ShowroomFade::alpha() const {
    if (!on) return 0;
    return (float)std::clamp(t < kTime ? t / kTime : 2 - t / kTime, 0.0, 1.0);
}
