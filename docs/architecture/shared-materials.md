# Shared materials

[ADR 0021](../adr/0021-shared-world-materials.md) defines the persistent catalog,
assignment, precedence, mass, rendering, SDK and historical-provenance contracts.

Use `World::set_material` to define a shared material and `set_entity_material` to
assign its UUID. `material_surface` resolves current physical values, optional
visual resource, box volume and predicted dynamic mass. The Runtime physics service
also reports actual native mass. A nil assignment selects legacy inline box data.

For reviewable file edits:

```
vice-world-edit 1
revision sha256:CURRENT_SOURCE_DIGEST
set_material MATERIAL_UUID "Rubber" 1100 0.9 0.8 RENDER_RESOURCE_UUID
set_entity_material ENTITY_UUID MATERIAL_UUID
```

`clear_entity_material ENTITY_UUID` restores legacy behavior.
`remove_material MATERIAL_UUID` succeeds only when the complete candidate has no
remaining entity users. Render resources must be registered in the World. Use the
existing `vice world edit SOURCE REQUEST --dry-run` and `--apply` commands to preview
all affected users/regions and publish atomically with source revision checks.

Rendering resolves the shared visual binding while snapshots preserve explicit
resource references. Conflicting shared and explicit render references are rejected.
The shared material UUID is the durable identity; shader resources and domain-local
material numbers are implementation bindings. Material changes require physical
reset. Checkpoints preserve definitions, assignments and original material UUIDs in
measured break events, without claiming to preserve earlier mutable profile values.
