# Private proposals and guarded publication

R041.a introduces in-process preparation primitives. Durable outcome storage,
idempotent request reconciliation and transport schemas remain R041.b/c. No
provider or CLI transaction commit endpoint is enabled by this slice.

`Document::prepareEdit` gives its callback a private copy sharing immutable scene
records and allocator bookkeeping, with no retained undo/redo history. The
callback must create exactly one composed edit at the next revision, preserve
the document session/identity and saved marker, and leave no redo branch. Failed
or empty preparation cannot publish. The returned `PreparedEdit` owns an opaque
validated edit and immutable history-free proposed document. Callers can inspect
it but cannot alter its records or base preconditions.

`canApply` and `applyPrepared` require the exact original document session,
content state and revision. Editing then undoing to the original geometry still
invalidates the proposal. Reopening the same document ID and revision creates a
new session and also invalidates it. Saving unchanged content does not invalidate
it. Applying uses the already prepared edit, never reruns the operation, and
preserves the live undo history and current saved marker while adding one entry.
A second apply fails. This core operation is not itself durable or idempotent.
The durable coordinator must prebuild a publication candidate, persist the
candidate and outcome together, then publish it after verified durability.

`StagingSession` prepares an existing validated command batch as one private
edit, including its history label and assistant task metadata. It uses a reduced
command result containing created-ID lists, avoiding the experimental command
driver's whole-document/topology JSON response. Geometry and validation operations
remain shared with ordinary batches. Proposed revisions and created IDs are
explicitly provisional until the later commit coordinator publishes them.

Each session retains at most four proposals, with a conservative 128 MiB total
record charge (callers may lower either limit). Admission checks the live record
charge before cloning. Final accounting includes proposed records, the prepared
edit's before/after records, result data and diff rows; shared data is charged
conservatively. Owned asset payloads count toward admission. These are retained
record limits, not a strict process RSS or transient allocator ceiling. Core
geometry/model and 64 MiB single-edit history limits (512 MiB for terrain edits, D-043)
still apply during staging.

Requests are at most 64 KiB compact JSON and retain the command driver's limit
of 100 operations. Results are at most 256 KiB. Oversized created-ID results fail
before retention or publication; callers can split a task. TTL is 1–300 seconds,
default 60, measured with a monotonic clock starting before preparation. Expiry
is checked before admission/access, after preparation, and after staged queries.
Expired storage is reclaimed on dispatch. Release/clear/destruction also release
session ownership; the trusted coordinator can explicitly pin an immutable
proposal while processing an accepted commit. A pin is not authorization or a
terminal outcome.

Staged inspection uses the existing bounded query registry against the proposed
revision and reports `stageId`, `status: staged`, provisional IDs and base revision.
It has no desktop selection or view image. Direct changed records are paged in
stable kind/ID order, at most 100 rows: bodies, definitions, instances, tags,
materials, assets and display units. Each row reports created/updated/removed.
Indirect world-space effects from a changed parent are represented by that parent
record; rendering a preview must use the complete proposed scene graph.

Any intervening live edit invalidates access to the proposal. Its storage remains
bounded until release or TTL expiry. Failed commands, wrong documents/revisions,
invalid TTLs/pages, exhausted capacity and expired proposals do not alter the live
model. The later durable coordinator must recheck content preconditions and
proposal validity before accepting a commit and distinguish cancellation before
commit from a durable committed result afterward.

## Implementation notes (D-082)

**Accounting rule.** A proposal is charged for what it holds beyond the live model it was
prepared from: its own edit, the fixed bookkeeping of its copy, 64 bytes for each record
entry (its map node), and each record whose pointer differs from the live record of the same
ID, or that the live model lacks, at full size. `Document::bytesNotSharedWith` computes the
records part; `PreparedEdit::retainedBytes(live)` adds the edit. `StagingSession::retainedBytes(live)`
sums the held proposals, each with its result and envelope bytes, against the current live
model. The charges are recomputed when a proposal is admitted, so a record that a later live
edit replaces stays charged for as long as the proposal holds it.

**Admission.** The one-third pre-check is removed; the draft-count check stays first. After
preparation, a proposal is refused with `STAGE_LIMIT` ("Proposal exceeds the retained staging
budget") when the held proposals' charges plus its own exceed the 128 MiB budget. Transaction
drafts and sealed previews use the same charge, through `TransactionCoordinator` and
`TransactionDispatcher`. Read snapshots follow the same rule ([ADR 0020](0020-inspection-snapshots.md#implementation-notes-d-082)).

**What stays capped.** Four proposals per staging session; the 128 MiB staging budget; four
transaction drafts; four captures and the 32 MiB snapshot budget per client; 64 KiB requests,
256 KiB responses and 100 operations; the 64 MiB single-edit history limit (512 MiB for terrain).
The durable outcome store keeps the before and after containers within its 64 MiB budget
(`encode` in `src/io/outcome_store.cpp`). That store is not changed here.

**Where this supersedes the text above.** The following describe the pre-D-082 rule:

- "Admission checks the live record charge before cloning": the check now runs after
  preparation. Peak memory during preparation is bounded by the document limits instead (below).
- "shared data is charged conservatively": shared records are no longer charged. Owned asset
  payloads count only when a proposal holds them beyond the live model.

**Measurements.** Synthetic model: `examples/m4-room-study.sketchyup` with thirteen 4 MiB
payloads, built in the tests (11 bodies, 15 assets, `readSnapshotBytes()` 52.07 MiB). The
"before" values are the old formula (fixed bookkeeping + edit + `readSnapshotBytes()`), computed
on the same binary. The old code refused these operations at their first step: the staging
pre-check (52 MiB is over a third of 128 MiB) and the snapshot budget (52 MiB is over 32 MiB).

| Operation | Charge before | Charge after |
| --- | ---: | ---: |
| Proposal, zero-distance move of body 7 | 52.08 MiB | 15.61 KiB (exclusive records 8.88 KiB) |
| Proposal, one-body move (0.5 m) | 52.08 MiB | 15.61 KiB |
| Two held proposals, session total | — | 35.90 KiB |
| Read snapshot at capture | 52.07 MiB | 7.48 KiB |
| Held snapshot, after 9 of its 4 MiB payloads leave the live model | 52.07 MiB | 36.01 MiB, so the next capture is refused |

Preparing two proposals raised RSS by 2.21 MiB. The process peak (VmHWM) of the measuring
program was 102.80 MiB, most of it the 52 MiB of payloads the program creates itself.

**Durable commits are still bounded by size.** On the same 52 MiB model, sealing a draft
succeeds, and the durable commit fails with `OUTCOME_LIMIT`, because the outcome store holds
the before and after containers within 64 MiB. Arithmetic, not a measurement: a model whose
saved container is larger than about 32 MiB cannot commit. D-082 moves the refusal from
staging to commit for such a model. Raising the durable budget is a decision for the owner.
