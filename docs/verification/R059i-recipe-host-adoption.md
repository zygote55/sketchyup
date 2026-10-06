# R059.i — Explicit room recipe adoption

Date: 2026-10-06 UTC. Depends on R059.h in the review stack.
[Hosted-component contract](../decisions/0054-hosted-components.md#explicit-adoption-of-authored-rooms).

The shared `assembly.room.adopt_hosted` command and native **Edit → Adopt recipe
window attachments** action explicitly upgrade a validated version-one authored
room. Both windows acquire canonical cutting glue, an immutable uncut wall and
validated opening identity caches in one Undo task. Default room creation and old
file loading retain their existing relationship policy. Schema 15 already stores
the adopted records; there is no format migration.

The implementation reconstructs and compares the authored wall, frame and glass,
then remaps generated opening records to existing wall vertex/face identities.
Instance poses, current paint/materials and edge flags survive. Coordinates remain
within floating-point precision (the default fixture differs by at most about
1.11e-16 m); face-loop start/order can canonicalize for exact cache reconstruction.
An outside placement of either shared window definition rejects adoption, so the
command never changes the behavior of a component outside the chosen room.

Core and command fixtures cover:

- Private preview, agreement with commit, one Undo, Redo and repeated-adoption rejection.
- Stable wall/reveal vertex and face IDs, a painted real reveal with front/back
  material assignments, edge appearance and exact preservation of other body records.
- Exact native container save/reopen, including mirrored, rotated and nonuniformly
  scaled enclosing rooms with retained world-space window poses.
- Ordinary Move, Delete and Detach update the correct opening and restore only the
  released cut. The default cut wall has 9.888 m³ material volume; removing one
  window restores it to 10.128 m³.
- Instance-only resize from 1.2 to 1.4 m makes the chosen definition unique, retains
  the sibling and its definition, keeps both attachments and gives 9.848 m³ wall
  volume. Adopted openings regenerate once through the general hosted lifecycle;
  the legacy manual jamb edit remains in the legacy path.
- Unknown fields, wrong targets, moved windows, duplicate slots, locks, external
  shared placements and component-owned rooms reject atomically, including rollback
  of an earlier command in the same failed batch.

The native fixture exercises the actual menu action, indirectly locked wall/window
rejection, numeric Move, save/reopen, Undo and action availability. It passes X11
and Wayland at DPR 1 and 2, plus Wayland DPR 2 with ASan, UBSan and leak detection.
The full development suite passes **94/94 tests in 61.20 seconds**, including the
real Blender worker. The two targeted recipe sanitizer suites pass in 101.20 seconds. CI includes the
new core suite and all five native configurations.

The [actual native OpenGL capture](images/R059i-adopted-room.png) shows both windows
in the adopted room after moving the selected left window 200 mm along its wall.
SHA-256: `7975d301fcb637e5ba6ff05337772394182b492732ecf6c8950215de825245d8`.

The executable `examples/hosted-room-recipe-v1.json` uses typed result references to
create and adopt in one transaction, then widen one window in another transaction.
The command catalog and generated headless/MCP/transaction schemas expose the strict
new command. The adoption receipt reports ordinary glue expansion; adoption also
installs the reconstructed host through a validated core edit. This exception to
the original ordinary-command-only recipe contract is documented. An installed
CLI smoke run completes all 17 example steps, saves revision 2 with both attachments,
measures the widened window as 1.4 × 0.1 × 1 m and reopens the wall at 9.848 m³.
Installed example bytes and generated headless/MCP schemas match their source files.

R059 dependency merges and the separate R060/M6 acceptance gate remain pending.
