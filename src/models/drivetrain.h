// The drivetrain part models' designs (src/models/engines_*.cpp, gearboxes.cpp): which design (variant index) of
// which model is which engine or gearbox, and where a gearbox or transfer case bolts on and drives out.
// drivetrainlook.cpp picks them for a car's spec; carparts.cpp places them; garageshop.cpp shows them.
//
// Every one of these models, built with param "bare" = 1, is as fitted in a car: no stand, skid or rails, and
//   an engine:  the crank axis on the X axis through the origin, +X its front (the accessory drive), +Y up,
//               +Z its left; the flywheel face is its rearmost point (nothing of it lies behind that plane).
//   a gearbox / transfer case: its input shaft on the X axis through the origin, +X toward the engine; the face
//               that bolts to the engine (gearbox) or to the gearbox's tail (transfer case) at x = BoxEnds::inX.
// Built without "bare" they stand on the ground for the shop's turntable (an engine on its "mount": stand, skid or
// pulled on timber; a gearbox on rails).
#pragma once
#include <string>

#include "../mathx.h"

namespace drivetrain {

// Designs of each model, in the order they're registered.
enum I4Design { I4_HONDA_SOHC, I4_HONDA_VTEC, I4_ECOBOOST };          // crate_engine_i4
enum I3Design { I3_TURBO };                                           // crate_engine_i3
enum Flat4Design { F4_DUAL_CARBS, F4_SINGLE_CARB, F4_STROKER };       // crate_engine_flat4
enum V6Design { V6_ECOBOOST, V6_5VZ, V6_GR };                         // crate_engine_v6
enum V8Design { V8_SMALL_BLOCK, V8_BIG_BLOCK, V8_LS };                // crate_engine_v8
enum V8DohcDesign { V8D_FLAT_PLANE };                                 // crate_engine_v8_dohc
enum ManualDesign { MT_TOPLOADER, MT_ALLOY, MT_FWD_TRANSAXLE, MT_TYPE1, MT_HYPERCAR };  // gearbox_manual
enum AutoDesign { AT_THM, AT_OVERDRIVE, AT_MULTI };                   // gearbox_auto
enum CvtDesign { CVT_BELT };                                          // gearbox_cvt
enum TransferDesign { TC_GEAR_IRON, TC_CHAIN, TC_FULLTIME, TC_ATLAS }; // transfer_case

// A gearbox's or transfer case's ends as built bare: the x of the face it bolts on by, and the U-joint centres
// (the cross pins) of its output yokes: `rear` the one at its tail, `front` a transfer case's front output.
struct BoxEnds {
    double inX = 0;
    bool rear = false, front = false;
    V3 rearAt, frontAt;
};
// false if `id` isn't a gearbox, CVT or transfer case model.
bool boxEnds(const std::string& id, int variant, BoxEnds& e);

}  // namespace drivetrain
