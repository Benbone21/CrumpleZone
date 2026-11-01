# Garage shop

The owner asked: "for the garage, I want to enter an interactive menu where you can buy your parts and
upgrades and see the models." This replaces the flat text garage (`drawGarage` in main.cpp) with a
garage you stand in.

## What it is

- **Entering.** G while driving, or GARAGE in the pause menu. In a career only at a repair station (on
  its pad); elsewhere a toast says to drive to one. In sandbox anywhere. The game camera blends from the
  chase view to the garage view of your truck where it stands, and the truck is parked (as before).
- **Data.** Everything comes from career.h and mods.h: no list of parts or prices of its own.
  - One category per mod slot (`MOD_SLOTS`: ENGINE, SUSPENSION AND LIFT, TRANSMISSION, WHEELS, TIRES),
    the slot's options from `modOptions`.
  - One category per kind of station-rack item (`stationRack()`: the key's prefix TOOL, REPAIR,
    ANCHOR): TOOLS, REPAIR PARTS, ANCHOR GEAR.
  - Owned / locked / price / tier: `Career::owns`, `Career::price`, `Career::tier`. Buying:
    `Career::buy`, `Career::buyGear`. Where a locked part is found: `partSources(key)` (new, read-only, in
    career.cpp next to the cache and gold-medal tables it reads).
  - Stats: from `tunedSpec` of what is fitted against `tunedSpec` with this part in its slot.
  - Categories that the game has no data for yet (armor and bumpers, winch, lights) appear when a mod
    slot or rack prefix for them exists: `shop::categories()` is a table keyed by slot and prefix.
- **Models.** Each item shows a code-made model on a turntable (an oil drum for a plinth for small
  things, the floor for big ones) beside the truck: the crate engine that matches the engine's
  cylinders, coilover or leaf pack, gearbox / transfer case / differential, a tire of the part's size,
  the rack item's own model.
- **Preview on the truck.** T (pad X) tries the focused part on the truck: the truck is rebuilt on the
  parts it would have (`tunedSpec`), the camera turns to it, and moving through the list keeps trying
  each part on. Leaving the list or closing puts the fitted parts back. (In a career you are at a
  station, where a parked truck is repaired for free anyway, so the rebuild gives nothing away.)
- **Buy / fit / remove.** ENTER (pad A) on a part: fit it if you own it; buy it and fit it if a station
  sells it and you have the cash. R (pad Y) puts the factory part back in that slot. Rack items: ENTER
  buys one. Sandbox: everything is owned and free; cash isn't shown.

## Layout

```
+-----------------------------------------------------------------------------------------+
| GARAGE  FORD BRONCO                                        CASH $4,200   AT PINE STATION |
| ---------------------------------                                                       |
|  ENGINE            >| 2.7 STAGE 1 TUNE      OWNED   |                                   |
|  SUSPENSION + LIFT  | RAPTOR 3.0 V6 SWAP    $2500   |       (the world: your truck,     |
|  TRANSMISSION       | BLAZER: 454 BIG BLOCK LOCKED  |        the part turning on its    |
|  WHEELS             | ...                           |        plinth beside it)          |
|  TIRES              |                               |                                   |
|  TOOLS              |                               |   +-----------------------------+ |
|  REPAIR PARTS       |                               |   | RAPTOR 3.0 V6 SWAP          | |
|  ANCHOR GEAR        |                               |   | 3.0L TWIN-TURBO V6 ...      | |
|                     |                               |   | POWER   330 -> 418 HP   +88 | |
|                     |                               |   | WEIGHT  ...                 | |
|                     |                               |   | $2500   ENTER BUY AND FIT  | |
|                     |                               |   +-----------------------------+ |
| W S MOVE  D OPEN  ENTER BUY/FIT  T TRY ON TRUCK  R FACTORY PART  ESC BACK                |
+-----------------------------------------------------------------------------------------+
```

The left side is shaded like the main menu (mainmenudraw.cpp: the same amber, rows with a bar on the
focused one, the hint line at the bottom). The right is the live world with the garage camera:

- category list focused: the whole truck, three-quarter front, drifting slowly;
- a part focused: the turntable, the truck behind it;
- trying a part on: the truck again, from the side that shows that part (the nose for an engine, a
  front wheel for suspension, wheels and tires).

## Controls

| | Keyboard | Mouse | Pad |
| --- | --- | --- | --- |
| move | W S / arrows | hover | D-pad / left stick |
| into a category / back | D, ENTER / A, ESC | click | D-pad right, A / B |
| previous / next category | Q E | | LB RB |
| buy and fit / fit / buy one | ENTER | click the focused row | A |
| try on the truck | T | | X |
| factory part back | R | | Y |
| speedo recalibration on fit | C | | |
| turn / tilt the model | | drag on the right | right stick |
| zoom | Z X | wheel on the right | (triggers not used) |
| close | ESC from the categories, G | | B, Start |

## Code

- `src/garageshop.h/.cpp` (no GL, no SDL, linked into the tests): categories, items, stats, the display
  model for an item, the navigation state machine (`shop::Shop`), the garage camera, and `liftSync`
  (puts on the car what the shop shows).
- `src/garageshopdraw.h/.cpp`: paints it (hudkit, the main menu's look).
- main.cpp: opens it, feeds it keys and the mouse, places the plinth model in `frameModels`, takes
  its camera, saves the garage and career after a buy or fit.
- tests/garageshop.cpp: navigation, buy / fit / cancel leaves the car right, sandbox vs career.
