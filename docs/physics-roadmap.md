# Physics roadmap: toward a BeamNG-class off-road sim

Notes from studying how a mature soft-body driving sim is organised (concepts only, from its
modder-facing text files; no code, data or tuning values copied). Everything here is to be
designed and tuned independently, against real-world measurements, in our own tests.

## What the reference architecture does that we don't (yet)

1. **Powertrain as its own rotational graph.** Engine → clutch/torque converter → gearbox →
   range box (high/low) → transfer case / centre coupling → axle diffs → wheel shafts. Each step:
   speeds flow up from the wheels, torque flows down from the engine; each device knows the
   inertia downstream of it, so the engine "feels" the whole driveline. Every device has
   parasitic losses (constant + speed + torque proportional), worsened by wear/damage.
   Drive torque reacts back onto the chassis through chosen frame nodes.
   *Ours:* rigid gear constraint solved at the end of `Car::wheelStep` with a crank inertia;
   no per-device losses, no torque converter, no range box as a device, no disconnect logic.
2. **Differentials as one device with modes:** open (tiny residual lock), clutch LSD
   (preload + torque-sensing lock, separate drive/coast), viscous (lock ∝ speed difference),
   locked (a stiff damped angle spring with a torque cap, so it can slip under abuse rather
   than being infinitely rigid), and actively commanded (e-locker / centre lock). Uneven
   torque split possible. *Ours:* open via the constraint solve, LSD/locker as velocity
   couplings with caps — refactor into one device with these modes.
3. **Disconnectable/broken shafts** fall out of the graph and spin a small virtual inertia
   (front axle disconnect, snapped half-shaft).
4. **Tires built from structure, with real pressure.** A ring of tread nodes around the hub
   with separate beam families (sidewall, tread, periphery, plus reinforcements), sidewall
   stiffness scaled by inflation pressure, and a gas volume: broken beams leak, the tire
   deflates, beads can unseat. Friction = ground model × tire multipliers × load sensitivity
   (grip per load falls as load rises) with a softness term. *Ours:* a contact disc with a
   slip curve and a psi scalar — the biggest fidelity gap for crawling.
5. **Beam vocabulary.** Plain spring-dampers plus: support (resist stretch only), bounded
   (free inside a range, limit spring/damper outside — shocks, limit straps, bump stops),
   anisotropic, precompression (preload), separate rebound damping and fast/slow damping
   split, high-frequency damping cutoff, plastic deform threshold + deform limit, break
   strength, break groups (whole assemblies snap together) and deform groups (damage feeds
   systems). Torsion bars (4-node angular springs) stiffen joints; rails + slide nodes locate
   axles and steering racks; hydros drive steering and actuators.
6. **Ground models** per surface: static vs sliding friction with a Stribeck speed between
   them, wet/hydrodynamic loss, roughness (vibration, noise), default sink depth, and for soft
   soils a non-Newtonian (power-law) drag with shear strength, so wheels sink, plough and
   churn. *Ours:* grip/bite/keep/sink per surface, a crush layer; no shear-strength model.
7. **Damage that matters to driving.** Deform groups degrade devices (added friction, then
   failure); part wear/odometer feeds friction; engine over-rev/over-torque damage; punctured
   tires deflate over time.
8. **Controllers:** traction control / yaw supervisors from wheel speed vs a virtual vehicle
   speed, diff-lock and centre-lock actuators, axle lift, tire-pressure control, drive-mode
   presets bundling diff/throttle/ESC settings, an electric winch device in the drivetrain.

## Suggested order for us (crawl-first)

1. Fix current regressions (see the cloud brief / agent.md): crawl tests, rear-axle flip.
2. Powertrain graph refactor: devices (engine, clutch, gearbox, range box, transfer/centre,
   diffs with modes, shafts with disconnect), per-device losses, torque reaction nodes.
   Tests: 0-100 and top speeds unchanged; open diff stalls on the twister, lockers clear it;
   centre lock binds on pavement; snapped shaft freewheels.
3. Structural pressure tires with air-down, sidewall flex over rocks, puncture/deflation,
   bead unseat at very low pressure + side load. Tests: contact patch length vs psi, ledge
   climb vs psi, puncture on sharp rock at speed.
4. Beam types: bounded beams for shocks/limit straps/bump stops, rebound damping split,
   torsion bars at link mounts; re-tune suspension; RTI against real trucks.
5. Ground model: static/sliding + Stribeck, soft-soil shear/drag, persistent ruts.
6. Damage → systems: bent axle housing changes camber/toe, bent tie rod, leaking diff,
   engine over-rev damage.
7. Controllers: ESC/TC from wheel speeds, e-locker actuators, tire-pressure control,
   drive-mode presets.
