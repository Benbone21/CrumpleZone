# Release verification — 0.1.9

All local and freshly fetched remote branch commits were merged into `main`, and all
branches were pushed. No open pull requests or uncommitted feature work remain. The
[integration report](release-merge-0.1.8.md) records source corrections and the complete
original regression run. Release 0.1.8 was stopped before publication while native CI
issues were investigated.

## Native CI corrections

Windows scenario checks passed individually but the process intermittently crashed at
thread exit with access violation `0xC0000005`. This matches MinGW's documented
[emulated TLS destruction-order defect](https://github.com/msys2/MINGW-packages/issues/2519).
Scratch vectors and autopilot caches now use independently allocated objects in native
Windows FLS. Their cleanup no longer depends on emulated TLS storage surviving until
C++ destruction. Linux and Mac retain ordinary C++ `thread_local` storage. Trivial
thread-local counters and pointers retain their existing implementation. Physics
calculations, ranges and baselines are unchanged.

The new `engine/thread-cache` scenario checks independent data and exact destruction
counts across 128 joined threads. Native Windows CI also repeats autopilot, wire-state
and cache checks in five separate processes, exercising production caches at shutdown.

Mac CI reports `Apple Inc. / Apple Software Renderer`. The original complete render
check passed all presets, settings, texture and MSAA checks in about nine minutes; the
initial unbuffered diagnostic run identifies the delay inside the first scene draw.
Graphics and packaged-app checks now have 15-minute timeouts suitable for this software
renderer. Check output is immediate, rather than hidden in buffered stdout until exit.
Every graphics assertion remains enabled. The packaged app must launch from outside
the repository, render a screenshot and load its bundled ground assets successfully.
The mounted DMG must contain the signed universal app and an Applications shortcut.

## Validation and limits

The initial full run is 448/468 passing with thirteen existing physics failures and
seven sandbox-denied network scenarios. With network access restored, all ten focused
network checks pass. All seven newly merged functional checks, 41 audio checks and five
importer checks pass. Linux render integration passes on the local AMD GPU and llvmpipe.
The extracted portable Linux DEB starts and renders away from the source directory.

The release workflow gates publication on Linux, Windows build, native Windows tests
and native Mac tests. It publishes the Windows installer EXE, Linux DEB, universal Mac
DMG/ZIP and checksums only when every required job succeeds. CI logs are retained under
`build/review/0.1.8/github/`; final release downloads go under `build/release-0.1.9/`.

High-speed damping and free-coil experiments remain disabled by default after the
original regression review found instability. Autopilot tail-latency targets remain
unmet, command-popup rendering/input remains planned, and no real Mac/NVIDIA GPU FPS
gain is claimed. Native serial/parallel checks establish deterministic simulation
within each platform, not bitwise equality between different compilers or platforms.
