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
    tab_ = ((tab_ + d) % TAB_COUNT + TAB_COUNT) % TAB_COUNT;
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
