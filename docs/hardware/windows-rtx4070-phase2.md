# Windows RTX 4070 Laptop phase 2

Machine as in `windows-rtx4070-baseline.md` (Ryzen AI 9 HX 370, RTX 4070 Laptop, 2048x1280 at 240 Hz, g++ 16.1.0). Windows power plan still **Balanced** (the agent cannot change it).

## Step 0: new baseline (main at dc7fe7f, 1 run each)
- **Tests:** 864 scenarios, 851 passed, 13 failed, 538 s. The same 13 as phase 1: net/bandwidth, tires/shear, tires/debead, structure/rock-hits/4runner, offroad/whoops, components/axle-landing, components/tire-into-fender/rock buggy, components/tire-into-fender/turbo utv, structure/weight/koenigsegg jesko, structure/weight/toyota tacoma, engine/performance, freeway/top-speed/turbo utv, frontier/current. Log: `raw/windows-rtx4070-phase2-step0-run_tests.log`.
- **Physics fingerprint:** `3db86794fec05596`, unchanged from phase 1.
- **Raw bench files:** `raw/suite-20261017-223557-1600x900.txt`, `raw/suite-20261017-223641-2048x1280.txt`.

Average fps, phase 1 to now:

| Map / preset | 1600x900 before | 1600x900 now | 2048x1280 before | 2048x1280 now |
|---|---|---|---|---|
| Frontier POTATO | 504 | 512 | 516 | 536 |
| Frontier LOW | 315 | 315 | 199 | 198 |
| Frontier MEDIUM | 205 | 204 | 130 | 130 |
| Frontier HIGH | 163 | 163 | 107 | 106 |
| Open Country POTATO | 556 | 597 | 619 | 625 |
| Open Country LOW | 366 | 408 | 259 | 262 |
| Open Country MEDIUM | 235 | 243 | 157 | 157 |
| Open Country HIGH | 187 | 187 | 124 | 125 |
| Boulder Gully POTATO | 439 | 440 | 480 | 497 |
| Boulder Gully LOW | 306 | 316 | 225 | 225 |
| Boulder Gully MEDIUM | 216 | 217 | 139 | 140 |
| Boulder Gully HIGH | 167 | 167 | 110 | 110 |

Main's new work (car drawing, physics threading, map loading) changes almost nothing here: the benchmark is GPU-bound and its SIM stage is 0. Only some POTATO and LOW runs moved.

## Items
Not started. Next in the plan: item 1, `net/bandwidth` on Windows.

## Item 2: scan-line stripes at HIGH (done first, at the user's request)
**Found:** the stripes come from the ambient occlusion (AO) pass, which only HIGH and ULTRA turn on. With AO off they disappear; with MSAA 2 or shadows 2 (the other two differences from MEDIUM) they stay. Before: `img/hq-scanlines-before.png` (crop `-crop`). After: `img/hq-scanlines-after.png`.

**Cause:** the AO texture is half resolution. Its pixel centres sit exactly on the corner of four full-resolution depth texels, and the shader read depth with a `NEAREST` texture fetch at those coordinates (and the +-1 pixel neighbours used to rebuild the surface normal). Which texel the GPU picks at an exact corner is decided by rounding, so rows were picked inconsistently and the rebuilt normals alternated by row. On a flat floor seen at a grazing angle that prints horizontal stripes. This is GPU- and driver-dependent, which fits it appearing on NVIDIA/Windows and Apple but not on the Linux AMD machine.

**Fix (`src/render.cpp`, AO shader):** read depth with `texelFetch` at integer pixel coordinates (`posPx`), centred on the 2x2 block of each AO pixel, for the centre, the four normal taps and the spiral taps. No hardware filtering decides anything now. Shared code, no `#ifdef`, so the Mac gets the same fix: **the Mac agent should skip this item and only confirm it once this is in main.**

**Checked:**
- Frontier, Open Country and Boulder Gully at HIGH, 2048x1280: no stripes after the fix.
- AO still works: contact shadows under the vehicles remain. Away from the stripe pattern the AO is slightly lighter than before, because the old stripes were also adding dark lines.
- Speed: Frontier HIGH at 2048x1280, 106.2 fps before and 106.5 / 106.4 after (no cost).
- Physics fingerprint not touched (render-only change). `render/water-mesh-budget` passes. The full suite was not rerun for this change.

## Item 1: `net/bandwidth` fails on Windows (fixed, a test problem, not a game bug)
The host drops a guest that sends more than 150 messages a second by the wall clock. The test's `usleep(200)` between frames is a no-op on Windows, so simulated time ran far ahead of real time. The first guest was dropped ("`G0 DROPPED: sending too much`" in the host log), every guest after it then saw 0 trucks, and the 2-player run measured nothing (0.01 KB/s). Each frame now waits until its simulated time has come on the clock (`tests/net_bandwidth.cpp`). It now passes with real traffic: 4 players 18.7 KB/s down per guest (budget 32), 8 players 64 KB/s (budget 92), every guest sees every other truck. Cost: the test takes about 32 s on the wall. Real clients send in real time, so Windows players can join busy servers; nothing to fix in `src/net.cpp`.

## Item 3: OPAQUE CPU at 5 to 6.5 ms with about 0 GPU (not a stall)
No synchronizing call sits in that stage (no `glFinish`, readback or fence; the grass timing query only runs under `CZ_BENCH`). The test: make the GPU's ground shader cheaper with `--ground` and watch OPAQUE's CPU time (Frontier HIGH, 2048x1280):

| `--ground` | TERRAIN GPU ms | OPAQUE CPU ms | fps |
|---|---|---|---|
| 3 | 8.11 | 6.57 | 104 |
| 2 (HIGH) | 7.85 | 6.21 | 106 |
| 1 | 6.66 | 5.06 | 122 |
| 0 (procedural) | 2.32 | 1.18 | 252 |

OPAQUE's CPU time tracks the ground's GPU time, so it is the CPU waiting for the GPU (the driver's frame queue is full) at the first big draw. It is back-pressure from a GPU-bound frame, not something to remove. The real lever is the ground shader (item 5).

## Item 4: POTATO SWAP floor about 1.4 ms
Frontier POTATO, 1500 frames, SWAP ms (fps): windowed 1600x900 1.43 (545), windowed 2048x1280 1.38 (571), fullscreen 1.39 (560), windowed `--no-vsync` 1.44 (541), windowed 640x360 1.42 (547), windowed 320x180 1.40 (550). It is a fixed cost per present that does not depend on window size, fullscreen or the vsync flag, so POTATO tops out near 550 fps on this machine and driver. That is more than double the 240 Hz display, so it is not worth chasing in the game. Not tried: NVIDIA Control Panel settings (the agent doesn't change system settings).

## Item 5: TERRAIN on NVIDIA (partly done)
No Nsight (only FrameViewSDK is installed; nothing was installed). Instead each cost was removed in turn with temporary shader switches (not committed). Frontier HIGH, 2048x1280, TERRAIN GPU ms (baseline 7.83):

| Removed | TERRAIN GPU ms | Saved |
|---|---|---|
| shadow lookup (PCF + cloud) | 5.56 | 2.27 |
| fbm noise (all) | 6.35 | 1.48 |
| material layers K=1 | 6.76 | 1.07 |
| two-tap anti-tiling | 7.16 | 0.67 |
| steep-face triplanar | 7.53 | 0.30 |
| second texture array (normal/rough/AO) | 7.56 | 0.27 |
| lamp lighting | 7.60 | 0.23 |
| average-colour fetches | 7.67 | 0.16 |
| position wobble | 7.83 | 0.00 |

Within the shadow lookup, the PCF costs about 2 ms and the cloud shadow only 0.3 ms.

**Change (this commit):** the ground shader only (`PCF_EARLY`) now samples 4 of the PCF taps first, spread out to the kernel's edge, and returns when they all agree (fully lit or fully in shadow); shadow edges still take every tap. Props, trees and cars keep the full filter, because an early version applied everywhere changed leaf-shadow speckles on foliage.
- Frontier HIGH, 2048x1280: TERRAIN GPU 7.83 to 6.75 ms, **106.5 to 120.4 fps** (+13%); MEDIUM 130 to 139 fps.
- Visual check against the previous build at HIGH on all three maps: 47 to 134 of 655k sampled pixels differ by more than 3 levels, as isolated single pixels in the dithered soft edge of shadows (a few slightly darker specks along the lower edge of the car shadow at 4x zoom). Images: `img/pcf-shadow-edge-before.png` and `-after.png`, full frames `img/pcf-frontier-high-before.png` and `-after.png`. **The user should look at these**: a harder, noisier edge is the only visible change.
- Not full-suite re-tested after this change.

**Left, ranked by expected gain** (not done): fbm noise, 1.5 ms (nine or more `sin`-hash noise calls a pixel, all very low frequency: computing the smooth ones per vertex or cheaper would change little visually but needs vertex-shader work); material layers, 1.1 ms (changes the look); two-tap anti-tiling, 0.7 ms (changes the look).

## Item 6: auto-detect
`%APPDATA%\crumplezone\settings.txt` was moved aside (a copy was kept first and the original was put back byte-for-byte, hash checked), and the game was started with no options. `graphics.log`: "hardware: 24 cores, 31864 MB RAM, VRAM 8188 MB, guess HIGH", then "graphics detected: HIGH". Resulting settings: shadows 3, MSAA 4, AO on, bloom on, clouds on, render scale 1.00, grass 2, fullscreen off, vsync on (adaptive). The table guesses HIGH for any GeForce or RTX, and the warm-up probe confirmed it. For this laptop that is right: HIGH is about 120 fps at native 2048x1280 and 185 fps at 1600x900, and the quality gap to MEDIUM (139 and 218 fps) is the AO, shadows and MSAA. One 72 ms spike was logged at frame 90, all of it in SWAP (the first present after the window appears), once.

## Final results (after items 1 to 6; main at dc7fe7f plus this branch)
Average fps, 1 run each. Step 0 is before this branch's changes.

| Map / preset | 1600x900 step 0 | 1600x900 now | 2048x1280 step 0 | 2048x1280 now |
|---|---|---|---|---|
| Frontier POTATO | 512 | 514 | 536 | 526 |
| Frontier LOW | 315 | 323 | 198 | 201 |
| Frontier MEDIUM | 204 | 218 | 130 | 139 |
| Frontier HIGH | 163 | 185 | 106 | 120 |
| Open Country POTATO | 597 | 582 | 625 | 622 |
| Open Country LOW | 408 | 423 | 262 | 281 |
| Open Country MEDIUM | 243 | 270 | 157 | 178 |
| Open Country HIGH | 187 | 230 | 125 | 153 |
| Boulder Gully POTATO | 440 | 446 | 497 | 497 |
| Boulder Gully LOW | 316 | 328 | 225 | 241 |
| Boulder Gully MEDIUM | 217 | 244 | 140 | 158 |
| Boulder Gully HIGH | 167 | 203 | 110 | 134 |

HIGH gained 13 to 23% at native resolution (all from the ground shader's shadow lookup); POTATO is unchanged (it has no shadows), as expected. Raw: `raw/suite-20261017-234701-1600x900.txt`, `raw/suite-20261017-234737-2048x1280.txt`.

- **Tests:** 864 scenarios, 851 passed, 13 failed (`raw/windows-rtx4070-phase2-final-run_tests.log`, `--jobs 6`). The 11 known failures, `engine/performance` (flaky), and `net/find` (flaky under load: it passes alone). `net/bandwidth` now passes.
- **Fingerprint:** `3db86794fec05596`, unchanged by every edit here (all changes are rendering or test code).

## What is left, ranked
1. Ground shader (TERRAIN is still about 6.8 ms of 8.3 ms at native HIGH): fbm noise about 1.5 ms; fewer material layers about 1.1 ms and single-tap about 0.7 ms (these change the look).
2. Power plan: still Balanced. Best performance might lift the 1% lows.
3. POTATO SWAP floor (about 1.4 ms, 550 fps ceiling): only the NVIDIA Control Panel or another present path could change it; not worth it at 240 Hz.
4. Mac: the AO fix in item 2 and the shadow change in item 5 are shared code; the Mac agent should skip item 2 and confirm both there.

## ULTRA (added afterwards; the bench suite only covers POTATO to HIGH)
`--quality 3` (MSAA 8x, grass 3, ground 3), 1 run each, `--bench 600,120`, this branch's code. Average fps (1% low ms):

| Map | 1600x900 | 2048x1280 |
|---|---|---|
| Frontier | 159 (8.17) | 106 (13.20) |
| Open Country | 206 (7.14) | 140 (11.36) |
| Boulder Gully | 174 (8.75) | 120 (10.46) |

Compared with HIGH now (Frontier / Open Country / Boulder Gully): at 1600x900 185 / 230 / 203, at 2048x1280 120 / 153 / 134. So ULTRA costs about 10 to 15% against HIGH. Still GPU-bound: TERRAIN GPU is 4.0 to 4.9 ms at 1600x900 and 5.5 to 7.6 ms at native, and OPAQUE's 2.4 to 6.4 ms of CPU is again the CPU waiting for the GPU (item 3). Not checked for the scan-line artifact (the AO fix applies to ULTRA too, since it uses the same AO pass).
