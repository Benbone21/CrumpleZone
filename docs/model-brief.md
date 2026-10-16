# Model brief: authoring Crumple Zone's code-made models

Crumple Zone draws everything from code: no model files.

**Where models live.** Models are in `src/models/*.cpp`. They are built with the model kit (`src/modelkit.h`), so read that header first; it is short. `src/models/gear_fuel.cpp` (the jerry can) is the worked example to copy.

**How variants get picked.** Each model has 2–3 **design variants** with a few tweakable **params**. The owner looks them over in the model lab (`crumplezone --lab`). For each model they pick one, tweak its params, or flag it "redo" with a note. The game then uses the picked variant.

## Conventions

- **Units and frame.**
  - Metres.
  - Origin on the ground under the middle of the footprint.
  - **+X is the front** (the face you'd walk up to: a door, a sign's face, the business end), **+Y up**, **+Z left**.
  - Model at real-world size.
- **Colours.**
  - sRGB 0..1, in the game's muted, slightly weathered palette.
  - Nothing neon except hi-vis and safety items.
  - Use `paint(i)` / `paintParam()` for painted parts.
  - Vary tones (darker undersides, worn edges as a slightly lighter strip, rust spots as small dark-orange boxes) rather than one flat colour.
- **Materials.**
  - `PAINT`: painted metal (clear-coat shine).
  - `METAL`: bare or galvanised steel.
  - `CHROME`.
  - `WOOD`: shader grain.
  - `ROCK`.
  - `CONCRETE`: shows form lines.
  - `MATTE`: plastic, rubber, cloth, paper.
  - `FOLIAGE`.
  - `GLASS`.
  - `EMISSIVE`: lamps and reflectors only; glows at night.
- **Shape quality: avoid the "stack of boxes" look.**
  - Use `box(..., bevel)` with small bevels on almost everything man-made (1–3 cm on small things, 3–8 cm on big things).
  - Use `lathe` for anything turned (drums, bottles, bolts, posts, tanks, nozzles, cones).
  - Use `extrude` / `extrudeUp` for cut shapes (brackets, boards, sign plates, profiles, gussets).
  - Use `tube` along a path for ropes, handles, pipes, rails and bent tubing.
  - Use `torus` for rings, coils and tyres.
  - Use `blob` for organic things.
  - Use `letters` for stencils, sign text and labels; it is cheap and very effective.
  - Add the small details that make a thing read at a glance: bolts, handles, hinges, welds, straps, labels, edge trim.
- **Two-sided.**
  - The game doesn't cull back faces, so open shapes are fine.
  - Normals must face outward (the kit's primitives handle this).
  - For a single flat sheet use a thin `box`, not a bare quad: the back of a quad lights wrong.
- **Triangle budgets** (per model as built; the sheet prints the count). Things placed in the hundreds or thousands must stay cheap:
  - trees: 250–500;
  - bushes, stumps, markers: under 150;
  - signs, cairns: under 400;
  - gear and parts: under 2,500;
  - small buildings: under 6,000;
  - big structures: under 12,000.
- **Params.**
  - 2–5 per variant, each meaningful to someone looking at it: paint or colour choices, size or proportion, wear, optional bits on or off, count of something.
  - Each needs a sensible default, so the default build looks best.
  - Things that must vary per placed copy (listed in each spec as **instance params**) must use exactly the key given and work in **every** variant.
  - Natural things get `seedParam()`, so "reroll" gives a new one.
- **Variants** must be genuinely different design directions, not colour swaps. Examples:
  - steel vs. timber vs. quonset shed;
  - NATO can vs. flat pack vs. plastic can.
  - Give each a short CAPS name (no `:` or `,`) and a one-line blurb.
- **Registration.**
  - One `static Reg name({id, TITLE, CATEGORY, use, size, {variants}})` per model, in an anonymous namespace.
  - `id` is exactly as given below.
  - `size` is roughly the model's largest dimension.

## Checking your work (do this every iteration)

Build a contact-sheet tool for just your file. It is fast, and it doesn't touch the shared build:

```
cd /home/bread/Documents/GitHub/MemeNG-blazer
nice -n 19 g++ -O1 -std=c++17 -Wall -Wextra -o build/sheet_NAME tools/modelsheet.cpp src/modelkit.cpp src/platform.cpp src/models/YOURFILE.cpp -lm
nice -n 19 ./build/sheet_NAME --out build/review/models          # every model in your file
nice -n 19 ./build/sheet_NAME --out build/review/models trail_sign  # just one (substring)
```

Each model gives `build/review/models/<id>.png`. It shows one column per variant, front-left and back-right views, the size in metres, the triangle count, a ground grid (10 cm squares for small things, 1 m for big ones) and a grey 1.8 m person beside anything over 1.2 m.

**Look at every PNG with the Read tool.**
- Iterate until each variant reads instantly as what it is, sits on the ground (no floating, nothing sunk unless it's meant to be), is the right size, and has no gaps, inside-out bits or z-fighting (coplanar faces of different colours flicker in game: offset by 2+ mm).
- Expect several passes per model. Judge like an art director: proportions first, then silhouette, then detail.

## Rules

- **Files you may touch.** Only edit or create the `src/models/` files you're assigned.
- **Kit bugs or missing features.** Don't edit the kit. Work around it, and mention it in your report.
- **Shared resources.**
  - Don't run `make`.
  - Don't touch `build/` except your own `build/sheet_NAME` and the PNGs.
  - Don't run the game.
  - Don't use git.
- **Resources.** Use `nice -n 19` on everything, one compile at a time, and no downloads.
- **Report back.**
  - Each id with its variants and params.
  - Triangle counts.
  - Anything you couldn't get right.
  - Any kit problems.
