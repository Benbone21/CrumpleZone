# Front wheel damage: what bends, what a field repair fixes

User report: front tires often ended up "super tweaked in an impossible way", and no repair
fixed them. Lasting damage should only come from much bigger crashes.

Survey: `./build/run_tests survey/front-wheel-damage -v` (tests/wheeldamage.cpp). It runs each
hit on a fresh Blazer, Civic, Bronco and Jesko on flat ground. The hits are a square 15 cm curb
and a 30 cm rock under one front wheel at 20, 35 and 50 km/h; a wall overlapping the nose by
35 cm at 15, 30, 45 and 65 km/h; flat drops of 1, 2 and 3.5 m; and a 25 m rock field at 30 and
45 km/h. Each run measures camber, toe (against the rear wheels' thrust line), how far the
pick-ups moved, knuckle shape and rail flow. Then it runs `fieldRepair()` with a full kit and
measures again. Set `WD_CAR` and `WD_HIT` to run a subset.

## Root causes (before -> after)

1. **The front knuckles had no shape of their own.** Since the 3 Hz weave fix, a rear
   (unsteered) knuckle has been held as one rigid piece (`Car::Knuckle`, `kKnuckleKappa`). A
   steered knuckle was left out ("its toe is the rack's"), so it was only a light truss of
   `BEAM_KNUCKLE` beams. Ordinary hits folded that truss, and nothing in the repair list
   covered it:
   - Blazer, 30 cm rock at 50 km/h: knuckle beams set 6.5 %, toe +5.7 deg.
   - Jesko, rock at 35 km/h: knuckle set 11.9 %, toe +5.5 deg.
   - Bronco, rock field at 45 km/h: knuckle set 10 %, toe -4.0 deg.
   - Blazer, rock field at 30 km/h: 17 % set on both sides, camber +0.8 deg.

   Now steered knuckles get the same shape hold. Only the shape is held, with no toe spring,
   so steering is unchanged. A steered live axle holds the hub, kingpin pair and steering arm.
   All of the cases above now come out at 0.0-0.3 mm out of shape, with toe under 0.3 deg.
2. **A bent knuckle could never be put right.** When a held shape is shoved more than 3 cm
   out, it re-takes its rest shape from the bent nodes. Field repairs only reset arm and
   tie rod `L0`, so they could not undo that.

   Now `Car::knuckleBent` / `Car::newKnuckle` handle it. A bent knuckle is swapped together
   with its lower arm: the "REPLACE THE BENT KNUCKLE AND ARM" job, which uses the CONTROL ARM
   part. The swap restores the factory rest shape and the beams, and puts the nodes back in
   that shape where the knuckle sat. Bronco, 45 km/h corner into the wall: the knuckle was
   47 mm out of shape before the swap and 0.6 mm after.
3. **The ladder frame brackets bent before the arms bolted to them.** `kBracketYield` was
   70 kN, which is about what bends a control arm here (60-75 kN). Bronco, 30 cm rock at
   50 km/h: the lower-arm bracket flowed 3.6 cm, the toe went -6.7 deg and the camber
   +2.6 deg, and that is frame damage no field repair touches.

   Now brackets yield at 160 kN, about 2.2 times the arm. That is how real brackets are
   built: boxed and gusseted, so the arm or the tie rod is the part that gives. The node
   stays held as stiffly as before, because more stiffness rang at the 2 kHz step (tried:
   holding it 2.3 times stiffer blew the Bronco up on a 20 km/h curb). Instead it flows that
   much further before it sets (`ChassisFrame::yieldScale`). Bronco, rock at 50 km/h: toe now
   +0.1 deg and the pick-ups do not move.
4. **A crumpled corner moved the rack end and nothing could re-set the toe.** Civic, 30 km/h
   corner tap: toe -1.7 and -1.4 deg, with no part bent that a field repair could swap.

   Now a front wheel's tie rod is set to whatever length points it straight again
   (`Car::tieTarget`, an alignment against the thrust line). That happens when the TIE ROD
   job swaps the rod, and in `fieldRepair()`. It works within a real adjusting sleeve's range,
   +-25 mm (`Car::kTieAdjust`). Beyond that the corner is too far out, and it is a frame
   shop's job. Results after the repair:

   | Case | Toe before | Toe after |
   |---|---|---|
   | Civic, rock at 35 km/h | -1.76 | -0.05 |
   | Civic, wall at 30 km/h | -1.71 / -1.42 | +0.06 / -0.15 |
   | Jesko, rock at 50 km/h | -5.3 | -0.7 |
5. **A live axle's tube braces set when it was dragged over rocks.** The braces are a tenth as
   stiff as the housing but yielded at the same strain. Blazer, rock field at 30 km/h: 30 %
   set and both wheels cambered 3 deg. Now they take four times the stretch, about 85 kN,
   which is roughly what bends a 3 in tube.

## Thresholds

- **Field-repairable** (tie rod, control arm with knuckle, alignment): a 30 cm rock at up to
  about 50 km/h, a 15 cm curb at 50, flat drops to 3.5 m, and a corner into a wall at up to
  30 km/h (45 for the Jesko). The regression tests are `damage/front-wheel-mild/<vehicle>`.
- **Unrepairable** (frame or body at the pick-ups, rack shoved past the sleeve): about
  60-70 km/h into something solid. Bronco at 65 km/h: the pick-ups moved 3 cm and the toe is
  still 43 deg off after a field repair. Civic at 65 km/h: 18 cm and 32 deg. The regression
  test is `damage/front-wheel-severe`. A real 64 km/h offset test (IIHS, 40 % overlap) leaves
  the front structure and suspension mounts of most vehicles bent.
- **Grey zone (40-60 km/h):** results depend on the vehicle. A wheel that strikes the wall
  head-on can bend its knuckle (now swappable), and a big hit can move the rack past what the
  sleeve can adjust.

## Not fixed here

- Live front axles (Blazer, rock buggy) do not get the knuckle shape hold: held as one with the housing it pulled the rock buggy off line under braking. Steered IFS knuckles are held at 0.4x the rear kappa (at the full kappa the Bronco rang loose on its light steering arm).
- **Blazer, 30-45 km/h corner into a wall:** the front fenders and wheel wells crush down onto
  both front tires (the tires press on the body with 2-9 kN). The truck then rides on them,
  leaning about 7 deg against its axle, with nothing in the suspension bent. This is body
  damage, a station job. To keep a 30 km/h tap off the tires, the Blazer's nose needs to crush
  less into the wheel wells, or the wheel-well rub needs to push the sheet metal out of the
  way.
- **Bronco, 45 km/h with the wheel itself striking the wall:** after the knuckle swap there
  is still about 7.7 deg of toe, because the alignment would need more than the 25 mm sleeve.
