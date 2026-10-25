# Career Phase 1: Profiles and World Saves — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Career becomes a list of named worlds (difficulty, mode, map, starting vehicle) plus a per-player profile, and a career world remembers where every one of your vehicles was left and in what condition.

**Architecture:** The existing `Career` struct (`src/career.*`) becomes the **profile** (saved to `profile.txt`, migrated from `career.txt`) and gains a list of owned vehicles. A new `src/careerworld.*` holds `WorldSave` (settings + vehicles left in that world), saved atomically per world. A new `src/carsave.*` captures and restores one car's condition (pose, fuel, battery, engine, wheels, dents). The main menu's CAREER button opens a world list and a New World page in `src/mainmenu.*`; `src/main.cpp` loads, spawns and autosaves.

**Tech Stack:** C++17, the in-tree test harness (`tests/harness.h`, `SCENARIO`), `writeFileAtomic` (`src/platform.h`).

**Spec:** `docs/superpowers/specs/2026-10-25-career-mode-design.md` (sections 1 and 2 for difficulty/start cash, section 6 for starting vehicle).

## Global Constraints

- Build/test: `nice -n 19 make -j3 crumplezone build/run_tests`; tests `nice -n 19 timeout -k 10 3000 ./build/run_tests --jobs 3 [filter]`. Never `--update-baseline`.
- Physics fingerprint `./build/bench_sim 12 separated 4000` must stay `da17a95524fab674`.
- Difficulty is chosen once at world creation: Light / Medium / Hardcore. Start cash: Light $2,000, Medium $1,000, Hardcore $500.
- Mode: Personal (default) or Shared crew (stored only in this phase; behaviour is phase 10).
- Starting vehicle: any stock vehicle except "KOENIGSEGG JESKO".
- Saves: `writeFileAtomic(path, data, &err, /*keepBak=*/true)`; load falls back to `path + ".bak"` when the main file is missing or unparsable.
- Paths: profile `configDir() + "/profile.txt"`; worlds `configDir() + "/worlds/<slug>/world.txt"`; slug = name lower-cased, `[a-z0-9-]` only, other chars → `-`, max 32 chars, non-empty (fallback `world`), de-duplicated with `-2`, `-3`…
- Sandbox behaviour unchanged (no world, `career.on == false`).
- Windows build must compile (`make build/win/CrumpleZone/crumplezone.exe`); no POSIX-only calls without guards (use `platform.h` helpers: `makeDir`, `configDir`, `writeFileAtomic`; add `listDir`/`removeTree` there if missing, with Windows branches).
- Text file format style: one `key value` per line, unknown keys ignored (forward compatible), like `Career::load`.

## Review Focus

- **Old `career.txt` and no `profile.txt`:** first run migrates cash/parts/caches/claims/stock into the profile and leaves `career.txt` in place. Test in Task 2.
- **Truncated or garbage `world.txt` (crash mid-write, hand edit):** load uses `.bak`; if both bad, the world shows in the list as "DAMAGED SAVE" and can't be continued, nothing crashes. Test in Task 3.
- **A saved vehicle whose catalog index no longer exists or whose dent count doesn't match the vehicle (game update changed the model):** that vehicle is restored undamaged at its pose, not skipped and not crashed. Test in Task 4.
- **Two worlds with names that slug the same ("My World", "my world!")** get distinct folders. Test in Task 3.
- **Quitting mid-drive / Alt-F4-ish exit paths** (menu quit, window close, map change) all write the save; autosave runs every 180 s. Test the save trigger function in Task 6.

---

### Task 1: Difficulty and world settings types

**Files:**
- Create: `src/careerworld.h`, `src/careerworld.cpp`
- Modify: `Makefile` (add `src/careerworld.cpp` to `PHYS`, `tests/careerworld.cpp` to `SCEN`)
- Test: `tests/careerworld.cpp`

**Interfaces:**
- Produces:
  - `enum Difficulty { DIFF_LIGHT, DIFF_MEDIUM, DIFF_HARDCORE, DIFF_COUNT };`
  - `enum WorldMode { MODE_PERSONAL, MODE_CREW };`
  - `const char* difficultyName(Difficulty)` → `"LIGHT"`, `"MEDIUM"`, `"HARDCORE"`; `const char* modeName(WorldMode)` → `"PERSONAL"`, `"SHARED CREW"`
  - `int startCash(Difficulty)` → 2000 / 1000 / 500
  - `bool startableVehicle(int catalogIndex)` → false for the Jesko (by `vehicleCatalog()[i].name == "KOENIGSEGG JESKO"`), true for other listed vehicles
  - `std::string worldSlug(const std::string& name, const std::vector<std::string>& taken)`

- [ ] **Step 1: Write failing tests** `career/world-settings`:
  - `startCash(DIFF_LIGHT)==2000`, `startCash(DIFF_MEDIUM)==1000`, `startCash(DIFF_HARDCORE)==500`
  - `!startableVehicle(<index of KOENIGSEGG JESKO>)`; every other `listedVehicles()` entry is startable
  - `worldSlug("My World", {}) == "my-world"`; `worldSlug("my world!", {"my-world"}) == "my-world-2"`; `worldSlug("", {}) == "world"`; a 50-char name gives ≤ 32 chars
- [ ] **Step 2:** `./build/run_tests career/world-settings` → FAIL (not defined).
- [ ] **Step 3:** Implement in `src/careerworld.*`.
- [ ] **Step 4:** Run → PASS.
- [ ] **Step 5:** Commit `Career worlds: difficulty, mode, start cash, slugs`.

### Task 2: The profile (Career → profile.txt, migration, owned vehicles)

**Files:**
- Modify: `src/career.h`, `src/career.cpp`
- Test: `tests/career.cpp` (extend)

**Interfaces:**
- Consumes: `Difficulty`, `startCash` (Task 1)
- Produces:
  - `struct OwnedVehicle { int vehicle = 0; ModSet mods; std::string world; std::string condition; };` (`condition`: the `carsave` text from Task 4, "" = factory fresh; `world`: the world slug it was left in)
  - `Career::vehicles` (`std::vector<OwnedVehicle>`)
  - `static std::string Career::profilePath()` → `configDir() + "/profile.txt"`
  - `bool Career::loadProfile()` — loads `profilePath()`, else `.bak`, else migrates `defaultPath()` (career.txt) and returns true; false only if nothing exists
  - `bool Career::saveProfile() const` — atomic, keeps `.bak`
  - `void Career::startFresh(Difficulty d)` — cash = `startCash(d)`, plus today's starting anchor/tool set (keep the existing `startFresh()` as `startFresh(DIFF_MEDIUM)`)
  - Profile lines added: `veh <index> <world-slug> <mods-text>` and `cond <n> <condition-text>` (n = position in `vehicles`)

- [ ] **Step 1: Write failing tests:**
  - `career/profile-migrate`: write a career.txt (cash 4321, `own TIRES:RACE SLICK`, `cache 3:WALL TOP`) to a temp config dir (set `CZ_CONFIG_DIR` or the existing override the tests use; add one to `configDir()` if none exists) with no profile.txt → `loadProfile()` true, cash 4321, owns the slick, cache opened; `saveProfile()` writes profile.txt; career.txt untouched.
  - `career/profile-roundtrip`: two owned vehicles with mods, worlds and condition strings → save, load into a fresh Career → equal fields.
  - `career/profile-bak`: corrupt profile.txt (truncate to 5 bytes of garbage) with a good .bak → load gives the .bak's cash.
  - `career/start-cash`: `startFresh(DIFF_LIGHT).cash == 2000`, etc.
- [ ] **Step 2:** Run `career/` → the new ones FAIL.
- [ ] **Step 3:** Implement; reuse the existing line parser for the shared keys.
- [ ] **Step 4:** Run `career/` → all PASS (old career tests still pass).
- [ ] **Step 5:** Commit `Career profile: profile.txt, migration from career.txt, owned vehicles`.

### Task 3: World save files

**Files:**
- Modify: `src/careerworld.h`, `src/careerworld.cpp`, `src/platform.h/.cpp` (add `listDir(dir) -> std::vector<std::string>` and `removeTree(dir) -> bool` if missing, with Windows branches)
- Test: `tests/careerworld.cpp`

**Interfaces:**
- Produces:
  - `struct LeftVehicle { std::string owner; int vehicle = 0; std::string condition; };` (owner = player name; condition = carsave text)
  - `struct WorldSave { std::string name, slug; int map = 0; Difficulty diff = DIFF_MEDIUM; WorldMode mode = MODE_PERSONAL; std::string build; std::vector<LeftVehicle> left; std::set<std::string> opened; double playedSeconds = 0; bool damaged = false; };`
  - `std::string worldsDir()` → `configDir() + "/worlds"`
  - `bool saveWorld(const WorldSave&)`, `bool loadWorld(const std::string& slug, WorldSave&)` (falls back to `.bak`; sets `damaged` and returns false if both fail)
  - `std::vector<WorldSave> listWorlds()` — every folder in `worldsDir()`, damaged ones included, most recently saved first (by world.txt modification time)
  - `bool deleteWorld(const std::string& slug)`
  - `WorldSave newWorld(const std::string& name, int map, Difficulty, WorldMode, const std::vector<WorldSave>& existing)` (fills slug and build = `kRelease`)

- [ ] **Step 1: Write failing tests:**
  - `career/world-save`: save a world with 2 left vehicles and 2 opened caches → `loadWorld` equals; `listWorlds()` contains it.
  - `career/world-damaged`: overwrite world.txt with garbage and delete .bak → `loadWorld` false, `listWorlds()` entry has `damaged == true`, no crash; with a good .bak → loads the .bak.
  - `career/world-slugs`: `newWorld("My World")` then `newWorld("my world!")` → different slugs, both saved and listed.
  - `career/world-delete`: delete removes the folder; list no longer has it.
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run → PASS.
- [ ] **Step 5:** Commit `Career worlds: save, load (.bak fallback), list, delete`.

### Task 4: Car condition capture/restore

**Files:**
- Create: `src/carsave.h`, `src/carsave.cpp` (add to `PHYS`)
- Modify: `src/net.h/.cpp` only if needed to expose the dent encoding as a free function (e.g. move `NetSession::dentsOf` logic into `std::vector<int16_t> carDents(const Car&)` and `void applyCarDents(Car&, const std::vector<int16_t>&)`, with NetSession calling them; behaviour unchanged)
- Test: `tests/carsave.cpp` (add to `SCEN`)

**Interfaces:**
- Produces:
  - `std::string carsave::capture(const Sim&, const Car&)` — one line of text: catalog index, pose (com + orientation), fuel, jerry, battery, engineHealth, floodedHealth, hydrolocked, per-wheel state (off/flat/broken/tire wear/pressure), hood/gate state, mods, dents (base64 or hex of the int16 list)
  - `bool carsave::restore(Sim&, Car&, const std::string& text)` — car already built for that vehicle+mods at the pose; applies everything; velocities zero; returns false (and leaves the car factory-fresh at the saved pose) when the text is for another vehicle index or the dent count doesn't match the car's
  - `int carsave::vehicleOf(const std::string& text)` and `ModSet carsave::modsOf(const std::string& text)` to build the right car first

- [ ] **Step 1: Write failing tests:**
  - `carsave/roundtrip`: Blazer driven into a barrier at 40 km/h (as `damage/` tests do), flat front-left tire, fuel 23.5 L, battery 0.4 → capture → new Sim, build Blazer from `vehicleOf/modsOf`, `restore` → every node's rest position within 2 mm of the original's, same damage within 1e-6, fuel 23.5, battery 0.4, tire flat.
  - `carsave/mismatch`: text from a Blazer restored onto a Civic → false, Civic undamaged, placed at the saved pose.
  - `carsave/garbage`: `restore` with "" and with random bytes → false, no crash.
  - `carsave/no-physics-change`: fingerprint unchanged (run bench_sim in the verification step, not a test).
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement; reuse the net dent encoding so both paths stay identical.
- [ ] **Step 4:** Run `carsave/ net/` → PASS (net tests prove the refactor kept behaviour). `./build/bench_sim 12 separated 4000` → `da17a95524fab674`.
- [ ] **Step 5:** Commit `Car condition save/restore (pose, fuel, battery, engine, wheels, dents)`.

### Task 5: Main menu — world list and New World page

**Files:**
- Modify: `src/mainmenu.h`, `src/mainmenu.cpp`, `src/mainmenudraw.cpp`
- Test: `tests/mainmenu.cpp` (extend)

**Interfaces:**
- Consumes: `WorldSave`, `listWorlds`, `difficultyName`, `modeName`, `startableVehicle` (Tasks 1, 3)
- Produces:
  - `enum Page` gains `P_WORLDS, P_NEW_WORLD, P_DELETE_WORLD`
  - `enum Act` gains `A_CAREER_CONTINUE, A_CAREER_NEW`
  - `Result` gains `std::string world` (slug), `int difficulty`, `int mode`, `std::string worldName`
  - `Nav::worlds` (`std::vector<WorldSave>`, set by the game before opening)
- Behaviour:
  - Front page CAREER → `P_WORLDS`: rows = each world ("NAME — MAP — DIFFICULTY — MODE", damaged ones greyed "DAMAGED SAVE") then NEW WORLD. Enter on a world → `A_CAREER_CONTINUE` (not for damaged). Delete key / gamepad X on a world → `P_DELETE_WORLD` ("DELETE <NAME>? YES / NO").
  - `P_NEW_WORLD` rows: NAME (text entry, default "WORLD <n>"), MAP (A/D cycles listed maps), DIFFICULTY (A/D: LIGHT/MEDIUM/HARDCORE, default MEDIUM, hint shows start cash), MODE (A/D: PERSONAL/SHARED CREW), VEHICLE (A/D cycles startable vehicles only), CREATE → `A_CAREER_NEW`.
  - Esc goes back one page. Gamepad as the existing pages.

- [ ] **Step 1: Write failing tests** `mainmenu/career-worlds` (no SDL, like the existing mainmenu tests): with two worlds (one damaged) → CAREER shows 3 rows; Enter on the damaged one does nothing; Enter on the good one → `A_CAREER_CONTINUE` with its slug; NEW WORLD → set difficulty to HARDCORE, vehicle cycling never lands on the Jesko across a full cycle, CREATE → `A_CAREER_NEW` with name, map, difficulty 2, mode, vehicle; delete flow returns to the list.
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement Nav logic, then drawing in the existing main-menu style.
- [ ] **Step 4:** Run `mainmenu/` → PASS. Offscreen screenshot of P_WORLDS and P_NEW_WORLD into the scratchpad.
- [ ] **Step 5:** Commit `Main menu: career world list, new world (difficulty, mode, start vehicle), delete`.

### Task 6: Game wiring — start, spawn, autosave

**Files:**
- Modify: `src/main.cpp` (career setup near the existing `Career career;` ~line 1957, the main-menu result handling ~line 3145, quit paths ~line 2523)
- Create: `src/careersession.h/.cpp` (add to `PHYS`) for the testable logic, so main.cpp only calls it
- Test: `tests/careerworld.cpp` (extend)

**Interfaces:**
- Consumes: everything above
- Produces:
  - `struct CareerSession { Career profile; WorldSave world; double sinceSave = 0; static constexpr double kAutosaveEvery = 180; };`
  - `void careersession::begin(CareerSession&, Sim&, const WorldSave&, const std::string& playerName)` — sets `profile.on = true`; for a new world: `startFresh(diff)` only if the profile has no vehicles yet, adds the chosen vehicle to `profile.vehicles` (world = slug) and spawns it at the map's spawn; for continue: builds and `carsave::restore`s every profile vehicle in this world (and, for MODE_CREW, every `world.left` entry), the player put in the most recently driven one
  - `void careersession::capture(CareerSession&, const Sim&)` — writes each owned car's condition back into `profile.vehicles` / `world.left`
  - `bool careersession::tick(CareerSession&, const Sim&, double dt)` — returns true (and saves profile + world) when `sinceSave >= kAutosaveEvery`
  - `void careersession::end(CareerSession&, const Sim&)` — capture + save both; called from quit, return-to-menu, map change and window close
- main.cpp: `A_CAREER_NEW` → `newWorld(...)`, `saveWorld`, `begin`; `A_CAREER_CONTINUE` → `loadWorld`, `begin`; per frame `tick`; every exit path `end`. The old career.txt path stays only for migration.

- [ ] **Step 1: Write failing tests:**
  - `career/session-new`: new HARDCORE world with the Bronco → profile cash 500, one owned Bronco in this world, sim has a Bronco at spawn.
  - `career/session-continue`: start a world, drive the car 30 m and dent it, `end` → new Sim + `begin` continue → the car is back within 0.1 m of where it was left, dents match (2 mm).
  - `career/session-autosave`: `tick` with dt summing to 179 s → false; at 180 s → true and files exist.
  - `career/session-sandbox`: sandbox start creates no world files and leaves `profile.txt` untouched.
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement `careersession`, then wire main.cpp.
- [ ] **Step 4:** Run `career/ carsave/ mainmenu/` → PASS; then the full suite `--jobs 3` → only the known failures; fingerprint `da17a95524fab674`; `make build/win/CrumpleZone/crumplezone.exe` builds.
- [ ] **Step 5:** Manual smoke (offscreen ok): `SDL_VIDEODRIVER=offscreen CZ_CONFIG_DIR=<scratch> ./crumplezone --menu-career ...` or a scripted run creating a world, driving, quitting, continuing; screenshot before/after continue into the scratchpad.
- [ ] **Step 6:** Commit `Career worlds in the game: new/continue, vehicles left where they were, autosave`.
