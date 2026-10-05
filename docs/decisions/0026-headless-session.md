# Explicit headless automation sessions

R042.a adds a persistent JSON-lines session to `sketchyup-cli`. It uses the same
bounded inspection, snapshot and transaction dispatchers as the in-process API.
It needs Qt Core, but no display server, desktop window or provider. Recipe-file
execution is the following R042 slice. MCP remains R043.

## Launch and scope

Choose one existing model with `--input FILE`, or create a new model with
`--new --output FILE`. Both require `--session --outcomes DIRECTORY`. There is no
implicit current document. `--session-capabilities` prints the installed schema
without opening a model or outcome store. Session flags cannot be combined with
legacy script/query/import/recovery operations; invalid combinations fail before
model creation. Argument parsing errors are structured and nonzero.

The outcome directory is private durable transaction storage, never a model save
destination. Model paths must be outside it, including through resolved symlinked
parents. A save destination must have an existing parent directory and be a
regular file or absent. New-model creation requires an absent destination. It
persists the initial empty native model and identity before accepting transactions.
If later outcome-store opening fails, that created baseline may remain; open it
with `--input` after resolving the error rather than generating another identity.

The process owns one coordinator and document. `session.describe` returns its
identity, current revision, dirty state, uncertainty and save availability. It
explicitly reports that desktop selection and view are unavailable. All modeling,
inspection and save requests require the bound document identity and their shared
revision/transaction preconditions. No request can select another file, outcome
root or output path. The trusted launcher chooses those capabilities explicitly.

An existing model older than the latest durable transaction requires the explicit
`--recover-latest` launch option. Recovery uses the original verified candidate and
undo metadata without replay. With recovery enabled, output must be a separately
selected copy; the original input is preserved. Pending requests whose private
staging was lost are durably aborted on opening the coordinator.

## Wire protocol

Each input line is one object with exactly `id` and `request`. The correlation ID
is a nonempty string of at most 128 UTF-8 bytes. The request is an API-version-1
object selecting exactly one registered `operation` or `query`. The response is:

```json
{"id":"step-1","ok":true,"result":{"apiVersion":1}}
```

Failures use `ok: false` and `error: {code, message}`. Messages are bounded; model
geometry is never dumped as an error. The correlation ID only matches responses to
calls. Durable retry identity remains the sealed request ID/hash from R041; a
correlation ID does not make arbitrary operations idempotent.

`session.describe` and `session.capabilities` require only `apiVersion` and
`operation`. Other operations use their published strict schemas. Inspection
queries include expiring snapshots. Transaction operations are unchanged. Output
contains one compact JSON object per line and no progress logs. Startup failures
are structured JSON on stderr. Request failures allow later status/reconciliation
calls, but make the eventual process exit nonzero. Oversized or excessively nested
input terminates the stream with a structured failure and nonzero exit.

Each line is at most 66 KiB, including its newline and envelope; each request is
at most 64 KiB, and inspection keeps its stricter 16 KiB limit. Input nesting is
at most 64 levels. Replies are at most 1 MiB including discovery; ordinary
inspection and transaction results retain their smaller shared limits. Reads and
writes apply backpressure instead of queuing an unbounded stream. Commands run
synchronously on the document owner thread. Closing stdin releases snapshots and
uncommitted drafts, recording cancellation for pending sealed work. Committed
outcomes remain queryable. Abrupt process termination leaves reconciliation to the
next explicitly opened session; disconnect is never proof that a commit failed.

## Scoped saves and external changes

`document.save` requires the current `documentId` and `expectedRevision`. It saves
only to `--output`, including the native `.bak` file; without that launch option,
save is unsupported. The response contains the saved revision and native-byte
SHA-256. Saving is an external effect, separate from the geometry transaction and
undo. A commit alone does not overwrite the explicitly saved model.

The process holds an advisory lock for its output destination. It records the
initial file hash and compares the current bytes before each save. An external
change, deletion or replacement rejects with `FILE_CHANGED` without writing. A
second automation writer for the same destination rejects with `FILE_BUSY`.
These checks cannot eliminate races with a noncooperating writer between hash
verification and atomic replacement; automation and the desktop should not write
the same destination concurrently. Native atomic save, backup and fsync behavior
remain unchanged.

A save that fails after entering native persistence reports
`SAVE_OUTCOME_UNKNOWN` conservatively. The session exposes that state and blocks
further dependent operations; bounded reads, session discovery and transaction
status remain available. Reopen and inspect the selected destination before
continuing. No geometry undo is invented to reverse a possibly successful save.
While transaction publication is uncertain, inspection sees the last published
live state; clients must resolve the durable outcome before claiming work absent.
