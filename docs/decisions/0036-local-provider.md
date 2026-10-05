# ADR 0036: measured local Ollama profile and shared provider transport

Status: implementation contract; measurement and CI acceptance recorded separately.

OpenAI remains the explicitly selected remote provider. The optional local path
uses Ollama's native HTTP API on a numeric loopback address, with no proxy,
credential, redirects, automatic model download or cloud fallback. Endpoint,
model, context window and CPU thread count are trusted host configuration, never
model arguments. Remote URLs, DNS hostnames, URL credentials, non-root paths and
cloud model metadata are rejected. This is a local-service trust boundary, not a
sandbox for the installed runtime or model.

The measured profile is Ollama **0.35.1**, `qwen3:4b-instruct` Q4_K_M, a 32,768-token
context and eight CPU threads. Only the tested runtime version is accepted for
now. A context-free `GET /api/version` precedes a model-only `POST /api/show`;
only then may `/api/chat` receive document context. The model must declare GGUF,
completion, tools and a sufficient native context window. Models declaring
thinking must explicitly support disabling it. The measured Qwen3 4B Instruct
2507 metadata is an exception: Ollama exposes a legacy architecture thinking
capability but omits thinking settings for this non-thinking instruction model.
That exact metadata profile omits the `think` option. Unknown thinking-only
profiles are rejected. This does not infer model quality from capability flags.

Requests reuse the complete host-authorized catalog schemas and `AssistantTask`
transcript. Ollama 0.35.1 decodes tool properties through a typed structure that
drops `const`, `oneOf` and several bound keywords before prompt rendering. The
adapter retains the original schemas and adds redundant type/enum hints for
constants and `anyOf` beside `oneOf`; bound keywords also appear in property
descriptions. This preserves the original wire-schema constraints while making
the runtime's reduced prompt useful. The host still validates every argument
against the original schema; provider hints never expand authority. Stable wire aliases map back to the advertised tool names before
engine validation. Responses require a completed assistant message, matching
model, valid reported usage and at most eight advertised function calls with
object arguments. Host-generated call IDs are unique within each attempt. Full
accepted assistant messages and tool receipts are replayed; rejected replies are
not added to replay. Provider prose remains explicitly unverified. Only the host
can Apply a sealed preview or reconcile a durable publication outcome.

The request sets `stream:false`, `truncate:false`, `shift:false`, temperature zero,
seed zero and a bounded `num_predict`. The runtime tokenizes the full rendered
prompt and rejects overflow; it must not silently drop history before generation
or shift context during generation. Byte counts are not treated as exact token
estimates. Request/replay memory is bounded at 4 MiB, responses at 1 MiB, and
reported prompt plus output usage must fit the configured context. Incomplete
or length-limited responses fail before any tool execution. The normal engine
turn, tool, response, total-token and wall-time limits still apply.

`AssistantNetworkProvider` now owns the asynchronous lifecycle shared with
OpenAI: consent through the task, revision/deadline polling, cancellation,
manual redirects, bounded replies and task-owned retries. OpenAI retains its
fixed HTTPS endpoint and 30-second request limit. Ollama uses a 120-second
request limit; task wall time is separately bounded (300 seconds in the corpus).
Successful preflights are retained across retries. No transport retries a tool
receipt or commit. A dropped provider connection cannot publish a draft or erase
a committed edit. Manual modeling and saving remain independent of this service.

The corrected CPU corpus exhausted its five-minute budget on all four live tasks;
no measurement, room or resize quality pass was obtained. This profile is
experimental, not an M5 quality-gate success. The stopped-endpoint case preserved
manual editing and saving. See the retained verification summary for exact results.

Images are unsupported in this profile: the host sends text and structured
inspection data only, with no screenshot or image-asset attachment. Hardware
admission does not promise throughput or quality. The measured CPU deployment
has an explicit 10 GiB container memory limit and no GPU; the app neither starts
that container nor changes desktop/system settings. Other GPUs, quantizations,
runtimes and larger contexts need their own evidence before becoming supported
profiles. Dependency/runtime/model licenses remain their own.

Sources: [Ollama tool calling](https://docs.ollama.com/capabilities/tool-calling),
[pinned request controls](https://github.com/ollama/ollama/blob/v0.35.1/api/types.go),
[pinned prompt handling](https://github.com/ollama/ollama/blob/v0.35.1/server/prompt.go),
[CPU Docker setup](https://docs.ollama.com/docker),
[Qwen instruction model](https://huggingface.co/Qwen/Qwen3-4B-Instruct-2507).
