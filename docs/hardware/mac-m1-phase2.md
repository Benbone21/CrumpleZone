# Mac M1 phase 2 report

Machine: MacBook with Apple M1, 8 GB, Retina 2560x1600 (logical 1440x900), macOS 26.6, plugged in. Build: `make mac-dev` with the Command Line Tools (`DEVELOPER_DIR=/Library/Developer/CommandLineTools`). Branch `hw/mac-m1-phase2`.

Measurement note: short single-run benches (`--map 3 --quality Q --bench 300,100`, Frontier, 1600x900 window), not medians of 3. Run-to-run noise is about 1 ms. A full before/after `--bench-suite` was not run (the user asked to cut back on game testing).

## Changes (each its own commit)
| Change | Frontier avg frame time before -> after |
|---|---|
| Scene renders at logical pixels on Retina (target 2880x1676 -> 1440x838); base Apple chips (M1) auto-detect LOW | LOW 61.4 -> 25.5 ms, MEDIUM 92.5 -> 38.1, HIGH 107.2 -> 44.4 |
| Scan lines at HIGH fixed: the half-resolution AO pass read depth on the boundary between two depth rows, and NEAREST rounding picked either row line by line. It now reads a texel centre. | none (visual fix). Row-to-row banding on pavement 1.265 -> 0.338, equal to AO off |
| Ground shader: material slots at constant indices instead of run-time-indexed arrays | LOW 26.2 -> 23.2, HIGH 44.7 -> 41.5 (0.0026% of pixels differ by more than 2/255 in a HIGH shot) |

Combined, Frontier is now about 23 ms at LOW (about 43 fps), 41.5 ms at HIGH.

## Where the GPU time goes (HIGH, Frontier, whole-frame ablation)
GL timers don't exist on macOS and `--bench-sync` inflates numbers (every sync splits the tile renderer's pass: 61 ms synced against 44 ms normal), so each feature was switched off in turn and the whole frame timed.
- **Terrain draw: about 26 of 44.7 ms (58%)**; at LOW 13.7 of 26.2 ms. It is fragment-bound and scales with pixel count (about 11 ns per shaded pixel at LOW); everything else is a flat 12.4 ms (CPU) that doesn't change with resolution.
- Inside terrain on HIGH (before the slot fix): environment noise about 5.7 ms, shadows about 3.5, photo texture taps about 2.7, material slots about 3, plus a long tail of lighting ALU.
- MSAA 4x -> off: 8 ms. Shadows 3 -> 0: 3.3 ms. AO and bloom: about 0 ms.
- The `sin()` hash is not a bottleneck (replacing it saved 1 ms).
- **PROPS is not slow.** With GPU sync it costs about 4 ms; the 28-67 ms phase 1 showed was the driver waiting for the GPU to finish earlier passes.

## Ideas not done, ranked
1. Render scale below logical on LOW (for example 0.75): terrain cost is linear in pixels, so about 40% of the GPU cost for a mild softness. A visual change; the user's call.
2. Bake the low-frequency environment noise (gN1, gDry, gForest, brightness) to a 1 m texture at map load: saves maybe 3 ms; small look change.
3. Cheaper MSAA on M-series (2x at HIGH): 4 ms; look change.
4. Shader warm-up at load for the first-use stall, and checking whether the shader cache is used on macOS: not investigated ("25 shader programs compiled, 0 from the cache" on every launch suggests it may not be).
5. The async sim thread: Frontier POTATO went from 55 fps (phase 1) to 46 fps after main's sim-thread change; cause not isolated (thermal or the thread on 8 GB).

## Tests and physics
- `./build/run_tests --jobs 4`: 693 pass, 17 fail, the same 17 as in phase 1 (known list plus `frontier/autopilot-lodge`, `fieldrepair/steps` bad_alloc, `net/lan-discovery`, `net/custom-port`, `net/find`). No new failures.
- `bench_sim` state: `69497071eddf9087`, unchanged by all edits.
- Not checked: the UI and mouse at logical scale by hand, and what auto-detect picks on a fresh settings file.
