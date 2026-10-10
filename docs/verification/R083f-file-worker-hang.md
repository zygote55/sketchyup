# R083.f — Sanitized file-worker CI hang (D-002): disk exhaustion behind a modal

2026-10-10. Source: `main` at `8534bb67`. Defect D-002 in the
R089.a defect triage was the intermittent 60-second timeout of the
sanitized Wayland 2× `file_operation_input_tests` fixture.

**Conclusion.** The fixture did not hit a deadlock or a missed wakeup. The CI runner
disk was nearly full, so a save failed with `ENOSPC`. The application then reported the
failure in its modal "Save failed" dialog, and nothing in the test closes that dialog.
The GUI thread stayed idle in `QDialog::exec()` until Weston's 60-second timeout. A user
in the same situation sees an error dialog and can dismiss it. The edit stays in memory
and the saved file is unchanged. Open and save do not depend on the window being exposed.
The hang came from the test harness running on a full disk, not from the product. This
meets the triage's stated condition for reclassifying D-002 as Minor.

## Hung-run analysis

[Per-job evidence](R083f-ci-disk-runs.json) covers 119 sanitized-step jobs. Of these, 116
come from the tracker's cache. The other three are earlier run attempts, which the
tracker misses because it reads only the latest attempt. All three also hung. In total
there were **17** hangs, not 14.

Every hung log has the same tail. After `Native scenes … passed`, the log shows the two
cursor-theme messages that Qt prints during start-up. In the `ENOSPC` runs these same
messages come before the fixture error, so they are printed before the test writes
anything. Nothing else appears until Weston logs `caught signal 15` at 60 s. The pre-#242
test wrote no progress lines and only printed its result at the end, so the test logs
cannot show where it stopped. The GitHub runner's own warning, "You are running out of
disk space … Free space left: N MB", does narrow it down. The runner printed that warning
in the same step:

| CI layout | Test | Outcome | Jobs | Runner free-space warning |
| --- | --- | --- | ---: | --- |
| One job: dev and sanitize builds on one disk | before #242 | Hang | 17 | 3–7 MB after the build (14); 7, 10, 56 MB during the last link, which are upper bounds (3) |
| | before #242 | Fixture `ENOSPC` | 1 | 23 MB during the last link |
| | before #242 | Pass | 11 | 8 MB (1); 82–86 MB (10) |
| | after #242 | Fixture `ENOSPC` | 4 | 0–1 MB |
| | after #242 | Link `ENOSPC` | 3 | — |
| | after #242 | Pass | 8 | 59–80 MB |
| One job, dev build reclaimed before sanitizers (`c968313f`) | after #242 | Pass | 17 | none |
| Separate sanitize job (#264) | before #242 | Pass | 18 | none |
| | after #242 | Pass | 40 | none |

The bands match the fixture's own disk use. The fixture writes a 2,100,399-byte model.
Each save writes the new bytes to a temporary file. It also writes the previous bytes to
a temporary `.bak`, then commits both. The first save peaks at three copies (6.3 MB) and
later saves peak at four (8.4 MB). With less than 2.1 MB free, the fixture write fails and
the test exits within a second with `Could not write save: No space left on device`.
With 2.1–8.4 MB free, the fixture fits but a save fails, and the test hangs. With more
free space, the test passes. Every warning issued after the build falls in the band its
outcome predicts. The 8 MB pass is on the boundary.

**Why #242 looked like a fix.** The tracker's "0 hangs in 65 runs" after #242 counts
passes only. Just 15 runs after #242 used the nearly full single-job layout, and 7 of
them failed with `ENOSPC`, which the tracker files as `OTHER_FAILURE`. That is close to
the earlier rate of 18 failures in 29. The later heads built larger binaries, so free
space fell below 2.1 MB or ran out during linking, instead of landing in the hang band.
`c968313f` then freed the dev build before the sanitizers ran, and #264 moved the
sanitizers to their own runner. All 75 runs since then passed, with both versions of the
test. #242 changed no application code. Its 5 ms heartbeat and first-frame readback cannot
close a modal dialog.

## Root cause in code (`8534bb67`)

- `src/io/save.cpp:90–101` writes the new bytes through one `QSaveFile` and the previous
  bytes through a second one for `.bak`, then commits both. `save.cpp:53` throws
  `Could not write save: …` on a short write. The original file stays intact.
- `src/app/file_operation.hpp:75–86` runs the worker with `std::async`. A 10 ms poll quits
  the nested `QEventLoop`, and `future.get()` rethrows on the GUI thread. The `Finish`
  guard at `:60–65` hides the progress window first.
- `src/app/window.cpp:1172` (`Window::save`) shows the exception with
  `QMessageBox::warning(this, "Save failed", …)`, which is modal and runs its own event
  loop.
- `tests/file_operation_input_tests.cpp:229` and `:233` trigger saves with no handler for
  that dialog. Before #242, the first save was at line 92. The only dialog handler,
  `chooseSave` at `:243–255`, answers the "Unsaved changes" prompt during
  save-before-replace.

## Deterministic reproduction

The reproduction uses the image `sketchyup-native-fonts-check:20261008` (the CI package
set plus DejaVu fonts), the private CI Wayland client library, and ASan/UBSan with leak
detection. `/tmp` is a tmpfs with a fixed size. Full output is in
[R083f-reproduction-results.txt](R083f-reproduction-results.txt).

| Binary | `/tmp` capacity | Result |
| --- | --- | --- |
| Archived pre-#242 fixture (`8e0afcbb`, R087.t), Wayland 2× | 2 MiB | Fixture write fails, exit 1 in 0.8 s |
| | 3, 5, 6, 7, 8 MiB | No output, then the 60 s timeout (the CI signature) |
| | 9, 10, 12, 16 MiB | Pass |
| Current `main` fixture, Wayland 2× | 2 MiB | Fixture write fails, exit 1 |
| | 4 MiB | Hangs after `save-with-newer-edit started` |
| | 7 MiB | Hangs after `save-current started` |
| | 9 MiB | Pass |
| Current `main` fixture, X11 2× | 4 MiB | Hangs after `save-with-newer-edit started` (timeout 124) |
| Current `main` fixture, Wayland/X11 1×/2× | 64 MiB | 4/4 pass |

In each hang, `gdb` shows the main thread in `ppoll` under `QDialog::exec()`, which was
called from `Window::save` at `window.cpp:1172`. That was reached from the test's
`save->trigger()`: line 92 before #242, lines 229 and 233 now. The same disk condition
hangs the current test, which shows where it stopped instead of staying silent. The
behavior is the same on X11.

On tmpfs, a partial write reports `Unknown error` in the fixture message. On ext4, CI
reported `No space left on device`.

## The product does not hang

[`probes/save-failure-modal.cpp`](probes/save-failure-modal.cpp) is a probe, not a
registered test. It fills its own 16 MiB filesystem after writing the 2 MB model, then
saves an edit. It asserts that:

- the "Save failed: Could not write save: No space left on device" modal appears and the
  compositor maps it;
- a 5 ms heartbeat keeps firing under the modal: 20–873 ticks before the dismissal in
  the final matrix;
- the edit stays dirty in memory, the saved file is byte-identical, and the failure
  banner shows;
- after the filler file is deleted, Save succeeds and clears the banner.

It then opens and saves a 24 MiB model while the main window is hidden or minimized,
with the progress window shown. A 40 s deadline makes it fail rather than hang. Results:

| Phase | Wayland 1× | Wayland 2× | X11 1× | X11 2× |
| --- | --- | --- | --- | --- |
| Save on a full disk, dismiss, recover | pass | pass | pass | pass |
| Open and save while hidden | complete; remap error (below) | pass | pass | pass |
| Open and save while minimized | complete; Weston crash (below) | complete; Weston crash | pass | pass |

Opening a model from the command line happens before the main window is first shown. The
sanitized `sketchyup --capture` passed this 6 of 6 times with the same 24 MiB model, at
Wayland 1× and 2×.

These cases cannot hang the product. The file-operation loop exits on the poll of the
worker's result, so it never waits for exposure, frame callbacks or painting. The progress
window is hidden before any error dialog appears. When a save or open fails, the only
wait is a user-dismissible modal whose event loop keeps running.

**Two failures outside D-002.** Both are crashes or disconnects, not hangs:

1. **Qt Wayland disconnect.** If the probe hides the main window, runs an open and save
   with the progress popup, and then shows the window again, Qt Wayland intermittently
   breaks the protocol (`xdg_surface has never been configured`). This happened in 6 of
   8 runs at 1× (1 of 6 in an earlier series) and in 0 of 3 runs at 2×. It happened in
   0 of 8 runs that hid and showed the window without file work. The disconnect comes
   after the operation has finished. Current flows never do this. The only programmatic hide is the unmap barrier in
   `Window::closeEvent` (`window.cpp:85`, `:1247–1261`). It runs after `canReplace()` has
   finished any save, and it shows the window again with no file operation in between.
2. **Weston crash.** Weston 15.0.1 logs "Detected an unmapped surface or view in the
   layer list", then segfaults in `libweston` request dispatch. This happens during or
   after an open/save while minimized, once a progress popup has been created for the
   minimized parent. Minimizing and remapping without file work does
   not crash it (4 of 4). This is a compositor defect, and other compositors were not
   tested.

## Commands

```sh
# Probe build hook, outside the tree: pass -DCMAKE_PROJECT_INCLUDE=<dir>/project_include.cmake
#   project_include.cmake:
#     cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "<dir>/probe_target.cmake")
#   probe_target.cmake:
#     add_executable(d002_save_failure_probe EXCLUDE_FROM_ALL
#       "${CMAKE_SOURCE_DIR}/docs/verification/probes/save-failure-modal.cpp")
#     target_link_libraries(d002_save_failure_probe PRIVATE sketchyup_desktop Qt6::Test)
docker run --rm -u 1000:1000 -e HOME=/tmp/home -v "$PWD:$PWD" -w "$PWD" -v <dir>:<dir>:ro \
  sketchyup-native-fonts-check:20261008 sh -c 'mkdir -p "$HOME" && cmake --preset sanitize \
  -DSKETCHYUP_BUILD_CLI=ON -DSKETCHYUP_BUILD_DESKTOP=ON \
  -DCMAKE_PROJECT_INCLUDE=<dir>/project_include.cmake && cmake --build --preset sanitize \
  --target file_operation_input_tests d002_save_failure_probe sketchyup --parallel 3'

# One low-disk case: Wayland 2x with 4 MiB of /tmp (hangs at save-with-newer-edit)
docker run --rm --tmpfs /tmp:size=4m,exec -v "$PWD:/src:ro" -v <client-lib-dir>:/wayland-client:ro \
  sketchyup-native-fonts-check:20261008 env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 QT_WAYLAND_DISABLE_WINDOWDECORATION=1 \
  QT_QPA_PLATFORMTHEME=generic QT_STYLE_OVERRIDE=Fusion QT_IM_MODULE=compose \
  SKETCHYUP_WAYLAND_CLIENT_LIBRARY=/wayland-client SKETCHYUP_TEST_SCALE=2 \
  bash /src/scripts/test-wayland.sh /src/build/sanitize/file_operation_input_tests

# Probe: its own small filesystem for TMPDIR and room in /tmp for the large model
docker run ... --tmpfs /tmp:size=200m,exec --tmpfs /small:size=16m -e D002_UNEXPOSED=hidden \
  ... bash /src/scripts/test-wayland.sh env TMPDIR=/small \
  /src/build/sanitize/d002_save_failure_probe
# X11: xvfb-run -a env QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=<1|2> LIBGL_ALWAYS_SOFTWARE=1 \
#        [TMPDIR=/small for the probe] timeout 60s <binary>
```

## Remaining

- The fixture still waits forever if a save fails unexpectedly, though it now prints the
  stage first. A guard that fails on any unexpected `QMessageBox` would turn this into an
  immediate, explained failure. This record does not change the test.
- The tracker reads only the latest attempt of each run and files `ENOSPC` exits under
  `OTHER_FAILURE`.
- The Qt Wayland remap disconnect and the Weston crash above need their own triage.
