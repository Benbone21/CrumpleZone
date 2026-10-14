# BeamNG.drive Physics Analysis & CrumpleZone Implementation Plan

## Executive Summary

After reverse-engineering BeamNG.drive's vehicle definition files (Jbeam format) and analyzing their soft-body physics approach, this document outlines specific techniques to improve CrumpleZone's physics simulation. BeamNG uses a node-and-beam soft-body system remarkably similar to CrumpleZone's approach, making many techniques directly transferable.

Key areas for improvement:
1. **Suspension tuning** - Real-world spring/damper rates with velocity-dependent damping
2. **Damage model** - Separate yield/break thresholds, break groups, plastic deformation
3. **Tire model** - Load sensitivity, slip angle curves, self-collision
4. **Drivetrain** - More detailed gear modeling, limited-slip differentials
5. **Steering geometry** - Ackerman steering, bump steer correction
6. **Multi-material beams** - Different beam types for different structural components

---

## 1. BeamNG's Physics Architecture

### Core Model
- **Nodes**: Point masses with position, velocity, mass
- **Beams**: Spring-damper elements connecting nodes (Hooke's Law + damping)
- **Triangulated meshes**: For collision detection
- **2 kHz timestep**: Fixed timestep for stability
- **Explicit integration**: Velocity Verlet or similar

### Beam Types
BeamNG defines multiple beam types via flags:
- `|NORMAL`: Standard spring-damper beam
- `|BOUNDED`: Beam with length limits (prevents compression beyond threshold)
- Various combinations with precompression, limit springs, etc.

### Key Insight: Separation of Yield and Break
BeamNG uses **two thresholds** for beam failure:
- `beamDeform` (yield): Beam permanently deforms past this strain
- `beamStrength` (break): Beam snaps completely past this strain

This creates realistic progressive damage: metal bends before it breaks.

---

## 2. Suspension System Improvements

### Current CrumpleZone Implementation
Simple spring-damper model with basic rates.

### BeamNG Approach
BeamNG models suspension with **multiple layered beams** for different aspects:

1. **Main spring**: Linear spring rate (e.g., 24000 N/m front, varies by vehicle)
2. **Primary damper**: Velocity-dependent damping (e.g., 3400 N/(m/s) bump)
3. **Rebound damper**: Different rate for extension (e.g., 6300 N/(m/s))
4. **High-speed choke**: Additional damping at high velocities (>3 m/s)
5. **Bump stop**: Progressive non-linear spring at end of travel
6. **Pre-compression**: Initial spring preload

### Implementation for CrumpleZone

```hemlock
// Replace simple spring with layered suspension
fn suspensionForce(compression, velocity) {
    let springRate = 24000.0;  // N/m
    let springForce = springRate * compression;
    
    // Velocity-dependent damping (higher at high speeds)
    let damping;
    if (velocity < 0.06) {
        damping = 3400.0;  // Normal bump damping
    } else {
        damping = 3400.0 + 2000.0;  // High-speed choke adds damping
    }
    let dampForce = damping * velocity;
    
    // Progressive bump stop at end of travel
    let bumpStopForce = 0.0;
    if (compression > 0.12) {  // Past 120mm of travel
        let excess = compression - 0.12;
        bumpStopForce = 36000.0 * excess;  // Progressive bump stop
    }
    
    return springForce + dampForce + bumpStopForce;
}
```

### Recommended Spring Rates by Vehicle
Based on BeamNG data:
- Civic: 24000 N/m front, 28000 N/m rear
- Jesko: 70000 N/m (race coilovers)
- Bronco: 40000 N/m (lifted truck)
- 4Runner: 35000 N/m (off-road setup)
- Tacoma: 40000 N/m front, 30000 N/m rear (leaves)

---

## 3. Progressive Damage Model

### BeamNG's Approach
BeamNG uses **break groups** for localized damage:
- Each part of the vehicle has a break group
- When beams in a group yield, the mesh deforms permanently
- When beams break, the mesh splits into separate pieces
- Glass has its own damage model (crack propagation)

### Current CrumpleZone Implementation
Simple damage scaling based on impact force.

### Implementation for CrumpleZone

Add a **damage multiplier system** with progressive failure:

```hemlock
struct DamageZone {
    name: string;
    yieldThreshold: f32;  // Force at which deformation starts
    breakThreshold: f32;  // Force at which part breaks
    currentStrain: f32;   // Accumulated deformation
    isBroken: bool;
    deformGroup: string;  // Group for mesh deformation
}

// Vehicle has multiple damage zones
let vehicleDamageZones = [
    { name: "bumper_front", yieldThreshold: 5000, breakThreshold: 15000, deformGroup: "bumper" },
    { name: "hood", yieldThreshold: 8000, breakThreshold: 20000, deformGroup: "hood" },
    { name: "door_fl", yieldThreshold: 6000, breakThreshold: 12000, deformGroup: "door_fl" },
    { name: "frame_rail_l", yieldThreshold: 15000, breakThreshold: 40000, deformGroup: "frame" },
    // ... more zones
];
```

### Mesh Deformation on Damage
When beams yield:
1. Calculate deformation amount based on force
2. Deform vertices in the damage zone's deform group
3. Update collision mesh

When beams break:
1. Split mesh into separate pieces
2. Update physics for each piece
3. Add debris particles

---

## 4. Enhanced Tire Model

### BeamNG's Tire Model
BeamNG uses a sophisticated tire model with:
- **Slip angle curves**: Non-linear grip vs slip angle
- **Load sensitivity**: Grip increases with load but not linearly
- **Self-collision**: Tires can't clip through each other or own body
- **Tread vs sidewall**: Different grip characteristics

### Key Parameters from BeamNG
From the suspension files, BeamNG uses:
- `softnessCoef: 0.5` - Tire stiffness modifier
- `treadCoef: 1.0` - Tread grip modifier
- `frictionCoef: 0.5` - Rolling friction
- `slidingFrictionCoef` - Sliding friction

### Implementation for CrumpleZone

Replace linear tire grip with a **slip curve**:

```hemlock
fn tireGrip(slipAngle, load, speed) {
    // Pacejka-like magic formula
    let peakSlip = 0.1;  // Slip angle for peak grip (radians)
    let peakGrip = 1.0;  // Peak lateral grip coefficient
    
    // Load sensitivity (diminishing returns at high load)
    let loadFactor = 1.0 / (1.0 + load * 0.0001);
    
    // Speed sensitivity (less grip at high speed)
    let speedFactor = 1.0 / (1.0 + speed * 0.001);
    
    // Slip curve (sine-based approximation of Pacejka)
    let slipFactor = sin(peakSlip * slipAngle) / sin(peakSlip);
    
    return peakGrip * loadFactor * speedFactor * slipFactor;
}
```

### Tire Self-Collision
Prevent tires from clipping through the body:

```hemlock
fn checkTireSelfCollision(tirePosition, vehicleBody) {
    // Simple sphere vs box collision
    for (let part in vehicleBody.parts) {
        if (part == tire) continue;
        
        let dist = distance(tirePosition, part.position);
        let minDist = tire.radius + part.halfWidth;
        
        if (dist < minDist) {
            // Push tire out
            let pushDir = normalize(tirePosition - part.position);
            tirePosition += pushDir * (minDist - dist) * 0.1;
        }
    }
}
```

---

## 5. Drivetrain Improvements

### BeamNG's Powertrain
BeamNG models powertrain with:
- **Engine torque curve**: Real-world torque vs RPM data
- **Gearbox**: Fixed gear ratios with efficiency losses
- **Differentials**: Open, limited-slip, locking
- **Half-shafts**: Torque coupling to wheels
- **Powertrain losses**: Drivetrain friction and windage

### Key Parameters
From the etki suspension files:
- `torqueCoupling`: Links wheel to drivetrain
- `torqueArm`: Links wheel to suspension
- `friction: 1.1` - Drivetrain friction factor
- `dynamicFriction: 0.0028` - Dynamic friction

### Implementation for CrumpleZone

Add **engine torque curve** instead of constant torque:

```hemlock
struct EngineCurve {
    rpm: f32;
    torque: f32;
}

let civicTorqueCurve = [
    { rpm: 1000, torque: 180 },
    { rpm: 3000, torque: 200 },
    { rpm: 5000, torque: 190 },
    { rpm: 7000, torque: 160 },
];

fn getEngineTorque(rpm) {
    // Interpolate torque curve
    let lower = findLowerPoint(rpm);
    let upper = findUpperPoint(rpm);
    let t = (rpm - lower.rpm) / (upper.rpm - lower.rpm);
    return lerp(lower.torque, upper.torque, t);
}
```

### Limited-Slip Differential
Model differentials with slip ratio:

```hemlock
fn limitedSlipDifferential(torqueLeft, torqueRight, slipRatio) {
    // Open differential: torque splits evenly
    // Limited-slip: transfers torque to wheel with more grip
    // Locking: both wheels rotate together
    
    let diffFactor = 0.5 + 0.5 * slipRatio;  // 0.5 = open, 1.0 = locked
    return torqueLeft * diffFactor + torqueRight * (1.0 - diffFactor);
}
```

---

## 6. Steering Geometry

### BeamNG's Steering
BeamNG models steering with:
- **Ackerman geometry**: Inner wheel turns more than outer wheel
- **Bump steer correction**: Wheel steering angle changes with suspension travel
- **Power steering**: Assist force proportional to steering speed
- **Steering ratio**: Degrees of wheel turn per degree of steering input

### Key Parameters
From the etki steering files:
- `steeringWheelLock: 550` - Maximum steering wheel rotation (degrees)
- `toe_FR: 1.0007` - Toe adjustment factor (1.0 = parallel)
- `beamPrecompression`: Used for toe angle
- `factor: 0.83` - Power steering assist factor

### Implementation for CrumpleZone

Add **Ackerman steering**:

```hemlock
fn ackermanSteering(steerAngle, wheelBase, trackWidth) {
    // Inner wheel turns more than outer wheel
    let innerAngle = atan(wheelBase / (trackWidth / 2.0 - tan(steerAngle) * wheelBase));
    let outerAngle = atan(wheelBase / (trackWidth / 2.0 + tan(steerAngle) * wheelBase));
    
    return { inner: innerAngle, outer: outerAngle };
}
```

Add **power steering assist**:

```hemlock
fn powerSteeringAssist(steeringSpeed, vehicleSpeed) {
    // More assist at low speed, less at high speed
    let assistFactor = 1.0 / (1.0 + vehicleSpeed * 0.1);
    return steeringSpeed * assistFactor * 0.83;
}
```

---

## 7. Aerodynamics

### BeamNG's Approach
BeamNG models aerodynamics with:
- **Drag coefficient**: `scaledragCoef: 0.93` for etki
- **Downforce**: Generated by body shape and aero parts
- **Yaw moment**: Side winds affect vehicle direction
- **Lift distribution**: Different lift on front vs rear

### Implementation for CrumpleZone

Add **speed-sensitive downforce**:

```hemlock
fn calculateDownforce(speed, downforceCoeff) {
    // Downforce increases with square of speed
    return downforceCoeff * speed * speed * 0.5;
}

// Apply to front and rear axles
fn applyAeroForces(vehicle, speed) {
    let frontDownforce = calculateDownforce(speed, vehicle.frontDownforceCoeff);
    let rearDownforce = calculateDownforce(speed, vehicle.rearDownforceCoeff);
    
    // Add to axle loads
    vehicle.frontAxleLoad += frontDownforce;
    vehicle.rearAxleLoad += rearDownforce;
}
```

---

## 8. Multi-Material Beam System

### BeamNG's Approach
BeamNG uses different beam materials for different parts:
- **Frame beams**: Very high stiffness (e.g., 10001000 N/m)
- **Body panels**: Lower stiffness (e.g., 100000 N/m)
- **Suspension links**: Medium stiffness (e.g., 2501000 N/m)
- **Rubber mounts**: Very low stiffness (e.g., 10000 N/m)

### Key Beam Parameters from BeamNG
- `beamSpring`: Spring constant (N/m)
- `beamDamp`: Damping constant (N/(m/s))
- `beamDeform`: Yield threshold (strain)
- `beamStrength`: Break threshold (strain)
- `beamLimitSpring`: Limit spring constant (for bounded beams)
- `beamLimitDamp`: Limit damping constant

### Implementation for CrumpleZone

Define **beam materials** with different properties:

```hemlock
struct BeamMaterial {
    name: string;
    spring: f32;
    damp: f32;
    yieldStrain: f32;
    breakStrain: f32;
    density: f32;
}

let beamMaterials = {
    steel_frame: { name: "steel", spring: 10001000, damp: 400, yieldStrain: 0.005, breakStrain: 0.02, density: 7850 },
    aluminum_body: { name: "aluminum", spring: 2501000, damp: 100, yieldStrain: 0.003, breakStrain: 0.01, density: 2700 },
    rubber_mount: { name: "rubber", spring: 10000, damp: 100, yieldStrain: 0.1, breakStrain: 0.3, density: 1100 },
    plastic_panel: { name: "plastic", spring: 500000, damp: 50, yieldStrain: 0.02, breakStrain: 0.05, density: 1000 },
};
```

---

## 9. Integration Plan

### Phase 1: Quick Wins (1-2 days)
- [ ] Add velocity-dependent suspension damping
- [ ] Implement progressive bump stops
- [ ] Add speed-sensitive downforce
- [ ] Tune spring rates per vehicle (use real-world data)

### Phase 2: Damage Model (3-5 days)
- [ ] Implement separate yield/break thresholds
- [ ] Add damage zones with deform groups
- [ ] Progressive mesh deformation on damage
- [ ] Glass crack propagation (optional)

### Phase 3: Tire Model (2-3 days)
- [ ] Implement slip angle curves
- [ ] Add load sensitivity
- [ ] Tire self-collision
- [ ] Tread vs sidewall grip

### Phase 4: Drivetrain (2-3 days)
- [ ] Add engine torque curves
- [ ] Implement limited-slip differentials
- [ ] Powertrain friction losses
- [ ] Gear efficiency modeling

### Phase 5: Steering (1-2 days)
- [ ] Add Ackerman steering geometry
- [ ] Power steering assist
- [ ] Bump steer correction (optional)

### Phase 6: Advanced (Future)
- [ ] Multi-material beam system
- [ ] Real-time mesh remeshing on damage
- [ ] Detailed suspension kinematics
- [ ] Wind and weather effects

---

## 10. Testing Strategy

### Regression Testing
- Run full test suite after each phase
- Compare with baseline physics behavior
- Ensure no new test failures

### New Test Scenarios
- **Suspension**: Hard landings, bump stops, high-speed bumps
- **Damage**: Progressive damage from low to high speed impacts
- **Tires**: Slip angle testing, load sensitivity, tire self-collision
- **Drivetrain**: Engine torque curve validation, differential slip
- **Steering**: Ackerman geometry validation, power steering assist

### Real-World Comparison
- Compare vehicle handling with real-world data
- Validate suspension travel and rates
- Check damage progression against real crash test data

---

## Appendix: BeamNG Vehicle Data Extracted

### ETKi 2400i (Toyota Celica equivalent)
- **Front spring rate**: 24000 N/m (stock), 28000 N/m (sport), 70000 N/m (race)
- **Front damping**: 3400 N/(m/s) bump, 6300 N/(m/s) rebound
- **Front bump stop**: 36000 N/m progressive at 120mm travel
- **Front sway bar**: 1534 N/m (stock), 1848 N/m (sport), 30000 N/m (race)
- **Rear spring rate**: Similar to front, varies by setup
- **Steering lock**: 550 degrees (stock), 470 degrees (sport), 360 degrees (race)
- **Toe adjustment**: ±0.01 (1% variation)
- **Drag coefficient**: 0.93

### Suspension Geometry Notes
- Double wishbone front suspension (etki)
- Multi-link rear suspension (etki)
- Solid rear axle (trucks like Tacoma)
- Coil springs with monotube shocks
- Separate bump and rebound damping
- High-speed damping for large bumps

### Damage Thresholds
- Frame beams: `beamDeform: 41000, beamStrength: 450000` (very high)
- Body panels: `beamDeform: 15000, beamStrength: 310000` (medium)
- Suspension links: `beamDeform: 55000, beamStrength: 195000` (high)
- Rubber mounts: `beamDeform: 5000, beamStrength: 50000` (low)

---

*This document was generated by analyzing BeamNG.drive's Jbeam vehicle definition files and comparing them with CrumpleZone's current implementation. All parameter values are taken directly from BeamNG's vehicle files and should be validated against real-world data before implementation.*