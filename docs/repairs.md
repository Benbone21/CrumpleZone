# Repairs

Every repair the game offers or models, what it needs, and where you get it in a career. In sandbox you have everything.

The field repairs are in `src/fixes.cpp` (`fixesFor`, `repairSteps`). The tools and parts are `Tool` and `RepairPart` in `src/fixes.h`. The test `repair/career-gear` (`tests/repairgear.cpp`) checks that each repair below can be done in a career with gear you can get.

## Where gear comes from (career)

- **Starting toolbox** (new career, `kStarterTools` in `src/career.h`): LUG WRENCH, JACK, WRENCHES, SOCKET SET, HAMMER, TIRE IRONS, and 1 spare TIRE. Before this change you started with only a lug wrench and a jack.
- **Station rack** (every repair station): every tool and every spare part, for cash.
- **Supply caches**: the first five caches on each map hold one tool each (SOCKET SET, HAMMER, WRENCHES, PRY BAR, TIRE IRONS), and every cache holds a spare part or two. The JUMP PACK is in cache 5 and the JUMPER CABLES in cache 8. A SPRING is in caches 2 and 6.
- **The truck's back** (trunk/inventory): what you own rides here. Open it with E at the back.
- **Paid repair** (R, `src/repairdeal.h`): fixes everything for cash, on the spot or as a tow to the nearest station. It is unchanged.

## Field repairs

| Repair (on screen) | Damage | Tools | Part | Jack | Before | Now |
|---|---|---|---|---|---|---|
| CV AXLE | a snapped half shaft | SOCKET SET, HAMMER, LUG WRENCH | CV AXLE | yes, wheel off | tools from caches or rack only | toolbox has them; part from caches or rack |
| DRIVE SHAFT | a snapped prop shaft | SOCKET SET | DRIVE SHAFT | no | socket set from cache or rack only | toolbox; part from caches or rack |
| TIE ROD | bent tie rod or toe link, or steering knocked out | WRENCHES, HAMMER | TIE ROD | no | as now | toolbox; part from caches or rack |
| TIE ROD (broken) | a snapped tie rod | WRENCHES, HAMMER | TIE ROD | no | **not offered**: no field repair | offered, same steps as a bent one |
| CONTROL ARM | a bent control arm or trailing arm | SOCKET SET, PRY BAR, LUG WRENCH | CONTROL ARM | yes, wheel off | as now | pry bar from cache or rack |
| CONTROL ARM (broken) | a snapped control arm, trailing arm or track bar | SOCKET SET, PRY BAR, LUG WRENCH | CONTROL ARM | yes, wheel off | **not offered** | offered |
| KNUCKLE AND ARM | a bent front knuckle (hub carrier) knocked out of shape; shown as REPLACE THE BENT KNUCKLE AND ARM, its marker on the knuckle | SOCKET SET, PRY BAR, LUG WRENCH | CONTROL ARM (covers the knuckle) | yes, wheel off | **not offered**: a knuckle couldn't bend | offered: a new knuckle and the lower arm it hangs on |
| SWAY BAR LINK | a bent sway bar | WRENCHES, PRY BAR | CONTROL ARM | no | as now | pry bar from cache or rack |
| SPRING | a broken coilover, or a broken leaf spring pack | SOCKET SET, WRENCHES | SPRING (new) | yes | **not offered**: no part or repair existed | new SPRING part on the rack and in two caches |
| NEW TIRE | a flat or shredded tire | TIRE IRONS, LUG WRENCH | TIRE | yes, wheel off (on trucks that lose wheels) | tire irons from cache or rack only; no spare tire part | toolbox has tire irons and one TIRE |
| FLOODED ENGINE | water in the engine (hydrolocked) | SOCKET SET, WRENCHES | SPARK PLUGS, ENGINE OIL | no (hood up) | tools from caches or rack | toolbox; parts from caches or rack |

## Other repairs and jobs

| Job | Damage | Needs | Where |
|---|---|---|---|
| Change a wheel | any wheel | LUG WRENCH, JACK | starting toolbox |
| Fit the spare | a wheel lost or flat | the spare on its carrier, LUG WRENCH, JACK | on the truck |
| Mount tire to rim | tire pushed off its bead (debeaded) | TIRE IRONS | starting toolbox (was: caches or rack) |
| Pick up a wheel | a wheel torn off, lying about | hands | - |
| Air up / let air out | tire pressure | onboard compressor or station air | on the truck |
| Jump start | dead battery | JUMP PACK, or JUMPER CABLES and another vehicle | caches 5 and 8, rack |
| Snow chains | - | chains in the back | on trucks that carry them |
| Winch, tow strap | stuck | the winch line or strap, an anchor (tree strap, chain sling, ground stake...) | anchor gear in caches and on the rack |

## Damage with no field repair

These are fixed by the paid repair (R) or at a station. They are kept that way on purpose:

- **Crushed engine** (engine health lost in a crash, or a dead engine from one). This is the body crushed into the engine bay, which you can't fix with hand tools.
- **Body damage** (dents, crushed panels, bent frame). This is the soft body itself.

## Multiplayer

The new repairs use the same sync as the old ones (`src/repairsync.cpp`). The SPRING job is `Fix::SPRING` (number 7, added last, so older numbers don't change). The "fixed" message carries its beam. A peer on an older build ignores a job number it doesn't know.

## Old saves

Spare parts are saved by name (`stock N REPAIR:SPRING`), so old saves load unchanged. They keep the tools they had. The starting toolbox only applies to a new career. The test `repair/old-save` checks this.
