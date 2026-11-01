# Engine recordings: preparation and source status

Three banks play real recordings: `honda_d16a6`, `honda_b16a` and `honda_b18c` (the EF Civic's stock engine and its two engine upgrades, and any car with one of those engines swapped in). All three are cut from one CC0 recording of a **Mercedes 190E inline-4 on a dyno**. That is an approximate match, not the Honda engines themselves: it is the same layout (an inline-4 firing every 180 degrees) and a similar size. Every other engine still uses its procedural voice, and the preview tool says which voice each engine uses. Research references below identify engine families; they are not redistributable game assets. The repository's CC0-only asset rule remains in effect. No purchases have been made.

## The 190E banks (Honda inline-4s)

**Source.** "The Dyno Sessions: 190E" by Chippy569 on Freesound, recorded by David of Track Time Audio: <https://freesound.org/people/Chippy569/packs/9658/> (pack 9658). The pack's own readme gives the license, Creative Commons 0, for the pack and for each sound, with each sound's URL. Retrieved 2026-11-01. The originals (490 MB, 96 kHz 24-bit) stay in the ignored `assets/source/190e/`; only the cut loops are committed. Credits and the take list are in `assets/audio/engines/SOURCES.txt`. Each bank's `import-plan.json` lists every loop's take, mic, window and measured rpm.

**Rebuilding.** `python3 tools/prepare_190e_banks.py` (it needs numpy) writes the loops and a plan per bank under `assets/source/190e/prepared/<key>/`. `--analyze` prints the measurements and writes nothing. Then run `python3 tools/import_engine_audio.py assets/source/190e/prepared/<key>/plan.json` for each key. Delete the old bank first, because the importer will not overwrite one.

**Measured rpm, not the file names.** An inline-4 fires twice per turn, so its strongest order is the 2nd (rpm/30 Hz). The tool tracks that order (summing the 2nd, 4th, 6th and 8th orders, in 10 ms steps) on the distant mic, which shows the firing most clearly. The numbers in the names are not always the rpm:

| Take | Measured | Notes |
|---|---|---|
| 19_500 | 1290 rpm (1274–1312) | under load; strongest peaks at 43 and 86 Hz |
| 19_2500 | 2556 rpm | under load, very steady |
| 20_6500 | 6210 rpm (drifts 6250 → 6185) | under load |
| 21_500 | 1010–1060 rpm, hunting | idle: the same rpm the coast-down settles to in 17_medsweep |
| 21_2500 | 2430 rpm | unloaded |
| 21_6500 | 6295 rpm | unloaded (not used: it is a held rev, not a coast) |
| 17_medsweep / 18_medsweep | 6900 → 1100 rpm | throttle shut at the top, coasting to idle |
| 17_shortsweep | 1700 → 6400 rpm in 1.8 s | free rev up |
| acceleration-sweep | 1200 → 7100 rpm in 4 s | loaded pull (4062 mic only) |

Checks: the envelope of each hold repeats at rpm/120 (one 720-degree cycle), and all three mics of a take agree to within 1%. 19_500 and 21_500 are not at 500 rpm. Read as 2nd order, their spectra give 1290 and 1030 rpm. 1030 is also exactly where the medsweep coast-down settles at idle.

**Load states.** The readme says nothing about load. The takes show it:
- Take 21 is far quieter than takes 19 and 20 at the same rpm: about 25 dB on every mic at 2500 rpm, and about 30 dB at idle. Its spectrum is also thinner. At 2500 rpm the d6 kick mic's centroid is 1250 Hz in take 21 but 270 Hz in take 19, because take 19 has the deep exhaust thump of an engine working against the dyno. A change in recorder gain cannot change the shape of the spectrum, so take 21 is the engine **unloaded** and takes 19/20 are **loaded** holds.
- Nothing in the pack is a held rpm with the throttle shut. Load 0 (coast; the game uses it whenever the throttle is closed, including at idle) is therefore made from the closest real material:
  - the idle (21_500);
  - the unloaded 2500 hold (21_2500), which is a light throttle and the nearest steady thing to a cruise coast;
  - two slices of the **coast-down after the throttle shuts** in the medsweeps, at about 4600 and 5900 rpm. These are the only true overrun sounds in the pack.
- The unloaded 6500 hold (21_6500) is left out, because a held rev at 6300 does not sound like lifting off.
- Load 1 (throttle) uses the three loaded holds plus one slice of a rev-up at about 4000–4800 rpm (17_shortsweep, or the loaded acceleration sweep on the 4062 mic). That slice fills the gap between 2556 and 6210.

**Loops.** Each window is "de-chirped": it is resampled along its measured firing phase so that the loop holds one steady rpm and spans a whole number of engine cycles. That puts the seam in phase. It also makes slices of a sweep loopable (the rpm moves 10–25% across a 0.35–0.7 s slice), and it flattens the idle's ±2.5% hunting. The start is chosen, a quarter turn at a time, where the loop's end runs on best into its start. The loader still crossfades the wrap over 5 ms. Sweep slices also have their slow swell or fade divided out (an 80 ms RMS envelope). Holds keep their own texture. The loops are 0.3–3.3 s long, at 48 kHz, 16-bit mono, and stored at -18 dBFS RMS. The bank gains set the levels: loaded is louder than coasting, and both get louder with rpm. They are matched to the procedural voices' preview levels (idle about 0.05 RMS). The banks are 1.2–1.3 MB on disk each.

**Three Hondas from one engine.** All three banks use the same windows. The differences between them come from mic choice and a formant shift. The shift reads the take slightly faster or slower and labels the loop with the rpm it now fires at. The game therefore still plays the right firing frequency, but the exhaust's resonances move:

| Key | Mic | Formants | Intent |
|---|---|---|---|
| `honda_d16a6` | distant | as recorded, softened above 9 kHz | stock 108 hp SOHC: the plainest |
| `honda_b16a` | 4062 (close, bright) | +5% | VTEC 1.6: raspier, smaller |
| `honda_b18c` | distant + the d6 kick mic below 250 Hz (time-aligned) | −3% | Type R 1.8: fuller, deeper |

The highest loaded loop is about 6000–6500 rpm. The B16A and B18C rev to 8200/8400, so above that point the top loop is pitched up as much as 1.4 times, and the loader's mip levels keep that free of aliasing.

**Wiring.** `engineSamples::forVehicle` already maps the EF Civic (catalog vehicle 0) to these keys by engine name, and a swapped engine to the bank of its origin vehicle. Nothing else needed wiring. The Bronco's 2.3 EcoBoost is also an inline-4 (`ford_23_ecoboost`), but it is turbocharged, so it stays procedural. The Baja's air-cooled flat-4 fires unevenly and sounds nothing like this, so it has no bank.

**Previews.** `./build/engine_preview build/review/engine-190e assets/audio/engines honda` renders an idle, three rev blips, and a full pull (flat out to the redline, lift, coast to idle) for each of the three engines. Measured on the renders: the 2nd order follows the scripted rpm to within about 3%, there are no sample-step outliers (clicks), and the levels are idle 0.05 RMS and pull 0.13–0.14 RMS. Procedural is 0.05 and 0.14–0.17. Those numbers do not say whether it sounds good: listen to the files.

## Muted.io evaluation

The muted.io "Performance Cars" CC0 sample pack (68 files, 96kHz/24-bit) was evaluated but not imported. The pack contains field recordings of cars driving, not loopable engine samples. The files are numbered 001-068 without descriptive names, making it difficult to identify specific engine types. The recordings are not designed for pitch-scaling, which the game's audio system requires to simulate different RPMs from a small set of clips. The pack is retained in `assets/source/performance-cars-free-sample-pack-mutedio/` for future reference if better clip selection techniques are developed.

## Reference research

| Game vehicle / stock engine | Reference or candidate | Status |
|---|---|---|
| Civic EF / D16A6 1.6 SOHC | [Tanabe D16A6 Civic exhaust demonstrations](https://www.tanabe-usa.com/sounds.asp?do=13) | Same engine family with aftermarket exhaust; reference only. Site does not authorize copying. A 2005 EX 1.7 library is the wrong engine. In game: the CC0 190E inline-4 stand-in above. |
| Jesko / 5.0 flat-plane twin-turbo V8 | [Koenigsegg engine demonstration](https://www.youtube.com/watch?v=MVVBJG6Nt_I) | Manufacturer reference; asset reuse rights not verified. Generic V8 flybys are not an exact match. |
| Bronco / 2.7 EcoBoost V6 | [Watson Wu 2022 Wildtrak library](https://www.watsonwu.com/vehicles/ford-bronco-suv-sound-effects-library) | Exact engine candidate; paid, not acquired. Game and public-source redistribution rights require verification. |
| 4Runner / supercharged 3.4 5VZ-FE V6 | [Supercharged 5VZ-FE owner demonstration](https://www.youtube.com/watch?v=6Nhk3zL9OuQ) | Engine-family reference in a different vehicle/exhaust; reuse rights not verified. A 2022 4Runner 4.0 NA library does not match this engine. |
| Rock buggy / LS3 6.2 V8 | [Southern Performance Systems engine videos](https://www.southernperformancesystems.com/main-gallery-hub/videos.html) | Builder references with differing cam/exhaust configurations; no exact buggy recording or reuse rights verified. |
| Side-by-side / turbo Rotax 900 triple | [Can-Am Maverick X3 manufacturer demonstration](https://www.youtube.com/watch?v=BcKOSR-QJcg) | Correct engine family reference; reuse rights not verified. Renegade V-twin and naturally aspirated Ryker packs are unsuitable substitutes. |
| Tacoma / 3.5 2GR-FKS V6 | [Watson Wu 2020 Tacoma library](https://www.watsonwu.com/vehicles/toyota-tacoma-truck-sound-fx-library) | Exact engine candidate; paid, not acquired. Game and public-source redistribution rights require verification. |

[Muted.io's performance-car field recordings](https://muted.io/performance-cars/) were evaluated but not imported. The pack contains field recordings of cars driving, not loopable engine samples suitable for the game's pitch-scaling audio system. External references have not been auditioned in this environment.

## Recorded bank format

The local-recording importer and audition workflow are documented in
[audio-soundscape.md](audio-soundscape.md). It requires documented CC0 PCM recordings
and explicit measured RPM/load clip ranges. No vehicle audio is synthesized by the importer.

The startup loader reads `assets/audio/engines/<engine-key>/bank.txt`. It requires source, author and license metadata, plus at least two distinct RPM loops for each of coast (0) and load (1):

```text
source https://example.com/original-recording
author Original recording creator
license CC0-1.0
loop 850 0 0.5 idle-coast.wav
loop 3000 0 0.5 cruise-coast.wav
loop 850 1 0.5 idle-load.wav
loop 3000 1 0.5 cruise-load.wav
```

Each loop row gives RPM, load state, gain and a plain filename inside the bank. WAVs must be mono/stereo PCM16/24/32 or float32, 8–192 kHz and 0.1–4 seconds long. Banks allow up to 16 loops with 16 MB per bank and 64 MB total decoded storage. Invalid or incomplete banks use procedural fallback. The loader verifies format and metadata presence; it does not establish legal permission.

Engine keys cover the stock and garage choices: `honda_d16a6`, `honda_b16a`, `honda_b18c`, `jesko_5l`, `ford_27_ecoboost`, `ford_23_ecoboost`, `ford_30_raptor`, `toyota_5vz`, `toyota_5vz_supercharged`, `toyota_1gr`, `toyota_1gr_supercharged`, `gm_ls3`, `gm_ls7`, `gm_ls3_supercharged`, `rotax_900_turbo`, `toyota_2gr`, `toyota_2gr_supercharged`.

Loading, seam treatment and anti-aliasing filters run before device startup. The callback blends RPM/load loops, smooths transitions and shares the same engine banks with nearby multiplayer voices without file I/O or allocation. Reloading an active audio device's banks is refused. Tests generate temporary WAV fixtures; those fixtures are not shipped as vehicle recordings.
