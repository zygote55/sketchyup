# R046.e — ChatGPT subscription connection

Date: 2026-10-05. Implementation and local verification complete; live account
sign-in/inference acceptance pending. This does not close R044/R046/R051 or M5.

The user requested their Codex subscription instead of separate API billing.
The native assistant now offers OpenAI's documented Sign in with ChatGPT flow
for eligible plan usage. Preferences includes separate ChatGPT/API-key choices,
browser sign-in, saved account/workspace registrations, an account-specific model
picker, sign-out and Manage ChatGPT usage. Session credentials use Linux Secret
Service; existing API keys retain their separate record.

## Checks

- Full development build; 63 enabled CTest suites passed (64 registered, the
  explicit real-Blender test skipped), 39.76 seconds. Final focused authentication
  and OpenAI provider checks passed after session-lock/cancellation refinements.
- Disposable authentication tests cover a real loopback callback and RSA-signed
  ID-token fixtures: PKCE/exact redirect/form encoding, incorrect state, issued
  client IDs, fresh nonce/state, stable host identity, cross-window locking,
  signature/issuer/audience/time/nonce failures, selected-account identity,
  multiple registrations with the same email, missing plan permission,
  catalog filtering, refresh rotation before use, terminal refresh rejection,
  local credential isolation and successful/unconfirmed revocation. Tests assert
  their preferences path is inside a disposable directory before any operation.
- OpenAI protocol tests exercise both API and subscription variants through the
  real inspection → staged edit → sealed preview → host Apply engine. They verify
  namespace authorization, opaque history replay, no unsupported generation cap,
  terminal-only SSE acceptance, partial/failed/incomplete streams, duplicate
  terminal events, stream byte limits and plan-limit guidance. No real tokens or
  live inference were used.
- Native assistant acceptance passed in isolated X11 DPR 1 and Wayland DPR 1/2,
  including the new authentication selector, private API-key field visibility,
  no setup-time network activity, existing consent, preview/direct one-entry
  Undo, clarification, stale-model guards, reconciliation and responsive layouts.
- ASan/UBSan authentication and OpenAI suites passed, 9.23 seconds. Native panel
  ASan/UBSan also passed under isolated Wayland with leak checking enabled. The
  previously documented Qt Wayland decoration workaround was applied only to
  that sanitizer run.
- Source archive generation and staged installation passed. OpenSSL 3 is an
  explicit provider build/runtime dependency. CI includes the new authentication
  suite in both ordinary and sanitizer checks.

Retained local artifacts: `build/evidence/r046e/` (ignored by Git), including
CTest logs, sanitizer logs, X11 panel images, source package and staged install.

## Remaining acceptance

The test binary is `build/dev/sketchyup`. Relaunch it, open Ctrl+J → Preferences,
choose OpenAI → ChatGPT subscription → Continue with ChatGPT, complete browser
sign-in, select a listed model and Save. The picker prefers `gpt-6.1-sol` only if
it appears in the account's catalog; otherwise it preserves server order. No
account entitlement or successful inference is inferred from documentation.

`provider_trial --chatgpt configured ...` can then run the synthetic live corpus.
OAuth and model-catalog traffic are excluded from its evidence recorder. A live
completed response, host receipts, independent geometry measurements and the
remaining native acceptance workflow are still required.

See [contract](../decisions/0041-chatgpt-plan.md) and
[acceptance guide](../M5_ACCEPTANCE.md).
