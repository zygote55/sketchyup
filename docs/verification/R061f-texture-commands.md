# R061.f — Scoped texture authoring commands

Verified 2026-10-06 on Arch Linux with Qt 6.11.2.
[Command contract](../decisions/0066-texture-authoring-commands.md).
Native mapping controls remain pending; this layer does not close M7.

`material.map_texture` accepts explicit planar, three-pin and affine projections,
or null to restore implicit mapping. The dedicated suite checks independent
front/back assignments, signed repeats, rotation, immutable preview, face-change
receipts, stale revisions, no-change rejection, one-item Undo/Redo and exact native
container round trips. Material and image identities remain independent.

There are 147 independent analytic UV samples across a reflected/sheared group
and two shared component placements. Scoped commands preserve the initiating
instance's world frame. Direct shared-member writes and out-of-scope targets
reject; an instance-only edit becomes unique and Undo restores sharing. Twenty-seven
malformed batches each follow an earlier valid mapping command and leave the
original container and history unchanged.

Legacy material sampling and bounded face inspection agree on stored/effective
local mappings. Native assistant staging checks object/null projection schemas,
private preview, editor locks before preview and after preview, and idempotent
commit. These are offline transaction checks; no provider credentials or live
provider request are needed for this layer.

The six-command packaged example includes its own two-by-two RGBA PNG and assigns
independent front/back projections. The test checks decoded alpha, analytic UVs,
resource counts and exact save/reopen. Its exported GLB passes Khronos validation
with zero errors and zero warnings ([report](R061f-glb-validation.json)).

Published schemas additionally pass independent Draft 2020-12 validation using
jsonschema 4.25.1: four valid projection forms and eleven malformed requests for
each of the three catalogs, totaling 12 accepted and 33 rejected cases
([report](R061f-schema-validation.json)). The internal validator now explicitly
checks the null type rather than letting arbitrary values match a null branch.

The installed CLI runs the installed example with display variables removed.
Moving the resulting model and reopening/exporting it produces the exact same
GLB as the development CLI exported before relocation. The image is embedded,
and installed schemas, decision and desktop binary match the tested build
([report](R061f-installed-smoke.json)). Independently created documents have
different identities; this byte comparison follows the same saved document.

Validation:

- The complete development CTest run passes all 106 suites in 104.45 seconds,
  including real Blender.
- All seven targeted ASan/UBSan suites pass with leak detection and halt-on-error:
  texture commands, texture export, native assistant session, transaction dispatch,
  inspection session, inspection and commands. The first six passed in the initial
  run; the commands suite passed in 77.63 seconds after adding its required catalog
  execution case. The initial failure was the exhaustive catalog coverage guard.
- Native material input, assistant preview and textured viewport pass on isolated
  Wayland at scale 2. These retain the existing native display/selection behavior;
  the authoring UI is the next layer.
- The development and sanitizer builds report no compiler warnings; whitespace
  validation passes.

Reproduce with `texture_command_tests`, `command_tests`, the installed
`examples/texture-mapping.json`, and the existing GLB validator. CI includes the
new suite in normal/sanitizer runs and validates the example's GLB.
