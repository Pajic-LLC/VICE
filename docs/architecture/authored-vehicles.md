# Authored vehicles

A dynamic, solid rigid box can carry a four-wheel ray-suspension vehicle:

```text
entity a1000000-0000-4000-8000-000000000002 spatial 0 0.8 0 1 rigid_box dynamic 0.85 0.25 1.8 0 0 0 0 0 0 0 0 continuous solid vehicle 2.5 1.5 0 0.32 0.5 30000 3500 6000 12000 4000 0.5 1 0 0 0 0
```

Vehicle fields are wheelbase, track, mount height, wheel radius, suspension travel,
spring N/m, damping Ns/m, total drive force N, total brake force N, per-wheel lateral
stiffness Ns/m, maximum steer radians, tire friction, throttle [-1,1], steering
[-1,1], brake [0,1], and travelled meters. Positive steering turns right; negative
throttle reverses. Y is up and +Z is forward in the physical chassis frame.

Choose the chassis density/dimensions through shared World materials. Scale affects
chassis collider size/mass, while wheel geometry remains in physical meters.
The default spring profile is a starting point for a roughly 1,200 kg chassis;
light/heavy bodies need appropriate tuning. At rest on flat ground, the ground lies approximately
radius + travel - mass * gravity / (4 * spring) below the suspension mounts.
The ray model has no wheel rotational inertia, drivetrain, rolling resistance,
damage, fuel, weather generation, audio or wheel art.

Use `set_vehicle <entity> <same values>` or `clear_vehicle <entity>` in a revision-
checked `.viceedit`. The complete candidate must include a dynamic solid chassis.
Profile/geometry/pose/material edits require physical reset. Use SDK
`Context::control_vehicle(id, throttle, steering, brake)` for live input and
`Context::read_vehicle(id, snapshot)` for speed, distance and contact forces.
Commands are saved immediately in World; SDK frame callbacks are read-only.

Road material friction limits the combined tyre forces. A zero-grip road supports
the car but cannot accelerate or brake it through its tyres. Dynamic supports
receive opposite impulses. Steering produces physical forces at the front wheels.
Full/reduced regions currently share this model; inactive regions skip physical
work. Existing body collision, CCD and distance constraints remain active.

Open `examples/vehicle-playground/vehicles.viceproj`. Three chassis compare dry,
slippery and zero-grip roads. The automatic tour brakes moving cars after 20m.
Press 1/2/3 to choose a car, arrows to drive and steer, and Space to brake. Preview
uses the same SDK module and shared authored simulation as standalone Runtime.
The Inspector and physics overlay show suspension support/load. Runtime debug
shows the contacted entity/material UUIDs and applied modeled tyre force.

```sh
python3 scripts/validate_vehicle_workflow.py --vice <executable>
python3 scripts/validate_vehicle_workflow.py --vice <executable> --run-runtime
```

Live World checkpoints retain chassis pose/velocity, vehicle profile, normalized
inputs, distance and existing causal history. Native suspension/contact caches
are rebuilt on stepping. Module globals and device input are not checkpointed.

See ADR 0022 for persistent format 13, validation, force laws, costs and limitations.
