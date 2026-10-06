# 0051 — Edge appearance is persistent metadata

Status: R057.e data/lineage and R057.f shared command acceptance pass locally.
Shared smooth-normal consumption is implemented in R057.g
([shading contract](0052-smooth-shading.md)). Native controls and explicit hidden-edge
display are subsequent R057 work.

Each native edge may have independent `hidden`, `soft` and `smooth` boolean flags.
All false is the implicit default and is omitted from the sparse body map. Hidden
and soft edges are intended to omit their ordinary stroke; smooth joins shading
across the edge. None changes vertices, loops, incidence, tessellation or solid
validity. Keeping the flags independent permits a visible smooth seam or a hidden
hard seam. Rendering and selection must use an explicit reveal mode to expose
hidden/soft edges, while locks and editing context continue to apply.

`setEdgeAppearance` requires 1–4,096 typed edges across at most 128 bodies and at
least one explicit flag. Omitted flags retain their value. Targets must belong to
the requested context and visible, unlocked bodies; explicit edge IDs can clear
an edge's own hidden flag. One atomic edit reports modified edge IDs, supports
prepared preview and one Undo item, and removes overrides when all flags clear.

## Identity and lineage

Surviving edge IDs retain their flags. Split descendants inherit the source flags;
raw copies transfer to fresh IDs, whole-body/component copies retain scoped IDs,
and partial grouping prunes unrelated records. Deleted edge records lose their
appearance. Welding multiple sources with differing flags rejects before
publication, including a default source and a nondefault source. Matching flags
merge without picking an arbitrary source by map order.

Document edits resolve appearance after authoritative topology and lineage have
been validated. Published changes record that their appearance is already
resolved. Composed batches, component projections, copy arrays and restored
transaction checkpoints similarly carry authoritative metadata from their
validated snapshots. Reapplying a prepared edit therefore cannot recreate flags
cleared by a later operation in that transaction. All paths still validate edge
references and canonical nondefault entries. Appearance-only changes identify
modified edges even when topology itself is unchanged.

New geometry with no source edge identity or declared/inferred edge lineage uses
default flags. This includes replacement Boolean/sweep output; visual continuity
is not guessed from unrelated nearby edges. The data contract does not silently
claim that every newly generated edge inherits a source stroke style.

## Persistence and bounds

Native JSON schema 13 adds required body `edgeAppearances` objects, keyed by
canonical edge-ID strings, with exactly three boolean fields. Canonical component
members use the same encoding. Missing references, unknown fields, nonboolean
flags and redundant all-false records reject. The container advertises required
`edge-appearance-v1` and `json-v13`; older readers must reject rather than discard
these semantics. Versions 1–12 migrate with empty maps and keep their old display
behavior. Existing allocator, history, snapshot, model and container bounds apply;
record count cannot exceed the authoritative edge limit. Metadata memory is
included in document snapshot/history accounting.

## Commands and inspection

`geometry.edge_appearance` requires an explicit `context` and an `entities` array
of typed `{body, edge}` IDs, plus at least one of `hidden`, `soft` or `smooth`.
Every supplied flag is strictly boolean. Missing flags retain their value; explicit
false clears that property. Duplicate references, unknown fields and missing IDs
reject the entire batch. The existing component-scope wrapper projects canonical
edits to all instances; Make Unique isolates future changes.

The command participates in the same sealed preview, revision checks, atomic
batch publication and Undo contract as geometry commands. Split-then-clear and
unify-then-merge-then-clear batches retain their final explicit flags. The published
transaction, headless-session and MCP schemas include the same command, and the
native assistant allowlist permits proposing it. `entity.describe` returns an
edge's `edgeAppearance`; paged `topology.query` edge rows include `appearance`.
Neither inspection path infers flags from geometry or coordinates.
