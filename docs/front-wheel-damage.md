# Front wheel damage: what bends, what a field repair fixes

The report: front tires often ended up bent at angles no real part allows, and nothing could
fix them. Lasting damage should only come from much bigger crashes.

The survey is `./build/run_tests survey/front-wheel-damage -v` (tests/wheeldamage.cpp). It
puts a fresh Blazer, Civic, Bronco and Jesko on flat ground and hits each one in these ways:

- a square-edged 15 cm curb under one front wheel at 20, 35 and 50 km/h;
- a 30 cm rock under one front wheel at the same speeds;
- a wall that overlaps the nose by 35 cm, at 15, 30, 45 and 65 km/h;
- flat drops from 1, 2 and 3.5 m;
- a 25 m rock field at 30 and 45 km/h.

After each hit it measures camber, toe (against the rear wheels' line), how far the pick-ups
moved, the knuckle's shape and how far the frame rails flowed. Then it runs `fieldRepair()`
with a full kit and measures again. To run a subset, set `WD_CAR` and `WD_HIT`. The numbers
below come from the final build: steered IFS knuckles held at 0.4x the rear kappa.

## Root causes (before -> after the field repair)

1. **The front knuckles had no shape of their own.** Steered knuckles were left out of the
   rigid shape-hold that rear knuckles get. Their light truss of `BEAM_KNUCKLE` beams folded,
   and no repair covered it. Before:
   - Blazer, 30 cm rock at 50 km/h: knuckle beams set 6.5 %, toe +5.7 deg.
   - Jesko, rock at 35 km/h: knuckle set 11.9 %, toe +5.5 deg.
   - Bronco, rock field at 45 km/h: toe -4.0 deg.

   Fixes:
   - Steered IFS knuckles are now held in shape. Only the shape is held, with no toe spring.
     They are held at 0.4x the rear kappa; at the full kappa the Bronco rang loose on its
     light steering arm.
   - A steered live axle's hub and steering arm are held too. The kingpin pair is left out,
     because it also belongs to the housing: held by both, it pulled the rock buggy off line
     under braking.
   - A steered live axle's own knuckle beams take 3x the strain before they set.

   Now:
   - Blazer, rock at 50 km/h: 0.1 mm out of shape, toe 0.04 deg after the repair.
   - Jesko, rock at 35 km/h: toe -0.03 deg.
   - Bronco, rock field at 45 km/h: toe +0.23 deg, and no repair is needed.
2. **A bent knuckle could never be put right.** Its held shape re-takes rest from the bent
   pose, and a repair only reset the beams' rest lengths. There is now a "REPLACE THE BENT
   KNUCKLE AND ARM" job (`Car::newKnuckle`; it uses the CONTROL ARM part). It restores the
   factory shape and puts the nodes back. Bronco, wall at 45 km/h: the knuckle goes from
   47.6 mm out of shape to 0.9 mm.
3. **The ladder-frame brackets bent before the arms did.** They yielded at 70 kN, about what
   an arm takes. Bronco, rock at 50 km/h: the bracket flowed 3.6 cm, toe -6.7 deg,
   unrepairable. An arm's or tie rod's bracket now yields at 160 kN, about 2.2x an arm. Its
   stiffness is unchanged: held stiffer, it rang at the 2 kHz step. Instead it flows further
   before it sets (`ChassisFrame::yieldScale`). Coil towers and pads stay at 70 kN, so a 5 m
   drop onto rock still kinks the frame. Bronco, rock at 50 km/h now: toe +0.76 deg, and the
   pick-ups move 0.1 cm.
4. **A crumpled corner shoved the rack end, and nothing re-set the toe.** The tie rod job and
   `fieldRepair` now set the toe against the thrust line (`Car::tieTarget`), within a ±25 mm
   adjusting sleeve. Within 2 mm of straight it is left at straight. The toe is not read while
   the car is jacked up or the wheel is off.
   - Civic, rock at 35 km/h: toe -1.84 -> -0.05 deg.
   - Civic, wall at 30 km/h: toe -1.73 / -1.43 -> +0.07 / -0.15 deg.
5. **The live-axle tube braces yielded too early.** They took 4x less stretch than their
   strength called for. They now take 4x the stretch, about 85 kN.
6. **A live axle had no steering stop.** A rock kicked a Blazer's wheel past full lock and
   the linkage folded over, leaving it 78 deg off with nothing bent. Steered live axles now
   stop on their housing just past full lock.

## Thresholds

- **Field-repairable** (tie rod, arm with knuckle, toe set):
  - a 30 cm rock at up to about 50 km/h;
  - a 15 cm curb at 50 km/h;
  - flat drops up to 3.5 m;
  - a rock field;
  - a corner into a wall at up to 30 km/h (45 for the Jesko).

  Tests: `damage/front-wheel-mild/<vehicle>`.
- **Unrepairable** (frame or body at the pick-ups, or the rack shoved past the sleeve): about
  60-70 km/h into something solid.
  - Bronco at 65 km/h: pick-ups moved 4.1 cm, toe +37 deg after a field repair.
  - Civic at 65 km/h: pick-ups moved 18 cm, toe -29 deg.

  This is comparable to a real 64 km/h 40 % offset crash test. Test:
  `damage/front-wheel-severe`.
- **Grey zone (40-60 km/h):** the result depends on the vehicle.

## Known left

- **Blazer, 30-45 km/h corner into a wall:** the fenders crush down onto both front tires
  (2-9 kN of push), and the truck leans about 7 deg on them with nothing in the suspension
  bent. That is body damage, a station job. At 45 km/h the struck wheel also ends up on its
  steering stop: toe +41 deg, still +22 deg after the repair.
- **Bronco, 45 km/h with the wheel itself striking the wall:** 7.9 deg of toe is left after
  the knuckle swap. Setting it would need more than the 25 mm sleeve.
- **Jesko, 30 cm rock at 50 km/h:** the toe is set before the newly swapped knuckle has
  settled, so it ends at +4.0 deg instead of -1.9. The fix is to set the toe a moment after
  the knuckle swap.
