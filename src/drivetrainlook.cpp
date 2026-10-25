#include "drivetrainlook.h"

#include <cstring>

#include "models/drivetrain.h"

using namespace drivetrain;

namespace {

struct Name {
    const char* s;
    bool has(const char* w) const { return s && std::strstr(s, w) != nullptr; }
};

bool is(const VehicleSpec& vs, const char* id) { return vs.id && std::strcmp(vs.id, id) == 0; }

}  // namespace

PartLook engineLook(const VehicleSpec& vs) {
    PartLook L;
    const Name n{vs.engineName};
    const bool blower = vs.induction == VehicleSpec::SUPERCHARGER;
    if (vs.airCooled) {
        L.id = "crate_engine_flat4";
        L.variant = n.has("1835") ? F4_SINGLE_CARB : n.has("2332") || n.has("STROKER") ? F4_STROKER : F4_DUAL_CARBS;
    } else if (vs.cylinders <= 3) {
        L.id = "crate_engine_i3", L.variant = I3_TURBO;
        L.add("stage", n.has("STAGE 2") ? 1 : 0);
    } else if (vs.cylinders <= 4) {
        L.id = "crate_engine_i4";
        if (n.has("TURBO")) {
            L.variant = I4_ECOBOOST;
        } else if (n.has("DOHC") || n.has("VTEC") || n.has("B16") || n.has("B18")) {
            L.variant = I4_HONDA_VTEC;
            L.add("typer", n.has("TYPE R") ? 1 : 0);
        } else {
            L.variant = I4_HONDA_SOHC;
            L.add("header", n.has("HEADER") ? 1 : 0);
        }
    } else if (vs.cylinders <= 6) {
        L.id = "crate_engine_v6";
        if (n.has("5VZ")) {
            L.variant = V6_5VZ;
            L.add("blower", blower ? 1 : 0);
        } else if (n.has("1GR") || n.has("2GR") || vs.induction != VehicleSpec::TURBO) {
            L.variant = V6_GR;
            L.add("gen", n.has("1GR") ? 0 : 1);
            L.add("blower", blower ? 1 : 0);
            L.add("intake", n.has("TUNED") || n.has("INTAKE") ? 1 : 0);
        } else {  // (a twin-turbo six: the EcoBoost)
            L.variant = V6_ECOBOOST;
            L.add("raptor", n.has("3.0") || n.has("RAPTOR") ? 1 : 0);
        }
    } else if (vs.flatPlane) {
        L.id = "crate_engine_v8_dohc", L.variant = V8D_FLAT_PLANE;
    } else {
        L.id = "crate_engine_v8";
        if (n.has("454") || n.has("BIG BLOCK")) {
            L.variant = V8_BIG_BLOCK;
        } else if (n.has(" LS") || n.has("LS3") || n.has("LS7")) {
            L.variant = V8_LS;
            L.add("ls7", n.has("LS7") ? 1 : 0);
            L.add("blower", blower ? 1 : 0);
        } else {
            L.variant = V8_SMALL_BLOCK;
            L.add("carb", n.has("4-BARREL") || n.has("ZZ4") ? 1 : 0);
            L.add("zz4", n.has("ZZ4") ? 1 : 0);
        }
    }
    return L;
}

PartLook gearboxLook(const VehicleSpec& vs) {
    PartLook L;
    const Name n{vs.gearboxName};
    if (vs.transverse) {  // (the engine's own transaxle beside it, whatever its ratios)
        L.id = "gearbox_manual", L.variant = MT_FWD_TRANSAXLE;
        L.add("sequential", n.has("SEQUENTIAL") ? 1 : 0);
    } else if (vs.midEngine) {  // (the transaxle behind it: the diff is in it)
        if (is(vs, "baja")) L.id = "gearbox_manual", L.variant = MT_TYPE1;
        else if (is(vs, "utv")) L.id = "gearbox_cvt", L.variant = CVT_BELT;
        else L.id = "gearbox_manual", L.variant = MT_HYPERCAR;
    } else if (n.has("CVT")) {
        L.id = "gearbox_cvt", L.variant = CVT_BELT;
    } else if (vs.stallRpm > 0) {
        L.id = "gearbox_auto";
        if (n.has("TH350") || n.has("TH400")) {
            L.variant = AT_THM;
            L.add("th400", n.has("TH400") ? 1 : 0);
        } else if (n.has("4L80") || n.has("A340")) {
            L.variant = AT_OVERDRIVE;
            L.add("toyota", n.has("A340") ? 1 : 0);
        } else {
            L.variant = AT_MULTI;
            L.add("speeds", n.has("10-SPEED") || vs.gears.size() >= 8 ? 1 : 0);
        }
    } else {
        L.id = "gearbox_manual";
        if (n.has("SM465")) {
            L.variant = MT_TOPLOADER;
        } else {
            L.variant = MT_ALLOY;
            L.add("sequential", n.has("SEQUENTIAL") ? 1 : 0);
        }
    }
    return L;
}

PartLook transferLook(const VehicleSpec& vs) {
    PartLook L;
    if (vs.midEngine || vs.transverse) return L;
    L.id = "transfer_case";
    switch (vs.transfer) {
        case XFER_NONE: L.id = ""; break;
        case XFER_FULL_TIME: L.variant = TC_FULLTIME; break;
        case XFER_SELECT: L.variant = TC_ATLAS; break;
        case XFER_PART_TIME_BASIC: L.variant = is(vs, "blazer") ? TC_GEAR_IRON : TC_CHAIN; break;
        default: L.variant = TC_CHAIN; break;
    }
    return L;
}
