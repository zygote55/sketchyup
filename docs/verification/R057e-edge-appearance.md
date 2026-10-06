# R057.e — Persistent edge appearance and lineage

Date: 2026-10-06 UTC. Depends on R057.d. Local core, sanitizer and full application regression acceptance pass. CI and
dependency merges remain pending. Shared commands,
viewport/reveal controls and smooth normal consumption remain subsequent R057 work.
[Contract](../decisions/0051-edge-appearance.md).

The sparse body metadata stores independent hidden, soft and smooth flags using
native edge IDs. All-false removes an override. Flag edits retain the complete
surface, topology, triangulation and allocator state, report modified edge IDs,
and publish one Undo item. Existing edit scope, persistent visibility and locks
remain authoritative. Component instances use the same canonical metadata.

The full development build and **78/78 CTest tests** pass in **54.08 seconds**,
including the actual Blender worker. A fresh temporary installation creates and
reopens schema 13 through the installed CLI, advertises the required container
feature and includes the installed contract. Its owned artifacts are removed.

`edge_appearance_tests` passes normally and with ASan, UBSan and leak detection:

- Independent patches, no-change requests, prepared immutable snapshots, exact
  Undo/Redo and native-container round trips.
- Retired split-edge identity is pruned and both children inherit flags; raw face
  copies receive corresponding flags on fresh edge IDs. Face reversal retains them.
- A composed split followed by explicit clear retains the final cleared state
  when the prepared edit publishes. Published/composed edits carry resolved
  appearance metadata rather than rerunning inheritance over an earlier baseline.
- Deleted edges lose their records; partial grouping preserves moved edge flags.
- Cross-body consolidation rejects differing seam flags, including an unstyled
  source, without changing document bytes. Matching seams retain their flags.
- Coincident wire cleanup independently exercises conflicts against a retained
  styled edge and matching-style welding with explicit edge lineage.
- Shared component edits update reflected instances; canonical and scene records
  reopen together with their edge appearances intact.
- Invalid selections, missing IDs, missing flags, locked targets and wrong editing
  contexts reject. The codec rejects nonboolean, redundant all-false, unknown-field
  and missing-edge records. Version 12 migrates to empty appearance maps.

Native JSON is version 13; containers require `edge-appearance-v1` and `json-v13`.
Older schemas remain readable. Existing migration fixtures explicitly remove the
new field when constructing old-format documents. Snapshot/history byte accounting
includes the sparse records. CI includes the new suite in sanitizer acceptance.

No native display or live-provider acceptance is claimed for this data slice.
