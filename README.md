# Crumple Zone

A soft-body driving sandbox for Linux, written from scratch in C++17 and OpenGL 3.3.

Every vehicle is built like a real one. A welded body structure sits on a stiff
frame, and everything else bolts on: an engine and gearbox on rubber mounts, wishbone
or live-axle suspension with coil-overs, a steering rack with tie rods, drive shafts,
and a hood, doors and bumpers held on by hinges, latches and clips. It's all point
masses and beams simulated at 2,000 Hz. Metal bends permanently when it's
overloaded and tears when it's stretched too far. Dents, crushed fronts, doors
hanging open, a bumper dragging on the road, bent steering, a snapped drive shaft
and torn-off wheels all come out of the physics. Nothing is pre-animated.

## Build and run

You only need `g++`, `make` and the SDL2 runtime library, which most desktops
already have (Debian/Ubuntu package `libsdl2-2.0-0`). You don't need any `-dev`
packages.

```bash
make
./crumplezone
```

| Option | Effect |
| --- | --- |
| `--fullscreen` | Start fullscreen |
| `--size 1920x1080` | Window size |
| `--no-audio` | Disable sound |
| `--no-vsync` | Disable vsync |
| `--no-pad` | Ignore game controllers (also `CZ_NO_PAD=1`) |
| `--spawn X,Z[,HEADING]` | Start somewhere else; for example, `--spawn 300,0,-80` puts you on the loop road |
| `--scenario NAME` | Play one of the test scenarios (see Testing) |
| `--host [PORT]` | Host a multiplayer session (default port 27015) |
| `--join HOST[:PORT]` | Join one |
| `--name NAME` | Your name in multiplayer |
| `--time HOURS` | Hold the clock at a time of day, e.g. `--time 21.5` (for screenshots) |

**Hot reload (Linux):** leave the game running while you rebuild (`make`). When the new
`./crumplezone` lands, the HUD says NEW BUILD READY; press F5 and the game restarts on
it, on the same map, in the same vehicle (with its garage parts), at the same spot.

## Releases

Download from the [releases page](https://github.com/Benbone21/crumplezone/releases). Each release has three builds:

| File | Platform | How to run |
|---|---|---|
| `CrumpleZone-<version>-mac.zip` | macOS 11+, Apple silicon and Intel | Unzip and open **CrumpleZone.app**. It isn't notarized, so the first time right-click it and choose **Open** (or run `xattr -dr com.apple.quarantine CrumpleZone.app`). |
| `CrumpleZone-<version>-windows.zip` | Windows 10/11, 64-bit | Unzip and run `crumplezone.exe` (keep `SDL2.dll` and `assets` beside it). |
| `CrumpleZone-<version>-linux.deb` | Debian 13+ / Ubuntu 24.04+, amd64 | `sudo apt install ./CrumpleZone-<version>-linux.deb`, then run `crumplezone` or pick Crumple Zone from the games menu. It installs to `/opt/crumplezone`. |

`.github/workflows/release.yml` builds all three on every push to `sync` or `main` (kept as workflow artifacts, as a build check). Pushing a version tag (`git tag v0.2.0 && git push origin v0.2.0`) also publishes them as a release, with notes from `docs/releases/<tag>.md`.

## Windows

`make windows` cross-compiles `build/win/CrumpleZone/crumplezone.exe` (with `SDL2.dll` beside it) and zips it. It needs the mingw-w64 cross compiler (`g++-mingw-w64-x86-64-posix`) and the official SDL2 x64 Windows runtime (`SDL2.dll`) in `third_party/sdl2-win64/`. Settings and the garage live in `%APPDATA%\crumplezone` on Windows. Multiplayer works between Linux and Windows.

## macOS

`make mac` (on a Mac with the Xcode command line tools) builds a universal `build/mac/CrumpleZone.app` (arm64 + x86_64, macOS 11+) and `build/mac/CrumpleZone-mac.zip`. It needs the official SDL2 macOS framework: copy `SDL2.framework` from the `SDL2-2.30.x.dmg` on SDL's releases page into `third_party/sdl2-mac/`. The app is ad-hoc signed, not notarized. Settings live in `~/.config/crumplezone`, as on Linux.

## Testing

The physics, the AI and every garage mod are covered by about 200 scenario tests in `tests/`.
Each one scripts a situation on the real map and checks the outcome against
real-world numbers, for example:

- the body twists 26–34 kNm per degree (a real modern car: 10–40)
- the front wheels steer by themselves less than 0.4° over 5 cm of suspension travel
- 100–0 km/h braking takes 33–42 m in the coupe
- an 8 km/h parking bump leaves no mark; a 25 km/h wall hit bends the nose 3–30 cm with the doors still shut
- a 50 km/h wall hit crumples the front 0.25–1 m, rebounds at most 10 km/h, knocks some panels loose, and the doors stay latched
- the AI laps the loop staying within 2.6 m of its lane

```bash
make test                       # build and run every scenario in parallel (~35 s)
make test T=braking             # only scenarios whose name contains "braking"
./build/run_tests --list        # list them all
./build/run_tests -v ai/        # verbose: show every check and metric
./build/run_tests --update-baseline   # record current values as the new baseline
```

**What every scenario checks**

- **Its own checks:** pass/fail assertions and measured metrics with expected ranges.
- **Global invariants, every frame:** no NaNs, no node faster than 150 m/s, nothing tunnelling through the ground.
- **Drift:** each metric is compared with `tests/baseline.txt`, and anything that moves more than 10% gets flagged, even if it's still inside its range. Behavior changes don't slip by unnoticed.

**The report:** each run writes `build/test_report.html`, with charts (speed, lateral g, body height, lane offset...) and range bars for every metric.

**Watching a scenario in the game:**

```bash
./crumplezone --scenario damage/head-on
./crumplezone --scenario ai/obstacle
```

Checks appear on screen as they're recorded. Slow motion and pause work. When the
scenario finishes, you can drive around in the aftermath.

**Scenario groups**

| Group | Covers |
| --- | --- |
| `dynamics/` | Settling, 0–100, braking, skidpad grip, high-speed stability, lane change, slope holding, drops, washboard, reverse (each per vehicle) |
| `structure/` | Torsional and bending stiffness (test rigs in free fall), how much the body shakes, weight and balance, suspension travel, bump steer (each per vehicle) |
| `damage/` | Wall crashes, pole, tree, head-on, T-bone, rear-end, rollover (van with a roof load), jump, crash boxes |
| `structure/rock-hits/` | Trucks on the trail: belly on a boulder, sill onto a rock, nose off a 1.2 m ledge, tipped on its side and roof, a corner into a boulder |
| `offroad/` | Drive modes and locker rules, twister, ledges, slickrock, air-down, ramp travel index, shaft breakage, the Widowmaker Spur and Boulder Gully; the crawl specials against the Bronco (ledge heights, a 48 degree slab, the twister with open diffs, the crawl park's ledges and dome) and the side-by-side through the whoops lane |
| `recovery/` | Winching up the dome, winch stall and line snap, a strap tow to the shop, repairs, righting a wreck, rebuilding on a slope |
| `autopilot/` | To the shop, holding a set speed, the trailhead to the Halfway Hut on the trail, cross-country to the lake, through the boulder lane, following a truck; and what it sees: a boulder dropped on its route (around it), a knee-high rock in a walled lane (over it), a 0.38 m ledge (stops, 4L, locker, climbs), a wall with a gap, a parked truck, the Civic avoiding a rock a truck would straddle |
| `surfaces/` | The side-by-side flat out through mud and sand; stopping from 40 km/h on pavement, wet, gravel, sand, mud, snow and ice; locked wheels vs ABS on pavement and gravel; sand sinking, floating aired down and digging in; all-terrains vs semi-slicks in mud; ice; the autopilot on sand and ice |
| `freeway/` | Top speed per vehicle (flat out until it stops gaining; holds its lane, nothing scrapes), and seamless 400 m wraps at 200 km/h |
| `handling/` | Keyboard steering at 130 km/h: a tap is a nudge, a hold is a hard sweeper without a spin (each per vehicle) |
| `gamepad/` | The controller logic with synthetic pad input: deadzone and curves, the speed-sensitive steering ratio, trigger pedals and reverse, the button bindings, rumble, a Bronco's lane change at 80 km/h and a 4L crawl on the pad |
| `net/` | A host and a client over loopback: each sees the other's truck (node for node), a strap between them tows, leaving cleans up |
| `ai/` | Laps (all vehicles and both directions), following, oncoming traffic, passing an obstacle, getting back on the road, getting unstuck, 8-car traffic |
| `engine/` | Determinism, timestep convergence, sleeping, performance |

## Controls

| Key | Action |
| --- | --- |
| Esc | Pause menu: resume, pick a car, garage, settings, controls, reset, quit |
| W / S or ↑ / ↓ | Throttle / brake (hold S when stopped to reverse) |
| A / D or ← / → | Steer. Keys are all or nothing, so full steer is as much as the tires can hold at your speed, on that surface (full lock when parking, a few degrees at 130 km/h), plus room to catch a slide |
| Space | Handbrake |
| 1 2 3 4 5 6 7 | Become the Civic / Jesko / Bronco / 4Runner / rock buggy / side-by-side / Tacoma (repaired, in place) |
| G | Garage: W/S pick engine, suspension or transmission, A/D change the part, Enter fits it |
| Q / E | Shift down / up (switches to manual) |
| M | Manual / automatic gearbox |
| Shift+L | Headlights: AUTO (on at dusk) / LOW BEAM / HIGH BEAM / OFF |
| L | Transfer case (4x4s): Bronco 2H / 4A / 4H / 4L; 4Runner 2H / 4H / 4H LOCK / 4L LOCK; rock buggy 2H / 4H / 4L; side-by-side 2WD / 4WD. Into or out of low range under 8 km/h |
| F / B | Front / rear locker (4x4s; the front one needs 4WD engaged; the Bronco's front and the 4Runner's factory rear only work in low range; the rock buggy has both, the side-by-side only the front) |
| Y | Front sway bar disconnect (Bronco: a dash button under 30 km/h, reconnects itself above; 4Runner: quick-disconnect pins, stopped) |
| 9 / 0 | Hold to let air out of / pump up the tires (stopped). 35 psi street, 15–18 for rock |
| V | Winch: pick an anchor (every tree, boulder or truck within the 60 m line is marked, plus a ground anchor straight ahead and wherever the camera looks; drag the camera to aim, wheel or Tab for the next, V or click to hook, Esc to cancel). With a line on, V unhooks it |
| Z / Shift+Z | Winch in / pay out (- and = set the winch speed, 10-100%) |
| O | Tow strap: hook to the nearest truck within 9 m, or unhook |
| I | Spawn a parked vehicle ahead of you |
| K | Spawn AI traffic on the loop road |
| U | Autopilot on/off: drives to the place picked in Esc → Autopilot |
| Tab | Switch to another vehicle |
| Delete | Remove the vehicle you're in |
| R | Repair in place. R twice quickly: respawn at the start (in a level: restart the attempt) |
| T | Real recovery: back on your wheels where you are, damage stays. Arcade: repaired, upright |
| C | Camera: chase / orbit / cockpit / drone (WASD fly, Q/E down/up, drag look; the truck stays put) |
| F10 | Photo mode: hide the HUD |
| Mouse drag, wheel | Look around (drag up past level to look up at the sky), zoom |
| N | Beam view: x-ray (colored by part: frame orange, engine yellow, suspension cyan, springs magenta, steering blue, shafts white, panels pale) / beams only (colored by strain) |
| [ and ] | Slow motion down / up |
| P | Pause |
| X | Traction control on/off |
| F11 | Fullscreen |
| H | Controls overlay |
| F3 | Show FPS |
| F5 | Load the new build when the HUD says one is ready (see Build and run) |
| J | Expand the checks list while a scenario plays |

## Controller

Xbox, PlayStation and generic pads work through SDL2's game controller API: plug one in (or unplug it) at any time and the game says `CONTROLLER CONNECTED: <name>`. The HUD hints show the pad's button names (A / CROSS and so on) while it was the last thing you used; touch the keyboard or mouse and they go back. `--no-pad` (or `CZ_NO_PAD=1`) leaves controllers alone. The pad does nothing while the game window doesn't have the focus.

Driving is analog. The left stick is a real analog steering input: a radial deadzone (0.12 by default), a response curve, and a speed-sensitive ratio. Full stick is what the tires can hold at that speed (the same limit the keys use, a little more room), so it is full lock when parking or crawling and a few degrees at 130 km/h; half a stick is half of that. RT is the throttle and LT the brake, both analog; LT at a standstill puts the car in reverse (RT while rolling backwards brakes, then drives forward), exactly as holding S does.

| Button | Action |
| --- | --- |
| Left stick | Steer |
| RT / LT | Throttle / brake (LT at a stop: reverse) |
| A or B | Handbrake (hold) |
| X / Y | Shift down / up (switches to manual) |
| Right stick | Look around (the chase camera recentres on its own) |
| R3 (hold) | Look back; let go to reset the view |
| RB | Camera: chase / orbit / cockpit / drone |
| D-pad up | 4x4 mode (L) |
| D-pad left / right | Front / rear locker |
| D-pad down | Front sway bar disconnect |
| L3 | Headlights |
| Back (View) | The map: sticks pan, RT / LT zoom, A sets the waypoint, Back or B closes |
| Start (Menu) | Pause menu |
| LB + D-pad up / down | Winch: hook (or unhook) / tow strap |
| LB + RT / LT | Winch in / out (right stick up and down sets the winch speed) |
| LB + D-pad left / right | Air down / pump up the tires (hold, stopped) |
| LB + A | Repair in place (twice quickly: back to the start) |
| LB + B | Back on its wheels |
| LB + X | Traction control on/off |
| LB + Y | Get out / get in |
| LB + RB | Manual / automatic gearbox |
| LB + L3 | Autopilot on/off |
| LB + Back | Next vehicle |
| LB + Start | The controls list (shows the pad's map) |

Picking a winch anchor: right stick aims, D-pad left/right or LB/RB step through the anchors, A hooks, B cancels. Menus, the title screen, the level list and the garage: D-pad or left stick move (hold to repeat), A selects, B or Start goes back. On foot the left stick walks, A jumps, and the right stick looks; Y with LB gets back in. The drone camera flies with the left stick and looks with the right.

**Rumble** (settings, CONTROLLER page: OFF / LOW / HIGH): a thump on impacts, a rumble over rough ground, a buzz from tires sliding and from the engine on its limiter. It only runs while the pad is the last thing you used and the window has the focus. The same page sets STEERING (SOFT / NORMAL / SHARP: how the stick maps to the wheels) and STICK DEADZONE (SMALL / MEDIUM / LARGE). Multiplayer needs nothing: each machine reads its own pad.

## Main menu and levels

Launched plainly, the game opens on the title screen: free drive (pick map and vehicle), levels, multiplayer, world editor, garage, settings. Levels are races (laps through gates) and sprints (start to finish through gates) with medal times, damage/time limits and a no-repairs rule. Seven come with the game (Frontier GP, The Wall, Mesa Climb, Snake River Ford, Summit Run, Widowmaker, Boulder Gully); your own are `.czl` files in `~/.config/crumplezone/levels` or `./levels`. `--level NAME` starts one. In multiplayer the host picks the level and everyone gets it (edits and all), starts together, and sees a live leaderboard.

## Playing together

- **Map (M):** the whole map, north up: trails, water, repair shops, places, everyone's trucks. Wheel zooms, drag pans, left click sets your waypoint, right click pings GO HERE for everyone (Shift: HELP). Shift+M is now manual/automatic.
- **Pings:** middle click pings GO HERE where you point; Shift+middle click pings HELP at your truck. Everyone sees them in the world and on the map.
- **Chat:** Enter, type, Enter. **Meet up** (Esc → Multiplayer) parks your truck behind a buddy's (again for the next one). Name tags float over your buddies' trucks (and over them on foot).
- **On foot (J):** get out (stopped) to spot a line or walk the trail. You see through your own eyes: the mouse looks around, WASD walk where you look, Shift run, Space jump; you wade, swim, and climb over rocks. V hooks your truck's winch to the tree, rock or truck within arm's reach, or anchors the line right where you stand (up to 60 m from the truck); F pours your truck's spare can into the nearest truck (a buddy's too). J again by a truck gets in.
- **Exploring:** every named place on a map starts undiscovered ("?" on the map); get within 30 m to find it. Your discoveries are kept between sessions.
- **Fuel:** engines burn fuel for the power they make (setting FUEL: OFF / REAL / FAST, default FAST: 4x). Run dry and the engine dies. Shops fill the tank and the spare can; the Bronco and 4Runner carry a 20 L can.

## World editor

Build your own worlds and levels: title screen → WORLD EDITOR (a new level on the map picked), Levels → OPEN IN THE EDITOR (the game's levels open as your copy), or F2 while driving. `--editor` opens it on launch.

- **Fly:** WASD, Q/E down/up (Space up), right-drag to look, Shift fast, wheel to fly forward.
- **1 Sculpt:** raise, lower, smooth, flatten (to the height you click) with a round brush; wheel or [ ] sizes it, Shift is fine.
- **2 Paint:** grass, dirt, mud, sand, desert sand, gravel, rock, red rock, scree, snow, ice, concrete, tarmac, wet tarmac. It drives like it looks.
- **3 Water:** click in a hollow to fill it (dig a basin first); Shift+click dries.
- **4 Objects:** rocks, ramps/blocks (tilt them; rock slabs grip like rock), logs, trees, repair shops. Click to place, click one to select and drag it, Q/E turn, Delete removes (the map's own objects too).
- **5 Course:** the first click sets the start (facing the way you look), then each click adds a gate; drag to move, Q/E turn, [ ] width; the last gate is the finish.
- **6 Settings:** name, mode (free drive / race / sprint), laps, vehicle, time of day, repairs, damage and time limits, medal times.
- Ctrl+Z / Ctrl+Y undo and redo, Ctrl+S saves to `~/.config/crumplezone/levels`, F5 plays it (F2 brings you back), Esc leaves. Offline only.

## Pause menu and settings

Press Esc to pause and open the menu. Use the mouse, or W/S to select, A/D to
change and Enter to confirm. Esc goes back.

- **Vehicle:** pick any of the seven vehicles, then press Enter (or click) to drive it.
- **Settings:**
  - Game: speed in km/h or mph, time of day (CYCLE: dawn, day, dusk and night go round, a whole day in 6 to 96 real minutes, 24 by default; or held at MORNING, NOON, EVENING or NIGHT; the clock carries on from where you left it), volume, music volume (default 40%, 0 turns it off; it sits under the master volume), field of view.
  - Graphics: a quality preset (low / medium / high / ultra), shadows, anti-aliasing (MSAA), ambient occlusion, bloom, clouds, grass, scanned scenery (photo-scanned ground and props: off / low / high / ultra), render scale (50–200%).
  - Display: fullscreen, vsync, FPS counter.

Settings are saved to `~/.config/crumplezone/settings.txt`.

## Vehicles

All seven are built from short side-profile maps plus a parts list in
`src/vehicles.cpp` (where every heavy item sits, the engine's size and position,
the suspension type and its ride frequency), so adding a new one is mostly a
matter of drawing it in text.

| Vehicle | Drive | Weight (front/rear) | Engine | Suspension front / rear |
| --- | --- | --- | --- | --- |
| Honda Civic Si (2022) | Front-wheel, helical LSD | 1,318 kg (60/40) | 1.5 L turbo four, 6-speed manual | MacPherson strut / multi-link |
| Koenigsegg Jesko Attack | Rear-wheel, mid-engine | 1,420 kg (42/58) | 5.0 L twin-turbo V8, 9-speed | Double wishbone / double wishbone |
| Ford Bronco 4-door | 4x4 with low range | 2,214 kg (56/44) | 2.7 L twin-turbo V6, 10-speed auto | Double wishbone / solid axle |
| Toyota 4Runner (2002 Limited, built) | 4x4 with low range | 2,100 kg (54/46) | Supercharged 3.4 L V6, 4-speed auto | Double wishbone / solid axle |
| Rock buggy (Ultra4 class) | 4x4, 4.3:1 low range, both lockers | ~1,820 kg (55/45), estimate | 6.2 L V8 (450 hp), 4-speed auto | Solid axle, 4-link / solid axle, 4-link; 16-17 in of travel |
| Turbo side-by-side (RZR / Maverick class) | 4WD, front diff lock | ~800 kg (44/56), estimate | 900 cc turbo triple (195 hp), CVT (5 steps) | Double wishbone / double wishbone; 16-18 in of travel |

The **rock buggy** and the **side-by-side** are the two crawl and rough-ground specials (keys 5 and 6). Both are tube-frame vehicles: the lattice is the chassis and cage and isn't drawn; the roll cage, frame, bumpers, seats and exhaust are tubes and parts bound to it, and only a nose cone and tail (buggy) or hood, fenders and tail (side-by-side) carry skin. They have no doors, glass or covers.

| | Rock buggy | Turbo side-by-side |
| --- | --- | --- |
| Class figures | An Ultra4 / King of the Hammers car: LS3-class V8 (crate rating 430 hp at 5,900 rpm, 424 lb-ft; these run ~450 hp), 4L80E 4-speed auto behind an Atlas-style two-speed case (low 4.3:1), 5.38 axles | A Can-Am Maverick X3 Turbo RR / Polaris RZR Turbo S: a 900 cc Rotax-class turbo three (195 hp, ~85 mph limiter), CVT |
| Size | 4.70 x 2.22 m over the tires (1.88 m track), 2.90 m wheelbase, 0.62 m under the frame, 1.70 m to the cage top | 3.35 x 1.70 m (1.46 m track), 2.60 m wheelbase, 0.40 m under the skid plate |
| Tires | 40x13.5-17 mud-terrain on beadlocks, 30 psi (the game airs down to 12; a soft, sticky compound on rock) | 32x10R15 all-terrain, 14 psi |
| Suspension | Solid axles on long four-links, 0.41 m front and 0.44 m rear wheel travel, no sway bars; the front axle steers on kingpins | Double wishbones all round, 0.41 m front and 0.46 m rear travel, light bars |
| Drive | 2H / 4H / 4L; selectable front and rear lockers | 2WD / 4WD (center locked); front diff lock |
| Numbers | 0-100 km/h ~5.2 s (4H), limited to 150 km/h | 0-100 km/h ~4.0 s, limited to 130 km/h |

Weights, travel, and the Atlas ratio are class estimates, not one builder's car (marked in `src/vehicles.cpp`). Measured against the Bronco in the tests: the buggy climbs a 0.65 m square ledge with room to spare (about 0.7 is its limit; the Bronco 0.45 m, the side-by-side 0.3 m), a 48 degree slickrock slab (the Bronco slides back), and walks through the twister with open diffs where the Bronco bogs down; ramp travel index ~960 against the Bronco's 580. The side-by-side is quickest through the whoops lane (53 km/h average at a 55 km/h cruise, level, undamaged) and 20% faster than the Bronco across 50 m of mud or sand. At that pace the Bronco, 4Runner and buggy slow, stall or roll.
| Toyota Tacoma TRD Off-Road (2019 Double Cab) | 4x4 with low range | 2,040 kg (55/45) | 3.5 L V6, 6-speed auto | Double wishbone / solid axle on leaf springs |

The 4Runner is one particular truck, the Bring a Trailer car from December 2021:
black over silver, 2 in body lift and Old Man Emu coils on 35x12.50R17 KO2s and
bronze wheels, steel bumpers with a winch and swing-out spare, rock sliders,
snorkel, roof basket and a Magnuson supercharger. Its garage "AS BUILT" options are
that truck; the others put parts back to factory or take it further.

The Tacoma is a stock 3rd-gen TRD Off-Road: cab and open bed as separate boxes on one
long frame, Bilstein shocks, a rear e-locker that only works in 4L, a skid plate and
all-terrains. Its garage takes it to a TRD Pro, a long-travel build or a 6-speed manual.

## How a vehicle is built

- **Body structure.** A lattice of nodes and beams that is held in shape by a stiff
  chassis frame. It doesn't flex or shake when you drive, but it crumples in a crash:
  where the metal bends for good, the frame lets go and takes on the new shape.
  The floor rails, sills and crossmembers are stronger members, and every point
  something bolts to (spring towers, arm and engine mounts) is reinforced.
- **Ladder frames (Bronco, 4Runner, Tacoma).** Two boxed steel rails with crossmembers
  carry everything: the suspension brackets, the engine mounts and the body, which sits
  on rubber body mounts and is only a skin by comparison. Each rail holds its shape
  like real steel: springy up to its yield point, then it kinks and keeps the kink
  (a whole rail section bends together, it doesn't fold at one node). A press midway
  between the axles sets the Bronco's boxed frame at ~88 kN, the 4Runner's at ~53 and
  the Tacoma's (longer, open C-channel under the bed) at ~39. Bare, the frames twist
  11, 3.6 and 2.9 kNm per degree; bolted to their bodies, 25, 15 and 23. A suspension
  bracket breaks loose at ~70 kN, so a hard hit tears a bracket before it folds the rail.
- **Parts that touch.** The tires collide with their own body and the frame rails:
  a wheel pushed up past its travel dents the fender liner and the panels above it,
  a bent arm can drive the tire into the door, and a wheel knocked inboard meets the
  rail. Each wheel well knows what tire it was built for (and what size fits before
  it rubs, ~35s on the Bronco, ~32s on the Tacoma), so only tires bigger than that or
  wheels knocked out of place scrape. A loose panel can swing out but not in: an
  unlatched door opens to ~70° on its check strap and stops there, a hood to ~80°, and
  none of them pass into the cabin or the engine bay (flung open hard, the strap
  tears). Anti-roll bars, arms and links bend and keep the bend; engine and body
  mounts squash onto rubber snubbers and tear or take a set beyond them.
- **Weight.** Every heavy item is placed where it really sits: engine, gearbox, seats,
  dashboard, fuel tank, battery, radiator, exhaust, spare wheel. That sets the
  front/rear balance, the center of gravity height and how the car pitches and rolls.
- **Engine and gearbox.** One heavy block on rubber mounts. It rocks under throttle,
  pushes back on the body and the differential (squat under power, axle wind-up on
  the live axles), and in a big crash it's shoved back and can tear off its mounts.
- **Suspension.** Real linkages:
  - Double wishbones with ball joints, a coil-over and a tie rod (or toe link).
  - A twist beam on the hatchback.
    - On the rock buggy, live axles front and rear on long four-links: the front one steers, with knuckles turning on kingpins at the tube ends and tie rods from the axle.
  - On the Bronco and 4Runner, a live axle on trailing links with a panhard rod.
  - On the Tacoma, a live axle on leaf springs: a fixed front hanger, a swinging
    shackle behind, inter-leaf friction that firms the ride over small bumps, and
    axle wrap (the housing twists the springs under torque until they hold it).
  - Anti-roll bars.

  Spring and damper rates come from each vehicle's ride frequency and the actual
  motion ratio of its linkage. Dampers are firmer in rebound and give way on sharp
  hits. Bump stops are progressive rubber. The geometry has anti-dive and almost no
  bump steer.
- **Steering.** A rack moves the tie rods and the knuckles turn on their kingpins,
  with Ackermann geometry. A hard knock can bend a tie rod, and the car then pulls.
- **Driveline.** Prop shafts and half shafts. If one is torn off, the wheels it drives
  lose power.
- **Underbody.** A solid axle's diff and tubes hit the ground and the rocks (the diff is
  the lowest thing on the truck: hang it on a rock and the axle's held up), and on a 4x4
  the transfer case's skid plate hangs under the frame between the axles, where a ridge
  high-centres it. Clearance and angles come out at the real trucks' (the Tacoma's 9.4 in,
  32° approach, 23.5° departure, 21° breakover: `structure/clearance`).
- **Panels.** The hood, doors, bumpers and trunk lid (rear doors on the van) are
  separate sheets pinned on with hinges, latches and clips. In a crash, clips and
  latches break: doors swing open on their hinges, hoods pop up, and bumpers fall off.
- **Brakes and tires.** Per-wheel ABS, brake pressure that takes a moment to build,
  brake torque that reacts through the knuckles (nose dive), rolling resistance, and
  tire forces that act at the contact patch (which is what rolls the body).
- **Stability control.** Above ~10 km/h, if the truck yaws faster than the steering
  asks (or its rear slides), it brakes the outside front wheel and trims the
  throttle, as real ESC does. It's off in low range and with traction control off.

## Damage

- **Bodywork** dents permanently and tears apart. Bent metal gets stiffer as it crumples, and crushed metal barely springs back, so a 50 km/h wall hit gives about 0.3–0.5 m of crush and a small rebound, like a real crash test.
- **Panels** come loose: bumpers drop off, hoods pop open, doors swing open to their check straps or come off.
- **Windows** shatter once their frame bends: the bonded windshield at about 5%, side and back glass (loose in rubber channels) at about 7.5%.
- **Tires** blow out on hard rim strikes, such as big landings or kerbs taken at speed. You then run on the rim with much less grip, and the next big hit lands on steel.
- **Engine:** a crushed engine bay loses power, smokes, and eventually dies. Torn engine mounts hurt it too.
- **Suspension and steering** bend: a bent tie rod or control arm knocks the wheel out of line, and the car pulls.
- **Drive shafts** snap, and the wheels they drive lose power.
- **Wheels** come off when their suspension mounts break.

## 4x4

The Bronco, the 4Runner, the Tacoma, the rock buggy and the side-by-side have real transfer cases and selectable lockers (the side-by-side has a front diff lock). The HUD shows a small driveline next to the gauges: wheels light up when they're driven, the prop shaft is solid when front and rear are tied together, and a red block marks a locked axle.

| Truck | Modes (L) | Lockers |
| --- | --- | --- |
| Bronco (part-time, advanced 4x4) | 2H; 4A (all four driven, center free, fine on pavement); 4H and 4L (front and rear shafts locked together) | rear e-locker; front too with the garage's 4.70 AND LOCKERS (Sasquatch) |
| 4Runner (full-time multi-mode) | 2H; 4H (center diff open); 4H LOCK; 4L LOCK | rear e-locker; ARB front and rear with the garage's locker options |
| Rock buggy (Atlas-style two-speed case) | 2H; 4H and 4L (shafts locked together; it leaves in 4H) | air lockers front and rear, in any range |
| Side-by-side (on-demand 4WD) | 2WD; 4WD (the clutch ties the shafts; no low range) | front diff lock (F); the rear is a limited-slip |
| Tacoma (part-time) | 2H; 4H and 4L (front and rear shafts locked together) | rear e-locker (4L only) |

- **Open diffs:** each axle splits torque equally, so a lifted wheel spins and the other gets almost nothing.
- **Lockers:** both wheels on the axle turn together.
- **4H and 4L:** front and rear turn together. On dry pavement this binds the driveline in tight turns, just like the real thing, so use 4A or 2H there.
- **Low range:** multiplies every gear (2.72:1 Bronco, 2.57:1 4Runner and Tacoma, 4.3:1 rock buggy: about a 57:1 crawl ratio in first).
- **Locker rules, as on the real trucks:** the Bronco's front locker only engages in 4L; the 4Runner's factory rear e-locker only in 4L LOCK, the Tacoma's rear e-locker only in 4L (the ARB lockers from the garage work in any mode). Leaving low range drops them.
- **Axle shafts break:** the shaft carries the engine's torque plus whatever a locker forces through it. Full throttle in low range with a locked axle and a wheel in the air, or a front locker at full lock, can snap a CV joint (IFS CVs take 65% of a solid-axle shaft, less at steep angles). That wheel freewheels until a repair station fixes it.
- **Sway bar disconnect (Y):** more articulation for crawling, more body roll on the road. Ramp travel index on a 20° ramp: Bronco ~550, ~610 disconnected; 4Runner ~520, ~570; Tacoma (no disconnect) ~470.
- **Tire pressure (9 / 0):** airing down softens the tire into a longer patch, about +1.2% grip per psi off pavement (15 psi climbs a 41° granite slab that 35 psi spins out on). The price: the sidewall rolls over in fast corners and the rim is closer to the rocks, so a fast hit pinches a tire. Shop air at a repair station pumps up fast.

## Autopilot

Esc → Autopilot picks where and how; U starts and stops it (steering or the pedals take over too).

- **Drive to:** the nearest repair station, any station, the trailhead, the summit lookout, the crawl park, the Widowmaker Spur, Boulder Gully, the lake (proving ground: spawn, crash wall, jumps, washboard), wherever the camera is looking, following any other vehicle (yours or a buddy's), or cruising the loop road with traffic.
- **Speed:** 5–100 km/h. It slows for bends, steep descents and rough ground, and stops at the end.
- **Route:** roads only, roads and trails (prefers them, cuts across when it pays), or anywhere it can get through.
- **Route:** A* over the map (2 m cells) from slope, side slope, surface, and what's on the ground. Rocks, logs and ledges cost in proportion to how much of what this vehicle can climb they take; anything taller than it can climb at its best (low range, lockers, aired down), trees, walls and deep water are off limits. So a Bronco's route crosses a 0.3 m rock the Civic's goes round.
- **What it sees:** ten times a second it scans the ground ahead: exact heights of the terrain, boulders, ledges, logs, trees, walls and other vehicles. It tries about 50 lines (hold a steering angle for a third, two thirds or all of the next 9–20 m, then head back to the route) and drives each one in its head a quarter meter at a time: where every tire sits and what step or drop it meets, whether the diffs, belly or bumpers touch, how far the truck leans and pitches. Lines that hit something, lean past a safe angle or need a ledge taller than the truck can climb are out. The rest are scored on how far they stray from the route, how slowly they'd have to be taken, and how rough they are. So it straddles a small rock, puts a tire up on a bigger one, and goes round one that would mean crawling or scraping if there's room.
- **What it can climb:** measured on square ledges: the Bronco climbs 0.15 m in 2H, 0.30 m in 4H and 0.45 m in 4L; the 4Runner 0.15 / 0.20 / 0.35 m; the rock buggy 0.7 m in 4L with lockers at 12 psi (its table has a 1.2x factor for 40 in tires and 16 in of travel); the side-by-side 0.3 m. Lockers and aired-down tires add a little. If it gets stuck on something it thought it could climb, it remembers that it can't.
- **Setup:** 2H on pavement, 4WD on dirt and gravel, the center locked on loose, steep, sandy, muddy or snowy ground, low range for ledges, rock and steep grades (it slows to shift into or out of low). Lockers go in for ledges, side hills and wheel spin and come out for tight turns (a locked front axle won't turn and snaps CVs) and above 25 km/h. The sway bar comes off for crawling. Before a long rocky or sandy stretch, or a ledge it can only climb aired down, it stops and airs the tires down to 20 psi, and it pumps them back up at a repair station. It eases off on low grip: slow on snow and mud, very slow on ice.
- **On screen:** the route on the ground, the line it's taking (green), the lines it tried (faint; red where they hit something), brackets on what it sees (ROCK 0.45M: OVER / AROUND LEFT / STRADDLE / BLOCKED), what it's doing ("LEDGE 0.38 M: CLIMBING IT"), and its setup (4L, lockers, sway bar, psi, how tall a ledge it can climb right now). It announces each shift and locker change.
- **Crawling:** on ledges in low range it keeps first gear and walks up to each one.
- **Stuck:**
  - Against a ledge on full tires, it stops and airs down to 20 psi, then tries again.
  - Otherwise it leans on the throttle, then backs off at a walk and tries again with more drive.
  - Stuck twice at the same spot, it plans a new way round.

## Multiplayer

Up to 8 players on one map. One of you hosts; the rest join.

- **From the menu:** Esc → Multiplayer. Type your name, then either Host a game (on the map you're on) or type the host's address and Join. Leave the session from the same page.
- **From the command line:**
  - Host: `./crumplezone --map 1 --host` (port 27015 by default, or `--host 27100`)
  - Join: `./crumplezone --join 192.168.1.20` (or `HOST:PORT`), with `--name YOURNAME` if you like
- **Over the internet**, the host needs TCP and UDP port 27015 forwarded on their router. On a LAN it just works.
- **How it plays:**
  - Your own truck runs on your machine, so driving has no lag. The others see it, dents and all, about 0.1 s behind (about 40 KB/s per moving truck).
  - Other players' trucks are solid: you can bump them, winch to them, and strap them (V, O). A line between two players' trucks pulls on each truck on its owner's machine, so a buddy can tow you to a repair station while you steer.
  - Name tags float over the others' trucks, the compass points to them, and the player list (top right) shows everyone's ping.
  - The host's parked trucks and crash props are shared. The host picks the map and the time of day (its clock goes out every second), and there's no pausing or slow motion while you're in a session.

## Recovery

The idea: trucks break, and your buddies get you home. (Until multiplayer lands, a buddy is any other truck: Tab into it and drive.)

- **Winch (V, Z, Shift+Z):** the Bronco, 4Runner and rock buggy carry a 9,500 lb winch, the side-by-side a 4,500 lb one (20 kN), each with 30 m of synthetic line and a 30 m extension in the recovery bag (60 m in all). V hooks the best anchor ahead: a tree, a boulder (0.45 m and up), another truck's recovery point, or, anywhere else, a ground anchor (dug into loose ground, or a bolt in bare rock). It reels in at ~0.13 m/s, slower under load, and stalls at its rated pull (the drum brake holds). The line snaps past ~90 kN.
- **Trail repairs:** stop within a few meters of another truck (yours, a parked one, or a buddy's) and its crew and kit fix what can be fixed out there: flat tires, snapped axle shafts and CVs, bent steering and suspension links, a flooded engine. Dents, panels, glass, a lost wheel or a wrecked engine still need a repair station.
- **Tow strap (O):** a 9 m kinetic strap between your nearest recovery point and the nearest other truck's. A truck on a strap rolls in neutral, brakes off, steering itself to follow. It breaks at 130 kN.
- The HUD (top left) shows line out, pull, and how close it is to breaking. The pull is spread through the frame, so recovering doesn't bend the truck.
- **Repair stations:** park on the pad and stop. The crew fixes everything (dents, panels, glass, tires, shafts, engine, hydrolock) in 4 s plus time per thing broken; moving resets the work. Towed trucks get fixed too, and the strap comes off.
  - Mountain: Trailhead Garage (in the lot), Crawl Park Pit (by the dome), Halfway Hut (a pull-off on the summit trail, where the Widowmaker Spur leaves it).
  - Backcountry: Trailhead Camp, Sawmill Camp, Far Side Camp and Pass Hut.
  - Proving ground: the shop beside the spawn.
- **Real or arcade (Settings → Recovery):** in Real (the default) damage stays until a station fixes it: T only puts you back on your wheels and R tows you to the start as you are. Arcade keeps the old instant repair.
- **Compass (top):** headings, the nearest repair station and the summit, with distances.

## Maps

Pick the map in the pause menu (Esc). The game remembers it. From the command line: `--map 0` proving ground, `1` mountain, `2` freeway, `3` Frontier, `4` Backcountry.

### Backcountry

Forested hill country for crawling, mudding and exploring with buddies. You start at **Trailhead Camp** in the south and a network of trails (about 4 km in all) radiates from it, each its own challenge and rated on its signs: green easy, blue, black hard, red extreme. Elk River runs east to west across the middle, and the trails loop so a group can split up and meet again: Mud Run → Sawmill → ford → Far Side Camp on one side, Creek Crawl → mine → Slickrock Ridge → deep crossing → Far Side Camp on the other. Fuel is something to plan for (the camps have the shops).

- **The Mud Run (black, 580 m):** a forest track east of camp to Sawmill Camp, worn into deep ruts, with five mud stretches and four **bog holes** (0.4–0.6 m of water over a mud bottom, wider than the trail so there's no way round). A 2WD car stalls in the first mud; a 4x4 in low range with lockers, aired down, wades through all four.
- **Creek Crawl (red, 375 m):** up a gully with a trickle of water over its bed: boulder gardens and eight rock ledges from 0.2 m to 0.45 m (the same as the autopilot's best-set-up climb), to **the old mine** (portal, ore cart on rails, tailings, office). A Civic stops at the first boulders; a Bronco in 4L with lockers crawls the lot.
- **Slickrock Ridge (black, 440 m):** from the mine along a bare rock spine over **Slab Point** (a lookout with a cairn and a bench), with off-camber slabs (the tread leans toward the drop, one way and then the other) and three rock drops, steeply down to Elk River.
- **Deep crossing (black):** the ridge wades the river across a ~0.9 m pool with a current of about 1.2 m/s. The 4Runner's snorkel makes it; a Civic's engine drowns. It comes out on the way to Far Side Camp.
- **Ridge Road (green, 230 m) and the ford:** Sawmill Camp to Far Side Camp, across the **ford** (0.4 m of gravel, a current of about 0.9 m/s: a truck wades it at walking pace).
- **Heartbreak Hill (red, 175 m):** from Sawmill Camp straight over the hill beside the Ridge Road: loose gravel up a ~30% face, rocks in the tread, and down the far side.
- **Sand Wash (blue, 520 m):** east from Sawmill Camp down a dry wash (soft sand, boulders along it) to a **dune bowl**, once round it on the ring trail, and out to the Mud Run. The tires sink: air down.
- **Shelf Road (black, 270 m):** from Far Side Camp north along the rim of a canyon: a one-truck shelf with a sheer 15–25 m drop on one side (orange posts mark the edge; go over it and you land on the canyon floor), the **Cliff Overlook** pull-out, and a trail map. **Wreck Spur (green, 200 m)** runs up the canyon floor beside a stream to **the wreck site**, an old pickup on its side.
- **High Pass (blue, 435 m):** the shelf road goes on up three switchbacks to **Pass Hut** (a repair shed and a cabin at 80 m), through snow: packed snow with two patches of black ice and a frozen pond by the hut.
- **Tower Road (green, 670 m):** from the hut down the east side to the **fire tower** (a four-legged tower on a hairpin) and on to the ford. The **cabin spur** (175 m) leaves it for **Trapper's Cabin** on the river.
- **Repair stations:** Trailhead Camp (it has a wash-down bay), Sawmill Camp (old mill, a stack of logs), Far Side Camp, and Pass Hut.
- **Places to find:** 16 named places besides the camps (the autopilot menu lists them): Creek Crawl foot, the old mine, Slab Point, the deep crossing, the ford, Heartbreak Hill, the Mud Run's start and its third bog (THE SINK), the sand wash, the dune bowl, the shelf road, Cliff Overlook, the wreck site, Snow Corner, the fire tower, Trapper's Cabin.
- **Water and land:** Elk River and the creeks run with a current on the height grid; trees (pine, broadleaf, snags) thicken away from the river, boulders and logs lie about, and every surface type is somewhere on the map.
- **In multiplayer:** the Mud Run bogs and the Creek Crawl are made for winching each other out. Jobs and scouting runs use the places above.
- Music: the mountain's airy folk.

### Frontier

Everything in one 1 km map. You start in the paddock in the middle.

- **Race circuit:** 1.4 km of pavement with kerbs: a 400 m straight, sweepers of 85-120 m radius and one 20 m hairpin; a wet skidpan in the paddock. The HUD times laps from the checkered line; leave the circuit and the lap doesn't count.
- **Rally stage:** a 4 km loop of gravel, sand, mud and dirt that links everything. It runs from the paddock through the desert and the foothill forest, crosses both rivers twice at shallow gravel fords, follows the lakeshore and passes a mud bog in the south meadows.
- **Desert (west):**
  - wind-rippled dunes of soft sand and cracked hardpan
  - a red-rock mesa with **MESA CLIMB**, fourteen 0.32 m ledges up its east face
  - a butte, a slickrock bowl, and a dry wash choked with boulders
- **THE WALL:** twelve rock ledges up a gully lined with boulders, off the rally stage in the western hills, to a lookout. Climb it in 4L, aired down.
- **The range (north):**
  - pines up to the treeline
  - a switchback trail about 140 m up to the summit, through a boulder garden and scree
  - snow on top, and a frozen tarn under the summit
- **Water:**
  - **Snake River** comes down out of the range into **Echo Lake**, which has a sandy beach.
  - A muddier river drains the lake west across the south and spreads into a mud delta.
  - Both rivers have current. Deep water floats a car off downstream and floods its engine. The fords are 0.4–0.5 m deep, which a truck can wade at walking pace.
- **Blackwater Bayou (south-east, black):** a swamp in the lowlands below Echo Lake, about 300 × 170 m of mud flats, grassy hummocks, still brown water a hand or two deep, drowned snags, reeds, fallen logs and stumps. A 740 m mud trail marked with orange-topped posts leaves the rally stage just south of the third ford. It runs east along the north side, round the far end, back west along the south side, and out through the old bog onto the stage again.
  - **On the way:** a corduroy of logs over the first wet stretch, a churned trough with mud waves, and 15–20 puddles from 4 cm to 30 cm deep.
  - **THE WALLOW** (north leg) and **THE SINK** (south leg): pits wider than the trail, 0.4 and 0.5 m of water over a mud bottom, with a 20–30% slimy climb out. A Civic drops in and can't get out. A Bronco in 4L with lockers, aired down, wades through. Each one has a black sign before it.
  - **MUD HILL (red):** an island in the middle, 7 m high. Its climb cuts straight across the loop from the north leg to the south leg: up a mud face of ~20% (30% at the steepest), over the top, and down the far side.
- **Cold Creek (north-east, blue):** a glacial valley cut into the range east of Snake River, under snow.
  - **The snow road** leaves the stage at its north-east corner. It's a dirt track to the snowline, then packed snow up the valley floor at 8% at most, with ploughed banks, wheel tracks and orange snow poles. On the way it crosses the frozen creek on the ice and has two patches of black ice.
  - **The valley:** 80 m wide between rock-banded walls, with snowfields drifted into long, low waves and groves of snow-laden pines. A frozen pond sits on the east side. **Cold Creek Lodge** at the head has a log cabin and a repair shed.
  - The snow is real snow (grip about a third of pavement, and the tires sink in), and the ice is ice. A 4x4 drives up the road at 30 km/h; take it easy on the ice.
- **Repair stations:** in the paddock, at the desert outpost, at the lakeside garage, at the mountain hut, and at Cold Creek Lodge.
- **Surfaces:** every surface type is somewhere on the map.

The autopilot (Esc → Autopilot) can drive you to the paddock, the mesa climb, the slickrock bowl, THE WALL, the summit, the tarn, the Snake River ford, the beach, the bog, the delta, Blackwater Bayou, the edge of THE WALLOW, the top of MUD HILL, Cold Creek valley or Cold Creek Lodge.

### Mountain

You start at a gravel trailhead in a valley under a 210 m massif.

- **Summit trail:** a dirt track that spirals 1.3 km round the massif to a lookout at about 140 m. Grades reach 24%, there are rocks along the edges and in the tread, and it fords a dry rocky creek near the bottom.
- **Crawl park** (west of the lot): side-by-side lanes:
  - **Twister:** offset mounds that lift diagonal wheels. Open diffs stall here; lockers walk through.
  - **Boulder field.**
  - **Rock ledges:** 0.3 m steps up to 1.5 m and back down.
  - **Logs:** they get thicker as you go, and they're slippery.
  - **V-notch:** big rocks either side of a trench.
  - **Slickrock dome:** grippy granite, about 30 degrees.
- **Whoops lane:** east of the lot.
- **Valley loop:** a gravel road round the valley. AI traffic drives it (K).
- **Scenery:**
  - pine forest up to the treeline
  - broadleaf trees, bushes and grass in the valley
  - boulders everywhere
  - bare rock and snow up top

- **Lake:** east of the trailhead, with reeds, picnic tables and a fire ring. Wade in carefully: water above the air intake hydrolocks the engine (the 4Runner's snorkel takes its air at roof height). The proving ground has a pond out in the hills.
- **Widowmaker Spur (black):** a knife-edge ridge leaving the summit trail on the east face (~55 m up) and running 160 m out to a pinnacle. It holds its height while the mountain falls away, so the drops either side grow to 40–60 m. On the way out: a loose scree descent, a 3.2 m knife edge, an off-camber slab climb, two rock ledges, another off-camber stretch, scree with boulders, and the pinnacle to turn round on. Reflector posts mark the edges; go over one and you tumble all the way down.
- **Boulder Gully (red):** a ravine up the west peak, reached by a track from the crawl park's north edge across the creek. It's 110 m long and about 30 m high, choked with boulders (the big ones toward the walls), with three rock ledges and two loose scree pitches at up to 40%. It needs both lockers.
- **Scree:** loose broken stone (grip 0.6) on stretches of the upper summit trail, the spur and the gully.
- **Trailhead:** a split-rail fence, a log cabin, a trail map, and signs at each crawl lane rated green, blue, black or red.
- **Rocks:** irregular faceted boulders, outcrops on the slopes and scree up top. Each one collides exactly as drawn.
- **The woods:** fallen logs and stumps, which are solid.
- **The sky:** cloud shadows drift over the land.

- **Mud** in the middle of the creek bed (the ford is packed gravel), a **sandy beach** round the lake, and **snow** on the gentler ground above the lookout.

### Freeway

A straight six-lane divided freeway across farmland that never ends, for top speed: three 3.6 m lanes each way, a concrete median, guardrails, lamp posts every 50 m, sign gantries, and a rest area with a repair shop (off the eastbound side, down a gravel slip road) every 400 m. Start in the middle eastbound lane. The HUD shows how far you've driven and your best speed.

It never ends because everything repeats every 400 m, and whenever you get 200 m from the middle, the sim moves every vehicle, line and route back 400 m onto the identical stretch. Physics and the view can't tell (tested: no change in speed, lane or the gap to a car behind). Not in multiplayer: there it's just the 1.2 km of the map.

Top speeds on it (`freeway/top-speed`): Jesko ~413 km/h (drag-limited, 9th gear, after ~7 km), Civic Si ~200 km/h, Bronco 161 km/h, 4Runner 171 km/h and Tacoma ~180 km/h (governed, like the real ones).

### Proving ground

**The grid test pad** has:

- a concrete crash wall about 115 m ahead of spawn
- a kicker and landing ramp
- a row of poles
- a stack of foam crash boxes
- a line of concrete barriers
- a washboard of bumps
- a banked ramp
- an 8 m platform to drive off

**The surface course**, north-west of the pad: six 70 m strips side by side (wet asphalt, gravel, sand, mud, snow, ice) on a concrete apron, to feel the difference. Spawn on it with `--spawn -112,94,0` (sand; the strips are 9 m apart from z = 76).

**A link road** leaves the north side of the pad (+Z, to your right at spawn).
It joins a two-lane loop road that winds through wooded hills. Grass has less
grip than pavement.

## Surfaces and tires

Each surface has its own grip and its own way of letting go, and each tire tread gets on with each surface differently.

| Surface | Grip | How it behaves |
| --- | --- | --- |
| Pavement | 1.0 | Bites at a few degrees of slip, then lets go: a locked or spinning tire has ~70% left |
| Wet asphalt | 0.68 | Less grip, lets go sooner; past ~80 km/h the tires aquaplane (half the grip again at 140) |
| Rock (granite) | 1.0 | Like pavement |
| Dirt | 0.82 | Softer: grip builds more slowly with slip and stays when sliding |
| Grass | 0.72 | Soft, a little drag |
| Gravel | 0.68 | Loose: slides are progressive, and a locked wheel piles stones up in front and stops about as short as ABS (shorter, often) |
| Scree | 0.6 | Loose broken stone, rolls under the tread |
| Sand | 0.58 | The tires sink in (~3–6 cm, deeper at high pressure) and drag (rolling resistance ~10x pavement); spinning digs them down to 11 cm (and they stay down until you roll out); aired down they float (rolls 1.5x further). Keep your momentum. Soft to land on: see below |
| Mud | 0.4 | Slick, sinks deep, and packs into the tread as you go (the tire turns into a slick) until wheel speed flings it out. Mud-terrain lugs shed it best |
| Snow | 0.32 | Sinks a little, progressive slides, long stops |
| Ice | 0.1 | Almost nothing to steer or stop with, little rolling drag: stops take 8x as far as on pavement |

| Tread | Fitted to | Good at | Bad at |
| --- | --- | --- | --- |
| Street | Civic | Pavement | Mud, snow |
| Semi-slick (Cup 2 R) | Jesko | Pavement: stiff, bites at ~5° | Everything else: wet (and aquaplanes sooner), mud (it barely moves), snow, grass |
| All-terrain | Bronco, 4Runner | Dirt, gravel, sand, snow, mud | A little softer on pavement |
| Mud-terrain | 37 in tire options | Mud (sheds it), dirt | Ice and wet pavement |

ABS and traction control aim for the slip that grips best on that tread and surface (they let a tire dig more on loose ground). Traction control also stops engine braking from dragging the driven wheels into a slide on ice and snow (engine drag control). The HUD shows what you're on, your tread, and how far you've sunk or whether the tread's packed with mud. Tires throw what they're on (roost) and spray off wet asphalt.

**Soft landings.** Sand, mud and snow give under a hard landing. Past what the ground can bear under a tire (~120 kPa for loose sand, more the deeper it goes: estimates), the tire digs a crater instead of squashing onto its rim (up to 22 cm in sand, 20 in mud, 16 in snow), and the ground keeps the energy instead of bouncing it back. Body panels hitting it plough into a crush layer (a soft spring, packed hard 25–40 cm down) that doesn't push back as they lift out. A Bronco dropped flat from 5 m takes about a tenth of the damage in sand that it does on rock, with a third less peak deceleration; from 8 m rock pinches two tires on their rims and sand none; a jump landed 35° nose-down does far less (`surfaces/soft-landing`). It's still in a hole afterwards: ease out of it (floor it and the tires spin and dig in).

**Tracks and sinking.** Every tire on loose ground leaves a track the width of its tread, laid on the ground as a groove: a darker floor with the tread's lugs printed in it, walls shaded by the light, and in sand, mud and snow the ground it shoved aside banked up either side, as deep as the tire sank (deeper and darker where it spun). Fainter on dirt, gravel and grass; black rubber on pavement where a tire slides. Tracks are drawn multiplied into the ground under them, so they take its light: sun, shadow, headlamps, night. The last ~12000 segments stay (about 600 m behind each wheel of one truck; the oldest fade out), for every car including other players'. A tire sunk into sand, mud or snow sits down in a collar of the ground it pushed aside, so you can see how far it's in.

## Graphics

- **HDR pipeline:** the scene renders into a floating-point target (with MSAA), then goes through screen-space ambient occlusion, bloom and ACES tone mapping.
- **Sun (and moon) shadows:** two cascades, a sharp one around the car and a wide one out to the treeline, with soft filtered edges.
- **Day and night** (`src/daylight.cpp`): a clock runs the sun across the sky (38° north in early summer: up at about 5:20, down at about 18:40). The sky, ambient light, fog color and exposure follow it through golden hour, a red and orange sunset and the blue hour into night. At night the moon (just past full, low in the south, shaded by its phase) takes over as the key light and casts weaker, bluish shadows; stars fade in at dusk and wheel round the pole with the clock. Exposure opens up and a touch of blue night vision keeps the dark readable.
- **Sky:** the gradient with a glow round the sun, and drifting procedural clouds lit by the sun or moon, which also show in paint and glass reflections.
- **Headlights** (`src/lights.cpp`): every car's low or high beam is a real light: two spot lights per car with a flat low-beam cutoff, a hot spot and distance falloff, lighting the road, scenery, grass, water, dust and other cars (the nearest 8 cars with their lamps on). The lenses glow (and bloom), tail lamps light up with them and brighten when braking, reversing lamps come on in reverse. A lamp goes dark once the bodywork under it is crushed or the panel it's on is gone. Yours are on Shift+L (AUTO turns them on at dusk); AI, autopilot and other players' cars switch on at dusk, parked ones stay dark.
- **Atmosphere:** height fog that thins up the mountains.
- **Terrain:**
  - two-tone meadow with dry patches
  - rock with strata on steep ground
  - snow on high tops
  - craggy rim mountains, and a mountain range out to the horizon (visual only; the physics map is unchanged)
- **Photo-scanned ground** (`src/groundtex.cpp`, `src/groundshader.cpp`, `assets/ground/`): ten CC0 Poly Haven
  PBR sets (colour, normal, AO/roughness, height) in two texture arrays. Each 1 m cell's paint picks a mix of
  materials (mud, dirt, leaf litter, two grasses, gravel, pale and orange ground, rock, snow); neighbouring cells
  blend through a noise-wobbled, height-aware border (the knobbly material shows through first). Normal and ARM
  maps light it; a second, turned and rescaled sample and large patches of lighter and warmer ground break up the
  tiling; steep banks are mapped from the side (triplanar); detail fades out with distance. Paved ground, ice, the
  freeway and the race circuit keep the code-made look. Tire tracks, rut lips, puddles in mud, the wet shore,
  editor painting and night lighting all work on it. SCANNED SCENERY (settings) sets how many layers and taps it
  takes and the texture size; OFF is the code-made ground.
- **Scanned props** (`src/propdraw.cpp`, baked into `assets/props/` by `tools/bakeprops.cpp`): Poly Haven
  scans drawn instanced with their colour, normal and AO/roughness maps, lit and shadowed like everything else
  (they cast shadows too), each at one of three levels of detail by distance, culled to the view. Pines are
  fir and pine saplings scaled to the tree, snags and logs are dead trunks (a log fitted to its collider), bushes
  are shrubs, cliffs lie on the steep faces, and round the camera grass, ferns, stumps and branches grow off the
  trails, water and pads. Far trees are billboards baked from eight sides. Boulders are the scans too, and so is
  what you hit: `World::addRock` places the scan's convex hull (48 corners at most, in `rocks.hull`) scaled and
  turned at random, and the renderer draws the scan in the same frame. Without `assets/props` (or with SCANNED
  SCENERY off) the scenery is the code-made one, and so are the rock hulls without the bake.

## Vehicle visuals

**Body**
- The general body shell subdivides the physics skin twice (Catmull-Clark), with crisp shoulder and roof lines. Dedicated shells can replace it while following the same physics.
- The rock buggy and the side-by-side are tube-frame vehicles: their cage, chassis rails, bumpers, seats, harnesses, exhaust, winch and light bars are tubes and small parts bound to the physics lattice (so a hit bends them with it), and only the nose cone and tail (buggy) or hood, fenders and tail (side-by-side) carry body skin. The exposed V8 and the four-link axles show.
- The Jesko has a dedicated curved hood, canopy, rear haunches and open wheel arches, with swept lamps, side intakes, a shaped wing and center exhaust. Its shell uses Koenigsegg's [published dimensions](https://www.koenigsegg.com/technical-specifications-jesko-attack): 4,626 × 2,030 × 1,210 mm, on the existing 2,700 mm wheelbase. Surface contours are hand-shaped approximations, not factory CAD.
- Its hood, doors and engine cover have separate mesh seams and follow their physical panels. The glass follows physical breakage, and garage ride-height changes move the shell too. The collision solver continues to use the coarse physics cage. The spring package is an equivalent inboard inclined coil, not a full Triplex rocker simulation.
- The 4Runner, Bronco and Civic have sculpted shells built from their real lines (the Bronco's black hardtop, exposed door hinges, squared flares and round lamps; the EF hatch's wedge nose, wide flush lamps and moonroof). They dent, and their doors, hood and gate open, with the physics.
- Every visual vertex is a fixed weighted mix of physics nodes, so dents and tears appear exactly where the physics puts them.
- Details are drawn per pixel: framed side windows with pillars, windshields, headlamps with reflectors, tail lamps that brighten under braking, a grille, black bumpers and side skirts, door seams and handles, and number plates.
- Paint has a clear-coat that reflects the sky.

**Glass and cabin**
- Windows are tinted and see-through. Inside are seats, a dashboard and a steering wheel.
- Shattered windows become real holes.

**Wheels**
- Rounded tires with tread blocks and grooves.
- Dished five-spoke rims with lug nuts and a center cap.
- Brake discs that spin, and calipers that don't.

**Running gear**
- Drawn every frame from the live physics: wishbones with ball joints and bushings, knuckles, tie rods and the steering rack, coil-overs that compress as you drive, anti-roll bars, the twist beam, live axle housings with their links, prop and half shafts, differentials and the frame rails.

**Under the hood and underneath**
- The engine (block, head or two V-banks, valve covers, intake, headers, pulleys, alternator and belt) and the gearbox ride on the physical engine block, so you see them rock on their mounts. You'll see them once the hood pops open.
- Radiator, battery, exhaust with muffler and chrome tip, fuel tank, mirrors and wipers are attached to the body, so they crumple with it.

**Panels**
- Each bolt-on panel is drawn from its own nodes with a crisp gap around it, so a door swings open as a door and a hole shows where a panel came off.

The Jesko's mesh checks run without a graphics window:

```bash
make build/test_jesko_visual
nice -n 15 ./build/test_jesko_visual
```

They cover dimensions, rigid transforms, detached doors and hoods, broken glass,
garage ride heights, and a 50 km/h wall impact with finite, deforming surfaces.

## HUD

The HUD stays out of the way:

- **Bottom right:** a gauge cluster with tachometer, redline, needle, speed, gear, and TC-off / autopilot / handbrake lights.
- **Bottom left:** a small vehicle card. It shows a damage bar, the weight and front/rear balance, and warning pills (for example ENGINE DAMAGED, DRIVESHAFT BROKEN, STEERING BENT, LEFT DOOR OPEN or FRONT BUMPER OFF).
- **Top center:** short toasts that fade out.

Help only appears when you press H.

## Effects and sound

- Tire smoke on pavement and dust on grass
- Skid marks, and tire tracks pressed into loose ground (see Surfaces and tires)
- Sparks from scraping metal
- Glass shards
- Engine smoke
- Brake lights
- Procedural engine, tire, wind, crash, glass and blowout sounds
- Each engine has its own voice, built from the vehicle's spec:
  - **Firing pitch:** set by the cylinder count, rpm / 60 × cylinders / 2.
  - **Depth:** bigger engines sound deeper.
  - **Character:** a lumpy four (Civic), an even, howling flat-plane V8 (Jesko), smoother V6s.
  - **Turbos:** spool whistle and a blow-off valve when you lift (Jesko, Bronco).
  - **Supercharger:** Roots blower whine that tracks the revs (4Runner).
- Background music, generated live (no recordings), one mood per map:
  - **Proving ground:** mellow lo-fi. Swung drums, 7th/9th chords on an FM electric piano, a sub bass, vibes and a picked guitar.
  - **Mountain:** airy pads, fingerpicked plucks (Karplus-Strong), a breathy flute and bells, sometimes in 3/4.
  - **Freeway:** synthwave. Pumping saw bass and pads, a pulse arpeggio, a gated snare and a saw lead with echo.
  - **Frontier:** a desert road trip. Open pads, a twangy picked guitar with a dotted echo, root-fifth bass, cross-stick and shaker.
  - **Backcountry:** the mountain's airy folk again.
  - A seeded generator picks the key, mode and tempo and writes chord progressions (4–8 chords). Melodies come from a two-bar motif (statement, sequence, cadence) that lands on chord tones.
  - Every 30–90 s a new section adds or drops layers, and may change the progression or the key. Changing maps crossfades over 4 s.
  - The music sits about 8–14 dB under the engine at the default setting and costs under 1% of one CPU core.
  - To review it offline, `build/review/music/musictest.cpp` renders each map to WAV and writes spectrograms, levels and a note trace.

## Scanned assets

The scans are CC0 from [Poly Haven](https://polyhaven.com). The raw downloads stay out of git:

```bash
python3 tools/fetch_polyhaven.py assets/source/polyhaven   # ~154 MB, md5-checked
make bake                                                  # -> assets/props (committed): 3 LODs, atlases, hulls, billboards
```

`make bake` (needs ImageMagick's `convert`) simplifies each solid scan to three levels of detail (rocks about
1,500, 400 and 100 triangles), unwraps each level and bakes the full scan's colour, normal and AO/roughness onto
it, cuts each rock's convex hull down to 48 corners (`rocks.hull`, the format `World::addRock` uses), thins the
leaf and needle cards for the lower levels (dropping cards, scaling the rest up, inside a triangle budget) and
renders the trees' far billboards. `CZ_PROPS_DIR=DIR` points the game and the tests at another bake.

The ground has no grass, sand or asphalt scan yet: grass is two scanned grounds tinted green, sand the pale
ground warmed, asphalt the code-made look. CC0 sets that would fill the gaps (names to check against the sites):
Poly Haven `leafy_grass`, `aerial_grass_rock` (grass), `coast_sand_01`, `aerial_beach_01` (sand), `asphalt_02`
(asphalt); ambientCG `Grass001`-`Grass005`, `Asphalt012`.

## Credits

- Scanned rocks, cliffs, trees, plants, logs and ground materials: [Poly Haven](https://polyhaven.com), CC0
  (rock_moss_set_01/02, rock_07, rock_09, boulder_01, stone_01, namaqualand_boulder_03/04/05,
  namaqualand_boulders_01, namaqualand_stones_01, rock_face_01/02, namaqualand_cliff_01, fir_sapling,
  pine_sapling_small, dead_tree_trunk, dead_tree_trunk_02, shrub_02/03/04, fern_02, grass_medium_02,
  tree_stump_01/02, dry_branches_medium_01; forrest_ground_01, brown_mud_leaves_01, brown_mud_03, brown_mud_dry,
  rocky_terrain_02, gravel_ground_01, sandy_gravel_02, snow_02, forest_leaves_04, rocky_trail).
- `src/third_party/stb_image.h`: Sean Barrett, public domain.
- `src/third_party/cgltf.h`: Johannes Kuhlmann, MIT license.

## How it works

| File | What it does |
| --- | --- |
| `src/softbody.*` | Node/beam solver, plastic deformation and breaking, the chassis frame, panel pins, steering rack, anti-roll bars, tire model, engine, gearbox and torque reactions, ABS and traction control, contact between vehicles, sleeping, AI driver (Stanley steering, IDM car-following, passing, recovery) |
| `src/carbuild.cpp` | Builds a vehicle: body lattice, frame members, weight placement, engine block and mounts, suspension linkages (with rates from their geometry), driveline, bolt-on panels |
| `tests/` | Scenario harness, the scenarios, the parallel runner and its HTML report |
| `src/vehicles.*` | Vehicle and prop definitions |
| `src/world.*` | Baked heightfield, road carving, trees, static obstacles, collision queries |
| `src/render.*` | Shadow-mapped renderer: car body shader (per-pixel lamps, windows, trim), see-through glass pass, detailed wheels, HUD drawing |
| `src/groundtex.*`, `src/groundshader.cpp` | The photo-scanned ground: texture arrays and the terrain shader that blends them |
| `src/props.*`, `src/propdraw.*` | Scanned props: the baked model and hull files (no GL; the physics uses the hulls), and their instanced renderer |
| `tools/bakeprops.cpp`, `tools/fetch_polyhaven.py` | Fetches the Poly Haven sources and bakes them into `assets/props` |
| `src/carvisual.*` | Visual car models: subdivided body shell skinned to the physics, lattice-attached parts, running gear from the live suspension |
| `src/fx.*` | Particles, skid marks and tire tracks (decals multiplied into the ground), the lips of shoved-aside ground along ruts and round sunk tires |
| `src/audio.*` | Procedural sound |
| `src/music.*` | Generative background music (one style per map), mixed in by `audio.cpp` |
| `src/sdl_min.h`, `src/gl.*` | Minimal SDL2 and OpenGL declarations, so no headers need installing |
| `src/gamepad.*`, `src/gamepad_sdl.cpp` | Game controller support: shaping, steering, pedals, bindings and rumble (no SDL), and the SDL device with hot-plugging |
