# Multiplayer motion, sound and lights (2026-10-13)

The previous implementation anchored remote playback to the arrival of each newest
snapshot. Variable network delay therefore changed the playback time abruptly, producing
backward jumps and variable apparent speed. Engine RPM/load/boost and reverse gear were
not transmitted. The renderer also forced every remote headlight switch to AUTO, and
the audio mixer only synthesized the selected player's engine.

## Changes

- Remote presentation follows a monotonic clock with slowly corrected sender/local clock
  offset. The existing 100 ms interpolation buffer is retained. Steering, braking, wheel
  spin, RPM/load/boost and damage interpolate between snapshots. Brief packet gaps coast
  for at most 250 ms; prolonged gaps stop the remote body's velocity.
- Three bounded fragment assemblies per car let an older snapshot finish while a newer
  one is only partially received. Fragment order and overlapping packet assembly are
  tested. Snapshot build counters reject queued states from before a car was rebuilt.
- Discontinuous recovery/repair moves clear the interpolation history and jump to the
  new pose instead of sweeping through intervening geometry or generating huge velocities.
  Initial spawns now send the vehicle origin instead of passing its center of mass to
  `Car::build`; this prevents the extra height followed by a drop on the first snapshot.
- Headlight mode is a reliable TCP message, independent of the UDP interpolation buffer
  and sleeping-car update rate. The current mode is included in spawn messages for late
  joiners. OFF/LOW/HIGH/AUTO are used directly by the renderer. Brake state and reverse
  gear arrive in the periodic snapshots.
- Snapshot RPM, effective engine load and boost drive the same engine synthesizer used
  for the local car. Up to eight nearby engines mix with distance attenuation, stereo
  positioning and fades on departure. This includes remote vehicles and nearby local
  AI cars. Voices keep their identity as distance ordering changes.
- Engine synthesis is shared in `src/engineaudio.*`; voice constants and filter coefficients
  are cached. The audio callback consumes a fixed-size triple buffer, with no locks or
  allocations for nearby-engine updates. Existing tires, impacts, wind and music remain.

The wire protocol is now **3**. **Both computers must run this updated build.** Older
protocol-2 releases are rejected at the join handshake and shown as incompatible in
discovery; they cannot decode the new spawn/snapshot fields.

This improves snapshot presentation and missing shared state. Each player still owns
their own vehicle simulation, and cross-player collision forces are not a new centralized
authoritative physics system. A live two-PC test is still needed to assess collision
feel and audio balance on actual speakers.

## Verification

Production snapshot encoding/decoding, reliable light messages, fragment assembly and
remote playback are exercised without sockets by `tests/net_state.cpp`. All three
scenarios passed. With simulated variable delay (10–80 ms) and reordered packets,
a car at 10 m/s had no backward jumps; its largest 120 Hz presentation step was 0.09 m,
and the wheel animation's largest step was 0.17 rad. The tests also check late joins,
reverse gear, RPM/load/boost, light changes, stale states after rebuilds, truncation,
stopping after packet loss and recovery teleports.

Offline audio checks passed audibility, left/right pan, distance attenuation, departure
fades, bounded finite samples, all seven engine voices, concurrent snapshot publication,
and SDL's callback startup/shutdown using the silent dummy driver. No speakers or visible
window were used. The Windows game, scenario runner and audio test cross-compile locally.

```bash
nice -n 19 make -j6 build/run_tests build/test_audio
nice -n 19 ./build/run_tests --jobs 1 -v net/state
SDL_AUDIODRIVER=dummy CZ_AUDIO_DEVICE_TEST=1 nice -n 19 ./build/test_audio
```

The local sandbox denies socket binding (`Operation not permitted`) and Wine execution,
so a real host/client session cannot be verified here. Release CI now includes native
Windows loopback networking, state/playback, worker and audio tests. That workflow was
edited locally and has not been run from this session. Final full-suite results are in
`docs/performance.md`.
