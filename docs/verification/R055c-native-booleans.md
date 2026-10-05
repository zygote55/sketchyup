# R055.c — Native solid Boolean controls

Date: 2026-10-05 UTC. Depends on R055.b; CI/dependency merges remain pending.
[Command and native interaction contract](../decisions/0046-solid-booleans.md).

The native fixture uses real click/Ctrl-click selection of faces on two solids,
Shift+B activation, Enter/click Apply, Escape cancellation and Alt-drag orbit.
Framebuffer inspection detects the amber preview, with document bytes unchanged.
The viewport names target/tool and retention choice. Draw actions change operation,
swap the subtraction order and toggle Keep originals, recomputing the preview.
Union produces 12 m³ from overlapping 2 m cubes; swapped subtraction produces
4 m³ on the independently expected side and explicitly consumes both operands.
Overlapping intersection produces 4 m³; disjoint union selects both separate
8 m³ results.

Cancellation retains source records, selection, allocator and history. Apply
selects generated bodies and publishes one history item. Native Undo/Redo and
save/reopen restore the result. Manual edits invalidate pending previews. Open
solids report operand body and boundary-edge IDs before mutation. Empty retained
intersections explain no change; an explicit consuming preview says both originals
will be removed, applies an empty selection and remains undoable. Shared component
union generates one solid per instance in a single history item, with the active
instance's scene result selected.

`boolean_input_tests` passes on isolated X11 and Wayland at DPR 1 and 2. The same
fixture passes ASan/UBSan/leak checks on Wayland at DPR 2. CI includes all four
native display runs. No live-provider quality trial is claimed by this UI slice.

The full development build succeeds; all 71 enabled CTest suites pass in 40.85 s
(72 entries, opt-in real Blender skipped). Adjacent native intersection, sweep,
offset, numeric input, tool lifecycle and responsive shell fixtures pass on X11.
A final cosmetic toolbar label uses “Boolean” to fit the narrow tool rail.

The isolated X11 recording fully decodes. Captures were visually inspected;
retained originals overlap the generated union, as the enabled retention option
specifies. Preview shows source-face provenance boundaries as native edges.

- [Preview](images/R055c-boolean-preview.png), SHA-256
  `af42f7196c5e6ba2ef32b6fe307ba0514560a171d26d3a402b6bf8b2b9d33205`.
- [Applied](images/R055c-boolean-applied.png), SHA-256
  `fef38863bd8d731e829d898de3e347cd1be7fd0a02a972414f130c176d87689b`.
- Local recording `build/evidence/r055c/native-boolean.mp4`, SHA-256
  `0b9d10b6edc02639ff281e4f5046992aa599d6865c3c4956a731a03ee02af509`.

Enclosed cavities remain an explicit adapter rejection pending R056 containment
work. Native controls do not broaden the kernel's documented numerical limits.
