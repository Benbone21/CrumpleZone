# Obstacle sections and the expedition generator: handoff

Written 2026-10-16 by a Claude Code session working with the owner. It covers the goals agreed in
that session, the design, what was measured in the physics, the prototype that exists, what went
wrong, and what is still open. Start here before touching terrain generation.

**Where things stand:** an approved design, a physics feasibility spike with numbers, and a
**throwaway prototype** of seven obstacle-section generators (in `prototypes/sections/`, outside the
build) that export playable levels. **Update: the generators now live in `src/sections.*` and the
Proving Ground shows them, see §8.** The owner rejected the first
visuals and wants to hand-build sections in the editor as well as generate them. The next step, a
section library plus a stitcher, is designed below but **not yet approved by the owner**.

---

## 1. What the owner wants

- **A co-op game about travelling somewhere with a buddy** in your trucks, working together to get
  over and through obstacles.
- **Hard challenges.** The owner's words: the current worlds "suck" and the existing obstacle
  courses are "the dumbest and easiest things ever". Difficulty is the main goal.
- **Procedural generation, and levels too.**
- **The owner builds and tests content themselves.** Don't validate courses with the autopilot or
  with scripted drives for gameplay purposes. The owner said "not the autopilot or you". Scripted
  drives were used only to debug the physics.
- **The owner wants to hand-make sections** in the world editor and have the procedural system use
  them alongside generated ones.

### Decisions made (owner-approved)

| Topic | Decision |
|---|---|
| Core loop | **Expedition runs:** a chain of 4–6 seeded stages, camp to camp, obstacle sections along the way, difficulty rising per stage. Existing `.czl` levels stay as a campaign or practice mode. |
| Fail state | **Respawn at the last camp, with a cost** (time, fuel or spare parts). Camps are checkpoints, repair and refuel points. |
| Kinds of challenge | All four: **driving skill and line choice**, **problem-solving with tools and the buddy** (winch, strap, airing down), **survival and attrition** (fuel, damage, parts), **high-stakes commitment** (no-return drops, real consequences). |
| Generator approach | **Stage-sized seeded grids** (the existing 1201×1201, 1 m `World` grid). No streaming chunks. In multiplayer only the seed and stage need to be synced. |
| Autopilot | **Out.** Don't use `planRoute` or autopilot playtests to validate. Validation happens by construction plus the owner playing. |

### Section types the owner asked for

Steep hill · mud pit with varying depths · river with lots of rocks · steep rock wall · sand dunes ·
cliff shelf road (barely any room, rocks and dips in the road) · **trees blocking the path** (later:
get out, cut the trees, pull them clear with the winch).

From a second batch of reference photos (these weren't saved as files):
- a granite shelf road with big boulders on both sides;
- a red-rock ledge shelf along a cliff, as in Moab;
- a narrow cut road across a steep grassy mountainside, with snow patches and a big drop;
- a talus/scree track through pines;
- a snowy valley track.

### "Rock wall" means real rock crawling

Reference images are in `docs/reference-images/rockwall-*.webp`. What makes them read as a rock wall:
- layered sandstone ledges with rounded lips that **curve across the trail** (not straight lines);
- risers **0.6–1.5 m**, taller than a tire;
- big slabs and boulders stacked on the ledges;
- slot-canyon walls with a **V-notch** floor that wedges and tips the truck;
- one climbable line among faces that are too tall;
- a pour-over pool below a ledge.

The first prototype (`prototype-wall-v1-rejected.png`), flat slabs on a gentle ramp, was called
"complete trash". v3 (`prototype-wall-v3-slabs.png`) "looks better" but the owner still wants to
edit it by hand.

---

## 2. Design

The original spec, `docs/superpowers/specs/2026-10-16-expedition-generator-design.md`, is
**outdated**: it predates the pivot to challenge design. Treat this section as the current design.

### 2.1 Engine layers

1. **Capability model.** One table of what each vehicle and setup can do: max ledge, max slab angle
   at each tire pressure, wading depth, safe side slope. It is owned by the generator and seeded
   from the measured numbers in §3. Don't depend on `src/autopilot.*` or `pilotsense.*`; copy the
   numbers instead.
2. **Obstacle templates.** Each template declares what it tests, maps its parameters to the kit
   they require, the cost of failing, and the buddy's role. Parameters are set **between "fails with
   setup X" and "passes with setup Y"**, so the player has to work out Y. That gap is the puzzle.
3. **Composer.** Builds a stage:
   - a spine of 6–10 slots from camp to camp;
   - a difficulty ladder by stage;
   - combination rules: a commitment drop is followed by at least two obstacles before the next
     camp; a consequence follows skill (a drop beside an off-camber, a crossing right before a
     climb); never the same template twice in a row;
   - 1–2 branches (a short hard line or a long one that costs fuel);
   - one crux per stage, in the final third;
   - a fuel budget sized so that detours and winching cost.
4. **Validation by construction** (no simulation):
   - every obstacle sits in a **sealed chokepoint** (walls ≥ 65°, see §3, a river, or a tree wall)
     so it can't be driven around;
   - parameters come from the capability table;
   - a cheap geometric self-check: the chokepoint is sealed, anchors are in rope reach, camps are
     clear;
   - if the check fails, regenerate deterministically from seed + k.
5. **Levels from the same engine.** Good seeds can be saved as `.czl` and curated into a campaign.

### 2.2 Obstacle templates (agreed list)

| Template | Tests | Tuned so that… | Failing costs | Buddy role |
|---|---|---|---|---|
| Ledge staircase | skill | steps above what 4H climbs; 4L + lockers + a good line needed | stuck, a broken shaft | spotter |
| Slickrock slab | problem-solving | steep enough to need ~15 psi | sliding back, damage | winch if botched |
| Off-camber shelf | commitment | lean near the tipping point, a real drop on the low side | rollover off the edge | belay winch from above |
| Twister / V-notch | skill | the axles cross up; open diffs bog | stuck, high-centred | strap |
| Boulder garden | line choice | only 1–2 lines clear the underbody | body damage | spotter |
| Bog with no anchors | problem-solving + co-op | too deep to drive; no trees in winch reach | stuck until rescued | buddy's truck is the anchor |
| Deep crossing | problem-solving + attrition | deeper than stock air intakes, with current | hydrolock | tow, or scout the depth on foot |
| Washout gap | co-op | too wide, logs lying nearby | none until solved | tow logs in to bridge it (**needs new code**, see §3.2) |
| Wall climb | co-op | taller than any solo capability | none | winch chain |
| One-way drop | commitment | can't be climbed back | committed to the rest | lower the second truck on the winch |

### 2.3 Proposed next step (not yet approved): section library and stitcher

- **A section** is a strip about 180 m long along +X with an **entry** and an **exit**. It has
  two sources, which produce the same data (heights relative to the entry, surfaces, water,
  objects):
  - **Hand-made** in the existing world editor, saved as a normal `.czl`. The course tool's
    **start** is the entry and its **last gate** is the exit. The `about` field carries a tag such
    as `SECTION WALL 4` (type and difficulty 1–5). No editor changes are needed for v1.
  - **Generated** by code generators, moved from `prototypes/sections/` into `src/`, with their
    parameter ranges in a text file the owner can tune.
- **The stitcher** builds a stage from the composer's choice of sections: each entry snaps to the
  previous exit, heights are matched, seams blended over about 15 m, the surroundings filled with
  procedural land, camps placed between sections. The output is a `World` (to play) and/or a `.czl`
  (to open in the editor and fix).
- Level files store **absolute cell indices on one map**. Reading a section back means converting
  to local coordinates relative to its entry.

---

## 3. Physics findings (measured)

Measured with throwaway probes (`prototypes/sections/probe.cpp`) on a flat, cleared region of the
proving ground, using scripted controls.

### 3.1 Spike results

**Sealing a chokepoint with a grid wall** (4 m tall, rock surface, best setup: 4L, both lockers,
12 psi):

| Wall angle | Rock buggy | Bronco |
|---|---|---|
| 35° | over the top | over the top |
| 45° | over the top | 3.7 m, held |
| 55° | 1.3 m, held | 0.35 m |
| 65° | 0.45 m | 0 |
| 75° | 0 | 0 |

→ **Use ≥ 65° (rock) for anything that must seal.** Grid faces behave as smooth slopes, never as
square edges.

**Off-camber traverse, Bronco crawling along a side slope:**

| Slope | Stock, dirt | Stock, rock | Set up, dirt | Set up, rock |
|---|---|---|---|---|
| 25° | ok | ok | ok | ok |
| 30° | **slides 12 m** | ok | ok | ok |
| 35° | slides 40 m | slides 34 m | ok (53° lean) | ok |
| 40° | **rolls** | slides | **rolls** | slides 39 m |

→ The tier gap is **30° on dirt** (stock slides, set up holds), and **40°** means rolling or
sliding. A shelf at 32–36° with a drop on the low side is a real commitment test.

**Mud pit, solo Bronco set up, 14 m long, banks 1:2, at about 3 m/s:** it drove through every
depth from 0.6 to 1.5 m dry. At 1.5 m with water, the **engine drowned**.
→ **Mud does not trap trucks by sinking** (`SURF_MUD` sink is only 0.13 m). Mud pits need **steep
exit banks** (about 1:1), length, water depth, ruts, or no momentum (a tight entry).

### 3.2 What the world can and can't express (from a code audit)

- **Vehicle lineup:** the Civic, UTV and rock buggy are **no longer offered in the game**
  (`listedVehicles()` in `src/vehicles.h`: Blazer, Bronco, 4Runner, Tacoma, Jesko). Their numbers in §3.1
  (the buggy columns) and the UTV/Civic remarks below are history; §3.3 is the capability table for the
  current lineup. They are still in the vehicle catalog, and the garage still offers their parts to the
  other trucks.
- **Terrain:** a 1 m grid, bilinear; contact samples one point under each wheel; no max-slope clamp.
  It **can't make crisp lips**. Existing maps make square ledges with **rock boxes**
  (`Box.rock = true`): `addSteps` in `backcountry.cpp`, `staircase` in `frontier.cpp`.
- **Rock hulls** (`World::addRock`): convex, collided exactly as drawn, placed **relative to the
  terrain height only**, so they can't be stacked. With scanned hulls on, the scan whose proportions
  fit best is used.
- **Rock slab boxes:** can sit **at any height and orientation**, so they stack. They're the best
  primitive for sandstone strata and ledges.
- **Winch:** 30 m line plus a 30 m extension (59 m reach), stalls at 44 kN (UTV 20 kN). Anchors are
  trees and poles with r ≥ 0.15, rocks with r ≥ 0.45, other vehicles, and **`ANCHOR_STAKE`, a ground
  anchor that works anywhere** unless the water there is over 0.3 m deep (`recovery.cpp:407-419`).
  **Stakes defeat any "no anchor in reach" puzzle**, and rocks with r ≥ 0.45 are anchors too.
  Possible fix: no stakes on bare rock, scree, ice or deep mud. Rock slab boxes are not anchors,
  which helps.
- **Tow strap:** 9 m, **vehicle to vehicle only**.
- **Logs are static capsules.** Nothing movable exists except proving-ground crash props
  (wheel-less soft-body `Car`s with `isProp`), which are excluded from winch and strap. **The washout
  gap / tow-a-log template and "cut the tree and winch it away" need new code**: dynamic log props,
  and letting ropes attach to them.
- **Water:** buoyancy and drag against flow; hydrolock when the intake (highest engine node) goes
  under. Only the 4Runner has a snorkel. The Civic drowns at 0.7–1.2 m.
- **Tipping:** no rollover threshold in code. The tilt-table test implies 41–52° for a Bronco, but
  tires slide first on normal ground (see the camber table).
- **Damage:** a Bronco dropped flat 5 m onto rock breaks parts or kinks the frame; at 8 m tires pop.
  There's no test of a 5–10 m cliff fall onto terrain. `engineDead` and `maxDamage` end a level run.

### 3.3 Capability table: the listed vehicles

§3.1 was measured on the rock buggy and the Bronco. The game now offers only the Blazer, Bronco,
4Runner, Tacoma and Jesko (`listedVehicles()`), so this section redoes the table for the four
overlanders. The Jesko is at the bottom. Measured with `prototypes/sections/capability.cpp` (over 4,000
scripted runs, physics of commit `91faf6d`, same blank rock/dirt/grass test region as `probe.cpp`, scripted
controls only; this is physics measurement, not gameplay validation). The build and run recipe is in the
file header.

**Common conditions.** 4L (4L LOCK on the 4Runner), traction control off, every fitted locker engaged
(a locker that is low-range-only goes in because we are in 4L). "Normal" pressure is what the truck is
sold with (Blazer 32, Bronco 35, 4Runner 35, Tacoma 30 psi); "aired" is the game's minimum, 12 psi.
"Stock" is the catalog truck (the 4Runner's stock is the as-built lifted truck: 35s, snorkel, winch).

#### Setups used

The garage offers every vehicle's parts on every vehicle (`modOptions`), including the unlisted
buggy's and UTV's. I only used each truck's own parts and picked by **ledge ablation** (6 to 8 variants
per truck, 3 to 9 tries each). Differences between the top variants are within noise, so the choice is
"best of a flat top", not a clear winner, except where noted.

| Truck | Stock | Set up (what I fitted) | What I tried and rejected |
|---|---|---|---|
| Blazer | 350 2-bbl, leaf springs, 0.385 m A/T tires, **no lockers**, open diffs, NP205 low 1.96 | **ZZ4 crate 350** (355 hp), **4 in lift and 35s** (0.445 m mud tires), **4.56 gears and lockers** (front and rear), 12 psi | 454 big block (same ledge result, +70 kg); lift alone or lockers alone (a little worse than the full set) |
| Bronco | 2.7 twin-turbo V6, 33s (0.415 m A/T), rear e-locker only, front locker is a garage part | **Raptor 3.0 V6 swap**, **3 in lift and 37s** (0.47 m mud tires), **4.70 and lockers** (front and rear), sway bar disconnected, 12 psi | Stage 1 tune and no engine part (equal within noise); Sasquatch 35s (ledges about 0.1 m worse but **better on side slopes**, see the camber table); the 7-speed crawler box (6.59 first, no lockers) did as well on ledges (usual 0.55 to 0.75 in 9 tries) and snapped 5x fewer shafts, but has no lockers for the twister or slabs |
| 4Runner | as built: supercharged 3.4 V6, 35s (0.445 m A/T), snorkel, rear e-locker (4L only), 4L LOCK | as built **plus 4.88 and lockers** (ARB front and rear, any range), sway pins pulled, 12 psi | 37s and long travel, the 4.7:1 crawl box, the 1GR swaps. **Nothing beat stock**: more torque or lower gearing snaps the axle shafts |
| Tacoma | 3.5 V6, 265/70R16 A/T (0.389 m), rear e-locker (4L only) | **Magnuson supercharger**, **long travel and 35s** (0.445 m mud tires), **crawl box** (4.88 gears, 4.3:1 low, front ARB locker), 12 psi | 3 in lift and 33s (worse), lockers without the crawl box (equal), tune only (equal) |

"Set up, normal pressure" below means the same parts at the pressure the truck is sold with.

#### 1. Max square ledge (rock box across the lane, rock surface)

A 16 m long rock box, `H` high, the truck crawls at it from 12 m back. Because the result is chaotic near
the limit, each height was tried at **nine crawl speeds** (0.5 to 2.0 m/s; heights up to 0.40 m: three
speeds). "Cleared" means the centre of mass ends 5 m past the face. Three numbers per cell, in metres:
**solid** = every try cleared (and every lower height too); **usual** = at least 60% of tries cleared;
**best** = the highest height any single try cleared (can be one lucky run).

| Truck | Stock, normal psi | Stock, 12 psi | Set up, normal psi | Set up, 12 psi |
|---|---|---|---|---|
| Blazer | 0.40 / 0.40 / 0.60 | 0.40 / 0.45 / 0.55 | 0.40 / 0.45 / 0.65 | **0.50 / 0.75 / 0.85** |
| Bronco | 0.40 / 0.40 / 0.50 | 0.40 / 0.45 / 0.60 | 0.40 / 0.45 / 0.75 | **0.50 / 0.65 / 0.85** |
| 4Runner | 0.35 / 0.35 / 0.65 | 0.40 / 0.45 / 0.70 | 0.35 / 0.35 / 0.55 | 0.40 / 0.45 / 0.65 |
| Tacoma | 0.30 / 0.30 / 0.45 | 0.45 / 0.50 / 0.50 | 0.35 / 0.40 / 0.60 | 0.45 / 0.60 / 0.70 |

Share of tries that cleared, by height (the shape of the cliff; 0.40 m and below were three tries):

| Truck | Setup | psi | 0.40 | 0.45 | 0.50 | 0.55 | 0.60 | 0.65 | 0.70 | 0.75 | 0.80 | 0.85 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Blazer | stock | normal | 100% | 44% | 22% | 0% | 11% | 0% | 0% | 0% | 0% | 0% |
| Blazer | stock | 12 | 100% | 78% | 56% | 44% | 0% | 0% | 0% | 0% | 0% | 0% |
| Blazer | set up | normal | 100% | 78% | 56% | 56% | 11% | 22% | 0% | 0% | 0% | 0% |
| Blazer | set up | 12 | 100% | 100% | 100% | 78% | 78% | 89% | 78% | 67% | 11% | 22% |
| Bronco | stock | normal | 100% | 44% | 33% | 0% | 0% | 0% | 0% | 0% | 0% | 0% |
| Bronco | stock | 12 | 100% | 89% | 11% | 22% | 11% | 0% | 0% | 0% | 0% | 0% |
| Bronco | set up | normal | 100% | 78% | 33% | 33% | 0% | 0% | 0% | 22% | 0% | 0% |
| Bronco | set up | 12 | 100% | 100% | 100% | 89% | 89% | 89% | 56% | 44% | 0% | 11% |
| 4Runner | stock | normal | 33% | 22% | 11% | 22% | 0% | 11% | 0% | 0% | 0% | 0% |
| 4Runner | stock | 12 | 100% | 67% | 56% | 44% | 44% | 33% | 22% | 0% | 0% | 0% |
| 4Runner | set up | normal | 33% | 22% | 0% | 11% | 0% | 0% | 0% | 0% | 0% | 0% |
| 4Runner | set up | 12 | 100% | 67% | 56% | 44% | 44% | 22% | 0% | 0% | 0% | 0% |
| Tacoma | stock | normal | 33% | 11% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% |
| Tacoma | stock | 12 | 100% | 100% | 67% | 0% | 0% | 0% | 0% | 0% | 0% | 0% |
| Tacoma | set up | normal | 67% | 22% | 11% | 22% | 11% | 0% | 0% | 0% | 0% | 0% |
| Tacoma | set up | 12 | 100% | 100% | 67% | 78% | 67% | 33% | 11% | 0% | 0% | 0% |

(The Bronco's stock-aired "usual 0.45" reproduces the 0.45 m of §3.1.) Failed tries at the face often
end with one or two snapped axle shafts (11 to 31 of the 93 runs in each set-up cell, almost none in the
stock cells except the 4Runner: the failure cost is real).

**Takeaway for templates:** at normal pressure nothing is reliable above **0.40 m** (Tacoma 0.30, 4Runner
0.35), even set up. Airing down adds +0.05 to +0.2 m ("usual") to a stock truck, and **the set-up parts pay
off only when aired**: usual +0.3 m for the Blazer, +0.2 m for the Bronco, +0.1 m for the Tacoma, 0 for the
4Runner. So a 0.45–0.50 m ledge needs air; 0.55–0.65 m needs air and a set-up Blazer, Bronco or Tacoma
(0.75 m: set-up Blazer, and only just); treat **0.85 m and up as a wall** (the single best try anywhere was
0.85 m). Use the "solid" column for "this must pass" and the "best" column plus 0.05 m for "this must fail".

#### 2. Grid wall angle (4 m high, rock surface)

Height reached in metres; "over" = over the top. Crawl at 1.2 m/s, 60 s, one run each. "Set up" is the
12 psi set-up truck (the best setup); "stock" is stock parts at normal pressure.

| Truck | | 35° | 45° | 55° | 65° | 75° |
|---|---|---|---|---|---|---|
| Blazer | set up, 12 psi | over | over | 0 | 0 | 0 |
| Blazer | stock, normal | **2.4 (stalls)** | 0 | 0 | 0 | 0 |
| Bronco | set up, 12 psi | over | over | 0.7 | 0 | 0 |
| Bronco | stock, normal | over | 0.8 | 0.2 | 0 | 0 |
| 4Runner | set up, 12 psi | over | **0.5** | 0 | 0 | 0 |
| 4Runner | stock, normal | over | 0.4 | 0 | 0 | 0 |
| Tacoma | set up, 12 psi | over | **0.5** | 0 | 0 | 0 |
| Tacoma | stock, normal | over | 0.2 | 0 | 0 | 0 |

(Stock aired: Blazer 35° over, 45° 0.05 m; Bronco 45° 3.95 m, 55° 0.3 m, close to §3.1; 4Runner
and Tacoma as set up.)

**Takeaway:** **≥ 65° seals for all four** (0 m), as in §3.1; 55° is a seal for everything except a set-up
Bronco (0.7 m). A 45° face is **not a wall for a Bronco or a set-up Blazer** (they go over 4 m of it) but
is for the 4Runner and Tacoma (they stop at about 0.5 m).

#### 3. Off-camber side slope (drive along it at 1.5 m/s for 30 s)

A slope falling 40 m to the side, driven along its middle. "slides N" = metres of lateral drift (**about
38–40 means it slid to the bottom of the slope**, unrecoverable). Stock = normal pressure, factory lockers
in. Set up = 12 psi, sway bar off, lockers in. One run each; cells that change when the speed is 1.0 or
2.0 m/s are marked with ~.

| Truck | | Dirt 25° | 30° | 35° | 40° | Rock 25° | 30° | 35° | 40° |
|---|---|---|---|---|---|---|---|---|---|
| Blazer | stock | ok | slides 37 | slides 40 | slides 38 | ok | slides 34 | slides 38 | slides 38 |
| Blazer | set up | ok | ok | ok~ | **rolls** | ok | ok | slides 7~ | slides 41 |
| Bronco | stock | ok | ok | slides 20 | **rolls** | ok | ok | slides 40 | **rolls** |
| Bronco | set up (37s) | ok | **slides 12** | **slides 18** | **rolls** | ok | **slides 19** | slides 13~ | **rolls** |
| Bronco | set up (Sasquatch 35s) | ok | ok | ok | **rolls** | ok | ok | ok | slides 39 |
| 4Runner | stock | ok | slides 16 | slides 20 | slides 38 | ok | slides 8~ | slides 38 | **rolls** |
| 4Runner | set up | ok | ok | ok | **rolls** | ok | ok | ok | slides 38 |
| Tacoma | stock | ok | ok~ | slides 18 | **rolls** | ok | ok~ | slides 40 | slides 38 |
| Tacoma | set up | ok | ok | ok | **rolls** | ok | ok | ok~ | slides 38 |

Two more facts behind the stock rows:
- **The rear locker decides the stock 30° case.** Without it (rear locker left out), stock Bronco slides
  12 m on 30° dirt (§3.1's number), stock Tacoma 20 m, stock 4Runner 17 m (rock 30°: 10 m). With it, the Bronco
  and Tacoma hold 30°. The Blazer has no locker at all.
- **Stock parts but aired, sway bar off, lockers in** hold 30° on every truck, and 35° on the Bronco and
  4Runner (both surfaces) and the Tacoma (dirt only; the Blazer slides at 35°), so most of the "set up" gain on
  slopes is pressure, the sway bar and the lockers, not the parts. The 4Runner's e-locker (engaged in these runs)
  doesn't save the stock truck at 30°.

**Takeaway:** the tier gap is **30° (stock slides or holds depending on the rear locker) vs 35° (set up
holds)**; **40° is never survivable** (rolls on dirt, slides on rock). A 32–36° shelf with a drop below is a
real commitment test for a set-up truck. **The Bronco's tall mud tires (37s) lose side grip** (they slide at
30°); a Bronco that is set up for ledges is the *worst* set-up truck on a shelf road.

#### 4. Max slab angle on rock (straight climb, grid slope 14 m long)

Highest angle climbed to the top (14 m of slope, then flat) at all of 0.7, 1.2 and 2.0 m/s (angles from 35° up
were retried at those speeds); the bracket says what happened one step higher.

| Truck | Stock, normal psi | Stock, 12 psi | Set up, normal psi | Set up, 12 psi |
|---|---|---|---|---|
| Blazer | **30** (35 only at 0.7 m/s) | 35 (40 only at 0.7) | 30 (35 only at 0.7) | 35 (40 at 0.7 and 1.2, not 2.0) |
| Bronco | 35 (40 only at 0.7) | 40 (42 at 1.2; 45 fails, 9.7 of 9.9 m at 0.7) | 35 (40 only at 0.7) | **45** (50 stops at 1.8 m) |
| 4Runner | 35 | 40 (42 stops at 5 m) | 35 | 40 (42 at 1.2; 45 stops at 0.5 m) |
| Tacoma | 35 | 35 (40 at 1.2 and 2.0; 7 of 9 m at 0.7) | 35 | 40 (42 stops at 0.9 m) |

Slower climbs go higher on the margin. **Tires popped** in 21 of 112 aired slab runs (8 of 112 at normal
pressure), mostly on the gentle 20–30° slabs. **Not the crest:** see "Where the pops and snapped shafts
come from" below. Don't read this as a cost of airing down over a lip.

**Where the pops and snapped shafts come from** (re-checked with a debug print at the one place a tire
pops, `softbody.cpp` rim strike: rim touched at more than `7 m/s * (psi+5)/(stock psi+5)`, i.e. 3.2 m/s at
12 psi). Nothing here is a game bug; nothing was changed.
- *Slab pops are the probe running off the end.* `slopeProbe` shapes only `run + 30` m of plateau and the
  truck keeps driving for 60 s, so every climber that makes the top goes off a 4.8 to 10 m drop and lands on
  the rim (n up, 4 to 16 m/s) or hits the cliff step (n.x 0.97). All 19 pops in the 116 set-up aired
  slab/slabv runs re-measured here sit at or past the plateau's end, none on the crest. With a plateau that
  keeps going, a 35° grid crest, a 35° box ramp onto a box plateau and a grid ramp onto a box plateau gave
  0 pops in 24 runs (Blazer and Bronco set-ups, 12 psi, 1.0 to 2.5 m/s). A drop of 8 m onto rock popping a
  tire is pinned on purpose (`surfaces/soft-landing`), so the cliff pops are right. The 8 normal-pressure
  pops are the same fall (7 m/s limit). A box corner is a real point contact (`World::contact`), but no pop
  came from one: the rim has to bottom, and that takes a hub falling at over 3.2 m/s.
- *Ledge and stair pops need a stall and a flat-out throttle.* The probe's driver goes to full throttle
  as soon as the truck slows at the face. The front tires bite the lip with a locked, low-range driveline,
  the nose pitches up 35 to 43° and comes back down on the rims at 3.2 to 4.8 m/s. That is 10 pops in 504
  set-up aired ledge runs, all at 1.7 and 2.0 m/s (none at 1.5 m/s or less), and 2 of 18 runs up a
  5-step box staircase (steps of 0.2, 0.3, 0.4 m at 1.0, 1.5, 2.0 m/s; both pops were the Blazer at 2.0 m/s
  on 0.3 and 0.4 m steps, none at 1.5 m/s or less). Capping the throttle at 0.6 removed them. Real drivers
  feather it.
- *The 4Runner's shafts are the same story.* A solid-axle shaft takes `shaftLimit` (10,000 N·m); the 4Runner's
  independent front CVs take 65% of it, less with steering lock (snaps seen at 5.1 to 6.5 kN·m). The load is the
  tire's traction torque, `Fx * R`: a front tire carrying 11 to 20 kN on a ledge face and gripping at
  mu 1.2 on rock is 6 kN·m, so the front CVs go the moment one tire takes the whole front axle's load flat
  out (rears at 10 kN·m with the 37s and the 4.7:1 box, where the engine can put in more than that at
  stall). Ledge runs of the aired 4Runner: stock 1 of 126 with shafts, set up 8 (4.88 and lockers), 37s with
  crawl box 32. Capping the throttle at 0.5 gives 0, 0 and 12; at 0.35, 0, 0 and 1. So the gearing is
  not the fault; the probe's flat-out driver is. "Nothing beat stock" above means *with that driver*.
  Real crawl-box trucks do break shafts when driven this way.
If these probes are repeated, end the plateau with a ramp or run until the crest plus 5 m, and cap the
throttle at about 0.5 for a crawl.

**Takeaway:** a slab "needs ~15 psi" at **38–40°** (everything stops at 40° at normal pressure and
climbs it aired, except the Blazer, which needs a set-up and slow). 30–35° is free; 42–45° is for a
set-up Bronco only; 50° stops everything.

#### 5. Wading depth before hydrolock

The intake is the highest engine node (a snorkelled truck: the roof line less 0.15 m). The pit is 14 m of
flat water with 1:3 banks, driven through at 2 m/s (also 0.5 and 4 m/s for everything but the 4Runner;
the limit moves by at most 0.05 m).

| Truck | Intake at rest, stock / set up (m above ground) | Deepest dry crossing, stock | set up | Hydrolocks at |
|---|---|---|---|---|
| Blazer | 1.32 / 1.49 | 1.35 | 1.50 | 1.40 / 1.55 |
| Bronco | 1.16 / 1.29 | 1.20–1.25 | 1.30–1.40 | 1.25–1.30 / 1.35–1.45 |
| 4Runner | **1.74** (snorkel) / 1.74 | 1.85 | 1.85 | 1.90 |
| Tacoma | 1.20 / 1.31 | 1.25 | 1.35 | 1.30 / 1.40 |

**Takeaway:** wading depth is **the intake height at rest plus 0.03–0.10 m** (the body floats a little).
Only the 4Runner's snorkel is out of the normal range: stock trucks drown at **1.25–1.4 m**, a lift and a
tall engine buy about +0.1–0.15 m, the 4Runner doesn't drown until **1.9 m**. A crossing of 1.55–1.8 m is a
"needs the 4Runner" test (or a tow, or scouting on foot); up to about 1.15 m is safe for every truck.

#### 6. The Jesko (cheap checks only; stock, rear-drive, no low range, normal pressure)

| Test | Result |
|---|---|
| Ledge | clears 0.10 m on every try; **0.15 m stops it** |
| Wall (4 m, rock) | **0 m at 35°** already; can't climb a 35° grid slope at all |
| Slab | **0 m even at 20°** on rock |
| Side slope | dirt: ok at 25°, slides off at 30°; rock: ok at 30°, slides at 35° |
| Wading | intake 0.65 m; hydrolocks at **0.8 m** (0.5–0.7 m doesn't drown it, but it stalls in the pit and can't drive out) |

**Takeaway:** the Jesko can't do anything the sections are for. Give it a bypass road, or let it be a
spectator (0.10 m ledge, 25° side slope, 0.7 m water; it climbed none of the rock slopes tried, 20° and up).

#### Surprises and caveats

- **The 4Runner is no better set up.** Its stock truck (35s, lift, snorkel) is already at the plateau, and
  the crawl box, 37s and the bigger engine make ledges *worse* (more torque and lower gearing snaps the shafts:
  `shaftLimit` 10,000 N·m). At normal pressure it solidly climbs only 0.35 m, no better than the 1972 Blazer, and
  it stops at a 45° wall and slab.
- **A set-up 1972 Blazer is the best ledge truck** (usual 0.75 m aired) and goes over a 45° wall, level with
  the Bronco, while the stock Blazer (no lockers, open diffs) is the weakest truck at normal pressure: it stalls
  2.4 m up a 35° grid wall and can't climb a 35° slab (only at 0.7 m/s).
- **The Bronco's ledge setup (37s, lift) is the worst for side slopes**, and the Sasquatch 35s is the
  best. There is no one "best setup" per truck; the doc reports the ledge/slab setup and flags the exception.
- **Noise.** Ledge numbers are **very noisy near the limit** (a 0.50 m ledge can clear at 2.0 m/s and
  fail at 0.7, and clear again at 0.55); trust the share-of-tries table, not one run. The "best" column is one
  lucky try. Wall and camber are one run each: borderline cells (marked ~) flip with the crawl speed; slab
  35°/40° cells flip for the Blazer and Bronco. Wading is repeatable to 0.05 m. Walls, 65° and up, are not
  noisy (0 m everywhere).
- **Failure costs show up.** Snapped axle shafts (ledge, wall, slab 25–35°), popped tires aired down
  (ledge hits at 1.7 m/s and up, and falls off the slab plateau's end, not the crests), and tires and shafts
  lost on the 40° side slope. The probes record them as `SHAFTS-BROKEN` and
  `TIRES-POPPED` on every `CAP` line.

---

## 4. The prototype (`prototypes/sections/`)

**Throwaway code, outside the Makefile.** It was built against the game's existing object files
so that no game source had to change.

| File | What it is |
|---|---|
| `probe.cpp` | The physics probes in §3.1 (registered as `probe/...` scenarios) |
| `showcase.cpp` | Seven section generators, a level exporter, and a scripted crawl used only to debug the rock wall |
| `levelcheck.cpp` | Loads an exported `.czl` like the game does and checks a car can drive off the start (used to find the sand bug) |

### Building and running

```bash
make -j build/run_tests crumplezone        # the game's objects must exist
PHYS=$(sed -n 's/^PHYS *:= //p' Makefile); OBJS=$(for f in $PHYS; do echo build/${f%.cpp}.o; done)
g++ -O2 -march=native -std=c++17 -I tests -I src -c prototypes/sections/probe.cpp -o build/proto_probe.o
g++ -O2 -march=native -std=c++17 -I tests -I src -c prototypes/sections/showcase.cpp -o build/proto_show.o
g++ -O2 -o build/proto build/tests/run_tests.o build/tests/harness.o $OBJS build/proto_probe.o build/proto_show.o -lpthread -lm
./build/proto probe/                         # physics probes
./build/proto --jobs 1 export/wall-1         # write ~/.config/crumplezone/levels/gen-wall-1.czl
```

- **Exports must be run one at a time** (`--jobs 1`, one name per run). The recorder (`gLv`) is a
  global and the runner runs scenarios on parallel threads, so parallel exports overwrite each
  other.
- **Rendering scenarios:** link `showcase.o` with all of `GAME_SRC`'s objects into a dev copy of the
  game. Then run it with `SDL_VIDEODRIVER=offscreen ... --scenario show/wall-1 --photo --orbit
  yaw,pitch,dist --shot-prefix out --shot-frames 300,1800`. It writes PPM files; convert them with
  ImageMagick.

### How a section is built

- `blank()` clears a 360 m square of the proving ground (x, z in [−560, −200]): every static object
  removed, rolling noise land, grass. The section runs along +X from x = −450 to −310, centred on
  z = −380.
- Helpers:
  - `trail(half, blend, y(x), surface(x))` carves a trail to a height profile.
  - `walls(...)` raises sealing walls at a given angle.
  - `Canvas::forest(clear, density, wall)` adds trees; with `wall` on, the edge trunks are too close
    to drive between.
- **Recording:** every placement goes through `addRockR`, `addBoxR`, `markRock`, `addTreeR` and
  `addLogR`, which add the object to the world **and** record it as a `LevelObject`.
  `exportLevel()` then writes every changed cell, surface and water level in the region, plus the
  objects, the base objects to remove, and the start.

### Status of each section (seeds 1–3, exported as levels `GEN <TYPE>-<seed>`)

| Section | What it does | State |
|---|---|---|
| `steepHill` | 24–30° scree climb in a gully with 68° walls, 3 rock ledges (0.32–0.5 m) near the top, loose rocks | first draft, not reviewed |
| `mudPit` | 42–56 m pit, floor 0.3–1.5 m (fbm), gentle entry, **1:1 exit bank**, standing water, ruts, a tree wall both sides | first draft |
| `rockyRiver` | 20–28 m channel crossing the trail, 0.55–0.85 m water, current 1.4 m/s, up to 140 boulders (0.25–1.4 m) thickest mid-channel | first draft |
| `rockWall` (v3) | slickrock formation in a slot canyon (details below) | **closest to the target; owner will hand-edit** |
| `dunes` | dune field with gentle windward faces and steep slip faces, 4–6.5 m | bare, no objects |
| `cliffShelf` | 3.4–4 m shelf cut into a ~69° cliff, 5–10° tilt toward a ~70° drop, dips, fallen rocks narrowing it | bare and gravel-only; needs the shelf-road references |
| `timber` | 3.8 m forest track, tree walls, 4 trees standing in the track, 3 windfall logs | the cut-and-winch mechanic doesn't exist yet |

### The rock wall, and lessons from three versions

- **v1**: 0.3–0.5 m box steps 3–4.5 m apart on a ramp. They read as speed bumps. Rejected.
- **v2**: a terraced grid plus scattered hull rocks on the lips. The 1 m grid smoothed every riser
  into a soft orange slope, and the scattered rocks didn't form ledges. The truck got stuck at
  ledge 1.
- **v3** (current):
  - six ledges whose lip lines wander (sine plus noise);
  - each ledge a row of **thick rock slab boxes** (turned and tipped a little, overlapping) with
    tops at the upper tread, so the faces are 0.75–1.3 m;
  - a **notch** per ledge built as a **staircase of slabs, no step over ~0.40 m**, alternating
    sides;
  - a 2.7 m squeeze with a V-notch floor after ledge 3;
  - a pour-over pool under ledge 5;
  - a final ledge of 1.3–1.6 m with no notch (needs a winch to the junipers above);
  - **banded strata slabs jutting from the canyon walls**, which matched the references best;
  - scanned boulders heaped along the wall bases;
  - red-rock paint.
- The owner's verdict on v3: "looks better", but they'll change things themselves.
- **Lessons:**
  - Use rock slab boxes for any lip or step.
  - Use hull rocks for boulders.
  - Never rely on grid faces for steps.
  - Keep notch steps ≤ 0.4 m. The Bronco's measured limit is 0.45 m on square ledges, and
    jittered slab tops push the real step higher.
  - **Don't start a level on sand.** v3 started in a sand wash, the car bogged at full throttle
    (a Bronco did 9.5 m in 8 s), and the owner reported "the car doesn't drive". The start is now
    dirt.

### Scanned rocks

`assets/props` was never baked in this checkout, so the game draws code-made rock blobs. The sources
are already in `assets/source/polyhaven/models` (27 hulls, including `rock_face_01/02` and
`namaqualand_cliff_01`).

- **`make bake` is broken:** `build/bakeprops` fails to link with an undefined `exeDir()`. Add
  `src/platform.cpp` to its rule in the `Makefile`.
- Baked by hand (about 13 s, 25 MB) into a temporary folder and used through
  `CZ_PROPS_DIR=<dir> ./crumplezone`. That folder won't survive the session, so bake again.
- The owner was asked whether to fix the rule and commit the baked `assets/props`. They haven't
  answered.

---

## 5. Useful facts about the codebase

- **Maps:** `World(int map)` dispatches to `buildBackcountry` / `buildFrontier` / `buildMountain`
  (members of `World`, about 1,000+ lines each, with their own local lambdas: `scanNear`,
  `groundProfile`, `layTrail`, `levelDisc`, `corridor`, `addSteps`). Noise helpers
  (`valueNoise`, `fbmNoise`, `ridgedNoise`, `latticeHash`) are shared in `src/mathx.h`. They're
  unseeded, so offset the coordinates by the seed.
- **Levels** (`src/level.h`): a `.czl` names a base map and stores per-cell heights, surfaces
  (`surface << 8 | paint`), water, objects (rock, box with `sub = 1` for a rock slab, log, tree,
  station), removed base objects, a start, gates, and rules. `applyLevel` applies the edits in that
  order. In multiplayer the host sends the level's whole text.
- **`--level NAME`** finds a level by **case-sensitive substring of its uppercase name**. The
  exporter wrote lowercase names at first, so `--level "GEN WALL-1"` didn't match. Fixed: names are
  uppercased.
- **`--menu 4`** opens the Levels page.
- **World editor:** Levels → pick → OPEN IN THE EDITOR. It has sculpt, paint, water, objects
  (rocks, trees, logs, boxes and rock slabs, stations), course (start and gates), settings, save,
  and play. `src/editor.h`, `src/editcore.h`.
- **Test runner:** `build/run_tests <substring>` runs `SCENARIO`s in parallel; about 8× real time
  per core (the offroad group takes 15 s). Extra scenario files can be linked in without touching
  the `Makefile` (see §4).
- **Existing failures:**
  - `offroad/whoops` fails on a clean checkout (the side-by-side flips over).
  - `docs/handoff.md` lists 13 known physics failures.

---

## 6. Open questions for the owner

1. Approve the section library and stitcher (§2.3), or change it?
2. Fix `make bake` and commit the baked scanned rocks (about 25 MB)?
3. Keep three seed variants per section?
4. Ground stakes: restrict them (no stakes on rock, scree, ice or deep mud) so anchor puzzles work?
5. Movable logs and tree cutting: the owner said "will be done later". When?

## 7. Suggested next steps

1. Get answers to §6, then rewrite the spec from §2 and write an implementation plan.
2. Move the generators from `prototypes/sections/showcase.cpp` into `src/` as a section library,
   behind one interface (`buildSection(type, params, seed) → SectionData`). Turn the hard-coded
   region constants into a local frame relative to the entry.
3. Build the `.czl` ↔ section conversion and the stitcher. Get one stage from three sections
   playable.
4. New section types from the second photo batch: shelf-road variants (granite, red-rock ledge,
   grassy mountainside cut with snow), scree track, snow track.
5. Gameplay features the templates need: restrict stakes, dynamic log props attachable to winch and
   strap, tree cutting.
6. Camps, fuel and respawn cost, per the agreed fail state.

## 8. Status update: the section library is in the game (2026-10-16)

Step 2 of §7 is done, and the sections are on the Proving Ground, where the owner views and tests
every asset.

- **Library:** `src/sections.h/.cpp` (in the Makefile's `PHYS` list). `buildSection(World&, type, seed,
  SectionFrame{entry, heading})` stamps one of the seven types (`SEC_STEEP_HILL`, `SEC_MUD_PIT`,
  `SEC_ROCKY_RIVER`, `SEC_ROCK_WALL`, `SEC_DUNES`, `SEC_CLIFF_SHELF`, `SEC_TIMBER`) into a World and
  returns where its entry and exit ended up; also `sectionName`, `sectionLength`,
  `sectionFootprint`. Every parameter range is a named constant at the top of `sections.cpp`.
  The generators are the prototype's, ported to a local frame (u along the heading, v to the left,
  heights from the entry plane). The rock wall is the v3 construction as it was (slab ledges, notch
  staircases of slabs, strata in the walls, the pour-over pool). The level recorder, `crawlLine` and
  `gSlickLine` are gone. The call needs `w.buildGrid()` afterwards.
- **What changed from the prototype:** a section clears only its own ground (its length plus a 20 m
  flat run-in and run-out, and a half-width of 28-34 m), removes what stands there, and blends 24 m
  into the surroundings. The ground tilts a little along the section (at most 0.04) to meet the
  land. The hill's gully walls open out onto a summit instead of ending in a step. The dunes are flat
  at both ends (they used to start on sand). The cliff shelf eases onto its line from the entry, and
  its cliff levels off at 24 m. The river runs out into marsh at the sides of the cleared ground. Any
  heading works (terrain is sampled per world cell, boxes turn with it); the tests and the park use
  0, 90, 180 and 270 degrees.
- **On the map:** `World::buildSectionPark()` (world.cpp) lays seed 1 of each out beyond the loop road
  in the north-west, gated by `World::withShowcase` (renamed from `withModelYard`; the game turns it
  on, the tests keep it off). West strip, running north from (-465, 280): STEEP HILL, MUD PIT, ROCKY
  RIVER. North strip, running east from (-480, -455): ROCK WALL, DUNES, CLIFF SHELF, TIMBER. Each has
  its name over its entry (`World::tags`) and is a place (`NAME (SECTION)`) in the autopilot's
  DRIVE TO list, the compass and the map's discoveries. `crumplezone --spawn-section N` (0-based, in
  that order) starts you at an entry, facing along it.
- **Tests:** `tests/sections.cpp`. **Prototype:** `prototypes/sections/showcase.cpp` is unchanged and
  still compiles; it keeps its own copy of the generators (its level export needs the recorder the
  library dropped), so it is now out of date. Use it only for `export/...` levels until the
  stitcher reads sections straight from the library.

