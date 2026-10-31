# Durable request outcomes

R041.b adds an exclusive in-process `OutcomeStore` for one document in epoch 1.
It is a storage primitive for the following transaction coordinator, not a public
commit endpoint. It never executes commands or publishes a live model.

## Atomic checkpoint specialization

This specializes ADR 0005's co-recorded commit-frame ordering with a single
checksummed binary outcome checkpoint, atomically replaced. The checkpoint holds
the retained outcome table, request-ID high-water mark, and latest committed
before/after native containers. Candidate bytes and their terminal result are
one durability unit. There is no separate outcome write that can acknowledge a
model change absent from recovery, and no executable command replay.

The tradeoff is rewriting the checkpoint on each transition. The initial limit
is 64 MiB including both native containers and outcome metadata, so a large model
can exceed this transaction subset even if ordinary local editing accepts it.
This conservative implementation favors a verifiable complete old/new checkpoint
over append-log compaction. Append storage can replace it later with equivalent
fault evidence. [Initial measurements](../verification/R041b-durable-outcomes.md)
record the rewrite cost for empty, room, 1000-face and 8 MiB asset fixtures.
The limit is a storage limit, not a process RSS ceiling. D-086 raised the 64 MiB
limit to the budget in the implementation notes below.

The format is eight-byte `SKUPOUT1`, little-endian uint32 metadata byte count,
32-byte SHA-256 over the remaining payload, compact JSON metadata, then raw
before/after native containers. Metadata declares document, epoch, version,
lengths, per-container hashes, next request ID, outcomes and latest commit ID.
Readers check bounds, exact lengths, checksums, fields, identity, revisions,
timestamps and native container validity before accepting state. Unknown formats,
truncation and corruption preserve the file and fail closed. A missing checkpoint
in an existing store directory cannot silently reset request identities.

Directories are private, created and synchronized before use. The writer holds a
process lock; competing actors fail. Files use owner read/write permissions.
Checkpoint replacement uses a sibling temporary file without direct-write
fallback: write/flush, fsync file, atomic rename, then fsync parent directory.
Success is acknowledged only afterward. Failure before replacement leaves the
old checkpoint authoritative. Failure during or after replacement marks the store
uncertain and rejects dependent operations, including cancellation, until explicit
reconciliation rereads a complete valid checkpoint and synchronizes file/directory.
Opening after process restart performs the same read/validation/synchronization.

## Identity and outcomes

`begin` issues a durable monotonically increasing decimal request ID, scoped to
(document ID, epoch), bound to a SHA-256 normalized-payload hash and exact native
base fingerprint/revision. Request IDs are server-issued; an unknown or expired
retry cannot create new work. A lost begin response cannot hide an edit because
commit requires the issued identity. The later transport supplies authentication
and authorization on every attempt, including retries.

States are `pending`, `committed`, `aborted`, and explicit `unknown` with
reconciliation required for absent identities. A reused identity with a different
hash fails with `REQUEST_CONFLICT`. A terminal retry returns its original stored
result without rewriting storage. Cancellation of a committed identity reports
committed; an aborted identity cannot later commit. Transport response loss does
not change the stored state.

Begin records a 1–300 second lifetime, default 60. Commit after the deadline
records an aborted result. The coordinator owns monotonic staging expiry and
cancellation of abandoned pending work; lookup alone does not manufacture a
terminal transition. Commit requires the exact accepted baseline and one next
revision. A live document older than, or divergent at the same revision as, the
latest recorded commit requires reconciliation. The accepted commit stores both
native containers and the bounded original result in the same checkpoint.

Retain terminal outcomes for at least 30 days. At most 10,000 outcomes and the budget
in the implementation notes below are allowed (64 MiB before D-086); callers may lower limits. Admission reserves 256 KiB plus overhead
per pending result, and rejects capacity exhaustion before accepting new work.
Pruning removes only expired terminal entries as part of a new durable checkpoint;
the latest commit is retained with its recovery pair. Clock rollback cannot prune
young results. The request-ID high-water mark survives pruning, so expired IDs
remain unavailable and cannot be replayed as fresh operations.

No live history is reconstructed in this slice. The following coordinator must
prepare publication before disk effects, publish only after durability, reconcile
uncertain writes without rerunning commands, and restore the verified assistant
undo entry from the stored delta and task metadata.

## Implementation notes (D-089 and D-086)

Found by a peer session's 1.6.0 test on 2026-10-31. Fixed in
[PR #560](https://github.com/zygote55/sketchyup/pull/560) (branch `fix/d089-outcome-budget`).
Triage: [R089a](../verification/R089a-defect-triage.md).

**The rule.** The budget counts what the store writes: the encoded before and after
containers, and the metadata. `OutcomeStore::begin` encodes the base once and refuses when
two copies of its container, plus the 260 KiB result reservation, do not fit the budget
(`2 × size + 266,240 ≤ limit`). `OutcomeStore::commit` no longer checks in-memory sizes.
`encode()`'s own check (metadata + before + after + 44 ≤ limit) is authoritative. The
outcome count, result size, TTL and retention limits are unchanged.

**Where this contradicts the ADR.** The ADR states the storage limit in native containers,
and the rule now measures exactly that. The code it replaces measured
`Document::readSnapshotBytes()`, the in-memory size, in `begin` and in `commit`. That size
counts every materialized placement, so a model with many placements can be far above its
container. ADR 0022's D-082 notes say a 52 MiB durable commit is refused; with this change
it commits.

**Cause.** The failing tests below confirm it. A model of 95.2 MiB in memory and 1.63 MiB
stored was refused at `begin` with "Base document exceeds durable storage capacity". The
peer's report gives the same message from the same check. I did not run the peer's
model, which is not mine to use, so their 27.9 MB model is not measured here.

The peer's report also says the outcomes folder holds only an empty `<documentId>-1`
directory. I did not reproduce that. `OutcomeStore`'s constructor writes an empty
checkpoint `OUTCOMES` when it opens, so the folder should hold that file. Not verified.

**The budget.** `outcomeStoreBytes` = 2 × `NativeLimits::containerBytes` + 16 MiB =
268,812,472 bytes (256.3 MiB).

- `NativeLimits::containerBytes` = 16 + 4 MiB manifest + 32 MiB model + 64 MiB assets +
  20.2 MiB terrain mesh = 126,017,628 bytes (120.2 MiB). `decodeContainer` refuses anything
  larger, so this is the largest valid container. The brief named 128 MiB, which is
  `NativeLimits::fileBytes`, the bound on a raw file. The store writes containers, so the
  container bound is the right one.
- 16 MiB holds the outcome records and their results, each result at most 256 KiB. The
  commit's own check decides whether a particular commit fits.
- `transactionCapabilities()` reports `outcomeBytes` from the same constant, and the
  store's `hardLimit` is now `outcomeStoreBytes`, the largest accepted `Limits::bytes`.

**Tests.** Before the fix, on the unchanged code: `outcome_store_tests` stopped at
"Base document exceeds durable storage capacity" on the placed model;
`outcome_store_tests --measure-commit assets` and `placed` were refused at `begin` the
same way; `transaction_dispatch_tests` stopped at the 52 MiB durable commit with
"Durable outcomes exceed the storage budget".

The tests changed as follows. The D-082 dispatch block asserted that the 52 MiB commit is
refused; it now asserts that it commits. The commit-budget test in `outcome_store_tests`
used a 200 KiB base and a 400 KiB budget, which `begin` now refuses, so it uses a 100 KiB
base, a 450 KiB after and a 512 KiB budget. Both suites pass after the fix. Added:
a 3,000-placement model that begins and commits; 64 MiB of payloads that commits; 42 MiB
of payloads that commits, and the same model refused at `begin` under the old 64 MiB
budget with the existing message and no persisted identity; and a dispatch round trip
(begin, apply, preview, commit, save, reload) on the placed model.

**Measurements.** Debug build with `-g0`, shared libraries, Linux, this host. Each model
ran in its own process with `outcome_store_tests --measure-commit`, so the peak resident
set (VmHWM) is that commit's own.

| Model | In memory | Stored before / after | begin | commit | Peak RSS before → after commit |
| --- | ---: | ---: | ---: | ---: | ---: |
| 3,000 placements of a 160-sided face | 99,866,358 B (95.2 MiB) | 1,712,315 B each (1.63 MiB) | 78 ms | 146 ms | 146.0 → 165.8 MiB |
| 4 × 16 MiB payloads | 67,115,058 B (64.0 MiB) | 67,113,679 B each (64.0 MiB) | 152 ms | 548 ms | 125.7 → 526.3 MiB |

The commit holds the before and after containers, `encode()`'s concatenated payload, and the
result copy: about 3 × 2 × S at container size S. The 64 MiB case adds 400 MiB. At the
120.2 MiB bound it would be about 720 MiB, transient. Not reduced here.

**Not measured.** A container at the 120.2 MiB bound. The payload model is the largest I
built, because the model JSON and the terrain mesh were not built at their limits. A second
commit also copies the retained checkpoint in `write`, so its peak is higher than the first.
That was not measured.

**Not done.** No copy reduction in `encode()`. No change to the outcome count, result size,
TTL or retention limits.
