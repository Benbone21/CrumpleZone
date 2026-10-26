# SDD ledger — plan: docs/superpowers/plans/2026-10-25-career-phase1-profiles-worlds.md
Spec: docs/superpowers/specs/2026-10-25-career-mode-design.md (read)
Branch: wip/career-spec, base 2b38c53

## Preflight scan
| Pair / task | Produces vs consumes | Finding |
|---|---|---|
| T1→T2 | Difficulty, startCash | consistent |
| T1→T3 | Difficulty, WorldMode, worldSlug → newWorld | consistent; T3 extends files T1 creates |
| T1→T5 | startableVehicle, difficultyName, modeName | consistent |
| T2↔T4 | OwnedVehicle.condition = carsave text | opaque string in T2, defined in T4: consistent |
| T3↔T4 | LeftVehicle.condition = carsave text | consistent |
| T2→T3 | config-dir override for tests (T2 adds if missing) | T3 tests rely on it: consistent order |
| T3→T5 | WorldSave, listWorlds, damaged flag | consistent |
| T1..T5→T6 | careersession consumes all | consistent |
| T1 self | tests vs code | consistent |
| T2 self | startFresh() kept as startFresh(DIFF_MEDIUM) | consistent |
| T3 self | listDir/removeTree added to platform | consistent |
| T4 self | lists "carsave/no-physics-change" as a test but says verify via bench_sim | inconsistency |
| T5 self | consistent |
| T6 self | Step 5 names a `--menu-career` flag that may not exist | inconsistency |

Ruling: T4 "no-physics-change" is a verification step (bench_sim fingerprint), not a SCENARIO — the plan text says so in Step 4 — cost if wrong: none.
Ruling: T6 Step 5 manual smoke may use any existing scripted/menu path; `--menu-career` is illustrative — cost if wrong: smoke done differently, no code impact.

## Progress
Task 1: implementer DONE_WITH_CONCERNS (observations: slug rules, no separate RED run) b07a757
Task 1: minor (deferred): slug length tests assert <= 32, not exact values
Task 1: minor (deferred): no slug tests for punctuation-only / padded / non-ASCII names
Task 1: ⚠️ Windows build + fingerprint: implementer reported Windows build OK; no physics touched — accepted
Task 1: complete (commits 2b38c53..b07a757, review clean)
Ruling: T2/T3 test config dir — add `std::string careerConfigDir()` (in careerworld.h) returning a thread_local override when set (`struct ScopedCareerDir { explicit ScopedCareerDir(std::string dir); ~ScopedCareerDir(); }`) else configDir(); profilePath(), defaultPath() migration source and worldsDir() all use it. Not XDG_CONFIG_HOME — tests run as threads in one process (run_tests.cpp pool), env vars would race — cost if wrong: one small helper to remove later.
Task 2: implementer DONE 1c25fe2; review: spec ✅, quality Needs fixes (1 Important)
Task 2: minor (deferred): career.cpp:314 clamp yields -1 when modOptions empty
Task 2: minor (deferred): loadProfile leaves half-reset object when all sources fail
Task 2: minor (deferred): career.h header comment still says career.txt
Task 2: minor (deferred): test includes order; freshDir relies on cwd; no test for missing .bak + corrupt main
Task 2: fix round 1/5 (1 addressed, 0 open; commits 1c25fe2..caef9b2)
Task 2: minor (deferred): bare `veh` line with no args isn't counted (career.cpp:311 before slotOf push) — only from hand-edited files
Task 2: complete (commits b07a757..caef9b2, review clean)
Task 3: implementer DONE a6e3ce9; review: spec ✅, quality Needs fixes (1 Important: truncated world.txt with a name line counts as valid)
Task 3: minor (deferred): hand-made world folder with invalid slug chars listed as damaged but undeletable
Task 3: minor (deferred): removeTree ignores child failures; read-only dirs on Windows
Task 3: minor (deferred): escapeLine drops \r (lossy)
Task 3: minor (deferred): tests missing truncated-file / symlinked-folder / folder-without-world.txt cases; delete test ignores saveWorld returns
Note for final review: profile.txt (Task 2) has the same truncation weakness (valid if it has a `cash` line) — apply the same end-marker rule there.
Task 3: fix round 1/5 (1 addressed, 0 open; commits a6e3ce9..26794da)
Task 3: minor (deferred): pre-fix world.txt without `end 1` loads as damaged (unreleased branch; no real saves exist)
Task 3: complete (commits caef9b2..26794da, review clean)
Task 4: implementer DONE_WITH_CONCERNS 65fbb11 (no battery field on this branch; spare/chains/jack/trunk not saved; mods must match)
Ruling: battery — main now has Car::battery (jump pack merged 2b130cc); rebase wip/career-spec onto origin/main before Task 6 and add a `bat=` key + test check then (carried into Task 6 dispatch) — cost if wrong: one extra key later.
Ruling: spare/chains/jack/trunk not in carsave — out of Task 4's brief list; deferred to career phase 2+ — cost if wrong: trunk contents reset on continue.
Task 4: minor (deferred): carsave.cpp:532 applyCarDents result ignored (half-apply only if model invariant breaks)
Task 4: minor (deferred): roundtrip test doesn't check orientation (dentPose rot)
Task 4: minor (deferred): wheel item style not range-checked
Task 4: minor (deferred): commit title says battery; mention when bat= lands
Task 4: ⚠️ carried to Task 6: callers must handle vehicleOf()==-1 (catalog index gone) and loose wheels lying in the world (sim.looseWheels) aren't in the car text
Task 4: complete (commits 26794da..65fbb11, review clean)
Task 5: implementer DONE_WITH_CONCERNS 575e100 (pad X not mapped to delete; CAREER dead-ends until Task 6; other mainNav.open sites don't refresh worlds)
Task 5: minor (deferred): dead career loop in tests/mainmenu.cpp:752
Task 5: minor (deferred): delete title not upper-cased
Task 5: minor (deferred): P_WORLDS rows shrink without cap/scroll at ~25+ worlds
Task 5: minor (deferred): duplicate SDL_GetKeyboardState calls main.cpp:3769
Task 5: ⚠️ carried to Task 6: pad X → K_DELETE; A_CAREER_* handling; refresh Nav::worlds at every mainNav.open()
Task 5: complete (commits 65fbb11..575e100, review clean)
Ruling: Task 6 starts by rebasing wip/career-spec onto origin/main (needs Car::battery from the jump pack merge; main moved: engine work, characters, garage shop, join codes may land) — the implementer resolves conflicts and re-runs career/ carsave/ mainmenu/ before Task 6 code — cost if wrong: a messy rebase, recoverable from the pre-rebase sha 575e100.
Task 6: implementer DONE_WITH_CONCERNS (rebased onto origin/main 4f6d096; 5a4bfa1 bat=, b99973b wiring)
Ruling: migrated career.txt progress is kept when creating a new world (no reset to difficulty start cash) — spec section 1 "nothing is lost" outranks the plan's startFresh rule — cost if wrong: migrated players skip the start-cash reset.
Ruling: begin() gains a startVehicle argument — the brief's signature can't carry the chosen vehicle — cost if wrong: none.
Task 6: review: spec ❌, quality Needs fixes (3 Important: in-world vehicle swaps saved as owned; deleted world strands/resurrects vehicles; map change leaves a world-less career)
Ruling: in a career world, block pause DRIVE / FREE_DRIVE vehicle swaps, the MODE toggle and dev SWAP_VEHICLE (toast why), and capture() refuses to overwrite an owned vehicle whose placed model/mods differ — spec §6 — cost if wrong: owner may want in-world swaps later via the dealer (career phase 7).
Ruling: deleting a world moves its profile vehicles to world "-" (kept, not placed; reachable later via delivery, career phase 7) and newWorld de-duplicates slugs against slugs still referenced by the profile — spec §1/§6 silent; keeping bought trucks is the safer loss — cost if wrong: owner may prefer they vanish (one-line change).
Ruling: map changes (LOAD_MAP, FREE_DRIVE to another map) are blocked inside a career world with a toast "LEAVE TO THE MAIN MENU TO CHANGE MAP" — spec §1 worlds are the only way into career — cost if wrong: an extra menu trip.
Ruling: Minor 3 (saveBoth writes the real config dir in scripted runs) is treated as Important — global constraint: never modify ~/.config/crumplezone from tests/scripts — cost if wrong: none.
Task 6: minor (deferred): migration check brittle (careersession.cpp:71-73)
Task 6: minor (deferred): begin() doesn't check startableVehicle
Task 6: minor (deferred): fallback vehicle drops mods
Task 6: minor (deferred): two toasts on entering a world
Task 6: minor (deferred): session-sandbox test weak
Task 6: minor (deferred): comment that a removed wheel comes back on continue
Ruling (owner amendment, supersedes the swap-block ruling): in career, swapping is allowed only at a station/base (helper canSwapHere, stations now, outposts/camps later) and only between OWNED vehicles; the other truck is parked there with its condition; MODE toggle + dev swap stay blocked — owner's words: 'you can only swap vehicles when at a station or base' — cost if wrong: none (owner-specified).
Task 6: fix round 1/5 (3 addressed, 1 open — finding 3: playLevel/editor/HOST/JOIN/RESET_WORLD still end the session leaving career on; finding 4 gap: --script runs write; commits b99973b..b05f2f6)
Ruling: finding 3 catch-all — endCareer() also drops career mode (career.on=false and back to sandbox state, as the CHANGED handler does) whenever it ends a session other than via return-to-main-menu/quit; dev RESET_WORLD inside a world restarts the same world (careerNext) instead — cost if wrong: leaving a world via the editor drops you into sandbox.
Ruling: finding 4 — --script runs count as scripted: no writes — global constraint — cost if wrong: none.
Task 6: minor (deferred): swapTo has no guard for prop player car / walker (caller checks walker); multiplayer not considered in swapTo; DRIVE can't pick between two owned vehicles of the same model
Task 6: fix round 2 done 002e4a9 (finding 3 catch-all, finding 4 --script)
Task 6: fix round 2/5 (2 addressed, 0 open; commits b05f2f6..002e4a9)
Task 6: minor (deferred): JOIN onto the same map keeps the career world session running inside multiplayer
Task 6: minor (deferred): --script runs still write garage-career.txt via the shop (pre-existing, sandbox garage too)
Task 6: minor (deferred): endCareer sandbox switch (main.cpp:2282-2292) has no test
Task 6: minor (deferred): RESET_WORLD during an in-flight map load restarts on whichever map finishes
Task 6: complete (commits 4f15afa..002e4a9, review clean)
Final review: HELD — owner low on tokens; run the Opus whole-branch review (requesting-code-review/code-reviewer.md) on merge-base..002e4a9 with the deferred-minor list when the owner says go.
Merge with main: d6d00a9 (origin/main 1f96a1d merged; conflicts Makefile/net.h/platform.h/main.cpp resolved keeping both; career begins on a big-map move too; fix 72f24c5: owned-vehicle paint in profile veh line + carsave paint=, own paint in worlds, RESET_WORLD uses world map; fingerprint da17a95524fab674; suite 1101: only the known failures + 2 load flakes that pass alone; Windows builds)
Final review fix 1 (migrated progress wiped): begin() never startFresh's a loaded profile; only a brand-new one (none on disk; write off: Career::fresh, set by startFresh) takes the world's difficulty; profile.txt/.bak there but unreadable refuses the world (begin false + why, toast, back to sandbox, files kept; main never saves over it either); loadProfile reads aside and no longer falls back to career.txt over a damaged profile — test career/session-keeps-progress
Final review fix 2 (garage overwrites/repairs owned trucks): in a world the shop opens with the car's own parts (careersession::shopMods: its built spec's mods) and fitting goes into profile.vehicles[slot].mods + saveProfile (careersession::fit), not garage.fitted/garage-career.txt; opening with no change doesn't rebuild — test career/session-garage
Final review fix 3 (free starter mint): ownedOf/swapTo only this world's vehicles; begin mints a starter only when the profile owns none, else moves the most recently driven one (highest drove) with its condition to the spawn — test career/session-starters (session-station-swap's Tacoma now in the world)
Final review fix 4 (caches per world): careersession::cacheOpened/openCache use world.opened while a world is active (world+profile saved), profile.opened only without a world; main's HUD, interact spots, crates and openCache use them — test career/session-caches
Final review minors: Career::load returns parseCareer's result (broken/empty career.txt isn't migrated as $0) — test career/profile-broken-old; begin() with write off reads no profile; saveSettings gated on careerQuiet (scripted and --script never write settings.txt: endCareer, --career-world/A_CAREER CHANGED, all others)
Ruling: until career phase 7 (delivery), DRIVE/swap only offers owned vehicles in this world; a world with none of yours gets your most recently driven vehicle moved to its spawn, a free starter only when you own none — owner's call relayed by the controller — cost if wrong: a player can't get a truck from another world into this one except by entering a world that has none of theirs.
Ruling: difficulty start cash applies only to a brand-new profile (none on disk); an existing profile keeps its cash and parts in a new world; a damaged profile refuses the world rather than starting over — cost if wrong: a player with old progress gets their cash, not the difficulty's.
Ruling: opened supply caches are per world while in a world; the profile's opened set is only for career without a world (and the migrated career.txt's) — cost if wrong: caches reopen in each new world (more parts).
Final review: minor (deferred): "most recently driven" compares drove (each world's playedSeconds) across worlds — approximate until a profile-wide clock
Final review: minor (deferred): a fitted part is in ov.mods at once but the condition text's mods only at the next capture (crash in between: the old parts come back)
Final review: minor (deferred): profile end marker; hosting keeps the world; failed delete; quit mid-load
