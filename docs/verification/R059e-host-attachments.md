# R059.e — Persistent hosted component lifecycle

Date: 2026-10-06 UTC. Depends on R059.d in the review stack.
[Hosted component contract](../decisions/0054-hosted-components.md).

The authoritative Document path now stores immutable attachment frames and one
original surface per host. It expands component/host changes privately and validates
pose, canonical profile and cached native opening identities before publication.

`hosted_components_tests` checks:

- Face-aligned placement and explicit inset stage privately and publish placement,
  relationship and native opening as one history entry. Undo restores the original
  surface; Redo restores the cut and binding. Snapshot admission includes baselines.
- Numeric replacement keeps one Undo entry. In-plane component movement preserves
  opening corner/reveal identities, painted reveals and an unrelated neighboring
  opening. Off-plane/outside/overlapping moves and independent host geometry changes
  reject with the document stamp, revision and history bytes unchanged.
- Shared-definition resizing keeps each stored glue anchor and preserves native
  opening correspondence. Make Unique permits a peer to become alignment-only.
  Rehosting restores the previous region and cuts the new host atomically.
- Selection deletion removes a component and restores its opening. Deleting the host
  releases surviving placements without moving them. Clearing glue releases only
  its definition's attached placements. Bake retains geometry and permits subsequent
  raw host editing; Undo restores the complete relationship.
- Reflected, sheared and nonuniform ordinary parent groups preserve attachment
  coordinates under common-ancestor movement. Host-only movement carries the
  component. Locks protect both objects, including metadata-only detach and bake.
- Binding an already aligned component can change metadata alone. Composed snapshot
  edits preserve it; falsely marked resolved edits still fail coherence validation.
  Retained snapshots and mutable caller aliases cannot alter published records.
- Definition axes, gluing/non-gluing replacement, deleted source-face rejection and
  rejection of a component-owned host use the same authoritative checks.

`hosted_components_io_tests` checks exact raw/container round trips and subsequent
movement after reopening, for both cutting and alignment-only attachments. Malformed
host/face/instance IDs, missing or unknown fields, bad frame/inset values, duplicate
records, oversized host arrays, baseline drift, extra material state and invalid
source-corner/reveal maps reject. Schema 14 migration retains existing geometry and
canonical glue without inventing relationships; feature/encoding mismatches reject.
Captured saves and recovery snapshots remain immutable. Durable recovery reconstructs
one complete Undo, including a binding that changed no body geometry.

Targeted development tests pass (attachment lifecycle, hosted persistence and glue
persistence: 3/3, 0.72 s). The final lifecycle and persistence suites, including
additional axes/replacement checks, pass ASan, UBSan and leak detection (2/2,
6.69 s). All 91 development CTest suites pass (50.72 s), including the real
Blender worker. Existing native component interactions pass X11 at DPR 1 and
Wayland at DPR 2. A fresh temporary installation loads schema 15 alignment records,
moves an attached component through the shared CLI batch path, preserves its host
baseline and saves/reopens identical container bytes. Installed contract presence
is checked and all temporary files are removed. Additional stale-proposal and
unrelated-attachment amendment cases pass the final development and sanitizer
lifecycle reruns.

Core APIs and persistence are the scope of this layer. Shared commands, native
placement controls, copy/array attachment behavior and adoption of existing recipe
host relationships remain separate R059 work. Existing unbound copies do not acquire
a host implicitly. M6 acceptance remains pending.
