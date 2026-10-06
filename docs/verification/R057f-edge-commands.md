# R057.f — Edge appearance commands and inspection

Date: 2026-10-06 UTC. Depends on R057.e. Shared commands and inspection pass
local acceptance. Native edge controls, explicit reveal display and smooth normal
consumption remain subsequent R057 work. [Contract](../decisions/0051-edge-appearance.md).

`geometry.edge_appearance` publishes independent hidden, soft and smooth flags
through the shared catalog, CLI/session/MCP schemas and native assistant policy.
The request requires a context, 1–4,096 typed edge references and at least one
boolean flag. Omitted flags retain their value. Metadata edits leave geometry,
topology and tessellation unchanged and report modified edge IDs.

The full development build and **79/79 CTest tests** pass in **66.46 seconds**,
including the actual Blender worker. CI includes edge commands in sanitizer
acceptance; dependency CI and merges remain pending.

`edge_command_tests` passes normally and with ASan, UBSan and leak detection:

- Immutable preview predicts the exact committed change map and one Undo item.
  Undo/Redo restores the exact before/after body, and container reopen retains flags.
- Edge detail and paged topology inspection expose identical independent flags.
- Missing flags/context, duplicate or missing references, unknown fields, nonboolean
  values, stale revisions, locked targets and a late invalid command reject atomically.
- A real split-then-clear command batch preserves cleared descendant flags through
  final publication. A real unify-then-merge-then-clear batch also keeps its final
  metadata; Undo restores the distinct source appearances and source bodies.
- Component-scoped edits project to reflected instances; Make Unique protects the
  other instance's records. Scoped changes survive native persistence.

The all-published-command suite exercises the new catalog entry alongside every
existing command. The three published transport schemas are regenerated from the
runtime catalog. The installed example names an explicit native edge and sets
soft/smooth independently of hidden.

The native assistant test passes on X11 at DPR 1. Its simulated provider proposes
face creation and an edge-smoothing flag in the same transaction; native preview,
Apply and one-step Undo pass, alongside direct mode, staleness, consent, responsive
layouts and uncertain-outcome reconciliation. This verifies local policy and
publication without using account credentials or a live provider.

A fresh temporary installation runs the installed example, reopens its saved
native container and matches all three published schemas against the installed
CLI's runtime output. The temporary installation removes its owned artifacts.

No native edge-display or live-provider acceptance is claimed for this slice.
