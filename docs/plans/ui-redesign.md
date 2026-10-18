# In-game UI redesign: proposal (not implemented)

Status: 2026-10-17. Part 1 (the overlap fix, below) is done on wip/hudfix. Directions A (one grid) and C (contextual) are built on wip/hudac, with a smaller set of speedometer changes: see "Built: A + C" next. B is not started.

Reference shots of the current HUD (Blazer and Bronco on the proving ground, F3 off):
- before the fix: `/tmp/claude-1000/-home-bread-Documents-GitHub-MemeNG/974f9a4d-9df0-4dc6-906e-0a5ec985ab51/scratchpad/hudfix/before_1280.png` (fuel gauge sitting on the terrain tag)
- after the fix: `/tmp/claude-1000/-home-bread-Documents-GitHub-MemeNG/974f9a4d-9df0-4dc6-906e-0a5ec985ab51/scratchpad/hudfix/after_{1024x768,1280x720,1600x900,1920x1080,2560x1440,2560x1600}__0120.png`
- with F3 up: `/tmp/claude-1000/-home-bread-Documents-GitHub-MemeNG/974f9a4d-9df0-4dc6-906e-0a5ec985ab51/scratchpad/hudfix/f3_1280x720__0120.png`, `/tmp/claude-1000/-home-bread-Documents-GitHub-MemeNG/974f9a4d-9df0-4dc6-906e-0a5ec985ab51/scratchpad/hudfix/f3_1024x768__0120.png`

## Built: A + C (wip/hudac)
**A, one grid.** Every HUD element's rect comes from `src/hudlayout.h` (`u = H/720`, never recomputed there). New in it: `kDialR` (the dial's radius, 100u), `prompt()` (the bottom-centre stack: repair station, what you carry or hold, winch aiming; slot 0 is lowest, just above the vehicle card's tallest case; as wide as the gap left of the 4x4 schematic, shrinking then clipping), `fpsBlock()` and `playersList()` (top right: the players list sits under the FPS block when that is on, no longer under it). `tests/hudlayout.cpp` checks them with the rest at eight sizes (`hud/layout`, `hud/layout-prompts`). Same font, colours and panel look.

**Speedometer (the smallest set that reads better).** Dial 184u to 200u across. Speed digits 3.5x to 4.5x (999 still fits) with a brighter unit label. The drive mode (2H/4H/4L/4A) moved out of the text row into a coloured chip beside the gear (amber low range, green front driven, grey 2H). Redline zone is twice as thick; for manual-shift trucks the dial's rim flashes near redline (shift light). Below 720p only every other tach label is drawn. TC/MAN/locks stay a text row (icons: later). Compass unchanged.

**C, contextual (`src/hudcontext.h`, tests `hud/context`, `hud/context-full`).** Elements fade in over 0.15 s and out over 0.7 s (smoothstep, no popping); "full" shows everything within 0.08 s. Final rules:

| Element | Shows when |
|---|---|
| Compass, speedometer (gauges, lights, gear, mode chip), warning pills (ENGINE DEAD, FLAT TIRE...), OUT OF FUEL banner, toasts, prompts, vehicle name | always (faults and prompts are never hidden) |
| Fuel gauge | under 25% of the tank, out of fuel, for 4 s after fuel goes up or the can changes (refuelling), and while stopped |
| Terrain / tire tag | for 4 s after the surface changes (and when a vehicle appears), and while sunk (over 2 cm) or the tread is packed with mud |
| Inventory line (anchor gear) | on foot within 8 m of the truck, with its inventory open, while a rope is out, for 4 s after a count changes |
| Winch, strap, job, cash, level panels | only while in use (they never were always-on) |
| Damage bar and percent | for 5 s after damage goes up, and while damage is 50% or more of the bar |
| Weight and mod lines of the card | while stopped |
| Tire pressure (PSI, SWAY OFF) | while an air key is held, while off street pressure (over 1 PSI), for 4 s after it changes |
| 4x4 widget | for 4 s after the drive mode, an axle lock, the centre lock or the sway bar changes, and while in low range or with any diff locked |
| Everything above | always, with SHOW FULL HUD: ALWAYS (Settings, display; default CONTEXTUAL, saved as `fullhud`), or while the SHOW THE FULL HUD key is held |

"Stopped" has hysteresis (stopped under 0.4 m/s, moving again over 0.8 m/s) so a crawl doesn't flicker. A new vehicle shows its terrain, 4x4, pressure and anchor pieces once for 4 s. Space for hidden pieces stays reserved (the layout never moves with visibility), so nothing jumps. The hold key is **F9** (`key_hudall`, group CAMERA AND VIEW, default unbound elsewhere): the letters, digits and function keys 1-8, 10-12 are all taken or dev-gated (digits 1-7 swap vehicles in DEV KEYS mode, N and K are dev tools), and `keybinds/*` checks every default pair for clashes. Not done: a gamepad button for it (the pad already has few free buttons), fading the winch/job panels out (they have no state to draw once off).

## What Part 1 changed
`src/hudlayout.h` now computes the HUD's rects from one scale `u = max(0.75, H/720)` and the screen size, with no GL.
`tests/hudlayout.cpp` (`hud/layout`) checks that no two rects overlap and all stay on screen at 1280x720, 1600x900, 1920x1080, 2560x1440, 2560x1600, 1024x768, 1366x768 and 1440x900, with the longest text each element can carry.
- The terrain tag, the fuel gauge and the anchor line (STRAPS 3 SLING 2 ...) are a column above the dial, each in its own slot, right-aligned to the dial. A slot never moves with its text; a long line shrinks (to font scale 1) and is then clipped at 400u wide.
- Top left: playtest banner, career cash, job panel and winch panel used to all start at (12u, 12u). They now stack in that order, below the DEV KEYS pill when it is up, and stop short of the compass.
- Winch key hints break onto a second line instead of running under the level panel; the winch panel widens for its longest state text (it overflowed at some scales).
- The level run's clock panel and the toasts both started at y = 52u: toasts now start below the panel.
- The vehicle card's mod summary shrinks its font to stay left of the 4x4 schematic (it touched it at 1024x768).
- Chat lines are clipped before the fuel column.
Nothing about how `u` is computed changed.

## Inventory: HUD (in game, driving)
All HUD text is the 5x7 bitmap font at whole-number scales (`px(k*u)`); backgrounds are black pills at 0.4 to 0.55 alpha. Colours: amber (1, .75, .3) for warnings and keys, grey-white for values, green and cyan for status.

| Element | Shows | Where | When | Problems |
|---|---|---|---|---|
| Compass strip | headings N..NW, markers for repair station, summit, autopilot goal, buddies | top centre, 360u wide | maps with stations; hidden in scenario playback | marker labels hang below the strip in unboxed text (hard to read on bright sky); only ~135 deg visible |
| Speedometer dial | rpm arc and needle with 1000 rpm ticks, redline, speed number + KM/H or MPH, gear box | bottom right, 184u across | always | speed digits small for the most-read number; tach labels 0..8 are tiny at 720p; unit is the only place that says km/h vs mph; the tach dominates, speed is secondary |
| Indicator lights | TC OFF, MAN, 2H/4H/4L/4A, F LOCK, R LOCK, SWAY, P, AUTO | inside the dial, above the hub | when set (drive mode always) | tiny text inside the dial; drive mode appears again under the schematic; no icons |
| Throttle limit | THROTTLE 40% | under the gear box | cap under 100% | squeezed against the dial bottom |
| Terrain tag | surface, tire type, SUNK n CM, TREAD PACKED WITH MUD | column above dial | on the ground | was overlapped by fuel (fixed); a sentence of caps in 1x font; tire type rarely changes but is repeated all the time |
| Fuel gauge | litres, jerry can, bar | column above dial | fuel mode on, fuel trucks | text label with a bar; OUT OF FUEL banner is a separate centred pill |
| Anchor line | STRAPS, SLING, STAKES, BOLTS, ICE counts | column above dial | winch trucks only | a sentence of nouns and numbers, no icons; ICE?? is unexplained |
| 4x4 schematic | wheels lit when driven, locked diffs, prop shafts, mode name, PSI, SWAY OFF | left of dial | trucks with transfer case or lockers | mode name duplicated; PSI and SWAY OFF float above the pill in unboxed 1.1x text |
| Vehicle card | name, damage bar and %, mass and weight split, mod summary | bottom left | always | four lines of different styles and sizes; the mod summary is long amber text; weight split is jargon |
| Warning pills | ENGINE DEAD, WHEEL OFF, FLAT TIRE... | above the vehicle card | on fault | stack grows upward over the world; no priority order |
| Toasts | events, discoveries, laps | under compass, centre | 2.6 s | up to 3; wide ones cover the middle of the view |
| Hint line | "H: CONTROLS  ESC: MENU" | under toasts | first 7 s | lives in the toast stack so it jumps |
| Pause / slow-mo | PAUSED, SLOW MOTION | under compass | when active | |
| Winch / strap panel | state, length, force, load bar, key hints | top left | rope in use | hint line very long on pad and keyboard; top-left real estate is shared with 3 others (now stacked) |
| Winch aim | ring, anchor reticles, labels, stage prompt, crosshair | world and bottom centre | aiming | two bottom-centre pills at H-139u and H-110u |
| Job panel / banner | title, status, marker | top left, banner at 28% height | jobs | |
| Career cash, cache markers | cash, SUPPLY CACHE distance | top left, world | career | |
| Level run panel | clock, gates, medals, banner, multiplayer progress | under compass | level runs | collides with toasts (fixed) |
| Frontier lap / freeway odometer | lap times, trip and best speed | under compass | those maps | same stack as toasts |
| Players list, name tags | peers, ping, vehicle tags | top right, world | multiplayer | clashes with FPS/GPU text at top right |
| Chat | last 7 lines, SAY prompt | bottom left above the card | multiplayer | unbounded width (now clipped) |
| Repair station | status / SOMETHING'S BROKEN | bottom centre H-70u | on pad or broken | shares y with carry and hand prompts |
| Carry / in-hand prompts | what you hold and how to put it back | bottom centre H-110u | on foot | |
| Autopilot | status | pilot overlay | pilot on | |
| NEW BUILD pill, DEV KEYS, FPS | dev only | top | dev | |
| F3 debug screen | everything | all four corners | F3 | covers the whole HUD by design; toasts print over it |
| Help overlay | key list (keyboard or pad) | full screen | H | |

## Inventory: menus and full-screen panels
Main/pause menu and settings (`menu.cpp`: video, audio, controls, fuel, throttle, dev menu), garage and mod picker (`main.cpp` garage panel; career buy flow), inventory/trunk menu (`invmenu.cpp`, `invdraw.cpp`), world map (`coop.cpp`), key rebinding (`keybinds.cpp`), level and section pickers, the editor (`editor.cpp`, its own toolbars), the lab. Problems: the menus use the same pixel font but their own layout constants (not `hudlayout`), long single-line help strings, several panels sized in absolute u that assume 16:9, no focus ring consistent between mouse and pad, and no scrolling on small windows.

## Cross-cutting problems
1. Everything is positioned with magic numbers (`H - 245 * u`) in separate functions, so two features each look fine and still collide. Part 1 fixes this for the right column, the top-left stack and the card; the rest (centre bottom, top right) still use magic numbers.
2. Four different sentence styles: nouns in amber ("STRAPS 3 SLING 2"), values in white ("FUEL 79 L"), full sentences in caps (OUT OF FUEL: A BUDDY'S SPARE CAN ...), and key hints.
3. Text is the only visual language: no icons, so every state costs width.
4. Information that rarely changes (tire type, mod summary, weight split) is always on, while the most-read value (speed) is the smallest big number.
5. `u` follows height only: a 4:3 or a phone in landscape gets the same pixel size per element as 16:9 of the same height, but far less width.

## Design directions
All three keep the compass on top and the dial as the bottom-right anchor.

### A. Tidy: same look, one grid
Smallest change. A 12-column safe-area grid; every element lives in a named slot.
```
+--------------------------------------------------+
| [job/winch stack]      [ COMPASS ]      [players]|
|                       toast stack                 |
|                                                   |
|                                       [anchors]  |
|                                       [fuel]     |
| [vehicle card]         [4x4]   [tag] ( DIAL )   |
+--------------------------------------------------+
```
Changes: all HUD functions take rects from `hudlayout`; one text style for labels and one for values; the bottom-centre prompts (station, carry, winch aim) share one slot. Low risk, Part 1 already did a third of it.

### B. Instrument cluster: group by meaning
Merge the three bottom-right readouts into one cluster around the dial.
```
+--------------------------------------------------+
| [mission stack]        [ COMPASS ]       [status]|
|                                                   |
| [warnings]                                        |
| [vehicle card:                          fuel arc |
|  name, damage bar]           [4x4]    ( DIAL )   |
|                              terrain strip       |
+--------------------------------------------------+
```
- Fuel becomes a short arc on the dial's left rim, opposite the tach; low fuel turns it red.
- The terrain tag becomes a strip under the dial with a coloured dust swatch and short words (GRASS, MUD TIRES); SUNK and PACKED become small badges.
- Anchors move to the inventory menu, with a small count badge near the winch panel only while a winch is out.
- Vehicle card keeps name and damage; mass, split and mod summary move to the pause menu / F1 car sheet.
Medium risk; changes the look of the lower right the most.

### C. Minimal: show only what matters now
Contextual HUD: the dial and compass always; everything else fades in on change and out after a few seconds (fuel under 25%, terrain on change, schematic when a diff, mode or air pressure changes, anchors when near an anchor). Toasts and prompts use one centre-bottom slot. Cleanest screenshot, but information is hidden until it changes, so it needs a "show all" key and testing that low fuel never goes unnoticed.

Recommendation: A first (it is the grid every other option needs), then B's cluster for the lower right.

## Adjusting the speedometer
Keep the dial; options, which can be combined:
- **Size.** 184u across now. Grow to about 210u and move the speed digits up to 4.5x, since speed is read far more than rpm; or keep 184u and shrink the tach labels to every other tick below 720p.
- **What lives inside.** Current: tach ticks, speed, unit, gear, mode, lights. Proposed: speed (big), unit, gear in the bottom opening, drive mode (2H/4H/4L/4A) as one coloured chip beside the gear, TC/MAN/locks as 3 small icons on the rim instead of a text row.
- **Units.** KM/H or MPH is fixed in settings; show it only as a small unit label, and let a tap on the dial (touch) or a key flip it. Speed numbers themselves are owned by the speedo agent; the layout must not assume 3 digits only (999 fits).
- **Tach arc.** Redline zone is thin (5u); widen it, add a shift light (rim flash) near redline for manual-shift trucks, and drop the numeric tick labels on small sizes (below about 150 px diameter) keeping only ticks.
- **Legibility at small sizes.** The bitmap font only scales in whole numbers, so a 0.75u phone UI snaps to 1x and crushes. Use a floor of 7 px per glyph row (1x) only for secondary text; primary text (speed, gear) gets its own minimum of 3x.

## One UI scale (resolution independence)
- Everything is `u * constant` already; Part 1 keeps that and gathers constants in `hudlayout.h`. Proposal: split `u` into `u = ui_scale = f(height, dpi, user setting)`, computed in one function. Nothing should call `H / 720` directly (a dozen places in `main.cpp`, `coop.cpp`, `menu.cpp` still do).
- HiDPI (Mac Retina): the framebuffer is 2x the logical size. If the scale moves to logical points times a pixel ratio, `hudlayout` needs no change: rects are in framebuffer pixels from `u`. A Mac change to how `u` is derived drops in without touching layout code.
- Aspect: height-based `u` is right for wide screens; for 4:3 and portrait, also cap by width: `u = min(H/720, W/1100)` so the column and the dial never exceed a fixed share of the width. At 1024x768 today the HUD fits (the test proves it); below about 900 px wide it will not.
- Layout test: extend `tests/hudlayout.cpp` for every new element; it already iterates the sizes.

## Phone and touch
- Safe areas: notches and rounded corners need an inset (`safe = {l,t,r,b}` from SDL's safe-area query) added to every margin; today margins are 26u and 12u from the raw edge.
- Touch controls (steering, pedals) take the bottom corners, which the dial and the vehicle card occupy now. On a phone the dial moves to bottom centre-right above the pedals, or shrinks into a corner of the speed readout; the column above it goes away (option C's contextual fading suits phones best).
- Minimum hit target about 44 px (9 mm): HUD buttons such as pause, camera, map and horn need an invisible hit rect of at least that, while the glyph stays small.
- Phone landscape is about 844x390, which gives `u = 0.75` (the floor): the HUD would be 40% too big for the height. Lower the floor to about 0.5 for touch devices and use the width cap above.
- No hover states, no 1 px lines; the 5x7 font is at 1x below 0.75u, which is about 5 mm tall on a phone, so use a bigger font floor for the speed and gear and let secondary text drop.
- Keep the key-hint strings out of the touch HUD (they would show keys that do not exist); pad.ui.last already has a switch for pads, add a touch mode to it.
