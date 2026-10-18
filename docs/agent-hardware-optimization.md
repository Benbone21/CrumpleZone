# Brief for an agent optimizing Crumple Zone on this machine

You are running on one of the user's test machines: a Windows laptop or a Mac laptop. The main development machine is a Linux desktop with an RX 7900 XTX and a 360 Hz monitor.

Your job: measure how the game performs on **this** hardware, find what limits it here, and fix it without hurting the other platforms. The user wants very high frame rates everywhere and wants the game to run on very simple hardware.

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

## 3. Find the limit
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

## 5. Deliver
- Work on a branch named `hw/<os>-<gpu>`, e.g. `hw/windows-rtx4060` or `hw/mac-m2`. Commit there and push the branch. Don't push to `main`; the main machine merges it after running the full Linux suite.
- Write `docs/hardware/<os>-<gpu>.md` with:
  - the machine specs;
  - the bench-suite table before and after;
  - the limiting stage and why;
  - what you changed, with the measured gain per change;
  - anything left over, ranked by expected gain.
- Keep the user's token budget in mind. Measure, fix the biggest limit, and re-measure. Don't scatter small speculative changes.
