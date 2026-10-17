#!/bin/bash
# Profile-guided optimization for the release builds (opt-in; nothing else depends on it).
#   tools/pgo.sh linux     profile build/rel, then: make PGO=use build/rel/crumplezone   (or: make PGO=use deb)
#   tools/pgo.sh rebase    point a profile trained in another checkout path at this one (the Linux CI job)
#   tools/pgo.sh windows   same for the mingw build (trained under wine), then: make PGO=use windows
# Steps: build the instrumented binaries (PGO=gen), run a fixed training workload (the CPU sim, a test subset, two short
# offscreen game renders), then leave the profile in build/pgo and the instrumented objects removed. Exit status 0 only
# if the profile is complete; on ANY failure it cleans up and exits 1, and the caller builds without PGO (never a blocker).
#   env: JOBS (make -j, default nproc), LTOJ (LTO partitions, default auto)
# Only the sim (bench_sim, run_tests), not the GPU driver, is what the profile teaches the compiler, so the render runs are short.
set -u
cd "$(dirname "$0")/.."
JOBS=${JOBS:-$(nproc)}
PGODIR=$PWD/build/pgo
mangled=${PWD//\//#}   # (/a/b -> #a#b, the way gcc names profile files)
here_rel="${mangled}#build#rel#"
fail() { echo "pgo: $* (building without PGO)" >&2; rm -rf $CLEAN; exit 1; }
CLEAN=""

case "${1:-linux}" in
linux)
    CLEAN="build/rel $PGODIR"
    rm -rf build/rel "$PGODIR"
    make -j"$JOBS" PGO=gen build/rel/bench_sim build/rel/run_tests build/rel/crumplezone || fail "instrumented build failed"
    export GCOV_PREFIX_STRIP=0
    # CPU sim: the serial path, the worker pool, and a crash-heavy pileup.
    build/rel/bench_sim 12 separated 600 1 || fail "bench_sim"
    build/rel/bench_sim 12 pileup 300 4 || fail "bench_sim pileup"
    # Scenario subset (physics, collision, net, tools).
    build/rel/run_tests --jobs 1 engine/collision engine/physics-workers tires/pressure net/state vtools/ || fail "run_tests subset"
    # Offscreen game: the draw/cull/scenery path on two maps (software GL is fine; only the game's own code is profiled).
    for map in 4 1; do
        SDL_VIDEODRIVER=offscreen LP_NUM_THREADS=2 CZ_BENCH=1 build/rel/crumplezone --no-audio --no-pad --map $map --quality 0 \
            --size 480x270 --time 10 --photo --adaptive 0 --shot-prefix build/pgo-shot --shot-frames 60 || fail "game render map $map"
    done
    rm -f build/pgo-shot*
    ls "$PGODIR"/*.gcda >/dev/null 2>&1 || fail "no profile data written"
    rm -rf build/rel   # objects were built with -fprofile-generate; the next build has to be PGO=use
    echo "pgo: profile in $PGODIR ($(ls "$PGODIR" | wc -l) files)"
    ;;
windows)
    # The mingw compiler's profiles only fit its own build (gcc stamps them with its version; Linux gcc differs), so the
    # instrumented CPU-side exes (bench_sim, run_tests: the sim, collision, net, tools) are trained under wine. The game exe
    # itself (render code) gets no profile on Windows and builds as plain LTO.
    CLEAN="build/win build/wineprefix $PGODIR/win"
    command -v wine >/dev/null 2>&1 || fail "no wine to run the instrumented Windows build"
    rm -rf build/win "$PGODIR/win"
    make -j"$JOBS" PGO=gen build/win/bench_sim.exe build/win/run_tests.exe || fail "instrumented Windows build failed"
    export WINEPREFIX=$PWD/build/wineprefix WINEDEBUG=-all WINEARCH=win64 DISPLAY=${DISPLAY:-}
    wine build/win/bench_sim.exe 12 separated 600 1 || fail "wine bench_sim"
    wine build/win/bench_sim.exe 12 pileup 300 4 || fail "wine bench_sim pileup"
    wine build/win/run_tests.exe --jobs 1 engine/collision engine/physics-workers tires/pressure net/state vtools/ || fail "wine run_tests subset"
    ls "$PGODIR"/win/*.gcda >/dev/null 2>&1 || fail "wine wrote no profile data"
    rm -rf build/win build/wineprefix   # instrumented objects: the next build has to be PGO=use
    echo "pgo: Windows profile in $PGODIR/win ($(ls "$PGODIR/win" | wc -l) files)"
    ;;
rebase)
    # A profile trained in another checkout path (e.g. downloaded from another CI job): rename it to this one's.
    for f in "$PGODIR"/*#build#rel#*.gcda; do
        [ -e "$f" ] || continue
        b=$(basename "$f"); n="${here_rel}${b#*#build#rel#}"
        [ "$b" = "$n" ] || mv "$f" "$PGODIR/$n"
    done
    ;;
*) echo "usage: $0 linux|windows|rebase" >&2; exit 2 ;;
esac
