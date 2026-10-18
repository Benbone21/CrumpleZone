# Crumple Zone on mobile: plan

Status: planned, not started (2026-10-17). Do the performance work first.

## Why it's feasible
- **SDL2** already supports Android and iOS. It gives us the window, input, audio, threads and file paths.
- **No external libraries.** The only links are system libraries, and the game has its own tiny OpenGL loader (`src/gl.h`).
- **The renderer is OpenGL 3.3 core**, close to **OpenGL ES 3.0** (every phone from about the last 8 years).
  - All 29 shaders use `#version 330 core`.
  - The only desktop-only feature found is `GL_CLAMP_TO_BORDER` for the shadow map (`src/render.cpp` ~2215).
- **Shipped assets are about 40 MB.** `assets/source` (758 MB) is source material and isn't loaded.

The real work is touch controls, physics cost on a phone CPU, heat and battery, and app-store packaging.

## Phases

### 0. Finish the performance work first
Mobile needs all of it, and each item gains roughly twice as much on a phone:
- the POTATO preset
- LODs and billboards
- a cheaper ground shader
- car sleep and the rigid proxy
- per-stage timing

### 1. OpenGL ES 3.0 renderer, tested on desktop
- Pick the shader header at build time: `#version 300 es` plus precision qualifiers on mobile, `330 core` on desktop. The shader bodies stay the same.
- Request an ES 3.0 context via SDL behind a build flag. The loader already resolves functions through SDL.
- Shadow map: replace border clamp with clamp-to-edge plus a bounds check in the shader.
- Render targets: use RGBA16F, not RGBA32F.
- MSAA: use FXAA or 4x MSAA.
- Run the Linux build in ES mode (Mesa supports it) and add it to the tests and CI, so desktop work can't break mobile.
- Size: medium. One Sonnet agent. No phone or SDK needed.

### 2. Android build
- SDL's Android project template (Gradle + NDK). Add a CMake or Android.mk file next to the Makefile.
- Assets: pack them into the APK and load them via `SDL_RWops`. Route every asset `fopen` through it. Saves and config go to SDL's per-app pref path (`configDir()`).
- ARM64. Add a NEON path if the physics SIMD work lands.
- Watch for POSIX/desktop-only calls (we just fixed a Windows `setenv`).
- Sideload a test APK first. Play Store later.
- Needs: the Android SDK + NDK installed (a download for the user to approve) and a phone with USB debugging.

### 3. Touch controls and phone UI (the biggest design job)
- On-screen controls:
  - steering by tilt or a left thumb slider;
  - throttle and brake on the right;
  - buttons for gear, handbrake, winch and interact;
  - camera drag.
- Bluetooth controllers already work through SDL's gamepad path.
- Bigger tap targets in the menus. Text fields open the on-screen keyboard (SDL text input; `src/textedit.*` already takes SDL_TEXTINPUT).
- Hide the editor and dev keys on mobile builds.
- Safe areas for notches and rounded corners.

### 4. Phone performance targets
- **MOBILE preset:**
  - 0.6–0.75 render scale;
  - no AO or bloom;
  - one shadow cascade or blob shadows;
  - short draw distance with fog;
  - fewer grass and plant instances;
  - billboards;
  - ASTC/ETC2 texture compression.
- **Physics is the main risk.** One full soft-body car; the others asleep or on the rigid proxy. Fewer substeps on low settings, and a cap on traffic.
- **Heat and battery.** A 30/60 fps cap (FPS TARGET exists now). Pause properly when the app goes to the background.

### 5. iOS
- The same ES renderer works. Apple deprecated GLES but it still runs.
- Needs a Mac and Xcode; the CI Mac job exists.
- Long term: Vulkan via MoltenVK, or Metal.

### 6. Cross-play
- Phones and PCs use the same protocol.
- Mobile clients join servers only; they don't host.

## Open questions for the user
- Do you have an Android phone to test on?
- Android or iPhone first?
- Steering feel: tilt, an on-screen wheel or slider, or left/right buttons?

## Recommended first step
Phase 1 only, with one light Sonnet agent: the ES 3.0 renderer, with desktop unchanged, plus an ES-mode test. It needs no phone or SDK, and it shows how close the game already is.
