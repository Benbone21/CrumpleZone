#include "partpicker.h"

#include <algorithm>

void PartTree::build(int vehicle) {
    for (int s = 0; s < MOD_SLOTS; s++) {
        auto& g = groups[s];
        g.clear();
        const auto& opts = modOptions(vehicle, s);
        const int n = (int)opts.size(), own = std::min(modOwnCount(vehicle, s), n);
        if (s == MOD_TIRES) {  // (a tread is a tread: no variants to open)
            for (int o = 0; o < n; o++) g.push_back({opts[o].fromVehicle, o, 1});
            continue;
        }
        g.push_back({vehicle, 0, own});
        // The others come as consecutive runs, one donor after another in catalog order.
        for (int o = own; o < n; o++) {
            if (o > own && opts[o].fromVehicle == g.back().donor) g.back().count++;
            else g.push_back({opts[o].fromVehicle, o, 1});
        }
    }
}

int PartTree::groupOf(int slot, int option) const {
    if (slot < 0 || slot >= MOD_SLOTS || option < 0) return -1;
    const auto& g = groups[slot];
    for (int i = 0; i < (int)g.size(); i++)
        if (option >= g[i].first && option < g[i].first + g[i].count) return i;
    return -1;
}

TreadPattern treadPattern(int tireType) {
    switch (tireType) {
    case TIRE_STREET: return TREAD_GROOVES;
    case TIRE_SEMI_SLICK: return TREAD_TWO_GROOVES;
    case TIRE_RACE_SLICK: return TREAD_PLAIN;
    case TIRE_ALL_TERRAIN: return TREAD_BLOCKS;
    case TIRE_MUD_TERRAIN: return TREAD_LUGS;
    case TIRE_SNOW: return TREAD_SIPED;
    default: return TREAD_UNKNOWN;
    }
}

int tireOptionType(int vehicle, int option) {
    const auto& opts = modOptions(vehicle, MOD_TIRES);
    if (option < 0 || option >= (int)opts.size()) return -1;
    return opts[option].fromVehicle < 0 ? opts[option].fromOption : vehicleCatalog()[vehicle].tireType;  // (a tread names its type; option 0 keeps the vehicle's own)
}

namespace {
// Swatch geometry, in the unit square: ribs side by side for grooves, a grid of blocks, a staggered grid of big lugs.
std::vector<TreadBar> ribs(int n, float gap) {
    std::vector<TreadBar> v;
    const float w = (1 - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++) v.push_back({i * (w + gap), 0, w, 1});
    return v;
}
std::vector<TreadBar> grid(int cols, int rows, float gx, float gy, bool stagger, bool siped) {
    std::vector<TreadBar> v;
    const float w = (1 - gx * (cols - 1)) / cols, h = (1 - gy * (rows - 1)) / rows;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            float x = c * (w + gx);
            if (stagger && r % 2) x = std::min(x + w * 0.5f, 1 - w);  // (alternate rows shift: the lugs of a mud tire)
            if (siped) {  // a thin slit down the middle of each block
                const float slit = w * 0.14f, half = (w - slit) / 2;
                v.push_back({x, r * (h + gy), half, h});
                v.push_back({x + half + slit, r * (h + gy), half, h});
            } else v.push_back({x, r * (h + gy), w, h});
        }
    return v;
}
}  // namespace

const std::vector<TreadBar>& treadBars(TreadPattern p) {
    static const std::vector<TreadBar> none;
    static const std::vector<TreadBar> all[TREAD_PATTERNS] = {
        ribs(4, 0.07f),                         // street: three grooves
        ribs(3, 0.12f),                         // semi-slick: two
        ribs(1, 0),                             // slick: one smooth band
        grid(3, 4, 0.08f, 0.08f, false, false),  // all-terrain: blocks
        grid(2, 3, 0.14f, 0.12f, true, false),   // mud-terrain: fewer, bigger, staggered
        grid(3, 4, 0.08f, 0.08f, false, true),   // snow: the same blocks, siped
    };
    return p >= 0 && p < TREAD_PATTERNS ? all[p] : none;
}

void PickerNav::open(int vehicle, const ModSet& fitted, int buildCount) {
    vehicle_ = vehicle;
    buildCount_ = std::max(buildCount, 0);
    tree_.build(vehicle);
    pick_ = fitted;
    for (int s = 0; s < MOD_SLOTS; s++) {
        pick_.pick[s] = std::min(std::max(pick_.pick[s], 0), (int)modOptions(vehicle, s).size() - 1);  // (like applyMods)
        // Each group remembers the fitted option if it holds it, else its first.
        rem_[s].clear();
        for (const PartGroup& g : tree_.groups[s]) {
            const int f = pick_.pick[s];
            rem_[s].push_back(f >= g.first && f < g.first + g.count ? f : g.first);
        }
    }
    yours_ = pick_;
    tab_ = TAB_ENGINE, level_ = 1, group_ = 0, scroll1_ = scroll2_ = 0;
    cursor_ = std::max(tree_.groupOf(MOD_ENGINE, pick_.pick[MOD_ENGINE]), 0);
    fit();
}

void PickerNav::setView(int cols, int tileRows, int listRows) {
    cols_ = std::max(cols, 1), tileRows_ = std::max(tileRows, 1), listRows_ = std::max(listRows, 1);
    fit();
}

int PickerNav::tileCount() const {
    if (tab_ == TAB_BUILDS) return 1 + buildCount_;  // YOUR PICK, then the builds
    return level_ == 2 ? tree_.groups[tab_][group_].count : (int)tree_.groups[tab_].size();
}

void PickerNav::fit() {
    if (level_ == 1) {
        const int row = cursor_ / cols_, rows = (tileCount() + cols_ - 1) / cols_;
        scroll1_ = std::min(std::max(scroll1_, row - tileRows_ + 1), row);
        scroll1_ = std::min(scroll1_, std::max(0, rows - tileRows_));  // (no blank rows after a taller view)
    } else {
        scroll2_ = std::min(std::max(scroll2_, cursor_ - listRows_ + 1), cursor_);
        scroll2_ = std::min(scroll2_, std::max(0, tileCount() - listRows_));
    }
}

bool PickerNav::pickGroup() {
    const int o = rem_[tab_][cursor_];
    const bool changed = pick_.pick[tab_] != o;
    pick_.pick[tab_] = o;
    return changed;
}

bool PickerNav::pickRow() {
    const int o = tree_.groups[tab_][group_].first + cursor_;
    rem_[tab_][group_] = o;
    const bool changed = pick_.pick[tab_] != o;
    pick_.pick[tab_] = o;
    return changed;
}

bool PickerNav::pickBuild() {
    const ModSet was = pick_;
    pick_ = yours_;
    if (cursor_ > 0 && resolve) pick_ = resolve(cursor_ - 1, yours_);
    bool changed = false;
    for (int s = 0; s < MOD_SLOTS; s++) {
        pick_.pick[s] = std::min(std::max(pick_.pick[s], 0), (int)modOptions(vehicle_, s).size() - 1);  // (like open)
        changed = changed || pick_.pick[s] != was.pick[s];
        // Only the pick's groups remember a build's parts; the rest are as they were on entering BUILDS (so browsing
        // and going back to YOUR PICK leaves nothing behind).
        rem_[s] = remBase_[s];
        const int g = tree_.groupOf(s, pick_.pick[s]);
        if (g >= 0) rem_[s][g] = pick_.pick[s];
    }
    return changed;
}

bool PickerNav::tab(int d) {
    const int to = ((tab_ + d) % TAB_COUNT + TAB_COUNT) % TAB_COUNT;
    if (to == tab_) return false;  // (the open tab: nothing to redo, and BUILDS would take YOUR PICK again)
    tab_ = to;
    level_ = 1, scroll1_ = 0;
    if (tab_ == TAB_BUILDS) {
        yours_ = pick_;
        for (int s = 0; s < MOD_SLOTS; s++) remBase_[s] = rem_[s];
        cursor_ = 0;
    } else {
        cursor_ = std::max(tree_.groupOf(tab_, pick_.pick[tab_]), 0);
    }
    fit();
    return false;
}

bool PickerNav::move(int dx, int dy) {
    const int step = level_ == 1 ? dx + dy * cols_ : dy;
    const int c = std::min(std::max(cursor_ + step, 0), tileCount() - 1);
    if (c == cursor_) return false;
    cursor_ = c;
    fit();
    if (tab_ == TAB_BUILDS) return pickBuild();
    return level_ == 1 ? pickGroup() : pickRow();
}

bool PickerNav::enter() {
    if (level_ == 2) {
        bool close;
        return back(close);
    }
    if (tab_ == TAB_BUILDS || tree_.groups[tab_][cursor_].count < 2) return false;
    group_ = cursor_;
    level_ = 2;
    cursor_ = rem_[tab_][group_] - tree_.groups[tab_][group_].first;  // (on the remembered variant, which is the pick)
    scroll2_ = 0;
    fit();
    return false;
}

bool PickerNav::back(bool& close) {
    close = level_ == 1;
    if (level_ == 2) {
        level_ = 1;
        cursor_ = group_;
        fit();
    }
    return false;
}

bool PickerNav::clickTile(int i) {
    if (level_ != 1 || i < 0 || i >= tileCount()) return false;
    cursor_ = i;
    fit();
    return tab_ == TAB_BUILDS ? pickBuild() : pickGroup();
}

bool PickerNav::clickRow(int i) {
    if (level_ != 2 || i < 0 || i >= tileCount()) return false;
    cursor_ = i;
    fit();
    return pickRow();
}
