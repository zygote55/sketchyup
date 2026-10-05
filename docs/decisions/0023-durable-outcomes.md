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
The 64 MiB limit is a storage limit, not a process RSS ceiling.

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

Retain terminal outcomes for at least 30 days. At most 10,000 outcomes and 64 MiB
are allowed; callers may lower limits. Admission reserves 256 KiB plus overhead
per pending result, and rejects capacity exhaustion before accepting new work.
Pruning removes only expired terminal entries as part of a new durable checkpoint;
the latest commit is retained with its recovery pair. Clock rollback cannot prune
young results. The request-ID high-water mark survives pruning, so expired IDs
remain unavailable and cannot be replayed as fresh operations.

No live history is reconstructed in this slice. The following coordinator must
prepare publication before disk effects, publish only after durability, reconcile
uncertain writes without rerunning commands, and restore the verified assistant
undo entry from the stored delta and task metadata.
