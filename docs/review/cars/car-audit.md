# Car audit, 2026-11-12 (branch `wip/car-pass`, from main `dd7a941`)

Every catalog vehicle looked at in offscreen shots and measured with the existing scenario
tests. Nine vehicles: five listed in the garage (Blazer, Bronco, 4Runner, Tacoma, Jesko) and
four built but hidden (Civic, rock buggy, UTV, Baja Bug).

Real-car figures are from memory, not looked up (no downloads in this pass): treat them as
approximate.

## What this pass changed

| Commit | Kind | What |
|---|---|---|
| `855e216` | visual | Buggy, UTV, Baja Bug: skid plate kept behind the nose. Buggy: link brackets drawn |
| `f4b936d` | visual | Tail end cap laid in level strips: no zig-zag seams across a gated tail (Civic hatch) |
| `84a4817` | tools | `tools/trace/main/hop.cpp`, `bead.cpp`; `tools/trace/build.sh` links again |
| `6fc9413` | **physics** | Leaf axle wind-up damping x6 -> x15 (Tacoma, Blazer): axle hop under power more than halved |

`bench_sim 12 separated 4000` prints `428a06134d54de92` before and after every commit. For
the physics commit that is because the bench only builds the Civic, Jesko, Bronco and 4Runner:
those four are bit-identical, the Tacoma and Blazer are not, and the fingerprint cannot show it.

`<name>-after.jpg` is the sheet after the pass for the four vehicles whose looks changed (Civic,
buggy, UTV, Baja Bug). Everything else is in the tables below as a finding for the owner.

## How the shots were taken

```
XDG_CONFIG_HOME=<scratch> SDL_VIDEODRIVER=offscreen ./crumplezone --no-audio --no-update-check \
  --size 1100x700 --photo --resume-model N --time 11 --orbit YAW,PITCH,DIST \
  --shot-prefix <out> --shot-frames 90
```

Orbit yaw as it is now (agent.md has it the other way round): 0 = from behind, 90 = left side
(nose to the right), 180 = from the front, 135 = front three-quarter, 45 = rear three-quarter.
A pitch of -4 at 3.6 m gives a low view into the wheel wells. There is no camera that gets
under the floor, so there are no true underside shots. `--resume-model` paints every vehicle
the default yellow, not its factory colour.

`<name>-before.jpg` in this folder is each vehicle's sheet at the start of the pass: front 3/4,
rear 3/4, side / front, rear, low.

## Visual findings

| Vehicle | Finding | Status |
|---|---|---|
| Civic | Black zig-zag seams radiating across the hatch panel between the tail lamps and the bumper | **Fixed** (`f4b936d`): `civic-hatch-seams-*.jpg` |
| Rock buggy | Chassis ends of the lower links and track bars hang in mid-air 23 cm under the frame (brackets are beams only, never drawn) | **Fixed** (`855e216`): `buggy-links-*.jpg` |
| Rock buggy | Front skid plate is a flat shelf sticking out ahead of the nose | **Fixed** (`855e216`): `buggy-skidplate-*.jpg` |
| UTV | Same skid plate shelf, ~0.2 m ahead of the nose | **Fixed** (`855e216`): `utv-skidplate-*.jpg` |
| Baja Bug | Same plate, 8 cm past the nose (mostly hidden by the push bar) | **Fixed** (`855e216`) |
| Civic | Headlamps are a low slim strip; the EF's are tall rectangles at the hood's leading edge. Roof spoiler stands on stilts over the hatch glass | Owner decision (restyle) |
| Civic | A dark box hangs ~0.2 m below the rear bumper at the centre (the muffler, drawn across the car) | Owner decision |
| Tacoma | Hood scoop: the TRD Off-Road has none (that is the TRD Pro). Bed looks long against the cab: 5 of 14 lattice columns, ~1.9 m, where the short bed is ~1.6 m outside | Owner decision (re-mesh) |
| Blazer | Engine and blue ancillaries visible through the front wheel openings (no inner fender); the grille texture runs the full height of the face and the chrome bumper reads as a grey-green band | Owner decision |
| Bronco, 4Runner, Jesko | Nothing found beyond style. Wheels sit centred in their arches on all nine; track and wheelbase match the specs | - |
| All | Mottled dark patches on the paint in every shot: it is the dirt/wear layer, not a normals fault | - |

## Physics per vehicle (measured on main, before any change)

Columns: curb weight, front axle share, CG height (structure/weight); 0-100 km/h; top speed
(freeway/top-speed); 100-0 braking; skidpad; static stability factor.

| Vehicle | kg | front % | CG m | 0-100 s | top km/h | brake m | skidpad g | SSF |
|---|---|---|---|---|---|---|---|---|
| Civic (1990 EF Si) | 1033 | 58.8 | 0.52 | 9.30 | 210 | 44.9 | 0.84 | 1.40 |
| Jesko | 1424 | 42.7 | 0.46 (fails, limit 0.46) | 3.10 | 458 | 31.0 | 1.14 | 1.83 |
| Bronco | 2203 | 57.6 | 0.71 | 5.67 | 222 | 42.0 | 0.76 | 1.16 |
| 4Runner (built) | 2100 | 55.9 | 0.79 | 8.83 | 207 | 43.8 | 0.71 | 0.99 |
| Rock buggy | 1819 | 53.8 | 0.76 | 4.87 | 222 | 50.4 | 0.65 | 1.24 |
| UTV | 796 | 44.9 | 0.59 | 4.07 | 143 | 45.0 | 0.77 | 1.23 |
| Tacoma | 2035 | 53.2 | 0.69 | 7.43 | 221 | 43.9 | 0.75 | 1.15 |
| Blazer | 1847 | 55.6 | 0.72 | 10.7 | 175 | 51.5 | 0.65 | 1.14 |
| Baja Bug | 899 | 43.3 | 0.65 | 8.40 | 176 | 47.7 | 0.75 | 1.08 |

Dimensions, gear ratios, tire sizes, spring frequencies and travel in `src/vehicles.cpp` were
read against the real cars. Weights, wheelbases, tracks, ratios and engine ratings match what I
remember of the published figures for the Civic, Jesko, Bronco, Tacoma and Blazer. No single
spec number is clearly wrong. What is off is systematic:

### 1. Every engine makes 15 % more torque than its spec says (owner decision)

`vehicleCatalog()` ends with `for (TorquePoint& tp : s.torque) tp.nm *= 1.15;` (there since the
first commit, no comment). Every spec's comment gives the real rating ("330 hp at 5,250 rpm"),
and the curve is then scaled past it. With no speed governors (replaced by tire speed ratings in
`4499147`), that shows up as:

| Vehicle | Game | Real (approx.) |
|---|---|---|
| Bronco 2.7 0-100 | 5.67 s | 6.6-7.0 s |
| Bronco top speed | 222 km/h | ~160-180 (governed) |
| Tacoma top speed | 221 km/h | ~180 (governed) |
| Civic EF Si top speed | 210 km/h | ~185-190 |
| Tacoma 0-100 | 7.43 s | 7.7-8.2 s |

The test ranges were set round the boosted numbers, so they pass. Recommendation: decide whether
the 15 % is wanted. Removing it puts the 0-100 times on the real cars' and needs the
`dynamics/acceleration` and `freeway/top-speed` ranges re-centred (most still pass as they
are). Not changed here: it changes how every vehicle drives.

### 2. The build moves weight away from where the spec puts it (diagnosed, not landed)

`Car::build`'s stability pass lends mass to light nodes on stiff beams so the 2 kHz explicit
step stays stable, then tilts the body's mass fore-aft and sideways to put the balance back.
The tilt only uses "roomy" nodes (more than twice the mass they need), and nothing restores
the height. Centre of gravity before and after the pass (m; x forward of the car's middle):

| Vehicle | CG height, as specified | as built | x as specified | as built |
|---|---|---|---|---|
| Civic | 0.474 | 0.530 (+56 mm) | 0.186 | 0.173 |
| Jesko | 0.416 | 0.483 (+68 mm) | -0.127 | -0.134 |
| Bronco | 0.767 | 0.739 (-29 mm) | 0.239 | 0.237 |
| 4Runner | 0.835 | 0.817 (-18 mm) | 0.297 | 0.297 |
| Rock buggy | 0.787 | 0.789 | 0.033 | 0.067 |
| UTV | 0.578 | 0.607 (+29 mm) | -0.114 | -0.177 |
| Tacoma | 0.738 | 0.720 (-19 mm) | 0.428 | 0.303 (-124 mm) |
| Blazer | 0.776 | 0.736 (-40 mm) | 0.195 | 0.283 (+88 mm) |
| Baja Bug | 0.653 | 0.663 | -0.344 | -0.052 (+292 mm) |

(Heights are in the build's frame; the tests read ~2 cm lower once the tires squash.)

This is the cause of the open `structure/weight/koenigsegg jesko` failure. On the Jesko the
pass moves 91 kg onto the beltline nodes at 0.8 m (79 kg -> 170 kg; they need 167 kg to step
stably) and takes 112 kg out of the floor.

It also means three specs are compensating: the Baja Bug's items would put 31 % on the front
axle, and the pass moves that to the 43 % the test checks; the Tacoma's would give ~57 %, built
53.2 %.

A fix was written and measured: after the pass, a constrained least-change shift of body-node
mass that restores the specified CG height while holding x and z, never taking a node below a
stability floor. Results by floor (multiple of the mass a node needs at the pass's target):

| Floor | CG heights restored | New failures against main (7 categories, 380 scenarios) |
|---|---|---|
| 1.25 (what the pass leaves its donors) | trucks, buggy, Baja; Jesko and Civic unchanged | `structure/rock-hits/4runner` (2.43 % frame set, the known bistable ledge), `damage/front-wheel-severe` (Bronco pick-ups moved 1.34 cm, needs 1.5); Jesko still fails |
| 1.0 | all but Jesko (0.476), Civic (0.518), Blazer (0.748) | Jesko passes; `rock-hits/4runner`, `front-wheel-severe`, `damage/front-wheel-mild/civic` (0.68 deg, limit 0.60) fail |
| 0.8 (the pass's own last-resort limit) | all but Jesko (0.438) | 21 failures: settle, jiggle, damage/share. Not stable enough |

No floor both fixes the Jesko and leaves the suite clean, so it is **not on this branch**. It is
on `wip/car-pass-cg-height` (one commit on top of this branch) for the owner to look at.
Recommendation: the Jesko's real problem is that its beltline and canopy are built as stiff
as its tub. Softening the lattice above a carbon tub's first cell layer (they are thin
panels on the real car) would cut the mass those nodes need. That changes its crash and torsion
behaviour, so it is a decision, not a tidy-up.

### 3. Tacoma and Blazer: axle hop under power at 16 Hz (more than halved, not cured)

`tools/trace/main/hop.cpp` (new) drives from rest at a set throttle and reads the rear wheel
loads every physics step. On main the Tacoma's rear axle hops at 15-16 Hz, both sides together,
and the hop **grows under steady power with almost no wheel slip**:

| Tacoma, rear load swing (static 4,666 N a side) | main (x6) | landed (x15) | x30 |
|---|---|---|---|
| 25 % throttle, 16 km/h | +-581 N | +-170 N | +-70 N |
| 50 % throttle, 27 km/h | +-1,229 N | +-531 N | +-109 N |
| 50 % throttle, 35 km/h (slip 0.07) | +-1,522 N, hub +-6.3 mm | +-652 N, +-2.7 mm | +-114 N, +-0.5 mm |
| 100 % throttle, 50 km/h | +-593 N | +-108 N | +-8 N |

The Bronco (coils, 5-link) shows nothing. Cause: a leaf-sprung axle's wind-up is held by one
stand-in link from under the diff to the frame (`carbuild.cpp`, "wrap"), a 3,000 kN/m steel arm
with 5,400 N s/m of damping: a loss factor of 0.09 at 16 Hz. Drive torque feeds the wind-up
mode faster than that takes it out. Sweeping only that damping: x1 makes it worse (+-1,986 N),
x10 +-1,055, x15 +-652, x20 +-440, x30 +-114 (flat, no longer growing). Leaf friction (0 to 3x)
does nothing. Softening the link 5x also damps it (+-311 N) but would let the axle wind up
~20 degrees on a launch.

x30 is the value that cures it, and it cannot be used as the build stands: the link's two end
nodes then need ~17 kg each to step stably, the stability pass (section 2) takes that weight
from the body's heaviest nodes, and the Tacoma's front axle share falls from 53.2 % to 52.5 %
(limit 53), the Blazer's ride height error goes to 16.6 mm (limit 12) and 16 Blazer scenarios
fail (settle, braking, fitment). x15 is the most that leaves both trucks' weights where they
are (53.1 %, 9.6 mm), so that is what is landed. Curing it needs either the section 2 balance
fix or damping the wind-up somewhere that does not load a light bracket node. Open.

### 4. Open issues, with what was found

| Issue | Finding | Recommendation |
|---|---|---|
| `structure/weight/koenigsegg jesko` (CG 0.46) | Section 2 | Owner decision |
| `tires/debead` | The bead rolls off when side force / load stays above `0.35 + psi/20` (0.75 at 8 psi). That ratio can never exceed the tire's lateral grip: 0.95 x 1.10 x 0.74 = 0.77 for the 4Runner, and it peaks at 0.70 in the test's turn (`tools/trace/main/bead.cpp`: 0.70 / 0.64 / 0.69 / 0.63, full lock no higher). The feature cannot trigger on pavement for the 4Runner or the Blazer | Lower the constant (`0.25 + psi/20`: 0.65 at 8 psi, 0.85 at 12) or scale it by the tire's lateral grip. It sets how easily players lose a bead, so owner decision |
| `tires/shear` | The Bronco's 45 km/h curb hit loads the studs to 27 kN; `lugShear` is 120 kN ("above every hit in the suite"). The spec and the test contradict each other by design | Six M14 studs really hold several hundred kN. Either retire the "wheel comes off at 45 km/h" check or bring `lugShear` down to where curb hits reach it. Owner decision |
| `components/axle-landing` (buggy, 2 m tail first: 9.49 cm past the stop, limit 8) | The jounce stop's rate is 25x the coil rate, so the buggy's soft 1.3 Hz coils give it a soft stop: stop rate follows spring softness, not the weight landing on it | Size the stop from the corner weight, not the coil rate. Changes every live-axle landing: owner decision (buggy retune is paused) |
| `components/tire-into-fender/rock buggy` (0 kN) | The buggy has no fender to hit: the axle is on its stop before the tire reaches the narrowed frame | The check does not apply to a tube chassis; exempt it as other tube-frame checks are (`tubeFrame()`), or leave it failing as a reminder. Owner decision |
| `engine/performance` | 0.99x real time in one run here, passes in others: load from other processes | No action |
| Frame brackets have no ground contact (`fix/horn-contact`) | Read; not retried. Both attempts gave the brackets contact (tucked, then lifted linkages) and regressed 6 and 14 tests. The buggy's link brackets drawn in `855e216` make the same geometry visible: its link ends hang 23 cm below the chassis | A different approach would be contact on the links themselves (capsules between existing nodes) rather than on the bracket nodes. Not attempted |
| Rock buggy ride height error 22 mm (limit 25) | Same pass as section 2 lends ~60 kg to each live axle's nodes | Part of the paused buggy retune |
| UTV ground clearance 0.31 m | Can-Am's X3 is 0.36-0.41 m | Part of the paused X3 re-spec |

### 5. Small things noticed, not changed

- Bronco item "fuel tank (64 L, full)" weighs 60 kg and the tank is 79 L: the label is wrong, the mass is right.
- Baja Bug: `fuelCapacity` comment says "tank in the nose", its mass item is a fuel cell behind the seats.
- Blazer `engineName` "350 V8 2-BARREL": the 1972 350 was a 4-barrel. Its 117 L tank is the later trucks' 31 gal; 1972 was ~21 gal standard.
- 4Runner `length` 4.656 m: I remember the third generation as ~4.54 m. Not changed without a source.
- `bench_sim` only builds catalog vehicles 0-3 (Civic, Jesko, Bronco, 4Runner), so its fingerprint cannot see a change to the buggy, UTV, Tacoma, Blazer or Baja Bug.
- `tools/trace/build.sh` could not link anything (it read `PHYS` out of the Makefile's text, which now names `$(MODELS)`). Fixed.
- agent.md's orbit yaw convention is out of date (see the top of this page).
