# Crashing into each other over the internet (net-collisions, 2026-10-25)

The report, from play over the internet (owner on Linux hosting, friends on Macs): "often one
person will crash into the other, but on their computer they never hit and are still driving
around fine. Also the physics are different between both."

## In plain words

Every player's computer drives its own truck and tells the others where it is, 20 times a
second. Until now each computer showed the other players' trucks **where they were a moment
ago**: a fixed safety margin plus the time the message took to arrive, 0.14 to 0.3 s on an
internet line. At 65 km/h that is 2.5 to 5 metres behind where the truck really is.

So when you drove into your friend, you hit the *ghost* of where they had been. Your computer
crashed you; theirs, which shows *you* a moment behind too, saw you still metres back, so for
them nothing happened. Each computer had its own, different crash, or none at all.

The fix: each computer now shows the other trucks **where they are now**. It works that out
from the latest message, running the truck on for the time the message took, along the curve
it was driving. Both computers then see the trucks touch at the same moment and place, and each
one crashes its own truck the way it should be crashed. In tests over lines from 1 to 250 ms
each way, with jitter and lost packets, both machines now see every hit, within 0 to 33 ms of
each other, and each truck gets the same knock as when both trucks are on one machine.

"The physics are different" had three parts, two of them fixed:
- the ghosts (above), and after a hit your friend's truck on your screen drifting 2.5 to 18 m
  from where it really was before it snapped back (now 0.3 to 1.4 m, eased back);
- a truck you keep pushing became *your* computer's for as long as you pushed and went its own
  way, metres off (now it goes back to its owner's say after 0.6 s);
- **probably different builds.** Your game runs from main; the friends likely ran the 0.4.0
  release. Since 0.4.0 the tires have 10 % more grip and warm up, and rocks and rivers changed.
  Both speak the same network protocol, so they could play together, but each truck drove by its
  own owner's physics. Now each side is told when the other runs a different build
  ("BUDDY RUNS BUILD 1c62ca1, THIS GAME 87f3859: HANDLING MAY DIFFER").

Mac against Linux math is *not* a cause: nothing in multiplayer expects two machines to compute
the same physics (each one only simulates its own trucks and sends their actual shape).

Everyone must update to 0.4.1 to play together (protocol 8, shared with join codes): a game
that shows trucks behind would see the old ghosts again, so mixing them is refused.

## How multiplayer worked (before)

- Each machine simulates its own vehicles at 2 kHz and sends each one's state (centre, rotation,
  velocity, every node's dent in mm, wheels, controls) at 20 Hz over UDP, delta-compressed;
  guests' states go through the host (relayed with their original stamp). Reliable messages
  (spawns, lines, chatter) over TCP.
- One clock (the host's); every state is stamped with it. A **remote** car on each machine is the
  other's truck, moved by those states: shown at `view.render` = shared clock less a delay of
  `max(0.1 s, mean lateness + 2 x send gap + 2.5 x jitter)` (net::Lateness, net::Timeline),
  interpolated between states. It is solid (it pushes back) but not simulated.
- When my truck hits a remote copy with more than 2 g of force and 1 m/s closing speed, the copy
  is **held**: taken onto my physics as a whole soft body for 0.4 s (re-armed while the force
  lasts), then handed back and eased onto its states (`Car::holdHere`, `handBackTo`,
  `settleShove`). Nothing is sent: the struck truck's owner only has a crash if, on *their*
  machine, my (delayed) copy reaches their (real) truck. Damage is each owner's own.
- That works for a parked target (net/crash-share): the lagged copy gets there in the end, both
  machines have about the same crash, at different times. With both moving it doesn't.

## Root causes, with numbers

Reproduction: `tests/net_state.cpp` `movingCrash` (now `net/moving-crash/*`). Two machines, a
Blazer each, A at 18 m/s into the back of B at 10 (or both at 12 into B's side; or B braking
hard / swerving 0.3 s before), states both ways at 20 Hz over a line 1 to 250 ms each way with
10 ms jitter and 2 % loss, against the same crash with both trucks on one machine. Run with the
old behaviour (`NetSession::predict = false`) at 75 ms each way:

1. **Copies shown in the past.** View delay 139 / 168 / 213 / 288 ms at 1 / 30 / 75 / 150 ms
   each way. Copy behind its owner's truck before the hit: 1.6 / 2.0 / 2.6 / 3.6 m mean (rear
   end), 1.5 to 3.2 m (side). Guest to guest goes through the host: both legs add up; a relayed
   join-code link adds more.
2. **Each machine has its own crash, at its own time.** Rear end at 75 ms: A's machine hit at
   1.73 s, B's at 2.90 s (1.2 s later, and only because A's driver kept the throttle on; a
   stopped or deflected A would never reach B there: the report). Side at 150 ms: B's truck got a
   1.1 m/s knock instead of 6.6 and 0.0 % damage instead of 6.6 %.
3. **After the hit the copy is far off**: 2.6 to 18 m, 0.5 s on (each machine's copy ran its own
   crash, from the wrong place).
4. **A held copy stays held while pushed**: re-armed for as long as the force lasted, it never
   went back to its states (6 m off in the braking case).
5. **Builds**: protocol 7 is spoken by 0.4.0 and by main since, but the physics changed in
   between (tire grip +10 %, warm-up, scanned rock colliders, river drag). Not provable from here
   without knowing what the friends ran; the most likely reason "the physics are different".
6. Not a cause: per-platform fingerprints (Mac and Linux differ: `bench_sim` 12 separated 4000
   is da17a95524fab674 here). No lockstep anywhere: states carry the node positions themselves.
   (A machine under 20 fps does run its own physics slower than real time: its truck is then
   slow on everyone's screen. Separate issue.)

## Designs

**A. Show the vehicles now; each machine crashes its own truck (built).** Run each copy on from
its newest state by that state's age, along its arc (turn rate from the last two states) and
with its yaw rate; each state that comes in corrects the guess, eased over 0.15 s. Both machines
then hold the copy and crash their own truck at the same moment. No new messages, no added
input lag, no authority, works the same direct, through the host or relayed.
Costs: what a driver does within the last latency is guessed (braking hard, the copy is ~0.2 m
and ~1.5 m/s off at 150 ms); after a crash the two machines' tangles differ a little, so the
copies are eased back. Chatter (finish times, "go") stays on the delayed view, so it now comes
a latency after the truck is seen crossing the line.

**B. A plus a "hit report".** The machine that sees a hit sends the struck owner the knock it
gave (impulse, point, time); the owner applies it if it had no hit of its own with that truck
within ±0.3 s. Covers the cases A guesses wrong (a hit seen on only one side). Costs: a new
message both ways (TCP and the join-code tunnel), dedupe that can double a knock if it gets it
wrong, a rigid shove without dents, and a way for a cheat to shove others (would need caps and
distance checks). In every A test both sides saw every hit, so this is not built; it is the
next step if real play still shows one-sided hits.

**C. The host decides every contact (or all physics).** The host runs both trucks in a crash;
guests send inputs and are corrected. The only design where the two machines can't disagree,
but guests drive a round trip behind their own inputs unless their truck is predicted and
rewound (a soft-body rewind at 2 kHz), the host's CPU grows with players, and it's a rewrite
(the shelved wip/server-authoritative-physics, .superpowers/REVIEW-server-authoritative*.md).
Not for 0.4.1. If it is ever wanted, the session host (the join-code authority) is the one.

**Recommendation: A**, built (below). B only if one-sided hits are still reported.

## What was built (wip/netsync)

- `NetSession::predict` (default true): vehicles shown at the shared clock less the frame about
  to be run, run on from the newest state up to `net::kMaxLead` = 0.5 s (then held), along the
  arc of travel and with the yaw rate, position and rotation errors eased out (`applyRemotes`).
  `vehicleTime()`, `vehicleLead()`; F3: "VEHICLES SHOWN NOW (RUN ON n MS)".
- Held copies: at most `Car::kMaxHold` = 0.6 s at a stretch (then back to their states, eased);
  and once the owner's states are 0.15 s past the moment of the hold, steered gently onto them
  (velocity toward theirs over ~0.15 s). Not earlier: during the knock itself that changed the
  damage (rear end, B's damage 2.3 % against 3.0 % on one machine).
- Protocol 8, release 0.4.1 (the same number as join codes). HELLO and WELCOME carry the build
  (`NetSession::build`, the git commit; trailing strings, so nothing else in them moved); a
  different build is let in with a warning to both sides.
- Tried and dropped: running copies on with their acceleration too (better while braking, much
  worse after a knock: 8 m off at 250 ms).

Results (`run_tests -v net/moving-crash`, five lines each, 0.4.1):

| | copies off before the hit | the two machines' hits apart | knock vs one machine | damage vs one machine (median) | copies off from 0.5 s after |
|---|---|---|---|---|---|
| rear end, 1 to 150 ms | 0.01 to 0.04 m | 0 to 17 ms | 0.99 to 1.01 | A 0.97 to 1.10, B 0.89 to 0.95 | 0.4 to 0.8 m |
| side, 1 to 150 ms | 0.00 to 0.02 m | 0 ms | 1.01 to 1.02 | A 1.19 to 1.31, B 0.98 to 1.09 | 0.6 to 1.0 m |
| B brakes, 1 to 150 ms | 0.01 to 0.05 m | 0 to 33 ms | 0.92 to 1.00 | A 0.76 to 1.00, B 0.61 to 1.25 | 0.75 to 1.1 m |
| B swerves, 1 to 150 ms | 0.00 to 0.02 m | 0 to 17 ms | 0.96 to 1.04 | 0.82 to 1.01 | 0.4 to 1.4 m |
| 250 ms each way (relayed) | 0.01 to 0.10 m | 0 to 33 ms | 0.86 to 1.03 | 0.56 to 1.25 | 1.7 to 2.8 m |
| 75 ms, 30 ms jitter, 10 % lost | 0.03 to 0.04 m | 0 to 17 ms | 0.96 to 1.03 | 0.58 to 1.31 | 0.5 to 0.8 m |

(Before, at 75 ms each way: copies 2.4 to 2.6 m off, hits 0.17 to 1.17 s apart, B's knock 0.42
to 0.88 of one machine's.)

Tolerances the tests hold: both machines see the hit; hits within 60 ms; each truck's knock
0.75 to 1.33 times one machine's; damage 0.6 to 1.6 times (median of five lines: a crash's
damage is bimodal, a nose folding up neatly or not, 2 % or 6 %; 0.5 for the braker's own truck
on a line of 150 ms each way or more, or lossy); copies within 0.25 m before the hit; within 1.5 m from 0.5 s after
(4 m at 250 ms each way).

## For the owner to decide

1. **Another build: warn (built) or refuse?** Refusing guarantees the same physics but means
   your dev build can't play with friends on a release.
2. **Hit report (design B)**: only if one-sided hits are still seen in real play.
3. **Chatter now trails the trucks** by the view delay (a finish heard ~0.1 to 0.3 s after the
   truck is seen crossing). Fine as is, or deliver chatter on arrival instead?
4. ~~Slow machines~~: decided, fixed (below).

Decided (2026-10-25): warn on another build and let in; no hit report for now; chatter as built.

## Slow machines (fixed after the decisions)

Below 20 fps a frame ran at most 50 ms of physics (`simMaxSteps`, and `Sim::advance` let the
rest go), as single player does so as not to spiral. In multiplayer that put a slow machine's
own truck in slow motion for everyone: its states are stamped with the shared clock, so the
others saw it cover 0.60 of the ground at 12 fps and 0.75 at 15 (`net/slow-host`, a Mac on a
6 km map), and kept easing it back as each state undid the guess at where it would be.

Now, in multiplayer only, every frame's physics is run (`simNetSteps`): as many steps as are
owed, hitches too, as far as 50 ms of physics a frame goes at the cost a step has had lately
(never fewer than before). What's still owed waits for the next frames, up to 0.25 s; past
that the machine can't run real time even with nothing else to do, and slows as before (a test
with 2 ms steps: 0.60 of real time, no spiral). Single player and the physics themselves are
unchanged (fingerprint da17a95524fab674). Measured: 12 and 15 fps hosts' trucks shown at 1.00 of
real speed, within 0.05 m of where they really are.
