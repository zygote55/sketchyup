# ADR 0034: OpenAI Responses adapter and Linux credentials

Status: accepted implementation contract; live provider acceptance pending.
The user explicitly selected **OpenAI** as the first remote provider and allowed
credentials to be configured later. A model ID remains an explicit host setting;
the adapter does not silently substitute models or assume account access.

`OpenAiProvider` owns one existing `AssistantTask`. The task must declare OpenAI
and remote context; its host consent grant remains mandatory before any request.
Requests use QtNetwork asynchronously at the fixed HTTPS Responses endpoint,
normal TLS verification, manual redirect handling and no streaming. No endpoint
URL, authorization header or credential comes from model arguments. The native
Preferences and assistant panel integration follows in R046.

The adapter maps advertised catalog names to stable `tN` aliases and translates
replies back before the existing task validates and executes tools. The original
schemas retain optional fields and free-form document properties, so declarations
explicitly use `strict:false`; application validation remains authoritative.
Parallel tool calls are disabled and multi-call responses rejected. Only supplied
function tools are available. No provider shell, network or filesystem tools are
added. Preview ends the provider loop; host Apply owns commit.

Requests set `store:false`, carry their complete conversation and explicitly ask
for encrypted reasoning output. Complete output items are retained unchanged for
stateless replay, including reasoning items and message phase. Only assistant
text/refusals and advertised function calls enter the application reply. Opaque
reasoning stays in memory and is not shown in the conversation. Provider output
is retained only after the task accepts it; failed attempts never replay tools.

Limits: 30-second absolute request deadline plus transfer timeout, 1 MiB HTTP
response, 4 MiB retained provider outputs, 8 MiB serialized request, 64 output
items and JSON depth 64. Existing task limits independently bound tools, text,
turns, cumulative usage and elapsed time. `max_output_tokens` is capped by the
remaining task budget; reported input and output usage must be valid integers.
Incomplete/failed responses cannot execute tools. These bounds do not guarantee
prebilling cost or compatibility with every model's context size. Model/account
limits and quality require a configured live trial.

HTTP 429, 408/504 and server/connectivity failures use the task's bounded retry
policy; no separate network retry loop is added. Retry-After numeric seconds are
clamped to ten seconds. Authentication and other rejected requests fail with a
fixed diagnostic. Raw error bodies, network error strings and headers are never
shown or logged. Document staleness, task deadline, cancellation or destruction
abort the outstanding request. Cancellation cannot undo a committed edit.

`OpenAiCredentialStore` uses libsecret's `secret-tool` asynchronously with fixed
application/provider/account attributes. The key is sent through stdin, exactly,
without an appended newline; it never appears in argv, environment or document
JSON. Lookup bytes are private and consumed once. Store/lookup/clear have a
60-second deadline and terminate/kill cancellation. Output is bounded, diagnostics
are discarded, and key fields are cleared on completion/destruction. Qt/process
buffers can retain copies; this is not a secure-memory erasure guarantee.

The helper and an unlocked desktop Secret Service must be available. There is no
plaintext fallback. Unavailable/missing/failed/canceled states remain distinct;
a canceled store may have reached the service, so callers should look up again
before claiming absence. The optional Arch dependency is `libsecret`. Selecting
an alternate helper or injected network manager is a trusted host/test operation,
never an assistant tool.

Protocol references checked 2026-10-05:
- [OpenAI function calling](https://developers.openai.com/api/docs/guides/function-calling)
- [OpenAI conversation state](https://developers.openai.com/api/docs/guides/conversation-state)
- [Create Response reference](https://developers.openai.com/api/reference/resources/responses/methods/create)
- [libsecret helper source](https://gnome.pages.gitlab.gnome.org/libsecret/coverage/tool/secret-tool.c.gcov.html)
