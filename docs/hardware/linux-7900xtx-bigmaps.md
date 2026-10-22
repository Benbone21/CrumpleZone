# Big maps: Linux 7900 XTX measurements

Before and after each phase of `docs/plans/big-maps.md`. Machine: Ryzen 9 9900X (24 threads), RX 7900 XTX (Mesa 26.1.6 radeonsi), 31 GB RAM, Debian 13. Every run used `nice -n 19`.

How to measure:
- **World build:** `build/worldtime [--scans] [MAP...]` (headless). It prints the build time, the RAM added, the grid, the object counts and a land hash. The hash covers heights and water to 1 cm, surfaces, and object positions. A phase that must not change the maps must leave the hashes unchanged.
- **In game:**
  - `CZ_LOADPROF=1 ./crumplezone --load-tour --no-audio --no-pad` for map switches and their worst frames;
  - `./crumplezone --no-audio --no-pad --map N --bench 600,120` for frame times, with the peak RSS sampled from `/proc`.
- **Physics fingerprint:** `build/bench_sim 12 separated 4000`.

## Baseline: main @ 898c8e2 (2026-10-22)

| Map | World build | RAM added | Map switch in game | Worst frame in switch | Bench avg (HIGH 1600x900) | 1% low | Peak RSS |
|---|---|---|---|---|---|---|---|
| Proving ground | 0.08-0.10 s | 24 MB | - | 65 ms | - | - | - |
| Mountain | 0.22-0.38 s | 37 MB | - | 93 ms | - | - | - |
| Frontier | 0.94-1.20 s | 46 MB | 1.49 s | 75 ms | 6.27 ms | 18.3 ms | 917 MB |
| Backcountry | 0.55-1.02 s | 46 MB | 1.08 s | 53 ms | 5.52 ms | 16.2 ms | 926 MB |
| Open country | 0.12-0.21 s | 26 MB | - | 24 ms | - | - | - |

Other baseline numbers:
- Cold startup on the proving ground spends 12.7 s of CPU in 32 world builds on workers, plus 4.4 s on the section park.
- Dedicated server RAM after startup: 91 MB on Frontier, 80 MB on Backcountry.
- Fingerprint: `91850b5879f5e3e3`.
