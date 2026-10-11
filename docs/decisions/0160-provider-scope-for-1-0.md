# Provider scope for 1.0

Date: 2026-10-11. Owner decision on scope row [A05](../SCOPE.md). R086.

## Decision

The project owner decided on 2026-10-11: "I don't think local model needs to be a
requirement of this. Users can connect to codex already, right?"

A passing local model configuration is **not a 1.0 requirement**. The supported
provider for 1.0 is the remote OpenAI connection through a ChatGPT plan sign-in.
Local configurations stay available and documented as **experimental**.

This narrows a Core row by explicit owner decision. It is not a silent change:
A05 stays Core, and its acceptance text is rewritten below.

## How the remote connection works

The owner's premise holds, with one precision. **Continue with ChatGPT** in the
assistant preferences uses OpenAI's documented Sign in with ChatGPT flow for
open-source apps ([ADR 0041](0041-chatgpt-plan.md)). It signs in to the same
ChatGPT subscription that includes Codex, and the assistant shows "Using ChatGPT
plan" while it is active. It is SketchyUp's own browser sign-in with PKCE, OS
credential storage and a ChatGPT usage link. It does not use the Codex tool, and
it does not read Codex's credential files. A separately billed OpenAI API-key
connection also exists, but the frozen corpus was passed with the ChatGPT plan
sign-in, so that is the profile this decision names.

## Evidence

- Remote: the OpenAI ChatGPT-plan profile (gpt-6-astra) passes the frozen corpus,
  24 of 24 reviewed checks ([R086.b](../verification/R086b-remote-evaluation.md)),
  and again on a later candidate ([R086.as](../verification/R086as-openai-inspection-batches.md)).
- Local: the frozen Ollama `qwen3:4b-instruct` profile scores 0/3 on all seven live
  tasks ([R086.c](../verification/R086c-local-evaluation.md)). The later local
  guidance and Qwen3-Coder campaigns also fail ([R086.ak](../verification/R086ak-remote-local-guidance.md)).
  The triage list classed this as release-blocking defect D-001
  ([R089.a](../verification/R089a-defect-triage.md)).

## New A05 wording

Before: "At least one remote provider and one documented local configuration pass a
published capability/evaluation suite; credentials stay outside documents."

After: "At least one remote provider passes a published capability/evaluation suite;
credentials stay outside documents. Local configurations are documented and
experimental, and are not a 1.0 gate."

## What remains

- The remote profile must still pass the frozen corpus at the release candidate
  under M9 R086, with the same hard safety blockers as before.
- Local profiles stay available and are labeled experimental in the app, the
  [support matrix](../SUPPORT_MATRIX.md) and the [user guide](../USER_GUIDE.md).
  They carry no quality guarantee, automatic download or cloud fallback.
- The earlier local evaluations stay recorded and are not edited or removed.
  Statements in them that A05 needs an accepted local configuration are
  superseded by this decision.
- D-001 becomes a documented limitation. Any future local profile that passes may
  be promoted by a later decision.
