# R039.a: history data and navigation

Date: 2026-10-04. Local and CI validation passed; merged. Requires
[PR #51](https://github.com/zygote55/sketchyup/pull/51).

The [history decision](../decisions/0017-labeled-history.md) records bounds,
provenance semantics, atomic navigation and the separate public control API.

Focused core/API tests pass. They cover multi-step backward/forward navigation,
agreement with ordinary undo/redo, saved-content markers, stale/unavailable target
rejection, revision overflow before mutation, redo-branch removal, bounded pages,
10,000-entry pruning and the reachable retained baseline. Numeric amendment
preserves the original label and assistant request/task metadata as one step.
Invalid labels/metadata reject without editing the model. Preview publishes no
history. Reopening a saved model fabricates no history or stored task prompts.

The full development suite passes 42/42 and the core sanitizer suite passes 29/29,
including the retained-history stress fixture. CLI navigation/query cases pass.
Existing native X11 numeric editing, shared components and recovery regressions
pass. Native history controls and M4 integrated acceptance remain the next slices;
this record does not mark R039 or M4 complete.

CI run 37238923333 failed at the older DPR 2 Wayland pointer-scale test's
exact floating-point equality assertion. Ten consecutive local DPR 2 Wayland
runs passed, so the original discrepancy was not reproduced or measured. The
assertion now uses the model's geometric tolerance (as its adjacent rotation
check does) and prints all coordinates at full double precision on failure.
This does not change modeling behavior; subsequent CI must validate the result.

[PR #52](https://github.com/zygote55/sketchyup/pull/52) merged on 2026-10-04 as `a7d2fc6` after both Native build runs passed: [37239903246](https://github.com/zygote55/sketchyup/actions/runs/37239903246), [37239900355](https://github.com/zygote55/sketchyup/actions/runs/37239900355).
