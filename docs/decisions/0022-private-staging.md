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
geometry/model and 64 MiB single-edit history limits still apply during staging.

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
