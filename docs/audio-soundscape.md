# Audio, music and outdoor ambience

This audio pass is isolated in `wip/audio-soundscape`, in `/home/bread/Documents/GitHub/CrumpleZone-audio`, based on field-repair commit `6ed8b86`. The optimization agent's checkout and work remain in `/home/bread/Documents/GitHub/MemeNG`. Merge this branch after that agent saves its work; the small audio hooks in `main.cpp`, settings rows and Makefile entries may need combining with its edits.

## Audible changes

- Wind gusts and foliage rustle vary slowly in stereo. Mountain, Frontier and Backcountry have distinct outdoor profiles. Freeway has a wind-only profile.
- Short bird phrases have varied pitch, timing, spacing and stereo position; activity is strongest around dawn/dusk and fades at night. Quiet insects replace birds after dark. These are synthesized sounds, not field recordings or specific species.
- **Esc → Settings → AMBIENCE** controls the outdoor layer independently from music. It saves between launches; default 60%. Cockpit view attenuates the outdoors and softens the vehicle mix. Camera changes, volume changes and remote-engine panning smooth their transitions.
- Backcountry gets a new 72–88 BPM arrangement with electric piano, picked strings, warm bass, occasional vibraphone phrases and light hand percussion. It has its own harmony/rhythm weights and space between lead phrases rather than sharing the Mountain style.
- The music follows the country under you (`src/biome.h`): dunes and sand are slow, airy and dry (open fifths, a breathy pad, dry plucks, hand drum, a high flute; Dorian or Phrygian); pine woods are warm and woody (strummed and picked acoustic guitar, low strings, soft hand drum; major or mixolydian); red rock and cliffs are tense (a deep drone, slide guitar, a heartbeat tom, a long echo; Phrygian or minor); rivers, lakes and mud flow (running harp arpeggios, droplets of bells; lydian, mixolydian or major); snow and high bare ground are sparse and bright (glassy pads, music-box bells, no drums). Open meadow, roads and the pad keep the map's own piece, and so does the freeway. A new country has to hold for 3 s before the music takes it up, then it crossfades over 8 s; another change waits 14 s. `build/biome_preview [dir [seconds [seed]]]` writes a WAV of each (and of the maps' own pieces to compare levels).
- Music backs off modestly during hard throttle and impacts. Master volume now applies to vehicle sounds, music and ambience together; muting master actually silences every layer. Menu attenuation affects world sounds while music keeps its normal master-controlled level.
- Walking or flying away attenuates your car's engine, tire noise and speed wind. Nearby multiplayer engines retain their shared voices and gain smooth panning.
- Switching engine layouts or recorded banks crossfades the old and new voices over 40 ms, using the old engine's last control state while it fades.

- **Placed sounds** (`src/worldsound.cpp`, drawn by `Audio::renderWorldVoices`): a workshop din (`shop.wav`) plays from the nearest 1-2 repair stations / garage sheds within 55 m; the nearest point of the two loudest water bodies plays `river.wav` (flowing >= 0.5 m/s and over 1500 m2) or `stream.wav` (brooks; slack water and lakes at a fraction of the level), heard to 30-120 m by size; `forest.wav` is a bed in the pine woods by day only. Gain falls as (1 - d/range)^2, farther voices are duller, all follow the AMBIENCE volume, master and the cabin.

## Recorded engine sounds

**No real vehicle recordings were imported.** Existing banks still take precedence over procedural voices, and missing banks still use procedural fallback. Shell downloads are blocked by DNS in this session. Reference research and licensing status are in `docs/engine-recordings.md`; the CC0-only rule remains in effect.

`tools/import_engine_audio.py` now cuts documented local PCM recordings into startup banks. A plan supplies the verified source/author/license plus measured RPM, coast/load state, start time and duration for each clip. It retains the original recording, preserves format, refuses existing banks and invalid/partial plans, and writes a provenance manifest. It never invents missing recordings or RPMs. The runtime validates a bank before using it.

```bash
python3 tools/import_engine_audio.py path/to/bank-plan.json
make build/engine_preview
./build/engine_preview build/review/engine-audio path/to/engine-bank-root
```

Example plan structure (replace all example metadata and filenames with real authorized sources):

```json
{
  "engine": "honda_d16a6",
  "source": "Original CC0 recording URL",
  "author": "Recording creator",
  "license": "CC0-1.0",
  "clips": [
    {"file": "coast.wav", "start": 2.0, "duration": 1.0, "rpm": 850, "load": 0, "gain": 0.5},
    {"file": "coast.wav", "start": 8.0, "duration": 1.0, "rpm": 3000, "load": 0, "gain": 0.5},
    {"file": "throttle.wav", "start": 3.0, "duration": 1.0, "rpm": 850, "load": 1, "gain": 0.5},
    {"file": "throttle.wav", "start": 10.0, "duration": 1.0, "rpm": 3000, "load": 1, "gain": 0.5}
  ]
}
```

## Silent review and verification

```bash
nice -n 19 make -j 2 build/test_audio build/soundscape_preview
CZ_AUDIO_DEVICE_TEST=1 SDL_AUDIODRIVER=dummy nice -n 19 ./build/test_audio
nice -n 19 python3 tests/test_engine_import.py
nice -n 19 ./build/soundscape_preview
```

The preview tool writes `build/review/soundscape/index.html`, five 45-second music WAVs and three 25-second forest auditions (day, night, cockpit); it opens no device/window and plays nothing automatically. The output logs RMS, peak, DC and music generation CPU cost. These numerical checks do not establish subjective sound quality; audition the files before judging the mix.

Verification covers recorded-engine loading/playback, remote stereo engines, complete master mute/unmute, distance attenuation, day/night habitats, callback-block independence, sample-rate safety, five music arrangements, transition/mute tails, importer errors and preservation of source/existing banks. Native Windows/NVIDIA listening still needs a real machine. Vehicle physics and optimization code were not changed in this pass.

Results on 2026-10-14: **41 audio checks passed**, including SDL dummy-device lifecycle and no C++ allocations during mixer/map-change calls; **5 importer tests passed**, plus an importer-to-production-preview integration check using a temporary synthetic fixture. **8 offscreen render/menu checks passed**, including saved AMBIENCE volume. Native game and Windows game/audio-test executables compile, and `build/win/CrumpleZone-windows.zip` is rebuilt with the audio documentation. The full physics suite was not rerun because this pass does not change physics.
