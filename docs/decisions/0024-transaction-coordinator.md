# Durable publication and transaction reconciliation

R041.c connects private proposals to durable outcomes through an in-process
`TransactionCoordinator`. It owns the live document and serializes operations on
its constructing thread. External callers receive const model access; manual
changes use a guarded callback applied privately before publication. Wrong-thread,
reentrant and uncertain-state mutations are rejected. Read-only model access can
still show the last published state while `uncertain()` is true.

This slice accepts a complete immutable command batch for preparation. The later
R041.d transaction registry supplies incremental begin/apply/preview dispatch and
versioned schemas; CLI/MCP and provider authorization remain later layers. No
remote commit endpoint is enabled here.

## Prepare, inspect and commit

Preparation uses the bounded staging session, hashes the normalized compact JSON
batch and issues a durable server request ID bound to that hash and base state.
The volatile stage ID and durable request ID are distinct. Private inspection and
paged diffs remain available until an intervening edit, cancellation or expiry.
Stale/expired bindings are retired before further admission. Lost staging on
restart is durably aborted rather than recreated by replaying commands.

Commit checks the durable outcome first. Terminal retries return the original
receipt; unknown identities never create work. Pending work must still have a
valid session/state/revision and unexpired proposal. A stale proposal produces an
aborted outcome with `STALE_REVISION`; expiry produces `EXPIRED`. Human edits are
preserved. A save without content changes does not invalidate the proposal.

Before disk effects, the coordinator builds a publication candidate with the live
history and applies the prepared edit without rerunning its commands. It also
builds the created IDs, direct changed records, topology mappings and original
undo metadata. Mapping construction uses a conservative charge before creating
JSON; the complete stored result is at most 256 KiB. Excess results fail before
commit and remain cancellable. Callers can split such tasks. Nested component
batches return needed changes without constructing a whole-document JSON response.

The candidate and original receipt are then co-recorded by `OutcomeStore`. Only a
confirmed committed result permits a noexcept move into the live document. The
store allocates its return envelope before its durability boundary as well, so a
post-write response allocation cannot hide a durable commit from publication.
Successful application increments the revision once and adds one labeled task to
history. Its receipt identifies the original task and commit revision; it does
not promise that entry survives future history pruning or document replacement.

## Uncertainty and cancellation

A failure before checkpoint replacement leaves the live document unchanged and
the request pending for a safe retry or cancellation. Possible replacement keeps
the publication candidate and baseline stamp in memory and freezes dependent
operations. Status reports `unknown`; cancellation cannot invent an aborted
result while the commit may already be durable.

Reconciliation rereads and synchronizes the durable checkpoint. If it contains
the exact retained committed candidate, publish that candidate once. If it proves
there was no commit, record an abort and discard the proposal. Mismatching or
unavailable evidence leaves publication blocked. No commands are replayed. Losing
a transport response after success changes neither the outcome nor undo history;
a retry or late cancellation returns the same committed receipt.

## Restart and undo recovery

Opening a model older than, or divergent at the same revision as, the latest
transaction requires explicit recovery. A newer validated model can continue
from its later revision while retaining old outcomes. Explicit `RecoverLatest`
loads the latest co-recorded before/after pair and original undo metadata. It
reconstructs the verified record delta through core validation and checks that
the resulting native container exactly matches the recorded candidate.

Semantic comparison of decoded records avoids treating unchanged pointer
identities as edits, including nested component definitions and owned asset
payloads. The recovered model and its undo baseline remain dirty: a transaction
checkpoint is not an explicit save. Only the latest verified assistant entry is
restored; earlier manual/assistant history is not fabricated. Older durable
receipts remain queryable independently of retained undo history. Input model
files are never overwritten by coordinator recovery.

Geometry preparation, encoding and storage are synchronous on the owner thread in
this initial coordinator. Future desktop/provider integration must schedule work
around that constraint, preserve the same publication ordering, and enforce user
scope/authorization on every call and retry.
