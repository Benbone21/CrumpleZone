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
