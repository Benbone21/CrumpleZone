# Agent guide: Crumple Zone

A soft-body driving sandbox for Linux in C++17 and OpenGL 3.3, in the spirit of BeamNG.
Read `README.md` first. It covers controls, vehicles, damage and how a vehicle is built.
This file covers how to work on the code. **What's in flight right now (known failures, paused
work, the wheel-swap feature, releases): `docs/handoff.md`.**

## Build, run, test

```bash
make                                   # builds ./crumplezone (needs g++ and the libsdl2-2.0-0 runtime; no -dev packages)
./crumplezone --player 3               # start next to car 3 of those parked at the start (0 Civic, 1 Jesko, 2 Bronco, 3 4Runner)
./crumplezone --resume-model 4         # be vehicle 4, 5 or 6 (the rock buggy, the side-by-side, the Tacoma); keys 1-7 do it in the game
make build/run_tests
nice -n 15 ./build/run_tests --jobs 6  # every scenario in parallel, about 6 min; writes build/test_report.html
./build/run_tests -v damage/           # one group, verbose (filter = substring of the scenario name)
./build/run_tests --update-baseline    # after an intended behavior change: re-record tests/baseline.txt
```

- Every change must keep all scenarios passing (currently 284; `engine/performance` fails on a
  slow or shared CPU, and `offroad/shaft-break` is open: see Open work). A failing range means the
  physics drifted from real-world numbers. Fix the physics, don't just widen the range,
  unless the range itself was wrong. If you widen one, say why in a comment.
- Don't hand-edit `tests/baseline.txt`. Drift warnings (>10% from it) are informational.
- The user plays other games on this machine. Run the test suite with `nice`. Launch the
  game only when you need to, and close it when you're done.

### Screenshots without a person at the keyboard

The user works on this machine while agents run: never open a visible window, and keep the load low.
Prefix every game launch with `SDL_VIDEODRIVER=offscreen nice -n 19` (renders on the GPU, no window;
`--shot-prefix` works the same) and run tests and builds with `nice -n 19` and at most 6 jobs.

```bash
SDL_VIDEODRIVER=offscreen nice -n 19 ./crumplezone --no-audio --size 900x560 --player 3 --orbit YAW,PITCH,DIST \
  --shot-prefix /tmp/shot --shot-frames 90   # writes /tmp/shot_0090.ppm, then exits
convert /tmp/shot_0090.ppm shot.png
```

Yaw 0 is a side view. Yaw -90 is from the front, and 90 is from the back.
Other flags: `--garage`, `--mods E,S,T` (fit mod options), `--scenario NAME` (play a test visually),
`--time H` (hold the clock, e.g. `--time 23` for night), `--lamps 0-3` (headlights: auto, low, high, off).
Without `--time`, captures start at 10:00 with the clock running.

## Source layout

| File | What's in it |
| --- | --- |
| `src/vehicles.h/.cpp` | `VehicleSpec` and the catalog of the seven vehicles (indices 0-6: Civic, Jesko, Bronco, 4Runner, rock buggy, side-by-side, Tacoma; the code addresses them by index, so only ever append). Each spec is a text side-profile lattice, dimensions, mass items, engine, gearbox, suspension, tires, aero and looks |
| `src/mods.h/.cpp` | Garage mods (engine, suspension, transmission). Each option is a function that edits a copy of the spec. `tunedSpec()` caches the result. The garage is saved to `~/.config/crumplezone/garage.txt` |
| `src/carbuild.cpp` | Turns a spec into nodes and beams: body lattice, ladder frame (`ladderFrame`: rails, crossmembers, brackets, body mounts), engine on mounts, suspension linkages (wishbones, struts, 4-link and leaf-sprung live axles), bolt-on panels with pins, and the stability pass. Beam material table `kSpec` and the frame constants (`kRailStiffness`, `kBracketYield`) are at the top |
| `src/softbody.h/.cpp` | The 2 kHz simulation: beams (yield, work hardening, breaking, mount snubbers, leaf friction), pins, shape-matching groups (`chassis`, the plastic `railFrame[2]`), tires (and tire vs own body, `wheelBodyContact`), brakes, ABS/TC/ESC, diffs and half shafts, torque converter, gearbox and auto-shift, aero, damage stats, glass |
| `src/carvisual.cpp` | Builds the visible car: a smooth skin from the lattice, plus parts (engine, suspension incl. leaf packs and shackles, frame rails, interior) and `buildExtras()` (wing, flares, armor bumpers, snorkel, roof basket...) |
| `src/bodyshell.cpp` | Sculpted body shells (`ShellLines` + a per-car `Style`), bound to the physics cells so they dent and their panels open. Pickups (`ShellLines::bed`): behind `xCab` each section is the U of the bed |
| `src/render.cpp` | GL renderer. The body shader draws lamps, grilles, door seams and two-tone trim from lattice coordinates. Wheel meshes come in a style per `WheelStyle`. The pipeline is HDR: the scene renders into an RGBA16F target (MSAA), then `postProcess()` runs SAO, the bloom chain and the ACES composite. There are two shadow cascades. Sky, clouds and fog live in `kCommonFS`; everything outputs linear HDR. Scenery-only terrain (crags and the backdrop range) comes from `sceneryHeight()` |
| `src/main.cpp` | Window, input, camera, HUD, garage UI, command-line flags |
| `src/music.h/.cpp` | Generative background music, one style per map (`Style`, indexed by `World::map`; later maps reuse them in turn, and the Backcountry (map 4) takes MOUNTAIN's). Seeded progressions, motif-based melodies, sections of 30–90 s; band-limited synths, Karplus-Strong plucks, an FDN reverb. `Audio` mixes it in the callback; volume is `Settings::music` x master. Offline review: `build/review/music/musictest.cpp` (WAVs, spectrograms, `layers`, `trace`, `soak`) |
| `src/daylight.h/.cpp` | Time of day: `computeDaylight(hours)` gives the sun, moon and key-light directions, light and sky colors, exposure and the star sphere's turn; `kKeys` holds the sky keyframes by sun height. The game runs the clock (`dayHours` in main.cpp; setting TIME OF DAY: the cycle at a day length, or a fixed time; `--time H` holds it) and hands `Renderer::daylight` the result; in multiplayer the host's clock goes out as `MSG_CLOCK` |
| `src/lights.h/.cpp` | Vehicle lamps: where each model's head and tail lamps sit (from the shell lines the body shader uses), the body node each rides on, and whether it survived (`carLamps`). The renderer turns the nearest 8 cars' headlamps into spot lights (`lampLight()` in `kCommonFS`, evaluated in every lit shader) and feeds the body shader `uLampOn/uLampOk/uReverse`. Shift+L is the player's switch (`LampMode`) |
| `src/menu.h/.cpp` | Esc pause menu and `Settings` (units, volume, FOV, graphics, display), saved to `~/.config/crumplezone/settings.txt`. Offline tests use `--menu 0/1` (opens the menu in screenshots), `--quality 0-3` and `--mph` |
| `src/hudkit.h` | HUD drawing helpers shared by the HUD and the menus |
| `src/world.cpp` | Maps (`World(MAP_...)`): the proving ground (pad, crash wall at x = 114, poles, ramps, loop road, trees, the surface course at x -118..-48, z 72..126), shared collision (terrain, boxes, poles, boulders = spheres, logs = capsules), the surface table `surfaceInfo()` (grip, bite, keep, rolling, sink, dust color) |
| `src/freeway.cpp` | The freeway map (`World::buildFreeway`): everything repeats every `World::wrapPeriod` (400 m) along x; `Sim::wrapAround` (run by `advance`) moves all cars, ropes and autopilot routes back a period when the player is half a period out (`Car::translate`, `Sim::wrapShift`; the game shifts the camera and `Effects::translate`). Anything added to it must repeat with the period. The renderer carries the road on past the map via `sceneryHeight` and the clamped paint texture |
| `src/frontier.cpp` | The Frontier map (`World::buildFrontier`, `MAP_FRONTIER = 3`, 1 m grid): race circuit, rally stage, desert (mesa staircase, bowl, wash), THE WALL (its site is searched for at build time: a ~30% hillside just off the rally stage, approached in line with the stairs), the range with the summit trail (`summitTrail`), rivers and lakes with flowing water on the grid (`World::waterY/flowX/flowZ`), stations. `staircase(...)` cuts a ramp along the ledge lips and lays thin pitched rock boxes on it (a step is never more than a riser). Blackwater Bayou (`kBayou*`: the swamp floor at a set water level on the grid, `bayouMask`; the mud trail `bayouTrail()` laid to a designed profile with puddles, the two wallows and a corduroy; MUD HILL's line across the loop) and Cold Creek (`kCold*`: a U-shaped valley carved into the range, `coldMask` for where it's under snow; the snow road `snowRoad()`, the frozen creek and pond, the lodge). `groundProfile`/`layTrail` lay a trail to a set profile after THE WALL's site is found (so it never moves); the bayou and Cold Creek scenery use their own random numbers, so everything placed before stays put. `frontierFeature(name)` gives positions for tests and the autopilot menu (start, mesa, mesaclimb, wall, walltop, wallfrom, garden, summit, tarn, lake, bog, delta, bowl, ford1-4, track, outpost, lakeside, hut, bayou, bayoufrom, wallow1-2, mudhill, mudhillfrom, mudhillto, snowroad, snowfield, coldcreek, lodge, icepond; `bayou:S` / `snowroad:S` are the points S m along the mud trail / snow road) |
| `src/editcore.h/.cpp` | The world editor's working state, no graphics: brushes, paint, flood/dry, objects (and the map's own), start and gates, settings, undo/redo, picking; every edit goes into the World and the Level at once |
| `src/editor.h/.cpp` | The world editor's UI: free-fly camera, tools, panels, the brush ring and course marks drawn over the view, open/save/play. main.cpp routes events to it while `editor.active`; the renderer catches up via `updateTerrainRegion/updateWater/updateObjects` |
| `tests/editor.cpp` | The editor core: brushes, undo, objects, course, water, and that what it makes saves and plays back the same |
| `tests/backcountry.cpp` | The Backcountry: map sanity (4 camps on flat pads, every surface, 3-5 km of trail, ford shallow and deep crossing deep, the shelf's drop), a Civic stalling in the Mud Run's mud and on the creek's boulders, a set-up Bronco through all four bog holes, up the Creek Crawl, down Slickrock Ridge and over Heartbreak Hill, wading the ford, the 4Runner crossing the deep crossing (a Civic's engine drowns) and driving the shelf and pass roads, autopilot from the trailhead to Far Side Camp. `BC_TRACE=1` prints the drives |
| `tests/frontier.cpp` | Map sanity (fords shallow, rivers deep, every surface present), wading a ford, drifting in the current, the mesa and THE WALL by autopilot and by a scripted crawl, autopilot across the river; the bayou (puddles, wallows, mud share) and Cold Creek (snow share, ice, road grade) maps, a Civic bogged in THE WALLOW and a set-up Bronco through both wallows and over MUD HILL, the 4Runner up the snow road, autopilot to the bayou and the lodge (`driveLine` follows a trail by pure pursuit) |
| `src/backcountry.cpp` | The Backcountry map (`World::buildBackcountry`, `MAP_BACKCOUNTRY = 4`, 1 m grid): forested hill country with a trailhead camp and a network of trails (Mud Run with ruts and four bog holes, Creek Crawl with boulders and ledges, Slickrock Ridge, Ridge Road and the ford, Heartbreak Hill, Sand Wash and the dune bowl, Shelf Road over a canyon, High Pass in snow, Tower Road, cabin and wreck spurs), Elk River with a ford and a deep crossing, four repair camps, the mine, fire tower, cabin and wreck. Trails are laid like Frontier's (`groundProfile` = the land smoothed and grade-capped with pins at the camps and junctions; `layTrail` sets the tread and eases the land into it; `scanNear` visits the cells near a polyline), plus `corridor` (shapes the land round a trail to a designed profile: the creek's gully, the ridge's spine), `addSteps` (rock ledges: the profile steps and a slab stands on the lip) and `crewTrail` (a switchback generator on the smooth `macroHeight` land, for the pass and the tower road). Its polylines are pure functions of constants, so tests and the menu can use them: `backcountryTrail(name)` (mud, creek, ridge, ridgeroad, hill, wash, shelf, pass, tower, cabin, wreck, river) and `backcountryFeature(name)` (trailhead, mine, slab, ford, deepcrossing, fordbank, deepbank, hilltop, mudstart, bog1-4, washmid, dunering, shelf, overlook, wreck, snowcorner, towerlook, firetower, cabinend, passhut, sawmill, farside...). `BC_DEBUG=1` prints each profile's worst cut and fill while building |
| `src/mountain.cpp` | The mountain map: massif, spiral summit trail, valley loop, trailhead, crawl park, creek, the Widowmaker Spur and Boulder Gully, repair stations, scenery. `mountainFeature(name)` gives feature positions for tests (spur, pinnacle, gully, gullytop, halfway, garage, pit, whoops...) |
| `src/rope.h`, `src/recovery.cpp` | Winch lines and tow straps (`Sim::ropes`, forces in `Sim::ropeForces` at 2 kHz, spread over the chassis by `Car::pullHook`), anchors, towed-car behaviour (`Sim::drive`), tire airing, sway-bar reconnect, repair stations (`Sim::updateTrail`, run by `advance`), `placeUpright`/`rotateRigid` |
| `tests/recovery.cpp` | Winching, strap tow, repair, righting |
| `src/net.h/.cpp` | Multiplayer (`NetSession`): TCP for reliable messages (join, spawn/despawn, lines), UDP for vehicle states (20 Hz, fragmented, node offsets from the rigid rest shape as int16 mm). The host relays. Each machine owns and simulates its own cars; others' are `Car::remote` (kinematic: positions from the network, they only push back). `send()` diffs cars (`netId`, `buildCount`) and ropes (`Rope::netId`) each frame, so game code doesn't call into it |
| `src/autopilot.h/.cpp` | Route planning (A* on a 2 m grid, cached per `World::stamp`: slope, surface, how tall the rocks/logs/ledges in each cell stand), seeing (`sense`: ~50 candidate lines rolled out over the scene, scored), drive setup (`setUp`: transfer case, lockers, sway bar, tire pressure), driving, unsticking. `Car::pilot`, run by `Sim::pilotStep` from `Sim::drive` |
| `src/pilotsense.h/.cpp` | What the autopilot sees: `Scene` (exact tops of boulders, boxes, logs, poles, other vehicles over the terrain, cached on a 0.125 m grid per look) and the step-climb table `stepCap` (measured: see the README) |
| `tests/surfaces.cpp`, `tests/handling.cpp` | The surface course strips, tire treads, ABS/TC on loose ground, soft landings (drops and jumps onto sand vs rock), ruts (`surfaces/ruts` is also the screenshot scene for tracks and sinking); keyboard steering at speed |
| `src/gamepad.h/.cpp`, `src/gamepad_sdl.cpp` | Game controllers (Xbox / PlayStation / generic, SDL2 GameController API; declarations in `sdl_min.h`). `gamepad.cpp` has no SDL or GL (it's in `PHYS`, so the tests link it): radial deadzone and curves, speed-sensitive steering (`steerLimit` mirrors the key limit in `Sim::drive`; change one and change the other), trigger pedals with the keyboard's brake-to-reverse (`pedals`), the bindings (`Logic::update`: button presses come out as `Tap`s, i.e. the key the keyboard would press, which main.cpp feeds into its one key switch; held controls are `Intent`), `Logic::apply` (puts analog steering/pedals on the car after `Sim::drive`; centred pad = keyboard path untouched), `Rumbler`, glyph names (`pad::hint`, `helpRows`). `gamepad_sdl.cpp` is `pad::Device`: hot-plug, polling, rumble; it's the only file that calls SDL controller functions. In main.cpp every pad hunk is fenced with `// ---- gamepad`. Game flags: `--no-pad` / `CZ_NO_PAD=1` (use them in every automated launch; the pad is also ignored when the window lacks focus and in scripted `--shot-frames` runs). Settings: `padSteering/padDeadzone/padRumble` (CONTROLLER page under SETTINGS) |
| `src/platform.h` | Linux/Windows differences (config folder, mkdir). Sockets are wrapped at the top of `net.cpp` |
| `tests/autopilot.cpp` | Autopilot routes and drives |
| `tests/gamepad.cpp` | Controller logic with synthetic input: shaping, steer ratio, pedals and reverse, bindings, rumble, a Bronco's 80 km/h lane change and a 4L crawl on the pad |
| `tests/net.cpp` | Host + client sims in one process over loopback sockets |
| `src/scenery.cpp` | Meshes for trees (pine, broadleaf, snag), boulders, rock slabs, logs and bushes. Rocks hug their colliders. Each builder skips what the scanned props cover (`ScanCover`) |
| `src/props.h/.cpp` | Scanned props without GL: the baked `.prop` model files (3 LODs, materials), `props.txt` (manifest), `rocks.hull` (one convex hull per rock scan, <= 48 corners, the `Rock` v/f format), `buildRockHull` (the hull builder the baker uses), and the per-thread switch for scanned rock hulls (`activeRockScans`, `ScopedRockScans`): the game turns it on, `run_tests` never does (so the physics tests keep the code-made rocks); `tests/props.cpp` turns it on for its own scenarios. `CZ_PROPS_DIR` points at another bake |
| `src/propdraw.h/.cpp` | The scans on screen: instanced meshes with diffuse/normal/ARM maps lit by `kCommonFS` (sun, sky, fog, lamps, shadows; props cast shadows), LODs by distance (trees) or size on screen, culling by 32 m cells, 8-view billboards for far trees, scatter round the camera (grass, ferns, stumps, branches: off roads, painted ground, water, pads; the editor's ground edits re-place it), cliffs on slopes steeper than ~44 degrees (visual only). What maps to what: `setWorld`. Quality = `RenderSettings::props` (SCANNED SCENERY row with `ground`). `CZ_BENCH=1` prints its GPU time (timer queries), CPU time and counts |
| `src/groundtex.h/.cpp`, `src/groundshader.cpp` | The photo-scanned ground: 10 Poly Haven sets in two texture arrays (`assets/ground/`), and the terrain shader: per-cell paint -> material mix (`expandPaint`), wobbled height-aware blending, two-tap anti-tiling, triplanar on steep ground, distance fade; paved/ice/freeway/circuit stay procedural (`terrainProcedural` in render.cpp) |
| `tools/bakeprops.cpp`, `tools/fetch_polyhaven.py` | `python3 tools/fetch_polyhaven.py assets/source/polyhaven` (gitignored), then `make bake` -> `assets/props` (committed, keep it under ~60 MB). The unit tables (`kUnits`, `kCards`) say what each scan becomes: triangle targets and atlas sizes per LOD, card keep shares and triangle budgets, hulls |
| `tests/offroad.cpp` | 4x4 drive modes and lockers, rock hits per truck, ramp travel index, crawl tests on the mountain (twister open vs locked, ledges, slickrock), map sanity |
| `tests/components.cpp` | One part at a time: airborne tires, broken half shafts, door glass, live axle articulation and landings, brake TC, torque converter, tire into fender, sway bar set, frame press and frame twist |
| `tests/scenarios.cpp` | Every scenario and the per-vehicle real-world expectation tables, indexed like the catalog |

## Conventions and gotchas

- **Coordinates:** vehicle-local x is forward, y is up, z is right (so -z is the left, driver's side). The lattice goes front to back: `latticeX(vs, 0)` is the nose.
- **Real data:**
  - Every vehicle is a real car built from published specs: dimensions, curb weight, weight split, torque curve, gear ratios, tire sizes.
  - Comment where each number comes from.
  - Estimates are fine, but label them as estimates.
- **The 4Runner is one specific truck:**
  - It's the modified 2002 Limited from the Bring a Trailer listing `2002-toyota-4runner-41`.
  - That means black over silver, OME coils plus a 2 in body lift, 35x12.50R17 KO2s, armor bumpers with a winch, a snorkel, a roof basket and a Magnuson supercharger.
  - Its mod slot 0 is "AS BUILT".
- **Explicit integrator, so watch stability:**
  - Light nodes on stiff beams blow up.
  - The stability pass at the end of `Car::build` lets nodes borrow mass from their neighbors, keeping the total.
  - Raise stiffness (`k`) cautiously. Raising yield or break strain is safe.
- **Multiplayer and the sim:** remote cars skip forces and integration (they coast on their sent velocities between frames), never sleep or repair, and lines pull only on local cars. A winch's length is set by its owner's machine. Don't pause or scale time while `net.active()`.
- **Don't overwrite a binary someone's playing:** `make build/crumplezone-dev` links the game to a side path.
- **Rebuilding in place:** call `Car::setDown(world)` after `respawn()` anywhere that isn't flat (repairs, arcade recovery, vehicle swaps already do). `build()` makes the truck level, and on a hillside the uphill tires would start inside the ground and get rammed up through the suspension into the body (`recovery/rebuild-on-slope`).
- **Turning a car rigidly:** use `Car::rotateRigid` (it also turns `chassis.rot`). Rotating the nodes alone makes the shape-matching frame drag the body back to its old attitude and leave the wheels behind.
- **Truck strength:** `frameStrength`, `panelStrength` and `pinStrength`, plus physical `EXTRA_SKID_PLATE`/`EXTRA_SLIDERS`, only reach the frame between the axles and the vertical/crosswise members under the belly and sills. The crumple zones ahead of the front axle and at the tail are left alone, so the wall crash tests still crumple. Bumper covers stay stock.
- **World changes redraw themselves:** `World::buildGrid()` bumps `World::stamp`; the game re-uploads the map when it changes (so a scenario's own map and the rocks/boxes it adds show up), and the autopilot's route grid rebuilds. Call `buildGrid()` after adding anything to a world.
- **The surface course** sits on the proving ground north-west of the pad (x -118..-48, z 72..126, flattened, a concrete apron 4 m round it). Tests that drive there will hit sand and mud: keep test lanes off it.
- **Tires:** `tireCurve(s, bite, keep)` with bite from the surface and tread (`kTreadBite`), keep from the surface; grip = surface grip x `kTreadGrip[tread][surface]`. ABS and TC aim at `slip x Wheel::bite`. `Wheel::sink` lowers the tire into sand/mud/snow (it's subtracted from the contact penetration) and adds bulldozing drag; it only comes back up as the tire rolls. A hard landing past `softBearing()` pushes it down to `softCrushMax()` instead of loading the tire further; body nodes on those surfaces get a crush layer (6% stiffness, hard below `sink x 3`, little push back). The harness's tunnelling check allows that depth. `Wheel::clog` is mud in the tread. Keyboard steering is limited to what the tires hold (`Sim::drive`): the AI and autopilot set `steer` directly and aren't.
- **Hot reload:** the game watches `/proc/self/exe`; after a rebuild F5 `execv`s the new binary with `--map --player --spawn --resume-model`. `CZ_AUTORELOAD=1` reloads without the key (testing).
- **Tube-frame vehicles (rock buggy 4, side-by-side 5):** `VehicleSpec::openFrame`. The lattice is the chassis and cage and isn't drawn; profile char `p` marks the cells that carry skin ('w' is a wheel-well cell with skin; plain `#`, `G` and `W` stay hidden). They have no doors, covers or glass: the carbuild panel list keeps only the hood. Everything you see of them is `buildOpenFrame()` in `carvisual.cpp` (cage, rails, bumpers, seats, wheel, exhaust, light bars, fenders), bound to the lattice like any part. Keep their mass items left/right symmetric: an 8 mm CG offset made the buggy veer 2 m in 8 s of coasting. The front solid axle steers (`carbuild.cpp`, "SOLID_AXLE": kingpin nodes shared between the housing and the knuckle, tie rod to a node on the housing; the steering code works from `kpLow/kpUp/armRest/rackRest` in rest coordinates, so it needs no special case). Spring rates are sized for the tire it has (`tireSoft`), and `looseGrip` is a crawler compound's extra grip off the pavement.
- **Live axles on hard landings:** they land on jounce bumpers on the tube's centre line and their frame brackets are one welded cluster, so they don't flip over. `components/axle-landing` drops every live-axle truck (Bronco, 4Runner, rock buggy, Tacoma on leaves) 1 m and 2 m flat, 2 m tail first and 1.5 m at 40 km/h: worst housing pitch 4-14 degrees, and it settles back level. It watches the rear axle only (the buggy's steered front isn't checked yet).
- **Visual-only parts don't collide.** Anything in `buildExtras()` is drawn but has no physics. Add its weight as a `MassItem` in the spec.
- **Panels** are separate node sheets pinned to the body (hinges, latches, clips).
  - Pins break by distance (`breakDist` = force / k).
  - They also break when the panel around them bends (the limits in `Car::updateStats`).
- **Ride height:** `raiseBody()` moves the body, engine and mass items up. Mods use it for lifts, and `tires()` calls it for the change in tire radius.
- **Mods:** the mods test checks that the ride height changes by exactly what the parts say, that the 0–80 km/h time changes the expected way, and that there's no damage or tipping.
- **Ladder frames** (`VehicleSpec::ladderFrame`): each rail is its own shape-matched group (`Car::railFrame[2]`, see `chassisGroupForces`). Centreline nodes (|z| < 5 cm) belong to both rails at weight 1: they set how much the frame twists. A rail flows plastically by whole sections (a station's 4 corners move together by their mean excess; a lone node only past 3x `yieldDist`), and the frame beams at a flowed node take the new rest length. The fit is by real mass; bracket nodes are held harder (`kBracketYield`, 70 kN) but never lead the fit, and the leftover net force/torque is removed (a loaded bracket on one rail used to steer the truck). Calibrate with `components/frame-press` and `frame-twist`; `railYield` and `frameStiffness` are per vehicle.
- **Mounts:** `Beam::snub` (ladder-frame engine and body mounts only) is a hard stop between the snubber gap and 2.5x yield; past that the mount takes a set. Unibody engine mounts must stay soft or the wall crashes stop crumpling.
- **Panels vs body** (`panelBodyContact`): a panel node off its pin is held outside the body quads it sat on (inside only when behind all of them, so a node at a fold isn't caught), and only if it arrived from outside (`Panel::outside`): one that got deep in some other way is left alone, or the contact pumps it. Hinged panels also get `PIN_CHECK` stops (`Pin::slack`, `Panel::checks`, not in `Panel::pins`, whose order matches `panelNodes`): doors on their lower half only.
- **Tire vs own body** (`buildWheelRub`, `wheelBodyContact`, faces in `Wheel::rubFaces`: skin triangles and the rails' sides): every skin sample has an allowance, how far the stock tire (or `wellTireRadius`/`wellTireWidth`, the biggest it was cut for) already reaches there over its travel and steering lock. Only penetration past that pushes. Change a vehicle's suspension travel or tires and the allowance follows.
- **Leaf springs** (`leafSprings`, `leafHalf`): the visible `LINK_LEAF` is the front half (seat to hanger). A hidden lateral tie and a hidden wrap link (from under the housing to the frame) stand in for the pack's side and wind-up stiffness; the spring rate is a virtual coil above the seat with `Beam::friction` (inter-leaf friction, 8% of the corner's weight). `Wheel::shackle` is only for drawing.
- **Stability pass and CG:** borrowing mass for stability moves the CG; a rebalance afterwards tilts the masses of the body's roomy nodes to put it back. If a new vehicle's CG test moves after a stiffness change, that's why.
- **ESC** (`escYaw`, `escBrake`): brakes the outside front wheel and trims the throttle when yaw rate exceeds the steering's reference, or the rear slides; needs ABS, off in low range and with TC off. Brake TC judges spin along the drive direction (signed).
- **Damage metric:** `Car::damage` is plastic strain over the BODY and CABIN beams, with frame and mount damage added on top (capped at 1). Adding frame beams doesn't dilute it.
- **Shader edits without a GPU:** `python3 tools/glslcheck.py` pulls every program's shaders (car, lit, photo ground, water, grass, props, billboards and their depth passes) out of `render.cpp`, `groundshader.cpp` and `propdraw.cpp` and compiles them with `glslangValidator` (apt `glslang-tools`).
- **Screenshots of the scenery:** `--photo` hides the HUD, `--props N` / `--ground N` set the scanned scenery level, `--menu 1` opens the settings page. Without a GPU (Mesa llvmpipe) a 640x360 frame at quality 1 takes seconds: keep sizes and frame counts small.
- **Style:** match the surrounding code. Short comments explain *why* in plain words, with real-world figures where they matter.

## Open work

- **Visual polish:** the five vehicles are recognisable, but they're built on a coarse lattice. Shapes are rough at the nose, the Civic's hatch and the Jesko's canopy.
- **Sound mix:** the engine voices haven't been heard on real speakers yet, only checked offline. Balance the volume and turbo/blower levels per car once someone listens.
  - To check offline, `build/review/claude/audiotest.cpp` renders each voice to WAV and spectrogram images.
- **`offroad/shaft-break`:** a Bronco in 4L, both locked, at full lock on rock is meant to bind and snap a CV. The front CV's limit at full lock is ~4.65 kNm and the grip-limited shaft torque peaks ~7.6 kNm in a smooth bind, but the half shafts' compliance soaks up the hop spikes that break real ones. Failing since the ladder frames (839f1c8).
- **Tacoma:** no sound voice of its own yet (it uses the generic V6), no winch or sliders (stock truck).
- **Frames don't neck in at the front wheels:** the stock tire's sweep at full lock and full bump reaches ~8 cm past the rail's outer face (Bronco, 4Runner), so that much is allowance and a wheel knocked inboard can sink that far into the rail. Real frames narrow there; doing it means moving the front brackets with them.
- **Solid axles are ~20-40% too heavy:** every housing node is tied to every other at knuckle stiffness, and the stability pass has them borrow ~35-80 kg off the body to stay stable (Tacoma's axle and wheels: 288 kg against ~210 from its parts). At 60% stiffness the weight is right but the housing flexes enough to steer the wheels (a Bronco yawed 27 degrees braking). A leaner truss with the same rigidity would fix it.
- **Scanned props and ground (branch `scans`, paused, WIP):** `assets/props` isn't baked: api.polyhaven.com was blocked by the cloud session's network policy, so `tools/fetch_polyhaven.py` couldn't download the sources. The baker, hull physics, renderer and `tests/props.cpp` were checked against synthetic stand-in glTFs only (props tests pass on that bake; without a bake they fail with "run make bake"). To do: fetch, `make bake`, check `du -sh assets/props` (< 60 MB), re-run `props/`, measure the Backcountry at quality 2 on a real GPU, tune card budgets and LOD distances, check ground screenshots (ruts, puddles, night, editor paint, Frontier colours).
- **Suspension links vs the frame:** arms are beams with no contact; in a roll a lower arm can pass ~4 cm through a rail.

## History

- **2026-10-11, Claude:**
  - Real-car rebuild of all four vehicles.
  - Rebuilt the 4Runner as the listing truck.
  - Strengthened panels and bodies.
- **2026-10-11/12, Codex:**
  - Turned the Civic into the 1990 EF Si hatch.
  - Gave each car its own lamps, trim and wheels, and the Bronco its black hardtop.
  - Made door glass and mirrors move with their panels.
  - Physics fixes: timestep-independent contacts, airborne tire state, locked diff with a broken shaft.
  - Added `tests/components.cpp`.
  - Stopped at its usage limit with 7 failing tests.
- **2026-10-12, Claude (took over from Codex):**
  - Fixed the 7 failures:
    - Door window frames now rest on soft seal pins (`PIN_SEAL`).
    - The Jesko's mass is placed where the real car carries it.
    - The pitch-inertia test now uses the dynamic index.
  - Found and fixed a long-standing bug: the steering rack jumped instantly, so every car's tie rods bent when the AI snapped the wheel. The rack now moves at a finite speed (`rackSteer`), with test `structure/steering-snap`.
  - AI lane holding now has an integral trim, and its recovery ramps smoothly.
  - The skidpad test now holds a real 30 m circle and measures true lateral acceleration.
  - The Jesko's roll stiffness is biased to the rear, and it has Cup 2 R lateral grip.
  - Added per-vehicle engine sound.
- **2026-10-12, Claude:**
  - Added a sculpted 4Runner body shell (`src/bodyshell.cpp`), drawn instead of the smoothed cage.
  - Its vertices are bound to the physics cells and panels, so it dents and its doors, hood and liftgate move.
  - The shader draws its windows, lamps, grille and seams in meters (`uShell`, `uShA`–`uShD` in `render.cpp`).
  - At the same time, Codex built the Jesko's mesh (`buildJeskoMesh` in `carvisual.cpp`).
- **2026-10-12, Codex — Jesko body pass:**
  - Authored curved, dimensioned Jesko patches with open arches and a separate canopy, bound to the cage and detachable panels.
  - Split topology at panel seams; attached lamps to the hood and windows/mirrors to their doors. Glass damage maps to physical glass faces.
  - Matched the dashboard, two bucket seats, lamps, intakes, wing and hexagonal center exhaust to the body; hid solver-only upper tub braces.
  - Moved its equivalent coil package inboard, below the low hood, retaining the linkage-derived spring and damper rates. This does not implement the full real Triplex rocker system.
  - Added `make build/test_jesko_visual` / `nice -n 15 ./build/test_jesko_visual` for mesh dimensions, transforms, detached seams, glass, garage height mods and crash deformation.
  - Final validation: 155/155 physics scenarios and 13/13 mesh checks passed. Screenshots, logs and the handoff are in `build/review/jesko-review.md`.
- **2026-10-12, Claude: graphics and pause menu**
  - Moved rendering to HDR, with MSAA, SAO ambient occlusion, bloom, ACES tone mapping and a vignette.
  - Two shadow cascades with Poisson PCF.
  - Procedural sky and clouds (also in reflections), and height fog.
  - Terrain: meadow, rock with strata, snow, craggy rim and a backdrop mountain range (render only).
  - Better trees.
  - Esc now opens a pause menu instead of quitting. It works with mouse and keyboard and has car select, garage, settings, controls, reset and quit. Settings are saved; mph is an option.
- **2026-10-12, Claude: 4x4, mountain map and scenery**
  - Transfer cases: `VehicleSpec::transfer` (part-time / full-time), modes in `driveModes()`, selectable lockers (`lockerFront/Rear`; `Car::setDriveMode/setLocker`, `diffType`, `centerLocked`). Default modes (4A / 4H) reproduce the earlier physics exactly.
  - Locker mods are now selectable (they used to be permanently locked).
  - Mountain map (`MAP_MOUNTAIN`), chosen from the pause menu.
  - New colliders: boulders (spheres) and logs (capsules), plus rock slab boxes.
  - Surfaces: dirt and rock.
  - Scenery: rocks and bushes on the proving ground too, and procedural grass round the camera (`kGrassVS`, from the terrain data texture).
  - Tests: 161.
- **2026-10-12, Claude: assets pass**
  - Rocks are convex hulls (`World::addRock`, exact closest-point collision).
  - Lakes (`World::addLake`, `waterLevel`): drag, buoyancy and hydrolock (`Car::hydrolocked`), plus a water shader.
  - Fixtures (`World::fixtures`: fence, cabin, signs, trail map, tables, stumps; meshes in `buildPropMesh`).
  - Forest logs, outcrops and scree, reeds, wildflowers in the grass, cloud shadows.
- **2026-10-12, Claude: Bronco and Civic shells**
  - `bodyshell.cpp` is now general: `ShellLines` plus a per-car `Style` (corner radii, nose roll, shoulders, side bow, arch shape).
  - Sculpted shells for the Bronco (`uShell` 2) and the EF Civic (3), alongside the 4Runner (1). Each has its own shader decals: windows, lamps, grille, hinges, hardtop, moonroof.
  - Bumper covers ride on their panels.
  - Flares follow the shell's arch (`shellArchHeight`).
  - `shellParam` now has 20 values (`uShE`).
- **2026-10-12, Claude: crawl and recovery update**
  - Tougher trucks: frame, panel and pin strength fields; skid plates and sliders are physical. New `structure/rock-hits` tests.
  - Suspension travel per vehicle (`bumpFront`...), sway-bar disconnect, tire pressure in the tire model, locker interlocks, axle shaft/CV breakage.
  - Winch, tow strap, repair stations (`PROP_GARAGE`), real/arcade recovery setting, compass HUD.
  - Mountain: Widowmaker Spur, Boulder Gully, scree surface (`SURF_SCREE`, `PAINT_SCREE`), three stations.
  - Tests: 174. Multiplayer is next: ropes, controls and repair state are plain `Sim` data.
- **2026-10-12, Claude: multiplayer**
  - `NetSession` (host/join from the menu or `--host` / `--join` / `--name`), remote cars, shared ropes across machines, player list, name tags, buddies on the compass.
  - Fix: `Car::setDown` after in-place rebuilds (the rear axle ended up in the body on slopes).
  - Tests: 176.
  - Not done yet: host migration, map change mid-session, remote engine sounds, chat.
- **2026-10-12, Claude: winch aiming, autopilot, trail repairs, Windows target**
  - V opens a picker (`Sim::winchTargets`, `hookWinchTo`; `WinchAim` in main.cpp).
  - Autopilot to named places, followed vehicles or the camera's look point, with a speed and route mode (menu page 3).
  - Trail repairs from any vehicle parked alongside (`Car::fieldRepair`).
  - `make windows` (mingw-w64, SDL2 2.30.9 runtime in third_party/sdl2-win64): built, and tested under Wine, including a Linux host with a Windows client.
  - Tests: 183.
- **2026-10-12, Claude: autopilot that sees, surfaces, handling, hot reload**
  - Autopilot scans the ground ahead and picks a line (over / straddle / around), and its own 2H-4WD-low range, lockers, sway bar and tire pressure. The step it can climb per setup was measured (Bronco 0.15 / 0.30 / 0.45 m). The HUD draws its line, options and verdicts.
  - New surfaces (gravel, sand, mud, snow, ice, wet asphalt) with their own grip curve, sinking, rolling drag and mud clogging; tire treads (street, semi-slick, all-terrain, mud-terrain); the surface course on the proving ground; mud creek, beach and summit snow on the mountain; roost, spray and ruts.
  - Keyboard steering limited to the tires' grip at speed (a tap at 130 km/h was asking 1.3 g of the Jesko). Engine drag control. Traction control aims a little nearer the peak.
  - Fixed: scenario mode wrote into a car that wasn't there yet (heap overflow); sim events were cleared before the HUD saw them; the route grid was shared between test threads.
  - Tests: 204.
- **2026-10-12, Claude: freeway**
  - Endless six-lane freeway map (wraps every 400 m), odometer and best speed on the HUD, top-speed tests.
  - Speed limiters (`VehicleSpec::speedLimit`): Bronco 161, 4Runner 171 km/h.
  - Jesko at 300-400 km/h: it weaved and crashed. The causes were the limited-slip diff holding the rear wheels together every substep (now it eases off above ~125 km/h, as e-diffs do) and the floor touching the road under downforce. The fix: raised 15 mm, plus a heave (Triplex) spring and damper per axle (`heaveRate/heaveGap/heaveDamping`) that comes in with airspeed. Keyboard steering also builds more slowly past ~150 km/h.
  - Tests: 209.
- **2026-10-12, Claude: background music**
  - Added `src/music.*`, generative music synthesized at runtime, one mood per map: lo-fi (proving ground), airy folk/ambient (mountain), synthwave (freeway) and desert road trip (frontier).
  - Crossfades on map change. There's a MUSIC slider in the settings (default 40%, saved as `music`).
  - Checked offline: about -18.7 dBFS RMS, peaks under -3 dBFS, no clipping or DC, no aliasing above 12 kHz from the tonal parts, under 1% of one core.
- **2026-10-12, Claude: day and night, headlights**
  - A clock runs the sun and moon (`src/daylight.*`): sky, ambient, fog, clouds, shadows and exposure follow it; stars, a phased moon, a night-vision grade. Setting TIME OF DAY (CYCLE at 6-96 min a day / MORNING / NOON / EVENING / NIGHT), `--time H`, `--lamps N`.
  - Real headlights (`src/lights.*`): two spot lights per car for the nearest 8 cars, in every lit shader; glowing lenses, tail, brake and reversing lamps; smashed lamps go dark. Shift+L.
  - The host's clock is shared in multiplayer (`MSG_CLOCK`). Physics untouched.
- **2026-10-13, Claude: world editor UI**
  - `src/editor.*` on the agent-written `src/editcore.*`; renderer partial updates (`gl.h`: BufferSubData, TexSubImage2D); F2, menu entries, `--editor`, `--script editdemo` (screenshots). Tests: tests/editor.cpp.
- **2026-10-12, Claude: levels, menus, drivetrain, toughness**
  - `src/level.*` (level file format, apply to World), `src/levels_builtin.cpp`, `src/levelrun.*` (gates, laps, medals, leaderboard), title screen + levels page in `src/menu.*`, MSG_LEVEL/MSG_GAME in net (protocol v2). Tests: tests/levels.cpp, net/level, tests/circuit.cpp.
  - Drivetrain: crank inertia + gear/diff constraints solved at the end of `Car::wheelStep` (open diffs split torque equally). TC watches combined slip; ABS cornering brake control; keyboard slide lock only when countersteering.
  - Cars ~40% tougher (kSpec yields), engines +15% (catalog and mods). Frontier circuit rebuilt from straights and arcs; paddock moved off the straight.
  - R = repair in place, R R = respawn/restart; winch speed (-/=); mouse look up.
  - Open issue: crawl tests (gully, widowmaker, wall/mesa stairs, shaft-break) regressed while the axle and soft-ground agents' edits were in flight.
- **2026-10-12, Claude: the Frontier map**
  - New map (`--map 3`): race circuit with lap timer, 4 km rally stage, desert with the mesa climb, THE WALL, a pine range with a summit trail, frozen tarn, two rivers with current and fords, Echo Lake, a mud bog and delta, four repair stations.
  - Water on the height grid with flow, so rivers run downhill. Floating cars drift with the current, and the water shader follows the flow.
  - Autopilot:
    - holds first gear crawling in low range (`Autopilot::holdFirst`)
    - walks up to ledges
    - airs down when stuck against a ledge on full tires
    - backs off at a walk in low range
    - deep water is dilated a cell in the planner
  - Debug env vars in `tests/frontier.cpp`: `CZ_TRACE` (`CZ_FAST` for every 10 frames), `CZ_DEBUG`, `CZ_ROUTE`.
  - Tests: 217.
- **2026-10-12, Claude: Frontier mud section and snow biome**
  - Blackwater Bayou (south-east): a swamp at Echo Lake's level, with still water on the height grid, mud flats, hummocks, drowned snags, logs and stumps. It has a 740 m marked mud trail with puddles of every depth, two wallows (a Civic can't climb out, a set-up Bronco wades through), a log corduroy and MUD HILL across the middle.
  - Cold Creek (north-east): a glacial valley under snow. It has a snow road with wheel tracks, ploughed banks, snow poles, black ice and a frozen-creek crossing, plus snow-laden pines, snowy boulders, a frozen pond, and Cold Creek Lodge with a repair station.
  - Scenery: pines standing in snow carry it on their boughs, rocks on snow have white tops, and reeds grow in still shallow water over mud.
  - Render (two small shader changes): wheel tracks on snow-covered roads and trails. Water over a mud bed is murky brown and doesn't foam white at its edge.
  - Tests: 8 new Frontier scenarios. `frontier/the-wall` (autopilot) was already failing from concurrent physics/autopilot work: it fails identically with the map as it was before these changes.
- **2026-10-12, Claude: tire tracks, visible sinking, soft landings**
  - Tracks for every wheel of every car (`Effects::Rut`, a 12000-segment ring, oldest fade): decal strips drawn multiplied into the ground (fx modes 3/4, `BlendFunc(DST_COLOR, SRC_COLOR)`), with a groove profile, tread print and shading from screen-space derivatives, so they're lit like the ground day and night. The old skid marks sat at the tire's bottom, under the ground, once it sank: hidden.
  - `Effects::buildRutWalls`: terrain-painted lips (lit shader material 7) along fresh ruts in sand, mud and snow, and a collar round each sunk tire; drawn right after the terrain.
  - Soft ground in the physics (`softBearing`, `softCrushMax`, the node crush layer): landings in sand, mud and snow are far gentler. Tests `surfaces/soft-landing`, `surfaces/ruts`.
- **2026-10-12, Claude: live rear axle clipping (Bronco, 4Runner)**
  - Found: a hard landing (≥2 m drop or tail-first jump) flips the 4-link over centre; the rear housing stays rolled ~110°, pinion up, in the floor. Cause: spring seat above/behind the tube, loaded by the bump stop through a tilting coil-over. Seat on the tube centre line fixes it, but costs crawl tests, so it's reverted. Still open.
  - Visual (`carvisual.cpp`): rails kick up over a live axle (`buildFrameRails`, `axleReach`), no centre rail over the prop shaft, crossmember behind the axle, exhaust over the axle, tank ahead of it, housing drawn at real size.
  - Test: `components/axle-articulation`. Landing test `axleLanding` is written but not registered until the flip is fixed.
- **2026-10-12/13, Claude: drivetrain, parts that touch, ladder frames, Tacoma**
  - Live axles land on jounce bumpers on the tube centre line and their brackets are one welded cluster: no more flipping (`components/axle-landing`). Half shafts are torsion springs; lockers are exact rows in the gear solve; torque converter; brake traction control on open diffs.
  - Tires collide with their own body (with a per-sample allowance for the stock tire's travel); sway bars take a set.
  - Real ladder frames on the Bronco, 4Runner and Tacoma: two plastic rails (shape-held per rail, bending by sections), crossmembers, brackets, rubber body mounts with snubbers. The body is a skin on top.
  - Toyota Tacoma TRD Off-Road (catalog 6, key 7 after the merge with the crawl vehicles): leaf-sprung rear axle, hybrid frame, cab and open bed, its own shell and shader style, mods.
  - ESC; rolling drag reacts on the knuckle; the governor reads filtered speed; the traffic AI feathers wheelspin.
  - Tests: 284 (Tacoma rows everywhere, rock hits, RTI, frame press/twist).
- **2026-10-13, Claude: crawl vehicles (branch `vehicles`)**
  - Two new catalog entries (indices 4 and 5, keys 5/6): an Ultra4-class tube-chassis rock buggy (6.2 L V8, 4L80E + Atlas 4.3 low, live axles on long four-links with a steering front axle, 40 in tires, both lockers, ~1,820 kg) and a turbo side-by-side (900 cc turbo triple, CVT as five steps, 4WD with a front diff lock, long-travel wishbones, 32 in tires, ~800 kg). Class figures and estimates are marked in `vehicles.cpp`.
  - New: `openFrame` tube-frame look, steered front solid axle, `XFER_SELECT` (2H/4H/4L) and `XFER_ONDEMAND` (2WD/4WD) transfer cases, `tireSoft`, `looseGrip`, `ledgeFactor` (autopilot step table), `linkLength/upperLink/coilTop/axleSeat/armLower/armUpper`, `mountainFeature("whoops")`, garage mods for both.
  - Tests: per-vehicle tables extended to six; `offroad/` ledge, slab, twister, ledges, dome and whoops comparisons, `surfaces/utv-mud-sand`, `autopilot/buggy-ledge`, `autopilot/step-table`, `net/crawlers`, `structure/rock-hits/buggy|utv`. Tube-frame checks that assume glass, doors or a crushable nose have their own limits (`tubeFrame()` / `tallBuggy()` in `tests/scenarios.cpp`).
  - Tuning lessons: 0.7 m is the buggy's ledge (0.8 m only with 1.05 Hz springs; it trades against road manners); steeper slabs are friction-limited; a 4-speed with 5.38 axles in 2H is a wheelspin machine, so it leaves in 4H.
  - Baseline before the change: `frontier/mesa-climb` and `mods/bronco/engine/raptor-3.0-v6-swap` failed.

- **2026-10-13, Claude: the Backcountry map**
  - New map (`--map 4`, `src/backcountry.cpp`): forested hill country built for driving with buddies. A trailhead camp and 4 km of trails radiating from it, each rated green to red on its signs: the Mud Run (ruts, mud stretches, four bog holes), Creek Crawl (boulder gardens and eight ledges up to 0.45 m, to an old mine), Slickrock Ridge (off-camber slabs, rock drops, down to Elk River), the ford and the deep crossing, Heartbreak Hill, the sand wash and dune bowl, the shelf road along a canyon (a real drop, the wreck site on the floor below), the snow pass with ice and a frozen pond, the tower road, the cabin spur. Four repair camps, 16 discoverable places (`pilotPlaces`), music style reuses the mountain's.
  - Tests: 12 new scenarios in `tests/backcountry.cpp`. Known flaky or failing, not from this change: the crawl tests `frontier/mesa-climb`, `frontier/the-wall`, `maps/hard-lines`, `offroad/gully`, `offroad/widowmaker`, `offroad/shaft-break` (the last full run failed only `frontier/mesa-climb`), and `mods/bronco/engine/raptor-3.0-v6-swap` (0-80 ratio 1.00, limit 0.99).
  - Bugs found on the way: a corridor shaped round one trail cut into another one's tread (it skips cells near an earlier tread now); rock ledges placed near the river ended up taller than designed, because the river's bank was cut after them.

- **2026-10-13, Claude: game controller support (branch `gamepad`)**
  - Xbox, PlayStation and generic pads through SDL2's GameController API (`sdl_min.h` gained the declarations and event offsets, checked against SDL2's documented values; `SDL_INIT_GAMECONTROLLER` is started with `SDL_InitSubSystem` so a failure only loses the pad). Hot-plugging works (toast `CONTROLLER CONNECTED: <name>`). The full button map is in the README's Controller section.
  - Design: pad buttons become the key presses they stand for (one key switch in main.cpp handles everything, menus included); sticks and triggers are analog and set `Car::steer`, `steerInput`, `throttle`, `brake` and `gear` after `Sim::drive` (so `softbody.cpp` is untouched). Speed-sensitive steering reuses the key limit (what the tires hold), so it is full lock at a crawl and ~4 degrees at 130 km/h for a full stick.
  - Rumble from impacts, rough ground, sliding tires and the rev limiter, only while the pad was the last input and the window has the focus. Settings: RUMBLE, STEERING, STICK DEADZONE (new CONTROLLER page; the settings page now squeezes its row spacing to fit 720p). HUD shows button names while the pad is in use.
  - Tests: 6 new `gamepad/` scenarios. Baseline before the change: 341/415 passed (74 failing from an unrelated merge: the rock buggy and side-by-side rows, `mods/civic` and `mods/buggy`, `components/airborne-tires`, several `offroad/` crawl tests and so on); same set after.
  - Not verified: how the sticks and rumble feel on a real pad (the logic is tested with synthetic input and a virtual SDL controller; the game was only run offscreen), and a Windows build.

- **2026-10-13, Claude: merge, releases, Mac build, wheels that come off (branch `sync`)**
  - Merged the buggy/UTV retune, the merge-failure fixes and the scanned-props work into `sync`; the buggy's frame narrowing at 0.9 (at 0.62 its diff got trapped in the floor on hard landings).
  - Springs sized after the stability pass (live rears sat ~1 cm high); ladder rails held to the same strength end to end.
  - Releases: `.github/workflows/release.yml` (Linux .deb, Windows .zip, universal Mac .app) on a version tag; v0.1.0 published. `make deb`, `make mac`, `src/platform.cpp` (exe path on every OS).
  - Wheels that come off and real spares, Bronco/4Runner/Tacoma only (`src/tires.*`); the on-foot swap is next. A/D on foot were swapped (fixed). Details and the plan: `docs/handoff.md`.
  - Trace tools kept in `tools/trace/`.

- **2026-10-14, Claude: multiplayer page redesign, LAN finding, main menu screen (branch `sync`)**
  - The multiplayer page (`PauseMenu` page 2, `drawMulti`) is two cards: HOST A GAME (game name, map, most players, START HOSTING) and JOIN A GAME (the network list, a typed address); one SESSION card while in a game; a CONNECTING card (Esc cancels) that shows why a join failed. Rows carry `col` (0 across, 1 host, 2 join); up/down stay in a card, left/right switch cards. `drawRow` is the shared row drawer.
  - The main menu is its own screen: `main.cpp` skips `drawScene` while `menu.open && menu.title` and `drawBackdrop` draws a contour-map background; sub-pages opened from it share it.
  - Finding games (`net::Finder`): asks 255.255.255.255, every adapter's subnet broadcast (`net::localAddresses`: getifaddrs, GetAdaptersAddresses + `-liphlpapi` on Windows) and 127.0.0.1; also listens on UDP 27017 for hosts' 2 s announcements. `CZFIND2`/`CZHOST2` add the game name, most players and a session id (one row per game); 0.1.4's `CZFIND`/`CZHOST` still work.
  - Joining no longer blocks the frame: `NetSession::beginJoin`/`pollJoin` (join() loops over them). While `net.joining()`, main skips `net.receive`/`send`.
  - Settings: `session`, `maxplayers`; the join address no longer defaults to 127.0.0.1.
  - Tests: `net/lan-discovery`, `net/async-join`. Suite: 423/436; the 13 failures are physics WIP (tires/*, axle-landing, buggy/UTV rows, rock-hits/4runner, weights, frontier/current, engine/performance on a busy CPU).
