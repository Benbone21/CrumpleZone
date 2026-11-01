# One map: Linux 7900 XTX measurements

The 8 km big map (`docs/plans/one-map.md`) against the 6 km Backcountry it grew from. Machine: Ryzen 9 9900X (24 threads),
RX 7900 XTX, 31 GB RAM, Debian 13. Every run used `nice -n 19`, with other agents' work running (load average 6-8 for the
in-game runs, which were taken back to back with main's at the same load). Branch `wip/onemap`.

## The world alone (headless, no cache, best of 2-3)

| | 6 km (main) | 8 km big map | Ratio |
|---|---|---|---|
| World build | 2.05 s | 3.1-3.4 s | 1.6x |
| RAM added by the world | 313 MB | 608 MB | 1.9x |
| Peak RAM during the build (scanned rocks, cache written) | 691 MB | 1303 MB | 1.9x |
| Read from the world cache | 0.33 s | 0.64 s | 1.9x |
| World cache file | 32 MB | 82 MB | |
| Trees / rocks | 66.7k / 8.5k | 71.9k / 11.5k | |

(The square is 1.78x the cells; the regions add their old maps' objects; nothing grows on the edge mountains past 3072 m.)

## In the game (`--map 4 --quality 2 --bench 600,120`, 1600x900 HIGH, a private world cache)

| | main 6 km | big map | Ratio |
|---|---|---|---|
| Launch to the end of the bench, cold | 9.7 s | 10.9-11.3 s | 1.14x |
| ...warm (cache) | 4.9-5.1 s | 5.9-6.1 s | 1.2x |
| Renderer ready, cold / warm | 2.4 / 1.5 s | 3.0 / 2.2 s | |
| Peak RSS, cold (first launch) | 1.21 GB | 1.96 GB | 1.62x |
| Peak RSS, warm | 1.03 GB | 1.45 GB | 1.41x |
| Frame time at the trailhead (avg / 1% low) | 3.45-3.77 / 5.9-6.1 ms | 3.43-3.58 / 5.9-6.6 ms | same |

The bench's 720 frames take about 4 s of each launch time above.

At each region's start (the big map) against that old map on its own (`--standalone`), same settings:

| Start | Old map alone | On the big map |
|---|---|---|
| Mountain (`--map 1`) | 3.30 ms, 1% low 6.07 | 3.19 ms, 1% low 5.92 |
| Frontier (`--map 3`) | 2.54 ms, 1% low 3.57 | 2.83 ms, 1% low 4.28 |
| Open Country (`--map 5`) | 1.99 ms, 1% low 3.13 | 2.15 ms, 1% low 2.84 |

The extra 0.15-0.3 ms at the Frontier and the Open Country is the GPU drawing more land into the distance (the old maps ended in a
backdrop) and the CPU's per-frame loops over the bigger map.

## Dedicated server (`build/crumplezone-server --map 4`)

| | |
|---|---|
| Startup, cold / warm | 5.9 s / 1.1 s |
| RSS after startup, cold / warm | 954 MB / 603 MB (the 6 km map: 436 MB, `docs/hardware/linux-7900xtx-bigmaps.md`) |

## Same map everywhere

- Land hash (`World::landHash`, heights and water to 1 cm, surfaces, every object) of the big map: `0924b024284809eb` on Linux and on
  the Windows build under wine (`build/win/worldtime.exe 4`), the same built on 1 thread or many and read back from the cache
  (`onemap/same-every-time`).
- Physics fingerprint (`bench_sim 12 separated 4000`): `da17a95524fab674`, unchanged.

## Against the targets (plan section 6)

- Load within 1.5x: yes (1.14x cold, 1.2x warm to the end of the bench; the world build alone 1.6x).
- RAM within 1.5x: warm yes (1.41x); the first launch's peak is 1.62x (the world cache being written while the world is in memory).
- Frame rate: the same at the trailhead; at the Frontier's and the Open Country's starts 8-11% slower than those old maps alone were.
