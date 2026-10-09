# Trusted assistant draft state

Live local evaluations repeatedly tried to inspect an empty draft and supplied a public document revision as expectedVersion. The dispatcher rejected those calls correctly, but the provider repeatedly retried them or abandoned the task.

The host now includes the owned draft's currentVersion, commandCount and, after accepted commands, proposedRevision in its trusted task-budget envelope. These values come from successful backend begin/apply/describe receipts. An earlier apply replay cannot replace the latest proposal metadata with the earlier receipt's fields. Abort and replacement retire the prior metadata. Instructions distinguish the public revision from the private integer version and direct reads of the original model through public inspection tools. Rejected empty-draft and stage-version calls provide explicit retry guidance.

Arguments, dispatch, authorization, public-revision guards, per-reply/task budgets, preview sealing and explicit host Apply are unchanged. The host never fills or rewrites provider arguments, creates commands to make an empty draft inspectable, or applies an edit on a provider's prose claim.

All 14 normal and 14 ASan/UBSan suites pass with leak detection. The new real-actor regression rejects empty inspection and a wrong stage version without changing public bytes, accepts later valid private edits, preserves the latest metadata through an earlier replay, clears abort state, starts a replacement at version zero and publishes exactly one history entry only on host Apply. The parent source fails the new draft-state assertion. The [result](R086ae-draft-state-result.json), [prospective input freeze](R086ae-draft-state-freeze.json), [regressions](R086ae-draft-state-regressions.txt), [parent failure](R086ae-draft-state-parent-failure.txt) and [controller](R086ae-draft-state-controller.txt) retain the evidence.

All 477 public runtime and 301 test inputs in this publication match the qualified candidate. Private local transport/runtime profile changes are separate and are not promoted here. Both provider corpora require new evaluations on this host source; earlier remote success is not relabeled as a result for this candidate. Fresh full CI, the local quality gate, R086/M9 and release acceptance remain open.

The subsequent [fresh remote evaluation](R086ah-remote-draft-state.md) fails the advanced-assembly capability threshold. This candidate remains unaccepted.
