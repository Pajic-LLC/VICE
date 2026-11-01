# Physical rig bridge lab

The weighted mannequin is driven by eleven World rigid bodies and ten powered swing/twist joints. Animation is paused. Space disturbs the bodies; R toggles powered/passive behavior. The pelvis is suspended by a static body to isolate articulation and skinning from balance control. This is a mechanical validation fixture, not a walking or recovery controller.

Each body stores a physical_rig binding with root UUID, node and COM-to-bone offset. Body poses drive the visible skin through the shared World pose evaluator. No generated vertex or native solver handle is saved. Source IDs, offsets, targets and physical state survive checkpoint restore.

The visible lab is near the render origin. CPU World-to-rig and physics tests exercise 1e12 coordinates; the legacy absolute-float raster path has a separate far-coordinate precision limitation.

The lab uses deliberately stiff 18 Hz pose springs with bounded torque to make target tracking easy to inspect; it is not a tuned human muscle model.
