// The garage shop's state and data (garageshop.h). No GL, no SDL.
#include "garageshop.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>

#include "drivetrainlook.h"
#include "modelkit.h"
#include "softbody.h"

namespace shop {

namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// What to call a mod slot in the shop (a slot without a line here goes by modSlotName).
struct SlotName {
    int slot;
    const char *name, *blurb;
};
const SlotName kSlotNames[] = {
    {MOD_ENGINE, "ENGINE", "SWAPS AND TUNES"},
    {MOD_SUSPENSION, "SUSPENSION AND LIFT", "SPRINGS, LIFT KITS, LONG TRAVEL"},
    {MOD_TRANSMISSION, "TRANSMISSION", "GEARBOXES, AXLE GEARS, LOCKERS"},
    {MOD_WHEELS, "WHEELS", "OTHER TRUCKS' WHEELS AND TIRE SIZES"},
    {MOD_TIRES, "TIRES", "TREADS FOR THE GROUND YOU DRIVE"},
};
// ...and a kind of rack item, by its key's prefix.
struct RackName {
    const char *prefix, *name, *blurb;
};
const RackName kRackNames[] = {
    {"TOOL:", "TOOLS", "HAND TOOLS FOR TRAIL REPAIRS"},
    {"REPAIR:", "REPAIR PARTS", "SPARES TO CARRY FOR WHAT BREAKS"},
    {"ANCHOR:", "ANCHOR GEAR", "WHAT THE WINCH PULLS FROM"},
};

std::string upper(std::string s) {
    for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
    return s;
}

// A model's "use" line, the player's part of it: up to the first ';' or the notes on its origin.
std::string useLine(const std::string& id) {
    const mk::ModelDef* d = mk::findModel(id);
    if (!d) return "";
    std::string u = d->use;
    for (const char* cut : {";", ": origin", ", origin", " (", ": +X", ": grip", ", driven", ", right hand"}) {
        const size_t at = u.find(cut);
        if (at != std::string::npos) u = u.substr(0, at);
    }
    return upper(u);
}

double weightOf(const VehicleSpec& s) {
    double kg = s.structureMass + s.panelMass + s.engineMass + s.gearboxMass + 4 * (s.wheelMass + s.hubMass);
    for (const MassItem& m : s.items) kg += m.kg;
    return kg;
}

// A drivetrain part's own model, design and params (drivetrainlook.h).
void showLook(Display& d, const PartLook& L) {
    d.id = L.id, d.variant = L.variant;
    for (int i = 0; i < L.n; i++) d.set.emplace_back(L.set[i].key, L.set[i].value);
}

}  // namespace

const std::vector<Category>& categories() {
    static const std::vector<Category> cats = [] {
        std::vector<Category> v;
        for (int k = 0; k < MOD_SLOTS; k++) {
            Category c;
            c.group = G_MOD, c.slot = k, c.name = modSlotName(k);
            for (const SlotName& n : kSlotNames)
                if (n.slot == k) c.name = n.name, c.blurb = n.blurb;
            v.push_back(c);
        }
        {
            Category c;
            c.group = G_PAINT, c.name = "PAINT", c.blurb = "BODY COLOUR, ACCENT AND FINISH";
            v.push_back(c);
        }
        // The rack's kinds, in the order the rack has them.
        for (const ShopItem& it : stationRack()) {
            const size_t colon = it.key.find(':');
            const std::string prefix = colon == std::string::npos ? it.key : it.key.substr(0, colon + 1);
            if (std::any_of(v.begin(), v.end(), [&](const Category& c) { return c.group == G_GEAR && c.prefix == prefix; })) continue;
            Category c;
            c.group = G_GEAR, c.prefix = prefix, c.name = prefix.substr(0, prefix.size() - (colon == std::string::npos ? 0 : 1));
            for (const RackName& n : kRackNames)
                if (prefix == n.prefix) c.name = n.name, c.blurb = n.blurb;
            v.push_back(c);
        }
        return v;
    }();
    return cats;
}

std::vector<Item> items(const Category& c, int vehicle, const ModSet& fitted, const Career& career) {
    std::vector<Item> out;
    if (c.group == G_MOD) {
        const auto& opts = modOptions(vehicle, c.slot);
        for (int o = 0; o < (int)opts.size(); o++) {
            Item it;
            it.group = G_MOD, it.slot = c.slot, it.option = o;
            it.key = modPartKey(vehicle, c.slot, o);
            it.name = opts[o].name;
            if (o == 0) it.name = (opts[o].name == "STOCK" ? std::string("FACTORY") : opts[o].name) + ": " + (opts[o].detail.empty() ? std::string("AS IT CAME") : opts[o].detail);
            it.detail = opts[o].detail;
            it.price = Career::price(vehicle, c.slot, o);
            it.tier = Career::tier(vehicle, c.slot, o);
            if (fitted.pick[c.slot] == o) it.state = ST_FITTED;
            else if (career.owns(vehicle, c.slot, o)) it.state = ST_OWNED;
            else if (it.price > 0) it.state = ST_FOR_SALE;
            else {
                it.state = ST_LOCKED;
                for (const std::string& w : partSources(it.key)) it.where += (it.where.empty() ? "" : "  OR  ") + w;
            }
            out.push_back(it);
        }
        return out;
    }
    if (c.group == G_PAINT) {
        static const char* names[PAINT_ROWS] = {"BODY COLOUR", "ACCENT: TRIM AND RIMS", "FINISH", "FACTORY PAINT"};
        for (int r = 0; r < PAINT_ROWS; r++) {
            Item it;
            it.group = G_PAINT, it.option = r, it.name = names[r], it.key = std::string("PAINT:") + names[r];
            it.state = ST_OWNED;
            out.push_back(it);
        }
        return out;
    }
    const auto& rack = stationRack();
    for (int i = 0; i < (int)rack.size(); i++) {
        if (rack[i].key.rfind(c.prefix, 0) != 0) continue;
        Item it;
        it.group = G_GEAR, it.rack = i, it.key = rack[i].key;
        it.name = rack[i].key.substr(c.prefix.size());
        it.price = rack[i].price;
        it.state = ST_GEAR;
        it.tool = c.prefix == "TOOL:";
        it.detail = useLine(rack[i].model.empty() ? "tire_part" : rack[i].model);
        if (!career.on) it.carried = -1;  // (sandbox: all of it, always)
        else if (it.tool) it.carried = career.owned.count(it.key) ? 1 : 0;
        else {
            auto s = career.stock.find(it.key);
            it.carried = s == career.stock.end() ? 0 : s->second;
        }
        out.push_back(it);
    }
    return out;
}

ModSet withItem(const ModSet& fitted, const Item& it) {
    ModSet m = fitted;
    if (it.group == G_MOD && it.slot >= 0 && it.slot < MOD_SLOTS) m.pick[it.slot] = it.option;
    return m;
}

std::vector<Stat> compare(int vehicle, const ModSet& fitted, const ModSet& with) {
    const VehicleSpec& F = tunedSpec(vehicle, fitted);
    const VehicleSpec& W = tunedSpec(vehicle, with);
    std::vector<Stat> out;
    auto add = [&](const char* label, double a, double b, const char* unit, int better, int decimals, bool always) {
        const double eps = decimals >= 2 ? 0.005 : decimals == 1 ? 0.05 : 0.5;
        if (!always && std::fabs(a - b) < eps) return;
        Stat s;
        s.label = label, s.now = a, s.with = b, s.unit = unit, s.better = better, s.decimals = decimals;
        out.push_back(s);
    };
    add("POWER", peakPower(F) / 745.7, peakPower(W) / 745.7, "HP", 1, 0, true);
    add("TORQUE", peakTorque(F), peakTorque(W), "NM", 1, 0, false);
    add("REDLINE", F.redline, W.redline, "RPM", 0, 0, false);
    add("WEIGHT", weightOf(F), weightOf(W), "KG", -1, 0, true);
    add("GRIP", F.tireMu(), W.tireMu(), "", 1, 2, false);
    add("CORNERING", F.tireMu() * F.lateralGrip, W.tireMu() * W.lateralGrip, "", 1, 2, false);
    add("CLEARANCE", F.layerY.empty() ? 0 : F.layerY[0] * 1000, W.layerY.empty() ? 0 : W.layerY[0] * 1000, "MM", 1, 0, false);
    add("TIRE", F.wheelRadius * 2000, W.wheelRadius * 2000, "MM TALL", 0, 0, false);
    add("TIRE WIDTH", F.wheelWidth * 1000, W.wheelWidth * 1000, "MM", 0, 0, false);
    add("TRAVEL", (F.bumpFront + F.droopFront) * 1000, (W.bumpFront + W.droopFront) * 1000, "MM", 1, 0, false);
    add("BOUNCE", F.rideFreqFront, W.rideFreqFront, "HZ", 0, 2, false);
    add("GEARS", (double)F.gears.size(), (double)W.gears.size(), "", 0, 0, false);
    add("FINAL DRIVE", F.finalDrive, W.finalDrive, "", 0, 2, false);
    add("LOW RANGE", F.lowRange, W.lowRange, "", 0, 2, false);
    add("LOCKERS", (F.lockerFront ? 1 : 0) + (F.lockerRear ? 1 : 0), (W.lockerFront ? 1 : 0) + (W.lockerRear ? 1 : 0), "", 1, 0, false);
    return out;
}

Display displayFor(const Item& it, int vehicle, const ModSet& fitted) {
    Display d;
    if (it.group == G_PAINT) return d;  // (no part: the truck itself is the picture)
    if (it.group == G_GEAR) {
        const ShopItem& r = stationRack()[it.rack];
        d.id = r.model.empty() ? "tire_part" : r.model;
    } else {
        const ModSet with = withItem(fitted, it);
        const VehicleSpec& W = tunedSpec(vehicle, with);
        const VehicleSpec& stock = vehicleCatalog()[vehicle];
        switch (it.slot) {
            case MOD_ENGINE: showLook(d, engineLook(W)); break;
            case MOD_SUSPENSION: d.id = W.leafSprings && W.leafFront ? "leaf_pack" : "coilover"; break;
            case MOD_TRANSMISSION:
                // A new gearbox shows the box; new axle gears or lockers the axle's centre; a deeper low range the transfer case.
                if (it.option == 0 || W.gearboxName != stock.gearboxName || W.gears != stock.gears) showLook(d, gearboxLook(W));
                else if (W.lowRange != stock.lowRange && *transferLook(W).id) showLook(d, transferLook(W));
                else d.id = "differential";
                break;
            default:  // wheels and tires: one tire of the size it gives
                d.id = "tire_part";
                d.set = {{"radius", W.wheelRadius}, {"width", W.wheelWidth}};
                break;
        }
    }
    const mk::ModelDef* def = mk::findModel(d.id);
    double size = def ? def->size : 0.6;
    for (const auto& p : d.set)
        if (p.first == "radius") size = p.second * 2;
    d.scale = size < 0.45 ? std::min(6.0, 0.45 / std::max(0.01, size)) : 1.0;
    d.size = size * d.scale;
    d.onDrum = d.size < 0.68;
    d.standUp = d.id == "tire_part";
    return d;
}

void modelBounds(const Display& d, V3& lo, V3& hi) {
    static std::mutex lock;
    static std::map<std::string, std::pair<V3, V3>> cache;
    static uint64_t cachedFor = ~0ull;
    std::string key = d.id + "#" + std::to_string(d.variant);
    for (const auto& p : d.set) {
        char b[48];
        std::snprintf(b, sizeof b, " %s=%.4f", p.first.c_str(), p.second);
        key += b;
    }
    std::lock_guard<std::mutex> g(lock);
    if (cachedFor != mk::picksVersion()) cache.clear(), cachedFor = mk::picksVersion();
    auto at = cache.find(key);
    if (at == cache.end()) {
        std::pair<V3, V3> b(V3(-0.2, 0, -0.2), V3(0.2, 0.4, 0.2));
        if (const mk::ModelDef* def = mk::findModel(d.id)) {
            const int v = d.variant >= 0 && d.variant < (int)def->variants.size() ? d.variant : mk::pickedVariant(*def);
            mk::P p = mk::pickedParams(*def, v);
            for (const auto& s : d.set) p.v[s.first] = s.second;
            mk::Mesh m;
            mk::build(*def, v, p, m);
            if (!m.v.empty()) b = {m.lo, m.hi};
        }
        at = cache.emplace(key, b).first;
    }
    lo = at->second.first, hi = at->second.second;
}

// ---- The navigation ------------------------------------------------------------------

void Shop::begin(int v, const ModSet& f, const Career& c, bool careerOn, bool station, const CarPaint& p) {
    open = true;
    paint = p, dragCtl = -1;
    vehicle = v, fitted = f, career = careerOn, atStation = station;
    previewing = false;
    focus = F_CATS;
    cat = clampi(cat, 0, (int)categories().size() - 1);
    sel = 0;
    spin = 0, tilt = 0.32, zoom = 1, idle = 99;
    careerRef_ = &c;
    refresh(c);
}

void Shop::refresh(const Career& c) {
    careerRef_ = &c;
    list = listFor(c);
    sel = clampi(sel, 0, std::max(0, (int)list.size() - 1));
}

namespace {
std::string colourName(bool set, const uint8_t rgb[3]) {
    if (!set) return "FACTORY";
    const int p = paintPresetOf(rgb);
    if (p >= 0) return paintPresets()[p].name;
    char b[16];
    std::snprintf(b, sizeof b, "CUSTOM %02X%02X%02X", rgb[0], rgb[1], rgb[2]);
    return b;
}
}  // namespace

std::vector<Item> Shop::listFor(const Career& c) const {
    std::vector<Item> l = items(category(), vehicle, fitted, c);
    if (category().group == G_PAINT) {
        for (Item& it : l) {
            if (it.option == PR_BODY) it.detail = colourName(paint.bodySet, paint.body);
            else if (it.option == PR_ACCENT) it.detail = colourName(paint.accentSet, paint.accent);
            else if (it.option == PR_FINISH) it.detail = paintFinishName(paint.finish);
            else it.detail = paint.factory() ? "ALREADY FACTORY" : "BACK TO HOW IT CAME";
        }
    }
    return l;
}

int Shop::paintRow() const {
    if (category().group != G_PAINT || focus == F_CATS || sel < 0 || sel >= (int)list.size()) return -1;
    return list[sel].option;
}

const Category& Shop::category() const { return categories()[clampi(cat, 0, (int)categories().size() - 1)]; }

const Item* Shop::item() const {
    if (focus == F_CATS || sel < 0 || sel >= (int)list.size() || list[sel].group == G_PAINT) return nullptr;
    return &list[sel];
}

ModSet Shop::shown() const {
    const Item* it = item();
    if (!open || !previewing || !it || it->group != G_MOD) return fitted;
    return withItem(fitted, *it);
}

void Shop::setCat(int i) {
    const int n = (int)categories().size();
    const int was = cat;
    cat = (i % n + n) % n;
    if (cat == was && !list.empty()) return;
    previewing = false;
    sel = 0;
    if (careerRef_) list = listFor(*careerRef_);
    // (A mod slot opens on what's fitted.)
    for (int k = 0; k < (int)list.size(); k++)
        if (list[k].state == ST_FITTED) sel = k;
}

void Shop::setSel(int i) { sel = clampi(i, 0, std::max(0, (int)list.size() - 1)); }

void Shop::nextCat(int dir) { setCat(cat + dir); }

void Shop::move(int dir) {
    if (focus == F_PICK) {
        ctl = (ctl + dir + PICK_CTLS) % PICK_CTLS;
        return;
    }
    if (focus == F_CATS) {
        setCat(cat + dir);
        return;
    }
    const int n = (int)list.size();
    if (n > 0) setSel((sel + dir + n) % n);
}

Result Shop::close() {
    Result r;
    open = false;
    dragCtl = -1;
    previewing = false;
    r.close = true;
    return r;
}

Result Shop::back() {
    if (focus == F_PICK) {
        focus = F_ITEMS;
        dragCtl = -1;
        return {};
    }
    if (focus == F_ITEMS) {
        focus = F_CATS;
        previewing = false;
        return {};
    }
    return close();
}

Result Shop::fit(int slot, int option, const std::string& said) {
    Result r;
    fitted.pick[slot] = option;
    previewing = false;
    if (careerRef_) {
        const int keep = sel;
        list = listFor(*careerRef_);
        setSel(keep);
    }
    r.fitted = true;
    r.toast = said;
    return r;
}

Result Shop::activate(Career& c) {
    Result r;
    careerRef_ = &c;
    if (focus == F_CATS) {
        focus = F_ITEMS;
        refresh(c);
        sel = 0;  // (onto what's fitted)
        for (int k = 0; k < (int)list.size(); k++)
            if (list[k].state == ST_FITTED) sel = k;
        return r;
    }
    if (focus == F_PICK) return back();
    if (category().group == G_PAINT && paintRow() >= 0) {
        const int row = paintRow();
        if (row == PR_BODY || row == PR_ACCENT) enterPick(row);
        else if (row == PR_FINISH) return cycleFinish(1);
        else {
            if (paint.factory()) {
                r.toast = "IT'S ALREADY THE FACTORY PAINT";
                return r;
            }
            paint = CarPaint();
            r.painted = true;
            r.toast = "FACTORY PAINT BACK ON";
            refresh(c);
        }
        return r;
    }
    const Item* it = item();
    if (!it) return r;
    const Item item = *it;  // (the list is rebuilt below)
    if (career && !atStation) {
        r.toast = "PARTS ARE SOLD AND FITTED AT THE REPAIR STATIONS";
        return r;
    }
    if (item.group == G_GEAR) {
        if (!career) {
            r.toast = "SANDBOX: EVERY TOOL AND PART IS ALREADY IN THE TRUCK";
            return r;
        }
        std::string why;
        if (c.buyGear(stationRack()[item.rack], why)) {
            r.saveCareer = true;
            r.toast = "BOUGHT " + item.name + ": $" + std::to_string(c.cash) + " LEFT";
            const int keep = sel;
            refresh(c);
            setSel(keep);
        } else {
            r.toast = why;
        }
        return r;
    }
    switch (item.state) {
        case ST_FITTED: r.toast = "IT'S ON THE TRUCK"; return r;
        case ST_OWNED: return fit(item.slot, item.option, "FITTED: " + item.name);
        case ST_FOR_SALE: {
            std::string why;
            if (!c.buy(vehicle, item.slot, item.option, why)) {
                r.toast = why;
                return r;
            }
            Result f = fit(item.slot, item.option, "BOUGHT AND FITTED: " + item.name + "  $" + std::to_string(c.cash) + " LEFT");
            f.saveCareer = true;
            return f;
        }
        case ST_LOCKED:
            r.toast = "NOT SOLD: " + (item.where.empty() ? std::string("FIND IT OUT THERE, OR WIN IT") : item.where);
            return r;
        default: return r;
    }
}

// ---- CarPaint -------------------------------------------------------------------------------

void Shop::enterPick(int row) {
    focus = F_PICK, pickRow = row, ctl = PC_PRESET, dragCtl = -1;
    const bool set = row == PR_BODY ? paint.bodySet : paint.accentSet;
    const uint8_t* c = row == PR_BODY ? paint.body : paint.accent;
    const uint8_t* start = set ? c : paintPresets()[0].rgb;
    rgbToHsv(start[0] / 255.0, start[1] / 255.0, start[2] / 255.0, hsv[0], hsv[1], hsv[2]);
}

// The picker's colour (hue, saturation, brightness) onto the colour being edited.
void Shop::setPickColour(Result& r) {
    double R, G, B;
    hsvToRgb(hsv[0], hsv[1], hsv[2], R, G, B);
    const CarPaint was = paint;
    if (pickRow == PR_BODY) paint.setBody(R, G, B);
    else paint.setAccent(R, G, B);
    if (paint != was) r.painted = true;
    if (careerRef_) {
        const int keep = sel;
        list = listFor(*careerRef_);
        setSel(keep);
    }
}

void Shop::applyPreset(int i, Result& r) {
    const auto& pr = paintPresets();
    if (i < 0 || i >= (int)pr.size()) return;
    const CarPaint was = paint;
    uint8_t* c = pickRow == PR_BODY ? paint.body : paint.accent;
    for (int k = 0; k < 3; k++) c[k] = pr[i].rgb[k];
    (pickRow == PR_BODY ? paint.bodySet : paint.accentSet) = true;
    rgbToHsv(c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, hsv[0], hsv[1], hsv[2]);
    if (paint != was) r.painted = true;
    r.toast = pr[i].name;
    if (careerRef_) {
        const int keep = sel;
        list = listFor(*careerRef_);
        setSel(keep);
    }
}

Result Shop::pickAdjust(int dir) {
    Result r;
    if (ctl == PC_PRESET) {
        const int n = (int)paintPresets().size();
        const bool set = pickRow == PR_BODY ? paint.bodySet : paint.accentSet;
        const int at = set ? paintPresetOf(pickRow == PR_BODY ? paint.body : paint.accent) : -1;
        applyPreset(at < 0 ? (dir > 0 ? 0 : n - 1) : (at + dir + n) % n, r);
        return r;
    }
    const int k = ctl - PC_HUE;
    const double step = 0.02 * dir;
    if (k == 0) hsv[0] = hsv[0] + step - std::floor(hsv[0] + step);
    else hsv[k] = std::min(1.0, std::max(0.0, hsv[k] + step));
    setPickColour(r);
    return r;
}

Result Shop::cycleFinish(int dir) {
    Result r;
    paint.finish = (uint8_t)((paint.finish + dir + PF_FINISHES) % PF_FINISHES);
    r.painted = true;
    r.toast = std::string("FINISH: ") + paintFinishName(paint.finish);
    if (careerRef_) {
        const int keep = sel;
        list = listFor(*careerRef_);
        setSel(keep);
    }
    return r;
}

Result Shop::pointer(float x, float y, bool press, bool held) {
    Result r;
    if (!held) {
        dragCtl = -1;
        return r;
    }
    int col = -1, row = -1;
    if (press && hitAt(x, y, col, row) && col >= 2) {
        if (focus != F_PICK) {
            const int pr = paintRow();
            enterPick(pr == PR_ACCENT ? PR_ACCENT : PR_BODY);
        }
        if (col == 2) {
            ctl = PC_PRESET;
            applyPreset(row, r);
            return r;
        }
        if (row >= 0 && row < 3) dragCtl = row, ctl = PC_HUE + row;
    }
    if (dragCtl >= 0 && focus == F_PICK) {
        const Slider& sl = sliders[dragCtl];
        hsv[dragCtl] = std::min(1.0, std::max(0.0, (double)((x - sl.x) / std::max(1.0f, sl.w))));
        setPickColour(r);
    }
    return r;
}

Result Shop::tryOn() {
    Result r;
    const Item* it = item();
    if (!it) {
        r.toast = "PICK A PART TO TRY IT ON";
        return r;
    }
    if (it->group != G_MOD) {
        r.toast = "GEAR RIDES IN THE TRUCK: NOTHING TO TRY ON";
        return r;
    }
    previewing = !previewing;
    r.toast = previewing ? "TRYING IT ON THE TRUCK" : "BACK TO WHAT'S FITTED";
    return r;
}

Result Shop::factory(Career& c) {
    Result r;
    careerRef_ = &c;
    const Item* it = item();
    const int slot = it ? it->slot : category().slot;
    if (slot < 0) {
        r.toast = "NOTHING TO TAKE OFF HERE";
        return r;
    }
    if (fitted.pick[slot] == 0) {
        r.toast = "THE FACTORY PART IS ON";
        return r;
    }
    if (career && !atStation) {
        r.toast = "PARTS ARE FITTED AT THE REPAIR STATIONS";
        return r;
    }
    return fit(slot, 0, std::string("FACTORY ") + category().name + " BACK ON");
}

Result Shop::key(int sc, Career& c) {
    careerRef_ = &c;
    Result r;
    if (focus == F_PICK) {  // the colour picker: up and down pick the control, left and right change it
        switch (sc) {
            case K_LEFT:
            case K_A: return pickAdjust(-1);
            case K_RIGHT:
            case K_D: return pickAdjust(1);
            case K_R:
            case K_B: {
                bool& set = pickRow == PR_BODY ? paint.bodySet : paint.accentSet;
                if (set) set = false, r.painted = true, r.toast = "FACTORY COLOUR BACK";
                refresh(c);
                return r;
            }
            case K_ENTER:
            case K_KPENTER:
            case K_SPACE: return back();
            default: break;
        }
    } else if (paintRow() == PR_FINISH && (sc == K_LEFT || sc == K_A || sc == K_RIGHT || sc == K_D)) {
        return cycleFinish(sc == K_LEFT || sc == K_A ? -1 : 1);
    } else if (category().group == G_PAINT && focus == F_ITEMS && (sc == K_R || sc == K_B)) {
        if (paint.factory()) r.toast = "IT'S ALREADY THE FACTORY PAINT";
        else paint = CarPaint(), r.painted = true, r.toast = "FACTORY PAINT BACK ON";
        refresh(c);
        return r;
    }
    switch (sc) {
        case K_ESC:
        case K_BACKSPACE: return back();
        case K_G: return close();
        case K_UP:
        case K_W: move(-1); break;
        case K_DOWN:
        case K_S: move(1); break;
        case K_RIGHT:
        case K_D:
            if (focus == F_CATS) return activate(c);
            break;
        case K_LEFT:
        case K_A:
            if (focus == F_ITEMS) return back();
            break;
        case K_ENTER:
        case K_KPENTER:
        case K_SPACE: return activate(c);
        case K_Q: nextCat(-1); break;
        case K_E: nextCat(1); break;
        case K_T: return tryOn();
        case K_R:
        case K_B: return factory(c);
        case K_C:
            recal = !recal;
            r.toast = recal ? "SPEEDO: RECALIBRATED TO THE TIRES WHEN FITTED" : "SPEEDO: LEFT AS THE FACTORY SET IT";
            break;
        case K_Z: zoomBy(1 / 1.15); break;
        case K_X: zoomBy(1.15); break;
        default: break;
    }
    return r;
}

void Shop::hover(int col, int row) {
    if (col == 0 && focus == F_CATS) setCat(row);
    if (col == 1 && (focus == F_ITEMS || focus == F_PICK)) {
        if (focus == F_PICK && row != sel) focus = F_ITEMS;
        setSel(row);
    }
}

Result Shop::click(int col, int row, Career& c) {
    careerRef_ = &c;
    if (col >= 2) return {};  // (the picker: pointer())
    if (focus == F_PICK) focus = F_ITEMS, dragCtl = -1;
    if (col == 0) {
        if (focus == F_ITEMS && row == cat) return back();
        focus = F_CATS;
        setCat(row);
        return activate(c);
    }
    if (focus != F_ITEMS) {
        focus = F_ITEMS;
        setSel(row);
        return {};
    }
    if (row != sel) {
        setSel(row);
        return {};
    }
    return activate(c);
}

void Shop::turn(double dYaw, double dTilt) {
    spin += dYaw;
    tilt = std::min(1.2, std::max(-0.05, tilt + dTilt));
    idle = 0;
}

void Shop::zoomBy(double f) { zoom = std::min(2.2, std::max(0.5, zoom * f)); }

void Shop::tick(double dt) {
    idle += dt;
    if (idle > 2.5) spin += dt * 0.45;
}

std::string Shop::actionLabel(const Career& c) const {
    if (focus == F_CATS) return "OPEN";
    if (const int pr = paintRow(); pr >= 0) {
        if (focus == F_PICK) return "DONE";
        return pr == PR_FINISH ? "NEXT FINISH" : pr == PR_FACTORY ? (paint.factory() ? "" : "RESET") : "PICK A COLOUR";
    }
    const Item* it = item();
    if (!it) return "";
    if (it->group == G_GEAR) {
        if (!career) return "IN THE TRUCK";
        if (it->tool && it->carried > 0) return "YOU HAVE ONE";
        return (c.cash < it->price ? "NEED $" : "BUY ONE  $") + std::to_string(it->price);
    }
    switch (it->state) {
        case ST_FITTED: return "FITTED";
        case ST_OWNED: return career ? "FIT" : "FIT  (FREE)";
        case ST_FOR_SALE: return (c.cash < it->price ? "NEED $" : "BUY AND FIT  $") + std::to_string(it->price);
        case ST_LOCKED: return "LOCKED";
        default: return "";
    }
}

std::string Shop::hint(bool pad) const {
    if (focus == F_PICK)
        return pad ? "D-PAD UP DOWN CONTROL   LEFT RIGHT CHANGE   A DONE   Y FACTORY COLOUR   B BACK"
                   : "W S CONTROL   A D CHANGE   MOUSE CLICK OR DRAG   R FACTORY COLOUR   ENTER DONE";
    if (paintRow() >= 0)
        return pad ? "D-PAD MOVE   A PICK   LEFT RIGHT FINISH   Y FACTORY PAINT   RIGHT STICK TURN   B BACK"
                   : "W S MOVE   ENTER PICK   A D FINISH   R FACTORY PAINT   DRAG TURN   WHEEL ZOOM   ESC BACK";
    if (focus == F_CATS)
        return pad ? "D-PAD MOVE   A OPEN   LB RB CATEGORY   Y FACTORY PART   B CLOSE" : "W S MOVE   ENTER OPEN   R FACTORY PART   C SPEEDO   ESC CLOSE";
    const Item* it = item();
    const bool mod = it && it->group == G_MOD;
    if (pad)
        return std::string("D-PAD MOVE   A ") + (mod ? "BUY / FIT" : "BUY") + (mod ? "   X TRY ON TRUCK   Y FACTORY PART" : "") + "   RIGHT STICK TURN   B BACK";
    return std::string("W S MOVE   ENTER ") + (mod ? "BUY / FIT" : "BUY") + (mod ? "   T TRY ON TRUCK   R FACTORY PART" : "") + "   DRAG TURN   WHEEL ZOOM   ESC BACK";
}

std::string Shop::title() const { return std::string("GARAGE  ") + vehicleCatalog()[clampi(vehicle, 0, (int)vehicleCatalog().size() - 1)].name; }

bool Shop::hitAt(float x, float y, int& col, int& row) const {
    for (const Hit& h : hits)
        if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) {
            col = h.col, row = h.row;
            return true;
        }
    return false;
}

// ---- The camera ----------------------------------------------------------------------------

namespace {
V3 flatNorm(const V3& v, const V3& fallback) {
    const V3 f(v.x, 0, v.z);
    return len(f) > 1e-6 ? norm(f) : fallback;
}
// Moves the aim point toward the screen's left by `k` m, so what's looked at sits right of the menu.
V3 offAim(const V3& eye, const V3& target, double k) {
    const V3 d = norm(target - eye);
    const V3 right = norm(cross(d, V3(0, 1, 0)));
    return target - right * k;
}
}  // namespace

V3 plinthSpot(const Frame& fr, const VehicleSpec& vs) {
    const V3 fwd = flatNorm(fr.fwd, V3(1, 0, 0)), right = flatNorm(fr.left, V3(0, 0, 1)) * -1.0;
    V3 p = fr.com + right * (vs.width * 0.5 + 2.2) + fwd * (vs.length * 0.15);
    p.y = 0;
    return p;
}

Cam camera(const Shop& s, const Frame& fr, const VehicleSpec& vs, const V3& part, double partSize, double t) {
    const V3 up(0, 1, 0), fwd = flatNorm(fr.fwd, V3(1, 0, 0)), right = flatNorm(fr.left, V3(0, 0, 1)) * -1.0;
    const double L = vs.length, Wd = vs.width;
    const V3 com = fr.com;
    Cam c;
    const Item* it = s.item();
    if (it && !s.previewing) {  // the turntable, the truck behind it
        const double dist = (0.55 + partSize * 2.3) * s.zoom;
        const V3 out = norm(right * 0.8 + fwd * 0.6);
        c.eye = part + out * (dist * std::cos(s.tilt)) + up * (dist * std::sin(s.tilt) + 0.1);
        c.target = offAim(c.eye, part, dist * 0.3);  // (right of the menu)
        return c;
    }
    if (it && s.previewing) {  // the truck, from the side that shows the part
        const int slot = it->slot;
        if (slot == MOD_ENGINE) {
            c.eye = com + fwd * (L * 0.5 + 4.0) + right * 3.2 + up * 2.0;
            c.target = com + fwd * (L * 0.3) + up * 0.1;
        } else if (slot == MOD_TRANSMISSION) {
            c.eye = com + right * (Wd * 0.5 + L * 1.3 + 2.0) - fwd * 0.6 + up * 0.6;
            c.target = com - up * 0.2;
        } else {
            c.eye = com + fwd * (L * 0.62 + 2.4) + right * (Wd * 0.5 + 4.4) + up * 0.7;
            c.target = com + fwd * (L * 0.3) - up * 0.25;
        }
        c.eye = c.eye + (c.eye - c.target) * (s.zoom - 1) * 0.6;
        c.target = offAim(c.eye, c.target, len(c.eye - c.target) * 0.26);
        return c;
    }
    // The whole truck, three-quarter front, drifting slowly.
    const double a = 0.78 + 0.12 * std::sin(t * 0.07), D = (L * 1.05 + 2.4) * s.zoom;
    c.eye = com + fwd * (std::cos(a) * D) + right * (std::sin(a) * D) + up * (1.0 + L * 0.12);
    c.target = offAim(c.eye, com + up * 0.05, D * 0.3);
    return c;
}

// ---- The truck ---------------------------------------------------------------------------

bool liftSync(Sim& sim, const Shop& s, const std::function<bool(const VehicleSpec&)>& ready) {
    if (sim.player < 0 || sim.player >= (int)sim.cars.size()) return false;
    Car& c = sim.cars[sim.player];
    if (c.isProp() || c.remote || !c.spec || c.spec->model != s.vehicle) return false;
    if (c.paint != s.paint) c.setPaint(s.paint);  // (paint is cheap: the PAINT page shows live)
    const ModSet shown = s.open ? s.shown() : s.fitted;
    const VehicleSpec& want = tunedSpec(s.vehicle, shown);
    if (c.spec == &want) return false;
    if (ready && !ready(want)) return false;
    sim.swapPlayerVehicle(want);
    return true;
}

}  // namespace shop
