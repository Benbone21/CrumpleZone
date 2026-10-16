# Expedition generator — milestone 1 (design + plan)

## Context

MemeNG ("Crumple Zone") is a soft-body off-road game with up to 8-player co-op (host or new dedicated server). The goal is a co-op game: buddies travel through obstacles together, using winches, tow straps, tire pressure and so on to get through.

**Brainstorm decisions (agreed with the user):**
- **Core loop: Expedition runs.** A chain of 4–6 seeded, procedurally generated stages. Each stage runs camp to camp with authored obstacle set pieces and escalating difficulty. Existing levels (`.czl`) stay as a separate campaign/practice mode.
- **Fail state:** a wrecked car respawns at the last camp with a cost (time, fuel or spare parts). Camps are checkpoints, repair/refuel points and a supply loop.
- **Milestone 1 = generator only:** seeded terrain + route + camps. No co-op obstacles, roles or supply systems yet.

**Deferred ideas (not in this milestone):**
- Buddy-required obstacles: winch-anchor mud pit, winch climb, log-bridge washout, spotter on foot, counterweight, rollover recovery.
- Scout/hauler loadouts and shared fuel/parts.
- Respawn cost, teamwork scoring, contracts, weather and time-of-day modifiers, unlocks.

**Why a seeded whole-grid generator (approach A):**
- `World` is a 1 m height grid, 1201×1201 (`kHalfSize = 600`, `src/world.h:153`), baked per map by builder functions (`buildBackcountry`, `buildFrontier`, `buildMountain`). It is not chunked.
- A stage-sized generated grid keeps physics, collision, LOD/culling and the editor working unchanged. Streaming chunks (approach B) would force a rewrite of all of them.
- Multiplayer sync becomes the seed (and stage number) instead of a map.

## Design

**Entry point.** Add `MAP_EXPEDITION` to `enum MapId` (`src/world.h:113`, before `MAP_COUNT`) and `mapName()` (`src/world.cpp:61`). Extend the constructor to `World(int map = MAP_PROVING_GROUND, uint32_t seed = 0, int stage = 0)`. The constructor dispatch (`src/world.cpp:309`) gets `else if (map == MAP_EXPEDITION) buildExpedition(seed, stage);`.

**New files.**
- `src/expedition.h`: a pure, World-independent description of a stage:
  ```
  struct StageLayout { uint32_t seed; int stage; int biome; double difficulty;
                       std::vector<V3> route;          // ~2 m spaced, start camp -> end camp
                       std::vector<Camp> camps;        // position, yaw, name
                       std::vector<Slot> slots; };     // along-route positions reserved for future set pieces
  StageLayout generateLayout(uint32_t seed, int stage);   // deterministic, no globals
  ```
- `src/expedition.cpp`: `generateLayout` plus `World::buildExpedition`, which rasterizes the layout onto the grid. Splitting layout from rasterization makes the layout unit-testable without a 1.4M-cell bake.

**Pipeline (all deterministic from `seed`, `stage`):**
1. **RNG.** Use a small self-contained PRNG (e.g. splitmix/xorshift) and integer hashing. Do not use `std::*_distribution`, whose output differs between standard libraries (Linux, Windows and macOS builds all exist). Reuse the existing value/fbm/ridged noise helpers from `src/mountain.cpp`/`backcountry.cpp`; confirm their names and whether they are file-local (they may need moving to a shared header).
2. **Route.** Start camp in the south, end camp in the north. Control points are displaced by noise to meander, within the map bounds. Sample with the existing `splinePoints()` (`src/world.h`). Target length is roughly 700–1000 m, scaling with stage.
3. **Biome.** Choose from the seed among forest, desert, snow and swamp. The biome picks the surface palette, which already exists (`SURF_*` with sink and grip), plus tree kinds and noise amplitude.
4. **Base land.** Noise terrain with amplitude scaled by `difficulty` (rises with stage).
5. **Carve the trail.** Flatten a corridor along the route with a graded profile. Smooth the profile along the route as `bakeTerrain` does with `profile()` (`src/world.cpp:443`). Cap the grade by difficulty and blend the shoulders into the terrain. Follow how `backcountry.cpp` carves its trails.
6. **Dressing (single-car only for now).** Use existing builders only: mud stretches (surface paint), a river or ford (`waterY` and flow vectors, or `addLake`), rock gardens (`addRock`), logs (`addLog`), trees (`addTree`) and bushes (`scatterScenery`). Density follows `difficulty`. This validates drivability. Real co-op set pieces fill the reserved `slots` in a later milestone.
7. **Camps.** Flatten a pad and call `addStation()` at the start, the end and one mid-route camp. Set `spawn`, `spawnYaw` and `parking`.
8. **Self-check and deterministic retry.** The generator validates itself (route continuity, max grade, camps flat and clear of obstacles). On failure it retries with `seed + k` for a bounded `k`, so every machine takes the same path.

**Integration (minimal).**
- `src/main.cpp:1489` builds `World(settings.map)`; pass the seed and stage there. Add a `--seed N` / `--stage K` option next to the existing `--map` handling (`src/main.cpp:1427`) and a map-menu entry (`mapName`/`MAP_COUNT` already drive the menu).
- Add an `MAP_EXPEDITION` branch where `main.cpp` switches on `world.map` (around lines 447–487, 832 and 1283: spawn list, markers, station list), so the HUD, compass and map work.

**Optional last step (cut if it grows): network seed.** `src/net.h` carries only `map` (an int) and the level text. Add `seed` and `stage` to the session handshake and bump the protocol version (currently 3). Host and clients then build the same stage. If it is cut, record it as the first task of milestone 2.

## Risks

- **Cross-platform determinism.** Terrain is baked per client, so small float differences in heights are harmless (each client simulates its own car). Discrete choices (object counts and positions) must not hinge on a float threshold. Add a checksum of heights plus object counts, and in a later step send it over the network to detect desync.
- **Existing physics test failures.** `docs/handoff.md` records 13 known failures. Run `make test` first and compare with `tests/baseline.txt`, so new failures are attributable.
- **Noise helpers may be file-local** (see Pipeline step 1).
- **Bake time.** The grid is about 1.4M cells. Check against `docs/performance.md` and `tests/perf.cpp`.

## Implementation steps

0. Save this plan as the design spec at `docs/superpowers/specs/2026-10-16-expedition-generator-design.md` and commit it.
1. Baseline: `make test`, saving the result for comparison.
2. `src/expedition.h/.cpp`: `generateLayout` (PRNG, route, camps, slots, self-check). TDD with `tests/expedition.cpp` using the `SCENARIO` pattern in `tests/levels.cpp`: same seed gives an identical layout, different seeds differ, route is continuous, grade ≤ cap, camps are flat. Check how `Makefile` lists test sources, and whether new files need adding.
3. `World::buildExpedition`: rasterize terrain, trail, biome, dressing and camps. More tests: heights and object counts hash equal across two builds, every route point sits on solid ground and clear of obstacles.
4. Wire `MAP_EXPEDITION` into `World`, `mapName`, `main.cpp` (seed/stage options, menu entry and map-specific branches).
5. Drivability smoke test: use the autopilot test harness (`tests/autopilot.cpp`, `src/autopilot.cpp`) to drive a truck from start camp to end camp on several seeds and stages. Report which fail and why, and tune the generator's grade and dressing density.
6. (Optional) Network seed and stage plus protocol bump, with `tests/net_state.cpp` coverage.
7. Short `docs/expedition.md` (seed/stage usage, generator stages, extension points).

## Verification

- `make test T=expedition`, then the full `make test` against the baseline. Expect no new failures beyond the known 13.
- Run `./crumplezone --map <MAP_EXPEDITION> --seed 1 --stage 0` (and other seeds and stages). Drive the whole route by hand and check the HUD, map, compass and camps.
- Look at a few seeds in different biomes and stages in the Browser pane or on screen, for variety and difficulty progression.
- (If step 6 is done) host and client on the same seed see the same terrain, and the checksum matches.
