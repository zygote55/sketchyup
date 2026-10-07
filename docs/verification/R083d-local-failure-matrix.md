# R083.d — Combined local failure-path verification

2026-10-07. Source includes R083.a–c (`168e22c`) and the CI queue-readiness
correction. [Exact build identities and timings](R083d-local-failure-matrix.json).
Normal **13/13 in 30.33 s**; ASan/UBSan **13/13 in 296.33 s**, leak detection and
halt-on-error enabled. This is local hardening evidence, not release acceptance.

| Area | Executed suites | Assertions covered |
| --- | --- | --- |
| Geometry and persistence sequences | geometry_fuzz, geometry_sequence | Bounded seeded edits, topology invariants, atomic rejection, preview/commit, save/reopen, undo/redo and backup |
| Parser/format boundaries | parser_fault_corpus, native_format | 5,120 deterministic corrupted inputs across ten formats; accepted native validation; public format/version/resource contracts |
| New-file publication | new_file_faults | Full/partial disk exhaustion, fsync/link faults, racing destinations, existing/symlink protection and killed writers |
| Native save/migration | persistence, m4_persistence | Format migration, source and backup preservation, corrupt container rejection, interrupted saves and disk faults |
| Recovery journal and CLI | recovery, recovery_cli | Recovery framing/corruption, acknowledged snapshots, interrupted writers and recoverable explicit output |
| Transactions | staging, outcome_store, transaction_coordinator, transaction_sequence | Private staging, stale revisions, durable receipts, corruption/truncation, interrupted publication, unknown-outcome reconciliation and replay protection |

A separate extended geometry run uses seeds **33536–33599**, **48 steps each**:
**3,072 attempts**, **1,895 accepted**, **803 rejected**, **374 no-ops**. Normal and
sanitizer reports are identical and pass. This supplements the standard geometry
corpus instead of replacing its seeds. Failures report replay seeds and operation
traces. No failure was discovered in these runs.

Build/load contention affects elapsed times; these are test durations, not application
performance measurements. Intentional process-kill tests model process interruption,
not physical power loss. Continuous coverage-guided fuzzing, release-candidate
revalidation, full platform/package gates and the final data-loss defect audit remain
open. Existing parser-specific tests retain traversal and expansion-limit coverage;
this matrix does not claim exhaustive parser, state-space or filesystem coverage.
