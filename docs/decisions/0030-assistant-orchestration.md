# Provider-neutral assistant orchestration

R044.a implements an in-process `AssistantTask` state machine over the existing
bounded inspection and transaction engine. It is not a remote provider adapter,
credential store, assistant panel or evidence of natural-language modeling quality.
Those remain R044.b/R045/R046 acceptance work.

## Host and adapter boundary

A trusted host binds an `AutomationSession`, or equivalent fixed-document-session
backend, and supplies the original user prompt, provider/model identity, selected
initial inspection requests and a modeling-command allowlist. An empty allowlist
permits inspection only. No credential is accepted by this interface. The backend
must preserve its document session lifetime and owner-thread discipline; it may
not silently retarget another window or reopen a file under the same identity.
The host must not publish task-owned drafts behind the task engine; normal manual
edits remain permitted and invalidate the task revision.

The host calls `nextRequest()` on the document owner thread. A returned object
contains an opaque attempt ID, provider/model, fixed system instructions, bounded
intermediate messages, the filtered shared tool schemas and `maxOutputTokens`.
An adapter translates these intermediate values into its provider's protocol and
performs asynchronous transport. In particular, `context` is an intermediate data
role, not a promise that every remote protocol accepts that role literally.
The adapter delivers `accept(attemptId, reply)` or `providerFailed(...)` back on the
owner thread. Late, canceled and superseded attempt completions cannot execute tools.

The fixed instructions treat model names, metadata, imported content and tool
results as untrusted data. The original prompt is a user message; selected context
and tool output remain separate data messages. Unsupported tool names and commands
cannot expand the host's allowlist. This is a structural boundary and fixture test,
not a measured claim that a live language model always ignores prompt injection.

A remote request cannot be constructed without the trusted host's
`remoteContextApproved` grant. `disclosure()` identifies provider/model, prompt
bytes, initial context queries/bytes and the broader bounded document-inspection
access available through tools. It explicitly excludes screenshots and credentials.
The host must obtain the consent required by the UX contract before setting this
grant. The forthcoming UI owns per-provider consent persistence and keychain access;
a provider response or tool argument cannot grant itself consent.

## Scoped tools and preview gate

The task advertises the core bounded inspection catalog and, when authorized,
transaction begin/apply/describe/inspect/diff/preview/abort. It does not advertise
retained snapshots, save, commit, cancel, reconcile, shell or filesystem operations.
The initial context already includes session identity and revision.

Every tool name must match exactly one operation/query discriminator. Shared schema
validation enforces all arguments. Requests must use the bound document and inspected
base revision. Each task owns at most one draft; another task's transaction ID is
rejected. The begin schema omits `history`: the host injects the original user request,
its own task ID and `assistant=true`, so the model cannot forge history provenance.
Apply accepts only command schemas explicitly authorized by the trusted host.

Any intervening revision makes the task stale. The task discards its staging and
preserves human changes; it does not silently rebase or infer a new authorization.
Private inspection uses the engine's proposed revision and provisional IDs.

A successful `transaction.preview` seals the original durable identity and stops
provider work in `preview-ready`. Preview must be the last tool call in that reply.
Only the host's `apply()` can commit that sealed identity. No model-visible function
can invoke this method. The current slice supports preview-first mode only; direct
mode and target-scope UI decisions remain later integration work. The host is
responsible for presenting the actual diff and obtaining Apply authorization.

A successful commit records the original engine receipt and one assistant history
entry. Repeated Apply and late cancellation retain that receipt. A provider that ends after staging without sealing a preview fails explicitly and
discards that staging. Model prose is
returned separately as `unverifiedModelText`; it is never used to set commit status.
`applied` is true only for a confirmed committed receipt, false for known non-commit,
and null for an unknown outcome. Callers must not interpret unknown as “nothing
was applied.” Model text should be displayed as plain untrusted text in future UI.

## Cancellation, uncertainty and retries

Cancellation retires only this task's draft. It does not close a shared session,
undo committed work, save a file or discard unrelated human edits. An uncertain
commit stays unknown even when cancellation is requested. `reconcile()` asks the
shared coordinator for verified durable evidence, then resolves the original
request ID/hash. A verified commit publishes once; an aborted receipt remains
aborted; a verified pending request returns to its original preview gate.

Transient provider timeouts, rate limits and unavailability allow bounded retries
before any reply has been accepted. There are at most two retries by default,
with exponential delay from 250 ms and provider retry-after capped at 10 seconds.
The host polls the monotonic deadline; no thread sleeps in the task engine. Retries
consume provider turns and use new attempt IDs. They do not replay an accepted
batch of tool calls. Reusing a prior tool-call ID in another accepted response fails;
the shared transaction operation ID still provides explicit append idempotency.

`nextRequest()` also refreshes the deadline and document revision while waiting
for a reply or while showing a preview. The host must poll it and cancel its
transport when the task leaves the active state. Synchronous geometry and durable
storage are checked at operation boundaries; there is no hard preemption halfway
through a core edit or file synchronization.

## Resource accounting

Default bounds are 16 provider attempts, 64 tool calls, eight calls per reply,
180 seconds, 64 KiB per provider reply, 256 KiB selected initial context and 4 MiB
serialized conversation/request size. Configuration has hard maximums and cannot
disable these limits. Provider replies are depth-limited before tool dispatch.
The task reserves the maximum shared-tool receipt size for every call in a reply
before executing any call, so a full transcript cannot hide an accepted effect.
These bounds cover serialized retained content; they are not a process-RSS promise.
Adapters must also bound their raw incoming wire buffers before parsing.

The default requested output cap is 2,048 tokens per response and the cumulative
reported usage budget is 131,072 tokens. Remote adapters must supply input usage;
missing usage stops the task. The engine passes the remaining output cap to the
adapter and rejects over-budget reported replies before dispatching tools. Exact
provider input tokenization, context limits, pricing and billing are adapter-specific.
This reported-usage budget is not a guarantee that a remote request incurs no cost
before an oversized usage report is received. Live adapter acceptance must record
its model-specific accounting and enforce supported transport-side limits.

## Verification boundary

Deterministic fixtures drive real `AutomationSession` and coordinator backends.
They verify a 2 × 3 m face in staging, its 6 m² measurement, the preview gate,
commit, assistant history, stale edits, cancellation, unknown-outcome reconciliation,
remote-context gating, restricted tools, untrusted metadata, budgets, late replies,
retry backoff and owner/reentrancy guards. See the
[verification record](../verification/R044a-assistant-orchestration.md).
No provider identity in these fixtures represents a configured remote service.
