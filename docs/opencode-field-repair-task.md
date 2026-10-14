# OpenCode task: field repairs, tire work, and first-person feel

Use the locally configured **`qwen3.8:latest`** model. Read `agent.md`, `README.md`, and `docs/handoff.md` before changing code. Work from current `main` in a separate branch named `wip/field-repair`; do not modify `main` directly. The user asked for implementation work on the game's trail-side repairs, quicker tire changes, visible tire behavior, a jack, and first-person controls.

## Start by understanding the existing system

The game already has an on-foot interaction loop and wheel handling:

- `src/interact.h/.cpp`: gaze-ray target selection and hold-E jobs; current jobs include taking/stowing the spare, removing/fitting a wheel, reseating a debeaded tire, and picking up a loose wheel.
- `src/tires.h/.cpp`: `WheelItem` (wheel/tire state), loose wheels, spare handling, and wheel fit/remove. Wheel changes currently take 8 seconds off and 10 seconds on; reseating a tire takes 20 seconds.
- `src/walker.h/.cpp`, `src/main.cpp`: walking, camera/look, keyboard/controller input and prompts.
- `src/recovery.cpp`: automatic stopped-vehicle field repairs. At present, a nearby stopped helper vehicle repairs a popped tire, shaft/CV, tie rod, bent suspension arm, and sway bar after a timer. Repairs are effectively free and do not consume parts. Keep shop-station full repairs distinct from trail repairs.
- `src/render.cpp`: wheel meshes already draw the tire and rim as separate vertex ranges. A flat currently scales the tire almost down to the rim; a debeaded tire leaves the rim on the hub. Tire pressure updates in `Sim::updateTrail`, but the tire mesh does not visibly deform as pressure changes.
- `tests/tires.cpp`, `tests/recovery.cpp`, and `tests/walker.cpp`: current behavior coverage. Check `docs/handoff.md` for known failing tire scenarios before interpreting results.

Trace the real call paths before editing. Preserve working wheel physics, vehicle-specific wheel styles and sizes, the multiplayer ownership rules, and the existing distinction between a flat tire, a debeaded tire, a removed wheel, a broken shaft, and a damaged suspension link.

## User's goals

1. Make trail-side repair a physical, understandable activity. Repairs to suspension and axles should require the right replacement parts and use them up. Players should be able to inspect what their vehicle or helper truck carries. Keep repair stations useful for complete repairs and resupply.
2. Make removing and fitting tires/wheels substantially quicker than the current 8/10-second holds. Keep actions long enough to read clearly, but tune them to feel responsive. The user singled out tire changes as the first place to improve.
3. Add a usable jack. The player should place/use it to lift a wheel or support an axle for safe work. Show where it can be placed and why placement or lifting failed. The vehicle must remain supported while the wheel is off; do not fake the jack by simply teleporting the body or disabling gravity.
4. Make tire state legible from a normal driving view. Airing up/down should visibly change tire shape over time. A puncture should animate into a visibly deflated/damaged tire while leaving the actual rim visibly present. A debeaded tire should separate from and be removable from its rim; a bare wheel rim is not the same thing as a flat. Preserve each car's correct tire size, sidewall width, and wheel/rim style.
5. Improve first-person on-foot handling so looking, aiming, walking, approaching a repair point, and carrying/placing a wheel feel direct and predictable on mouse/keyboard and controller. Keep prompts/crosshair readable and make cancellation/recovery from a mis-aimed interaction obvious.

## Implementation approach

Start with a brief code-backed implementation outline in your response, then implement the smallest coherent playable slice. Prioritize in this order:

### A. Repair inventory and honest field repairs

- Replace free, invisible automatic part replacement with explicit repair-kit inventory and clear player feedback. Decide and document which parts the standard field kit carries (at minimum a tire/wheel repair item, half-shaft/CV, tie rod, and a suspension-link repair item) and reasonable starting counts.
- Consume the corresponding part when fixing a popped tire, broken shaft/CV, bent tie rod, or damaged suspension arm. If a needed part is unavailable, preserve the damage and say what is missing. Do not silently repair a component with no replacement.
- Make the helper-vehicle behavior visible and consistent with its inventory: the helper can work only while stopped and nearby, and its repair animation/timer and inventory consumption must have a clear start, progress, completion, and failure state. Avoid repeatedly restarting progress when target choice or frame timing changes slightly.
- Keep the player in control of whether a repair starts where practical. Do not remove station-based full repair, existing road recovery, or multiplayer restrictions as a side effect.

### B. Jack-assisted, faster wheel work

- Reduce the current wheel-off/on interaction holds substantially (aim around 3–5 seconds each for a normal wheel change); make tire-only bead work faster than its current 20 seconds. Keep pick-up/stow separate. Use understandable progress feedback.
- Add an actual jack/tool state and ground placement/vehicle lift interaction, with reachable rated lift points. A wheel cannot be removed or fitted until its corner is safely unloaded, unless a documented vehicle/tool rule makes that possible. Prevent the car from falling through the jack while supported. Lowering/removing the jack restores the vehicle's weight to its suspension naturally.
- Keep failure cases safe and clear: moving vehicle, unstable/slope placement, invalid lift point, jack capacity exceeded, or missing tool should refuse the action without losing the wheel/part or leaving the car in a corrupted pose.
- Avoid implementing a large new rigid-body machine if the existing world/support model can represent a stable jack safely. If safe dynamic support proves to require a larger physics feature, finish the inventory and tire-visual slice first and report the remaining jack work with a concrete estimate.

### C. Tire visuals and first-person interaction feel

- Derive visible tire deflection from current tire pressure and load/state. Animate pressure changes and the transition into a flat instead of snapping between an inflated tire and a tiny ring. Keep a clearly defined rim mesh visible under a flat tire, with rubber remaining around it. A debeaded tire should visibly detach as its own `WheelItem` while the rim stays mounted.
- Use the existing separate tire/rim mesh ranges where possible; change mesh construction only if needed for a genuinely convincing shape. Preserve tread, sidewall width, wheel styles, brake placement, loose-wheel and carried-wheel rendering.
- Review mouse/controller sensitivity, acceleration and deadzones for on-foot view and aim. Make an interaction that is started then canceled easy to restart. Keep controls consistent with the current recovery picker and gamepad conventions; update the controls/help text if bindings change.
- Keep first-person hand/tool/wheel animation lightweight and readable. Do not add elaborate animation assets or block the repair logic on external artwork.

## Requirements and limits

- Keep the changes focused on field repair, tire/wheel handling, jack/support, and first-person controls. Do not alter vehicle mass, tire grip, suspension rates, drivetrain behavior, networking protocol, or collision tolerances just to make the new interactions easier.
- Do not hand-edit `tests/baseline.txt`, run `--update-baseline`, or widen a physics range to hide a failure. `docs/handoff.md` records known failures in `tires/shear`, `tires/debead`, and tire-swap mass accounting; report whether each is pre-existing or changed by your work.
- Keep the code usable on Windows and Linux. Do not introduce platform-specific input or rendering paths without matching the game's wrappers.
- Don't claim a feature is complete unless it is implemented, represented in the UI, and exercised. If a jack cannot be made safe in this task, say so plainly and leave its behavior unimplemented rather than adding an unsafe visual-only jack.
- Build and run the relevant existing checks using the repository's `nice -n 19` / low-job guidance. Record exactly what passed, failed, or could not run. Do not open a visible game window while the user may be using the machine.
- Do not commit or push without the user's explicit request. Finish with changed files, controls, repair inventory behavior, tests/builds, remaining limitations, and a short Windows/Linux testing note.

## Completion checklist

- [ ] Trail repairs require and consume named parts; missing parts leave damage intact and explain why.
- [ ] Tire removal/fitting is notably faster; invalid/canceled interactions do not consume the wheel or item.
- [ ] Jack placement/lift/support/lower behavior is safe, or the unfinished physics scope is clearly reported without a misleading jack feature.
- [ ] Inflation, deflation, puncture, debeading, bare rim, and detached tire/wheel have distinguishable visual states and smooth transitions.
- [ ] On-foot view and aim feel responsive on mouse and controller; help/prompts match the bindings.
- [ ] Relevant checks and builds are recorded without changing the physics baseline.
