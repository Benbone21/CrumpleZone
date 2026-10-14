# Handoff: where Crumple Zone stands (2026-10-13)

**Engine recordings / release checkpoint (2026-10-13):** user requested committing/pushing all updates before continuing, then asked for a Windows release. Prepared `docs/releases/v0.1.6.md`; publishing requires pushing the updated `sync` and a new version tag. The Windows ZIP builds locally; the installer is built by release CI (local `makensis` is unavailable).
- Recorded-loop startup loading, vehicle/garage engine mapping and RPM/load playback now serve local and nearby multiplayer voices. No actual recordings have been imported. Missing banks explicitly retain procedural audio. See `docs/engine-recordings.md` for format and reference sources.
- All **27** audio checks pass, including temporary PCM recordings, actual driver/remote playback, bank transitions, startup retention and malformed-file fallback. Native and Windows game/audio-test executables build. No new physics changes in this recorded-audio preparation.
- The CC0-only asset rule still applies. An optional question about allowing other game-use licenses is unanswered; no purchases or third-party audio downloads have been made.

**Engine audio / recovery / drivetrain follow-up (on `sync`):**
- Full 720-degree procedural engine cycles, bank-specific V8 rumble, exhaust/intake
  resonances and quieter induction sounds replace the old click/noise voice. Same synth
  serves local/nearby multiplayer engines. Silent before/after auditions are in
  `build/review/engine-audio/index.html` (WAVs alongside it); source is
  `tools/engine_preview.cpp`. These are approximations, not vehicle recordings.
- V opens a mouse/controller winch picker. O (letter O, explicitly confirmed by user)
  chooses your front/rear point, then the other vehicle's; body-area clicks snap to
  recovery points. Right stick aims, A selects, B cancels; mouse middle-drag looks,
  wheel zooms, Esc/right click cancels. No target cycling required. 0 still airs up tires.
- Saved winch multiplier defaults to 4x (~0.52 m/s unloaded), adjusts 0.1–20x with -/=
  or Settings. Rated stall pull, line break strength and tire grip are unchanged.
  Unattended tows release both brakes, temporarily select neutral and restore the
  prior gear when taking the wheel or ending the tow; no hidden slack-line brake.
- Manual R/N/1… with Q/E; W gas/S brake (RT/LT on pad) preserve selected gear.
  Neutral free-revs; reverse and first-gear engagement guards use travel/shaft speed.
  Reverse wheelspin no longer falsely opens engine-drag assistance after release.
  Manual clutches disengage/match RPM through shifts. Driver-selected shifts in torque
  converter automatics unlock; automatic-controller overlap remains unchanged.
- Validation: 14 offline/dummy-device audio checks and offscreen render integration pass.
  Windows game, audio test, runner and benchmark cross-compile; ZIP rebuilt. Full run
  before the last controller correction: **441/461 passed, 20 failures** (454 s).
  Final affected recheck: **26/26 passed**, including the previously failing Backcountry
  autopilot, modified buggy acceleration, Tacoma rock hits and all drivetrain guards.
  The remaining **19 failures are pre-existing**, including seven socket-blocked network
  tests; no baseline edits or range widening. Reports/logs are in
  `build/review/recovery-drivetrain/`. Native Windows/NVIDIA and live two-PC remain untested.
- User requested OpenCode with `ollama/qwen3.8:latest` as a sub-agent. Tried service and
  standalone modes; both cannot start a server in this sandbox (listen EPERM). No report
  or code contribution was received from OpenCode. Do not claim it participated.
- User explicitly requested a visible game launch after checks; this overrides the usual
  no-visible-window rule for that final launch only. Launch attempted: default PulseAudio initialization hung with `pa_write EPERM` and was
  interrupted. Explicit X11 and Wayland launches both failed (`SDL_Init: not available`);
  GTK also could not open the display. No test/game window was left running. To launch
  from a normal desktop terminal: `cd /home/bread/Documents/GitHub/MemeNG && ./crumplezone --map 0 --resume-model 2`.

See `docs/recovery-audio.md` for controls and validation details.

Start with `agent.md` (layout, rules, history). This page covers what's in flight.

**Windows/NVIDIA and CPU optimization work (2026-10-13, Codex):** changes are on `sync`.
The game now has saved **PHYSICS THREADS** (Esc → Settings): AUTO or 1 through all
available logical CPUs, applied immediately. AUTO retains the conservative four-thread
budget; manual counts above four are supported, also via `--physics-threads auto|all|N`.
This budget controls independent car forces and collision geometry; shared forces retain their order and the 2 kHz step. Swept collision
bounds, parked-car grid reuse, cached triangle projections and GL uniform locations;
single window sampling, HDR MSAA fallback, Windows `graphics.log` and F3 CPU timings.
Linux and Windows builds and new collision/worker/GL checks pass. Benchmarked separated
cars at roughly 3× the prior CPU throughput and the dense 12-car pileup at 23% less CPU
time, with matching state fingerprints. See `docs/performance.md` for measurements and
full-suite validation. Native Windows/NVIDIA hardware is still unverified; native Windows
CPU checks were added to release CI. Other worktrees do not automatically include these changes.
Final full suite (including multiplayer follow-up): **423/442 passed**, **19 pre-existing failures**, no new failures (seven
network tests cannot open sockets in this sandbox). Before: 416/436, 20 failures; the
load-sensitive serial performance threshold passed in the final run. Windows ZIP:
`build/win/CrumpleZone-windows.zip`; the Linux `./crumplezone` was rebuilt too.

**Multiplayer follow-up (same local work):** protocol 3 carries RPM/load/boost/reverse;
headlights use reliable per-car switches and late-join state. A monotonic playback clock,
three fragment assemblies, interpolation of controls/wheels and build counters address
jitter and queued old states. Shared engine synthesis mixes nearby cars spatially through
a lock-free audio frame buffer. New socket-free wire/playback and offline audio checks
pass; native Windows loopback checks were added to CI. **Both players need this build.**
See `docs/multiplayer.md`. Local real-socket and Wine tests remain sandbox-blocked.

**Rules:** work on branch `sync`. Never run `--update-baseline` until the suite passes, never edit `tests/baseline.txt` by hand, and use CC0 assets only. Run tests with `nice -n 19 ./build/run_tests --jobs 6`. Take screenshots offscreen (`SDL_VIDEODRIVER=offscreen`); never open a window. Fix the physics or the specs; never widen a test's range to make it pass.

## The suite on `sync`

Last full run (commit `eee5769` plus the A/D fix, `nice -n 19 ./build/run_tests --jobs 8`): **426 scenarios, 416 pass, 10 fail**. The 10 are the same ones as right after the merge.

The known failures, and what's behind each:

| Scenario | What fails | Cause, as far as known |
|---|---|---|
| `structure/rock-hits/4runner` | Frame set 2.4–2.5 % (limit 1) off the 1.2 m ledge | The truck high-centres its tail on the ledge's lip with the rear tires hanging, and the rear drops ~1 m/s onto it. The result swings ~20× with the crawl target: 0 % at 2.3–2.4 m/s and 2.65+, 3–12 % at 2.5–2.6. No frame or link variant tried fixes it (stronger tail bay, rail strength evened out, bumper bolted to the rail ends). `tools/trace/main/rltrace.cpp`. |
| `components/axle-landing` (buggy rows) | Settles 6–9 cm high after 2 m drops; 8.5–14 cm past the bump stop (limits 4 and 8) | The buggy's diff top sits level with its floor at rest, so a hard landing drives it into the floor nodes. At the old 0.62 frame narrowing it was trapped there, rolled 40°; at 0.9 (now) it lands upright. A real fix would raise the floor over the rear axle. `tools/trace/main/bland.cpp`. |
| `offroad/whoops` (UTV) | 42.5 km/h average (45–60); the UTV should be the fastest | It launches off the faces and dives nose first. Very sensitive to speed and damping. Branch `wip/buggy-utv` has push-only coils, which help travel but made whoops worse for everyone. |
| `freeway/top-speed/turbo utv` | 134 km/h (124–132) | The governor overshoots. Not investigated. |
| `components/tire-into-fender` (buggy, UTV) | No push, or nothing bends | Buggy: the solid axle lands on its bump stop before the tire reaches the narrowed frame. UTV: the chromoly lattice is far stronger than 1.75 in tube. |
| `structure/weight/toyota tacoma` | Front 52.1 % (53–58) | Since the stability net (commit 9f8882c): counting panel pins moves mass, and the CG rebalance has few nodes to work with. |
| `structure/weight/koenigsegg jesko` | CG 0.46 m (limit 0.46) | Same cause as the Tacoma. |
| `frontier/current` | Drift 0.43 m (1.5–40) | Same mass shift. The Civic rests on the riverbed, so the test is marginal anyway. |
| `engine/performance` | Real-time factor | The shared cloud CPU, not physics. |

## Paused work
- **Rock buggy and UTV retune.** Paused on request. The specs were rebuilt (no more Bronco and Jesko leftovers), the half shafts and axle-tube buzz were fixed, the buggy reaches its 150 km/h governor, and its approach and departure angles are now 47/54°. Still open: everything in the table above, plus re-speccing the UTV to the X3 X rs (1.60 m track, 22/24 in travel, 16 in clearance). The experiment is on branch `wip/buggy-utv` (commit `8df376c`, push-only coils, not merged).
- **Scanned props (Poly Haven).** The renderer, LODs, billboards, scatter and scanned rock hulls are in `src/propdraw.*` and `src/props.*`, with `make bake` and `tools/fetch_polyhaven.py`. Nothing is baked: this environment blocks `api.polyhaven.com` (and probably `dl.polyhaven.org`). To unblock, allow those hosts, fetch, `make bake`, check `du -sh assets/props` < 60 MB, and run `props/` (it skips without a bake). Budget: ≤4 ms at quality 2 on the Backcountry, measured on a real GPU.
- **Ground textures.** Blending, normal/ARM maps, anti-tiling, fading and quality steps are in. The steep-bank triplanar mapping from the paused branch compiled but was never looked at in a screenshot. Still unchecked: ruts, puddles, night, editor painting, map colours, and white squares seen on the Creek Crawl water.

## Wheels that come off, and swapping the spare on foot (in progress)

**Scope:** the Bronco, 4Runner and Tacoma only (`VehicleSpec::wheelLoss`). Every other vehicle is unchanged; the buggy turns it off explicitly because its spec starts as a copy of the Bronco's.

The user's choices:
- Wheels come off from hard hits shearing the studs, and from aired-down tires rolling off the rim.
- The swap is quick and physical: hold E, carry it, hold E.
- Any loose wheel can be picked up and fitted.
- "Road repair" means fixing the car on the trail.

### Done (commit `eee5769`)
- `src/tires.h/.cpp`:
  - `WheelItem` (a wheel or bare tire you can carry, store or fit).
  - `LooseWheel`, a rigid cylinder of its own: it rolls, tips over, sleeps, and pushes and gets pushed by vehicles' tires and nodes.
  - Sim functions: `shedWheel`, `addLooseWheel`, `takeSpare`, `stowSpare`, `removeWheel`, `fitWheel` (any size; a car sunk on its bare hub is raised onto the new tire), `pickUpWheel`, `dropWheel`, `looseWheelForces`, `looseWheelStep`. They're wired into `Sim::substep`.
- `Wheel::mounted` / `debeaded`: with no wheel the corner rolls on its bare hub (`kBareHubR` 0.17 m, steel, mu 0.3); with the tire off, on the rim.
- **Stud shear:** the moment of a sideways shove at the tread, as that force (`studLoad`, smoothed over 5 ms). A tire squeezed straight in on its face (a truck lying on its side) doesn't count. Limits: Bronco and 4Runner 100 kN, Tacoma 90 kN.
  - Measured peaks in the suite: 4Runner 80 kN (rollover only), Bronco 36, Tacoma 28, everything else lower.
  - Not yet confirmed that a real hit sheds one. `tools/trace/main/curb.cpp` slides a truck sideways into a curb at a given speed; run it for 15–60 km/h and expect nothing at 15 and a wheel off by ~45.
- **Debeading:** below 14 psi, a side load over (0.35 + psi/20) × the tire's load at more than 3 m/s, smoothed over 60 ms, rolls the tire off (`beadLoad`).
- **The spare** is the mass item named "spare … wheel" (Bronco on the gate, 4Runner on the carrier, Tacoma flat under the bed). `Car::spareAt()` and `spareAxis()` follow the body, and its weight comes off the truck with it.
- **On foot, A and D** were swapped (and you got out of the passenger door); fixed, and `walker/basics` checks both.

### Also done (uncommitted work pushed as-is at handoff)
- `src/interact.h/.cpp`: hold E (pad X) for take spare / remove / fit / reseat / pick up / stow; G (pad B) drops. `Walker::carrying` (1.2 m/s, no run or jump, J refused). There's an on-foot crosshair, prompt, progress bar and a "CARRYING" line (`main.cpp`).
- Drawing (`render.cpp` `drawWheels`): bare hub, bare rim, the spare drawn where it rides (only while it's there; the static mesh copy is off for `wheelLoss` trucks in `carvisual.cpp`), loose wheels, and the carried wheel in first person (`Renderer::carried`).
- `tests/tires.cpp`: swap, loose-wheel, refit-lost and wrong-size pass.

### Open problems (start here)
1. **The stud load can't yet tell a crash from normal use.** With the moment taken at the true contact point (the sidewall edge when a curb catches it), a sideways curb slide at 30–45 km/h peaks at 58–67 kN, but the existing tests peak higher: Bronco 100 kN (`surfaces/`, landings), 83 kN (`offroad/`), ~70 kN (`components/`, axle landings); 4Runner 99 kN (`damage/rollover`); Tacoma 79 kN. The limits are set above all of those (Bronco and 4Runner 120 kN, Tacoma 110 kN), so nothing sheds in normal play, and `tires/shear` (expects the 45 km/h curb slide to shed a wheel) **fails**. Ideas: smooth over ~30 ms (landings are short spikes, a curb slide is sustained); count only the moment that pulls the wheel off the hub face; or treat the sidewall as softer than the tread.
2. **Debeading never triggers (`tires/debead` fails).** The existing low-pressure "sidewall roll" model already cuts the side force by up to 40 %, so the measured ratio never reaches the trigger. Fix: compare the side force *before* the roll reduction (`Fm * sy / sm * lateralGrip`) in `wheelStep`.
3. **The spare's weight barely comes off (`tires/swap`: 3.8 kg instead of ~44).** The 8 lattice nodes around the spare are light after the stability pass, and `shiftSpareMass` caps each at 50 % of its mass. Spread the removal over body nodes within ~1 m (cap ~30 % each) and record exactly what came off, so stowing puts back the same amounts.

### Next, in order
1. **First-person controls.**
   - New `src/interact.h/.cpp` (in PHYS, testable without a window):
     - `Interact::options(sim, walker, eye, lookDir)` picks the target nearest the view ray within 2.5 m;
     - `Interact::update(dt, holdE)` runs the hold timer and calls the Sim functions above.
   - Hold times: take spare 5 s, remove wheel 8 s, fit 10 s, reseat a tire 20 s, pick up 1 s, stow 5 s. Drop (G) is instant.
   - `Walker::carrying` (`std::optional<WheelItem>`): 1.2 m/s, no running or jumping, and J refused while carrying.
   - Keys on foot: E hold, G drop (E is free on foot, see `main.cpp`'s gear keys); gamepad X and B.
   - HUD: a crosshair, and a prompt "HOLD E  FIT WHEEL (35 IN)" with a progress bar. The FLAT TIRE / WHEEL OFF warnings gain "SPARE: YES/NO".
2. **Drawing.**
   - Loose wheels and the carried wheel (first person, low in front of the eye; remote players at the chest).
   - The spare moves out of the static mesh (`carvisual.cpp` `EXTRA_SPARE`) into a per-frame wheel draw at `spareAt()`.
   - An empty hub shows only its rotor. `drawWheels` in `render.cpp` needs `!mounted` and `debeaded` cases.
   - A pose-based variant of `wheelMatrix` covers loose and carried wheels (local z = axle).
3. **Multiplayer.**
   - `"walk"` carries the held item.
   - Messages to spawn and remove loose wheels.
   - `takespare` / `fitwheel` / `removewheel` requests to the car's owner, like the `"fuel"` message.
   - `mounted`, `debeaded` and `hasSpare` bits in the snapshot's wheel flags (`net.cpp`).
4. **`fieldRepair`, open question:** a helper truck parked alongside still fixes a flat by itself, and `recovery/trail-repair` expects it to. Options: leave it (their crew fits a spare), make it use up the broken truck's own spare (`hasSpare`), or drop flats from it now that you can swap one yourself. Ask the user.
5. **Tests (`tests/tires.cpp`):**
   - `tires/shear`, `tires/debead`, `tires/loose-wheel`;
   - `tires/swap` (remove, drop, take the spare, fit, then drive 50 m straight);
   - `tires/refit-lost`, `tires/wrong-size`;
   - and a guard that no existing scenario sheds a wheel.
6. **Later trail repairs on the same interaction system:** plug a punctured tire and air it up, pull a broken half shaft and drive home in 2WD, straighten a bent tie rod, strap a hanging bumper.

## Releases
- `.github/workflows/release.yml` builds Linux (`.deb`), Windows (`.zip`) and Mac (universal `.app` in a `.zip`) on every push to `sync` or `main`, kept as artifacts.
- A version tag (`v1.2.3`) also publishes them as `CrumpleZone-<version>-mac.zip`, `-windows.zip` and `-linux.deb`, with notes from `docs/releases/<tag>.md`.
- **v0.1.0** is published, with Windows and Linux.
- **v0.1.1** (adds Mac) is ready (`docs/releases/v0.1.1.md`). The user has to push the tag: `git tag v0.1.1 origin/sync && git push origin v0.1.1`.
- **Waiting on the user:** renaming the GitHub repository from MemeNG to `crumplezone`, in its settings. The README already links the new name.
- **The cloud session can't** create releases, push tags or change repository settings. It can push branches (including `wip/*`) and read the Actions API, but not download job logs: the log host is blocked.
- **The Mac build** compiles and is signed on GitHub's macOS runner (SDL 2.30.9 framework, ad-hoc signed, not notarized). It has never been run on a real Mac.

## Environment notes (cloud container)
- **Offscreen screenshots** need Mesa: `apt-get install libegl1 libegl-mesa0 libgl1-mesa-dri libgles2 libgl1`. Software rendering is slow: ~3.5 s a frame on the Backcountry.
  - Example: `SDL_VIDEODRIVER=offscreen nice -n 19 ./crumplezone --no-audio --size 640x360 --map 4 --resume-model 2 --script drive --shot-prefix /tmp/s --shot-frames 40`
- **Windows:** `apt-get install g++-mingw-w64-x86-64-posix zip wine`, then `make windows`. Test under Wine with `xvfb-run` (the Windows SDL has no offscreen driver).
- **Linux package:** `make deb VERSION=x.y.z`. Build the binary first with `CXXFLAGS="-O2 -march=x86-64-v2 -mtune=generic"`; the default is `-march=native`.
- **Trace tools:** `tools/trace/` (see its README). `tools/trace/build.sh <tool>.cpp` builds one after `make build/run_tests`.
- **Pushing work:** never `git add -A` at the root (agent worktrees under `.claude/worktrees/` would go in as gitlinks); add paths explicitly.
- **Work branches on GitHub:**
  - `wip/buggy-utv`: the push-only coils experiment.
  - `wip/civic-mods`, `wip/rear-links`, `wip/4runner-braking`, `wip/merge-fixes`, `wip/scans`: the originals of fixes that are already on `sync` (as cherry-picks or merges), kept for history.
