// The showroom's part picker without any drawing: each slot's options grouped by the vehicle
// they come from (level 1: tiles), and the cursor over them (level 2: that group's variants).
#pragma once
#include <functional>
#include <vector>

#include "mods.h"

enum PickerTab { TAB_ENGINE, TAB_SUSP, TAB_TRANS, TAB_WHEELS, TAB_TIRES, TAB_BUILDS, TAB_COUNT };  // first five in ModSlot order

// Options first .. first + count - 1 of a slot's modOptions, all from one donor (-1: a tread).
struct PartGroup {
    int donor, first, count;
};

struct PartTree {
    // Group 0 is the vehicle's own parts, then one group per other donor in catalog order. In
    // TIRES every option is a group of its own.
    std::vector<PartGroup> groups[MOD_SLOTS];
    void build(int vehicle);
    int groupOf(int slot, int option) const;  // -1 if out of range
};

class PickerNav {
public:
    void open(int vehicle, const ModSet& fitted, int buildCount);
    // How many tile columns, and how many tile rows / list rows fit, so scroll() keeps the cursor in view.
    void setView(int cols, int tileRows, int listRows);
    // Each returns true if the pick changed (opening or leaving a level never does).
    bool tab(int d);                  // Q/E, LB/RB: wraps; level 1, cursor on the pick's group (BUILDS: on YOUR PICK)
    bool move(int dx, int dy);        // level 1: the grid, clamped; level 2: rows (dy only)
    bool enter();                     // level 1: open a multi-option group; level 2: back to the tiles
    bool back(bool& close);           // level 2: back to the tiles; level 1: close = true
    bool clickTile(int i), clickRow(int i);  // absolute: the group, and the option within the open group
    const ModSet& pick() const { return pick_; }
    int tabIndex() const { return tab_; }
    int level() const { return level_; }
    int cursor() const { return cursor_; }
    int scroll() const { return level_ == 1 ? scroll1_ : scroll2_; }  // first visible tile row / list row
    const PartTree& tree() const { return tree_; }
    // Level 1: tiles in this tab (BUILDS: YOUR PICK, then the builds). Level 2: the open group.
    int tileCount() const;
    int openGroup() const { return group_; }
    // The group's remembered option (absolute index into modOptions); what highlighting its tile picks.
    int remembered(int slot, int group) const { return rem_[slot][group]; }
    // The pick as it was on entering BUILDS: what YOUR PICK restores.
    const ModSet& yourPick() const { return yours_; }
    // BUILDS: moving onto build k (cursor k + 1) picks resolve(k, yourPick()); without it the pick stays.
    std::function<ModSet(int build, const ModSet& base)> resolve;

private:
    bool pickGroup();  // set the slot's pick to the cursor's group's remembered option
    bool pickRow();    // set the slot's pick to the cursor's row, and remember it
    bool pickBuild();  // BUILDS: set the whole pick to YOUR PICK (cursor 0) or the cursor's build over it
    void fit();        // scroll so the cursor shows
    PartTree tree_;
    ModSet pick_, yours_;
    std::vector<int> rem_[MOD_SLOTS];
    int vehicle_ = 0, buildCount_ = 0;
    int tab_ = TAB_ENGINE, level_ = 1, cursor_ = 0, group_ = 0;
    int scroll1_ = 0, scroll2_ = 0;
    int cols_ = 2, tileRows_ = 3, listRows_ = 6;  // (the phase 2 layout's defaults until the caller says)
};
