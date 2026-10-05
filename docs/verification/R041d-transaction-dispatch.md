# R041.d incremental transaction dispatch

Date: 2026-10-05 UTC. Local verification passed; CI acceptance pending. Requires
[PR #62](https://github.com/zygote55/sketchyup/pull/62).
[Dispatch contract](../decisions/0025-transaction-dispatch.md).

The new dispatcher fixture begins an empty draft, creates a face, inspects its
provisional ID, then appends a move and typed semantic properties. Live bytes
remain unchanged until commit, which produces one revision and the original
assistant task/undo entry. Repeating an append returns its cached receipt;
conflicting payloads and stale stage versions reject. Failure while addressing a
missing entity preserves the previous proposal and reports a structured error.
Repeated sealing returns the same durable identity. New appends after sealing
reject. Save without content changes preserves draft validity.

Lost commit responses, late cancellation, draft release and explicit restart
recovery retain the original committed receipt without duplicate edits. User undo
remains separate from the recorded outcome. The actual M4 room fixture verifies
edit-then-undo invalidation and preservation of a later human edit. Cleanup retains
the authoritative `STALE_REVISION` cause. Empty/unknown drafts, unknown durable
identities, unsealed and sealed aborts, wrong threads/documents/API versions,
unsupported operations, unknown parameters, malformed property values, oversized
requests/command arrays, draft counts and owned-resource limits are covered.

Monotonic expiry rejects old drafts, does not reset on apply, and cannot be
extended by sealing. A fault hook advances time during durable intent issuance;
the expired accepted proposal is retired without live publication. Another fault
throws after checkpoint rename. The dispatcher reports unknown, blocks dependent
work, rejects reentry and reconciles the verified candidate exactly once.

All 52 development CTest suites and seven relevant ASan/UBSan suites pass:
dispatcher, coordinator, outcome store, staging, retained inspection, bounded
inspection and command regressions. The final stale-cause adjustment passes the
dispatcher suite normally and under sanitizers. The unchanged core matrix passed
in R041.a. No compiler warnings were reported.

The published transaction schema exactly matches live discovery. A local install
compares installed `sketchyup-cli --capabilities` transaction metadata with the
installed JSON artifact. The source archive includes the registry, schemas,
contracts and fixture. Disposable package CI now checks schema equality after
installation and removal of the artifacts during uninstall. Native modeling
semantics and desktop actions are unchanged; this slice introduces no transport
or provider integration.
