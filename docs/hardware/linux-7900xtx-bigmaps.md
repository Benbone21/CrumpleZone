# Big maps: Linux 7900 XTX measurements

Before and after each phase of `docs/plans/big-maps.md`. Machine: Ryzen 9 9900X (24 threads), RX 7900 XTX (Mesa 26.1.6 radeonsi), 31 GB RAM, Debian 13. Every run used `nice -n 19`.

How to measure:
- **World build:** `build/worldtime [--scans] [MAP...]` (headless). It prints the build time, the RAM added, the grid, the object counts and a land hash. The hash covers heights and water to 1 cm, surfaces, and object positions. A phase that must not change the maps must leave the hashes unchanged.
- **In game:**
  - `CZ_LOADPROF=1 ./crumplezone --load-tour --no-audio --no-pad` for map switches and their worst frames;
  - `./crumplezone --no-audio --no-pad --map N --bench 600,120` for frame times, with the peak RSS sampled from `/proc`.
- **Physics fingerprint:** `build/bench_sim 12 separated 4000`.

## Baseline: main @ 898c8e2 (2026-10-22)

| Map | World build | RAM added | Map switch in game | Worst frame in switch | Bench avg (HIGH 1600x900) | 1% low | Peak RSS |
|---|---|---|---|---|---|---|---|
| Proving ground | 0.08-0.10 s | 24 MB | - | 65 ms | - | - | - |
| Mountain | 0.22-0.38 s | 37 MB | - | 93 ms | - | - | - |
| Frontier | 0.94-1.20 s | 46 MB | 1.49 s | 75 ms | 6.27 ms | 18.3 ms | 917 MB |
| Backcountry | 0.55-1.02 s | 46 MB | 1.08 s | 53 ms | 5.52 ms | 16.2 ms | 926 MB |
| Open country | 0.12-0.21 s | 26 MB | - | 24 ms | - | - | - |

Other baseline numbers:
- Cold startup on the proving ground spends 12.7 s of CPU in 32 world builds on workers, plus 4.4 s on the section park.
- Dedicated server RAM after startup: 91 MB on Frontier, 80 MB on Backcountry.
- Fingerprint: `91850b5879f5e3e3`.

## Phase 1: map size per map, fewer startup world builds

The world builds and land hashes are unchanged on every map, with and without scanned rocks. The fingerprint is still `91850b5879f5e3e3`. The seven section-park section files come out byte-identical to the ones the old build made.

Startup measured to the end of `--map N --bench 10,0`, wall clock, with an empty private cache (`CZ_CACHE_DIR`) for "first run" and the same cache again for "later":

| Start on | First run before | First run after | Later before | Later after |
|---|---|---|---|---|
| Proving ground | 6.0 s | 3.5 s | 2.6 s | 2.5 s |
| Backcountry | 6.1 s | 2.4 s | 2.6 s | 2.0 s |

Cold startup on the proving ground (`CZ_LOADPROF=1 --load-tour`):

| | Before | After |
|---|---|---|
| World builds | 32 | 15 |
| CPU in world builds | 12.7 s | 4.3 s |
| Section park | 4.4 s | 0.64 s |

Where the time went:
- The sim's own proving-ground world was built with the model yard and the section park even when another map or the menu's camp replaced it at once. It is now built bare in that case.
- The section-park cache built every built-in level's world (Frontier, Mountain, and 7 section worlds twice) just to read one section each. It now builds only the one world it needs.

## Phase 2: compact land data, parallel bake, world cache (branch wip/bigmaps-p2)

Land hashes and every cell's roads and water are bit-identical to before, on every map. The fingerprint is still `91850b5879f5e3e3`. The suite fails only the known 8.

| | Before | After |
|---|---|---|
| World RAM, Frontier / Backcountry / Mountain | 47 / 50 / 48 MB | 26 / 26 / 39 MB |
| World build on 24 threads, Frontier / Backcountry | 0.89 / 0.54 s | 0.73 / 0.39 s |
| Map switch with the world cache warm, Frontier / Backcountry / Mountain | 0.79 / 0.43 / 0.14 s | 22 / 21 / 38 ms |
| Worst frame in a map switch (`--load-tour`) | 53-75 ms | 30-39 ms |

Notes on the table:
- The worst-frame numbers are noisy: other agents were running during the "before" runs.
- The world cache files are 19 / 20 / 29 MB (Frontier / Backcountry / Mountain), not yet compressed.
- Re-seeding the scatter per cell was not done. The existing maps keep their layouts. Per-cell seeds come with the new land in phase 4, where they are needed. The quadratic tree-spacing checks are only in the proving ground and the open country's scatter.

### Frame-time baseline before phase 3

`--bench-suite`, defaults (600 frames after 120, 1600x900), on the phase 2 build. Taken at about 17:12 on 2026-10-22, with load average 8 on 24 threads (another agent's tests were using about 3 cores), so these numbers are a little noisy.

Average frame time, with the 1% low in brackets:

| Scene | POTATO | LOW | MEDIUM | HIGH |
|---|---|---|---|---|
| Frontier | 2.39 (3.90) | 2.85 (4.38) | 2.71 (4.24) | 2.46 (3.59) |
| Open country | 1.58 (2.56) | 1.96 (2.92) | 1.92 (2.65) | 2.22 (3.59) |
| Mountain / Boulder Gully | 2.04 (2.62) | 3.07 (4.99) | 3.11 (4.33) | 3.17 (5.51) |

The 6.3 / 5.5 ms single-scene numbers in the baseline section were taken while the machine was much busier. Compare against this table.

## Phase 3: streamed terrain drawing (branch wip/bigmaps-p2)

These numbers are on a 6 km test map, today's open country built at 6 km: `--map 5 --map-half 3072`. Before phase 3, the renderer drew it as one mesh.

| | Before | After |
|---|---|---|
| Peak RAM (whole game) | 4.8 GB | 0.77 GB |
| Renderer ready | 5.6 s | 0.5 s (cache warm), 1.0 s (cold) |
| Launch to first bench frame | about 9 s | 2.3 s (warm), 2.8 s (cold) |
| Average frame, standing (HIGH 1600x900) | 2.36 ms | 2.30 ms |
| Flying 2.4 km at 60 m/s, eye 3 m up, looking along the ground | 5.1 ms (64 draws a frame per far chunk before grouping) | 2.48 ms, 1% low 3.9 ms, worst 11 ms |

Today's maps:
- Their screenshots match the previous renderer. PSNR is 64-67 dB against the 2a build on Backcountry, Frontier and the proving ground. The open country aerial differs by 13 pixels.
- Frontier HIGH, run interleaved against the 2a build (3 runs each, load average 10): 2.79 / 2.98 / 2.83 ms before, 2.95 / 2.91 / 2.93 ms after. Every stage is the same within 0.02 ms, so the difference is within the noise.
- `--bench-suite` after phase 3 (load average 10, noisy):
  - Frontier: POTATO 2.16, LOW 2.67, MEDIUM 2.94, HIGH 2.96 ms.
  - Open country: POTATO 1.58, LOW 1.99, MEDIUM 2.21, HIGH 2.19 ms.
  - Boulder Gully: POTATO 2.52, LOW 3.16, MEDIUM 3.36, HIGH 3.44 ms.

What phase 3 left out:
- Tree impostors. On a big map the forest stops at 2 km instead (800 m on the lowest props setting), and static scenery batches stop at 2.5 km.
- A textured M map. On a big map the M map just uses coarser cells: 30 m at 6 km.
- The water mesh and the water sound still scan the whole grid at load. That is fine at 6 km: about 0.1 and 0.3 s.
