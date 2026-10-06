# Persistent associative dimensions and labels

R065.b, 2026-10-06. Document annotations are immutable distance or label records,
with independent stable IDs and unique names. A distance has two geometric anchors;
a label has one, Unicode text and an optional leader. Distance text is a prefix,
not a replacement for the measured value. Measurement uses world metres; later
native formatting uses the document's display units. Offsets are world-space
vectors, and text size uses logical pixels independently of model/display scale.
Records also retain RGB color. Tables are bounded to 4096 records, names to 1024
bytes and annotation text to 4096 bytes. The anchor contract is decision 0078.

Ordinary geometry edits remap attached anchors before atomic publication. Generated
annotation changes participate in the same history entry, memory budget, saved
state, prepared snapshot and Undo/Redo as geometry. Shared component scopes cannot
author document annotations; resulting scene geometry edits update scene references.
New/rebound attachments must resolve; existing broken labels remain editable.
Compound snapshots carry already-resolved annotations so publication cannot apply
lineage twice. Deletion stores missing state and the last resolved world position.
Measurements with missing/ambiguous anchors have no numeric value. Display code
must show an explicit broken marker, never a stale dimension presented as current.

Schema 21 adds `annotations` and `nextAnnotationId`, `json-v21`, a matching allocator
floor and the required `annotations-v1` feature. Readers of versions 1–20 migrate to
empty annotation tables without changing existing geometry, sections or saved scenes.
Actual prior-writer bytes are retained as a migration fixture. Older readers reject
schema 21 instead of dropping data. Strict decoding rejects unknown/missing fields,
noncanonical IDs, incorrect kinds/anchor counts, invalid support coordinates,
nonfinite values and malformed colors/text sizes. Missing geometry references are
retained and diagnosed; loading cannot silently bind them to another entity.

Surface-only GLB/Blender output omits annotation graphics and reports their count
as `annotationsOmitted`; the native result panel displays this limitation. This
layer supplies persistent records and evaluation, not dimension/label authoring UI.
Shared commands and native workflows are subsequent R065 work.
