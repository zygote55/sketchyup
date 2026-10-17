# 0041 — ChatGPT plan authentication

Status: implemented; live sign-in and inference verified with gpt-6-astra.
Full native M5 acceptance is tracked separately.

The user chose a Codex/ChatGPT subscription for the first remote assistant trial.
SketchyUp offers OpenAI's documented Sign in with ChatGPT flow for open-source
apps alongside the existing, separately billed API-key connection. It does not
read Codex's private credential files or call ChatGPT backend endpoints.

## Registration and credentials

Continue with ChatGPT opens the system browser after binding an ephemeral IPv4
loopback listener at `/auth/callback`. Initial requests use dynamic_agent_client,
a stable host UUID, SketchyUp's name, cryptographically random state and nonce,
and PKCE S256. An issued client ID is required for code exchange. Returning
registrations reuse that ID and the host ID. Redirect URI is exact within the
attempt. Callbacks are bounded, one-use, state checked, and expire after ten
minutes; individual incomplete sockets expire after ten seconds. Token exchange
uses form POST over HTTPS with redirects disabled and a 30-second request timeout.

OpenSSL 3 verifies RS256 signatures against OpenAI's fixed HTTPS JWKS endpoint.
Issuer, audience/authorized party, expiration, issued-at, optional not-before,
nonce and nonempty subject are checked before identity is trusted. Returning
identity must match the selected registration. A validated registration is retained when plan permission is declined; an
explicit subsequent sign-in requests consent again. Inference requires the granted
plan-usage and resource scopes; identity alone is insufficient.

Public account metadata and a stable host ID live in QSettings. Access, refresh
and retained ID tokens live only in the OS Secret Service under a distinct
ChatGPT/provider + issued-client/account record. The existing API key record is
separate. Secret Service receives a bounded base64url JSON record through stdin,
never argv. There is no plaintext credential fallback. Buffers are best-effort
cleared; Qt/OpenSSL copies do not offer a secure-memory guarantee.

A process lock at a fixed application data path serializes credential
lookup/rotation/sign-in across native windows and the opt-in trial executable. Before an assistant task, refresh if fewer than 930 seconds remain, then
persist the rotated token set before releasing a credential for inference. Native
tasks are bounded to 900 seconds. After model discovery, a credential must still
cover the whole task before it is released for inference. A failed renewal requires another explicit
connection attempt; no API-key or local-provider fallback occurs. Sign-out
attempts renewable-session revocation, clears only the selected credential, and
retains its public registration for later sign-in. Unconfirmed remote revocation
is disclosed with a route to ChatGPT Settings.

## Inference and interface

Account-specific models come from `GET https://api.openai.com/v1/models` using
the same OAuth session. Only `visibility: list` choices are shown, in server order;
the selected slug is rechecked before sending a prompt. Preferences permits
multiple account/workspace registrations even with identical email addresses.
Continue with ChatGPT, a first-use plan confirmation, Using ChatGPT plan and
Manage ChatGPT usage distinguish this path from API billing.

Responses use `https://api.openai.com/v1/responses`, store:false, stream:true,
full bounded input history, developer instructions and a SketchyUp function
namespace. Unsupported max_output_tokens is omitted. The stream is buffered
within the existing 1 MiB response cap and decoded only after response.completed;
partial calls never execute. Completed `response.output_item.done` items are
assembled by output index when the terminal envelope has an empty output array,
as observed in the live ChatGPT plan stream. Item identities, contiguous indexes,
completion and any nonempty terminal output must agree. Failed, incomplete,
interrupted or conflicting streams fail closed.
Known plan-usage errors point to Manage ChatGPT usage. ChatGPT plan replies use
the host's 524,288-total-reported-token, 16-turn and fifteen-minute native task
budgets, without a separate per-response token rejection. The unsupported
generation cap cannot constrain these replies, and reported output includes
hidden reasoning and formatting tokens. All reported usage still counts toward
the aggregate budget, and response byte and tool-call limits remain enforced
before dispatch. API-key and local connections retain their per-response caps.
Budget failures identify the exhausted limit and measured usage.
The native panel and native workflow trial use one shared limit profile. The previous
262,144-token profile exhausted its allowance during a fence-and-gate request:
the rejected reply reported 23,034 input plus 163 output tokens with only 1,339
remaining. Repeated schema and transcript input must fit across the allowed
16 turns. A deterministic streamed-provider regression exercises all 16 turns
at that input usage, including larger reasoning replies, and reaches preview
and commit using 430,337 reported tokens. The old profile rejects that sequence
and discards its staged edit. This regression does not establish live fence
modeling quality; earlier frozen-corpus results retain their original limits.
Total tokens include cached input;
the initial 131,072 limit stopped the measured window workflow before preview.
Preferences discloses the revised limits. The initial development corpus used a separate 12-turn / 131,072-total profile;
the frozen headless corpus retains its public five-minute draft ceiling and
records that override explicitly; native workflow trials use the native limits;
these are acceptance limits, not a server-side generation-token cap. Existing
consent, tool authorization, immutable preview, sealed Apply and one-entry Undo
remain authoritative.

The opt-in `provider_trial --chatgpt configured ...` uses the selected native
account. OAuth/model-catalog traffic uses a separate network manager and never
enters acceptance evidence. Successful completed inference and host receipts
remain necessary for the live milestone gate.

The opt-in `assistant_workflow_trial MODEL PROMPT.txt NEW_REPORT.json` reproduces
native requests on an empty disposable document with the panel's command allowlist
and subscription limits. It retains a private activity report and saves successful
results as `NEW_REPORT.json.files/result.sketchyup`; `--inspect MODEL.sketchyup`
reports saved world bounds without making a provider request. It is excluded from
the default build and CTest.

On 2026-10-16, the live `gpt-6-astra` request for a 16-foot by 6-foot modern fence
with a centered 4-foot gate reached Apply on the baseline's last allowed turn:
16 turns, 295,885 ms and 380,620 reported tokens. Individual topology lookups for
newly created profiles consumed much of that budget. Face-creation receipts now
return surviving face references, the native allowlist includes the existing array
command, and instructions explain batching and reserving turns for measurements.
The updated live run completed in 12 turns, 222,429 ms and 292,889 reported tokens.
Reloading its saved document verified 4.8768 m overall width, 1.8288 m height and
a centered 1.2192 m clear gate opening; the gate leaf is 47 inches wide with half-inch
side clearances. That run recovered from one rejected array target; the assistant
instructions now explicitly require entity `"0"` for whole-body context targets.
The timings are individual observations, not a latency or success-rate guarantee.
A native regression also creates fourteen profiles, extrudes them using returned
face references, arrays eleven copies and reaches preview in five turns; Apply
creates one history item and Undo restores the original document.

A subsequent native fence request still exhausted the five-minute wall-clock
deadline. A successful individual run was insufficient evidence that this latency
budget was suitable. Native ChatGPT tasks now allow fifteen minutes with the same
turn, token, command and memory bounds. The model receives remaining wall-clock
seconds, and expiry reports the configured duration and usage. Native in-process
staging and transaction dispatch accept a host-configured ceiling up to 900 seconds;
the public defaults and published API schemas retain their 300-second ceiling.
The draft's lifetime never exceeds the original task deadline. Sealing grants at
most five minutes of review, also capped by the remaining draft lifetime; durable
outcome leases and their stored format remain unchanged.

Injected-clock native tests inspect a draft after 350 seconds, seal it after 400,
and apply after 650 with one-entry Undo. Separate cases verify exact task expiry
at 900 seconds, sealed review expiry at 700 seconds, cancellation and stale-model
rejection without publishing the draft. Authentication tests renew credentials
with ten minutes left before a fifteen-minute task and reject a renewal whose
lifetime is still insufficient.

The final live fence run with the extended profile completed and applied in
11 turns, 176,305 ms and 280,341 reported tokens without a rejected tool call.
Reloaded geometry again measured 16 feet overall, 6 feet high and a centered
4-foot clear gate opening. This run finished within the former deadline; the
injected-clock cases, rather than that timing, establish behavior beyond it.

## Release corpora from version 4

By owner decision on 2026-10-17, release corpora from
[version 4](../verification/R086e-corpus-v4.md) (SketchyUp 1.2.0) run every task through
the native assistant session: `NativeAssistantSession` with
`nativeAssistantTransactionOptions()`, the shared ChatGPT-plan limit profile and its
fifteen-minute task lifetime (`taskSeconds: 900`). The earlier statement that the frozen
headless corpus keeps its public five-minute draft ceiling applies to versions 1 to 3
only. The headless session's public 300-second ceiling, its defaults and the published
API schemas are unchanged.

## Sources

Verified 2026-10-05 against official OpenAI documentation:

- [Registration and sign-in](https://developers.openai.com/siwc/token-sharing-open-source/sign-in)
- [Accounts and sessions](https://developers.openai.com/siwc/token-sharing-open-source/profiles-and-sessions)
- [Models and inference](https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference)
- [Preview limitations](https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations)
- [Identity verification](https://developers.openai.com/siwc/website)
- [UI guidance](https://developers.openai.com/siwc/ui-ux-guidelines)

- [Completed streaming items](https://developers.openai.com/api/reference/resources/responses/streaming-events)
