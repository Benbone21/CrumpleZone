# Engine audio, recovery controls and gearing

The local build includes a new engine voice for every vehicle, pointer/controller
recovery picking, faster adjustable winching and drivetrain fixes.

## Try it

- **Shift+M** selects manual. **Q/E** select gears in order **R, N, 1, 2…**.
  Neutral is shown as **N** and lets the engine rev without driving the wheels.
  In manual, **W is throttle and S is brake**, including in reverse. On a controller,
  RT is throttle and LT is brake. Automatic retains its existing drive/brake/reverse
  pedal behavior. Reverse engagement is refused above walking speed; entering neutral
  is allowed while moving. Shift+M back to automatic selects first if in neutral.
- **V** opens the winch picker. Point at a marked tree, boulder, front/rear vehicle
  recovery point or ground anchor and click. The preview shows the selected attachment.
  **O** opens the strap picker: click your vehicle's front/rear area, then the other
  vehicle's front/rear area. Body clicks snap to that end's actual recovery point;
  these are rated attachment points, not arbitrary panel/body attachments.
- With a controller, the **right stick** aims the center crosshair and **A** selects
  each point; **B** cancels. With a mouse, middle-button drag turns the camera, the
  wheel zooms, and Esc/right-click cancels. Backspace in the second strap step returns
  to choosing your point. Clicks in empty space don't silently attach another vehicle.
  V/O with a line already attached unhooks it.
- **Z** reels in; **Shift+Z** pays out. **-/=** step through 0.1x, 0.25x, 0.5x,
  1x, 2x, 4x, 6x, 8x, 10x, 15x and 20x. Esc → Settings → **WINCH SPEED** adjusts
  the same saved preference. The default is **4x**, about **0.52 m/s unloaded**;
  20x gives **2.6 m/s unloaded**. The multiplier changes drum speed, not pull rating:
  speed still falls with load, the winch stalls at its rated pull and the line can snap.
  At 1x it retains the original 0.13 m/s unloaded speed.

The strap reaches 9 m and the winch plus extension reaches 60 m. Unattended towed
vehicles release both brakes and temporarily select neutral. They steer to follow
and no longer apply a hidden brake whenever the line goes slack. Taking their wheel,
unhooking, or ending the tow restores the prior gear. An active player retains control
of their brakes and gears; select neutral when your friend is towing you. Tire grip,
rolling resistance, suspension and rope strength are unchanged.

Reverse wheelspin previously activated engine-braking assistance in the wrong direction,
opening the throttle after release. The assist now reads slip relative to travel direction.
Manual gearbox shifts open the clutch and match RPM through the shift. Driver-selected shifts in automatics
unlock the torque converter and keep engine speed independent of turbine speed; the
automatic controller retains its existing lockup/overlap behavior. Near-instant LST
and stepped CVT models retain their continuous coupling. Existing downshift redline
checks and automatic gear hysteresis remain.

## Audio audition

The synth now uses a full 720-degree combustion cycle with softer compression/rarefaction
pulses, distinct cross-plane bank rumble, exhaust/intake resonances scaled by engine size,
and quieter turbo/blower sounds. RPM and load change pitch and timbre smoothly. The same
voices serve the driver and nearby multiplayer cars. These are procedural approximations,
not recordings of the actual cars; subjective sound quality still needs a listening review.

Generate silent WAV previews with:

```sh
nice -n 19 make -j 2 build/engine_preview
nice -n 19 ./build/engine_preview build/review/engine-audio/after
```

Each clip includes idle, acceleration, a shift, throttle lift and engine stop. The combined
`all-vehicles.wav` plays Civic, Jesko, Bronco, 4Runner, buggy, UTV and Tacoma in catalog order.
No sound device opens when generating these files.

## Validation

`drivetrain/` covers neutral/free revving for all vehicles, manual pedal semantics,
reverse coast-down and shift matching. New `recovery/` scenarios cover point picking,
a healthy running car being towed, and speed/stall/payout behavior. Offline audio checks
cover spectrum/pitch, load response, voice differences, sample-rate changes, stalls,
stereo nearby engines and concurrent publication; SDL's dummy driver tests the callback
without playing sound. Windows cross-compilation and offscreen Linux rendering are checked
locally. Native Windows/NVIDIA and live two-player tests still require actual hardware.

Local validation result: the full run before the final automatic-controller correction
passed 441/461 scenarios. The final affected recheck passed 26/26, including the corrected
Backcountry autopilot failure; the other 19 failures predate this follow-up (seven cannot
open network sockets in this sandbox). No physics range or baseline was widened. The
14 audio checks and offscreen render integration passed. Reports and logs are in
`build/review/recovery-drivetrain/`.
