# R083.d — Integrated failure-path matrix

2026-10-07. Integration `2dcb1e8`; parent [PR #206](https://github.com/zygote55/sketchyup/pull/206).

The integrated [13-suite regression](R083d-integrated-ctest.txt) passes **13/13 in
28.15 s** across persistence, recovery, geometry, parser corruption and transaction
failure paths. The [retained local matrix](R083d-local-failure-matrix.md) includes
all 13 suites under ASan/UBSan (**296.33 s**) and an additional **3,072-attempt**
geometry corpus with identical normal/sanitizer outcomes.

The queue-test atomic readiness fix and completed-build CI cleanup are already
present in the parent stack. Integration retains the portable `df -h .` correction
and every existing milestone gate. This layer adds verification records and
roadmap status only; application/test/build/package inputs are unchanged from
PR #206. Its installed/source-package checks therefore remain applicable.
Remote CI, ordered prerequisite merges, continuous fuzzing, release-candidate
revalidation and the final data-loss audit remain open; this does not accept R083/M9.
