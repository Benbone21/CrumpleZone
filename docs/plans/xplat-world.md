# The same world on every machine

**Problem (2026-10-27):** in multiplayer between Linux and Windows (and Mac), rocks were in different places in the Backcountry. Every
player builds the world on their own machine, so anything the build decides differently on another OS is a rock the guest drives
through, or hits where the host has none.

## What differed

Measured with `build/worldprint` (below) on Linux and on the Windows build under wine, at commit a45dd9e:

- **The scanned-rock fit** (`makeScannedRock`, src/world.cpp). Each rock picks a scan by a score made of two `std::log`s, sorted with
  `std::sort`. A scan as wide as it is long ties with itself turned round, and which of the two the sort puts first decides which way
  round (and so how big along each axis) the rock is. `log` rounds differently in glibc, the Windows CRT and Apple's libm, and `std::sort`
  orders ties differently in libstdc++ (Linux, mingw) and libc++ (macOS). Under wine 6 rocks of 62,940 were refitted (up to 0.7 m
  bigger or smaller, the same scan in the same spot). Putting the ties in any other fixed order on Linux refits about 2,000 of them:
  roughly what a Mac (libc++) had.
- **Everything else** that goes through `sin`, `cos`, `atan2`, `hypot`, `exp`, `pow`: rivers, trees, poles, props and trails differed in
  the last bits (1e-12 m: not visible, but not the same world, and any of it can cross a threshold on another machine).
- Not a cause: thread count (1, 2 and 3 threads give the same bits), `-march=native` against x86-64-v2 with LTO (the same bits: no FMA,
  `-ffp-contract=off` is on every target), the world cache (keyed by the build), the hull library (text with 5 decimals, read the same
  everywhere), `unordered_*` (only looked up in the Backcountry's build, never walked).

## The fix

- `src/detmath.h`: `dm::sin, cos, tan, atan, atan2, exp, log, pow, hypot, asin, acos, tanh, cbrt, rotateAxis`, built from + - * /,
  sqrt and bit operations only (fdlibm's sin, cos, atan, atan2 and exp; a double-double, correctly rounded log and hypot). The world's
  build uses these: backcountry, onemap, onemapbuild, regionland, bctrails, forest, rivers, places, biome, mountain, frontier, sections,
  spawnpad, bridgelayout, freeway and world.cpp. An LD_PRELOAD count of the C library's math calls during a Backcountry build: none left.
- `src/detsort.h`: `dm::sort`, the same introsort as libstdc++'s `std::sort` (checked against it on 200,000 tie-heavy arrays), so ties
  come out in Linux's order on a Mac too. Used for the rock fit and the trunk measure (forest.cpp).
- The physics is untouched (`bench_sim 12 separated 4000` = da17a95524fab674 still).
- The procedural world keeps the C library's math in the rock fit (`cLibm` in makeScannedRock) while it's being worked on; its own
  files (procworld, procdress, procreveal: a dozen `sin`/`cos`/`atan2`/`hypot`) still need the same change.

## What changed on Linux

The game's Backcountry (scanned rocks): 30 rocks of 62,940 refitted (the same scan in the same spot, sized or turned otherwise: their
centres move under 5 cm, their size up to 0.3 m; 24 of them under a metre across, 6 big ones of 3 to 5 m. The score's log is now
correctly rounded where glibc's isn't, and those rocks were on a near-tie). Land, trees, logs, bushes, props, places and
trails are the same to the millimetre (to the last bit, but for 1e-12 m in a few). The dedicated server's world (code-made rocks): the
same to the millimetre. So `net::kVersion` is 10 (a guest on the old build would have those 30 rocks otherwise), and the world cache's
format is 7.

## Checking it

- `build/worldprint [--scans] [--threads N] [--dump FILE] [MAP...]` (also `build/win/worldprint.exe`, `build/rel/worldprint`): a hash
  per category (terrain, ground, water, rocks, trees, logs, bushes, boxes, poles, props, places, trails). `--dump` writes every object and
  every 256 m tile of land as a line, doubles as their bits, to `diff` two machines' worlds and find the first thing that differs.
- `run_tests onemap/xplat-fingerprint`: the same hashes for the big map (code-made and scanned rocks) against Linux's, which are written
  in tests/onemap.cpp. CI runs it on Linux, Windows and macOS. If the map is changed on purpose, run it on Linux and paste the new
  `WORLDPRINT` lines' hashes into the test.

## Left over

- A tree's trunk radius comes from its model's mesh (forest.cpp measure, modelkit in double rounded to float): the libm there is
  rounded away by the floats, but not provably everywhere.
- The model lab's picks (`modelkit` variantIncluded / pickedParams, saved in the player's config) decide trees' designs and trunk radii:
  two players with different picks build different trees.
