# Windows RTX 2060 desktop baseline (phase 1)

Phase 1 only: nothing in the game code was changed. main at `1c62ca1` (2026-10-21), so the phase 2 AO stripe fix and the ground shader's `PCF_EARLY` are both in. A desktop, not a laptop: the phase 2 plan's Windows list is for the RTX 4070 laptop and doesn't apply here as written.

**Headline:** ground quality 2 (MEDIUM and up) costs this GPU about 17x what ground quality 1 does (TERRAIN 3.9 to 60-82 ms GPU at 4K). On the RTX 4070 laptop the same step is about 2x. LOW runs as fast here as on the 4070; MEDIUM and HIGH are 3 to 3.5x slower. That cliff drives auto-detect down too.

## Machine
- CPU: Intel Core i5-12400 (6 cores, 12 logical), 32 GB RAM
- GPU: NVIDIA GeForce RTX 2060 12 GB (Turing), discrete, active (GL 3.3.0 NVIDIA 610.88). An Intel UHD 730 is also present; the game used the NVIDIA.
- OS: Windows 11 Pro 25H2 (build 26200)
- Display: Samsung U32J59x, 3840x2160 at 60 Hz, **150% scaling**
- Power: Windows plan **Ultimate Performance** (desktop, mains power)

## Build
- `winget install MSYS2.MSYS2`, then in UCRT64: `pacman -Syu` (twice: the first updates the runtime and closes), `pacman -S --needed make mingw-w64-ucrt-x86_64-gcc git`.
- Compiler: g++ 16.2.0 (MSYS2 UCRT64 Rev6). The 4070 laptop used 16.1.0.
- `make -j12 windows build/win/run_tests.exe build/win/bench_sim.exe`: 233 s from clean, no ccache. 58 warning lines, no errors.
- Driven from Git Bash as `C:\msys64\usr\bin\bash.exe -lc "cd <repo> && make ..."` with `MSYSTEM=UCRT64`. `CHERE_INVOKING=1` alone did not keep the working directory (the login shell went to `~`); the explicit `cd` did.

## High DPI: the game renders at 2560x1440 on this 4K screen
The exe isn't DPI aware (no manifest entry, no `SDL_HINT_WINDOWS_DPI_AWARENESS`), and `SDL_WINDOW_ALLOW_HIGHDPI` does nothing on Windows. At 150% scaling, Windows gives it a 2560x1440 desktop and stretches the result to 4K. Your own fullscreen session logged `HDR target: 2560x1440`. Windows players on scaled 4K or 1440p screens get a blurry, upscaled image and can't pick native resolution.

For these runs, `SDL_WINDOWS_DPI_AWARENESS=permonitorv2` in the environment (SDL 2.30.9 reads hints from the environment) made the game per-monitor aware: `--fullscreen` then gives a 3840x2160 HDR target. Added a note to `docs/hardware-testing.md`. The 2560x1440 run below was made *without* the hint, so it's what you get today.

## Results: native 3840x2160, median of 3 runs (600 frames after 120)
Fullscreen, DPI aware. avg fps (1% low ms); 0 hitches in every run.

| Map | POTATO | LOW | MEDIUM | HIGH |
|---|---|---|---|---|
| Frontier | 342 (4.80) | 187 (6.87) | 12.5 (94.3) | 11.4 (102.4) |
| Open Country | 433 (3.18) | 228 (5.97) | 15.7 (70.1) | 14.4 (75.3) |
| Boulder Gully | 324 (5.10) | 174 (7.99) | 14.1 (98.4) | 12.9 (105.1) |

Biggest stages (CPU/GPU ms, medians):

| Map | POTATO | LOW | MEDIUM | HIGH |
|---|---|---|---|---|
| Frontier | SWAP 2.19, TERRAIN 0.05/0.82 | TERRAIN 0.31/3.86, OPAQUE 2.40/0.09, SWAP 1.96 | TERRAIN 0.35/76.3, OPAQUE 76.3/0.12 | TERRAIN 0.35/81.9, OPAQUE 84.0/0.17, POST 0.01/2.64 |
| Open Country | SWAP 1.92, TERRAIN 0.06/0.63 | TERRAIN 0.30/3.05, OPAQUE 1.71/0.11, SWAP 1.93 | TERRAIN 0.35/60.6, OPAQUE 61.1/0.09 | TERRAIN 0.35/64.4, OPAQUE 66.4/0.03, POST 0.01/2.74 |
| Boulder Gully | SWAP 2.39, TERRAIN 0.05/0.89 | TERRAIN 0.42/3.89, OPAQUE 2.15/0.02, SWAP 1.98 | TERRAIN 0.45/66.8, OPAQUE 67.4/0.02 | TERRAIN 0.46/71.2, OPAQUE 74.3/0.03, POST 0.01/2.78 |

Run-to-run spread is small: within 5% at POTATO and LOW, within 2% at MEDIUM and HIGH.

## Other resolutions (1 run each)
avg fps (1% low ms).

| Map / preset | 1600x900 window | 2560x1440 fullscreen, not DPI aware (today's default) | 4070 laptop 1600x900 (phase 1) |
|---|---|---|---|
| Frontier POTATO | 348 (4.89) | 347 (4.92) | 504 |
| Frontier LOW | 316 (4.39) | 313 (4.25) | 315 |
| Frontier MEDIUM | 58.9 (20.9) | 26.2 (45.5) | 205 |
| Frontier HIGH | 51.1 (24.0) | 23.5 (50.2) | 163 |
| Open Country POTATO | 423 (3.20) | 435 (3.19) | 556 |
| Open Country LOW | 338 (5.10) | 338 (5.14) | 366 |
| Open Country MEDIUM | 65.6 (17.8) | 31.4 (36.0) | 235 |
| Open Country HIGH | 55.8 (20.5) | 28.0 (39.4) | 187 |
| Boulder Gully POTATO | 324 (5.07) | 331 (5.10) | 439 |
| Boulder Gully LOW | 269 (5.34) | 262 (5.23) | 306 |
| Boulder Gully MEDIUM | 59.4 (22.0) | 28.3 (48.2) | 216 |
| Boulder Gully HIGH | 51.7 (24.3) | 25.3 (51.3) | 167 |

Raw files: `raw/windows-rtx2060-suite-20261021-225656-3840x2160.txt`, `-230524-3840x2160`, `-231119-3840x2160`, `-231713-1600x900`, `-231850-2560x1440`. The `.bmp` screenshots stay in `%APPDATA%\crumplezone\bench` on this machine (not committed).

## The ground quality 2 cliff
Frontier at 4K, one `--bench 300,120` each, changing one setting from the preset (raw: `raw/windows-rtx2060-20261021-2303*.txt`):

| Run | ground | avg ms | TERRAIN GPU ms |
|---|---|---|---|
| LOW (suite median) | 1 | 5.34 | 3.86 |
| MEDIUM with `--ground 1` | 1 | 8.07 (124 fps) | 4.70 |
| LOW with `--ground 2` | 2 | 66.8 (15 fps) | 65.1 |
| MEDIUM with `--props 1 --grass 1` | 2 | 78.2 | 74.6 |
| MEDIUM (suite median) | 2 | 80.0 | 76.3 |

Ground quality alone accounts for the whole jump. MEDIUM's other differences from LOW (MSAA 2, shadows 2, bloom, clouds, grass 2, props 2) cost about 3 ms together. ULTRA (`--quality 3`, ground 3, MSAA 8) was 11.1 fps with TERRAIN 83 ms.

Cost scales with pixels (TERRAIN about 76 ms at 4K and about 15 ms at 1600x900 for Frontier MEDIUM), so it's per-pixel shader cost, not a texture upload or a stall.

**Likely cause (not tested):** the `GQ >= 2` paths in `src/groundshader.cpp` choose and blend materials through local arrays indexed by loop variables (`sW[bi] = -1.0`, `selV[k]`, `A[k]`, `bwt[k]` over K up to 3 or 4). NVIDIA's compiler often puts dynamically indexed arrays in local memory instead of registers, and Turing has far less cache to absorb that than Ada (the 4070). That would explain 17x here against 2x on the 4070. The shader's existing `X_K1`/`X_R2`/`X_R3` switches, toggled one at a time, would confirm it. Phase 2 work.

## Verdict per preset (native 4K)
- **POTATO: present-limited.** It renders at half scale (1920x1080). CPU is about 0.5 ms and GPU about 1.3 ms, but SWAP is 1.9 to 2.4 ms. That is a higher floor than the 4070's 1.4 to 1.6 ms, at a 60 Hz panel (the bench turns VSync off).
- **LOW: GPU-bound, TERRAIN** (3.1 to 3.9 ms of about 4.5 ms GPU). OPAQUE's 1.7 to 2.4 ms CPU with about 0 GPU is the same driver wait the 4070 laptop showed (phase 2 item 3 there), not real CPU work.
- **MEDIUM and HIGH: GPU-bound, TERRAIN**, 60 to 82 ms, about 95% of the frame. OPAQUE CPU mirrors it (the wait again). Unplayable at 4K, 23 to 31 fps at 2560x1440, 51 to 66 fps at 1600x900.
- SIM is 0 in every run (drone camera), so these numbers say nothing about physics cost with cars.

## Tests
- `run_tests.exe --jobs 4`: **973 scenarios, 962 passed, 11 failed**, 941 s. 174 metrics drifted from the baseline (not updated). Log: `raw/windows-rtx2060-run_tests.log`.
- On the known list (8): tires/shear, tires/debead, structure/rock-hits/4runner, components/axle-landing, components/tire-into-fender/rock buggy, components/tire-into-fender/turbo utv, structure/weight/koenigsegg jesko, structure/weight/toyota tacoma.
- Flaky under load (2): `engine/performance` (0.62x real time single core), and `net/find`, which passes on its own. In the full run, `net/find` found the host on `192.168.56.1`, a VirtualBox host-only adapter. The machine also has Tailscale, Twingate and Ethernet adapters.
- **New, not on any list: `vehicle-physics/acceleration`.** "Invariant: no node faster than 150 m/s -- TOYOTA 4RUNNER #3 at t=34.08 s (node 27, -3.82 m under PAVEMENT)". It reproduces exactly when run alone, so it's deterministic, not load. The test was added 2026-10-15 and isn't in the 4070 laptop's 2026-10-17 failure list, so the 2026-10-18 physics change, perhaps combined with this platform's physics difference, is the first suspect. **The Linux machine should check whether it fails there too.**
- Known failures that now pass here: frontier/current, offroad/whoops, freeway/top-speed/turbo utv.

## Physics fingerprint
- `bench_sim` state: **`1147d5f0f5f7730d`**, the same on two runs.
- Linux after the 2026-10-18 change: `1cb2a908f74c3983`. **They differ**, as expected while cross-platform matching is paused. The 4070 laptop's `3db86794fec05596` predates the 2026-10-18 change, so it can't be compared. Whether g++ 16.2 versus 16.1 matters is unknown.
- Output: `12 cars separated (1 threads): 2.5-2.9 s wall, 0.69-0.79x real time, 21.2-24.3 ms/60Hz frame`. The 4070 laptop measured 14.5 ms/frame, but on older code, so this isn't a like-for-like CPU comparison.
- This is the reference for checking phase 2 edits on this machine.

## Auto-detect
`settings.txt` was moved aside (copied first; the original was restored byte for byte, hash checked), and the game was started with no options (1600x900 window, not DPI aware, VSync adaptive, 60 Hz display, so the probe aims at 60 fps and calls a median over 19.2 ms slow).
- `graphics.log`: "hardware: 12 cores, 32538 MB RAM, VRAM 12288 MB, guess HIGH", then after about 26 s "graphics detected: **POTATO**". Result: render scale 0.50, shadows 0, MSAA 0, AO, bloom, clouds and grass off.
- **Wrong for this machine.** HIGH (51 fps) and MEDIUM (59 to 66 fps) are below or on the edge of the 60 fps aim at this window size, so stepping below MEDIUM is fair. But LOW runs at 270 to 340 fps and should have stopped the probe. It stepped through LOW to POTATO anyway. Not explained; the ground texture swap from quality 2 to 1 landing inside LOW's 3 s measure window is one candidate.
- The renderer table guesses HIGH for any GeForce or RTX. A Turing card like this one is far below the 4070 at ground 2.
- One 67 ms SWAP spike at frame 457, around the preset changes.

## Problems seen
- No crashes or hangs in the bench suites, tests or bench_sim.
- The not-DPI-aware 2560x1440 rendering on a scaled 4K screen (above).
- The first suite batch was stopped and restarted once by the agent, to fix the 2560x1440 run's settings; no results came from the stopped batch.
- **No play-check was done**, so visual glitches, input and the HIGH scan-line fix were not checked by eye on this machine.

## Your read (ideas only)
1. **The ground quality 2 shader path on Turing.** Confirm the local-memory theory with the shader's `X_*` switches, then rewrite the material choice without dynamically indexed arrays (unrolled, with fixed `K` per tier). This is the difference between 12 and maybe 100+ fps at 4K MEDIUM, and it probably covers all GTX 16 and RTX 20 cards.
2. **DPI awareness on Windows**: make the exe per-monitor aware (manifest or the SDL hint before `SDL_Init`) and size targets from the drawable. Then render scale can default to something sane on 4K screens. Needs care with the HUD and mouse, as on the Mac Retina item.
3. **Auto-detect**: after item 1, re-check it. Even now it shouldn't fall through LOW; find why it did, and consider guessing MEDIUM rather than HIGH for pre-Ada cards.
