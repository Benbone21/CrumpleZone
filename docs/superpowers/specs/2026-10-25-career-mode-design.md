# Career mode: gameplay and progression (design)

Status: approved section by section with the owner, 2026-10-25. This document is the spec; the
implementation plan comes next.

## Summary

Career is an **overland exploration** game first and a **backcountry rescue business** second.
Neither is required: a player who only explores, or only takes rescue jobs, progresses either way.

What stops you going everywhere on day one is **your truck** (it can't yet cross deep rivers, climb
the ridges or carry enough fuel) **and your supplies** (fuel, spare parts and damage limit how far
from help you dare go). You push your range out by discovering **outposts** and by setting up
**your own camps**. When it goes wrong far out, you **pay for a tow** or **walk out**, and a friend
can always come and get you for free.

The game is open-ended, with a few big **landmark challenges** (the far summit, the lost mine, the
river's source) as major milestones.

Profiles travel with the player. Worlds hold their own state. A new multiplayer world can be a
**Shared crew** with one bank and garage.

## Decisions (owner's answers)

| Topic | Decision |
|---|---|
| Core loop | Exploration first, rescue second, no punishment for doing only one |
| Gating | Vehicle capability + supply/range (both) |
| Stranded | Choose: paid tow (by distance) or walk out; a friend rescues for free |
| Range | Discovered outposts + 1–2 movable camps of your own |
| Vehicles | Bought at dealers, or found abandoned and recovered |
| Start | Any stock vehicle except the Jesko |
| Multiplayer | Personal progress by default; "Shared crew" option when creating a multiplayer world |
| Exploration rewards | Caches, discovery pay + region completion, outposts, photo spots, map reveal |
| Jobs | Board at stations/outposts + urgent radio calls; types: recover, breakdown fix, delivery, pickup, scout |
| Running costs | Medium by default; difficulty (Light / Medium / Hardcore) picked at world creation |
| Long-term goal | Open-ended, plus landmark challenges |

## 1. Profiles, worlds and saves

**Profile** (one per player, `~/.config/crumplezone/profile.txt`, travels into any world):
- cash
- owned parts and gear (today's `Career::owned`, `stock`)
- owned vehicles: id, build (mods), condition (damage, fuel, battery), which world and where it was left
- discoveries, photos (album index), milestones, region progress

**World save** (one per world, on the host, `~/.config/crumplezone/worlds/<name>/world.txt`):
- name, map, difficulty, mode (Personal / Shared crew), creation build version
- vehicles left in the world: pose, damage, fuel, battery
- camps (owner, position, stocked fuel and parts)
- job board and active radio calls
- opened caches; abandoned-vehicle states (untouched, moved, recovered)
- Shared crew only: the crew bank, crew garage and crew discoveries, which replace each player's
  personal ones while in that world

**Menus:** main menu → CAREER → a list of worlds (Continue / New World / Delete). New World asks for
name, map, difficulty, mode and starting vehicle.

**Migration:** today's `career.txt` becomes the profile on first run; nothing is lost. Sandbox is
unchanged.

**Saving:** autosave every few minutes and on quit, with atomic writes (`writeFileAtomic`, as the
level editor does) and a `.bak` fallback.

## 2. Economy and difficulty

All numbers live in one table (`src/economy.*` or inside career) so they can be tuned in one place.
The values below are Medium.

**Income**

| Source | Pay |
|---|---|
| Rescue job | $400–$4,000 by distance, terrain rating and urgency |
| Radio call | +50% over the board rate; expires |
| Discovering a place | $150–$600 |
| Region complete | $2,500 |
| Photo spot | $300–$800 |
| Cache | a part, gear, or $200–$1,500 |
| Landmark challenge | a large payout + a unique part or cosmetic |

**Costs**
- **Fuel:** per litre at stations and outposts; range comes from each vehicle's real tank and use.
- **Repairs at a station:** parts + labour. On the trail you use carried parts (bought beforehand).
- **Tires:** wear with distance and abuse; damaged tires need replacing.
- **Tow:** base fee + per km to the nearest discovered station, outpost or own camp.
- **Walk out:** free; the truck stays where it was left.
- **A friend's tow or winch:** free.

**Difficulty**

| | Light | Medium | Hardcore |
|---|---|---|---|
| Starting cash | $2,000 | $1,000 | $500 |
| Costs | ×0.5 | ×1 | ×1.5 |
| Pay | ×1.25 | ×1 | ×0.9 |
| Own camps | 1 | 1 | 2 |
| Wear over time (bearings, ball joints, clutch, belts) | no | no | yes |
| Outpost stock | always | always | can run out; restocks over time |
| Totaled vehicles | no | no | yes: gone unless recovered and rebuilt |

Damage persists on every difficulty. Light makes it cheap to fix.

## 3. Outposts, camps and range

**Outposts** (placed by big maps phase 7):
- ranger station: full repair, shop, fuel, job board, garage and vehicle dealer
- fuel stop: fuel and a small parts shelf
- cabin or old camp: tow point and save point, some with a small repair kit

An outpost works only once discovered (you drive up to it). It then gets a map icon and becomes a
tow destination.

**Own camps:**
- Bought as a camp kit at a station. It takes cargo space and is hauled out in the bed or trunk.
- Placed on foot on fairly flat ground. It becomes a tow point and save point with a stock of fuel
  and parts.
- Stocked by unloading carried jerry cans and parts, or by paying a supply-run fee (by distance).
- Packed up on foot (about a minute) and moved.
- Slots: per the difficulty table, plus more unlocked by milestones.
- In multiplayer anyone in the session can use a camp, but only its owner packs it (crew camps in
  Shared crew mode).

**Range:** the HUD shows fuel range. The map shows reach rings from your fuel and your nearest
refuel points.

## 4. Rescue jobs

**Sources:**
- **Board:** at stations and outposts; 3–6 jobs, refreshed every in-game day, no expiry.
- **Radio calls:** occasional, while driving: "STRANDED HIKER NEAR BEAR FALLS, 2.1 KM". Accept with
  a key or ignore. They pay +50%, show a timer and expire. At most one active call at a time.

**Types** (built on existing systems):

| Type | Goal | Systems |
|---|---|---|
| Recover | Winch or tow a stuck or rolled vehicle to safe ground or an outpost | winch, recovery, towing |
| Breakdown | Fix a broken-down vehicle on the spot; parts cost reimbursed | field repair, engine work, jump pack |
| Delivery | Carry supplies to a remote cabin or camp | cargo, trunk/bed |
| Pickup | A stranded person walks to the truck, rides along, and is dropped at safety | walker, ride-along seats |
| Scout | Drive an unexplored route to its end, revealing it | map reveal, discovery |

**Pay** scales with distance from the nearest outpost, terrain rating and urgency, with a bonus for
an undamaged result. Jobs are generated from big-map data (stuck spots near crossings, rough trails,
remote cabins). A job is offered only if your current vehicle could plausibly reach it.

**Multiplayer:** anyone can take a job. Pay is split among helpers (within 200 m, or used their
winch or tools on it), or goes to the crew bank in Shared crew mode.

**Abandoning or expiry:** no penalty.

## 5. Exploration and discovery

- **Places** (lookouts, waterfalls, mine, cabins, crossings, summits, ruins, outposts):
  "DISCOVERED: <NAME>", cash, and a named map marker.
- **Map reveal:** the map starts dark and fills within about 150 m of where you've driven or walked
  (wider from high ground). Each region shows its % explored.
- **Region complete:** 90% explored + every place found gives "<REGION> COMPLETE", $2,500 and the
  region's name on the map.
- **Caches:** shown on the map only as a rough "?" area until found; opened once per profile (or per
  crew).
- **Photo spots:** marked scenic spots. Park your truck in frame and press the photo key; if your
  truck is in range and in view: "PHOTO: <NAME>", a payout, and the image saved to the album.
- **Milestones** (firsts, places reached, totals); some unlock camp slots or parts.
- **Landmark challenges:** Copper Ridge high summit, the lost mine (badlands, a dry-falls climb), the
  source of Elk River (a long canyon with deep fords), and later Frontier's equivalent. Each needs a
  strong build and a chain of camps, and pays a big reward with a unique part or cosmetic.
- **Career page** (pause menu): Overview, Regions, Milestones, Album, Vehicles.

## 6. Vehicles

- **Several owned vehicles,** each with its own build, condition and location. Drive any you can
  reach, or pay for delivery to a station (fee by distance).
- **Dealer** at ranger stations sells vehicles in stock form:

  | Class | Price |
  |---|---|
  | Civic, Baja Bug | ~$4,000 |
  | Blazer, Bronco, 4Runner, Tacoma | ~$9,000–$14,000 |
  | UTV, rock buggy | ~$18,000 |
  | Jesko | $250,000 |

  Selling back pays 50% of the stock price + 50% of the fitted parts.
- **Abandoned vehicles:** 3–5 per big map at fixed spots (the cabin garage, the mine pit, a river
  bend). Shown on the map only once seen. To recover one: get it moving (fix, jump, air up), then
  tow or drive it to a station or camp. It becomes yours, in rough shape with random worn parts.
- **Starting vehicle:** any stock vehicle except the Jesko, free at world creation.
- **Sandbox** unchanged: everything, free.

## 7. Multiplayer

- **Personal mode** (default; joining a friend):
  - The profile comes along.
  - Discoveries and caches count for everyone present; photos count for players whose truck is in
    the shot.
  - Job pay is split among helpers.
  - Your vehicles' condition saves to your profile when you leave. Your camp stays in the host's
    world but stays yours.
- **Shared crew mode** (chosen when the host creates the world): one bank, garage, set of camps and
  set of discoveries, saved in that world. Your personal profile is untouched while playing it.
- **Authority:** the host's game owns world state (job board, caches, camps, payouts), and per the
  collision work also vehicle-vehicle contacts. A guest cannot change another player's money or
  parts. Joining uses join codes.
- **Rescuing each other:** winch, tow, jump or repair a friend's truck, free to them (with your own
  parts).
- **Versions:** must match; a mismatch shows "UPDATE YOUR GAME".

## 8. Build order

| # | Phase | Playable result | Agent | Needs |
|---|---|---|---|---|
| 1 | Profiles and world saves, world list, New World, autosave, migration | Worlds with difficulty; trucks stay where and how you left them | Opus | — |
| 2 | Economy table and costs, tow or walk | Running costs; stranded choice | Sonnet | 1 |
| 3 | Outposts and range | Exploring extends range | Sonnet | 2, big maps phase 7 |
| 4 | Own camps | Deep remote trips | Sonnet | 3 |
| 5 | Jobs: board, radio, 5 types, pay | Rescue income | Opus | 2 |
| 6 | Discovery: pay, reveal, regions, caches, photo spots, album | Exploring pays | Sonnet | 1, big maps phase 7 |
| 7 | Vehicles: garage, dealer, delivery, selling, abandoned | Real garage | Sonnet | 1, 2 |
| 8 | Milestones and career page | Achievements | Sonnet | 6 |
| 9 | Landmark challenges | Big goals | Sonnet + Opus review | 4, 6, big maps phases 6–8 |
| 10 | Shared crew mode | Co-op career | Opus | 1, 5, join codes |
| 11 | Hardcore extras | Hardcore | Sonnet | 2, 7 |

Big maps phase 7 (points of interest) runs in parallel with career phase 1. The interactive garage
shop (separate work) reads career data and adapts as phases land.

## Testing

Each phase adds tests (`tests/career*.cpp`):
- save round-trips, migration from `career.txt`, an older save still loading, corrupt save → `.bak`
- per-difficulty cost and pay maths
- tow cost by distance
- camp place/pack/stock
- job generation reachability and each job type end to end
- pay split in multiplayer, and the host-authority checks (a guest can't grant money)
- discovery, region completion and the photo-spot check
- vehicle buy/sell/deliver, and recovering an abandoned vehicle
- Shared crew save

Full suite: only known failures. The physics fingerprint stays unchanged, except where a phase
deliberately changes physics (Hardcore wear), which must be called out.

## Out of scope (for now)

Story/characters, vehicle painting/cosmetics beyond challenge rewards, trading between players,
leaderboards, a convoy job type, trail ratings as a reward system.
