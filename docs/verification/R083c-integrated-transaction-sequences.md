# R083.c — Integrated transaction failure sequences

2026-10-07. Integration `0ad4c96`; fixture `ed2ee2a`.
Parent: [PR #205](https://github.com/zygote55/sketchyup/pull/205).

The final seeded sequence gate passes **1/1 in 6.01 s**. All counters exactly match
the retained [normal/sanitizer results](R083c-transaction-sequences.json): eight seeds
with 32 steps each, **198 committed, 58 aborted, 60 reconciled and 32 restarts**.
The [dedicated ASan/UBSan run](R083c-local-transaction-sequences.md) passed in
**38.91 s**, with leak detection and halt-on-error.

Sequences combine commit, cancel and stale requests with five persistence fault
phases. They check pending retries, editing fences while post-replacement outcomes
are unknown, exact reconciliation, an independent translation oracle, untouched
neighbors, canonical native roundtrips, one-step undo/redo, duplicate commits and
late cancellation. Periodic save/reopen and process-service reconstruction retain
prior receipts and protect source bytes. Application code is unchanged.

[Installed checks](R083c-final-installed-smoke.json) confirm byte-identical desktop,
CLI and helpers, every installed catalog/contract file and ADR 0144. The
[source package](R083c-final-source-package.json) contains **203 byte-exact installed
inputs**, SHA-256 `c985c46fb7e5147f223a7d5971d5a1551c9f843db839891d826ff9ab49caad75`.
These bounded sequences supplement the full persistence/recovery/fuzz matrix; they
do not establish exhaustive failure coverage. Complete hardening, remote CI,
ordered prerequisite merges and R083/M9 acceptance remain open.
