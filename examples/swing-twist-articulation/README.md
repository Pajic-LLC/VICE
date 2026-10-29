# Multi-axis articulation lab

A generic rigid link is held against gravity by a swing/twist constraint at 1e12 World coordinates. This is a solver validation fixture, not character behavior.

Space applies a three-axis angular disturbance. E inverts the quaternion target. R toggles both motors between powered and passive behavior. The cone is circular with independent twist limits. Swing torque caps apply separately on the two swing axes.

`vice project build articulation.viceproj` cooks the persistent World and stages the game module. `scripts/validate_swing_twist_workflow.py` checks transactional authoring, SDK staging, and optionally native Runtime checkpoint reconstruction.
