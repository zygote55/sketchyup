# R082.u — Prepare only a new component placement

Adding an instance previously copied all scene bodies and instance bindings and
reprojected existing placements before constructing the transaction. The new path
expands just the new instance. It retains definition and aggregate-size preflight,
the same ID allocation, and complete candidate validation through `Document::apply`.
Shared-definition edits continue to use the existing full publication path.
Command validation also reuses the immutable generated catalog instead of rebuilding
all descriptions for every nested recipe command. Returned descriptions detach on
caller mutation; tests check that such mutation cannot poison any authoritative
schema, and every cached entry equals the separately generated catalog. Unknown
commands remain rejected. Production document and native-file limits are unchanged.

Source `9ecc112155db51df19d7536f5ec8654a99fe037b` passes all 25 selected normal and all 25 ASan/UBSan
regressions with leak detection: components, placement, binding, nested scope,
hosted openings/copies, groups, transactions, recipes, persistence and recovery.
[Raw checks](R082u-placement-regressions.txt) and
[frozen inputs](R082u-incremental-component-placement.json) are retained.

The controlled constructor uses the same public creation calls, native IDs,
allocation order and Debug cache before and after the change. The document
bytes, body/instance counts and triangle counts match exactly.

| Instances | Before construction ms | After construction ms | Canonical fixture hash |
| ---: | ---: | ---: | --- |
| 25 | 103.918 | 95.434 | `97aadcff18317af573a28a7b03129f3f59f926c3ef36de255ae9832f0e36c405` |
| 1000 | 21602.014 | 12549.027 | `c874449f6143db01040c1b0df5f26dbc0f90daee66cd2635453eeac73c11376d` |

Construction excludes serialization, viewport drawing and presentation. These
measurements establish behavior preservation and constructor cost for the 25 and
1,000-instance controls; they do not establish million-triangle or release frame,
edit, inference or memory acceptance. R082 and M9 remain open.

The [first harness attempt](R082u-original-harness-failure.txt) stopped before
testing because CTest omits an unbuilt executable's command field. The corrected
harness resolves each unchanged case's exact CMake target and verifies available
CTest commands against that mapping. No case, assertion or timeout was removed.
Temporary cache source overlays were restored on both attempts.

The [prior default-sanitizer attempt](R082u-interrupted-sanitizer-attempt.txt)
completed all 25 normal cases and 11 sanitizer cases before severe anonymous-memory
reclamation in `model_recipes`. It was stopped without claiming a sanitizer pass.
A bounded read-only stack sample identified repeated full command-catalog generation;
the new catalog cache is qualified together with placement in this fresh attempt.
Sanitizer options, leak detection and all case assertions remain unchanged.

Validation uses up to eight CPUs, two compiler jobs, 4 GiB/no added swap and
disk-backed scratch. A retained mid-test observation shows the interrupted sanitizer worker
reached the 4 GiB cgroup limit with reclamation; it is not a whole-run OOM-counter
or process-peak report. No release memory budget follows from this operational cap.
