// People on foot (people.h): your own body in first person, posed from the eyes, keeps standing on its feet whatever
// way you look, walking, running or crouched (owner: "looking down, the body sits down and drags on the ground").
#include <cmath>
#include <cstdio>
#include <string>

#include "../src/people.h"
#include "../src/walker.h"
#include "harness.h"

namespace {
struct Gaitway {
    const char* name;
    double speed, crouch;
    double workF = 0, workDown = 0;  // leaning in to a job (holding E): the eyes this far forward and down
};
// (the walker's speeds: walking 1.7 m/s, running 4.6, crouched 45% of a walk)
const Gaitway kWays[] = {{"standing", 0, 0}, {"walking", 1.7, 0}, {"running", 4.6, 0}, {"crouched", 0, 1}, {"crouch-walking", 0.77, 1}, {"leaning in to work", 0, 0, 0.3, -0.6}};

// What the game does each frame on foot (main.cpp): the eyes over the feet (lower crouched), the body posed from them.
struct Worst {
    double eyeOff = 0, nearEye = 0, hipLo = 1e9, hipHi = -1e9, footLo = 1e9, footHi = -1e9, kneeBelowHip = 1e9, footBehind = -1e9, hipAboveKnee = 1e9;
};
Worst pose(const Gaitway& w, double pitch, bool firstPerson) {
    people::Mover m;
    Walker wk;
    wk.crouch = w.crouch;
    double stride = 0;
    const double yaw = 0.7;
    V3 at(3, 10, -2);
    const V3 F(std::cos(yaw), 0, -std::sin(yaw));
    Worst r;
    for (int i = 0; i < 240; i++) {
        const double dt = 1.0 / 60;
        at += F * (w.speed * dt);
        stride += w.speed * dt * 3.2;
        people::Gait g;
        g.p = at, g.yaw = yaw, g.vel = F * w.speed, g.stride = stride, g.crouch = wk.crouchAmount();
        const V3 eye = at + V3(0, wk.eyeHeight(), 0) + F * w.workF + V3(0, w.workDown, 0);
        const V3 lean = eye - (at + V3(0, Walker::kEyeStand, 0));
        people::Pose p;
        if (firstPerson) p = people::firstPerson(g, m, dt, yaw, pitch, eye, lean);
        else g.lookPitch = pitch, p = people::walking(g, m, dt);
        if (i < 120) continue;  // (settled)
        if (firstPerson) {
            const V3 e = people::eyesOf(p) - eye;
            r.eyeOff = std::max(r.eyeOff, std::hypot(e.x, e.z));
            mk::Mesh mesh;
            people::build(p, people::lookFor(V3(0.56, 0.37, 0.2), 7), mesh);
            int n = 0;
            for (const mk::MVert& v : mesh.v) n += len(V3(v.p[0], v.p[1], v.p[2]) - eye) < 0.151;
            r.nearEye = std::max(r.nearEye, (double)n / std::max<size_t>(1, mesh.v.size()));
        }
        const double hy = p.pelvis.y - at.y;
        r.hipLo = std::min(r.hipLo, hy), r.hipHi = std::max(r.hipHi, hy);
        double lowFoot = 1e9;
        for (int s = 0; s < 2; s++) {
            V3 hip, knee, ankle;
            people::legOf(p, s, hip, knee, ankle);
            lowFoot = std::min(lowFoot, ankle.y - at.y);
            r.footHi = std::max(r.footHi, ankle.y - at.y);
            r.kneeBelowHip = std::min(r.kneeBelowHip, hip.y - knee.y);
            if (ankle.y - at.y < 0.115) r.hipAboveKnee = std::min(r.hipAboveKnee, knee.y - ankle.y);  // (a foot on the ground: the knee over it; running, the heel kicks up higher behind)
            // How far the foot is behind the hips (along the way you face): dragged behind the body.
            r.footBehind = std::max(r.footBehind, dot(p.pelvis - ankle, F));
        }
        r.footLo = std::min(r.footLo, lowFoot);
    }
    return r;
}
}  // namespace

SCENARIO(peopleLookDown, "people/look-down", "First person on foot, looking anywhere from straight down to straight up (-1.45..1.45 rad), standing, walking, running and crouched: the hips stay at standing (or crouching) height, a foot stays on the ground, the knees bend forward under the hips (never folded up as if sitting), and the feet stay under the body") {
    for (const Gaitway& w : kWays)
        for (int fp = 1; fp >= 0; fp--)
            for (double pitch = -1.45; pitch <= 1.4501; pitch += 0.29) {
                const Worst r = pose(w, pitch, fp);
                char b[320];
                std::snprintf(b, sizeof b, "%s %s pitch %+.2f: hips %.2f..%.2f m, feet %.3f..%.3f m, hip over knee %.2f, knee over ankle %.2f, foot behind hips %.2f, eyes off %.3f, squashed round the eyes %.1f%%", fp ? "first person" : "third person", w.name, pitch, r.hipLo, r.hipHi, r.footLo, r.footHi, r.kneeBelowHip, r.hipAboveKnee, r.footBehind, r.eyeOff, r.nearEye * 100);
                const bool crouched = w.crouch > 0 || (w.workDown < 0 && fp);  // (third person has no eyes to lean: it stands)
                const double hipMin = crouched ? 0.45 : w.speed > 3 ? 0.80 : 0.84, hipMax = crouched ? 0.70 : 1.0;
                const bool ok = r.hipLo > hipMin && r.hipHi < hipMax && r.footLo > 0.05 && r.footLo < 0.12 && r.kneeBelowHip > (crouched ? -0.05 : 0.2) && r.hipAboveKnee > (crouched ? 0.12 : 0.2) && r.footBehind < 0.5 && (r.eyeOff < 0.03 || !fp) && r.nearEye < 0.002;
                ctx.check(b, ok, b);
            }
}
