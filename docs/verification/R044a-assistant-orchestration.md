# R044.a provider-neutral task engine

Date: 2026-10-05 UTC. Local regression/sanitizer verification passed; CI/merge
acceptance pending. Requires [PR #67](https://github.com/zygote55/sketchyup/pull/67).
[Orchestration contract](../decisions/0030-assistant-orchestration.md).

Scripted provider completions drive the real headless session and transaction
coordinator. The fixture creates a 2 × 3 m face privately, measures its staged area
as 6 m², seals a preview and verifies that the live revision is still zero. Only a
host Apply commits revision one. The resulting measurement is still 6 m², history
contains one assistant task with the original user request, and repeated Apply or
late cancellation cannot create another edit.

A text-only model claim that it built and saved a house leaves `applied=false` and
the model unchanged. Ending after a private edit without sealing a preview
explicitly fails and discards staging. Cancellation before or after sealing preserves the live model.
A manual units edit while a provider request is outstanding makes its reply stale;
a manual edit after sealing prevents Apply. Both preserve the human edit.

A real outcome-store fault throws after checkpoint rename during commit. The task
reports an unknown outcome and `applied=null`; cancellation cannot invent an abort.
Verified reconciliation publishes the original result once and preserves its undo
entry. This is engine fault-injection evidence, not a simulated provider's assertion
that reconciliation succeeded.

Other fixtures cover unadvertised save/shell tools, foreign documents, another
transaction's draft, commands outside the trusted allowlist, forged history and
mixed operation/query discriminators. Host-selected context contains a body name
that requests credential access and shell execution: the text remains data, the
system instructions remain separate, and an attempted shell tool is rejected.
This verifies authority boundaries, not live-model prompt-injection reliability.

A remote profile without a trusted context grant cannot construct a provider
request. Bounded backoff, superseded attempt IDs, late replies after cancellation,
duplicate tool-call IDs, deep arguments, response/token/turn/tool-count limits and
transcript preflight are exercised. Polling expires a provider that never replies.
Wrong-thread calls and reentrant backend callbacks cannot mutate task state.

All 56 development CTest suites passed. The assistant, transaction dispatcher and
coordinator suites passed ASan/UBSan. Final foreign-document classification and
cancellation-during-uncertainty checks are also run in the assistant suite normally
and under sanitizers. No compiler warnings were reported.

R044 remains incomplete: a remote provider has not been chosen or configured, OS
credential storage and transport translation are not implemented in this slice,
and no live provider trial or natural-language success rate is claimed. Native
preview/apply UI and direct mode remain R046 work. Manual modeling remains usable
without a provider; the task engine introduces no startup service or network call.

The local installation includes the orchestration decision record, and the source
archive includes the task engine and deterministic fixtures.
