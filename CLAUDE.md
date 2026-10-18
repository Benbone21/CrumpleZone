# Crumple Zone

A C++ soft-body off-road driving game. It uses SDL2 plus its own minimal OpenGL 3.3 loader, and has no other dependencies. The README explains the game and the code layout.

- Build (Linux): `make crumplezone build/run_tests`. Tests: `./build/run_tests --jobs 4 [filter] [-v]`. Never pass `--update-baseline`.
- On a Windows or Mac test machine: read `docs/agent-hardware-optimization.md` first and follow it. It is in phase 1 (test and report only, no code changes) until the user says otherwise.
- Windows build: install MSYS2 (`winget install MSYS2.MSYS2`), then `pacman -S --needed make mingw-w64-ucrt-x86_64-gcc git`. Run make with `MSYSTEM=UCRT64`, e.g. `C:\msys64\usr\bin\bash.exe -lc "cd <repo> && make build/win/run_tests.exe"`. Windows baseline: `docs/hardware/windows-rtx4070-baseline.md` (861 tests: 11 known failures, flaky engine/performance, and new net/bandwidth failure; physics fingerprint `3db86794fec05596` differs from Linux).
- Physics must stay bit-identical across builds (`-ffp-contract=off`; `tools/bench_sim` prints the state fingerprint).
- Benchmarks: `--bench N,WARM` for one scene, `--bench-suite` for the full set, and `--debug-screen` to show the F3 stats in screenshots. Measure in a real window; offscreen rendering may use a different GPU.
