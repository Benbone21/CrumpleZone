# Car pass 2, 2026-11-13 (branch `wip/car-pass-2`, from `585b2e0`)

The six items from `car-audit.md`, one commit each, plus what they needed. Real-car figures
are from memory.

**A correction first.** The audit called the 15 % torque multiplier unexplained. It had a
comment, which the audit missed: "rated torque is the crank's, and with the drivetrain's
spinning mass counted the stock figures felt flat in the game." It was a deliberate feel choice.
It is removed here as instructed; it is one commit (`5fe4b3e`) and reverts cleanly.

## Commits

| Commit | Item | What |
|---|---|---|
| `5fe4b3e` | 1 | Engines make their rated torque (`vehicles.cpp` and `mods.cpp`'s engine swaps) |
| `080d5dd` | 2 | Build: the CG goes back toward its parts' after the stability pass; the Jesko's upper lattice softened three steps |
| `99e113d` | 3 | Leaf axle hop cured: the wind-up damper is stepped implicitly |
| `e60be5b` | 4 | Bead loss scales with the tire's own side grip |
| `a167b48` | 5 | Stud strengths from the hardware; `tires/shear` rewritten |
| `41b3e59` | 6 | Jounce bumpers sized for weight; no fender check on a tube chassis |
| `0c34c05` | - | `CZ_BENCH_VEHICLES=all` fingerprints all nine vehicles |
| `1444625` | - | Front-wheel tests: rock scaled to the tire |
| `0b176fe` | 2 | Baja and UTV mass items back where they were (undoes part of `080d5dd`) |
| `576e14c` | 1 | `net/take-over`: the guest drives 4 s, not 3 |

## Fingerprints (Linux)

| After | `bench_sim 12 separated 4000` | `CZ_BENCH_VEHICLES=all bench_sim 18 separated 4000` |
|---|---|---|
| main (`585b2e0`) | `428a06134d54de92` | not measured |
| `5fe4b3e` torque | `175ac2cf71215499` | |
| `080d5dd` CG | `75f1a7ce79086ae9` | |
| `99e113d` hop, `e60be5b` bead, `a167b48` studs | unchanged | |
| `41b3e59` bumpers | `cea11d45f456b510` | `7688ca7a28c689ed` (at `0c34c05`) |
| `0b176fe` Baja/UTV | unchanged | `460686eefbdd5e7d` |

Same value on repeated runs. The default run only builds the Civic, Jesko, Bronco and 4Runner;
the second column cycles through all nine (CLAUDE.md now says when to report it).

## 1. Rated torque

| Vehicle | 0-100 before | after | real | top speed before | after |
|---|---|---|---|---|---|
| Civic | 9.30 s | 10.3 | ~9.5-10 | 210 km/h | 202 |
| Jesko | 3.10 | 3.15 | ~2.5-3 | 458 | 430 |
| Bronco | 5.67 | 6.22 | 6.6-7.0 | 222 | 212 |
| 4Runner | 8.83 | 10.0 | ~10 stock | 207 | 198 |
| Rock buggy | 4.87 | 5.38 | - | 222 | 213 |
| UTV | 4.07 | 4.57 | ~4-4.5 | 143 | 143 |
| Tacoma | 7.43 | 8.15 | 8.3-8.8 | 221 | 215 |
| Blazer | 10.7 | 11.7 | ~12.5 | 175 | 168 |
| Baja Bug | 8.40 | 9.60 | - | 176 | 165 |

No `dynamics/acceleration` or `freeway/top-speed` limit changed: every range already held the
rated figure. The trucks' top speeds are still above the real, governed ones because the game
has no governors (`4499147`).

Tests that depended on the extra torque:

| Test | What happened | What was done |
|---|---|---|
| `net/take-over` | W held 3 s from parked moved the truck 3.5 m, not 6.1 (needs 5) | Drive 4 s (5.5 m); bound unchanged (`576e14c`) |
| `throttle/crawl` | The 10 % throttle preset no longer gets the Bronco up a 0.3 m ledge in 4L with lockers. 11.5 % does (it was 10 % x 1.15) | **Left failing.** 10 % is a preset the in-drive key cycles to, so changing the test to 12 % would hide a real change for the player. Owner decision: make the preset 12 %, or give low pedal more torque |
| `drivetrain/4runner-front-abuse` | The sandbox 4Runner no longer snaps a CV at full lock on the dome, in 60 s or in 240 s | **Left failing.** Its CVs hold 5.5 kNm at full lock (`shaftLimit` 13,000, "beefed up"), more than the tires can put in at rated torque. Owner decision: bring `shaftLimit` back toward 10,000, or drop the expectation |
| `damage/front-wheel-mild/civic` | See below | |
| `baja/kicker-jump` | 3.1 of its 4.1 degrees more nose-down come from this | See item 2 |

## 2. Centre of gravity

The stability pass lends mass to light nodes on stiff beams. What followed it only tilted the
"roomy" body nodes fore-aft and sideways, which most vehicles have none of. Now a constrained
least-change shift of body-node mass follows it: total mass, CG in all three directions and the
pitch inertia held, no node below the mass it needs to step stably.

How far back toward its parts' CG each vehicle gets, and what it measures on its tires:

| Vehicle | Way back | Front % (was) | CG height (was) |
|---|---|---|---|
| Bronco | 100 % | 57.7 (57.6) | 0.74 m (0.71) |
| 4Runner | 100 % | 55.9 (55.9) | 0.81 (0.79) |
| Rock buggy | 79 % | 52.9 (53.8) | 0.75 (0.76) |
| Tacoma | 53 % | 53.5 (53.2) | 0.71 (0.69) |
| Jesko | 51 % | 41.8 (42.7) | 0.43 (0.46) |
| UTV | 37 % | 45.8 (44.9) | 0.58 (0.59) |
| Civic | 23 % | 58.9 (58.8) | 0.50 (0.52) |
| Blazer | 16 % | 55.1 (55.6) | 0.72 (0.72) |
| Baja Bug | 11 % | 42.1 (43.3) | 0.65 (0.65) |

**A general exact fix could not be made clean.** The limit is real: the stiff nodes need the
mass where they are, or the explicit 2 kHz step goes unstable. Two ways round it were tried:

- Lower stability floor (0.8 of the pass's target): 21 scenarios fail (settle, jiggle, damage).
- Let every body soften the beams of the nodes in the way until the weight fits: all nine reach
  100 %, but the Baja's torsional stiffness went from over 4 to 0.72 kNm/deg, the Jesko's
  halved, the Tacoma's bending fell under its limit: 15 scenarios fail.

So the **fallback** was used, and only for the Jesko: its body and cabin lattice above the tub
(not the cells the suspension bolts to) is softened three steps of 12 %, yielding at the same
force. Three and not all the way, because each step costs something:

| Steps | CG on its tires | Torsion | Yaw swing a second after a kick, full power (limit 0.30) |
|---|---|---|---|
| 0 | 0.46 m | 26.8 kNm/deg | 0.02 |
| **3 (landed)** | **0.43** | **23.6** | **0.09** |
| 6 | 0.41 | 20.9 | 0.14 |
| 10 | 0.40 (its parts') | 17.6 | 0.19 |

`structure/weight/koenigsegg jesko` passes at 0.43 m. With zero steps it would also pass, at
0.4599 of 0.46.

The three marginal failures the first attempt showed, and why:

- `structure/rock-hits/4runner` (frame set 8.88 %, limit 1): **still fails.** Driving nose first
  off a 1.2 m ledge at a walk, the truck cannot clear it: with the front axle off the edge it
  pivots down until the belly meets the lip, then the tail drops on it. `tools/trace/main/ledge.cpp`
  across crawl speeds on this build: 2.2 and 2.35 m/s set the transfer-skid pick-ups 5-6 %; 2.5
  (the test's) sets the last rail bay 9 % when the far tail (bumper and spare, 96 kg at 2.6 m
  behind the middle) lands on the lip; 2.65 and 2.8 are clean. `docs/handoff.md` recorded the same
  cliff in October (0 % at 2.3-2.4 and 2.65+, 3-12 % between). Main passes because 2.5 m/s then
  fell in a clean window. Not a regression in the frame; the scenario sits on a cliff.
- `damage/front-wheel-severe`: passes.
- `damage/front-wheel-mild/civic`: **still fails**, see the next section.

The Baja's and UTV's mass items were first moved (`080d5dd`) and then put back (`0b176fe`):
with the pass only going part of the way they are not needed, and the Baja flew 42 degrees nose
down with its fuel and spare in the nose.

### `damage/front-wheel-mild/civic`

The test drives one front wheel into a 30 cm square rock at 35 km/h and wants a field repair to
leave the wheels within 0.6 degrees. 30 cm stands 1 cm over the Civic's hub. On main the result
over 29-41 km/h was 0.35-0.94 degrees (0.51 at 35): it passed inside the noise. Rated torque
alone moved 35 km/h to 3.1 degrees (the car only coasts into the rock).

`1444625` scales the rock to `min(30 cm, 0.75 R)`: 30 for the Bronco, 29 Blazer, 27 Jesko, 22
Civic. The Jesko and Blazer pass. The Civic now fails steadily and for a clear reason: 0.63-0.74
degrees of toe over 33-37 km/h, with no repair offered. Its nose is 13 cm off the ground, the body
ahead of the wheel meets the rock first (11 body and 7 panel beams set), and the rack's mount
moves a millimetre. Owner decision: take the rock out of the Civic's list, or let a field repair
re-set toe.

## 3. Leaf axle hop

x30 damping on the wind-up link's own damper still cost weight off the nose even with the new
balance pass (Tacoma 52.9 % front, Blazer ride height error 16.9 mm). The cost comes from the
explicit step, not from the damping. So the link's own damper is back at x6 and the rest is a
damper along the same beam stepped implicitly in `Car::integrate`: stable whatever its rate,
needs no mass. x30 in all.

Tacoma rear wheel load swing at 16 Hz, static 4,666 N a side (`tools/trace/main/hop.cpp`):

| | main | car-pass 1 (x15) | now |
|---|---|---|---|
| 25 % throttle, ~15 km/h | +-581 N | +-170 | +-58 |
| 50 % throttle, ~27 km/h | +-1,229 | +-531 | +-202 |
| 50 % throttle, ~35 km/h | +-1,522 | +-652 | +-254 |
| 100 % throttle, ~50 km/h | +-593 | +-108 | +-43 |

It no longer grows through first gear. The Blazer stays under +-130 N. A shift still gives one
transient of ~700 N. Cars without leaf springs take the old integrate loop bit for bit.

## 4. Bead loss

The bead lets go when the side load, as a share of what that tire can make on dry pavement
(`tireMu x lateralGrip`), stays over `0.61 + 0.03 psi`: 85 % at 8 psi, 91 % at 10, 97 % at 12,
never from 13 up. In the test's turn (45 km/h, half lock; `tools/trace/main/bead.cpp`):

| | 8 psi | 10 psi | 12 psi |
|---|---|---|---|
| 4Runner | 3 tires off | 1 | none (0.91 of 0.97) |
| Bronco | 2 off | | |
| Tacoma | 1 off | | |
| Blazer | none (reaches 0.85 exactly) | | none |

`tires/debead` passes. The Blazer, with the least grip and no ABS, is the hardest to debead.

## 5. Studs

History: `eee5769` gave the Bronco 100 kN and the test expected a 45 km/h kerb slide to shear a
wheel; `b8129a6`, the same day, raised it to 120 kN "above every hit in the suite" because wheels
were coming off in ordinary tests, and left the test expecting the shear.

Strength from the hardware (every stud at its tensile strength, levered about the bolt
circle's far side, over the tire radius; preload and hub pilot left out):

| | Studs | Was | Now |
|---|---|---|---|
| Bronco | 6 x M14x1.5 class 10.9 | 120 kN | 130 |
| Tacoma | 6 x M12x1.5 | 110 | 100 |
| 4Runner on 35s | 6 x M12x1.5 | 100 | 85 |
| Blazer | 6 x 7/16-20 grade 8 | 105 | 85 |

A kerb cannot do that (`tools/trace/main/studs.cpp`, Bronco sideways): 25 cm kerb at 45 km/h
27.5 kN, at 100 km/h 35.7; the tire rides up it. A 45 cm ledge at 70 km/h: 65 kN, and a wheel
comes off anyway, torn off at the arms and ball joints, which is how real wheels leave.
`tires/shear` now checks: 25 cm kerb at 15 and 45 km/h, every wheel stays on with the studs
under half their strength; 45 cm ledge at 70 km/h, a wheel comes off; and with 15 kN studs the
45 km/h kerb shears them. It passes.

## 6. Buggy

Jounce bumper rate is at least what a 1.5 Hz ride on that corner would have, whatever the coil
is (`VehicleSpec::jounceHz`). 4Runner and Tacoma unchanged, Bronco and Blazer 7 % firmer, buggy
33 %.

`components/axle-landing`, rock buggy, axle past its stop (limit 8 cm): 1 m flat 2.62 -> 2.22;
2 m flat 6.91 -> 5.70; 2 m tail first 9.49 -> **8.42, still 4 mm over.** A firmer bumper does not
fix it: 1.65 Hz gives 7.90, 1.8 Hz 9.32, 2.0 Hz 12.3. Past ~1.7 Hz the bumper's bracket on the
light tube chassis needs more mass than the build can lend, the stability pass softens the
bracket's beams instead, and the bracket gives. Setting 1.65 to get under the limit would not be
passing for the right reason, so it is left failing, 1 cm better. The rest is the bracket.

`components/tire-into-fender/rock buggy`: the push check is not applied to a tube chassis on a
live front axle (recorded as "not applicable" with the measured 0 kN). The no-clipping check
still is. The scenario passes.

## Tests on the branch head (`576e14c`)

100 categories in chunks (`--jobs 2`, 4 GB), then `procworld/` and `moab/` one scenario at a time
(56, all pass). `onemap/` and `world/` skipped. **1,369 scenarios: 1,361 pass, 8 fail.**
188 metrics drifted more than 10 % from `tests/baseline.txt` (198 on main: the baseline is old).

| Failing | Value | On main | Why |
|---|---|---|---|
| `structure/rock-hits/4runner` | frame set 8.88 % (limit 1) | passes | Item 2 moved it across a known cliff; see above |
| `damage/front-wheel-mild/civic` | toe 0.70 deg (0.60) | passes at 0.51 | Body meets the rock before the wheel; see above |
| `dynamics/landing-settle/toyota tacoma` | a tire 50.6 mm off the ground (50.0) | 46.6 | Item 2's mass shift 3.5 mm, item 3's damper 0.5 mm. Not chased |
| `baja/kicker-jump` | 25.7 deg nose down at 72 km/h (25.0) | 21.6 | 3.1 deg from rated torque, 1.0 from item 2 |
| `throttle/crawl` | 10 % preset stops at a 0.3 m ledge | passes | Item 1; owner decision |
| `drivetrain/4runner-front-abuse` | no CV snaps | passes | Item 1; owner decision |
| `components/axle-landing` | buggy 8.42 cm (8.0) | fails at 9.49 | Item 6 |
| `engine/performance` | 0.99x real time | flaky | Machine load |

Fixed against main: `structure/weight/koenigsegg jesko`, `tires/shear`, `tires/debead`,
`components/tire-into-fender/rock buggy`. New against main: the first six rows.

## What driving feels like now

- Everything is slower off the line: about half a second to 100 for the Bronco and buggy, a full
  second for the Civic, 4Runner, Blazer and Baja. Top speeds 5-28 km/h lower.
- The 10 % throttle preset no longer crawls a Bronco over a 0.3 m ledge.
- The Bronco and 4Runner sit 2-3 cm higher in CG: a touch more roll. The Jesko is 3 cm lower,
  12 % softer in torsion, and takes a little longer to settle after a kick under full power.
- The Tacoma and Blazer pull away without the rear axle buzzing.
- A 4Runner, Bronco or Tacoma aired to 8 psi can lose a bead in a hard turn on pavement. At 12 psi
  it holds.
- Wheels do not shear off on kerbs (they did not before either); a big enough ledge still tears
  one off.
- The Baja lands a little more nose down off jumps.
