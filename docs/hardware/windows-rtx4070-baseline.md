# Windows RTX 4070 Laptop baseline (phase 1)

Phase 1 only: nothing in the game code was changed. **Incomplete versus the brief:** the bench suite was run once, not 3 times, and there was no 1080p run or play-check. The medians below are single-run values.

## Machine
- CPU: AMD Ryzen AI 9 HX 370 (24 logical cores), 31.9 GB RAM
- GPU: NVIDIA GeForce RTX 4070 Laptop GPU, discrete, active (GL 3.3.0 NVIDIA 616.92)
- OS: Windows 11 Pro 26H2 (build 26300)
- Display: 2048x1280, 240 Hz; bench window 1600x900
- Power: on AC, but the Windows power plan was **Balanced**, not Best performance. The agent can't change system settings, so this is unchanged. Numbers may improve with Best performance.

## Build
- Installed MSYS2 with `winget install MSYS2.MSYS2`, then `pacman -S --needed make mingw-w64-ucrt-x86_64-gcc git`.
- Compiler: g++ 16.1.0 (MSYS2 UCRT64 Rev5).
- Steps that worked, from a UCRT64 environment (`MSYSTEM=UCRT64`): `make -j$(nproc) build/win/run_tests.exe`, `make windows`, `make build/win/bench_sim.exe`. No doc fixes were needed.
- Note: the whole build was driven through `C:\msys64\usr\bin\bash.exe -lc` with `MSYSTEM=UCRT64` and `CHERE_INVOKING=1`, not an interactive shell.

## Results (1 run, 1600x900, 600 frames after 120 warm-up)
| Map | Preset | avg fps | 1% low ms | hitches | Biggest stages (CPU/GPU ms) |
|---|---|---|---|---|---|
| Frontier | POTATO | 504 | 3.46 | 0 | SWAP 1.53, TERRAIN 0.03/0.58 |
| Frontier | LOW | 315 | 5.54 | 0 | SWAP 1.61, TERRAIN 0.21/2.65 |
| Frontier | MEDIUM | 205 | 6.53 | 0 | TERRAIN 0.23/4.05, SWAP 3.41 |
| Frontier | HIGH | 163 | 7.41 | 0 | TERRAIN 0.24/5.00, SWAP 4.60 |
| Open Country | POTATO | 556 | 2.44 | 0 | SWAP 1.42, TERRAIN 0.05/0.40 |
| Open Country | LOW | 366 | 6.07 | 2 | SWAP 1.59, TERRAIN 0.22/2.06 |
| Open Country | MEDIUM | 235 | 6.74 | 3 | TERRAIN 0.24/3.45, SWAP 2.74 |
| Open Country | HIGH | 187 | 8.13 | 0 | TERRAIN 0.24/4.43, SWAP 3.96 |
| Boulder Gully | POTATO | 439 | 3.37 | 0 | SWAP 1.62, TERRAIN 0.05/0.51 |
| Boulder Gully | LOW | 306 | 4.70 | 0 | TERRAIN 0.31/2.30, SWAP 1.68 |
| Boulder Gully | MEDIUM | 216 | 6.30 | 0 | TERRAIN 0.32/3.74, SWAP 2.73 |
| Boulder Gully | HIGH | 167 | 7.53 | 0 | TERRAIN 0.32/4.75, SWAP 3.98 |

Raw output: `docs/hardware/raw/windows-rtx4070-suite-20261017-215513-1600x900.txt`. Screenshots (`.bmp`) are in `%APPDATA%\crumplezone\bench` and not committed.

## Verdict per preset
- **LOW, MEDIUM, HIGH: GPU-bound.** CPU stages sum to about 0.5 to 1.6 ms against 2.5 to 6.1 ms of GPU time. The limiting stage is **TERRAIN** (the ground shader): 2.1 to 2.7 ms at LOW, 3.5 to 4.1 at MEDIUM, 4.4 to 5.0 at HIGH.
- **POTATO: neither CPU nor GPU stages explain the frame time.** CPU is about 0.5 ms and GPU about 0.7 ms against about 1.8 to 2.3 ms per frame. Most of the time is in SWAP (about 1.4 to 1.6 ms), so present or driver overhead is the limit. Not investigated.
- SIM reads 0.00 ms in every run, since the bench uses a drone camera. These numbers say nothing about physics cost with cars in play.

## Tests
- `run_tests.exe --jobs 4`: **861 scenarios, 848 passed, 13 failed**, 565 s. 169 metrics drifted from the baseline (the baseline was not updated).
- 11 failures are on the known list: tires/shear, tires/debead, structure/rock-hits/4runner, offroad/whoops, components/axle-landing, components/tire-into-fender/rock buggy, components/tire-into-fender/turbo utv, structure/weight/koenigsegg jesko, structure/weight/toyota tacoma, freeway/top-speed/turbo utv, frontier/current.
- `engine/performance` failed. It is on the flaky-under-load list.
- **`net/bandwidth` failed and is not on any list.** Every "guest N joins" check fails and "every guest sees every other truck" reports 0 for 4 and 8 players. This looks like multiplayer connection setup failing on Windows (loopback sockets or firewall are the first things to check). Not investigated further.
- Full log: `docs/hardware/raw/windows-rtx4070-run_tests.log`.

## Physics fingerprint
- `bench_sim` state: **`3db86794fec05596`**
- Linux reference: `bbde8f6bd59f9c4a`
- **They differ.** This platform's physics differs from Linux, so multiplayer between Windows and Linux could drift. The Mac M1 also differs (`69497071eddf9087`), and all three values are different. The cause is not known; libm differences and the g++ 16.1 versus Linux compiler are the first suspects.
- Output line: `12 cars separated (1 threads): 1.741 s wall, 1.15x real time, 14.510 ms/60Hz frame`.

## Auto-detect
Not checked: the game was only run through `--bench-suite`, not a normal start.

## Problems seen
- Winget's MSYS2 install is slow (a few minutes with a Qt installer console window).
- Nothing crashed or hung during the bench suite or tests.
- No play-check was done, so visual glitches and input are unchecked.

## Your read (ideas only)
1. The TERRAIN ground shader is the GPU limit at every preset above POTATO (about 80% of GPU time at HIGH). Cutting its cost would help most.
2. Look at the physics mismatch (`3db86794fec05596`) first, before any optimizing here, since it affects correctness rather than speed.
3. Find out why POTATO sits at about 1.4 to 1.6 ms of SWAP; present overhead may cap fps near 500 to 550.
