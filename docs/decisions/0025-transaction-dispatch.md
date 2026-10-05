# Incremental transaction dispatch

R041.d exposes the document coordinator through an in-process, versioned JSON
registry. `capabilities.transactions` and the installed `transactions-v1.json`
are generated from the same operation schemas. CLI/MCP transports and provider
scope checks wrap this dispatcher in later slices. There are no filesystem,
shell, credential or arbitrary code operations in this registry.

## Drafts and immutable commit identities

`transaction.begin` requires the explicit document identity and current revision.
It returns a volatile UUID `transactionId`, initially at `stageVersion: 0`.
History metadata belongs to the complete task; assistant tasks require the original
request. Beginning a draft does not change the live model or issue a durable commit.

`transaction.apply` requires the UUID, `expectedVersion`, an `operationId` and
registered commands. It prepares the cumulative batch against the original base
state and replaces the private proposal only after successful validation. Created
IDs remain usable by later commands in that draft. Failure preserves the previous
proposal. Each successful append advances the stage version once; retries of the
same operation ID and normalized payload return its cached receipt. Reusing an ID
with different commands or version is `REQUEST_CONFLICT`. A replayed old receipt
reports both its original `stageVersion` and the draft's `currentVersion`.

`transaction.describe`, `transaction.inspect` and `transaction.diff` expose the
private proposal through the bounded inspection registry and direct-record pages.
Inspection requests target the reported proposed revision. Empty drafts cannot be
inspected. Provisional IDs and measurements describe proposed work, not published
geometry. An intervening edit, even edit followed by undo, invalidates the draft;
saving unchanged content does not. There is no implicit rebase.

`transaction.preview` seals the current version. It issues the coordinator's
durable `requestId` and immutable `payloadHash`, then releases the duplicate private
proposal. Repeating preview returns the same identity. Further appends require a
new draft; the durable identity never changes its payload. A lost preview reply
can be recovered by describing or previewing the same live draft. If staging is
lost at restart, pending work is aborted, never recreated from commands.

`transaction.commit`, `transaction.cancel` and `transaction.status` use only the
durable identity/hash plus explicit document scope. They remain useful after the
volatile draft is released or the process restarts. Terminal results preserve the
original stored outcome, result mappings and undo metadata; the response operation
field identifies the current call. A committed retry does not append another edit.
Late cancellation reports the original commit. Unknown identities do not create
work. `transaction.abort` discards an unsealed draft or cancels its sealed request.

Possible storage replacement freezes dependent operations until
`transaction.reconcile` verifies durable evidence. Status remains available while
uncertain. Successful reconciliation may publish the retained candidate once or
record a verified abort. Explicit restart recovery is a process-level choice;
requests cannot open arbitrary paths or override it. See the
[coordinator contract](0024-transaction-coordinator.md).

## Resource and lifetime bounds

Requests are at most 64 KiB of compact JSON. A draft accepts at most 100 cumulative
commands and a complete normalized batch of at most 64 KiB. Strict schemas reject
unknown parameters and unsupported commands. Geometry, cross-field and document
constraints still run through authoritative core validation. The shared schema
validator covers the vocabulary emitted by these registries, not arbitrary JSON
Schema supplied by callers.

At most four drafts retain at most 128 MiB in total: prepared edits/snapshots,
command/history metadata, sealed coordinator proposals and cached apply replies.
The accounting conservatively charges shared model resources; it is not an RSS
limit. Temporary replacement preparation can coexist with the previous proposal
until validation succeeds. A caller can lower admission limits. A proposal that
cannot fit fails without replacing the old draft or publishing live work.

Each draft has a monotonic lifetime of 1–300 seconds, default 60, starting at begin.
Apply and inspection check expiry after expensive work. Sealing gives the
coordinator only the remaining whole seconds, never a refreshed lifetime; less
than one second remaining cannot seal. Stale and expired drafts are retired on
dispatch. Durable outcome retention is independent of volatile draft lifetime.

Diff pages contain at most 100 direct changed records. Inspection retains its
16 KiB input, 256 KiB output and page limits. Transaction responses allow 272 KiB,
including the coordinator's 256 KiB stored result and bounded envelope. Outcome
storage retains at most 10,000 records/64 MiB and terminal outcomes for at least
30 days under its admission policy. Large tasks may need splitting before commit.

The dispatcher is confined to the coordinator's owner thread and rejects reentry.
Callers must enforce document/tool authorization on every request and retry. The
legacy `executeBatch` API remains an atomic local operation; durable retry semantics
require this dispatcher or the coordinator, not repeated legacy batch execution.
