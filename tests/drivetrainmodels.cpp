// Each engine and gearbox drawn as itself (src/drivetrainlook.h, src/models/drivetrain.h): every vehicle's own engine and
// transmission options get their own model and design; every design of the drivetrain models builds, as fitted and as
// shown in the shop, within budget; its settings change it; a gearbox's and transfer case's yokes are on its drawn shape.
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <string>

#include "../src/drivetrainlook.h"
#include "../src/modelkit.h"
#include "../src/models/drivetrain.h"
#include "../src/mods.h"
#include "harness.h"

using namespace drivetrain;

namespace {

int vehicleIndex(const char* id) {
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        if (std::string(vehicleCatalog()[v].id) == id) return v;
    return -1;
}

std::string lookText(const PartLook& L) {
    std::string s = std::string(L.id) + "/" + std::to_string(L.variant);
    for (int i = 0; i < L.n; i++) s += std::string(" ") + L.set[i].key + "=" + std::to_string((int)L.set[i].value);
    return s;
}
double param(const PartLook& L, const char* key) {
    for (int i = 0; i < L.n; i++)
        if (!std::strcmp(L.set[i].key, key)) return L.set[i].value;
    return -1;
}

double checksum(const mk::Mesh& m) {
    double s = 0;
    for (size_t i = 0; i < m.v.size(); i++) s += m.v[i].p[0] * 1.3 + m.v[i].p[1] * 2.1 + m.v[i].p[2] * 0.7 + m.v[i].c[0];
    return s + m.v.size();
}

const char* const kEngines[] = {"crate_engine_i4", "crate_engine_i3", "crate_engine_flat4", "crate_engine_v6", "crate_engine_v8", "crate_engine_v8_dohc"};
const char* const kBoxes[] = {"gearbox_manual", "gearbox_auto", "gearbox_cvt", "transfer_case"};

}  // namespace

SCENARIO(drivetrainEngineLooks, "drivetrain/engine-looks", "Every vehicle's own engine options are drawn as that engine: the 350 small block, the 454, the ZZ4, the LS family, the Honda SOHC and VTEC heads, the EcoBoosts, the Toyota sixes with and without the blower, the flat fours") {
    struct Want {
        const char* vehicle;
        int option;
        const char* id;
        int variant;
        const char* key;  // a param it must have set ("" none)
        double value;
    };
    const Want wants[] = {
        {"civic", 0, "crate_engine_i4", I4_HONDA_SOHC, "header", 0}, {"civic", 1, "crate_engine_i4", I4_HONDA_SOHC, "header", 1},
        {"civic", 2, "crate_engine_i4", I4_HONDA_VTEC, "typer", 0}, {"civic", 3, "crate_engine_i4", I4_HONDA_VTEC, "typer", 1},
        {"jesko", 0, "crate_engine_v8_dohc", V8D_FLAT_PLANE, "", 0},
        {"bronco", 0, "crate_engine_v6", V6_ECOBOOST, "raptor", 0}, {"bronco", 1, "crate_engine_i4", I4_ECOBOOST, "", 0},
        {"bronco", 3, "crate_engine_v6", V6_ECOBOOST, "raptor", 1},
        {"4runner", 0, "crate_engine_v6", V6_5VZ, "blower", 1}, {"4runner", 1, "crate_engine_v6", V6_5VZ, "blower", 0},
        {"4runner", 2, "crate_engine_v6", V6_GR, "blower", 0}, {"4runner", 3, "crate_engine_v6", V6_GR, "blower", 1},
        {"buggy", 0, "crate_engine_v8", V8_LS, "blower", 0}, {"buggy", 1, "crate_engine_v8", V8_LS, "ls7", 1}, {"buggy", 2, "crate_engine_v8", V8_LS, "blower", 1},
        {"tacoma", 0, "crate_engine_v6", V6_GR, "gen", 1}, {"tacoma", 1, "crate_engine_v6", V6_GR, "intake", 1}, {"tacoma", 2, "crate_engine_v6", V6_GR, "blower", 1},
        {"utv", 0, "crate_engine_i3", I3_TURBO, "stage", 0},
        {"blazer", 0, "crate_engine_v8", V8_SMALL_BLOCK, "carb", 0}, {"blazer", 1, "crate_engine_v8", V8_SMALL_BLOCK, "carb", 1},
        {"blazer", 2, "crate_engine_v8", V8_BIG_BLOCK, "", 0}, {"blazer", 3, "crate_engine_v8", V8_SMALL_BLOCK, "zz4", 1},
        {"baja", 0, "crate_engine_flat4", F4_DUAL_CARBS, "", 0}, {"baja", 1, "crate_engine_flat4", F4_SINGLE_CARB, "", 0}, {"baja", 2, "crate_engine_flat4", F4_STROKER, "", 0},
    };
    for (const Want& w : wants) {
        const int v = vehicleIndex(w.vehicle);
        if (v < 0 || w.option >= modOwnCount(v, MOD_ENGINE)) {
            ctx.check(std::string(w.vehicle) + " engine option " + std::to_string(w.option) + " exists", false);
            continue;
        }
        ModSet ms;
        ms.pick[MOD_ENGINE] = w.option;
        const VehicleSpec& s = tunedSpec(v, ms);
        const PartLook L = engineLook(s);
        const std::string tag = std::string(w.vehicle) + " " + s.engineName + ": ";
        ctx.check(tag + "drawn as " + w.id + "/" + std::to_string(w.variant), std::string(L.id) == w.id && L.variant == w.variant, lookText(L));
        if (*w.key) ctx.check(tag + w.key + " = " + std::to_string((int)w.value), param(L, w.key) == w.value, lookText(L));
    }
    // Every engine on offer anywhere (swaps included) is a design that exists.
    std::set<std::string> looks;
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        for (int o = 0; o < (int)modOptions(v, MOD_ENGINE).size(); o++) {
            ModSet ms;
            ms.pick[MOD_ENGINE] = o;
            const PartLook L = engineLook(tunedSpec(v, ms));
            const mk::ModelDef* d = mk::findModel(L.id);
            ctx.check(std::string("a model for ") + tunedSpec(v, ms).engineName, d && L.variant >= 0 && L.variant < (int)d->variants.size(), lookText(L));
            looks.insert(lookText(L));
        }
    ctx.check("the engines look different (at least 20 looks)", looks.size() >= 20, std::to_string(looks.size()));
}

SCENARIO(drivetrainGearboxLooks, "drivetrain/gearbox-looks", "Every vehicle's own gearbox and transfer case is drawn as that box: the SM465 and NP205, the TH350 and TH400, the 4L80E and the Atlas, the A340F and its full-time case, the modern multi-speeds, the transaxles and the CVT") {
    struct Want {
        const char* vehicle;
        int option;
        const char* id;
        int variant;
        const char* key;
        double value;
    };
    const Want wants[] = {
        {"blazer", 0, "gearbox_manual", MT_TOPLOADER, "", 0}, {"blazer", 1, "gearbox_auto", AT_THM, "th400", 0},
        {"buggy", 0, "gearbox_auto", AT_OVERDRIVE, "toyota", 0}, {"buggy", 1, "gearbox_auto", AT_THM, "th400", 1},
        {"4runner", 0, "gearbox_auto", AT_OVERDRIVE, "toyota", 1},
        {"tacoma", 0, "gearbox_auto", AT_MULTI, "speeds", 0}, {"bronco", 0, "gearbox_auto", AT_MULTI, "speeds", 1},
        {"bronco", 3, "gearbox_manual", MT_ALLOY, "sequential", 0},
        {"civic", 0, "gearbox_manual", MT_FWD_TRANSAXLE, "sequential", 0}, {"civic", 3, "gearbox_manual", MT_FWD_TRANSAXLE, "sequential", 1},
        {"jesko", 0, "gearbox_manual", MT_HYPERCAR, "", 0}, {"baja", 0, "gearbox_manual", MT_TYPE1, "", 0}, {"utv", 0, "gearbox_cvt", CVT_BELT, "", 0},
    };
    for (const Want& w : wants) {
        const int v = vehicleIndex(w.vehicle);
        if (v < 0 || w.option >= modOwnCount(v, MOD_TRANSMISSION)) {
            ctx.check(std::string(w.vehicle) + " transmission option " + std::to_string(w.option) + " exists", false);
            continue;
        }
        ModSet ms;
        ms.pick[MOD_TRANSMISSION] = w.option;
        const VehicleSpec& s = tunedSpec(v, ms);
        const PartLook L = gearboxLook(s);
        const std::string tag = std::string(w.vehicle) + " " + s.gearboxName + ": ";
        ctx.check(tag + "drawn as " + w.id + "/" + std::to_string(w.variant), std::string(L.id) == w.id && L.variant == w.variant, lookText(L));
        if (*w.key) ctx.check(tag + w.key + " = " + std::to_string((int)w.value), param(L, w.key) == w.value, lookText(L));
    }
    const std::map<std::string, int> cases = {{"blazer", TC_GEAR_IRON}, {"bronco", TC_CHAIN}, {"tacoma", TC_CHAIN}, {"4runner", TC_FULLTIME}, {"buggy", TC_ATLAS}};
    for (const auto& kv : cases) {
        const int v = vehicleIndex(kv.first.c_str());
        if (v < 0) continue;
        const PartLook L = transferLook(vehicleCatalog()[v]);
        ctx.check(kv.first + ": its transfer case is design " + std::to_string(kv.second), std::string(L.id) == "transfer_case" && L.variant == kv.second, lookText(L));
    }
    for (const char* id : {"civic", "jesko", "baja", "utv"}) {
        const int v = vehicleIndex(id);
        if (v >= 0) ctx.check(std::string(id) + ": no transfer case drawn", !*transferLook(vehicleCatalog()[v]).id, lookText(transferLook(vehicleCatalog()[v])));
    }
}

SCENARIO(drivetrainDesigns, "drivetrain/designs", "Every design of every engine, gearbox and transfer case model builds as fitted (bare) and as shown in the shop, within budget; each of its settings changes it; a box's yokes are on its drawn shape and its face is where it says") {
    for (const char* id : kEngines) {
        const mk::ModelDef* d = mk::findModel(id);
        ctx.check(std::string(id) + " exists", d != nullptr);
        if (!d) continue;
        for (int v = 0; v < (int)d->variants.size(); v++) {
            const std::string tag = std::string(id) + " " + d->variants[v].name + ": ";
            mk::P bare = mk::defaults(d->variants[v]), shown = bare;
            bare.v["bare"] = 1;
            mk::Mesh mb, ms;
            mk::build(*d, v, bare, mb), mk::build(*d, v, shown, ms);
            ctx.check(tag + "fitted within 3000 triangles", mb.triangles() > 200 && mb.triangles() <= 3000, std::to_string(mb.triangles()));
            ctx.check(tag + "shown within 3600 triangles", ms.triangles() <= 3600, std::to_string(ms.triangles()));
            ctx.check(tag + "fitted about its crank axis", mb.lo.y < -0.08 && mb.hi.y > 0.2 && mb.lo.x < -0.15 && mb.hi.x > 0.15, std::to_string(mb.lo.y) + ".." + std::to_string(mb.hi.y));
            ctx.check(tag + "an engine's size", mb.hi.x - mb.lo.x > 0.35 && mb.hi.x - mb.lo.x < 1.1 && mb.hi.z - mb.lo.z > 0.3 && mb.hi.z - mb.lo.z < 1.1,
                      std::to_string(mb.hi.x - mb.lo.x) + " x " + std::to_string(mb.hi.z - mb.lo.z));
            ctx.check(tag + "shown on the ground", ms.lo.y > -0.02 && ms.lo.y < 0.05, std::to_string(ms.lo.y));
            for (const mk::Param& q : d->variants[v].params) {
                if (q.key == "seed" || q.key == "wear") continue;  // (wear shows as grime; seed only with wear)
                mk::P other = bare;
                other.v[q.key] = q.names.empty() ? (q.def == q.hi ? q.lo : q.hi) : (q.def >= 1 ? 0 : 1);
                if (q.key == "mount") other = shown, other.v[q.key] = q.def == 1 ? 0 : 1;
                mk::Mesh mo, base;
                mk::build(*d, v, other, mo);
                mk::build(*d, v, q.key == "mount" ? shown : bare, base);
                ctx.check(tag + q.key + " changes it", std::fabs(checksum(mo) - checksum(base)) > 1e-6);
            }
        }
    }
    for (const char* id : kBoxes) {
        const mk::ModelDef* d = mk::findModel(id);
        ctx.check(std::string(id) + " exists", d != nullptr);
        if (!d) continue;
        for (int v = 0; v < (int)d->variants.size(); v++) {
            const std::string tag = std::string(id) + " " + d->variants[v].name + ": ";
            mk::P bare = mk::defaults(d->variants[v]);
            bare.v["bare"] = 1;
            mk::Mesh m, shown;
            mk::build(*d, v, bare, m), mk::build(*d, v, mk::defaults(d->variants[v]), shown);
            ctx.check(tag + "fitted within 2000 triangles", m.triangles() > 100 && m.triangles() <= 2000, std::to_string(m.triangles()));
            ctx.check(tag + "shown on the ground", shown.lo.y > -0.02 && shown.lo.y < 0.05, std::to_string(shown.lo.y));
            BoxEnds e;
            ctx.check(tag + "has its ends", boxEnds(id, v, e));
            ctx.check(tag + "its face is its front", m.hi.x > e.inX - 0.01 && m.hi.x < e.inX + 0.16, std::to_string(m.hi.x) + " for " + std::to_string(e.inX));
            auto near = [&](const V3& p) {
                double best = 1e9;
                for (const mk::MVert& q : m.v) best = std::min(best, len(V3(q.p[0], q.p[1], q.p[2]) - p));
                return best;
            };
            if (e.rear) ctx.check(tag + "the rear yoke is on it", near(e.rearAt) < 0.05 && e.rearAt.x < m.lo.x + 0.1, std::to_string(near(e.rearAt)));
            if (e.front) ctx.check(tag + "the front yoke is on it", near(e.frontAt) < 0.05 && e.frontAt.x > e.inX - 0.05, std::to_string(near(e.frontAt)));
            ctx.check(tag + "a drive line out of it (or it's a transaxle)", e.rear || id == std::string("gearbox_manual"));
            if (std::string(id) != "transfer_case") ctx.check(tag + "no front output", !e.front);
            for (const mk::Param& q : d->variants[v].params) {
                if (q.key == "finish" || q.key == "shifter" || q.key == "lever" || q.key == "converter") continue;  // (a colour; the levers the car hides; the converter shows out of the car only)
                mk::P other = bare;
                other.v[q.key] = q.names.empty() ? (q.def == q.hi ? q.lo : q.hi) : (q.def >= 1 ? 0 : 1);
                mk::Mesh mo;
                mk::build(*d, v, other, mo);
                ctx.check(tag + q.key + " changes it", std::fabs(checksum(mo) - checksum(m)) > 1e-6);
            }
        }
    }
}
