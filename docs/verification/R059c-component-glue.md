# R059.c — Canonical component glue records

Date: 2026-10-06 UTC. Depends on R059.b in the review stack.
[Hosted component contract](../decisions/0054-hosted-components.md).

`component_glue_tests` verifies:

- Explicit canonical member/face references produce a physical alignment frame and
  the selected face's outer cut outline. A window ring can anchor at its empty center;
  alignment-only behavior produces no cut outline.
- Configuration stages privately, commits one Undo entry, retains immutable snapshots,
  preserves placed scene records and treats identical configuration as a no-op.
- Shared member edits retain the reference and update its outline while preserving
  independent placements. Removing the referenced face rejects the complete edit.
- Make Unique copies behavior and isolates later settings. Changing component axes
  preserves world anchor and outline. Locked placements protect shared behavior.
- Reflected, nonuniform and sheared canonical member frames use independently expected
  physical normals, tangents and profile points. Missing members/faces, invalid planes,
  nonfinite values, nested-reference targets and cyclic paths reject.

`component_glue_io_tests` verifies exact JSON/container round trips with mirrored
placements, schema 14 and required feature pairing, strict malformed-record rejection,
schema/container 13 migration without invented behavior, immutable save snapshots and
verified recovery. A durable outcome whose only change is glue metadata reconstructs
the exact committed state and one Undo entry; Undo restores the prior absent behavior.

All four targeted sanitizer suites pass (glue core, glue I/O, components and component
records; 1.12 seconds), with address/undefined-behavior checks and leak detection.
All 88 development CTest suites pass (49.30 seconds), including real Blender. Native
component workflows pass X11 at DPR 1 and Wayland at DPR 2, covering shared/mirrored
edits, numeric amendment, Make Unique, axes and persistence. A fresh installed CLI
accepts, saves and reopens an explicit schema-14 glue reference; the updated contract
is installed. CI includes core discovery and explicit glue I/O sanitizer coverage. Persistent host attachments, regenerated
openings and native/automation placement controls remain subsequent work.
