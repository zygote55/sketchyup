# ADR 0167: Recoverable provider replies

Status: accepted (2026-10-17). Amends [ADR 0030](0030-assistant-orchestration.md)
("Scoped tools and preview gate"), [ADR 0034](0034-openai-provider.md),
[ADR 0036](0036-local-provider.md) and [ADR 0038](0038-assistant-clarification.md), and
the read-only batch rule recorded in
[R086.ap](../verification/R086ap-bounded-inspections.md). It follows defect D-041
([R089.a](../verification/R089a-defect-triage.md)): a complete HTTP 200 reply that failed
one decode rule stopped the whole task with `PROVIDER_FAILED`, without a retry and
without telling the model what was wrong.

## Rule

A provider reply is either **unusable** or **well-formed**.

**Unusable replies stay fatal** (`providerFailed(Fatal)`, then `PROVIDER_FAILED`;
status "Provider returned an incomplete or invalid response"). This covers:

- HTTP and transport errors, and the existing retry policy for 408/429/5xx;
- a body that is not valid JSON, is over its byte or depth bound, or is not a
  completed response (status other than `completed`, `done_reason` other than `stop`);
- a failed, incomplete, interrupted or conflicting event stream;
- missing or invalid usage;
- an output item that is incomplete (a function call or message whose status is not
  `completed`), has no call ID, has arguments that are not a JSON object, or is a
  content or output type the host did not request;
- a reply with neither text nor calls.

Task-level bounds stay fatal as before (`BUDGET_EXCEEDED`, `INVALID_PROVIDER_REPLY`):
more than eight calls in a reply, a duplicate or reused call ID, the transcript
reservation, and every per-reply and aggregate token, byte and tool-call budget.

**A well-formed reply that breaks a host rule is refused, not fatal.** The host
accepts the reply into the transcript, executes **none** of its calls, and answers
every call with a tool error receipt (`isError: true`) that names the rule. The model
reads these receipts on its next turn, as it already reads argument and schema errors.
The refusable host rules are:

| Rule | Code | Receipt message |
| --- | --- | --- |
| A multi-call reply may contain only read-only inspections (OpenAI, R086.ap) | `BATCH_NOT_READ_ONLY` | "Only read-only inspections may be batched; send *name* alone." |
| A call must name an advertised tool (alias, and the `sketchyup` namespace on the ChatGPT plan) | `UNKNOWN_TOOL` | "*name* is not an advertised tool; call only the advertised tools." |
| `transaction.preview` must be the last call in a reply (ADR 0030) | `PREVIEW_NOT_LAST` | "transaction.preview must be the last call in a reply; send it after the other calls." |
| `assistant.ask_user` must be the only call in a reply (ADR 0038) | `CLARIFICATION_NOT_ALONE` | "assistant.ask_user must be the only call in a reply; send it alone." |

*name* is the first offending call (truncated to 64 characters). Every receipt also
carries `refused: true`, `executed: false` and a `retry` sentence stating that the
host executed no call from that reply. The read-only batch rule keeps its R086.ap scope:
it applies to the OpenAI adapter only. The local adapter (ADR 0036) still accepts up to
eight advertised calls of any kind, subject to the preview and clarification rules.

Calls that do reach dispatch keep their existing per-call tool errors (argument schema,
authorization, document binding, draft ownership) and their existing fatal codes
(`STALE_REVISION`, `WRONG_DOCUMENT`, `OUTCOME_UNKNOWN`). This ADR does not change them.

## Budgets

A refused reply is charged like any accepted reply: it used the provider turn that
requested it, its reported tokens count toward the task total, and each of its calls
counts toward the task's tool-call budget. It cannot extend the deadline. A model that
keeps breaking a rule therefore ends with `BUDGET_EXCEEDED` (turns, tool calls or
tokens) or `TIME_LIMIT`, not with an endless loop. No retry from the transport retry
budget is spent, because the reply was delivered.

## Transcript and replay

The refused reply's provider output is retained and replayed unchanged, followed by one
function result per call ID, exactly as for an accepted reply. Opaque reasoning is kept.
Nothing about a refused reply changes the document, the draft, the draft state or the
preview seal.

## Host status

While the task continues after a refusal, the transport status reads
"Provider reply refused: *message*" and then, while the next request is in flight,
"Waiting for *provider* to correct a refused reply". The Assistant panel shows this
status in its working phases and lists the refused calls as "Rejected: …" activity,
as it does for other tool errors. "Provider returned an incomplete or invalid
response" is reserved for unusable replies.

## Unchanged

Assistant instructions, the advertised tool surface, the release corpus v4 manifest and
its instruction and tool hashes do not change. A refusal never executes a call, so it
cannot widen authorization, publish a change or seal a preview.

## Limits of this decision

The Responses API's handling of a replayed `function_call` item whose name is not in
the current tool list (`UNKNOWN_TOOL`) was not exercised against the live service; the
offline fixtures accept it. If the service rejects such a replay, that turn fails with
the existing HTTP 400 diagnostic, which is no worse than the previous fatal decode.

## Verification

All offline; no provider request leaves the machine.

- `openai_provider_tests`: the codec refuses a mixed batch (`transaction.begin` among reads,
  and `transaction.preview` among draft reads) with the named rule on every call, refuses
  unadvertised aliases and plan namespaces, and still rejects malformed argument JSON,
  unfinished calls, incomplete responses and truncated bodies. A scripted replay through
  `OpenAiProvider`, `NativeAssistantSession` and, for the plan, the streamed transport and
  `chatGptAssistantLimits()` (the corpus v4 path) accepts a batched `transaction.inspect` +
  `transaction.diff` reply, answers a `transaction.inspect` + `transaction.preview` batch
  with `BATCH_NOT_READ_ONLY` tool errors, then reaches preview in five turns and seven tool
  calls and commits on host Apply. A capped run that repeats the illegal batch ends with
  `BUDGET_EXCEEDED` at its sixth turn and publishes nothing.
- `assistant_tests`: a misplaced preview and an adapter refusal each refuse the whole reply,
  including a valid `transaction.begin` beside them; the turn, tokens and calls are charged;
  a mixed clarification reply is refused with `CLARIFICATION_NOT_ALONE` and the draft stays
  open.
- `ollama_provider_tests`: an unadvertised local alias is answered with a replayed
  `UNKNOWN_TOOL` error and the task completes on the next turn.
- `provider_release_corpus`: one extra, unscored Room run through the real `provider_trial`
  and native session batches `transaction.inspect` + `transaction.diff` (accepted), then
  sends `transaction.preview` before an inspection (the local adapter has no read-only batch
  rule, so this uses the provider-neutral preview rule), receives `PREVIEW_NOT_LAST` errors,
  and commits a verified edit with passing undo and recovery postconditions. The frozen v4
  manifest, instruction hashes and tool hashes are unchanged and still checked.
- `assistant_panel_tests`: while the corrected reply is pending the panel reads "Waiting for
  OpenAI to correct a refused reply" and lists both refused calls as "Rejected: …".
