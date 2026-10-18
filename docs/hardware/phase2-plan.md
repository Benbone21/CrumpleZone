# Phase 2: optimizing on the Windows and Mac laptops

Read `docs/agent-hardware-optimization.md` first. Its rules (sections 4 and 5) still apply. Use this file as your task list. Work through your machine's list in order, one item at a time: measure, fix, re-measure, commit. Stop when you run out of clear wins rather than making speculative changes.

## Step 0, both machines: new baseline first
Main changed a lot after the phase 1 runs. Main now has all of these:
- **Car drawing:** an intact car is one draw and is not rebuilt each frame.
- **Physics:** it runs during the GPU wait, uses SIMD, and the worker pool is faster.
- **Slow-frame fixes:** car part merges are spread over frames.
- **Map switches:** they load on a worker behind a fade.
- **Pileup contact:** 2 to 2.7 times faster.
- **Far traffic:** it uses a cheap proxy.
- **Bench fix:** `--bench` no longer serializes the CPU and GPU, which affected the old SWAP numbers.

Steps:
1. `git pull` on main, then rebuild.
2. Run `--bench-suite` once at the bench size (1600x900) and once at native resolution.
3. Run the test suite and the `bench_sim` fingerprint.
4. Put the results at the top of your phase 2 report (`docs/hardware/<os>-<gpu>-phase2.md`) as the new "before".
5. Set the OS power mode to best performance if you can. If you can't, say so; the Windows run used Balanced.

## Already being handled on the Linux machine (don't redo)
- A cheaper ground shader path for LOW and POTATO (fewer taps and layers). HIGH up close stays the same.
- The Mac `fieldrepair/steps` bad_alloc. It's being hunted with ASan/UBSan.
- A model-cache thread race (`models/threads` failing intermittently).
- **Physics differs between platforms.** Linux is `1cb2a908f74c3983`, Windows `3db86794fec05596`, Mac `69497071eddf9087`. Making them identical is paused for now (multiplayer works without it, since each player owns their car and the server sends positions). Don't try to fix it locally: report your fingerprint, and make sure your own edits don't change it.

If one of these turns out to be specific to your machine, say so in your report instead of fixing it.

## Note: physics changed on 2026-10-18
The rear-knuckle fix for the Jesko weave changed physics on purpose, so every platform's `bench_sim` fingerprint changes once. Linux's is now `1cb2a908f74c3983`. After you sync, record your new fingerprint as the reference for checking your own edits.

## Windows (RTX 4070 Laptop, Ryzen AI 9 HX 370, 2048x1280 at 240 Hz)
Phase 1: GPU-bound at LOW and above. TERRAIN is about 80% of GPU time at HIGH, 5 ms at 1600x900 and about 8 ms at native. POTATO is capped by about 1.5 ms of SWAP.

1. **`net/bandwidth` fails on Windows.** It fails in 1 s: "guest N joins" fails and the guests see 0 trucks. The other net tests pass, so find what this test does differently: ports, how many sockets, how fast it reconnects, loopback, non-blocking connect, `SO_REUSEADDR` semantics on Winsock, or the firewall. If it's a real game bug, Windows players could fail to join busy servers, so it comes first. Fix it in the shared net code, kept portable.
2. **The scan-line artifact at HIGH.** Horizontal lines across the screen at HIGH only, on every map. Also seen on the Mac, but not reported on Linux. Find the cause by toggling HIGH's differences from MEDIUM one at a time: AO, MSAA 4, shadows 3, bloom, and anything else `applyPreset` sets (see `src/perf.cpp`). Take `--debug-screen` screenshots of each and look at them. Likely suspects:
   - AO's half-resolution upsample;
   - an MSAA resolve into the HDR target with a row offset;
   - a precision or format problem on NVIDIA.

   Fix it in shared code so the Mac gets the fix too. Tell the user so the Mac agent skips it.
3. **OPAQUE CPU at 5.4 to 6.5 ms at native HIGH while its GPU time is about 0.** This is likely the driver making the CPU wait inside that stage. Find which call blocks: a buffer update that syncs (`glBufferSubData` on an in-use buffer, or a missing orphan), a query readback, or NVIDIA's "Threaded optimization". Remove the stall.
4. **The POTATO SWAP floor, about 1.5 ms.** Check how the window presents:
   - fullscreen desktop or exclusive (flip model compared with DWM composition);
   - the swap interval;
   - whether a GL-on-DXGI interop path is in use.
   
   Measure fps at POTATO in each mode. Also check whether the FPS TARGET cap (default MATCH DISPLAY) or VSync was on during the bench.
5. **TERRAIN on NVIDIA at MEDIUM and HIGH.** After step 0, if TERRAIN is still the biggest stage, profile the ground shader with Nsight Graphics if it's installed (don't install large tools without asking the user). Otherwise toggle its features one at a time. Propose changes that keep HIGH's close-up look. The user judges visuals harshly, so include screenshots.
6. **Auto-detect.** Start the game once normally with no `gfxchosen` in settings: move `%APPDATA%\crumplezone\settings.txt` aside and restore it afterwards. Report which preset it picks and whether it's right for this laptop.

## Mac (Apple M1, 8 GB, Retina 2560x1600, GL 4.1 over Metal)
Phase 1: GPU-bound. Only POTATO is playable (55 to 69 fps). A 1600x900 window rendered at 2880x1676, because Retina doubles both dimensions. GL timer queries return 0, so stage GPU times are unavailable. The large PROPS CPU time is where the driver waits for the GPU.

1. **Retina and HiDPI.** Render scale should be relative to the window's logical size. The default on HiDPI should be logical pixels (e.g. 1440x900 on this screen), with the option to go higher. Check `SDL_GL_GetDrawableSize` against the window size everywhere the renderer sizes targets, UI and the mouse. The UI must stay sharp and the right size. Measure fps before and after at each preset; this alone should be a large gain.
2. **Auto-detect for Apple M-series.** M1 should get POTATO or LOW once Retina is handled. Check that the warm-up probe steps down correctly. Report what it picks (move `~/.config/crumplezone/settings.txt` aside, then restore it).
3. **Where the GPU time goes.** Use Instruments (Metal System Trace) or Xcode's GPU frame capture on the GL app, if available without installing anything large. If not, toggle features one at a time (shadows, AO, MSAA, bloom, grass, ground quality) and measure each. Rank them by cost. Check what PROPS is actually waiting on: it's probably the first draw after a heavy pass.
4. **The first-use stall.** The first MEDIUM run had one 2.1 s frame in TERRAIN and OPAQUE. Find out whether it's shader compilation on Apple's GL-over-Metal. The shader binary cache may not work on macOS: check whether `configDir()/shadercache` fills up and is used on the second launch. If it isn't used, warm every shader permutation behind the loading screen.
5. **The scan-line artifact at HIGH.** The Windows agent owns this one. Only verify its fix on the Mac once it's in main, unless the user asks you to take it.
6. **Mac-only test failures.** `net/lan-discovery` and `net/custom-port` may be the macOS firewall or the sandbox; check and report. `frontier/autopilot-lodge` is probably the physics difference; just report it.

## Coordinating the two machines
- Both of you may touch shared files (render.cpp, main.cpp, net.cpp). Keep changes small and focused. Use `#ifdef _WIN32` or `__APPLE__` only for truly platform-specific code; a fix that is right everywhere goes in plainly. Pull main before you start each item to keep conflicts small.
- Branch: `hw/<os>-<gpu>-phase2`. Commit each item separately with its measured gain in the message. Push the branch and never push main. The Linux machine merges it after running the full suite there.
- Physics must stay bit-identical on your own machine: your `bench_sim` fingerprint must not change because of your edits.
- Report: `docs/hardware/<os>-<gpu>-phase2.md`. Include:
  - the step 0 baseline;
  - for each item: what you found, what you changed, and the before and after fps per preset;
  - anything you couldn't do and why;
  - what's left, ranked.
