# R051.b — Live ChatGPT plan acceptance

Date: 2026-10-05 UTC. User-connected OpenAI ChatGPT plan, exact selected model
**gpt-6-astra**. Sign-in, model entitlement and real tool inference work.
**M5 remains open:** the manually authored component target did not resize.
[Sanitized measurements and artifact hashes](R051b-live-summary.json).

## Compatibility correction

The first live measurement failed because the terminal `response.completed`
envelope contained an empty output array even though earlier completed item
events carried the tool call. The adapter now assembles `response.output_item.done`
items by index and only releases them after the whole response completes. It
rejects missing, duplicate, unfinished, conflicting and mismatched items or
response identities. Completed reasoning items are retained for bounded replay.
Protocol fixtures exercise this envelope across an actual staged-tool loop;
partial streams still cannot execute tools.

The native subscription task now allows 262,144 reported tokens, 16 turns and
five minutes, with an 8,192-token reply acceptance limit. Preferences shows the
limits. Repeated schemas and cached input count toward the cumulative budget.
The earlier 131,072-token native limit stopped a valid staged resize before
preview. These limits bound host acceptance, not provider billing or generation.

## Initial repeated corpus

All attempts are retained, including the pre-fix measurement. The CLI profile
used 12 turns / 131,072 total reported tokens / five minutes throughout this
initial corpus. A successful process exit is not used as a correctness grade.

| Task | Result after streaming correction | Reported tokens | Observation |
| --- | --- | --- | --- |
| Measure 6 m² face | 3/3 verified | 10,823; 6,768; 6,774 | Host measurement, no edit |
| Create room | 3/3 verified | 74,964; 74,393; 74,731 | Real staged geometry independently measured |
| Widen authored window | 1/3 verified | 40,203; 114,419; 103,728 | Incomplete stream; 12-turn budget; successful exact resize |
| Unsupported solid Boolean | 3/3 appropriate responses | 6,932; 6,927; 6,930 | Reviewed explanations, no false edit claim |

The first resize failure predates the added constant decoder diagnostic; its
precise incomplete-stream cause is unknown. The second aborted its private
stage at the turn limit. All corpus runs preserve a successful independent
manual edit/save/reopen check. No billing amount is inferred from token counts.

## Recorded native acceptance

`native_provider_trial` is an explicit opt-in developer executable, never a
CTest or installed app action. It uses the actual native panel, disclosure,
selection/context, preview/Apply or direct path, Undo/Redo, persistence and render
panel. It sends only synthetic fixtures. Public account metadata is copied into
disposable preferences; credentials stay in the OS facility and the user's
settings/model are untouched. Each output directory is private and must be new.

- Preview attempt 1 exposed a harness selection outside the editing context;
  the model correctly asked for clarification. The harness now enters the target's
  ancestor contexts and verifies selection before requesting the edit.
- Preview attempt 2 stopped at the former token limit, without live publication.
- **Preview attempt 3 passes:** 71.214 s, 10 turns/calls, 135,842 reported tokens.
- **Direct attempt 1 passes:** 69.558 s, 9 turns/calls, 120,330 reported tokens.
- **Manual target attempt 1 fails the modeling gate:** 46.435 s, 7 turns,
  6 calls, 84,558 reported tokens. The recipe rejects missing authored metadata;
  the assistant aborts the draft and accurately reports no resize.

Both passing native runs independently verify 1.4 m selected width, 1.2 m sibling
width, retained center/sill/height/depth and 80 mm frame members, unchanged unrelated
body records, and 9.848 m³ wall volume. Preview leaves the document unchanged until
Apply. Each AI edit is one history entry; Undo, Redo and save/reopen restore the
expected model contents. Root allocator high-water marks are excluded from the
Undo comparison because stable IDs are deliberately never reused.

Actual host Blender **5.2.1 LTS**, CPU, 512² and 32 samples renders the reopened
model while another manual edit occurs. The verified manifest identifies the
pre-edit revision. Native model and render screenshots were inspected. Recordings
capture only an owned, authenticated X server; all retained MP4s fully decode.
An initial isolated-display startup abort was caused by root-owned Xauthority,
corrected before testing; it did not involve the user's running application.

The manual fixture is made through real native rectangle, face erasure, push/pull,
component and material operations, with two windows and no recipe metadata.
`m4_workflow_tests --baseline PATH` preserves it before the separate depth edit.
This case requires an ordinary component-member edit while preserving its sibling;
recipe-only success does not satisfy that requirement. The next slice must expose
and validate that scope without granting shared-definition edit permission.

Raw transcripts, native models, journals, screenshots and recordings remain in
`build/evidence/r051-live-astra/`; only sanitized results and hashes are committed.
Raw inference envelopes can contain account correlation metadata, so they are
not publication artifacts. OAuth traffic and authorization headers are excluded.

## Reproduction

After explicit native Preferences sign-in, on an isolated graphical display:

```sh
build/dev/native_provider_trial --chatgpt gpt-6-astra preview examples/m5-room-before.sketchyup /absolute/new/preview
build/dev/native_provider_trial --chatgpt gpt-6-astra direct examples/m5-room-before.sketchyup /absolute/new/direct
build/dev/native_provider_trial --chatgpt gpt-6-astra manual /absolute/manual-before.sketchyup /absolute/new/manual
```

The native trial currently requires `/usr/bin/blender` for its final real render.
It returns nonzero if any required modeling/workflow check fails. It saves the
applied model before running the independent oracle, preserving failed results
for diagnosis. Follow [the acceptance procedure](../M5_ACCEPTANCE.md) for corpus,
failure injection and package evidence. This slice does not close M5 or start M6.

## Regression checks

All 63 enabled development CTest suites pass (39.69 s; the separate opt-in real
Blender CTest is skipped). The live native runs above independently exercised the
real renderer. The expanded OpenAI stream/staged-tool suite passes with ASan,
UBSan and leak detection. Native panel acceptance passes on X11 DPR 1 and
Wayland DPR 2, including setup, consent, clarification, preview/direct Undo,
staleness, responsive layouts and uncertain-outcome reconciliation. The opt-in
native runner builds with an explicit model argument. PR CI remains pending.
