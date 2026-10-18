# Crumple Zone

A C++ soft-body off-road driving game. It uses SDL2 plus its own minimal OpenGL 3.3 loader, and has no other dependencies. The README explains the game and the code layout.

- Build (Linux): `make crumplezone build/run_tests`. Tests: `./build/run_tests --jobs 4 [filter] [-v]`. Never pass `--update-baseline`.
- On a Windows or Mac test machine: read `docs/agent-hardware-optimization.md` first and follow it. It is in phase 1 (test and report only, no code changes) until the user says otherwise.
- Physics must stay bit-identical across builds (`-ffp-contract=off`; `tools/bench_sim` prints the state fingerprint).
- Benchmarks: `--bench N,WARM` for one scene, `--bench-suite` for the full set, and `--debug-screen` to show the F3 stats in screenshots. Measure in a real window; offscreen rendering may use a different GPU.
