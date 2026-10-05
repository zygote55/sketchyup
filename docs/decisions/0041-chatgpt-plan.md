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
lookup/rotation/sign-in across native windows and the opt-in trial executable. Before an assistant task, refresh if fewer than 330 seconds remain, then
persist the rotated token set before releasing a credential for inference. Native
tasks are bounded to 300 seconds. A failed renewal requires another explicit
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
Known plan-usage errors point to Manage ChatGPT usage. Output is still checked
against the host's 8,192-token per-response, 262,144-total-reported-token,
16-turn and five-minute native task budgets. Total tokens include cached input;
the initial 131,072 limit stopped the measured window workflow before preview.
Preferences discloses the revised limits. The development corpus retains its
separate 12-turn / 131,072-total profile for the initial comparison;
these are acceptance limits, not a server-side generation-token cap. Existing
consent, tool authorization, immutable preview, sealed Apply and one-entry Undo
remain authoritative.

The opt-in `provider_trial --chatgpt configured ...` uses the selected native
account. OAuth/model-catalog traffic uses a separate network manager and never
enters acceptance evidence. Successful completed inference and host receipts
remain necessary for the live milestone gate.

## Sources

Verified 2026-10-05 against official OpenAI documentation:

- [Registration and sign-in](https://developers.openai.com/siwc/token-sharing-open-source/sign-in)
- [Accounts and sessions](https://developers.openai.com/siwc/token-sharing-open-source/profiles-and-sessions)
- [Models and inference](https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference)
- [Preview limitations](https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations)
- [Identity verification](https://developers.openai.com/siwc/website)
- [UI guidance](https://developers.openai.com/siwc/ui-ux-guidelines)

- [Completed streaming items](https://developers.openai.com/api/reference/resources/responses/streaming-events)
