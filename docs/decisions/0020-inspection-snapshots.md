# Retained inspection snapshots

R040.b adds `InspectionSession::execute` as an in-process read session. Its
[generated schemas](../api/inspection-session-v1.json) extend the bounded query
registry with `snapshot.begin`, `snapshot.release` and an optional `snapshotId`
on each read query. Discovery lives under `--capabilities` → `inspectionSession`.
The existing file-only `--inspect` interface remains stateless; persistent CLI
and MCP adapters belong to R042/R043. Desktop view capture is described in [R040.c](0021-desktop-inspection.md).

## Consistent captured reads

`snapshot.begin` requires API version, document identity and the current expected
revision. An optional integer `ttlSeconds` is in 1–300, defaulting to 60. It
returns a random snapshot identifier, captured revision, UTC capture timestamp,
remaining lifetime and admission byte charge. Names, geometry, hierarchy,
definitions, tags, materials, assets, display units and saved/dirty state all
refer to that revision. A supplied editor selection/context/hiding/lock state is
captured with it; missing editor state remains explicitly unavailable even if a
later call supplies a live editor.

`Document::readSnapshot` shares immutable scene records and copies the ordinary
document bookkeeping, including allocator floors and saved/session stamps. It
drops retained undo and redo history. The session holds the resulting document
through a `unique_ptr<const Document>`; all inspection queries remain const.
Later live edits publish different scene records and cannot mutate the capture.
The captured editor remains bound to the matching document session and can be
read exactly, including hiding from contexts other than the currently active one.
It is not reconstructed through UI operations with selection eligibility filters.

A captured read carries the original query envelope and `snapshotId`;
`expectedRevision` must equal the captured revision. It can succeed after the
live document has advanced. Results include the snapshot identifier, capture
timestamp, remaining milliseconds, current `liveRevision` and a
`differsFromLiveRevision` flag. The `data` always comes from the capture. Paged
continuations bind the captured query and editor state, so intervening live
selection changes and model edits do not disrupt them. There is no silent
fallback to current state when a capture is missing.

`snapshot.release` identifies the document, captured revision and snapshot ID.
It releases retained state without editing the model. Expired, released,
unknown or replaced-document captures return `SNAPSHOT_UNAVAILABLE`; callers
must acquire a fresh capture rather than treating absence as successful reads.
A wrong captured revision returns `STALE_REVISION`. Capturing stale or
foreign-session editor state returns `STALE_SELECTION`.

## Lifetime and bounds

A session retains at most four captures with a combined 32 MiB admission budget.
The charge conservatively estimates scene records, definitions, instances,
semantic strings, topology, owned resource payloads, allocator bookkeeping and
editor state. It charges shared data at full size separately for each capture.
`retainedBytes` is this admission charge, not a measurement or exact cap on process
RSS. Native model complexity limits also apply. Preflight traverses record sizes
without serializing the model, copying undo history, duplicating resource bytes
or allocating captured editor sets. Only after admission does capture copy the
small record maps and editor state while sharing immutable model records. Native
snapshot bytes and undo history are not retained or transiently copied.

When either limit would be exceeded, `SNAPSHOT_LIMIT` rejects the new capture
without evicting a still-valid one or retaining partial state. Embedders can
configure lower limits. Resource exhaustion is not evidence that a model edit
failed; these operations never commit edits.

Expiry uses a monotonic clock and never slides on access. UTC is informational.
Validated document dispatch reclaims expired captures and captures from a replaced document
session, even when the replacement has the same persistent ID and revision.
Queries also check expiry after the bounded geometry work and reject a result
that finished too late. Reclamation is lazy until dispatch, explicit `clear`,
or destruction; this layer creates no background timer. `clear` and session
destruction release all retained state. Captures do not survive process restart.

The caller serializes dispatch with document/editor operations. This class does
not run workers, mutate selection, alter files, open sockets or provide view
capture. R040.c connects desktop feedback; R041 adds private mutation staging and
durable commit outcomes. A read snapshot is not an editing transaction.
