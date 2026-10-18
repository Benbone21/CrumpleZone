#!/bin/sh
# Checks the hang watchdogs end to end (run from the repo root after building):
#   run_tests: a scenario that sleeps past a tiny --scenario-timeout is reported as HANG and the process exits non-zero.
#   the game:  a stalled loop (test hook CZ_TEST_STALL_FRAME) prints "HANG: no frame for" and exits 3; SIGTERM ends a scripted run at once.
set -u
fail=0
out=$(timeout -k 5 60 ./build/run_tests --selftest selftest/hang --scenario-timeout 2 2>&1); rc=$?
echo "$out" | grep -q "^.*HANG selftest/hang after" && [ $rc -eq 1 ] && echo "ok   run_tests reports the hang (exit $rc)" || { echo "FAIL run_tests hang (exit $rc)"; echo "$out"; fail=1; }
if [ -x build/crumplezone-dev ]; then
    out=$(SDL_VIDEODRIVER=offscreen CZ_WATCHDOG_STALL=3 CZ_TEST_STALL_FRAME=5 timeout -k 5 120 ./build/crumplezone-dev --no-audio --no-pad --size 640x360 --shot-prefix /tmp/wd_selftest --shot-frames 400 2>&1); rc=$?
    echo "$out" | grep -q "HANG: no frame for 3 s at frame" && [ $rc -eq 3 ] && echo "ok   game watchdog (exit $rc)" || { echo "FAIL game watchdog (exit $rc)"; echo "$out" | tail -5; fail=1; }
    SDL_VIDEODRIVER=offscreen CZ_TEST_STALL_FRAME=5 CZ_WATCHDOG_STALL=1000 ./build/crumplezone-dev --no-audio --no-pad --size 640x360 --shot-prefix /tmp/wd_selftest --shot-frames 400 >/dev/null 2>&1 &
    pid=$!; sleep 15; kill -TERM $pid; sleep 2
    if kill -0 $pid 2>/dev/null; then echo "FAIL game survived SIGTERM"; kill -9 $pid; fail=1; else echo "ok   SIGTERM ends a scripted run"; fi
fi
exit $fail
