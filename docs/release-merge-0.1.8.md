# Release merge verification — 0.1.8

Integrated the available local and remote-tracking branches into `main` in
`CrumpleZone-release`, preserving all commits and the uncommitted prototype work.
OpenCode's coordination notes and its high-speed damping implementation are included.
The larger physics and menu roadmaps remain plans, not completed release features.

## Integration corrections

- Keep GPU diagnostics and ambience in the combined game source list. Add ambience/soundscape objects to native Mac audio tests as well as the existing Linux/Windows targets.
- Rename the new world query result from `Near` to `NearbySolids`; the original name shadowed the Frontier map's existing helper inside World methods and failed compilation.
- Wire autopilot gathering to the spatial lookup while sorting object indices to preserve the old deterministic ordering.
- Make profiling counters thread-local and output optional with `CZ_PILOT_PROFILE`, avoiding races between parallel scenarios.
- Remove the vehicle prototype's build reference to `vtoolsdraw.cpp`, which was never written. The command model, labels, removal helpers and tests are included; popup rendering/input remains unfinished.
- Correct vehicle-tool test fixtures: real props use `crashBoxSpec`, vehicle identity is tracked across index shifts, ray picking starts beyond the first car for the second-hit case, and popup expectations follow the actual selected labels and number of vehicles. Physics ranges and `tests/baseline.txt` are unchanged.
- Preserve the previously paused free-coil experiment behind `CZ_FREE_COILS=1`. The global experiment made the Bronco's lowest upright dot drop to 0.50 in the whoops workload, versus the earlier 0.85 result, and its original commit already documented instability. A separate check then showed the high-speed damper change also made the Bronco tip farther and caused a new shaft-break failure even with free coils disabled. Preserve it behind `CZ_HIGH_SPEED_DAMPING=1`. Both experiments remain available; normal gameplay retains the verified suspension defaults.
- Keep the two eight-autopilot performance workloads in `build/bench_autopilot`, separate from functional regression checks. The prototypes specify a future 2 ms budget. Initial measurements on this machine were about 1.9 ms/frame on Backcountry and 3.2 ms/frame on Frontier, with synchronized sensing spikes exceeding the intended tail-latency budget. Those targets remain unmet, and no limits were widened.
- Move Mac CI to `macos-15`: GitHub is retiring the macOS 14 runner on November 2, 2026 ([retirement notice](https://github.com/actions/runner-images/issues/13518)).
- Add `make mac-dmg`, using Apple's `hdiutil` to package the signed app and an Applications shortcut. CI verifies the DMG, mounts it, checks the universal app and signature, and requires a successful packaged-app render before release publication. [Apple's packaging documentation](https://developer.apple.com/documentation/xcode/packaging-mac-software-for-distribution).

## Checks

The final functional run is **448/468 passing**, with **20 failures** (469.6 seconds).
Its failure set exactly matches the previous verified v0.1.7 build: thirteen existing
physics failures and seven network checks denied sockets by the sandbox. All seven
new vehicle-tool and spatial-query checks pass. The suite reports 171 informational
metric drifts against the older baseline. No physics ranges or baseline values changed.
The shaft-break check passes with release defaults; the Bronco's whoops upright dot
returns to the earlier 0.85, compared with 0.50 with both experiments enabled and
-0.20 with high-speed damping alone. The existing UTV whoops failure remains.

Native and portable x86-64 Linux builds succeed. The extracted 0.1.8 Debian package
starts away from the source tree, loads its photo ground assets, renders a screenshot
and exits. Windows game, scenario runner, benchmark and audio tests cross-compile;
the ZIP passes integrity checks, and the PE exports both GPU-selection requests.
Windows imports only SDL2 and Windows system DLLs.

All **41 audio checks** pass on the dummy SDL device, and all **5 importer checks** pass.
Offscreen render integration passes all four quality presets, optional filtering,
menu persistence, ambience settings, MSAA fallback and resize/render-scale handling.
Serial and four-thread simulation fingerprints match in 12-car separated and pileup
workloads. Workflow YAML, shell and embedded Python syntax checks pass; Mac packaging
and universal targets pass Makefile dry-run validation. Native Mac/Windows CI and
real GPU/driver performance verification remain pending GitHub access.

The full experiment run, focused comparisons, final report and build logs are retained
under `build/review/0.1.8/`. The suspension experiment code remains available only with
`CZ_FREE_COILS=1` and/or `CZ_HIGH_SPEED_DAMPING=1`. The standalone autopilot benchmarks
still have unmet performance targets, as described above.

## Release state

GitHub access was restored on 2026-10-14. A fresh fetch confirmed that every branch
commit is merged into `main`; no open pull requests or uncommitted work remain.
All branches and `v0.1.8` were pushed. A focused local networking recheck passes all
ten scenarios, resolving the seven socket restrictions from the original full run.

The [tagged workflow](https://github.com/Benbone21/CrumpleZone/actions/runs/38644019223)
builds the Linux DEB, Windows installer and universal Mac app/DMG successfully. Native
Windows passes all 19 selected scenarios, audio checks and serial/parallel fingerprints.
Mac networking (19 scenarios), audio and full rendering checks also passed in the tag
workflow. Rendering took about nine minutes on Apple Software Renderer. Publication
was stopped during investigation, before the packaged-app check completed. Windows
intermittently crashes at thread exit with `0xC0000005`, despite passing every scenario;
an isolated rerun passed but the diagnostic run reproduced it. The native cache cleanup
correction and software-renderer timeouts are recorded in
[the 0.1.9 report](release-merge-0.1.9.md). Logs are retained under
`build/review/0.1.8/github/`.

Local logs are in `build/review/0.1.8/`. Prepared release packages are in
`build/release-0.1.8/`. The earlier review is preserved in `docs/platform-verification.md`.
