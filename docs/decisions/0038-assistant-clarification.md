# ADR 0038: bounded clarification within an assistant task

A native clarification card needs a structured question and a host-owned answer.
Provider prose is not parsed into controls. Hosts explicitly opt in with
`clarificationAvailable`; existing headless/corpus hosts keep their existing tool
catalog. The added `assistant.ask_user` tool is task dialogue, not a document
operation, and is never forwarded to the transaction or inspection dispatcher.

A valid question has at most 1,024 characters, two to six choices with distinct
bounded IDs and plain-text labels, and an explicit free-text flag. The question
must be the only call in its provider response. Mixed question/mutation responses
fail before executing any call. Malformed or unadvertised questions get normal
bounded tool errors. The host adds an opaque clarification identity; a provider
cannot choose it or answer itself.

The task enters `awaiting-clarification` and emits no further provider request.
Its original monotonic deadline, tool/turn/token budgets, revision and private
staging expiry continue to apply. The transport keeps checking deadline and
revision without network traffic. Manual edits make the task stale; Stop retires
its private draft. A pending question never publishes geometry or resets limits.

Only the trusted host's `answer(id, choiceId, text)` resumes the task. The answer
must match the current identity and an offered choice or permitted nonblank text
of at most 1,024 characters. Invalid answers leave the question available. A valid
answer appends exactly one receipt matching the original provider tool-call ID,
clears the card and resumes the same task. Duplicate/late answers are rejected.
Answers do not alter command authorization, provider consent, document identity
or the preview/commit boundary. The adapters replay their accepted original
assistant response followed by this host-supplied tool receipt.

Native UI must render questions, labels and answers as plain text, provide Stop,
show expiration/staleness honestly and keep Apply unavailable while answering.
This engine change does not itself implement a panel or claim live model quality.
