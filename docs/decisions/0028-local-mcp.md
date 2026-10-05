# Scoped local MCP transport

R043.a exposes the shared headless session through `sketchyup-cli --mcp`, with the
same explicit input/new model, outcome directory, recovery and optional save
scope. `--mcp-capabilities` prints the supported protocol, tools and limits without
opening a model. This slice provides the headless binding; native window selection
and view binding remain a separate R043 verification step.

## Protocol version

The implementation targets MCP **2026-07-28**. It uses `server/discover` and requires
protocol version and client capabilities in each request's `params._meta`. There
is no legacy `initialize` handshake. Missing metadata is an invalid-params error;
an unsupported version returns `-32022` with the requested and supported versions.
Complete results identify their result type and server implementation. Cacheable
results use a zero lifetime and private scope. These choices follow the current
[base protocol](https://modelcontextprotocol.io/specification/2026-07-28/basic/index).

Stdio carries one JSON-RPC object per line. Requests use bounded strings or safe
integer IDs; null IDs and batches are rejected. Notifications receive no reply.
Only protocol messages go to stdout. EOF closes private staging and subscriptions;
process loss does not erase durable transaction outcomes. See the
[stdio binding](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/stdio).

## Shared tools and document scope

The 29-tool headless catalog derives directly from the session, retained inspection
and transaction registries. Tool arguments retain the API version, explicit
operation/query name, document identity and revision or transaction identity. The
outer tool name must match the inner operation/query. No legacy full-model query,
arbitrary path, shell, import, credential or network tool is exposed.

`session.describe` identifies the process's explicitly selected model. Every
modeling call and retry then validates that identity through the shared backend.
`document.save` can write only the launcher's chosen output and native backup.
Capabilities supplied by a client do not grant model scope or change authorization.
Desktop selection is unavailable in this headless binding and returns the existing
`UNAVAILABLE_CONTEXT`; it is not presented as an invented empty desktop selection.

Unknown tools and malformed outer parameters are JSON-RPC errors. Authoritative
tool validation/model failures are actionable `isError` results. Successful and
failed tool results include both structured JSON and equivalent text. Read-only
hints are conservative: transaction queries may perform durable outcome cleanup,
so they do not claim to be free of environmental effects. Durable commit and append
identities retain the R041 retry contract. These result conventions follow the
[tool specification](https://modelcontextprotocol.io/specification/2026-07-28/server/tools).

## Resources and subscriptions

There is exactly one resource, `sketchyup://document/ID/state`. It reports bounded
identity, revision, dirty/save and publication-uncertainty metadata. Resource
requests accept only that exact URI; no filesystem URI is dereferenced. Catalogs
are fixed and bounded, fit one page and reject nonempty pagination cursors.

At most eight `subscriptions/listen` requests remain active. Each may subscribe
to the single bound state resource. The first notification acknowledges only the
supported filter. Updates carry the originating subscription ID. State changes
coalesce at serialized operation boundaries, with no accumulating event queue.
A completed commit emits a resource update; a retry returning the same outcome
emits no duplicate state change. Cancelling a subscription removes it without a
reply or further notifications. Server-initiated EOF sends completion results for
remaining subscriptions. This uses the current
[subscription protocol](https://modelcontextprotocol.io/specification/2026-07-28/basic/patterns/subscriptions).

Cancellation is observed between synchronous owner-thread operations. A late
JSON-RPC cancellation cannot undo or reclassify a completed model commit. Clients
must use durable status/cancellation tools to resolve accepted work. This adapter
does not promise preemption during geometry validation or filesystem sync.

## Bounds and verification

Wire messages are at most 66 KiB, nesting at most 64 levels and responses at most
1 MiB. Shared tool request/result limits remain in force. Tool calls use a token
bucket with a burst of 120 and refill of two per second; an exhausted bucket returns
an actionable rate-limit result before backend effects. Backpressure bounds output
buffering. IDs are strings of at most 128 UTF-8 bytes or integers within the exact
JSON/JavaScript safe range. Dispatch is confined to its document owner thread.

The transport core accepts an explicit backend callback/catalog so a native binding
can use the same protocol without receiving mutable access to a different model.
That interface is trusted application code; tool requests cannot replace it.

Synthetic process-test messages are independently validated against the upstream
[2026-07-28 schema at commit 271ecc9](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/271ecc9accafdd9b83a3c869fa67c22953b2af80/schema/2026-07-28/schema.json).
The verification script checks the pinned SHA-256 before validating each concrete
response/notification type. CI uses Python `jsonschema` for this check only; it is
not a modeler runtime dependency. Local evidence records the validator version,
schema hash, exercised message types and sizes.
