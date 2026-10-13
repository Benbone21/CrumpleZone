# Work list for the cloud session (branch `sync`)

`sync` has everything merged:
- **`coop`:** level editor, multiplayer co-op, fuel, jobs, drone, rock buggy, UTV, Backcountry map.
- **Physics:** the cloud's `physics/brake-traction-control` branch.
- **Unfinished work:** scanned props and ground textures.

Work on `sync`. Rules: read `agent.md`, never run `--update-baseline` until the suite passes, and use only CC0 assets.

**Vehicle index change in the merge:** the Tacoma is now catalog **6, key 7**. The rock buggy is 4 (key 5) and the side-by-side/UTV is 5 (key 6).

The suite right after the merge: 415 scenarios, 74 failing.

## 1. Finish the physics work in progress (failing on the cloud branch before the merge)
- `components/airborne-tires/*` (every vehicle): "wheel N resets ABS after leaving ground"
- `mods/civic/*` (all 9): the Civic doesn't stay upright, 19% damage, 0-80 ratio 9.0
- `dynamics/braking/toyota 4runner`
- `structure/rock-hits/4runner`
- `structure/suspension-travel/ford bronco`

## 2. Put the rock buggy (4) and side-by-side (5) on the new physics
They were tuned on the old physics and fail about 55 rows: settle, acceleration, braking, top speed, turning circle, torsion/bending, suspension travel, jiggle, clearance, tilt table, weight, damage walls, door stops, tire-into-fender, broken half shaft, steering snap, buggy mods, `ai/lap`, `offroad/ledges-buggy`, `autopilot/buggy-ledge`, `structure/rock-hits/buggy`.

**Fix the physics or specs, not by widening ranges.**

The buggy should:
- climb about 0.7 m ledges;
- out-crawl the Bronco;
- reach a ramp travel index of about 900–1,200.

The UTV should be the fastest through whoops.

**Check the hand-merged code in `src/carbuild.cpp` (SOLID_AXLE).** The cloud's design was kept:
- level lower links (R−0.12)
- jounce stops on the tube
- the welded bracket cluster
- upper link at R+0.05

The buggy's `dir` (links run toward the middle), `linkLength`, `upperLink` and `coilTop` were re-applied on top. Make sure a **steered front solid axle** still works: kingpins, tie rods, the steering arm and the bump stops.

Things the tube-frame vehicles may need to opt in or out of:
- tires colliding with their own body
- door check straps (they have no doors)
- ESC
- the torque converter (the buggy has a 4L80E automatic, the UTV a CVT)

**Estimates to check:** some test tables got buggy/UTV rows that are estimates (turning circle, clearance, tilt-table SSF in `tests/scenarios.cpp`). Correct them with real figures.

## 3. Other failures from the merge
- `freeway/top-speed/toyota tacoma`
- `offroad/slickrock-crawlers`, `offroad/slickrock-dome`, `offroad/twister-crawlers`, `offroad/whoops`, `offroad/ledge-crawlers`, `offroad/rti`
- `net/crawlers`
- `backcountry/autopilot-far` (use `BC_TRACE=1`)

For each, find the root cause: the index shift, a merge mistake, or the new physics.

## 4. Photoscanned world props (unfinished: `src/props.*`, `tools/bakeprops.cpp`)
**Getting the source assets.** They aren't in git. Run `python3 tools/fetch_polyhaven.py assets/source/polyhaven` to get the CC0 Poly Haven set (about 154 MB, md5-checked):
- 14 rocks and cliffs
- the trees: fir_sapling, pine_sapling_small, dead_tree_trunk, dead_tree_trunk_02
- the shrubs, fern and grass
- the stumps and dry branches
- 10 ground textures

The libraries are already in `src/third_party` (`cgltf.h`, `stb_image.h`).

What it needs:
- **Baker (`make bake`):** writes into `assets/props/` (committed, under about 60 MB in total).
  - Simplifies the meshes to 3 levels of detail. Rocks get about 1,500, 400 and 100 triangles.
  - Thins plant and tree cards (drop some cards, scale up the rest).
  - Makes far-view billboards for the trees.
  - Builds a convex hull for each rock (48 vertices or fewer) in the `Rock` format (v, f, fn, fd).
- **Physics:** `World::addRock` on every map, in the level editor and in level objects uses the scanned hull, scaled and turned at random, so what you hit is what you see.
  - The code-made hull stays as the fallback.
  - `run_tests` doesn't load the hull library by default.
  - New `tests/props.cpp`:
    - hull checks;
    - a Bronco crawls over a scanned boulder;
    - a car resting on a rock sits within 5 cm of the visible mesh.
- **Renderer:** instanced textured meshes with diffuse, normal and ARM maps.
  - Cutout plant cards, lit from both sides.
  - The scene's own sun, sky, fog, shadow map and time of day; props cast shadows.
  - Distance LODs, frustum culling, and billboards beyond 60–100 m.
- **What gets drawn with scans:**
  - Pine trees → fir/pine saplings scaled to `Tree::height`.
  - Dead snags → dead_tree_trunk.
  - Logs → dead_tree_trunk_02 fitted to the capsule.
  - Bushes → shrubs.
  - Visual-only scatter near the camera (stumps, branches, ferns, grass), kept off trails, water and pads, and tied to the quality setting.
  - Cliff meshes on steep faces.
- **Performance:** Backcountry (`--map 4`, about 3,400 trees) should stay smooth: about 4 ms or less for props at quality 2.
- **Credits** in README (Poly Haven CC0, stb_image public domain, cgltf MIT).

## 5. Photoscanned ground textures (unfinished: `src/groundtex.*`, `src/groundshader.cpp`, `assets/ground/`)
- Blend materials by surface type, with smooth, noise- and height-based transitions between cells.
- Normal and ARM lighting.
- Anti-tiling: a second sample at a rotated scale and large-scale variation.
- Triplanar mapping on steep slopes; fade detail with distance.
- Must keep working:
  - tire tracks, ruts and sinking;
  - puddles;
  - surfaces painted in the editor (`updateTerrainRegion` must stay fast);
  - map colours;
  - night.
- The quality setting scales it.
- There's no grass, sand or asphalt scan yet. Tint, grade or keep the code-made look, and list which CC0 textures would help.

## 6. Still open from before
- Solid axles flipping over on big drops: check it's fixed for every truck (the Bronco and 4Runner flipped at 1.5–2 m).
- Level editor limits:
  - can't set a level's start time of day;
  - repair shop signs can't be moved;
  - `World::removeNear` leaves a station's posts behind.

## How to test without getting in the user's way
They work on the same machine when testing locally:
- **Screenshots:** `SDL_VIDEODRIVER=offscreen nice -n 19 ./crumplezone ... --shot-prefix ...`, with no window.
- **Tests:** `nice -n 19 ./build/run_tests --jobs 6`.
