# Verification: performance and tire pressure, 0.1.10

## Included work

All previous feature branches were already merged into main. Fresh fetch and open-PR checks found no outstanding branch commits or PRs. This review includes the late tire-radius/autopilot/rut commits made after 0.1.9, OpenCode's uncommitted adaptive-quality and tire-inflation work, its engine-recording evaluation note, and the requested OpenCode engine-audio handoff. Original OpenCode changes and generated auditions are preserved locally under `build/review/0.1.10/`; generated previews and raw downloads are not game assets.

## Review fixes

- Adaptive quality originally modified `Settings::gfx` directly, potentially saving reductions as user choices. Runtime settings are now separate. Enable/disable, menu edits and saved toggle persistence are handled; scripted captures remain deterministic by default.
- Use actual frame time rather than reciprocal smoothed FPS. Ignore startup/suspend discontinuities, use independent elapsed-time hysteresis counters, preserve supersampling ceilings up to the user's setting, and permit recovery under 60 Hz VSync. Existing menu IDs remain stable; the initial suspected collision was not an actual collision.
- The first full run exposed a new Bronco ledge-climb regression from the added fixed-radius shrink. The existing tire spring already softens with pressure and lowers the body under load. Remove the duplicate geometric shrink from physics, traction control, autopilot, loose-wheel contacts and ruts. Share the original stiffness formula with the visual loaded-radius calculation, which leaves unloaded tires full size and handles low stock pressures without dividing by `stockPsi - 12`. Damaged tires retain their original rim/hub contact behavior.
- Apply visual vertical compression after wheel rotation. The tire ellipse no longer rotates with the tread. Only tire rubber widens/compresses; the rim retains its original transform.
- The new hiss originally consumed a per-frame trigger once per callback and advanced its envelope once per buffer using a single-sample time step. It now reads a persistent atomic target and advances per sample. Filters remain stable across 8–192 kHz; mute and release behavior are covered. A pre-existing glass-filter instability at low device rates was also fixed without changing its 48 kHz frequency.
- Tire hiss stops while paused, moving, or at pressure limits. Faster requested deflation/onboard/station rates are retained.
- macOS image verification retries temporary resource-busy errors. The packaged-app smoke draws one frame at low quality with photographic ground assets, while the native rendering test still exercises every graphics preset.

## Measurements

`CZ_BENCH=1`, offscreen, Backcountry, Ultra, 900×560, fixed time, no audio/controller, 180 frames (first 40 excluded), two runs per configuration. GPU: AMD Radeon integrated graphics (radeonsi); shared host, including an independently running user game. Tests/builds use nice 19 and limited concurrency.

| Configuration | Run 1 | Run 2 | Effective render scale |
| --- | ---: | ---: | ---: |
| 0.1.9 binary, fixed | 49.87 ms | 50.14 ms | 100% |
| Candidate, fixed | 50.37 ms | 50.43 ms | 100% |
| Candidate, adaptive | 38.48 ms | 38.53 ms | 50% |

The adaptive figure is approximately 24% lower scene render time with reduced visual quality; it is not a same-quality optimization or a guaranteed 60 FPS result. CPU/physics targets and Mac/NVIDIA FPS gains are not claimed.

Local separated-car and pileup fingerprints match between one and four workers. Both new headless scenarios (`engine/adaptive-quality`, `tires/pressure`) pass. Audio: 45 checks including real callback code, zero callback allocations, hiss/mute/block-size/rate checks. Importer: five tests. Graphics/menu: 12 checks including four presets with low-pressure tires at different spin angles, saved adaptive control, optional GPU features, MSAA fallback and resizing. Final game smoke renders offscreen.

Logs, screenshots, source patch, benchmark commands and full-suite report are under ignored `build/review/0.1.10/`. Native Linux/Windows/Mac jobs, Windows thread-exit stress, Mac serial/parallel determinism, universal architecture/signature/DMG checks and a packaged-app render gate the GitHub release.

## Full regression run

The first full run passed **457/471 scenarios** (447.4 seconds), with the thirteen existing failures plus the newly detected `offroad/ledge-crawlers` regression. The duplicate sag model was then corrected. The final focused rerun passes **4/4**: `offroad/ledge-crawlers`, `offroad/air-down`, `offroad/shaft-break`, and `tires/pressure`. The complete final-code rerun passes **458/471 scenarios** (446.4 seconds). Its thirteen failures exactly match the previously documented set; the newly introduced ledge failure is gone. The 176 baseline metric drifts are informational, and no ranges or baselines were changed. Thirteen physics scenarios were already failing before this release: `frontier/current`, `backcountry/autopilot-far`, `freeway/top-speed/turbo utv`, `structure/weight/koenigsegg jesko`, `structure/weight/toyota tacoma`, `components/axle-landing`, `components/tire-into-fender/rock buggy`, `components/tire-into-fender/turbo utv`, `structure/rock-hits/4runner`, `offroad/whoops`, `tires/swap`, `tires/shear`, and `tires/debead`. No test ranges or baseline files were changed.


Final native Windows verification passes 22/22 curated scenarios, five independent 5/5 thread-exit stress runs, all 45 audio checks, and matching one/four-worker fingerprints. The actual Ubuntu-built DEB identifies itself as 0.1.10/amd64 and successfully renders with bundled assets after extraction and launch from an unrelated temporary directory. Native release workflow: https://github.com/Benbone21/CrumpleZone/actions/runs/38664084681 (application commit `aa9dc431b08dbd342cda572d1db8ca720e9fd546`).

Native macOS regression checks also pass, including networking, adaptive quality, pressure controls, audio, all graphics presets, and matching serial/parallel simulation fingerprints. The packaged-app launch, mounted-DMG signature/architecture checks and publication are subsequent gates in the linked workflow.
