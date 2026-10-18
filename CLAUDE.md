# Crumple Zone

A C++ soft-body off-road driving game. It uses SDL2 plus its own minimal OpenGL 3.3 loader, and has no other dependencies. The README explains the game and the code layout.

## If you are an agent on the user's Windows or Mac laptop
You are doing **phase 2: optimizing for this machine.**
1. `git checkout main && git pull`.
2. Read `docs/agent-hardware-optimization.md` (the rules, sections 4 and 5) and `docs/hardware/phase2-plan.md` (your task list).
3. Do step 0 of the plan: rebuild, then make a new baseline with `--bench-suite`, the tests and the `bench_sim` fingerprint.
4. Work down your machine's list in order on branch `hw/<os>-<gpu>-phase2`. Push that branch, never `main`. Write your report as `docs/hardware/<os>-<gpu>-phase2.md`.

If the user says "sync", pull main again and re-read both files, because the plan may have changed.

## Build and test
- Linux: `make crumplezone build/run_tests`. Tests: `./build/run_tests --jobs 4 [filter] [-v]`. Never pass `--update-baseline`. Put a time limit on every run: `timeout -k 10 N ...`.
- Windows: install MSYS2 (`winget install MSYS2.MSYS2`), then run `pacman -S --needed make mingw-w64-ucrt-x86_64-gcc git`. Run make with `MSYSTEM=UCRT64`, e.g. `C:\msys64\usr\bin\bash.exe -lc "cd <repo> && make build/win/run_tests.exe"`. `make windows` builds the game.
- Mac: `make mac-dev` (see `docs/hardware-testing.md`). If the Xcode license blocks clang, `export DEVELOPER_DIR=/Library/Developer/CommandLineTools`.
- Physics must stay bit-identical across builds (`-ffp-contract=off`). `tools/bench_sim` prints the state fingerprint. Each platform currently has its own fingerprint (making them identical is paused): report yours and keep it unchanged by your edits.
- Benchmarks: `--bench N,WARM` for one scene, `--bench-suite` for the full set, `--debug-screen` to show the F3 stats in screenshots. Measure in a real window; offscreen rendering may use a different GPU.
- Baselines: `docs/hardware/windows-rtx4070-baseline.md` and `docs/hardware/mac-m1-baseline.md`.
