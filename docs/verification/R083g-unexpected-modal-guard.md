# R083.g unexpected-modal guard for file-operation tests

2026-10-10. `tests/file_operation_input_tests.cpp` now fails within about a second,
with the dialog's text on stderr, when any modal dialog the test did not drive
appears. Before this change a failed save raised the app's modal "Save failed"
`QMessageBox` (`src/app/window.cpp`), nobody answered it, and the test sat in
`QDialog::exec()` until the external 60 s/90 s timeout killed it with no output
(root cause: R083.f, PR #281, `docs/verification/R083f-file-worker-hang.md`).

## Mechanism

- A 50 ms `QTimer` (`UnexpectedModalGuard`) polls `QApplication::activeModalWidget()`
  for the whole run. The progress popup (`fileOperationDialog`) is always expected;
  the "Unsaved changes" prompt is expected only while the test triggers
  `file.new` (`expectUnsavedPrompt`), where `chooseSave` answers it.
- Anything else prints its class, window title and text (`text()` and
  `informativeText()` for a `QMessageBox`, label texts for other dialogs), is
  rejected so the nested event loop unwinds, and makes every following `check()`
  throw that text, so the test exits 1 with the dialog as the failure reason.
- No `src/` change. The guard is local to this test.

## Fault injection

`SKETCHYUP_TEST_INJECT_SAVE_FAILURE=1` removes write permission from the test's
private temporary directory (restored on exit) just before the first save, so the
real save path fails with `Permission denied` and raises the real "Save failed"
dialog. It is off by default and does not touch any user file.

## Commands

Run from the repository root exactly as `.github/workflows/native.yml` does, inside
the pinned Arch CI image (the host has no `xvfb-run` or `weston`):

```sh
xvfb-run -a env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 timeout 90s build/dev/file_operation_input_tests
xvfb-run -a env QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=2 LIBGL_ALWAYS_SOFTWARE=1 timeout 90s build/dev/file_operation_input_tests
scripts/test-wayland.sh build/dev/file_operation_input_tests
SKETCHYUP_TEST_SCALE=2 scripts/test-wayland.sh build/dev/file_operation_input_tests
# injected failure
SKETCHYUP_TEST_INJECT_SAVE_FAILURE=1 SKETCHYUP_TEST_SCALE=2 scripts/test-wayland.sh build/dev/file_operation_input_tests
```

## Results

Pinned Arch CI image (`sketchyup-native-fonts-check:20261008`), Qt 6.11.2,
software GL, wall time including compositor start-up:

| Run | Result | Wall time |
| --- | --- | --- |
| X11 1x | pass | 5.1 s |
| X11 2x | pass | 5.2 s |
| Wayland 1x | pass | 2.5 s |
| Wayland 2x | pass | 3.3 s |
| Wayland 2x, injected save failure | **fail, exit 1** | 2.7 s |
| X11 1x, injected save failure | **fail, exit 1** | 4.8 s |

The injected runs stop about 0.3-0.7 s after the save starts and print:

```
File worker fixture: save-with-newer-edit started
Unexpected modal QMessageBox titled "Save failed": Could not write save: Permission denied
File worker fixture: save-with-newer-edit finished in 651.372 ms
Unexpected modal QMessageBox titled "Save failed": Could not write save: Permission denied
```

(the last line is the test's own failure message, thrown by the next `check()`).

Under heavy host load (load average above 30 from concurrent builds) Wayland 2x
failed at `qWaitForWindowExposed` before any file operation ran, in 3 of 6
runs; with load near 15, 6/6 passed for both this build and the unmodified test.
That start-up exposure timeout is unrelated to the guard, which does not act until
a modal dialog exists.

This is local test hardening, not R083 acceptance; remote CI and the remaining
R083 items stay open.
