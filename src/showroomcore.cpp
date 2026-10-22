#include "showroomcore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "career.h"
#include "frameprof.h"
#include "hudlayout.h"
#include "studio.h"

namespace {
std::string modsText(const VehicleSpec& s) {  // "(mods 0 2 1 0 0)", for the log
    std::string t = "(mods";
    for (int k = 0; k < MOD_SLOTS; k++) t += " " + std::to_string(s.mods[k]);
    return t + ")";
}
}  // namespace

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
    building.clear();
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

void ShowroomCore::setPicks(const ModSet& m) {
    int differ = 0, slot = -1;
    for (int k = 0; k < MOD_SLOTS; k++) {
        const int o = std::clamp(m.pick[k], 0, (int)modOptions(vehicle_, k).size() - 1);
        if (o != pick_.pick[k]) differ++, slot = k;
        pick_.pick[k] = o;
    }
    lastSlot = differ == 1 ? slot : -1;  // (one slot: as setPick; several: no neighbours to prebuild)
    failed_ = false, neighboursDone = false;
    debouncing = true, held = 0;
    want = nullptr;
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
    building[s.get()] = 0;
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
            if (it->get() != shown && it->get() != want && !building.count(it->get())) break;
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
        }
        // A build not in after kBuildTimeout is let go (the cache frees it once it can), so it holds no place among
        // the specs or the builds in flight; if it was the pick's, the pick has failed.
        for (auto it = specs.begin(); it != specs.end();) {
            auto b = building.find(it->get());
            if (b != building.end() && hooks.ready(**it)) {
                building.erase(b);
            } else if (b != building.end() && (b->second += dt) >= kBuildTimeout) {
                std::fprintf(stderr, "showroom: %s %s not built in %.0f s, let go\n", (*it)->name, modsText(**it).c_str(), kBuildTimeout);
                if (it->get() == want) failed_ = true, want = nullptr;
                building.erase(b);
                evicting.push_back(std::move(*it));
                it = specs.erase(it);
                continue;
            }
            ++it;
        }
        if (want && want != shown && !building.count(want)) {
            double yaw = 0;
            if (!sim.cars.empty()) {
                const Frame f = sim.cars[0].frame();
                yaw = std::atan2(-f.fwd.z, f.fwd.x);
            }
            removePreview();
            sim.addCar(*want, V3(0, kDrop, 0), yaw, color);
            sim.cars[0].gear = 0, sim.cars[0].handbrake = 1;
            shown = want;
        }
        // Then the options either side in the slot last changed, so stepping through them is instant.
        if (shown && shown == want && !neighboursDone) {
            neighboursDone = true;
            if (lastSlot >= 0) {
                for (int d : {-1, 1}) {
                    ModSet m = pick_;
                    m.pick[lastSlot] += d;
                    if (m.pick[lastSlot] < 0 || m.pick[lastSlot] >= (int)modOptions(vehicle_, lastSlot).size() || specFor(m, false)) continue;
                    if ((int)building.size() >= kMaxInFlight) {
                        neighboursDone = false;  // try again next frame
                        continue;
                    }
                    specFor(m, true);
                }
            }
        }
        trim();
        if (!sim.cars.empty()) {
            const Car& c = sim.cars[0];
            bool blown = !c.finite();
            for (const Node& n : c.nodes) blown = blown || len(n.p) > kUnstable;
            if (blown) {
                std::fprintf(stderr, "showroom: %s %s blew up on the turntable, removed\n", c.spec->name, modsText(*c.spec).c_str());
                failed_ = true;
                removePreview();
                want = nullptr;  // not again until the next pick
            }
        }
        sim.advance(dt, simMaxSteps(sim.h));  // (capped like the main sim)
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
    // Tiles: (panel - 3 gaps) / 2 wide, a 4:3 picture, then two caption lines (7 px glyphs at the label scale, 4u apart).
    l.tileCols = 2;
    l.tileGap = 12 * l.u;
    l.tileTop = l.left.y + 60 * l.u;
    l.tileW = (l.left.w - 3 * l.tileGap) / l.tileCols;
    l.tileH = l.tileW * 0.75f + 2 * (7 * hudlayout::px(1.4f * l.u) + 4 * l.u);
    l.tileRows = std::max(0, (int)std::floor((l.left.y + l.left.h - l.tileTop + l.tileGap) / (l.tileH + l.tileGap)));
    return l;
}

bool showroomBuy(Career& career, int vehicle, int slot, int option, bool atStation, std::string& toast) {
    toast.clear();
    if (!career.on) return false;
    if (!atStation) return toast = "PARTS ARE SOLD AT THE REPAIR STATIONS", false;
    std::string why;
    if (!career.buy(vehicle, slot, option, why)) return toast = why, false;
    toast = "BOUGHT " + modOptions(vehicle, slot)[option].name + ": $" + std::to_string(career.cash) + " LEFT";
    return true;
}

bool ShowroomFade::start() {
    if (on) return false;
    on = true, black = false, t = 0;
    return true;
}

bool ShowroomFade::update(double dt, bool hold) {
    if (!on || (black && hold && t <= kTime)) return false;  // (held: black until let go)
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
