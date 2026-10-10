# Instanced component storage

Date: 2026-10-10. R082.cc. Persistence-only slice; in-memory instancing follows.

[Component records](0007-component-records.md) keep one canonical definition per
shared component and resolve every placement into scene records. Until now native
files also stored each resolved member body, so a model's size grew with its
placement count even though those bodies are exact projections. At 10,000
placements of the benchmark definition the model JSON reached 69.7 MB, above the
32 MiB document-chunk bound that [real-model fixtures](0141-real-model-performance-fixtures.md)
must fit.

Native model JSON advances from schema 24 to 25. A v25 file no longer stores the
member bodies of a component placement. The `bodies` array keeps every record that
is not a placement member, including each top-level placement root with its
instance-owned state: identity, parent, transform, name, properties, hidden,
locked and tag. Definition-owned state (geometry, topology, curves, guides,
materials, colors, texture mappings, edge appearance, editable text, reference
images) already lives in the definition and is not repeated. A nested placement
root is a member of its parent placement, so its state comes from the parent
definition, as it does when editing.

Instance records keep `root`, `definition` and the explicit canonical-to-scene
`members` map, so every member identity is stored rather than derived. They add a
required `floors` object keyed by scene member ID. A scene member's allocator
floors may exceed its definition's floors after definition edits are undone; each
such member stores `[nextId, nextEdgeId]` as canonical decimal strings. Members at
their definition floor write nothing. A stored floor that is not above the
projected floor, names a non-member or names the placement root is rejected, so
each document has one canonical encoding. Document `nextId`, retired identities
and every other allocator floor are unchanged.

Loading rebuilds members through the same projection the editor uses for
placement and definition edits. Definitions are validated first, so reference
cycles and dangling definitions reject before expansion. Each placement must bind
every definition member, and nested bindings must name the referenced definition;
projection never allocates identities. The resolved size is checked against the
document editing limits before records are built. A projected member that is
also stored in `bodies`, or that collides with another record, is rejected. The
complete document then passes the ordinary restore validation, so a v25 document
reopens with the same identities, geometry, allocator floors, revision and clean
history baseline that schema 24 produced.

Readers retain schemas 1–24. Schemas 8–24 store expanded members; before
restoring, each top-level placement is projected and every stored member and
placement root is compared with its projection by the canonical rule: exact
equality, except that a stored allocator floor may exceed (never fall below) the
projected floor. A mismatch rejects with a short diagnostic naming the scene
member, its component instance and that instance's definition. Geometry is never
dropped or rewritten to make a file load. Make-unique placements already become
separate definitions, so valid files do not contain mismatches; the check guards
against corrupted or hand-edited files. As with every failed migration, rejection
writes nothing and the source file remains byte-for-byte unchanged.

Container v2 is otherwise unchanged. Its document chunk uses `json-v25` and the
required features add `instanced-placements-v1`, so an older reader rejects the
file instead of opening placements without their members. The manifest's
allocator floors still list every resolved scene record. Immutable save snapshots,
recovery checkpoints, migration and inspection all use the same encoder and
decoder.

This layer changes storage only. Placements are still expanded into full scene
records in memory; the viewport, inference, construction and document limits are
unchanged. Shared in-memory storage, viewport instancing, instanced inference,
placement construction and the public limits follow in R082.dd–gg.
