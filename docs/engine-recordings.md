# Engine recordings: preparation and source status

No real-car recordings have been imported. The game continues to use procedural engine audio for all engines. The preview tool reports this fallback explicitly. Research references below identify engine families; they are not redistributable game assets. The repository's CC0-only asset rule remains in effect. No purchases have been made.

## Muted.io evaluation

The muted.io "Performance Cars" CC0 sample pack (68 files, 96kHz/24-bit) was evaluated but not imported. The pack contains field recordings of cars driving, not loopable engine samples. The files are numbered 001-068 without descriptive names, making it difficult to identify specific engine types. The recordings are not designed for pitch-scaling, which the game's audio system requires to simulate different RPMs from a small set of clips. The pack is retained in `assets/source/performance-cars-free-sample-pack-mutedio/` for future reference if better clip selection techniques are developed.

## Reference research

| Game vehicle / stock engine | Reference or candidate | Status |
|---|---|---|
| Civic EF / D16A6 1.6 SOHC | [Tanabe D16A6 Civic exhaust demonstrations](https://www.tanabe-usa.com/sounds.asp?do=13) | Same engine family with aftermarket exhaust; reference only. Site does not authorize copying. A 2005 EX 1.7 library is the wrong engine. |
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
