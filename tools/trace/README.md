# Trace tools

Throwaway-grade C++ probes written while chasing physics problems: each builds a vehicle in a
test world, does one thing (drops it, crawls a ledge, slides it into a curb, brakes it) and
prints what matters. They're kept so the next person doesn't have to write them again.

    make build/run_tests                                   # builds the objects they link
    tools/trace/build.sh tools/trace/main/rltrace.cpp      # -> build/trace/main/rltrace
    build/trace/main/rltrace ledge 3

Vehicle indices (vehicleCatalog()): 0 Civic, 1 Jesko, 2 Bronco, 3 4Runner, 4 rock buggy,
5 UTV, 6 Tacoma. Most read their options from argv and environment variables; read the top of
`main()`. Several used temporary `CZ_*` environment switches in the physics code (CZ_LT,
CZ_UP, CZ_PAN2, CZ_PANMIR, CZ_BORROW, ...) that were removed before commit: those options
do nothing now. The `.sh` files are the sweeps that drove them (run from anywhere).

Folders: `main/` (this session's), `buggy-utv/` and `mergefix/` (the buggy/UTV retune and
merge-failure agents), `brake/` (4Runner braking yaw), `rearlinks/` (spring preload and the
4Runner ledge), `civic-sleep/` (the parked-car sleep bug), `stale/` (no longer compile
against the current code; kept for the idea).

| Tool | What it's for |
|---|---|
| `brake/brake.cpp` | usage: brake VEH [opts...]   opts: zero (items z=0), mirror, noesc, noabs, mode=N, neutral, verbose, nobrake |
| `buggy-utv/air.cpp` | (no notes: see main()) |
| `buggy-utv/brackets.cpp` | How far each frame-mounted node (brackets) moves against the body under full throttle. |
| `buggy-utv/drive.cpp` | Trace: full throttle from rest for vehicle argv[1]. |
| `buggy-utv/hubs.cpp` | Where each hub sits in the body's frame vs its design position: at rest, then flat out. |
| `buggy-utv/rub.cpp` | Which faces does a tire rub at speed? argv[1] vehicle, argv[2] seconds |
| `buggy-utv/settle.cpp` | Settle test replica: which nodes are still moving after 4 s, and how. |
| `buggy-utv/shafts.cpp` | (no notes: see main()) |
| `buggy-utv/top.cpp` | Freeway flat-out trace: argv[1] vehicle, argv[2] seconds, argv[3] drive mode (optional) |
| `buggy-utv/whoops.cpp` | Whoops lane trace: argv[1] vehicle, argv[2] target km/h |
| `civic-sleep/mt.cpp` | (no notes: see main()) |
| `civic-sleep/sl.cpp` | (no notes: see main()) |
| `main/aistuck.cpp` | ai/stuck-recovery traced. |
| `main/angles.cpp` | Axle x positions and tire radius (local x along fwd). |
| `main/axlemass.cpp` | (no notes: see main()) |
| `main/axlenodes.cpp` | (no notes: see main()) |
| `main/axlesteer.cpp` | axle yaw from the lower link axle ends (lowMount = wheel.lower) |
| `main/barbend.cpp` | (no notes: see main()) |
| `main/barrier.cpp` | damage/barrier-bump (square): which beams tear. |
| `main/bay.cpp` | (no notes: see main()) |
| `main/bland.cpp` | (no notes: see main()) |
| `main/blowup.cpp` | (no notes: see main()) |
| `main/brakeflow.cpp` | Full brakes from 100 km/h: does the frame flow? toe of each wheel? |
| `main/brakehead.cpp` | Full brakes from 100 km/h: heading and drift, plus each wheel's toe and brake torque. |
| `main/brakesym.cpp` | (no notes: see main()) |
| `main/braketrace.cpp` | (no notes: see main()) |
| `main/bstop.cpp` | (no notes: see main()) |
| `main/bump.cpp` | (no notes: see main()) |
| `main/cg.cpp` | Center of mass height and roll inertia of each vehicle, settled on flat ground. |
| `main/cliff.cpp` | (no notes: see main()) |
| `main/clip.cpp` | Clipping probe: harsh events, and how far parts pass into each other. |
| `main/curb.cpp` | Slides a vehicle sideways into a curb at a speed (km/h); reports peak stud load and sheds. |
| `main/doormass.cpp` | (no notes: see main()) |
| `main/droop.cpp` | (no notes: see main()) |
| `main/droptrace.cpp` | housing: diff, pinion, dtop |
| `main/dropyield.cpp` | (no notes: see main()) |
| `main/engpts.cpp` | (no notes: see main()) |
| `main/flowtrace.cpp` | Drop a truck and report when/where its rails flow (rest moves) for the first time. |
| `main/framebend.cpp` | Three-point bend of a ladder frame: the truck on its wheels, a press pushing down on the |
| `main/keyhold.cpp` | handling/key-hold-130 traced: sideslip, steer, each wheel's load, slip angle and toe (rear axle steer). |
| `main/landflow.cpp` | A jump landing (axle-landing "1.5 m at 40 km/h"): which frame members flow, and how far. |
| `main/launchtrace.cpp` | Trace a full-throttle launch: gear, rpm, engine torque, tcCut, wheel omega*R vs speed. |
| `main/ledge.cpp` | Nose off a 1.2 m ledge (rock-hits #3): where does the frame take a set, and when. |
| `main/linkgeo.cpp` | (no notes: see main()) |
| `main/links.cpp` | (no notes: see main()) |
| `main/lowpts.cpp` | lowest colliding nodes overall excluding hub inner/outer |
| `main/massmap.cpp` | (no notes: see main()) |
| `main/mesastart.cpp` | (no notes: see main()) |
| `main/nodebeams.cpp` | List the beams at the frame node nearest a rest position. |
| `main/nodeinfo.cpp` | (no notes: see main()) |
| `main/nodeload.cpp` | (no notes: see main()) |
| `main/pininfo.cpp` | (no notes: see main()) |
| `main/quadwind.cpp` | (no notes: see main()) |
| `main/railallow.cpp` | (no notes: see main()) |
| `main/railgroups.cpp` | (no notes: see main()) |
| `main/railload.cpp` | (no notes: see main()) |
| `main/ram.cpp` | the pulse is applied per frame as an extra velocity change (impulse F*dt over the hub's mass) |
| `main/rltrace.cpp` | Usage: trace rest <vehicleIdx> / trace ledge <vehicleIdx> |
| `main/rollover.cpp` | damage/rollover: the loaded 4Runner's fishhook, traced. |
| `main/rtitrace.cpp` | RTI ramp: which wheel lifts, and where each spring is in its travel when it does. |
| `main/scrape.cpp` | (no notes: see main()) |
| `main/settle2.cpp` | dynamics/settle: the fastest node each 0.1 s of the last 2 s, and the body's motion. |
| `main/settlefast.cpp` | (no notes: see main()) |
| `main/settletrace.cpp` | (no notes: see main()) |
| `main/slope.cpp` | (no notes: see main()) |
| `main/snowroad.cpp` | frontier/snow-road traced: the 4Runner up the Cold Creek road at 30 km/h. |
| `main/splittrace.cpp` | (no notes: see main()) |
| `main/stance.cpp` | (no notes: see main()) |
| `main/stations.cpp` | (no notes: see main()) |
| `main/tilt.cpp` | Downhill is the car's right (+z): a lip outboard of the right tires stops them sliding. |
| `main/tipframe.cpp` | (no notes: see main()) |
| `main/turncircle.cpp` | outer front wheel when turning left = front right |
| `main/twist.cpp` | Torsion of a ladder frame alone (body mounts cut) vs with the body on, loaded at the axle stations. |
| `main/unsprung.cpp` | (no notes: see main()) |
| `main/washtrace.cpp` | (no notes: see main()) |
| `main/widow.cpp` | (no notes: see main()) |
| `main/windup.cpp` | (no notes: see main()) |
| `mergefix/cg.cpp` | Builds every catalog vehicle once (CZ_CG=1 prints the stability pass's CG before and after). |
| `mergefix/drive.cpp` | Full throttle from rest on the proving ground pad; prints the drivetrain each 0.25 s. |
| `mergefix/hang.cpp` | Holds a vehicle's body still in the air (wheels hanging at full droop) and prints each tire's |
| `mergefix/seq.cpp` | Runs the named scenarios one after another on this thread, the way run_tests' pool does, |
| `mergefix/shaftstab.cpp` | Per vehicle and suspension mod: the half shaft's damper against the side gear's inertia, and the |
| `mergefix/spawn.cpp` | The first second after a vehicle is added: each wheel's spin, shaft wind-up and load per frame. |
| `mergefix/touch.cpp` | Substep trace of one wheel around the first touchdown after a vehicle is added. |
| `mergefix/whoops.cpp` | The whoops run from tests/offroad.cpp for one vehicle, traced: speed, pitch, roll, wheel loads, ESC/TC. |
| `rearlinks/trace.cpp` | Usage: trace rest <vehicleIdx> / trace ledge <vehicleIdx> |
| `stale/export.cpp` | Export a settled vehicle's render geometry (body, parts, running gear, wheels) as triangles |
| `stale/fender.cpp` | which tris are touched: report their nodes' types |
| `stale/modrub.cpp` | A modded truck at full throttle: does a tire rub its body? (vehicle, slot, option) |
| `stale/rubcheck.cpp` | full-lock circle at walking pace, then a hard bump: max rub |
| `stale/tacdrop.cpp` | Drop a vehicle 0.3 m: how the body, the frame and the rear axle move afterwards. |
