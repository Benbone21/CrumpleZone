# Obstacle sections and the expedition generator: handoff

Written 2026-10-16 by a Claude Code session working with the owner. It covers the goals agreed in
that session, the design, what was measured in the physics, the prototype that exists, what went
wrong, and what is still open. Start here before touching terrain generation.

**Where things stand:** an approved design, a physics feasibility spike with numbers, and a
**throwaway prototype** of seven obstacle-section generators (in `prototypes/sections/`, outside the
build) that export playable levels. **No game code has changed.** The owner rejected the first
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
