# Parts list 2: fewer parts, plain names

The garage used to offer every vehicle's parts on every other vehicle. Each vehicle had 31 engines, 32 suspensions, 32 transmissions, 9 wheels and 6 tires to scroll through, and many names were part codes ("B18C TYPE R SWAP", "ZZ4 CRATE 350", "5VZ-FE"). Parts list 2 cuts each slot down to the factory part plus two or three upgrades. Each upgrade is clearly different from the others (better and best, or off-road and street) and has a plain name.

- **Engine, suspension, transmission:** each vehicle has only its own parts now. Swaps from other vehicles are gone.
- **Wheels:** three sizes, the same on every vehicle. Each size uses one vehicle's wheels and tires:
  - STREET WHEELS: the Jesko's, low tires on 20 in rims.
  - BIG WHEELS: the 4Runner's, 35 in tires.
  - HUGE WHEELS: the buggy's, 40 in tires.
- **Tires:** four treads: STREET, ALL-TERRAIN, MUD and RACE. A vehicle never sees its own factory tread twice. The Jesko's semi-slicks count as its street tire.
- **Internal names stay the same.** The specs still use their real names (`engineName`, `gearboxName`), and the engine sound, the engine models and the F3 screen read those names. Part keys keep their old numbers: the Blazer's BIG V8 is still `ENGINE:blazer:2`. So the keys for the parts that stayed did not change.
- **Physics:** stock vehicles are untouched (`bench_sim 12 separated 4000` still prints `adf7525f83403395`). Every part that stayed has the same effect as before. Only its name and description changed.

## Counts, before and after

| Slot | Before (each vehicle) | After |
|---|---|---|
| Engine | 31 | 3 |
| Suspension | 32 | 3, or 4 on the Jesko, Bronco and Tacoma |
| Transmission | 32 | 3 |
| Wheels | 9 | 4, or 3 on the Jesko, 4Runner and buggy (those vehicles' own wheels are one of the sizes) |
| Tires | 6 | 4 |

## Vehicle by vehicle

"Old" lists each vehicle's own parts in their old order (the number in the part key). "Went to" says where a cut part maps. Old saves and careers that name a cut part get the part in "Went to". An upgrade always maps to an upgrade, so nothing a player bought turns back into the factory part. The only exception is a part that was a downgrade (less power than stock): it maps to stock.

### Civic (`civic`)
| Slot | Old | New name | Went to |
|---|---|---|---|
| Engine | 0 STOCK (1.6 D16A6) | STOCK | |
| | 1 D16 CAM AND HEADER | (cut) | SPORT ENGINE |
| | 2 B16A VTEC SWAP | SPORT ENGINE | |
| | 3 B18C TYPE R SWAP | RACE ENGINE | |
| Suspension | 0 STOCK | STOCK | |
| | 1 LOWERING SPRINGS | (cut) | LOWERED |
| | 2 COILOVERS | LOWERED | |
| | 3 RALLY KIT | RALLY KIT | |
| Transmission | 0 STOCK | STOCK | |
| | 1 SHORT FINAL DRIVE | (cut) | SPORT GEARBOX |
| | 2 CLOSE RATIO AND LSD | SPORT GEARBOX | |
| | 3 SEQUENTIAL RACE BOX | RACE GEARBOX | |

### Jesko (`jesko`)
| Slot | Old | New name | Went to |
|---|---|---|---|
| Engine | 0 STOCK | STOCK | |
| | 1 E85 FLEX FUEL | MORE BOOST | |
| | 2 RACE FUEL AND BOOST | RACE TUNE | |
| Suspension | 0 STOCK | STOCK | |
| | 1 TRACK SETUP | TRACK SUSPENSION | |
| | 2 COMFORT AND LIFT | LIFT KIT | |
| | 3 ABSOLUT LOW-DRAG BODY | NO WING | |
| Transmission | 0 STOCK | STOCK | |
| | 1 SHORT FINAL DRIVE | SHORT GEARS | |
| | 2 TOP SPEED GEARING | TALL GEARS | |
| | 3 TRACK DIFF MAP | (cut) | SHORT GEARS |

### Bronco (`bronco`)
| Slot | Old | New name | Went to |
|---|---|---|---|
| Engine | 0 STOCK (2.7 twin-turbo V6) | STOCK | |
| | 1 2.3 ECOBOOST I4 (less power) | (cut) | STOCK |
| | 2 2.7 STAGE 1 TUNE | TUNED V6 | |
| | 3 RAPTOR 3.0 V6 SWAP | BIG V6 | |
| Suspension | 0 STOCK | STOCK | |
| | 1 SASQUATCH PACKAGE | LIFT KIT | |
| | 2 3 IN LIFT AND 37S | BIG LIFT | |
| | 3 STREET LOWERED | LOWERED | |
| Transmission | 0 STOCK | STOCK | |
| | 1 4.70 GEARS | (cut) | LOCKERS |
| | 2 4.70 AND LOCKERS | LOCKERS | |
| | 3 7-SPEED MANUAL | MANUAL GEARBOX | |

### 4Runner (`4runner`)
| Slot | Old | New name | Went to |
|---|---|---|---|
| Engine | 0 AS BUILT (supercharged 3.4) | AS BUILT | |
| | 1 FACTORY 3.4 V6 (less power) | (cut) | AS BUILT |
| | 2 4.0 V6 1GR SWAP | BIG V6 | |
| | 3 1GR SUPERCHARGED | BIG V6 SUPERCHARGED | |
| Suspension | 0 AS BUILT | AS BUILT | |
| | 1 FACTORY HEIGHT | (cut) | LOWERED |
| | 2 LONG TRAVEL AND 37S | BIG LIFT | |
| | 3 STREET SPORT | LOWERED | |
| Transmission | 0 AS BUILT | AS BUILT | |
| | 1 4.88 REGEAR | (cut) | LOCKERS |
| | 2 4.88 AND LOCKERS | LOCKERS | |
| | 3 CRAWL BOX | CRAWL GEARS | |

### Rock buggy (`buggy`)
| Slot | Old | New name | Went to |
|---|---|---|---|
| Engine | 0 STOCK (6.2 LS) | STOCK | |
| | 1 7.0 LS7 | BIG V8 | |
| | 2 BLOWN 6.2 | SUPERCHARGED V8 | |
| Suspension | 0 STOCK | STOCK | |
| | 1 42 IN TIRES | HUGE TIRES | |
| | 2 DESERT SETUP | DESERT KIT | |
| Transmission | 0 STOCK | STOCK | |
| | 1 TH400 3-SPEED | 3-SPEED AUTO | |
| | 2 ATLAS 5.0 LOW | CRAWL GEARS | |

### Side-by-side (`utv`)
| Slot | Old | New name | Went to |
|---|---|---|---|
| Engine | 0 STOCK | STOCK | |
| | 1 STAGE 2 TUNE | RACE TUNE | |
| | 2 ECO TUNE | TRAIL TUNE | |
| Suspension | 0 STOCK | STOCK | |
| | 1 RACE SHOCKS | RACE SHOCKS | |
| | 2 SOFT TRAIL SETUP | SOFT RIDE | |
| Transmission | 0 STOCK | STOCK | |
| | 1 SHORT GEARING | SHORT GEARS | |
| | 2 REAR LOCKER | REAR LOCKER | |

### Tacoma (`tacoma`)
| Slot | Old | New name | Went to |
|---|---|---|---|
| Engine | 0 STOCK (3.5 2GR-FKS) | STOCK | |
| | 1 INTAKE AND TUNE | TUNED V6 | |
| | 2 MAGNUSON SUPERCHARGER | SUPERCHARGED V6 | |
| Suspension | 0 STOCK | STOCK | |
| | 1 3 IN LIFT AND 33S | LIFT KIT | |
| | 2 LONG TRAVEL AND 35S | BIG LIFT | |
| | 3 STREET LOWERED | LOWERED | |
| Transmission | 0 STOCK | STOCK | |
| | 1 4.88 REGEAR | (cut) | LOCKERS |
| | 2 4.88 AND FRONT LOCKER | LOCKERS | |
| | 3 CRAWL BOX | CRAWL GEARS | |

### Blazer (`blazer`)
| Slot | Old | New name | Went to |
|---|---|---|---|
| Engine | 0 STOCK (350 2-barrel) | STOCK | |
| | 1 350 4-BARREL | (cut) | BIG V8 |
| | 2 454 BIG BLOCK | BIG V8 | |
| | 3 ZZ4 CRATE 350 | HOT ROD V8 | |
| Suspension | 0 STOCK | STOCK | |
| | 1 4 IN LIFT AND 35S | LIFT KIT | |
| | 2 SOFT TRAIL LEAVES | SOFT SPRINGS | |
| Transmission | 0 STOCK | STOCK | |
| | 1 TH350 AUTOMATIC | AUTOMATIC | |
| | 2 4.56 GEARS AND LOCKERS | LOCKERS | |

### Baja Bug (`baja`)
| Slot | Old | New name | Went to |
|---|---|---|---|
| Engine | 0 STOCK (2.0 dual carbs) | STOCK | |
| | 1 1835 STREET MOTOR | QUIET ENGINE | |
| | 2 2332 STROKER | BIG ENGINE | |
| Suspension | 0 STOCK | STOCK | |
| | 1 DESERT RACE AND 33S | LIFT KIT | |
| | 2 STREET BUG | LOWERED | |
| Transmission | 0 STOCK | STOCK | |
| | 1 WEDDLE LSD | LIMITED SLIP | |
| | 2 SAND BOX | SAND GEARS | |

## The parts every vehicle shared

| Slot | Old | New | Went to |
|---|---|---|---|
| Engine | each other vehicle's engines (27 or 28 per vehicle: "JESKO: 5.0L TWIN-TURBO V8", "SWAP: 7.0 LS7"...) | (cut) | this vehicle's own engine with the nearest peak power (the Jesko's V8 in the Blazer goes to its HOT ROD V8; the Civic's 1.6 goes to its STOCK) |
| Suspension | each other vehicle's suspension and its kits (28 or 29) | (cut) | this vehicle's STOCK suspension |
| Transmission | each other vehicle's gearbox and its kits (28 or 29) | (cut) | this vehicle's STOCK transmission |
| Wheels | the other 8 vehicles' WHEELS AND TIRES | STREET WHEELS (Jesko's), BIG WHEELS (4Runner's), HUGE WHEELS (buggy's) | the wheel size nearest that vehicle's wheels, stock included (a tie keeps the stock wheels) |
| Tires | STREET TIRES | STREET TIRES | |
| | SEMI-SLICKS | (cut) | STREET TIRES |
| | RACE SLICKS | RACE TIRES | |
| | ALL-TERRAIN TIRES | ALL-TERRAIN TIRES | |
| | MUD-TERRAIN TIRES | MUD TIRES | |
| | SNOW TIRES | (cut) | ALL-TERRAIN TIRES |

A cut tread maps to the factory tread when that is the one it went to. For example, snow tires on an all-terrain truck go back to STOCK.

## Career

- **Tiers** are set per part in `src/mods.cpp`:
  - Tier 1: everyday parts, sold cheaply. Examples: the first upgrade in most slots, STREET, BIG WHEELS, ALL-TERRAIN.
  - Tier 2: serious parts, sold at 2.5 times the price. Examples: the best upgrade, MUD, HUGE WHEELS.
  - Tier 3: not sold. The Jesko's and the buggy's engine upgrades, the Bronco's BIG V6 (481 hp in the game, over the old 480 hp line) and RACE TIRES.
- **Prices** are the same per slot as before: $2,500 engine, $1,500 suspension, $1,200 transmission, $800 wheels, $500 tires, times 2.5 for tier 2.
- **Caches** now hold the new names of the same parts, with one change: the Frontier's FROZEN TARN held snow tires and now holds ALL-TERRAIN TIRES.
  - THE WALL TOP and YARD CRATE B: Blazer BIG V8.
  - OLD MINE: Blazer HOT ROD V8.
  - CLIFF OVERLOOK: buggy BIG V8.
  - HEARTBREAK HILL: Blazer LOCKERS.
  - THE WALLOW: Bronco BIG LIFT.
  - WRECK SITE: 4Runner BIG LIFT.
  - MESA STAIRS: 4Runner LOCKERS.
  - ABOVE THE LODGE: Tacoma SUPERCHARGED V6.
  - DEEP CROSSING: Tacoma CRAWL GEARS.
- **First-gold rewards:** Jesko RACE TUNE, buggy SUPERCHARGED V8, Civic RACE ENGINE, Jesko TRACK SUSPENSION (it replaces the cut TRACK DIFF MAP), Bronco LIFT KIT and side-by-side RACE TUNE.

## Old saves

Saves name each fitted part by its place in its slot's list, and those places changed. So every save now says which list it uses:
- `garage.txt` has a `parts 2` line.
- The career profile (and the old `career.txt`) has a `parts 2` line.
- A car's condition text (in the profile and in career worlds) has a `parts=2` field.

Older builds skip the new line and field.

A save without the mark is from before parts list 2. It loads like this (`modLegacyMods` in `src/mods.h`):
- Each old pick is turned back into the part it named. `modLegacyKey` rebuilds the old list order: the vehicle's own parts, then each other vehicle's in catalog order, then the treads.
- That part then maps to its nearest one today, as in the tables above.
- A career's owned parts go through `modMigrateKey`, so a cut part becomes its nearest one. In an old profile, whatever the nearest part is on each vehicle also becomes owned, so a truck keeps what it had, near enough.
- Multiplayer sends the picks (`SpecId`) as before, but they now count in the new lists, so the protocol is version 11 (`net::kVersion`, first release 0.4.14).

The tests are `career/parts-migrate` (old garage, old career, old picks one by one) and `career/parts-simple` (3 or 4 plainly named options in every slot).

## Tests changed

- `career/ownership`, `career/shop`: the 454 shared between the Blazer and the Civic, the Jesko's V8 on the Civic and snow tires are gone. The tests now use the Blazer's own BIG V8, the HUGE WHEELS shared by every vehicle, the Jesko's RACE TUNE and ALL-TERRAIN TIRES.
- `paint/garage-file`: the old garage file's Bronco engine is the old stage 1 tune, read back as TUNED V6.
- `shop/stats`: the Bronco's BIG V6 is option 2 now.
- `engine samples` (in `audio`): the Civic's SPORT ENGINE and the Bronco's BIG V6 are found at their new places.
- `drivetrain/engine-looks`, `drivetrain/gearbox-looks`: options are looked up by name. The rows for the cut parts are gone: the Civic's cam and header, the Bronco's 2.3 four, the 4Runner's factory 3.4 and the Blazer's four-barrel. Their models are still in the game, but no garage part uses them now.
- `drivetrain/low-range-swap`: each low-range truck now uses its own strongest engine. It used to be the buggy's blown 6.2 swapped into every truck.
- `drivetrain/low-range-crawler`: the 4Runner now runs its BIG V6 SUPERCHARGED on CRAWL GEARS (64:1). It used to run the LS7 on the Blazer's gearbox (77:1). The check that a held brake snaps a shaft is gone, because that gearing no longer exists. `drivetrain/4runner-front-abuse` and `offroad/shaft-break` still test that a CV breaks.
- `drivetrain/low-range-buggy-engines`: now uses each of the 4Runner's own engines, because the buggy's engines no longer fit it.
- `drivetrain` (sandbox 4Runner), `frontalign`, `sections/cliff-shelf` drive, `dynamics/jesko-*`: the part names in these tests are the new ones. The Jesko's full track tune lost the cut TRACK DIFF MAP.
- `tires/treads`: winter tires are no longer sold, so the test fits the snow tread to the Bronco itself, the way the garage used to (the tread is still in the tire model).
- `garage/swaps/<vehicle>/<slot>`: now only for WHEELS and TIRES, the slots that still have shared parts. The engine, suspension and transmission versions of these tests are removed, because those swaps no longer exist.
- `mods/<vehicle>/<slot>/<part>`: one test per remaining part, named by its new name. The tests for the cut parts are gone: civic d16-cam-and-header, lowering-springs and short-final-drive; jesko track-diff-map; bronco 2.3-ecoboost-i4 and 4.70-gears; 4runner factory-3.4-v6, factory-height and 4.88-regear; tacoma 4.88-regear; blazer 350-4-barrel. Their `tests/baseline.txt` entries are no longer used, and neither are those of the renamed tests. The baseline was not updated.

## Attachments: winch and steel bumper

Two more garage slots, after the tires, fit every vehicle. Each is on or off, and the two are independent (`MOD_WINCH`, `MOD_BUMPER` in `src/mods.h`; the parts are in `src/attachments.cpp`).

- **WINCH** ("NO WINCH" / "WINCH"): a 12,000 lb winch (53.4 kN stall pull, 40 kg) with its drum of red synthetic line, motor, gearbox, tie bars, contactor box, hawse fairlead and hook. Without the bumper it sits on its own plate bolted ahead of the nose; with the bumper it sits in the bumper's cradle. The winch line leaves from its fairlead: the attachment moves the truck's front recovery point (`Car::hookRest[0]`) there, so hooking up on foot, payout and reel-in all work from it.
- **Vehicles with a factory winch** (Bronco, 4Runner, rock buggy, side-by-side) keep it as their factory part: option 0 is "WINCH" (as before, so a stock vehicle is unchanged), option 1 is "NO WINCH", which takes it off with its weight. Taking it off is free in a career. With the steel bumper fitted, the factory winch is drawn in the bumper's cradle (sized by its pull) and its line leaves from the bumper's fairlead.
- **STEEL BUMPER** ("STOCK BUMPER" / "STEEL BUMPER"): a plate-steel bumper with a tube grille guard, D-ring shackles and lights (75 kg). In the sim it is a row of heavy nodes on the bumper's face, one ahead of each node of the nose's bottom two rows. They are tied together and braced back into the nose's first two columns (and bolted to a ladder frame's horns) with boxed-steel members, and the bottom of the nose behind it is boxed in by its mounts. The face nodes collide, so the bumper takes the hit, and the nose behind it crumples less. The members' step margin is h·ω ≈ 0.3 (the explicit limit is 2), because the nodes carry the bumper's weight (~5 kg each).
- **The fit** comes from each vehicle's own nose: its length, the height of its bottom rows and its width there (`attachFit`). The physics and the drawing use the same fit, so the bumper's nodes, the fairlead and the drawn parts line up on every vehicle, the Jesko and the side-by-side included.
- **Nothing fitted is the factory vehicle itself**: `bench_sim 12 separated 4000` still prints `adf7525f83403395`.

Measured in `attachments/bumper-wall/*`: the same vehicle into the concrete wall at 50 km/h, with and without the bumper (the body's crush).

| Vehicle | Stock | Steel bumper |
|---|---|---|
| Civic | 25.0 cm | 18.4 cm |
| Jesko | 17.6 cm | 5.4 cm |
| Blazer | 14.7 cm | 3.8 cm |
| Bronco | 16.5 cm | 3.1 cm |
| Tacoma | 20.9 cm | 3.3 cm |
| 4Runner | 32.7 cm | 3.5 cm |
| Baja Bug | 18.9 cm | none (its rear engine takes more of a jolt on its mounts) |
| Side-by-side | 0.1 cm | 0.0 cm (a tube cage barely crushes either way) |

The body-on-frame trucks gain the most, because the bumper bolts to the frame horns and the frame carries the hit.

**Career:** both are tier 1 parts, sold at the stations: $1,800 for the winch, $1,400 for the bumper. Sandbox has everything.

**Saves:** the garage file, a career's vehicles and a car's condition text now hold seven picks. A save with five (from before the attachments) loads with the factory's winch and bumper. An older build reads the first five picks of a new garage line. Multiplayer sends all seven picks (`SpecId`), so other players see the attachments: protocol 12 (`net::kVersion`, first release 0.4.15).

**Screenshots:** `docs/review/attachments/`.

**Tests:** `attachments/every-vehicle`, `attachments/winch-kit`, `attachments/bumper-wall/<vehicle>`, `attachments/bumper-stable/<vehicle>`, `attachments/old-saves`, `attachments/net`. The per-part scenarios (`mods/<vehicle>/winch/...`, `mods/<vehicle>/bumper/...`) and the fitment sweeps (`fitment/<vehicle>/winch`, `.../bumper`) now cover them as well. `career/parts-simple` allows two options in the attachment slots.
