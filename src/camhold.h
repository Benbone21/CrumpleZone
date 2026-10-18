// When the chase / cockpit camera may drift back to straight behind (ahead of) the car. Header
// only, no SDL or GL (tests/camhold.cpp). A camera the player has turned is held while the car
// is parked or crawling; it only eases back while the car is actually being driven, and the
// pull builds up over a moment instead of snapping when driving starts.
#pragma once
#include <algorithm>
#include <cmath>

#include "mathx.h"

namespace camhold {

constexpr double kDriveSpeed = 3.0 / 3.6;  // m/s: under this (about 3 km/h) with no throttle, the car is parked
constexpr double kThrottle = 0.05;         // this much throttle counts as driving even at a standstill
constexpr double kIdle = 1.5;              // s after letting go of the look control before it starts easing
constexpr double kRate = 2.0;              // 1/s: the recentring rate at full drive weight (as before)
constexpr double kBuild = 1.5;             // s for the pull to build up to full once driving starts

inline bool driving(double speed, double throttle) { return speed > kDriveSpeed || throttle > kThrottle; }

struct Hold {
    double weight = 0;  // 0 parked .. 1 driving: scales the recentring rate
    // `yaw` / `pitch` are the player's look offsets (yaw 0 = straight; pitch below `floor` eases up to it).
    void step(double dt, bool isDriving, bool lookIdle, bool easeYaw, bool easePitch, double& yaw, double& pitch, double floor) {
        weight = isDriving ? std::min(1.0, weight + dt / kBuild) : 0.0;
        if (!lookIdle || weight <= 0) return;
        const double k = 1 - std::exp(-dt * kRate * weight);
        if (easeYaw) yaw = wrapAngle(yaw) * (1 - k);
        if (easePitch && pitch < floor) pitch += (floor - pitch) * k;
    }
};

}  // namespace camhold
