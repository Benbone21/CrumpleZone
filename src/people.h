// People, posed each frame: you in first person (arms, body, legs and boots when you look
// down; your hands on the tools), drivers in their seats (hands on a steering wheel that
// turns, feet on the pedals) and your buddies on foot (a walk and run cycle).
//
// A body is a skeleton of a few joints, placed in the world each frame, with its limbs
// solved to reach where they're wanted (two-bone IK: a hand on a grip, a foot on a pedal or
// the ground) and drawn round it: rigid pieces cached per outfit (boots, gloved hands at a
// few grip curls, the jacket's chest, the jeans' seat, the head) turned onto their bones,
// and tapered sleeves and legs between the joints. About 3k triangles close up.
// Render only: nothing here touches the physics.
#pragma once
#include <cstdint>
#include <string>

#include "carvisual.h"
#include "modelkit.h"
#include "softbody.h"

namespace people {

// What someone's wearing (the jacket colour picks the rest by a hash, so a buddy keeps theirs).
struct Look {
    V3 jacket, jacketDark, pants, pantsDark, boots, soles, laces, gloves, glovePad, skin, hair, hat;
    int hat_ = 0;  // 0 cap, 1 beanie, 2 bare head
    uint64_t key = 0;
};
Look lookFor(const V3& jacket, uint32_t seed);

// Joints and where the limbs are going, world space. Side 0 is the left, 1 the right.
struct Pose {
    V3 pelvis;                          // between the hip joints
    V3 pelvisF{1, 0, 0}, pelvisU{0, 1, 0};
    V3 chestF{1, 0, 0}, chestU{0, 1, 0};  // the chest, turned about the waist
    V3 headF{1, 0, 0};                  // where the face looks
    V3 hand[2], handX[2], handY[2];     // wrist; fingers' way and the back of the hand's
    double grip[2] = {0.3, 0.3};        // 0 open .. 1 a fist round a handle
    V3 elbowPole[2];                    // the way the elbows point
    V3 ankle[2], footF[2], footU[2];
    V3 kneePole[2];
    bool head = true;     // false: first person (you're looking out of it)
    bool stretch = false; // arms reach past their length at the shoulder (first person: work held further off)
    int detail = 2;       // 2 close, 1 middle, 0 far
};

// The skeleton's fixed points.
V3 shoulderOf(const Pose& p, int side);
V3 hipOf(const Pose& p, int side);

// Appends the posed body to `out` (world space).
void build(const Pose& p, const Look& look, mk::Mesh& out);

// A hand closing round a handle at `g` along `axis` (the thumb's way: toward the tool's head).
void gripAt(Pose& p, int side, const V3& g, const V3& axis, double grip = 0.95);

// On foot. `phase` is the walk cycle (gaitPhase), vel the body's velocity.
struct Gait {
    V3 p;               // feet, on the ground
    double yaw = 0;     // facing
    V3 vel;
    double phase = 0;
    bool grounded = true;
    double lookPitch = 0;  // head (radians, + up)
};
Pose walking(const Gait& g);

// A walk cycle that keeps its own length per stride (a longer one running), from the walker's
// distance-driven stride: one per person, kept between frames.
struct GaitClock {
    bool init = false;
    double lastStride = 0, phase = 0, speed = 0;
    // stride: the walker's (radians, 3.2 per m); dt: s since the last call. Returns the phase.
    double step(double stride, double dt, double speedNow = -1);
};

// First person on foot: the walking body drawn from your eyes (no head), set back from them as you
// look down so you see your chest, legs and boots, not the inside of your collar.
// `lean`: how far the eyes have moved off the standing ones (crouched and leaning in to work): the hips and
// shoulders go with them, the feet stay put.
Pose firstPerson(const Gait& g, double viewYaw, double viewPitch, const V3& lean = V3());

// In a driver's seat: hands on the steering wheel (`wheelTurn`: its turn, radians, + to the left),
// right foot on the throttle (or the brake), the head looking along `look` (or ahead).
struct Seat {
    V3 hip, wheel, pedal;    // world
    V3 fwd, up, right;       // the car's frame
    double wheelR = 0.17, tilt = 0.45;
};
bool seatOf(const Car& c, const VisualTemplate& t, Seat& s);
// Where the seated driver's eyes are (the cockpit view).
V3 seatEye(const Seat& s);
Pose seated(const Seat& s, double wheelTurn, double throttle, double brake, const V3& look);
// The steering wheel (rim, three spokes, hub), turned `wheelTurn`.
void steeringWheel(const Seat& s, double wheelTurn, mk::Mesh& out);
// How far the steering wheel's turned for a car's steering (radians, + left).
double wheelTurnOf(const Car& c);

// Triangle count of the last build() calls since reset (for the stats).
extern int lastTris;

}  // namespace people
