# Physics performance and Windows graphics (2026-10-13)

Changes are on `sync`, based on `3c9f527`. Independent forces and collision-grid preparation
run on a persistent pool with a configurable CPU thread budget, counting the caller. Small
workloads run serially. **Esc → Settings → PHYSICS THREADS** selects AUTO or any count
from one through the available logical CPUs, with the maximum marked ALL. It applies
immediately and persists in `settings.txt` as `physicsthreads`. Manual counts have no
four-thread cap; saved counts are clamped to the available CPUs after hardware changes.
AUTO retains the measured conservative default: half the reported logical CPUs, capped
at four. `--physics-threads auto|all|N` overrides the saved choice; 1 disables the pool.
Scenario runners remain serial by default to avoid nested parallel pools. More threads
do not guarantee more throughput: shared contact forces remain serial, and each physics
step synchronizes the independent work. The table below measures the four-thread budget.

Both previous all-pairs bounds scans now share one candidate list. Larger scenes sweep
the widest axis; candidates are sorted by the original car indices before waking cars
and accumulating contact forces. Geometry preparation caches the triangle edge vectors
and dot products used by many point queries. Sleeping supports retain their grids until
movement, network updates or rebuilding invalidate them. Cross-car forces, ropes,
loose-wheel forces and integration retain their original serial ordering and 2 kHz step.

## CPU measurements

Ryzen 9 9900X, Linux, `g++ -O2 -march=native`, low process priority. Median of three
interleaved runs of the original executable and the optimized four-thread executable.
Each run advances 2,000 substeps (one simulated second). No renderer, AI, audio or test
invariant overhead is included. Initialization and destruction are outside the timer.
Pileups start opposing, overlapping rows of moving cars; this intentionally stresses
deep contacts rather than representing average gameplay. These are CPU measurements,
not game FPS estimates or measurements of a Windows/NVIDIA machine.

| Workload | Before, CPU ms per simulated 60 Hz frame | After | Speedup |
|---|---:|---:|---:|
| 1 separated car | 0.90 | 0.92 | approximately unchanged |
| 12 separated cars | 13.76 | 4.89 | 2.82× |
| 32 separated cars | 37.72 | 12.76 | 2.95× |
| 64 separated cars | 80.37 | 28.59 | 2.81× |
| 4-car pileup | 18.76 | 13.21 | 1.42× |
| 12-car pileup | 98.70 | 76.49 | 1.29× |

Node position/velocity and beam-state bit fingerprints matched the original binary
in every measured run, including the 32/64-car stress cases beyond the game's usual
vehicle cap. Dense 12-car pileups still exceed a real-time frame budget: shared narrow
contact queries remain serial and are the next performance target.

To reproduce on Linux:

```bash
nice -n 19 make -j6 build/bench_sim
nice -n 19 ./build/bench_sim 12 separated 2000 1
nice -n 19 ./build/bench_sim 12 separated 2000 4
nice -n 19 ./build/bench_sim 12 pileup 2000 1
nice -n 19 ./build/bench_sim 12 pileup 2000 4
```

Arguments: car count, `separated` or `pileup`, substeps, thread count. The two optimized
thread counts should produce the same state fingerprint. `make build/win/bench_sim.exe`
builds the CPU-only Windows equivalent, which needs no SDL DLL or display.

## Windows and graphics

- Verified the built PE executable exports `NvOptimusEnablement` and
  `AmdPowerXpressRequestHighPerformance`; the requests already existed in the source.
  The NVIDIA request follows its [Optimus rendering policy](https://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf).
  Windows graphics preferences can override it ([NVIDIA's explanation](https://nvidia.custhelp.com/app/answers/detail/a_id/5035)).
- Window multisampling is disabled because the HDR scene target owns MSAA. This removes
  a second multisample target and its presentation overhead, including on the LOW preset.
- All HDR/postprocessing targets are checked for completeness. Unsupported HDR MSAA
  retries fewer samples, ending with a single-sample scene; that fallback is remembered
  until dimensions or the requested sample count change.
- Uniform locations are cached per linked program, including wheel/shadow/prop draws,
  to avoid repeatedly asking the driver for the same names. The cache resets on GL loading.
- Windows records stdout/stderr in `%APPDATA%\crumplezone\graphics.log`, including the
  GPU, driver/OpenGL version, thread count, target size/MSAA and shader errors.
- F3 shows physics and scene submission CPU time. Driver stalls count toward draw CPU
  time; it is not GPU timer-query time.

### GPU compatibility review (2026-10-14)

GPU vendor detection uses both `GL_VENDOR` and `GL_RENDERER` for diagnostics, including
Apple GPUs and Mesa software renderers. It does not override the saved VSync choice.
VSync ON requests swap interval 1 on every vendor; OFF, `--no-vsync`, and scripted
captures request 0. A renderer name alone is not evidence for a faster presentation mode.

Ground and prop textures now check for `GL_EXT_texture_filter_anisotropic` or
`GL_ARB_texture_filter_anisotropic` before querying or setting anisotropy. Drivers without
these extensions retain ordinary mipmap filtering. Existing limits stay at 16x for ground
and 8x for props. The extension's [specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_filter_anisotropic.txt)
defines these optional parameters; no texture compression or Metal backend was added.
See the [current platform verification report](platform-verification.md) for the review,
macOS networking fix, release checks and remaining hardware validation.

Linux and Windows executables were built locally. Configurable counts above four,
live changes between serial and parallel operation, saved settings, and menu selection
of AUTO and ALL were additionally verified after removing the hard limit. The graphics integration test passed
all four quality presets on Mesa llvmpipe without GL errors, including an emulated
driver rejecting multisample HDR, repeated frames after fallback, and render-scale
changes. A packaged-game offscreen smoke test also ran. This environment has no GPU
device; Wine execution is blocked by the sandbox (`Bad system call`). Real Windows
and proprietary NVIDIA driver performance therefore still need hardware verification.

The release workflow now checks collision/worker behavior and offscreen rendering on
Linux, checks hybrid-GPU exports, and runs serial/parallel fingerprints on a native
Windows runner before publishing. The new workflow has not been run from this session.

```bash
nice -n 19 make -j6 build/run_tests build/test_render
nice -n 19 ./build/run_tests --jobs 1 engine/collision engine/physics-workers
SDL_VIDEODRIVER=offscreen LP_NUM_THREADS=2 nice -n 19 ./build/test_render
```

The collision tests compare randomized scenes against brute-force bounds detection,
including exact-touching bounds, changes in body count and empty scenes. Worker tests
compare every node's position, velocity and force, beam deformation/breakage and wheel
speeds at every step, for all seven vehicle types in separated and overlapping scenes.
They also check exception propagation and reusing the worker pool after an exception.

Full-suite validation: before, **416/436 passed** with 20 failures; after,
**423/442 passed** with 19 failures and **no newly failing scenarios**. All three collision/worker and all three added networking scenarios passed. Seven failures are sandbox-denied network sockets
(`Operation not permitted`); the other twelve are the existing vehicle, tire and crawl
physics failures. `engine/performance` passed in the final run, but that serial threshold
is sensitive to contention: the before run used six scenario jobs and the final after run
used two. It is not evidence of the worker pool's speedup; use the interleaved CPU
benchmarks above for that comparison. Both suites reported the same 167 metrics drifting
from the repository's older baseline. Physics ranges and `tests/baseline.txt` were not changed. The full report is preserved at
`build/review/multiplayer/full-suite.html`; the final focused state checks also passed
after the spawn-origin and extrapolation-stop refinements. See `docs/multiplayer.md`
for the networking and audio checks.

The final local Linux executable is `./crumplezone`. The Windows build and assets are
packaged in `build/win/CrumpleZone-windows.zip`; extract the whole folder and run
`crumplezone.exe`. The archive integrity check passed. The installer was not rebuilt
locally because NSIS is not installed; release CI continues to build it.
