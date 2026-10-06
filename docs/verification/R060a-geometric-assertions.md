# R060.a — Final-state measurement assertions

Date: 2026-10-06 UTC. Depends on R059.i in the review stack.
[Assertion contract](../decisions/0055-geometric-assertions.md).

`assert.measurement` evaluates dimensions, minimum/maximum bounds, stored edge
length, stored face area or validated solid volume in an explicit local/world
frame. Assertions run on the final private candidate before publication, regardless
of command ordering. A mismatch or unavailable quantity rejects the entire edit.
Incremental transaction appends must retain every previous assertion in that draft;
a failed append keeps the previous valid stage available for preview and commit.
Assertions do not become persistent model constraints after publication.

The dedicated suite verifies:

- An assertion before extrusion measures the final 2 × 3 × 1 m box: 6 m³ volume,
  22 m² area, 24 m edge length and independently expected bounds. Private preview
  agrees with commit, successful assertions share one Undo, and save/reopen is exact.
- An assertion that matches only an intermediate face rejects after extrusion,
  preserving document bytes, IDs and history. Assertion-only batches create no history.
- Reflected/nonuniform hierarchical transforms give independent local/world bounds,
  area and volume. An enclosed inward cavity subtracts from material volume; the
  7 m³ result remains valid near the coordinate limit.
- Open and multiple-record volume targets cannot pass with an expected zero.
  Missing/deleted targets, wrong scalar/vector shapes and invalid tolerance reject.
- More than 32 assertions, 16 distinct targets or 256 aggregate hierarchy records
  reject before measuring. Additional vertex/edge/face/loop-corner caps are published.
- A later transaction step that scales an asserted solid fails atomically. The
  preceding stage still previews/commits with its measured assertion evidence.

All **95/95 development CTest suites** pass in **60.53 seconds**, including the real
Blender worker, command catalog, generated schemas, transaction and assistant suites.
The new assertion suite passes ASan, UBSan and leak detection (4.01 seconds).
The native assistant suite passes Wayland DPR 2, including advertisement of the
new routine verification command with the existing permission gates intact.

An installed CLI smoke run executes the shipped `asserted-solid.json`, verifies
all three numeric receipts, saves and reopens byte-for-byte, and confirms that a
failing assertion after a paint edit neither alters its input nor writes an output.
Installed example bytes, headless/MCP schemas and capability limits match the source.
CI runs the new sanitizer suite and executable example in addition to its full tests.

No schema-version change or new persistent constraint record is introduced. Roof,
stairs, furniture, site placement and the integrated M6 acceptance gate remain work
for subsequent R060 slices; this layer does not close R060 or M6.
