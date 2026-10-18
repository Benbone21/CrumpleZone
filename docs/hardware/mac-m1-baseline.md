# Mac M1 baseline (phase 1, test and report only)

Scope note: ONE bench-suite run (not 3), at 1600x900 only; no 1920x1080 run; no play-check. The user judged one run adequate. Numbers are single-run, not medians. No game code changed.

## Machine
- Apple M1, 8 logical cores, 8 GB RAM, integrated GPU (7 cores; the only GPU)
- macOS 26.6 (arm64). Built-in Retina LCD, 2560x1600 native, 1440x900 logical, 60 Hz
- Window 1600x900 (HDR target 2880x1676, so the render surface is larger than the window)
- GL 4.1 Metal 90.5 (GLSL 4.10). Plugged in (charging, 61% at start). Power mode not changed.

## Build
- `make -j8 mac-dev`: 39 s wall. Apple clang 21.0.0 (clang-2100.1.1.101).
- Problem 1: Xcode 27.0 license not accepted (`sudo xcodebuild -license`); every clang/git call failed with exit 69. Fix: `export DEVELOPER_DIR=/Library/Developer/CommandLineTools` uses the CLT toolchain and needs no license. (Doc suggestion: mention this in docs/hardware-testing.md Mac section.)
- Problem 2: SDL2 had to be fetched by hand (SDL2-2.30.12.dmg, copied to `third_party/sdl2-mac/`). Worked as documented.
- Test binaries: `make build/run_tests build/bench_sim` built fine.

## Results (single run, 1600x900, 600 frames after 120 warm-up)
| Map | Preset | avg fps | 1% low fps | hitches | Top stages (CPU ms) |
|---|---|---|---|---|---|
| Frontier | POTATO | 55.1 | 39.4 | 0 | PROPS 9.69, OPAQUE 2.91, SIM 2.61 |
| Frontier | LOW | 18.8 | 15.9 | 0 | PROPS 36.68, OPAQUE 5.71, SIM 4.77 |
| Frontier | MEDIUM | 12.9 | 11.9 | 0 | PROPS 57.06, OPAQUE 6.33, SIM 5.90 |
| Frontier | HIGH | 11.1 | 10.3 | 0 | PROPS 67.37, SIM 6.54, OPAQUE 6.60 |
| Open Country | POTATO | 69.1 | 56.9 | 0 | PROPS 7.95, SIM 2.26, SWAP 1.57 |
| Open Country | LOW | 24.3 | 22.1 | 0 | PROPS 28.22, SIM 5.14, OPAQUE 1.80 |
| Open Country | MEDIUM | 15.2 | 13.6 | 0 | PROPS 49.8, SIM 6.28, OPAQUE 1.85 |
| Open Country | HIGH | 12.7 | 11.8 | 0 | PROPS 60.75, SIM 6.82, TERRAIN 1.91 |
| Boulder Gully | POTATO | 56.8 | 39.5 | 0 | PROPS 9.18, SIM 2.92, OPAQUE 2.21 |
| Boulder Gully | LOW | 22.5 | 19.0 | 0 | see raw file |
| Boulder Gully | MEDIUM | 13.4 | 11.7 | 0 | see raw file |
| Boulder Gully | HIGH | 11.5 | 10.0 | 0 | PROPS 65.69, SIM 6.54, OPAQUE 5.02 |

1% low fps = 1000 / "1% low ms". Full per-stage tables: `docs/hardware/raw/suite-20261017-212458-1600x900.txt`. Screenshots (.bmp) are in `~/.config/crumplezone/bench/` (not committed).

## Verdict per preset
- **GPU timers read 0.00 for every stage** on macOS (no GL timer queries on this driver), so CPU- vs GPU-bound can't be taken from the rule "CPU stages > GPU total". Inference instead: the CPU stages other than PROPS sum to only about 10 to 15 ms, yet frames take 40 to 90 ms. The large time sits in PROPS, which looks like where the CPU waits on the GPU (the first draw/sync point after the heavy passes), so all presets above POTATO look **GPU-bound**, with the HDR/MSAA target at 2880x1676 on an M1 as the likely cost. POTATO (scale 0.50, no MSAA/shadows) is near CPU-bound territory: 55 to 69 fps with SIM 2 to 3 ms.
- Limiting stage: PROPS (CPU-measured, probably GPU wait) on every preset.
- Spike: one 2146 ms frame (frame 1446, typical 61 ms) on Frontier MEDIUM, spent in TERRAIN (792 ms) and OPAQUE (633 ms); likely shader compile or first-use upload. Not counted as a hitch by the suite.

## Tests (`./build/run_tests --jobs 4`)
689 pass, 17 fail.
- Known-list failures (ignored): frontier/current, freeway/top-speed/turbo utv, structure/weight/koenigsegg jesko, structure/weight/toyota tacoma, engine/performance, components/tire-into-fender/rock buggy, components/tire-into-fender/turbo utv, components/axle-landing, structure/rock-hits/4runner, offroad/whoops, tires/shear, tires/debead; net/find (flaky list).
- **NOT on known list:** `frontier/autopilot-lodge` (arrived: THERE 162 M), `fieldrepair/steps` (`std::bad_alloc`), `net/lan-discovery`, `net/custom-port` (likely sandbox/network related, not investigated).

## Physics fingerprint
`./build/bench_sim`: `12 cars separated (1 threads): 2.318 s wall, 0.86x real time, state 69497071eddf9087`.
**Does NOT match Linux `bbde8f6bd59f9c4a`.** Mac/ARM physics differs from the Linux desktop, so cross-platform multiplayer could drift. Possible causes (untested): ARM64 vs x86 libm results, or compiler differences despite `-ffp-contract=off -fno-fast-math`. Note also 0.86x real time with 12 cars.

## Auto-detect
Not checked (no normal play launch). The suite's own presets: POTATO = scale 0.50, no shadows/MSAA; LOW = shadows 1, MSAA 0; MEDIUM = shadows 2, MSAA 2; HIGH = shadows 3, MSAA 4, AO on.

## Problems seen
- Xcode license blocks the toolchain (see Build).
- No GPU stage timing on macOS.
- 2.1 s frame on first MEDIUM run.
- Visual glitch, added afterwards from the user's own observation (not measured by the agent): scan-line-like horizontal lines across the screen on the HIGH preset only, on every map. The same artifact appears on the Windows RTX 4070 laptop (see `windows-rtx4070-baseline.md`). Cause unknown; POST and SHADOWS at HIGH are the first suspects.
- No play-check done: crashes, glitches and input not assessed.

## Your read (ideas only, not implemented)
1. The 2880x1676 HDR target is too big for an M1 at LOW and above: render at a lower scale on Retina or add a Mac auto-preset, since only POTATO is playable (55+ fps).
2. Find what PROPS waits on (likely a GPU sync or heavy draw cost); 28 to 67 ms there dwarfs everything else. Count draws and state changes.
3. Investigate the physics fingerprint mismatch and the first-use stall (shader warm-up) behind the 2 s frame.
