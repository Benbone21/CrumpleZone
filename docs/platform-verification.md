# Platform verification — 2026-10-14

Reviewed the OpenCode changes in `MemeNG` on `wip/field-repair`, based on `6ed8b86`.
Separate audio, autopilot and vehicle-tool worktrees are outside this release's scope.

## Findings and fixes

1. GPU recommendations silently disabled a saved VSync ON setting on NVIDIA and Intel. VSync now uses the saved setting and command-line override on all vendors. Vendor detection uses both vendor and renderer strings; Apple-hosted AMD/Intel GPUs, Apple Silicon, software renderers and unknown names have explicit regression cases. The original optimization function only logged recommendations, so those claims were removed.
2. Anisotropy is optional for OpenGL 3.3. Ground and prop texture loaders now enumerate extensions before querying the limit or setting the optional texture parameter. Unsupported drivers retain ordinary mipmap filtering. Tests cover EXT/ARB support, missing support, misleading extension suffixes and real ground-texture loading with extensions hidden. Existing limits remain 16x for ground and 8x for props. [Khronos specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_filter_anisotropic.txt).
3. The POSIX networking path used `MSG_NOSIGNAL` without a fallback on macOS. It now supplies the fallback and uses `SO_NOSIGPIPE` on platforms that provide it, including native tools without the game's global signal handler. [Apple socket documentation](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/setsockopt.2.html). A Linux compile with a socket-header shim verified the missing-`MSG_NOSIGNAL` path; this is a syntax check, not a native Mac build.
4. Tripling the Bronco/4Runner/Tacoma body strength introduced all three 50 km/h truck crash failures, a shaft-break failure and a recovery damage-preservation failure. The previous body strengths are restored. Frame strength increases from 2.5 to 3.0 remain. Previous vehicle settings were compiled into a separate runner for comparison; no physics expectations or baseline values were widened.
5. The old Mac smoke step ignored process failures and missing screenshots. Release CI now gates publishing on native Mac physics/networking/audio/render checks, serial/parallel fingerprints, and a bounded packaged-app launch away from the repository. It requires both a screenshot and successful ground-asset loading. These CI changes have been syntax-checked locally but have not run on GitHub in this session.

## Local verification

Native Linux game, scenario runner, render integration, audio tests and benchmark build.
Windows game, benchmark, scenario runner and audio tests cross-compile with the release's
portable x86-64 flags. Windows archive integrity and NVIDIA/AMD GPU-selection exports
are checked. The Mac regression targets use the existing universal arm64/x86_64 flags.
No Mac SDK or native Windows host is available in this environment.

All **27 audio checks** pass with SDL's dummy audio driver. Offscreen rendering passes
all four quality presets, optional filtering checks, menu persistence, remembered HDR
MSAA fallback and resize/render-scale changes. A game launched from an unrelated folder
loads photo ground assets, renders a frame and exits. Local launches use offscreen SDL,
low priority and no controller/audio input.

The proposed OpenCode vehicle settings produced **436/461 passing scenarios**, with
25 failures. After correcting body strength, the final run produced **441/461 passing
scenarios**, with 20 failures (444.9 seconds). All 20 final failures reproduced in the
previous-vehicle-settings runners. The five introduced failures are removed: all three
truck 50 km/h crashes, `offroad/shaft-break` and `recovery/upright` pass. All seven
vehicle crash cases pass. `engine/performance` also passes; timing on a shared machine
is not a hardware performance guarantee.

Seven failures are sandbox-denied network sockets (`Operation not permitted`), also
reproduced with the previous settings. The thirteen existing physics failures are:

- `frontier/current` and `backcountry/autopilot-far`
- `freeway/top-speed/turbo utv`
- `structure/weight/koenigsegg jesko` and `structure/weight/toyota tacoma`
- `components/axle-landing`
- `components/tire-into-fender/rock buggy` and `components/tire-into-fender/turbo utv`
- `structure/rock-hits/4runner` and `offroad/whoops`
- `tires/swap`, `tires/shear` and `tires/debead`

The final suite reports 171 metric drifts against the older recorded baseline; those
are informational. `tests/baseline.txt`, scenario assertions and metric ranges are unchanged.
Serial and four-thread benchmark runs match state fingerprints for both 12-car separated
and pileup workloads. These are Linux CPU determinism checks, not Windows or Mac FPS measurements.

A separate portable Linux executable was built with `-march=x86-64-v2 -mtune=generic`,
packaged as a version 0.1.7 Debian package, extracted and launched from an unrelated
folder; it loaded its ground assets and rendered a screenshot. The Windows ZIP passes
archive integrity checks and imports only SDL2 plus Windows system libraries. The PE
exports both hybrid-GPU selection requests. The local Windows installer cannot be
built because NSIS is unavailable; release CI supplies the installer and Mac app.

Local release artifacts and checksums are in `build/release-0.1.7/`. Release notes are
`docs/releases/v0.1.7.md`. Publishing is blocked in this environment: Git cannot resolve
`github.com`, and GitHub CLI cannot reach `api.github.com`. Remote refs, the release
version and CI status could not be verified. The prepared release must be published
through the updated workflow once connectivity is available; it is not a published
GitHub release yet.

## Reproduction and artifacts

```bash
nice -n 19 make -j4 crumplezone build/run_tests build/test_render build/test_audio build/bench_sim
nice -n 19 ./build/run_tests --jobs 2
SDL_VIDEODRIVER=offscreen LP_NUM_THREADS=2 nice -n 19 ./build/test_render
SDL_AUDIODRIVER=dummy CZ_AUDIO_DEVICE_TEST=1 nice -n 19 ./build/test_audio
nice -n 19 make -j2 windows build/win/run_tests.exe build/win/bench_sim.exe build/win/test_audio.exe
```

Local logs and HTML reports are preserved under `build/review/platform/` (gitignored).
No live GPU/driver performance measurement was possible; software-renderer checks do
not establish NVIDIA, Apple Silicon or Intel hardware FPS improvements. Native Mac and
Windows CI results must be checked before treating those platforms as runtime verified.
