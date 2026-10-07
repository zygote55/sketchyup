# CI package capacity and atomic test readiness

2026-10-07. Correction implementation `240b19f`.

[The package run](https://github.com/zygote55/sketchyup/actions/runs/37559897157)
exhausted runner disk space while linking its full Release test executables after
Debug and ASan builds had completed. A new step immediately before the package
rebuild retains both CTest log trees and removes only the completed `build/dev`
and `build/sanitize` artifacts. All previous steps and push/pull-request triggers
are unchanged. The package still builds and checks its full test suite and exercises
installation, upgrade and removal. A disposable filesystem fixture verifies that
cleanup preserves CTest logs and unrelated evidence.

[The sanitizer run](https://github.com/zygote55/sketchyup/actions/runs/37569486898)
failed `render_queue` at `Valid worker PID`. Its child opened the readiness proof
file before writing JSON, while the parent treated existence as completion. The
fixture now uses QSaveFile to publish the complete proof atomically. Application
queue behavior and the forced-termination assertions are unchanged. Ten consecutive
normal runs pass in **6.27 s**, and ten ASan/UBSan runs pass in **23.96 s** with
leak detection and halt-on-error. This targets the observed readiness race.

Original feature verification files retain their tested source identities and
archive hashes. Each updated PR carries a separate `CI-PR<N>-source-package.json`
for the corrected source snapshot. Remote CI must pass again before merging.
