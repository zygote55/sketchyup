# R058.b — Guarded diagnostic inspection

Date: 2026-10-06 UTC. Depends on R058.a in the review stack.
[Diagnostic contract](../decisions/0053-geometry-diagnostics.md) and
[inspection semantics](../decisions/0019-bounded-inspection.md).

`geometry.diagnose` is a read-only query over one explicit native body record.
It uses document identity, revision and typed hierarchy guards, includes hidden
geometry, and explicitly excludes child records. Volume and shell orientation are
local; mirrored/nonuniform placement does not falsely invert native geometry.

The new inspection fixture verifies:

- Independent unit-volume expectations in a transformed nested body; empty group
  records never claim to describe their descendants.
- Wrong document, revision, hierarchy, target kind and unexpected space fields
  reject without changing model bytes or history.
- Hidden and locked inverted geometry remains inspectable, with complete typed
  face references. Geometric repair eligibility does not grant edit permission.
- Reopening the native file gives identical findings.
- A retained snapshot preserves an open sheet's boundaries after live extrusion
  creates a closed cube. Live diagnosis sees the closed solid.
- Actual MCP dispatch over a privately saved fixture returns the same report.
- A 127-level hierarchy using large uint64 IDs triggers the aggregate 192 KiB
  reference budget, while every scalar category/count remains present. Truncation
  is explicit and disables repair eligibility; the full response stays under 256 KiB.

A simulated assistant invokes the query through its native session and receives
four open boundary edges without mutating the document. Five targeted suites pass
normally (1.24 s) and with ASan, UBSan and leak detection (9.59 s). Desktop inspection
and native MCP regression tests pass on X11 at DPR 1.

All seven affected published schemas match runtime discovery. A fresh temporary
installation reproduces that equality using the installed native and CLI binaries,
then creates, saves, reopens and diagnoses a four-edge open face without changing
its saved bytes. Temporary files and isolated preferences are removed afterward.

The full development build and **84/84 CTest tests** pass in **55.81 seconds**,
including the real Blender worker. Native report/repair controls and a new live
provider acceptance run are not claimed by this inspection slice.
