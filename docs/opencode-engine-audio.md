# OpenCode task: add real vehicle engine audio

Replace the procedural engine sound with properly sourced recordings where we can find suitable, redistributable audio. Work in the current repository and read `agent.md`, `docs/audio-soundscape.md`, and `docs/engine-recordings.md` first. The project already has an engine-bank loader, CC0 PCM importer, preview tools, and audio tests. Extend those paths; do not build a second audio pipeline.

## Rights and sourcing

- The current asset rule permits **CC0-1.0 recordings only**. A page being publicly viewable or downloadable does not grant redistribution rights. Do not use YouTube rips, streaming captures, unclear sample packs, or recordings marked only “free for personal use.” Do not purchase anything or accept a different license without the user’s explicit approval.
- Research original sources online. Prefer the actual engine and vehicle configuration used by the game. Check the source page and its license statement for each recording; retain direct URLs, creator names, license evidence, and retrieval dates. Do not infer a license from a search result, another person’s repost, or a site-wide claim that does not cover the specific file.
- Record exact matches separately from approximate matches. Never label a generic V8 as a Jesko recording, for example. If only an approximate but clearly CC0 recording is available, document what it actually is and use it only if it is a useful, honest match.
- If no suitable CC0 recording can be verified for an engine, leave that engine on its current procedural voice and report the source/rights gap. Do not weaken the CC0 restriction to make the task appear complete.
- Keep downloaded originals under ignored `assets/source/` for local processing only. Do not commit a source file unless its license explicitly permits redistribution. Commit only the processed loops, the importer-generated bank manifest, and a source/rights note sufficient to audit the asset.

## Recording plan

Start with a small representative set of engines and expand only when the first banks sound and behave well. The supported keys and current research status are in `docs/engine-recordings.md`; keys include `honda_d16a6`, `jesko_5l`, `ford_27_ecoboost`, `toyota_5vz_supercharged`, `gm_ls3`, `rotax_900_turbo`, and `toyota_2gr`.

For every bank, collect clean, continuous recordings at a minimum of two distinct measured RPMs for **both coast/idle (`load: 0`) and throttle (`load: 1`)**. Four loops are the minimum; more RPM points are useful when the source supports them. Prefer stationary/onboard recordings with little wind, speech, music, clipping, gear changes, or unrelated vehicle noise. Capture representative idle, midrange, and high RPM where possible. Do not guess RPM from pitch: use reliable telemetry, a tachometer visible in the recording, or a source description that substantiates it. Record whether the engine is naturally aspirated, turbocharged, supercharged, and any distinctive cylinder layout when known.

Use distinct coast and throttle clips at the same RPM when the source has them. Avoid gear shifts and transients in steady loops. Choose loop regions with consistent RPM and minimal background changes; use the importer to cut from the original with measured start times and durations. Inspect seams and ensure the looping portions do not click or contain long silences. A short crossfade or seam correction is acceptable only if it preserves the recording’s character and does not conceal bad source material.

## Import and integrate

1. Put verified source recordings in `assets/source/` and create a temporary JSON plan beside them. The plan must contain the exact engine key, original source URL, creator, `CC0-1.0`, and measured clip ranges. See the plan example in `docs/audio-soundscape.md`.
2. Run `python3 tools/import_engine_audio.py path/to/plan.json`. The importer validates PCM WAV format, clip bounds, RPM/load coverage, and memory limits. It creates `assets/audio/engines/<engine-key>/` and refuses to overwrite a bank. Review its `bank.txt` and `import-plan.json`; correct the plan and remove only a newly generated, unshipped bank if reimporting is necessary.
3. Keep each bank within the existing limits: 4–16 loops, at least two distinct RPM values per load state, no more than 16 MB decoded per bank or 64 MB total. Avoid filling every bank with weak or repetitive recordings merely for coverage.
4. Confirm the engine key maps to the intended vehicle/spec in `src/engine_samples.cpp` and vehicle configuration. Do not change physics, throttle response, RPM behavior, or sound levels to compensate for a poor recording. Make only focused audio changes if the existing blending needs improvement.
5. Preserve the procedural fallback for missing, invalid, or incomplete banks. Runtime loading and sample callbacks must stay safe: no file access, allocations, locks, or device operations in the audio callback. Recorded and procedural transitions must remain smooth, including vehicle/layout changes and nearby multiplayer engines.

## Verification and handoff

Run the importer’s tests and the audio checks described in `docs/audio-soundscape.md`. Build and run `build/engine_preview` on the generated banks and inspect its output for each engine, including coast-to-throttle and RPM transitions. Use the soundscape preview to check the engine against ambience/music levels. Check durations, peak/RMS, clipping, silence, seams, and memory totals; explain any automated metrics’ limits. Never claim subjective quality from waveform metrics alone.

Follow `agent.md` for host safety. Do not open a visible game window or start playback on the user’s machine. Use the preview tools and offscreen checks, then leave auditioning to the user if the environment cannot safely capture and inspect the rendered audio. Do not change physics baselines or widen test ranges to hide a regression.

Before finishing, update `docs/engine-recordings.md` with the real source, creator, exact license, retrieval date, engine-match accuracy, and which banks were actually added. Update release/audio notes if appropriate. Report engines with verified recordings, engines still using procedural fallback, license evidence, tests run, and any missing source/rights needed to continue. Never say all cars use real recordings if only some banks were sourced.
