# 0055: Final-state geometric assertions

Status: implemented for the R060.a bounded measurement subset.

`assert.measurement` is an explicit postcondition in the ordinary shared command
registry. It accepts a stable `body` ID, `space: "local" | "world"`, a metric,
expected value and explicit absolute tolerance. The metrics are `length`, `area`,
`volume`, `dimensions`, `minimum` and `maximum`. The first three expect a finite
number; bounds expect three finite numbers. Tolerance is finite, from zero to one,
in the metric's SI unit (m, m² or m³). Vector bounds compare each axis independently.
There is no implicit relative tolerance, unit conversion or inferred target.

Assertions are collected while executing the batch and evaluated against its
**final private candidate**, before publication. Their position among commands
never permits checking an obsolete intermediate shape. Failure rejects the whole
batch, including earlier geometry, metadata, allocator and resource changes. A
successful edit and its assertions remain one Undo task. An assertion-only batch
has no committed changes and rejects without creating history; read-only callers
use the existing `measure.entity` query.

Incremental transactions replay cumulative commands, so appending a later step
must still satisfy every earlier assertion in that draft. Failure preserves the
previous valid stage/version and leaves the live document unchanged. Sealed preview
and commit receipts retain the measured evidence. Assertions are not persistent
model constraints: a later independent transaction is free to edit the model and
should declare its own postconditions.

## Measurement meaning and validity

Assertions reuse the [entity measurement contract](0009-entity-measurements.md).
A whole body includes descendants, hidden and locked geometry. Local coordinates
remove the target body's complete world placement, including inherited scale;
world coordinates retain it. Area sums stored face areas and length sums unique
stored topology edges. Neither is a boolean exterior/union measurement.

Volume is available only for one validated material solid in one geometry record,
possibly beneath a selected group. Closed manifold, face orientation, self-contact,
intersection and shell-containment checks must complete. Inward cavity shells
subtract void volume. Multiple geometry records or independent material components
are not silently summed. Mirroring preserves positive material volume; affine
scale uses the absolute determinant. Unavailable/ambiguous/limited solid analysis
returns `MEASUREMENT_UNAVAILABLE`, including the solid status, even if the expected
value is zero. Empty bounds likewise cannot satisfy a bounds assertion.

The successful `assertions` receipt includes the explicit target/frame/metric,
expected value and tolerance, plus `actual`, `unit`, `passed: true`, and
`evaluation: "final_batch"`. Volume also reports `solidStatus`. These receipts
are available in full, change and created-ID batch results, staged previews and
durable transaction outcomes. They are evidence for that candidate, not a claim
about subsequent edits.

## Bounds and failures

A batch permits at most 32 assertions on 16 distinct bodies. The aggregate union
of target hierarchies is bounded to 256 body records, 20,000 vertices, 40,000 edges,
2,000 faces and 64,000 loop corners before measurement begins. Overlapping scopes
are charged once. Each target is measured once and reused across metrics/frames;
the existing solid-analysis work budgets remain authoritative. Global capabilities
publish these limits. Command/request/response and staging budgets still apply.

- `ASSERTION_FAILED`: measured finite value differs beyond the explicit tolerance.
- `MEASUREMENT_UNAVAILABLE`: the requested quantity cannot be established.
- `ASSERTION_TARGET`: target is absent from the final candidate or its ID overflows.
- `ASSERTION_LIMIT`: assertion count, target count or aggregate work bound is exceeded.
- Existing schema errors reject malformed types, fields, metrics, frames and tolerance.

Assertions must be in the outer batch, not nested inside `component.edit` or
`component.edit_instance`. Place an assertion on the materialized instance after
its scoped edit to avoid confusing canonical and world coordinate frames. The
native assistant advertises this routine command and documents its final-draft
semantics. This subset does not introduce persistent constraints, cross-document
fingerprints, certification checks or assertions on arbitrary script expressions.

`examples/asserted-solid.json` demonstrates an assertion before extrusion that
checks the final 2 × 3 × 1 m box, with 6 m³ volume, 22 m² area and exact bounds.
R060's assembly recipes reuse this contract; their independent fixture checks must
also verify unrelated record preservation and dimensions specific to each recipe.
