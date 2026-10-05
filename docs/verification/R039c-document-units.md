# R039.c: document units data, API and persistence

Date: 2026-10-04. Local and CI validation passed; merged. Builds on
[PR #53](https://github.com/zygote55/sketchyup/pull/53).

The [units decision](../decisions/0018-document-units.md) separates canonical
meter geometry from document display/entry preferences. Core tests cover clean
initial units, geometry-preserving changes, undo/redo saved markers, amendment,
stale and invalid rejection, and shared-definition scope boundaries. IO/API tests
cover all three preferences through native/raw save/reopen, captured snapshots,
verified recovery, v11 migration, previews and atomic batch failures.

Native first-run/default-unit selection and measurement controls are the next
slice. This record does not mark R039 or the M4 gate complete.

The full development suite passes 44/44 and the core sanitizer suite passes
30/30. The catalog's 61 public commands each have executable validation cases.
The complete v11 golden model migrates with its component/asset records intact.
The new units recipe is included in headless CI.

Native X11 History, shared component and recovery regressions pass. A CLI-created
millimeter document reopens with `displayUnits: mm` and canonical `units: m`.

[PR #54](https://github.com/zygote55/sketchyup/pull/54) merged on 2026-10-04 as `de065cd` after both Native build runs passed: [37241019986](https://github.com/zygote55/sketchyup/actions/runs/37241019986), [37241017877](https://github.com/zygote55/sketchyup/actions/runs/37241017877).
