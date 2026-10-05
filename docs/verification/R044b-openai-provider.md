# R044.b OpenAI provider verification

Date: 2026-10-05 UTC. Local implementation fixture acceptance passed; CI/merge acceptance pending.
**R044 remains open for credential setup and a recorded live provider trial.**
No real credential was accessed and no live/paid API request was made during these
checks. User selected OpenAI; credentials may be configured later.
[Adapter and credential contract](../decisions/0034-openai-provider.md).

The QtNetwork fixture implements asynchronous reply objects and checks the fixed
HTTPS endpoint, authorization header, disabled redirects, stateless request
settings and absence of the fixture key from request JSON. It drives the real
AutomationSession/AssistantTask through document inspection, draft creation,
ordinary face creation and sealed preview. Four provider turns stop at preview;
only explicit host Apply publishes the edit. Late cancellation preserves the
committed receipt. Three earlier opaque reasoning items survive full replay.

Other fixtures cover consent absent (zero HTTP requests), incorrect remote task
configuration, final text without applied-edit claims, HTTP 429/503/408 retries,
retry exhaustion, authentication failure, redirect rejection, invalid JSON,
incomplete output, missing usage, unknown function aliases, invalid arguments,
oversized bodies and parallel calls. Raw provider errors and the fixture key do
not appear in status or task results. In-flight cancellation, source revision
change, elapsed task deadline and destruction abort pending network replies.

A disposable helper subprocess verifies fixed credential attributes, lookup,
exact stdin bytes for storage, clear, missing helper/key, failed helper, oversized
output and cancellation. No desktop keyring was queried or modified. This proves
the process contract, not an actual unlocked Secret Service round trip. The
Preferences integration and real desktop credential setup remain R046 work.

These fixtures establish protocol mapping and failure behavior, not model quality
or account/model availability. A live transcript must still record selected model,
API behavior, usage, inspected geometry, actual tool receipts, preview/apply and
unsupported claims before the parent R044 acceptance can close.

The full development run passed in 35.95 s: 60 suites passed, with the explicit real-Blender test skipped without its opt-in environment. The provider suite passed again in 2.11 s after adding closed-document exception handling. Final targeted ASan/UBSan with leak detection passed in 4.93 s; no checks were disabled.

The source archive includes provider implementation, credential helper, tests and ADR 0034; installation passed. The optional libsecret dependency is declared without making assistant configuration mandatory for modeling.
