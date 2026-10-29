# Powered articulation lab

A gravity-loaded rigid link driven toward a persistent angular target. This is an actuator engineering test, not a balanced biped or finished character demo. World owns local joint reference frames, target, drive limits and body poses/velocities. Native constraints and contact filters are reconstructed from that truth.

Space applies an angular-velocity disturbance; E changes the angular target; R switches the motor off/on. Directly connected parts do not collide with each other; they still collide with the environment. Restore preserves the angular reference instead of treating the saved orientation as zero.

Use `vice project build articulation.viceproj` then run the staged executable. Runtime debug for `d7000000-0000-4000-8000-000000000003` exposes target/error/actual motor and limit impulses. The example intentionally uses primitive geometry. Multi-axis anatomical joints, rig coupling, balance, foot placement and recovery behaviors remain subsequent slices.
