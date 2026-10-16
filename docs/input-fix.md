# Input fix: why Esc and the DualSense stopped working in the editor-ui build

Written on branch `inputfix` (merge of `main` cac2742 and `editor-ui` 4b48b76). For OpenCode and
anyone else reading later. The short version: nothing was wrong with SDL, the DualSense, or Esc
itself. The editor-ui branch forked ~130 commits before `main`, so it never had `main`'s working
controller code, and its keybinds commit moved the menu-key handling to a place where it only
sees one event per frame by accident.

## 1. Esc not opening the pause menu: root cause

Commit 4b48b76 ("customizable keybinds") deleted `case SC_ESCAPE:` from the key switch inside the
event loop and re-added the menu key *after* the loop:

```cpp
SDL_Event e;
while (SDL_PollEvent(&e)) { ... }          // loop ends when the queue is empty
...
if (e.type == SDL_KEYDOWN && !evKeyRepeat(e) && !menu.open ...) {   // <-- after the loop
    if (sc == settings.keys.menu) openMenu();
```

After the loop, `e` is just *the last event the loop happened to read this frame*:

- A quick tap is a KEYDOWN and a KEYUP in the same frame. The last event is the KEYUP, so the
  menu key is never seen. At 60 fps and a normal key press this is most presses.
- Any mouse-motion event after the press (the mouse is captured while driving, so there are
  plenty) also hides it.
- On a frame with no events at all `SDL_PollEvent` leaves `e` untouched, so a stale KEYDOWN is
  looked at again every frame: the same bug made the gear keys (E/Q, moved there too) repeat
  until another event arrived.

Every action moved into that block (gear up/down, winch, strap, steering assist) had the same
flaw; Esc is just the one people notice. OpenCode's later "attempts" (polling
`SDL_GetKeyboardState`, restoring the original `SDL_KEYDOWN` + `evKeyScancode(e) == SC_ESCAPE`
check) kept the deleted `case SC_ESCAPE` out of the switch, so they never touched the actual
problem.

Other defects in the same commit that bit the same code:

- `PauseMenu::rows()` lost its `return r;` when the keybinds page was spliced in
  (`if (page == 5) { return keybindsRows(s); } button(BACK_ROW, "BACK"); }`). Falling off the end of
  a function returning `std::vector` is undefined behaviour: this is the "corrupted size vs.
  prev_size" crash in the report.
- `Settings` saved two different lines called `steeringassist` (the on/off setting and the key's
  scancode). The loader's first match won, so the key never loaded and the on/off setting was
  set from a scancode (19 = always on).
- The Keybinds page bound *every* key to the selected row, arrows and Esc included, so the page
  couldn't be navigated or left.
- `P` as the default steering-assist key collides with `main`'s P = pause. Every letter is already
  used, so the default is now F4.
- `CONTROLS` (the help screen) was replaced by the keybinds page, losing the help overlay.

### The fix (on `main`'s architecture)

- `src/keybinds.h/.cpp` (no SDL, no GL): `Keybinds`, with `gameKey(scancode, fromPad)`. The key
  switch in `main.cpp` stays written against the *default* keys. A physical key is first turned
  into the default key of the action it is bound to, so the switch (and Esc) never needs to know
  about remapping. The `case SC_ESCAPE:` is back in the switch, inside the event loop.
- Esc always opens/closes the menu: it can't be bound to another action (the MENU row can get a
  second key, F1 say, but Esc keeps working). Gamepad button presses are fed in as default keys
  already, so they are not remapped: Start = pause menu whatever the keyboard has bound.
- The keybinds page is page 7 of the pause menu (Settings -> CONTROLLER -> KEYBOARD KEYBINDS):
  Enter on a row, then press the key; Esc cancels; keys that are taken swap; RESET TO DEFAULTS.
  Settings lines are `key_throttle`, `key_brake`, ... (the old bare names are ignored).
- Test: `tests/keybinds.cpp` (`keybinds/esc-opens-menu`, `keybinds/remap`, `keybinds/settings`).
  It builds synthetic SDL key events (down, up, repeat), runs them through `gameKey`, feeds
  Start into the real `pad::Logic`, and checks rebinding, swapping and loading garbage.
- Scripted check: `--press FRAME:SCANCODE,...` pushes real key events (down and up in the same
  frame) through SDL's queue, and logs `press-test: frame N menu OPEN/CLOSED`:
  `SDL_VIDEODRIVER=offscreen nice -n 19 ./crumplezone --no-audio --player 3 --shot-prefix /tmp/p --shot-frames 100 --press 30:41,70:41`
  (opens at 31, closes at 71).

## 2. DualSense "No gamepad found"

OpenCode's build used the raw *joystick* API (`SDL_JoystickOpen`, `SDL_INIT_GAMEPAD`, a name that
only exists in SDL3). `main` has had controller support since 2026-10-13: `src/gamepad.h/.cpp`
(logic) and `src/gamepad_sdl.cpp` (device) on SDL2's GameController API, with hot-plug through
`SDL_CONTROLLERDEVICEADDED/REMOVED`. It started with `SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)`,
and on this machine it printed `CONTROLLER CONNECTED: DUALSENSE WIRELESS CONTROLLER`. Nothing to
fix there; the branch just never had it. The merge keeps `main`'s system and drops OpenCode's.

One small hardening: `Device::start()` sets `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1` (default
priority, so the environment still wins), so pad events (including a Bluetooth reconnect while
the window is being focused) aren't dropped. The game still ignores pad *input* while the window
has no focus (`Device::read(focused)`). The HIDAPI PS5 hints are left at SDL's defaults.

## 3. What came from editor-ui

Merged cleanly: editor fixes (A/D, wheel zoom, Q/E size, offset, object editor), the six physics
suites (`simulation/`, `vehicle-physics/`, `dynamics/`, `extreme/`, `environment/`, `towing/`),
toggleable steering assist, keybinds (re-done on `main`'s input handling, above).
Adapted: tests that used the Blazer (index 4) now cover the catalog `main` has. Jesko: only the
anti-roll bar change kept; the stiffer springs and `lateralGrip 1.15` made
`freeway/top-speed/koenigsegg jesko` fail (scraping, 5 m off the lane, or spun at 330 km/h) on
`main`'s newer physics, so those two edits were not applied (the comments in `vehicles.cpp`).
Dropped: the Chevy Blazer (154fcfd) vehicle, body, mods and tests (being redone elsewhere), and
OpenCode's raw-joystick `GamepadState`, plus the uncommitted menu redesign in the release
worktree (not part of any commit).
