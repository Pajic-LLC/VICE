# Physical rig bridge lab

The weighted mannequin is driven by eleven World rigid bodies and ten powered swing/twist joints. Animation is paused. Space disturbs the bodies; R toggles powered/passive behavior. The pelvis is suspended by a static body to isolate articulation and skinning from balance control. This is a mechanical validation fixture, not a walking or recovery controller.

Each body stores a physical_rig binding with root UUID, node and COM-to-bone offset. Body poses drive the visible skin through the shared World pose evaluator. No generated vertex or native solver handle is saved. Source IDs, offsets, targets and physical state survive checkpoint restore.
