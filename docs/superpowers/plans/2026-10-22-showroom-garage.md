# Showroom Garage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the text-overlay garage with a studio showroom. The car sits on a turntable
and is rebuilt live on every pick. Parts are picked from donor tiles with a drill-in to their
variants, plus curated builds. Before/after stats and career prices show, and the picked
engine can be revved.

**Architecture:** A GL-free `ShowroomCore` (in `run_tests`) owns a studio `Sim`, the preview
car's lifecycle, a bounded set of preview specs, stats, the cart and the rev throttle. It
reaches the visual-template cache only through injected hooks. A thin `Showroom` (game only)
adds the fades, the world swap, the camera, the layout, input and drawing. `main.cpp` keeps a
few hooks. Phases ship as four stacked PRs.

**Tech Stack:** C++17, SDL2, the repo's own GL 3.3 loader, the repo's test harness
(`tests/harness.h`, `SCENARIO`), and MSYS2 UCRT64 make on Windows.

**Spec:** `docs/superpowers/specs/2026-10-22-showroom-garage-design.md`. Read it first. This
plan doesn't repeat its rationale.

## Global Constraints

- No new dependencies: SDL2 plus the repo's own GL loader only. CC0 assets only, and none are
  needed here.
- **Physics stays bit-identical** (`-ffp-contract=off`). The `bench_sim` fingerprint recorded in
  Task 0 must not change on any branch.
- Never pass `--update-baseline`, never edit `tests/baseline.txt`, never widen a test's range.
  Fix the code or report instead.
- **Windows bar:** 962/973, with exactly the known failures in
  `docs/hardware/windows-rtx2060-baseline.md`, plus the new showroom tests passing. That file
  is on the fork branch `hw/windows-rtx2060-baseline` (PR #4), not on `main`.
  - Get it with `git show fork/hw/windows-rtx2060-baseline:docs/hardware/windows-rtx2060-baseline.md`.
- Build on Windows with
  `C:/msys64/usr/bin/bash.exe -lc "cd /c/Users/Andrew/projects/CrumpleZone/.claude/worktrees/<wt> && MSYSTEM=UCRT64 make -j12 windows build/win/run_tests.exe"`.
  - Tests: `build/win/run_tests.exe --jobs 4 [filter]`.
  - Put a time limit on every run (`timeout -k 10 N`).
- **Multiplayer:** never pause or scale time while `net.active()`. In single player, the game
  sim is paused while the showroom is open.
- **Look:**
  - panels `(0.02, 0.025, 0.035, 0.88)`;
  - titles amber `(1, 0.75, 0.3)`;
  - labels grey `(0.6, 0.62, 0.66)`;
  - row highlight white at alpha `0.07`;
  - spacing `u = max(0.75, H/720)`;
  - the 5x7 bitmap font via `r.text`, `hudkit` `label`/`pill`.
- **Code style:** terse, matching the surrounding comment density and naming. New gameplay
  numbers get a comment saying where they come from.
- **Git:**
  - Never `git add -A` at the root; add paths explicitly.
  - Push only to the `fork` remote. Open PRs with
    `gh pr create --repo Benbone21/CrumpleZone --head awittmannTech:<branch>`.
- **No visible windows:** agents never open one (agent.md). The "by hand" steps go to the user,
  or need their explicit OK.
- **Coordinate:** another session is adding a tow truck in `src/vehicles.cpp` / `src/mods.cpp`.
  Touch `mods.cpp` only in Task 2, and rebase onto `main` before each PR.

## Review Focus

1. **Mashing through options faster than builds finish.** The showroom specs alive (built or
   building) stay at most `kMaxSpecs + kMaxInFlight` (8 + 3). Only the last stop gets built.
   Test in Task 4.
2. **Small or huge windows** (1024x600, 3840x2160). The panels never overlap the car area, the
   option list shows at least 3 rows, and nothing is drawn off-screen. Test in Task 4
   (`showroomLayout`).
3. **ESC or G pressed again during a fade, or FIT then ESC at once.** Each world is swapped in
   exactly once, a close can't happen before the open finishes, and there is no double
   `setWorld`. Test in Task 4 (`ShowroomFade`).
4. **A saved or fitted pick out of range** (a hand-edited `garage.txt`, or the catalog gaining a
   vehicle and shifting cross-vehicle option indices). It is clamped exactly as `tunedSpec`
   clamps, and there is no crash. Tests in Tasks 2 and 4.
5. **Changing the pick between the two F presses of buy-and-fit.** The confirmation applies
   only to the cart it was shown for. A changed pick cancels it and spends nothing. Test in
   Task 14.
6. **Browsing BUILDS must not lose your pick.** Tabbing through BUILDS changes nothing, YOUR
   PICK restores the exact pick you entered with, and a build's unnamed slots come from YOUR
   PICK, never from the previous build. Test in Task 9.
7. **The catalog gaining a vehicle** (the tow truck). It becomes a new donor group, every
   option is still in exactly one group, and every build key still resolves. Tests in Tasks 8
   and 9; re-run them after rebasing onto a `main` that has the truck.

---

## Phase 1: studio and live preview (branch `showroom/1-studio`)

### Task 0: Branch and baseline

- [ ] **Step 1:** Run `git fetch origin && git fetch fork`. Create `showroom/1-studio` from the
  commit that holds this plan, rebased onto `origin/main`.
- [ ] **Step 2:** Build (command in Global Constraints). Expected: no errors.
- [ ] **Step 3:** Run `timeout -k 10 3600 build/win/run_tests.exe --jobs 4`.
  - Expected: 962/973, failures matching the baseline file.
  - Save the summary to the scratchpad.
- [ ] **Step 4:** Build and run `build/win/bench_sim.exe` with the arguments used in the
  baseline file. Record the `state` hash; every later task must reproduce it.

### Task 1: Shared studio world and the isolation guard

**Files:**
- Create: `src/studio.h`, `src/studio.cpp`
- Modify: `src/lab.cpp:27-45` (delete `studioWorld`, include `studio.h`)
- Modify: `Makefile` (add `src/studio.cpp` to `PHYS`)
- Create: `tests/showroom.cpp`
- Modify: `Makefile` (add `tests/showroom.cpp` to `SCEN`)

**Interfaces:**
- Produces:
  - `World studioWorld();` in `studio.h`. The body moves unchanged from `lab.cpp`.
  - In `tests/showroom.cpp`, the test helper `static uint64_t simFingerprint(const Sim&)`: the
    FNV mix from `tools/bench_sim.cpp:64-76`, over node p/v and beam L0/broken.

- [ ] **Step 1: Write the failing test** `SCENARIO(showroomIsolation, "showroom/isolation", ...)`
  1. Build two identical `Sim`s, `a` and `b`. Each does
     `setupEmpty(); world = World(MAP_PROVING_GROUND)`, adds two catalog cars (indices 0 and 2)
     10 m apart, and gets `throttle = 0.5`.
  2. Build a third `Sim`, `s`, with `world = studioWorld()` and one car (catalog 3) at the
     origin, in neutral (`gear = 0`), handbrake 1, throttle 1.
  3. Run `a` alone for 600 frames: `a.advance(1/60.0, 64)`.
  4. Run 600 frames where each frame calls `b.advance(1/60.0, 64)`, then
     `s.advance(1/60.0, 64)`.
  5. `ctx.check("main sim unchanged by a studio sim", simFingerprint(a) == simFingerprint(b))`.
- [ ] **Step 2:** Build and run `build/win/run_tests.exe showroom/`. Expected: a compile error
  (no `studio.h`).
- [ ] **Step 3:** Create `studio.h/.cpp` and update `lab.cpp` and the `Makefile`.
- [ ] **Step 4:** Run `showroom/isolation`. Expected: PASS.
  - **If it fails:** stop. Find the shared state (worker pool, statics, `CZ_THREAD_CACHE`
    scratch) with a bisect over substep stages, and fix the sharing. Don't continue the plan on
    a failing guard.
- [ ] **Step 5:** Build the game (`make windows`) and start `--lab` offscreen as `agent.md`
  describes. Check that one screenshot frame matches the lab before the change.
- [ ] **Step 6:** Commit: `git add src/studio.h src/studio.cpp src/lab.cpp tests/showroom.cpp Makefile`,
  message "Studio world shared by lab and showroom; showroom/isolation guard".

### Task 2: `applyMods`, out of `tunedSpec`

**Files:**
- Modify: `src/mods.h` (declare), `src/mods.cpp:757-779`
- Test: `tests/showroom.cpp`

**Interfaces:**
- Produces: `void applyMods(int vehicle, const ModSet& mods, VehicleSpec& out);`
  - `out` is overwritten with a copy of the catalog entry, then each slot's pick is clamped to
    `[0, options-1]`, written to `out.mods[k]` and applied. `modSummary` is filled exactly as
    `tunedSpec` does today.
  - `tunedSpec` keeps its cache and calls `applyMods` on a cache miss.

- [ ] **Step 1: Write the failing test** `SCENARIO(showroomApplyMods, "showroom/apply-mods", ...)`.
  For every catalog vehicle and three ModSets (all 1s; the last option in every slot;
  `{999, -5, 1, 0, 2}`):
  ```cpp
  VehicleSpec s; applyMods(v, m, s);
  const VehicleSpec& t = tunedSpec(v, m);
  ctx.check(name, s.modSummary == t.modSummary && s.layerY == t.layerY && s.redline == t.redline
                  && s.finalDrive == t.finalDrive && s.wheelRadius == t.wheelRadius
                  && std::equal(s.mods, s.mods + MOD_SLOTS, t.mods));
  ```
  Also check that the out-of-range set gives `s.mods[0] == options-1` and `s.mods[1] == 0`.
- [ ] **Step 2:** Run it. Expected: a compile error (`applyMods` is undeclared).
- [ ] **Step 3:** Implement `applyMods` and make `tunedSpec` use it.
- [ ] **Step 4:** Run `showroom/apply-mods` and the existing mods tests
  (`run_tests.exe --jobs 4 mods`). Expected: PASS, with no change from the Task 0 results.
- [ ] **Step 5:** Commit `src/mods.h src/mods.cpp tests/showroom.cpp`, message "applyMods:
  tunedSpec's body, for owned spec copies".

### Task 3: Freeing a visual template

**Files:**
- Modify: `src/carvisual.h`, `src/carvisual.cpp` (`Slot`, `cache()`, near `visualReady`)
- Modify: `src/render.h` (declare), `src/render.cpp` (near `templateVbos`, :4040-4050)
- Create: `tests/showroom_visual.cpp`
- Modify: `Makefile`, adding these targets modelled on `build/test_loading_visual`:
  - `build/test_showroom_visual`, linking `tests/showroom_visual.o`, `carvisual.o`, `PHYS` and
    `harness.o`;
  - `build/win/test_showroom_visual.exe`, with the same objects under `build/win/obj` and the
    `win/run_tests.exe` link flags.

**Interfaces:**
- Produces:
  - `bool evictVisual(const VehicleSpec& spec, const std::function<void(const VisualTemplate&)>& forget);`
    - Returns false, and leaves the cache as it is, while the spec's slot is queued but not ready.
    - Otherwise it calls `forget(*t)` if a template exists, erases the slot from `cache()`, and
      returns true.
    - A spec never seen returns true.
  - `void Renderer::forgetTemplate(const VisualTemplate* t);`
    - Deletes the static VBO paired with `t` in `templateVbos` and removes the pair.
    - Resets every `CarSlot` whose `tpl == t` or `builtTpl == t`: zero counts, null
      `tpl`/`builtTpl`, `intact = false`.

- [ ] **Step 1: Write the failing test** `tests/showroom_visual.cpp`, in the style of
  `loading_visual.cpp` (prints PASS/FAIL, returns the failure count). It sets
  `blockOnTemplates = true`.
  - `evict frees`:
    1. `auto* a = new VehicleSpec(cat[0])`; build a `Car` of it; `visualTemplate(car)`.
    2. `evictVisual(*a, f)` is true and `f` was called once.
    3. `visualReady(*a)` is false afterwards.
  - `no stale template on address reuse`:
    1. Make a template for spec `a` (catalog 0) and record `t0`.
    2. Evict it and `delete a`.
    3. Placement-new a catalog 2 spec at the same address.
    4. Build its template: it is not `t0`, and its vertex count differs from `t0`'s.
  - `pending refuses`:
    1. With `blockOnTemplates = false`, call `prebuildVisual(*b, color)` and immediately
       `evictVisual(*b, f)`.
    2. Either it returns false, or (the build won the race) `visualReady` was true before it.
    3. Poll until ready, then evict: true.
- [ ] **Step 2:** Run `make build/win/test_showroom_visual.exe`. Expected: a compile error
  (`evictVisual` is undeclared).
- [ ] **Step 3:** Implement `evictVisual` (taking `cacheLock()` and `slot.build`) and
  `Renderer::forgetTemplate`.
- [ ] **Step 4:** Run `build/win/test_showroom_visual.exe`. Expected: all PASS, exit 0. Also run
  `build/win/run_tests.exe --jobs 4 showroom/` (still PASS).
- [ ] **Step 5:** Commit `src/carvisual.h src/carvisual.cpp src/render.h src/render.cpp tests/showroom_visual.cpp Makefile`,
  message "evictVisual / Renderer::forgetTemplate: templates can be freed".

### Task 4: `ShowroomCore`: preview lifecycle, layout and fades (GL-free)

**Files:**
- Create: `src/showroomcore.h`, `src/showroomcore.cpp` (added to `PHYS`)
- Test: `tests/showroom.cpp`

**Interfaces:**
- Consumes: `studioWorld()` (Task 1), `applyMods` (Task 2), `Sim::addCar/carRemoved/advance`.
- Produces:

```cpp
struct VisualHooks {  // the template cache (carvisual.h) in the game; fakes in tests
    std::function<void(const VehicleSpec&, const V3&)> prebuild;
    std::function<bool(const VehicleSpec&)> ready;
    std::function<bool(const VehicleSpec&)> evict;  // false: still building, try again later
};
class ShowroomCore {
public:
    static constexpr int kMaxSpecs = 8, kMaxInFlight = 3;
    static constexpr double kDebounce = 0.15, kDrop = 0.03, kBuildTimeout = 5.0, kUnstable = 5.0;
    explicit ShowroomCore(VisualHooks hooks);
    void open(int vehicle, const ModSet& fitted, const V3& color);  // clamps fitted like applyMods
    void close();                                    // queues every spec for eviction
    bool isOpen() const;
    int vehicle() const;
    const ModSet& fitted() const;
    const ModSet& pick() const;
    void setPick(int slot, int option);              // clamped; restarts the debounce
    void update(double dt);                          // debounce, build, swap, prebuild neighbours, step studio, evict (also while closed)
    const Car* preview() const;                      // null until the first build is in
    bool fitting() const;                            // a newer pick is building
    bool failed() const;                             // build timed out or the preview blew up
    int liveSpecs() const;                           // built + building + awaiting eviction
    Sim& studio();
};
struct Rect { float x, y, w, h; };
struct ShowroomLayout { float u; Rect left, right, bottom, car; int listRows; };
ShowroomLayout showroomLayout(int W, int H);
struct ShowroomFade {  // black fade between worlds: start() -> black (swap) -> clear
    static constexpr double kTime = 0.25;
    bool start();                                    // false if one is running already
    bool update(double dt);                          // true once, on the frame it reaches black
    float alpha() const;
    bool busy() const;
};
```

Behaviour the tests pin:
- **Preview specs:**
  - Held in `std::list<std::unique_ptr<VehicleSpec>>`, in least-recently-used order.
  - The spec of the shown preview is never evicted, and neither is one that is building.
  - Evicted specs go to a pending list that `update` retries through `hooks.evict`.
- **Swapping the preview** (when `ready(spec)`):
  1. `carRemoved(0)` and erase car 0;
  2. `addCar(spec, V3(0, kDrop, 0), yaw, color)`, where yaw is 0 on the first build and the old
     car's yaw after that;
  3. set `gear = 0` and `handbrake = 1`.
- **Neighbours:** after the current pick is shown, prebuild options −1 and +1 in the last slot
  changed, if they exist. `kMaxInFlight` counts these builds.
- **Failures:** `failed()` turns true when either:
  - a pick stays not-ready for `kBuildTimeout`;
  - any preview node is NaN or more than `kUnstable` m from the origin, in which case that car
    is also removed.

  `failed()` clears on the next `setPick`.
- **Layout:**
  - Left panel 26% of W, at least `280u`.
  - Right panel 24% of W, at least `260u`.
  - Bottom bar `40u` high.
  - `car` is the rest.
  - `listRows = floor((left.h - 60u) / (18u))`.
- **Studio stepping:** `update` steps the studio with `advance(dt, 64)`.

- [ ] **Step 1: Write the failing tests** in `tests/showroom.cpp`. They use a fake
  `VisualHooks` whose `ready` turns true a set number of `update` calls after `prebuild`
  (`FakeCache{int delayFrames; std::map<const VehicleSpec*, int> t; std::set<const VehicleSpec*> evicted;}`).
  - `showroom/memory`:
    1. Open catalog 3.
    2. 40 times: `setPick(MOD_ENGINE, k % opts)`, then `update(0.2)` six times.
    3. After every update, check `liveSpecs() <= 8 + 3`.
    4. Check that the fake saw no `prebuild` of an evicted address, that `tunedSpec`'s results
       are untouched (`&tunedSpec(3, m)` the same before and after), and that after `close()`
       plus 10 updates `liveSpecs() == 0`.
  - `showroom/mash` (Review Focus 1): 30 `setPick` calls, each followed by `update(0.05)`
    (inside the debounce), then `update(0.2)` × 10. The fake saw exactly 1 non-neighbour
    `prebuild`, for the final pick.
  - `showroom/stance`:
    1. Open with stock, then pick catalog 3's own first non-stock suspension option that raises
       the body.
    2. Run updates until shown, then 1.0 s more.
    3. Check that the preview's mean body-node height minus stock's is within the existing mods
       test's tolerance of the part's stated lift.
    4. Reuse the lift value the mods test reads (grep `tests/` for the ride-height check).
  - `showroom/failures`:
    1. With `delayFrames = INT_MAX`, `update` for 5.1 s: `failed()`.
    2. `setPick`: not failed.
    3. A preview with node 0 moved to `(100, 0, 0)` gives `failed()` and `preview() == nullptr`.
  - `showroom/clamp` (Review Focus 4): `open(3, {999, -5, 1, 0, 2}, c)` gives `fitted().pick`
    equal to `{opts0-1, 0, 1, 0, 2}`, and it builds and shows.
  - `showroom/layout` (Review Focus 2): for 1024x600, 1280x720, 1920x1080 and 3840x2160, check:
    - the rects lie inside the screen;
    - left, right and car don't overlap;
    - `car.w > 0.3W`;
    - `listRows >= 3`.
  - `showroom/fade` (Review Focus 3):
    - `start()` is true; a second `start()` while busy is false.
    - `update` returns true exactly once over 0.6 s of 1/60 steps.
    - `alpha` goes 0 → 1 → 0, and `busy()` is false at the end.
- [ ] **Step 2:** Run `run_tests.exe showroom/`. Expected: a compile error.
- [ ] **Step 3:** Implement `src/showroomcore.*` and add it to `PHYS`.
- [ ] **Step 4:** Run `run_tests.exe --jobs 4 showroom/`. Expected: all PASS. Then run
  `bench_sim`: the hash equals Task 0's.
- [ ] **Step 5:** Commit `src/showroomcore.h src/showroomcore.cpp tests/showroom.cpp Makefile`,
  message "ShowroomCore: preview lifecycle, bounded specs, layout, fades".

### Task 5: `Showroom`: scene, camera, panels and input (game side)

**Files:**
- Create: `src/showroom.h`, `src/showroom.cpp` (added to `GAME_SRC`)
- Modify: `tests/render.cpp` (one check)

**Interfaces:**
- Consumes:
  - `ShowroomCore`, `ShowroomLayout`, `ShowroomFade` (Task 4);
  - `evictVisual`, `Renderer::forgetTemplate` (Task 3);
  - `prebuildVisual`, `visualReady`, `studioWorld`.
- Produces:

```cpp
struct ShowroomResult { enum Act { NONE, FIT, CLOSED } act = NONE; ModSet mods; };
class Showroom {
public:
    Showroom(Renderer& r, std::function<void()> restoreGameWorld);  // restore: renderer.setWorld(sim.world) + drawnWorld
    void open(int vehicle, const ModSet& fitted, const V3& color, bool career);
    bool isOpen() const;          // from open() until the closing fade has cleared
    bool drawsScene() const;      // true while the studio is the world on screen (game skips drawScene/HUD)
    void handleEvent(const SDL_Event& e);
    void pad(const pad::Raw& raw, double dt);
    ShowroomResult update(double dt, Career& career, bool atStation);
    void draw(int W, int H);      // studio scene (when drawsScene) + panels + fade overlay
    const Car* previewCar() const;
    void requestClose();          // ESC/G/B, and after FIT
};
```

Pinned values:
- **Orbit:** around the preview's centre of mass, yaw measured from the car's +x.
  - Distance `1.25 × car length`, clamped 0.15×–4× by zoom, as in the lab.
  - After 4 s idle, auto-rotate at 0.18 rad/s.
- **Per-slot framing,** eased over 0.6 s on a slot change:
  - ENGINE and TRANSMISSION: yaw 0.6, pitch 0.25, distance factor 1.0;
  - SUSPENSION, WHEELS and TIRES: yaw 0.9, pitch 0.05, distance factor 0.8.
- **Input:** the spec's input table. Keys accept both W/S/A/D and the arrow keys, because the
  pad's menu mode taps arrows. `pad()` reads LB/RB edges for the slot, the right stick to
  orbit, LT held plus right-stick Y to zoom, and RT (Phase 4).
- **Speedo recal (C):** port `GarageUI::recal`'s handling unchanged.
- **Phase 1 right panel:** the detail lines of today's `drawGarage` (`main.cpp:1470-1491`),
  moved into `showroom.cpp`.
- **"FITTING..."** shows over the car while `core.fitting()`. While `core.failed()`, the line
  reads "COULDN'T BUILD THIS COMBINATION".
- **Mouse:** a `Hit` list as in `lab.cpp`. Left-drag inside `layout.car` orbits; the wheel over
  the list scrolls it.
- **`update` returns FIT** when ENTER/A/FIT is pressed and the fit is allowed. That means the
  pick differs from fitted, and in career the car is at a station and every part is owned.
  - Refusal toasts are the existing strings: "DRIVE TO A REPAIR STATION TO FIT PARTS",
    "LOCKED: <part>".
  - After FIT it calls `requestClose()`.

- [ ] **Step 1: Write the failing render check** in `tests/render.cpp`, following its existing
  offscreen pattern.
  1. Create a `Showroom` on the test's renderer and `open(3, {}, color, false)`.
  2. Run `update` and `draw` for 2 s at 1/60.
  3. Read the pixels: in the centre third, at least 5% of pixels differ from the mean of the
     border region (the car is in frame).
  4. `requestClose()`, run 1 s, then draw the test's normal game frame. Its car pixel count
     must be within 10% of the same frame drawn before the showroom opened.
- [ ] **Step 2:** Run `make build/test_render`. Expected: a compile error. This is a
  Linux/Mac target; on Windows this step only proves `showroom.cpp` compiles inside
  `make windows`.
- [ ] **Step 3:** Implement `src/showroom.*`. The fade drives `renderer.setWorld(core.studio().world)`
  at black on opening, and `restoreGameWorld()` at black on closing. Clear `r.frameModels` and
  `r.carModels` as `lab.cpp:541-543` does.
- [ ] **Step 4:** Build `make windows`. Expected: no errors. On Linux/Mac CI, `test_render`
  passes.
- [ ] **Step 5:** Commit `src/showroom.h src/showroom.cpp tests/render.cpp Makefile`, message
  "Showroom: studio scene, turntable camera, panels and input".

### Task 6: `main.cpp` hooks, removing the old garage, and the phase 1 PR

**Files:**
- Modify: `src/main.cpp`:
  - `GarageUI` and `drawGarage` (:1420-1496), removed;
  - `openGarage` (:2095), the G key (:3764-3772), the menu GARAGE row (:2736) and
    `--garage` (:2033), all calling `showroom.open`;
  - the key chain (:3585-3629), removed;
  - parking (:3964), `padCtx` (:3402), and the draw at :5525 / :5833.

Steps:
- [ ] **Step 1:** Make the changes, as numbered in the spec's "Main loop integration".
  - Events go to `showroom.handleEvent` before the game's while `isOpen()`.
  - ESC closes the showroom.
  - Keep `openGarage`'s refusals: props, `spec->model < 0`, and no player car.
  - Set `sim.paused = !net.active()` on open and restore it on close.
  - **On FIT:**
    1. `garage.fitted[v] = r.mods`, then `garage.save()`;
    2. `whenVisualReady(garage.spec(v), [&]{ sim.swapPlayerVehicle(garage.spec(v)); })`.
- [ ] **Step 2:** Build `make windows build/win/run_tests.exe`. Expected: no errors and no
  references to `GarageUI` left (`grep -n GarageUI src/main.cpp` is empty).
- [ ] **Step 3:** Run the full suite:
  `timeout -k 10 3600 build/win/run_tests.exe --jobs 4`.
  - Expected: Task 0's result plus the new `showroom/*` tests passing, and no new failures.
  - Run `test_showroom_visual.exe`: PASS. Run `bench_sim`: the same hash.
- [ ] **Step 4: By hand (user, real window).** Check each of these:
  - G opens the showroom with a fade, and the car is centred on the pad;
  - drag and the wheel orbit and zoom, and it auto-rotates after 4 s;
  - A/D rebuilds the car, and a lift visibly drops onto its springs;
  - ESC returns to the game with the car intact.
  - Measure: showroom FPS against game FPS at the same spot (F3), and pick-to-visible latency
    (target 300 ms, stopwatch on 10 picks).
  - Also check multiplayer: host plus one client, open the showroom on the host, and the client
    still sees the host's truck parked.
- [ ] **Step 5:** Commit `src/main.cpp`, message "Garage opens the showroom; old overlay
  removed".
- [ ] **Step 6:** Rebase onto `origin/main`, push `showroom/1-studio` to `fork`, and open the
  PR. Its body includes:
  - the suite result against the baseline;
  - the `bench_sim` hash;
  - the by-hand numbers, or "not measured" for any that weren't.

  Bind it with `ccd_pr`.

---

## Phase 2: part picker (branch `showroom/2-picker`, from `showroom/1-studio`)

Spec: "Phase 2: part picker". No change to `mods.cpp`, ModSet, `garage.txt`, career keys, the
network packet or physics in this phase. If a task seems to need one, stop and ask.

The Windows bar from here on is Phase 1's final result: 984/994, with the 10 known failures
(PR #5), plus the new tests passing. `bench_sim` stays `1147d5f0f5f7730d`.

### Task 7: Thumbnail spike (decides the capture path, or the text-tile fallback)

**Output:** a decision and its numbers, written to the SDD ledger (`progress.md`) and to the
group notes folder. Spike code is throwaway: it is not committed unless Task 10 adopts it as
is.

- [ ] **Step 1:** Create `showroom/2-picker` from the commit that holds this revised plan
  (`fork/claude/showroom-picker-design-9e0cc5`, which sits on `showroom/1-studio`). Build, run
  `showroom/` and `bench_sim`, and record the hash (expected `1147d5f0f5f7730d`).
- [ ] **Step 2:** Read how a car is drawn: `Renderer::drawScene`, the `CarSlot` binding by car
  index, the shadow and multisample targets (`msFbo`), and `iconTexture`'s offscreen pass
  (`render.cpp:5348`). Note what a second draw into another target would share or clobber
  (shadow cascades, bloom, `frameModels`).
- [ ] **Step 3:** Prototype one capture: catalog 3's stock car, WHOLE framing, into a 256x256
  multisampled target, resolved to a mipmapped texture. Try both:
  - **(a)** a second car in the studio `Sim`, parked well off the turntable and outside the
    studio camera's frustum, drawn with a fixed capture camera;
  - **(b)** a small capture `Sim` of its own (`studioWorld()`, one car), drawn the same way.
- [ ] **Step 4: Measure.**
  - Put a `GL_TIME_ELAPSED` query around one capture and log it to stderr.
  - On Windows, agents don't open windows. Ask the user for one run with `--garage` in a real
    window, or take the number from `tests/render.cpp` on Linux/Mac.
  - Run `showroom/isolation` and `showroom/memory` with captures running (a test hook that
    triggers one capture per update): both must pass.
- [ ] **Step 5: Decide,** against the spec's budget: at most 5 ms per capture on the RTX 2060,
  and isolation passes.
  - Pick (a) or (b), whichever meets it with less code. Record why.
  - If neither does, record **text-tile fallback**: Task 10 is skipped, and the pictures become
    a follow-up task after the phase 2 PR. Tell the user.
  - Don't run a second spike.

### Task 8: `PartTree` and `PickerNav` (GL-free)

**Files:**
- Create: `src/partpicker.h`, `src/partpicker.cpp` (added to `PHYS`, so `run_tests` links it)
- Test: `tests/showroom.cpp`

**Interfaces:**
- Consumes: `modOptions`, `modOwnCount`, `vehicleListed` (`mods.h`, `vehicles.h`).
- Produces:

```cpp
enum PickerTab { TAB_ENGINE, TAB_SUSP, TAB_TRANS, TAB_WHEELS, TAB_TIRES, TAB_BUILDS, TAB_COUNT };  // first five in ModSlot order
struct PartGroup { int donor, first, count; };  // donor: catalog vehicle (-1: a tread); options first .. first + count - 1
struct PartTree {
    std::vector<PartGroup> groups[MOD_SLOTS];
    void build(int vehicle);
    int groupOf(int slot, int option) const;  // -1 if out of range
};
class PickerNav {
public:
    void open(int vehicle, const ModSet& fitted, int buildCount);
    // Each returns true if the pick changed.
    bool tab(int d);                  // Q/E, LB/RB: wraps; level 1, cursor on the pick's group (BUILDS: on YOUR PICK)
    bool move(int dx, int dy);        // level 1: the 2-column grid; level 2: rows (dy only)
    bool enter();                     // level 1: open a multi-option group; level 2: back to the tiles
    bool back(bool& close);           // level 2: back to the tiles; level 1: close = true
    bool clickTile(int i), clickRow(int i);
    const ModSet& pick() const;
    int tabIndex() const, level() const, cursor() const, scroll() const;
    const PartTree& tree() const;
    // BUILDS: the nav asks for a resolved pick through this (set by the caller; Task 9).
    std::function<ModSet(int build, const ModSet& base)> resolve;
};
```

Pinned behaviour: the spec's "Level 1: tiles", "Level 2: variants" and "Live picking",
exactly. YOUR PICK is cursor 0 of TAB_BUILDS, and builds are cursor 1 onwards. The column count
comes from the caller (`ShowroomLayout::tileCols`, Task 11); default 2.

- [ ] **Step 1: Write the failing test** `showroom/picker-tree`. For every catalog vehicle and
  each slot:
  - every option `0 .. modOptions().size() - 1` lies in exactly one group;
  - group 0 is `{vehicle, 0, modOwnCount}`;
  - the other groups' donors rise in catalog order and skip `vehicle`;
  - in TIRES every group has `count == 1`.
- [ ] **Step 2: Write the failing test** `showroom/picker-nav`. On catalog 3 with fitted
  `{0,0,0,0,0}`:
  - on open, `tabIndex() == TAB_ENGINE`, level 1, and the cursor is on group 0;
  - moving onto the Jesko's tile picks the Jesko's first engine option; `enter()` and one row
    down picks its second; `back()` keeps it; moving away and back to the Jesko's tile picks
    the remembered second option;
  - `tab(1)` lands on the SUSP group holding the pick; a tab change never changes the pick;
  - `enter()` on a one-option tile returns false and stays at level 1;
  - `back(close)` at level 1 sets `close` and changes nothing;
  - fitted `{5,0,0,0,0}` (any non-own option): on open the cursor is on that option's group.
- [ ] **Step 3:** Run `run_tests.exe "showroom/picker"`. Expected: compile errors.
- [ ] **Step 4:** Implement `partpicker.*`.
- [ ] **Step 5:** Run `showroom/`: all PASS. Run `bench_sim`: the same hash.
- [ ] **Step 6:** Commit `src/partpicker.h src/partpicker.cpp tests/showroom.cpp Makefile`,
  message "Part picker: parts grouped by donor, and the tile/row navigation".

### Task 9: Builds and `ShowroomCore::setPicks`

**Files:**
- Create: `src/builds.h`, `src/builds.cpp` (added to `PHYS`)
- Modify: `src/showroomcore.h/.cpp`, `src/partpicker.cpp` (BUILDS through `resolve`)
- Test: `tests/showroom.cpp`

**Interfaces:**

```cpp
struct Build { const char* name; const char* detail; const char* parts[MOD_SLOTS]; };  // part keys (modPartKey); nullptr: not named
const std::vector<Build>& builds();
bool resolveBuild(int vehicle, const Build& b, ModSet& io);  // false (and one stderr line) if a key doesn't resolve
// ShowroomCore:
void setPicks(const ModSet& m);  // clamped; one debounce restart. Neighbour prebuild only if exactly one slot changed.
```

- [ ] **Step 1: Choose the parts.** List every part key with its title: a throwaway loop over
  `modOptions(v, k)` printing `modPartKey` and `modPartTitle`, kept out of the commit. Pick
  the five themes' parts from the spec ("BUILDS"). Engines and gearboxes are named by donor
  variant, for example the Jesko's race-fuel engine. Write them into `builds.cpp`, one comment
  per build saying why each part.
- [ ] **Step 2: Write the failing test** `showroom/builds`:
  - every build resolves on every catalog vehicle;
  - after `applyMods`, the spec has the build's engine name, gearbox name and tire type where
    the build names them;
  - the vehicle's own tread resolves to option 0;
  - a build over a base pick leaves the unnamed slots as the base;
  - through `PickerNav`: entering BUILDS changes nothing; moving onto a build picks it; moving
    back to YOUR PICK restores the base exactly; leaving the tab keeps the build, and the other
    tabs' remembered options follow it (Review Focus 6);
  - `setPicks` with an out-of-range pick clamps as `setPick` does.
- [ ] **Step 3:** Run it. Expected: compile errors.
- [ ] **Step 4:** Implement it.
- [ ] **Step 5:** Run `showroom/`: all PASS. Run `bench_sim`: the same hash.
- [ ] **Step 6:** Commit `src/builds.h src/builds.cpp src/showroomcore.h src/showroomcore.cpp src/partpicker.cpp tests/showroom.cpp Makefile`,
  message "Showroom builds: curated part combinations, and setPicks".

### Task 10: Donor thumbnails (skipped under the text-tile fallback)

**Files:**
- Modify: `src/render.h/.cpp` (the capture path Task 7 chose), `src/showroom.cpp` (queues
  captures)
- Modify: `tests/render.cpp` (one check)

**Interfaces:**

```cpp
enum class ThumbFrame { WHOLE, WHEEL };
GLuint Renderer::carThumb(int vehicle, ThumbFrame f);  // 0 until captured; captures at most one per frame, cached for the process
```

Pinned values:
- 256x256, 4x multisampled, resolved and mipmapped, sRGB, as `iconTexture`.
- **WHOLE:** front three-quarter, slightly high (yaw 0.6, pitch 0.25), the car filling 85% of
  the width.
- **WHEEL:** the front-left wheel's centre, at 2.4 × its radius across.
- Only listed donors (`vehicleListed`) are captured. Unlisted donors stay text.
- Captures use the catalog spec (`vehicleCatalog()[v]`), which is permanent. Never a showroom
  spec.

- [ ] **Step 1: Write the failing render check** in `tests/render.cpp`, after the phase 1 check:
  open the showroom, run until every listed donor has both thumbnails (cap 10 s), then check
  each texture is non-empty (at least 10% of pixels differ from the clear colour). The preview
  car's pixel count in the main view must be within 2% of the count before the captures.
- [ ] **Step 2:** Implement it, from Task 7's chosen prototype.
- [ ] **Step 3:** Build `make windows build/win/run_tests.exe`. Run `showroom/` (isolation
  and memory included) and `bench_sim`. `test_render` runs on Linux/Mac.
- [ ] **Step 4:** Commit `src/render.h src/render.cpp src/showroom.cpp tests/render.cpp`,
  message "Showroom: donor thumbnails from the game's own car renderer".

### Task 11: Tiles, rows and input in `Showroom`, and the phase 2 PR

**Files:**
- Modify: `src/showroom.h/.cpp` (`drawLeft`, `handleEvent`, `pad`, the bottom bar's hints)
- Modify: `src/showroomcore.h/.cpp` (`ShowroomLayout`: `tileCols`, `tileRows`)
- Test: `tests/showroom.cpp` (`showroom/layout`)

Pinned values:
- **Layout:** the spec's "Layout". Tiles are `(left.w − 3u·pad) / 2` wide, a 4:3 picture over
  two caption lines. `tileRows` is the whole tile rows that fit under the tabs (and the
  breadcrumb, at level 2).
- **Tile:** the spec's "Level 1: tiles". The picture is `carThumb` (WHOLE, or WHEEL on WHEELS),
  the tread swatch on TIRES, or none (text only). Colours are those under Global Constraints:
  amber titles, grey labels, the 0.07 white highlight, and a 1u amber outline on the pick's
  group.
- **Rows:** the phase 1 row drawing, with the "WHO: " prefix dropped (`name.substr(find(": ") + 2)`
  when `fromVehicle != vehicle`).
- **Input:** the spec's phase 2 table.
  - `pad()` reads LB/RB edges for tabs (as now) and X's edge for FIT.
  - A, B and the D-pad arrive as RETURN, ESC and arrows through menu mode.
  - ESC or B at level 1 calls `requestClose()`; G always does.
- **Every pick change:** `core->setPicks(nav.pick())`.
- **Hints:** keys "Q E TAB   ARROWS MOVE   ENTER OPEN   ESC BACK   F FIT"; pad
  "LB RB TAB   D-PAD MOVE   A OPEN   B BACK   X FIT", using `pad::buttonName`.

- [ ] **Step 1: Write the failing test** `showroom/layout` additions: at 1024x600,
  `tileRows >= 2`; at 1280x720, `tileRows >= 3`; at 3840x2160, every tile rect lies inside
  `left`.
- [ ] **Step 2:** Run it. Expected: a compile error.
- [ ] **Step 3:** Implement it.
- [ ] **Step 4:** Build. Run the full suite, `test_showroom_visual` and `bench_sim`. Same bar
  as Task 6 Step 3.
- [ ] **Step 5: By hand (user, real window).** Check:
  - ENGINE shows donor tiles with pictures (or text, under the fallback);
  - opening the Jesko shows its variants, and each rebuilds the car;
  - backing out keeps the pick;
  - BUILDS previews a whole car, and YOUR PICK restores;
  - ENTER on a tile never fits; F / X fits;
  - the pad works throughout.
  - Measure the showroom FPS while thumbnails are being captured (on first open), and after.
- [ ] **Step 6:** Commit `src/showroom.h src/showroom.cpp src/showroomcore.h src/showroomcore.cpp tests/showroom.cpp`,
  message "Showroom: part tiles, variant rows, builds tab; FIT on F / X".
- [ ] **Step 7:** Rebase onto `showroom/1-studio` (or `origin/main`, if phase 1 has merged).
  Push `showroom/2-picker` to `fork` and open the PR, with the same body contents as Task 6,
  plus the builds' parts list for the user to review. Bind it with `ccd_pr`.

---

## Phase 3: stats and career (branch `showroom/3-stats`, from `showroom/2-picker`)

### Task 12: `vehiclestats`

**Files:**
- Create: `src/vehiclestats.h`, `src/vehiclestats.cpp` (added to `PHYS`)
- Modify: `tests/scenarios.cpp:760-788` (call `measureStance`)
- Test: `tests/showroom.cpp`

**Interfaces:**
- Produces: the spec's `VehicleStats`, plus:

```cpp
double carMass(const Car& c);                        // sum of c.nodes[i].mass (check the field name in softbody.h:378)
VehicleStats specStats(const VehicleSpec& vs, double kg);
double topSpeedEstimate(const VehicleSpec& vs, double kg);  // km/h
void measureStance(const Car& c, const World& w, VehicleStats& out);  // sets clearance, approach, departure, breakover, measured = true
```

Pinned values:
- `measureStance`: the `structure/clearance` loop moved verbatim. The same 0.04 m node radius,
  the same 50-step bisection, and the same ramp formula.
- `topSpeedEstimate`: the lower of the power-limited and gear-limited speeds.
  - **Power-limited:** solve `eta·P = ½·1.2·dragArea·v³ + Crr·kg·9.81·v` by bisection on
    [1, 200] m/s, with `P = peakPower(vs) × 745.7`.
  - **Gear-limited:** `vs.redline × 2π × vs.wheelRadius / (60 × vs.gears.back() × vs.finalDrive)`.
  - `eta` and `Crr` are single named constants, commented as estimates. Start with
    `eta = 0.85` and `Crr = 0.015`.

- [ ] **Step 1: Record the reference.** Before any change, run
  `run_tests.exe "structure/clearance" -v` and save the printed metrics, all vehicles, to the
  scratchpad.
- [ ] **Step 2: Write the failing tests:**
  - `showroom/stats-engine`: for every catalog vehicle, `specStats(vs, 1500).hp == peakPower(vs, &r)`
    and the same for torque and the rpms.
  - `showroom/top-speed-estimate`: for every catalog vehicle, check
    `|topSpeedEstimate(vs, carMass(car)) − mid| ≤ 0.10·mid`.
    - `mid` is the midpoint of that vehicle's `freeway/top-speed` range. Copy the
      `expect[]` table from `tests/freeway.cpp:36` into a shared header `tests/topspeed.h`, and
      include it from both files.
- [ ] **Step 3:** Run them. Expected: compile errors.
- [ ] **Step 4:** Implement `vehiclestats.*` and switch `structure/clearance` to `measureStance`.
- [ ] **Step 5:** Run `structure/clearance -v`: the metrics are identical to Step 1, character
  for character. Run `showroom/stats-engine`: PASS.
- [ ] **Step 6:** Run `showroom/top-speed-estimate`. Fit `eta` and `Crr` once, as globals, until
  all pass.
  - **If no single pair passes every vehicle:** keep the best pair, leave the failing vehicles
    failing, and report them to the user with their estimate and range. Don't loosen the 10%,
    special-case a vehicle, or drop it.
- [ ] **Step 7:** Commit `src/vehiclestats.* tests/scenarios.cpp tests/freeway.cpp tests/topspeed.h tests/showroom.cpp Makefile`,
  message "vehiclestats: mass, stance and top-speed estimate; clearance test uses it".

### Task 13: Stats in `ShowroomCore`

**Files:**
- Modify: `src/showroomcore.h/.cpp`
- Test: `tests/showroom.cpp`

**Interfaces:**
- Consumes: Task 12.
- Produces, on `ShowroomCore`:
  ```cpp
  static constexpr double kSettle = 1.0;
  const VehicleStats* stats(const ModSet& m) const;  // null until that pick's car is built
  ```
  The stats cache is keyed by `(vehicle, pick[])`, lives for the process, and survives
  `close()`.
  - On swap: `kg = carMass`, `specStats(spec, kg)`.
  - `kSettle` s after the swap: `measureStance(*preview, studio.world, s)`.
  - The fitted pick's stats are the before column.

- [ ] **Step 1: Write the failing test** `showroom/stats`:
  1. Open catalog 3 (stock) with the fake cache set to ready immediately.
  2. Before the swap, `stats(fitted)` is null. After the swap, it is non-null with
     `measured == false` and `kg > 500`. After 1.0 s more, `measured == true`.
  3. **The 1 s settle is enough:** in a separate `Ctx`, add catalog 3 with the handbrake on.
     Run 1 s and measure, then run to 2 s and measure. Clearance must match within 1 mm and each
     angle within 0.1°.
     - If this fails, set `kSettle = 2.0` (the spec allows it) and re-run. Don't loosen the
       tolerance.
- [ ] **Step 2:** Run it. Expected: a compile error.
- [ ] **Step 3:** Implement it.
- [ ] **Step 4:** Run `showroom/`: all PASS.
- [ ] **Step 5:** Commit, message "ShowroomCore: before/after stats".

### Task 14: Career cart (logic)

**Files:**
- Modify: `src/showroomcore.h/.cpp`
- Test: `tests/showroom.cpp`

**Interfaces:**
- Consumes: `Career::owns/price/buy`, `modPartTitle`, `modPartKey`.
- Produces:

```cpp
struct Cart { int total = 0; std::vector<int> slots; std::string notForSale; };  // unowned picked parts, by slot
Cart cartFor(const Career& c, int vehicle, const ModSet& pick);
enum class FitCheck { SAME, OK, NEED_STATION, NEED_BUY, NOT_FOR_SALE, NEED_CASH };
FitCheck canFit(const Career& c, int vehicle, const ModSet& fitted, const ModSet& pick, bool atStation);
bool buyCart(Career& c, int vehicle, const ModSet& pick, std::string& why);  // all or nothing
struct BuyConfirm {  // the second F (X) of buy-and-fit
    static constexpr double kWindow = 3.0;
    void ask(int vehicle, const ModSet& pick, int total);
    bool confirms(int vehicle, const ModSet& pick, int total) const;  // same cart, inside the window
    void update(double dt);
    void cancel();
    bool pending() const;
};
```

- [ ] **Step 1: Write the failing test** `showroom/career`. Use a `Career` with `on = true`,
  `cash = 10000` and nothing owned.
  - A locked engine pick: `canFit(... atStation=false) == NEED_STATION`;
    `canFit(... true) == NEED_BUY`.
  - `buyCart` spends exactly `cartFor().total`, and afterwards `canFit == OK`.
  - With cash 1 less than the total:
    - `canFit(... true) == NEED_CASH`. `NEED_CASH` takes the place of `NEED_BUY` whenever cash
      is below the cart total.
    - `buyCart` is false, cash is unchanged, and nothing is owned.
  - A tier-3 part (find one with `Career::price == 0`): `cartFor().notForSale` is non-empty and
    `canFit == NOT_FOR_SALE`.
  - Sandbox (`on = false`): `cartFor().total == 0` and `canFit == OK`.
  - Pick equal to fitted: `SAME`.
  - **Review Focus 5:**
    1. `ask(3, pickA, t)`; `confirms(3, pickB, t')` is false.
    2. After `cancel()`, `confirms(3, pickA, t)` is false.
    3. `ask` again, then `update(3.1)`: false.
- [ ] **Step 2:** Run it. Expected: a compile error.
- [ ] **Step 3:** Implement it.
- [ ] **Step 4:** Run `showroom/career`: PASS. Run `career/`: no change from baseline.
- [ ] **Step 5:** Commit, message "Showroom cart: prices, fit checks, all-or-nothing buy".

### Task 15: Stats panel and career UI, and the phase 3 PR

**Files:**
- Modify: `src/showroom.cpp`

Pinned values:
- **Rows,** in this order:
  - POWER (hp @ rpm), TORQUE (Nm @ rpm), WEIGHT (kg), POWER/WEIGHT (hp/t),
    TOP SPEED EST. (km/h);
  - then OFF-ROAD: CLEARANCE (cm), APPROACH, DEPARTURE, BREAKOVER (°);
  - then DETAILS, collapsed: the phase 1 lines.
- **Columns:** fitted, picked, delta.
  - The delta is green `(0.45, 0.85, 0.45)` when better and red `(0.95, 0.4, 0.35)` when worse;
    lower is better only for weight.
  - Equal values show no delta.
  - Unmeasured rows show "..." with the previous value at 0.5 alpha.
- **Bars:** a thin bar per row, scaled to that stat's maximum over the stock catalog, computed
  on first open from the catalog's own `stats()`. The fitted value is a tick.
- **Variant rows:** blank for a factory part, "OWNED", "$price", or "FIND IT". Sandbox shows
  none.
- **Part tiles:** the same, for the variant the tile would preview.
- **Build tiles:** the total of their unowned parts (`cartFor` on the build's resolved pick),
  "OWNED" when the total is 0, or "FIND IT" when `notForSale` is set.
- **Bottom bar:** the cart total, cash, and "CAN'T FIT: <part> NOT FOR SALE" when that applies.
- **F / X with `canFit == NEED_BUY` at a station:** `BuyConfirm::ask`, and the toast "BUY N
  PARTS FOR $X? F TO CONFIRM" ("X TO CONFIRM" on a pad).
  - A second F / X that `confirms`: `buyCart`, `career.save()`, FIT.
  - `NEED_CASH` toasts "NEED $X MORE".
  - Any `setPicks` cancels the confirm.
- **B / Y / BUY** buys the highlighted part through `Career::buy`: the row's part, or the
  variant a part tile would preview. Not offered on build tiles.

Steps:
- [ ] **Step 1:** Implement it. Build with `make windows build/win/run_tests.exe`. Expected: no
  errors.
- [ ] **Step 2:** Run the full suite plus `test_showroom_visual` plus `bench_sim`. Same bar as
  Task 6 Step 3.
- [ ] **Step 3: By hand (user).** Check:
  - stats fill in, with the off-road numbers arriving about 1 s later;
  - the deltas are coloured;
  - in career, a locked part previews with its stats but won't fit away from a station;
  - buy-and-fit at a station needs two F presses and charges the cart total;
  - a build tile's price matches what buy-and-fit then charges.
- [ ] **Step 4:** Commit, message "Showroom: before/after stats panel and career prices".
- [ ] **Step 5:** Rebase onto the updated `showroom/2-picker` (or `origin/main` if that merged).
  Push and open the PR, with the same body contents as Task 6.

---

## Phase 4: audio preview (branch `showroom/4-audio`, from `showroom/3-stats`)

### Task 16: Rev throttle and auto-blip in `ShowroomCore`

**Files:**
- Modify: `src/showroomcore.h/.cpp`
- Test: `tests/showroom.cpp`

**Interfaces:**
- Produces, on `ShowroomCore`:
  ```cpp
  void setRev(bool held);       // R / RT
  double throttle() const;      // what the preview car's throttle is set to each update
  ```
  - **Auto-blip:** when a pick whose `MOD_ENGINE` option differs from the shown car's finishes
    building, and the rev isn't held: ramp 0→1 over 0.15 s, hold 0.5 s, back to 0.
  - **Preview car each update:** `throttle = throttle()`, `gear = 0`, `handbrake = 1`.

- [ ] **Step 1: Write the failing test** `showroom/rev`. For catalog vehicles 0, 3 and 6:
  1. Show stock, then hold the rev. `preview()->rpm` passes `0.9 × redline` within 3 s.
  2. Release: back within 10% of `idleRpm` within 3 s.
  3. Change the engine pick, and run updates until shown: `throttle()` reaches 1.0 within 0.2 s
     of the swap and is back to 0 by 0.9 s.
- [ ] **Step 2:** Run it. Expected: a compile error.
- [ ] **Step 3:** Implement it.
  - **If the neutral free-rev doesn't reach 90% of redline** (the drivetrain limits it), stop
    and report what it reaches. Don't fake the rpm.
- [ ] **Step 4:** Run `showroom/`: PASS. Run `bench_sim`: the same hash.
- [ ] **Step 5:** Commit, message "Showroom: hold-to-rev and auto-blip on engine change".

### Task 17: Audio feed and mix, and the phase 4 PR

**Files:**
- Modify: `src/main.cpp:4683-4700` (the audio block)
- Modify: `tests/audio.cpp` (one check)

- [ ] **Step 1: Write the failing audio check** `voice swap at full revs`, in `tests/audio.cpp`'s
  offline style.
  1. Render 1 s of catalog 0's engine at 95% of redline with load 1.
  2. Switch `setVoice`/`setRecording` to catalog 3, and render 1 s more.
  3. The largest sample-to-sample step across the switch must be ≤ the largest step in a 2 s
     render of catalog 3 alone revving idle → 95% of redline.
- [ ] **Step 2:** Run `make build/win/test_audio.exe && build/win/test_audio.exe`. Expected:
  FAIL, or PASS if the crossfade already holds. Either way, record the numbers.
  - **If it fails,** fix the click in `EngineVoice`'s identity change (`engine_samples.cpp:204-207`)
    with a short crossfade of the old and new voices, then re-run until it passes.
- [ ] **Step 3: Feed the preview car while the showroom is open.**
  - Use `const Car& car = showroom.previewCar() ? *showroom.previewCar() : lastPreview` for the
    engine calls. The calls are the same: `setEngine(rpm, throttle × tcCut)` without the
    `timeScale` factor, `setVoice`, `setRecording`, and `setBoost`.
  - `setTires(0)`, `setSpeed(0)`.
  - Music: `setMusic(map, settings.music × 0.5, biome)`.
- [ ] **Step 4:** Run the full suite, `test_audio`, `test_showroom_visual` and `bench_sim`.
  Same bar as Task 6.
- [ ] **Step 5: By hand (user).** Check:
  - the picked engine idles in the showroom;
  - R or RT revs it to the limiter and back;
  - a new engine blips on its own once it appears;
  - there's no click when swapping;
  - the music is quieter;
  - after ESC, the game's own engine is back.
- [ ] **Step 6:** Commit `src/main.cpp tests/audio.cpp` (plus `src/engine_samples.cpp` if
  changed), message "Showroom: engine preview audio, music ducked".
- [ ] **Step 7:** Rebase, push `showroom/4-audio` and open the PR, with the same body contents
  as Task 6.
