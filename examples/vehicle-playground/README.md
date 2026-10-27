# Vehicle Playground

Build with `vice project build examples/vehicle-playground/vehicles.viceproj` or open
the project in Editor and Play. The three primitive chassis demonstrate identical
vehicle profiles on dry (left), slippery (center) and zero-grip (right) roads.
The authored throttle starts an automatic comparison; moving cars brake after 20m.

Press 1/2/3 to select a chassis; arrows drive/reverse/steer and Space brakes.
Click/focus the game view for Editor input. Focus loss brakes the selected car.
The camera is fixed. These are physical prototype chassis, without wheel art,
animation, audio, a drivetrain or vehicle damage. The zero-grip car cannot propel
itself. Stopping and saving preserves velocity, input and travelled distance in World.

Inspect a running chassis using `vice runtime debug <session> <entity UUID>`.
Chassis IDs end in 2/3/4. Debug exposes each suspension contact, road material,
normal load and friction-limited tyre force. Contact observations describe the last
force evaluation; they are reconstructed by stepping after restart.

Run `python3 scripts/validate_vehicle_workflow.py --vice <vice executable>`.
Add `--run-runtime` for native acceleration, braking, checkpoint and restart acceptance.
