# R051.a alpha acceptance preparation

Date: 2026-10-05 UTC. Deterministic local acceptance passed; CI pending.
**M5 is still open. No live OpenAI result is claimed.**
[Reproduction procedure](../M5_ACCEPTANCE.md),
[measured offline summary](R051-offline-summary.json).

`scripts/verify-m5-offline.sh` runs the assistant, native bridge, both provider
protocol fixtures, coordinator/dispatch, session/recipe/MCP, persistence, Blender
worker and GLB suites. It also runs the independently measured room/window suite,
retains exact before/after native documents, executes the public CLI recipe and
exports the widened model. The native manual workflow, assistant fixture and
render workflow run in separately owned X servers with recordings. The runner
never reads real credentials or calls a provider.

All phases pass. The manual workflow covers actual native drawing, through
openings, component/context operations, history, save/reopen and killed-writer
recovery. It caught the nonstandard close-dialog dismissal regression corrected
in R046.d before merge. The assistant fixture covers consent, measured preview,
keyboard Apply, direct mode, one-entry Undo, clarification, stale human edits,
Discard and uncertain-outcome fencing/reconciliation. The room recipe independently
checks the 6 × 4 × 2.7 m room, 200 mm walls, 1.2→1.4 m outer-frame width, unchanged
80 mm members/center/sill/sibling records, eight 100 mm jamb moves, wall volumes
9.888→9.848 m³ and exact undo/redo/save/reopen.

The render test reopens the persisted widened fixture. Actual **Blender 5.2.2
LTS**, CPU, 512 × 512, 32 samples renders its current native camera. Manual editing
continues while the immutable snapshot renders. The verified result manifest
matches the fixture's document identity and revision 2; the result tab labels the
later model change. Its PNG and native result screenshot were visually inspected.
The three MP4 recordings fully decode (2.07 s manual, 4.53 s assistant fixture,
6.53 s render). These are fast automated native-event recordings, not real-user
input coverage or evidence of live natural-language modeling.

The final report retains hashes for 296 source/code files and every output
artifact, and explicitly records `liveProviderTested: false`, `m5GatePassed: false`.
Git is absent in the isolated container, so the report's Git fields are null;
its exact code-file hashes provide the source inventory. The committed summary
identifies the working source base and hashes that inventory. Full local evidence
is under `build/evidence/r051/verified/`, copied out of the container before it can
be removed. CI now runs the same command and uploads its evidence directory.

The `provider_trial` executable extends the prior opt-in corpus to the chosen
OpenAI adapter. It requires an explicit model or the native configured model,
reads the OS credential only when explicitly invoked with `--openai`, and retains
private synthetic baseline/result/journal files alongside the report. Requests
omit authorization headers from evidence; HTTP error bodies are omitted because
authentication errors can echo credential fragments. The completed-measurement
exit status remains distinct from model correctness. Area verification now checks
the requested body and world space, not just an arbitrary matching numeric value.

The stopped-loopback trial passes: three failed connection attempts, known
`applied: false`, unchanged model and successful manual edit/save/reopen. Reports
have mode 0600 and retained fixture directories 0700. Invalid remote model IDs are
rejected before credential lookup, and existing evidence is not overwritten.
No real OpenAI credential facility was accessed for these checks.

Source packaging and installation pass. Both installed native examples reopen
with nine bodies/two instances; the baseline has one shared definition and the
widened result has two. The archive includes the runner scripts, fixtures and
procedure; the installed package includes the procedure and integration contracts.

The remaining gate needs configured OpenAI credentials/model, repeated live
measure/room/resize/unsupported results, and a recorded live native
selection→preview/direct→undo/redo→save/reopen→render sequence. The manually
authored target without recipe bindings must be assessed explicitly; the authored
fixture does not prove general window recognition. Failure of a required live
case keeps M5 open. No M6 acceptance is claimed.
