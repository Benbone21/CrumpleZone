# Showroom garage: design (phases 1-3)

Status: design, approved for spec 2026-10-22. Nothing here is built yet.

## Goal

Make the garage feel like a showroom. Your vehicle sits on a turntable in a studio, and you
can orbit it. Every pick shows on the car as soon as you make it, including a lift or tire
change settling onto its springs. The stats show before and after for each pick. You can rev
the picked engine and hear it. Career prices and ownership show on every part.

Keep the game's own look: dark translucent panels, amber titles, the 5x7 bitmap font. Copy no
other game's UI or assets.

## Scope

In scope:
1. **Phase 1, studio and live preview.** The studio scene, the turntable camera, the panel
   UI, rebuilding the car on every pick, and bounded memory while browsing.
2. **Phase 2, stats and career.** Before/after stats with off-road numbers, prices, a cart,
   and buy-and-fit.
3. **Phase 3, audio preview.** The picked engine idles, revs while you hold a key, and blips
   on its own after an engine change.

Out of scope (later phases, separate specs):
- paint and two-tone;
- new mod slots (exhaust, body parts) and the multiplayer protocol bump they need;
- new body-part geometry, liveries, decals, metallic paint;
- a continuous lift slider.

The five existing slots and their options are unchanged. A vehicle added to the catalog (such
as the tow truck in progress in another session) shows up in the showroom with no extra work,
because the showroom reads `modOptions`.

## What exists (summary)

The full map with line numbers is in the side-session notes, `showroom-garage-feasibility.md`.
- `main.cpp` `GarageUI` / `drawGarage` is a text panel over the live chase camera. A pick
  changes nothing until ENTER. ENTER saves `garage.txt` and calls `Sim::swapPlayerVehicle`,
  which rebuilds the car where it stands.
- `tunedSpec(vehicle, mods)` caches every combination it is asked for, and keeps it forever,
  because cars hold raw spec pointers. The visual template (`carvisual.cpp`) is cached by spec
  address, built on a worker (`prebuildVisual`), and also never freed.
- `Car::build` is budgeted at under 100 ms. A built car already sits at its design ride height.
- Body paint is the `uPaint` uniform, taken from `car.color` on every draw.
- The audio is a push model. `main.cpp` feeds `Audio::setEngine/setVoice/setRecording/setBoost`
  from the player's car each frame. Neutral free-revs in the drivetrain.
- `src/lab.cpp` already runs a studio: its own `Sim`, `studioWorld()` (a paved disc in grass),
  `Renderer::setWorld`, `drawScene`, and an orbit camera with mouse hit-testing.
- Stats code: `peakPower` and `peakTorque` exist. Clearance and the approach, departure and
  breakover angles are computed only inside `tests/scenarios.cpp` (`structure/clearance`).
  Top speed is only measured by driving (`freeway/top-speed`).

## Architecture

### New units

| Unit | Purpose | Depends on |
|---|---|---|
| `src/studio.h/.cpp` | `studioWorld()`, moved out of `lab.cpp`, so the lab and the showroom share one studio. | `World` |
| `src/vehiclestats.h/.cpp` | `VehicleStats` and how to get them: from a spec (power, torque, top-speed estimate) and from a settled car (mass, clearance, angles). No GL, so it is testable. | `vehicles.h`, `softbody.h` |
| `src/showroom.h/.cpp` | The showroom: its state, its own `Sim`, the preview car's lifecycle, the camera, input, layout and drawing. | `mods.h`, `career.h`, `carvisual.h`, `render.h`, `hudkit.h`, `studio.h`, `vehiclestats.h` |

The showroom stays out of `main.cpp`, which is about 6k lines. `main.cpp` keeps only the hooks
listed under "Main loop integration".

### Changes to existing units

- **`mods.cpp`:** factor the body of `tunedSpec` into
  `void applyMods(int vehicle, const ModSet&, VehicleSpec& out)`. `tunedSpec` keeps its cache
  and behaviour, and calls `applyMods`. The showroom calls `applyMods` directly, on spec copies
  it owns. This touches only `tunedSpec`, so it should not conflict with the tow-truck work
  (new builders in `mods.cpp` and `vehicles.cpp`).
- **`carvisual.h/.cpp`:** add `void evictVisual(const VehicleSpec&)`. It frees that spec's
  template. It is a no-op while the template is still being built on a worker; the caller tries
  again later.
- **`render.h/.cpp`:** add `Renderer::forgetTemplate(const VisualTemplate*)`. It deletes that
  template's static VBO (`templateVbos`) and clears any `CarSlot` bound to it. `evictVisual`'s
  caller runs this on the GL thread before the template is freed.
- **`softbody.cpp` `Sim::addCar`:** no change in phases 1-3. Paint precedence is a phase 4
  problem.
- **`tests/scenarios.cpp` `structure/clearance`:** call `measureStance` from `vehiclestats`
  instead of its inline loop. The metrics must come out bit-identical; see Testing.

### Showroom interface (sketch)

```cpp
struct ShowroomResult { enum { NONE, CLOSE, FIT } act = NONE; ModSet mods; };
class Showroom {
public:
    void open(int vehicle, const ModSet& fitted, const float color[3], bool career, bool atStation);
    bool isOpen() const;
    void handleEvent(const SDL_Event&);        // mouse and keys
    void pad(const pad::State&);               // controller
    ShowroomResult update(double dt, Career&); // steps the studio Sim; buys parts; returns FIT/CLOSE
    void draw(Renderer&, int W, int H);        // scene + panels
    const Car* previewCar() const;             // for the audio feed (null while none is built)
    float revThrottle() const;
    void close();
};
```

`update` buys parts through `Career::buy`. It never fits: on FIT, `main.cpp` runs the existing
fit path.

## Phase 1: studio and live preview

### Entering and leaving

- G, the pause menu's GARAGE row and `--garage` open the showroom where they open the overlay
  today. The old `drawGarage` and its key chain (`main.cpp` around :1435 and :3585-3629) are
  removed. The showroom replaces them; there is no fallback.
- On open:
  1. Fade to black over 0.25 s.
  2. Call `renderer.setWorld(studio.world)`, and clear `r.frameModels` / `r.carModels`, as the
     lab does.
  3. Build the preview car of the fitted mods.
  4. Fade in.
- On close, fade out, call `renderer.setWorld(sim.world)`, set `drawnWorld = sim.world.stamp`,
  and fade in. The fades hide the two `setWorld` uploads.
- **The game's sim keeps running** under the showroom, because `main.cpp`'s loop goes on.
  - The player's car stays parked as it is today: handbrake on, inputs zeroed.
  - In single player, `sim.paused` is set while the showroom is open, as the pause menu does.
  - With `net.active()` the sim is never paused (agent.md rule), and `net.send` keeps going, so
    other players see your truck parked where you left it.

### The studio scene

- The studio has its own `Sim` holding `studioWorld()` and one car, the preview, parked on
  the paved disc at the origin in neutral with the handbrake on.
- It is stepped with the game's fixed step for the frame's `dt`, capped like the main sim's
  catch-up.
- It must not change the game's sim. The isolation test (see Testing) checks this, including
  shared statics, worker pools and per-thread scratch.

### Turntable camera

- The camera orbits the preview car's centre of mass. Inputs:
  - left-drag (mouse) or the right stick to orbit;
  - the wheel or the triggers to zoom.
- After 4 s without input it slowly auto-rotates (about 0.18 rad/s, as the lab does). The
  round paved pad reads as the turntable. No plinth geometry is added.
- **Per-slot framing.** Changing slot eases the camera over 0.6 s to a framing for that slot,
  until the player orbits again:
  - ENGINE and TRANSMISSION: front three-quarter, slightly high;
  - SUSPENSION, WHEELS and TIRES: low three-quarter at wheel height, so the stance change shows.

### Live rebuild

On every change of pick:
1. Wait out a 150 ms debounce. Scrolling fast through a list builds only where you stop.
2. Make the spec. Look in the showroom's own LRU, keyed by (vehicle, ModSet). On a miss,
   allocate a copy, call `applyMods`, and fill `model` and `mods[]` as `tunedSpec` does.
3. Call `prebuildVisual(spec, color)` on the worker. The current preview stays on screen with a
   small "FITTING..." label.
4. When `visualReady(spec)` is true:
   - remove the old preview car from the studio sim;
   - `addCar(spec, origin + 3 cm up, yaw kept)`.

   The car drops the 3 cm onto its new springs, so a lift or tire change visibly settles.
5. After the current pick is ready, prebuild the options either side of it in the current slot
   (idle time only).

Target: under 300 ms from the last key press to the new car on screen, on the RTX 2060 / i5-12400.

### Bounded memory

- The showroom owns its preview specs: up to 8, in `std::unique_ptr<VehicleSpec>`, least
  recently used first out.
- **Evicting a spec**, in this order:
  1. Skip it if its template is still being built, or if it is the live preview car's spec.
  2. Call `renderer.forgetTemplate(template)`.
  3. Call `evictVisual(spec)`.
  4. Free the spec.

  The order matters. The template cache is keyed by spec address, so freeing a spec first would
  let a new spec at the same address pick up the stale template.
- On FIT, the game takes its spec from `garage.spec(v)`, that is `tunedSpec`, which is permanent
  as today. The showroom's copies never reach the game sim.
- On close, the showroom evicts all its specs.

### Layout

The car is centred. Minimum supported size is 1280x720; spacing uses `u = max(0.75, H/720)`.

- **Left panel:**
  - slot tabs: ENGINE, SUSPENSION, TRANSMISSION, WHEELS, TIRES;
  - under them, the slot's option list, scrollable;
  - the fitted option marked "FITTED", the picked one highlighted;
  - each row's career state on the right (phase 2).
- **Right panel:** stats (phase 2). In phase 1 it shows the existing detail text from
  `drawGarage`: engine, redline, gearbox, final drive, tires, grip, ride-height delta, bounce Hz.
- **Bottom bar:** key or pad hints, cash (career), and the FIT action.
- **Drawing:** `hudkit` `label`/`pill`, `r.rect`, `r.text`. Panels use the existing colours
  (about 0.02, 0.025, 0.035 at 0.88 alpha), amber titles (1, 0.75, 0.3), grey labels
  (0.6, 0.62, 0.66), and a row highlight of white at 0.07.
- **Mouse:** a `Hit` list as in the lab. Click a tab or a row to pick it; the wheel over the
  list scrolls it.

### Input

| Action | Keys | Mouse | Pad |
|---|---|---|---|
| Slot | W/S | click tab | D-pad up/down, LB/RB |
| Option | A/D | click row, wheel over list | D-pad left/right, left stick |
| Orbit / zoom | — | left-drag / wheel over car | right stick / hold LT + right stick up-down |
| Rev (phase 3) | hold R | — | hold RT |
| Fit | ENTER | FIT button | A |
| Buy (phase 2) | B | BUY button | Y |
| Speedo recal | C | — | — (as today) |
| Leave | ESC or G | — | B |

The pad mappings go through `gamepad.cpp`'s menu mode, as the garage's do now. Check them
against the existing bindings when implementing, and keep them where they already agree.

### Fitting

- ENTER, the FIT button or A fits when:
  - the pick differs from what is fitted;
  - in career, the car is at a station and every picked part is owned.
- `main.cpp` then runs the existing path:
  1. set `garage.fitted[v]` and save it;
  2. `whenVisualReady(garage.spec(v), swap)`, where `swap` calls
     `sim.swapPlayerVehicle(garage.spec(v))`.
- The showroom closes after fitting. That way the swap happens in the world you return to,
  behind the fade.
- The refusal messages stay as they are today ("DRIVE TO A REPAIR STATION TO FIT PARTS",
  "LOCKED: ...").

## Phase 2: stats and career

### `VehicleStats`

```cpp
struct VehicleStats {
    double hp = 0, hpRpm = 0, nm = 0, nmRpm = 0;   // peakPower / peakTorque
    double topKmh = 0;                              // estimate (below)
    double kg = 0;                                  // carMass of the built car
    double clearance = 0, approach = 0, departure = 0, breakover = 0;  // measureStance, settled car
    bool measured = false;                          // the off-road fields are filled
};
double carMass(const Car&);                         // sum of node masses: known as soon as the car is built
VehicleStats specStats(const VehicleSpec&, double kg);  // engine numbers, and top speed for that mass
void measureStance(const Car&, const World&, VehicleStats&);  // clearance, angles (settled car): structure/clearance's code, moved
```

**Top-speed estimate.** The lower of two speeds:
1. **Power-limited:** solve P_wheel(v) = ½ ρ `dragArea` v³ + C_rr m g v, with P_wheel = peak
   power × a drivetrain efficiency, and C_rr a single constant.
2. **Gear-limited:** redline in top gear, `redline × 2π R / (60 × gears.back() × finalDrive)`.

Label it "EST." on screen. The efficiency and C_rr constants are fitted once, against the
stock catalog's `freeway/top-speed` ranges, and commented as estimates.

### Stats panel

- **Rows:**
  - POWER (hp @ rpm), TORQUE (Nm @ rpm), WEIGHT (kg), POWER/WEIGHT (hp/t), TOP SPEED EST. (km/h);
  - an OFF-ROAD group: CLEARANCE (cm), APPROACH, DEPARTURE, BREAKOVER (degrees);
  - DETAILS, collapsed by default: the phase 1 detail text.
- **Columns:** each row shows the fitted value, the picked value and the delta.
  - The delta is green when better and red when worse. Better is higher for everything except
    weight.
  - Equal values show no delta.
- **Bars:** each row has a thin bar, scaled to that stat's maximum over the stock catalog
  (computed once). The fitted value is drawn as a tick on the bar.
- **When the car numbers update:**
  - Weight, power/weight and top speed fill in as soon as the preview car is built.
  - The off-road group fills in once the preview car has settled. That is 1.0 s after it is added, with the handbrake on, the same as
    `structure/clearance`'s 2 s run halved. Measure that 1 s gives the same numbers to 1 mm and
    0.1 degree; if it doesn't, use 2 s.
  - Until then those rows show "..." and keep the previous value greyed.
  - Results are cached per spec.
- **"Before" values:** the fitted spec is the first preview built on open, so its measurement
  becomes the fitted column. They are cached per vehicle for the session.

### Career

- **Each option row shows one of:**
  - nothing for a factory part;
  - "OWNED";
  - "$price", where `Career::price` is above 0;
  - "FIND IT", where the price is 0 (tier 3: caches and gold medals).

  In sandbox no prices show.
- **Locked parts can be previewed, stats included.** They can't be fitted.
- **Cart:** the bottom bar shows the total price of the picked parts you don't own, and your
  cash. If any picked part can't be bought, it says "CAN'T FIT: <part> NOT FOR SALE".
- **B / Y / BUY** buys the highlighted part, as today (`Career::buy`).
- **Fitting unowned parts at a station:**
  - ENTER with unowned but buyable picked parts asks "BUY 2 PARTS FOR $3,750? ENTER TO
    CONFIRM". A second ENTER within 3 s buys them all and fits.
  - If cash is short it says "NEED $X MORE" and does nothing.
  - Away from a station, the existing message stands.
- Cash, purchases and `career.save()` go through the existing `Career` functions only.

## Phase 3: audio preview

### Engine voice

- While the showroom is open, `main.cpp`'s audio block feeds the showroom's preview car where it
  feeds the player's car today. The same calls apply: `setEngine`, `setVoice`,
  `setRecording(engineSamples::forVehicle(spec))` and `setBoost`.
  - With no preview car yet, it keeps the last one.
  - Tires and speed are fed 0.
- The preview car really runs. It is in neutral with the handbrake on, so the engine model
  produces its own idle, rev limiter and turbo spool, and load comes from throttle × `tcCut`,
  as for the player's car.
- The `timeScale` factor in the rpm feed is skipped for the preview, because the studio runs
  at real time.

### Revving

- **Hold R or RT:** the preview car's throttle goes to 1 and it free-revs. Release lets it fall
  back to idle.
- **Auto-blip:** when an engine pick finishes building, a scripted throttle runs: 0.15 s to
  full, held 0.5 s, then released. It only runs if the player isn't already revving.

### Mix

While the showroom is open, music is ducked to 50% of its setting. The engine plays at full
scene volume.

### Voice swaps

Changing engines changes `EngineVoice`'s identity, which crossfades (`engine_samples.cpp`).
Check that a swap at full revs doesn't click; see Testing.

## Main loop integration

All of these are in `main.cpp`:
1. Opening: replace `openGarage`'s body with `showroom.open(...)`.
2. Events: while open, the showroom takes all SDL events before the game. ESC closes the
   showroom, not the pause menu.
3. Update: call `showroom.update(dt, career)` after the main sim's step, and handle `FIT` /
   `CLOSE`.
4. Draw: while open, `showroom.draw(renderer, W, H)` replaces `drawScene` and the HUD.
5. Audio: branch on `showroom.isOpen()` in the existing audio block.
6. Parking: the existing `garageUI.open || menu.open ...` check reads `showroom.isOpen()`.

## Error handling

- **A build or template that never finishes:** the old preview stays up with "FITTING...".
  After 5 s the showroom logs to stderr and shows "COULDN'T BUILD THIS COMBINATION"; the pick
  stays selectable.
- **A preview car that goes unstable** (NaN or a node more than 5 m from the origin): remove it,
  show the same message, and keep the stats from the spec only.
- **Closing while a template is still being built:** that spec is evicted later, when it is
  ready. The showroom keeps a pending-evict list, which `update` empties even while closed.
- **`--garage` with no player car, or a prop:** same refusal as `openGarage` today.

## Testing

All new tests go in `run_tests`. On Windows the bar is the baseline: 962/973 with the known
failures in `docs/hardware/windows-rtx2060-baseline.md`, and no new failures. The `bench_sim`
fingerprint stays unchanged. Never use `--update-baseline`, and never widen a range.

| Test | Phase | What it checks |
|---|---|---|
| `showroom/isolation` | 1 | A world with two cars run 10 s alone, and again with a studio `Sim` stepped every frame alongside it: the main sim's fingerprint is identical. |
| `showroom/memory` | 1 | Scroll 40 picks through the showroom headlessly (templates built with `blockOnTemplates`): at most 8 showroom specs alive, every evicted template freed, `tunedSpec`'s cache doesn't grow. |
| `showroom/stance` | 1 | Preview of a lift mod: after settling, body height differs from stock by what the part says (the same tolerance as the existing mods test). |
| `structure/clearance` | 2 | After moving the code to `measureStance`: every metric bit-identical to before (compare the printed metrics on the same build before and after the move). |
| `showroom/stats` | 2 | `specStats` power and torque match `peakPower`/`peakTorque`. Stats measured after 1 s match the 2 s values to 1 mm and 0.1 degree. |
| `showroom/top-speed-estimate` | 2 | For every stock catalog vehicle, the estimate is within 10% of the midpoint of its `freeway/top-speed` range. If a vehicle can't be estimated that well (the Jesko's downforce), report it and agree what to do. Don't drop or loosen the check. |
| `showroom/career` | 2 | Preview of a locked part allowed, fit refused; buy-and-fit at a station spends exactly the cart total; short of cash spends nothing; sandbox shows no prices. |
| `showroom/rev` | 3 | Preview car in neutral, throttle held: rpm passes 90% of redline within 3 s; released: back within 10% of idle within 3 s. |
| audio check | 3 | An offline render of a voice swap at full revs has no sample-to-sample step larger than the largest step in a normal rev of the same engine. |
| render check | 1 | An offscreen screenshot of the showroom has the car in frame. After closing, a frame of the game draws the player's car, not the preview. |

**By hand,** on this machine in a real window: the frame rate in the showroom against the
game's, pick-to-visible latency (target 300 ms), and how the rev sounds. Report the numbers in
the PR. Don't claim them unmeasured.

## Delivery

- One branch and PR per phase, stacked:
  - `showroom/1-studio`;
  - `showroom/2-stats`, based on phase 1;
  - `showroom/3-audio`, based on phase 2.
- Push to the fork (`awittmannTech/CrumpleZone`), and open with
  `gh pr create --repo Benbone21/CrumpleZone --head awittmannTech:<branch>`.
- Each PR reports:
  - the Windows suite result against the baseline;
  - the `bench_sim` fingerprint;
  - the hand-measured numbers.

## Risks

- **Shared state between two `Sim`s:** worker pools, statics and per-thread scratch.
  `showroom/isolation` is the first thing built in phase 1. If it fails, fix the sharing before
  anything else.
- **`setWorld` cost** on open and close. Measure it. The fade hides up to about 250 ms. Past
  that, cache the uploaded studio world or upload it once at startup.
- **Renderer car slots** are indexed by car number. The preview is car 0 of the studio sim, the
  same slot as a game car. `CarSlot` rebinds by template every frame. The render check confirms
  the game's car comes back intact after closing.
- **Template eviction** is new code in a cache that never freed anything. Address reuse is the
  trap, which is why eviction has a fixed order.
- **The top-speed estimate** may fit badly for the Jesko (aero) and the governed vehicles. The
  test is honest about it rather than loosened.
- **Overlap with the tow-truck session:** only `mods.cpp`'s `tunedSpec` is shared. Rebase onto
  `main` after the truck merges.
