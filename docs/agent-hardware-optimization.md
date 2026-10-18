# Brief for an agent testing Crumple Zone on this machine

You are running on one of the user's test machines: a Windows laptop or a Mac laptop. The main development machine is a Linux desktop with an RX 7900 XTX and a 360 Hz monitor. The user wants very high frame rates everywhere, and wants the game to run on very simple hardware.

## CURRENT PHASE: 2 once the user says to start. Your task list is `docs/hardware/phase2-plan.md`. (Phase 1, below, is done on both laptops; its reports are in `docs/hardware/`.)

## Phase 1 (done): test and report only. Change no game code.
The big CPU work (car drawing and physics threading) is still being finished on the main machine. Optimizing here now would duplicate it or clash with it, so this phase only measures and reports. Phase 2, optimizing on this machine, comes later; sections 3 to 5 below describe it, and you only use them when the user says phase 2 has started.

What to do in phase 1:
1. **Build** (section 1). If a step in `docs/hardware-testing.md` is wrong or missing for this machine, fix the doc. That is the only file you may change, besides your report. Note every problem you hit and how you solved it.
2. **Check the setup.** Plug the laptop in. Set the OS power mode to best performance. Make sure the game uses the discrete GPU if there is one; F3 shows the active GPU. Close heavy apps.
3. **Run the benchmark suite 3 times** (section 2), letting the laptop cool for a few minutes between runs. Use the native resolution. On a high-DPI or Retina screen, also run it once at `--size 1920x1080`.
4. **Run the tests and the physics check**, where they build on this OS:
   - the test suite:
     - Windows: `make build/win/run_tests.exe`, then `build/win/run_tests.exe --jobs 4`
     - Mac: `make build/run_tests`, then `./build/run_tests --jobs 4`

     Record the pass/fail count, and list any failure that isn't on the known list in section 4.
   - the physics fingerprint:
     - Windows: `make build/win/bench_sim.exe` and run it
     - Mac: `make build/bench_sim` and run it

     Record the `state` line. On the Linux desktop it is `bbde8f6bd59f9c4a`. A different value means this platform's physics differs, and multiplayer between platforms could drift. That is important to report.
5. **Play-check, briefly.** Start the game normally, drive for a minute on Frontier at the preset the auto-detect picked, and note that preset. Note any visual glitch, crash, missing asset or input problem.
6. **Write the report** (below), commit it and push it.

### Don't get stuck
- A hung game or test process looks the same as a slow one, and these have hung before (a deadlock, and a game that ignored SIGTERM).
- Always put a time limit on runs:
  - Windows, MSYS2: `timeout -k 10 1800 <command>`.
  - Mac: install coreutils with `brew install coreutils` and use `gtimeout -k 10 1800 <command>`. Or watch the run yourself and kill it.
- If a run shows no new output or files for 5 minutes and uses no CPU (Task Manager or Activity Monitor), it's hung. Kill it, note which map, preset or test it was on, and report that as a finding. Don't keep retrying the same thing.
- If the same step hangs twice, skip it, report it, and move on to the remaining steps.

### Phase 1 report: `docs/hardware/<os>-<gpu>-baseline.md`
Use a short name, e.g. `windows-rtx4060-baseline.md` or `mac-m2-baseline.md`. The report has these parts:
- **Machine:** CPU, cores, RAM, GPU (and whether discrete was active), OS version, display resolution, refresh rate, scale factor, and power mode.
- **Build:** the steps that worked, the problems and fixes, the build time, and the compiler version.
- **Results table:** for each map and preset, the median of the 3 runs for average fps, 1% low fps and hitches, and the two or three biggest stages with their CPU and GPU ms.
- **Verdict per preset:**
  - CPU-bound or GPU-bound, from the stage times: CPU-bound when the CPU stages sum to more than the GPU total.
  - The limiting stage.
- **Tests:** the pass/fail counts and any failure not on the known list, plus the `bench_sim` state line and whether it matches `bbde8f6bd59f9c4a`.
- **Auto-detect:** the preset it chose, and whether that was right for this machine.
- **Problems seen:** crashes, glitches, missing assets, input issues, slow loading. Give exact messages.
- **Your read:** the 3 things most likely to help this machine, ranked. These are ideas only; don't implement them.

Also copy the raw `suite-*.txt` files into `docs/hardware/raw/`, the text files only. Don't commit the `.bmp` screenshots: they're large. Mention their folder path in the report instead.

Commit on a branch named `hw/<os>-<gpu>-baseline` and push the branch, never `main`. Then tell the user the branch name. The main machine will read it.

## 1. Set up and build
Follow `docs/hardware-testing.md` for the prerequisites and build steps for this OS:
- Windows: MSYS2 UCRT64 shell, then `make -j$(nproc) windows` (uses that shell's g++); the exe is `build/win/CrumpleZone/crumplezone.exe`.
- Mac: Xcode CLT and SDL2.framework in `third_party/sdl2-mac/`, then `make -j$(sysctl -n hw.ncpu) mac-dev` (host architecture, no LTO; the binary is `build/macdev/crumplezone`). `make mac` builds the universal app.

If a step in that doc is wrong for this machine, fix the doc in the same branch.

## 2. Measure first
- Run the benchmark suite in a **real window**, never offscreen:
  ```
  ./build/win/CrumpleZone/crumplezone.exe --bench-suite     # Windows (no console: output goes to files)
  ./build/macdev/crumplezone --bench-suite                  # Mac
  ```
  Optional: `--bench-suite FRAMES,WARMUP` (default 600,120), `--size WxH`, `--bench-out FILE`.
  The results go to `<config dir>/bench/`: `%APPDATA%\crumplezone\bench` on Windows, `~/.config/crumplezone/bench` on Mac. The suite covers Frontier, Open Country (Wilds) and Boulder Gully at POTATO, LOW, MEDIUM and HIGH. It writes `suite-<date-time>-<WxH>.txt` (machine, table, one `suite-csv:` line per run, stage tables) and one `.bmp` screenshot per run with the F3 stats showing. Windows also keeps console output in `%APPDATA%\crumplezone\graphics.log`.
- Make sure the game runs on the discrete GPU if the laptop has one. F3 shows the active GPU.
- For one scene, use `--map N --quality Q --bench FRAMES,WARMUP` (map 3 Frontier, 5 Open Country, 1 Mountain; quality 0-3 or `potato`). It writes `<date-time>-<map>-<quality>-<WxH>.txt` to the same folder (or `--bench-out FILE`). Its `bench-csv:` line gives the average, the 1% low, hitches, and CPU/GPU ms for each stage: GAME, SIM, SHADOWS, SKY, TERRAIN, OPAQUE, VEGETATION, PROPS, FX, POST, UI, SWAP.
- Record the machine: CPU, GPU, RAM, OS and refresh rate. The bench file has these.
- Measure several times and quote medians. Laptops throttle, so plug the laptop in and let it cool between long runs.

## Known so far (from the phase 1 reports)
- **Mac M1** (`docs/hardware/mac-m1-baseline.md`, before the car and physics CPU work landed):
  - It is GPU-bound. Only POTATO is playable (55 to 69 fps); LOW to HIGH run at 11 to 24 fps.
  - A 1600x900 window renders at 2880x1676 on Retina.
  - macOS has no GL timer queries, so GPU stage times read 0. The big PROPS CPU time is where the driver waits for the GPU.
  - The physics fingerprint differs from Linux (`69497071eddf9087`), probably Apple's libm.
  - `fieldrepair/steps` hits bad_alloc on the Mac only.
  - The Mac phase 2 starting points:
    - Retina/HiDPI: render scale relative to the logical window size, and default to logical pixels on HiDPI.
    - Auto-detect for Apple M-series: POTATO or LOW for M1, and check the probe steps down.
    - What the GPU spends its time on, using Xcode's GPU tools or Instruments (Metal System Trace) since GL timers are missing.

## 3. Find the limit (phase 2 only)
- **CPU-bound:** SIM plus OPAQUE plus the other CPU stages exceed the GPU total. Look at the sim (`src/softbody.*`), car drawing (`src/carvisual.*`, `render.cpp` buildFigures), and per-frame allocations (`CZ_ALLOC=60` prints per-frame counts).
- **GPU-bound:** the GPU stages dominate. TERRAIN, the ground shader, has historically been the biggest. Then SHADOWS, POST and VEGETATION.
- **Driver-specific:** Windows OpenGL drivers have higher per-draw-call cost, so count draws. macOS OpenGL is a deprecated 4.1 layer over Metal: check for GL features above 4.1, slow paths, and Retina scaling, where render scale versus framebuffer size matters.
- **Swap or present:** check VSync, the frame cap (FPS TARGET) and compositor effects.

## 4. Rules (same as on the main machine)
- Physics must stay bit-identical across builds.
  - Every build uses `-ffp-contract=off -fno-fast-math`.
  - `tools/bench_sim` prints a `state` fingerprint, and it must not change unless the change is intentionally to physics.
  - Never use `-ffast-math`, and don't add FMA intrinsics.
- Never run `./build/run_tests --update-baseline`.
  - Known failing tests (ignore): frontier/current, freeway/top-speed/turbo utv, structure/weight/koenigsegg jesko, structure/weight/toyota tacoma, components/axle-landing, components/tire-into-fender/rock buggy, components/tire-into-fender/turbo utv, structure/rock-hits/4runner, offroad/whoops, tires/shear, tires/debead.
  - Flaky under load: engine/performance, frontier/wall-stairs, net/find.
- Visuals must stay the same at HIGH. Take before and after screenshots with `--debug-screen` and look at them. The user judges visuals harshly. Lower presets may trade looks for speed.
- Keep changes portable: one code path, with small `#ifdef _WIN32` or `__APPLE__` pieces only where needed (see `src/platform.h`). The Linux build must still pass its test suite.
- Match the surrounding code style. Use terse comments. Add no new dependencies.
- Don't touch the user's settings files except through the game. Don't run anything with admin or sudo.

## 5. Deliver (phase 2 only)
- Work on a branch named `hw/<os>-<gpu>`, e.g. `hw/windows-rtx4060` or `hw/mac-m2`. Commit there and push the branch. Don't push to `main`; the main machine merges it after running the full Linux suite.
- Write `docs/hardware/<os>-<gpu>.md` with:
  - the machine specs;
  - the bench-suite table before and after;
  - the limiting stage and why;
  - what you changed, with the measured gain per change;
  - anything left over, ranked by expected gain.
- Keep the user's token budget in mind. Measure, fix the biggest limit, and re-measure. Don't scatter small speculative changes.
