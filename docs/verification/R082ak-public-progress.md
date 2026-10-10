# Reused native file-progress window

The public candidate retains one parent-owned native progress dialog and hides it synchronously when each operation ends. This avoids repeatedly destroying its native window during open/save. A call-owned 150 ms timer keeps short operations quiet and is cancelled when the call returns or throws, so an earlier short or failed operation cannot display the retained window later. The original polling, immutable worker inputs, modal/noncancellable behavior, reentrant-file fences and shutdown join remain in place.

The source b3a141f8c1957ae04bdfaacec034daf671dd2b58 combines this change with the previously qualified decoder ownership, single parsed payload and one-time topology validation path, retaining the current public assistant batching and guidance. No private model profile or event/phase instrumentation is compiled into this candidate. All 478 runtime and 301 test inputs match the normal and sanitizer sources.

The rebuilt previous progress implementation fails the new hidden/inactive dialog lifecycle assertion. The candidate passes 17 normal and 17 ASan/UBSan suites with leak detection, plus four normal and four sanitizer Wayland file-worker cases (warm and verified-cold at scales 1 and 2). Lifecycle coverage also verifies a failed short load cannot cause a later popup, subsequent saves reuse the same active dialog, and completion releases modal input. Cache sources are restored after qualification. Sanitizer native checks retain the previously qualified CI Wayland client reference fix and original 60-second deadlines.

Six actual AMD hardware cases retain the original 100 MB fixture bytes, three warm/three verified-OS-cache-cold repetitions, 5 ms heartbeat, trailing gap, first loaded frame, stale-save, history, recovery and reentrancy oracles. All 18 operations stay below five seconds. 17/18 measured heartbeat maxima still reach 50 ms; responsiveness remains unaccepted. The largest measured gap is 72.947 ms. Original instrumented investigations and earlier failing public measurements remain retained; timing differences alone do not establish universal performance or release acceptance.

| Case | Operation | Operation ms | Maximum heartbeat gap ms |
| --- | --- | ---: | ---: |
| warm-1 | open | 157.007 | 17.303 |
| warm-1 | save-with-newer-edit | 382.934 | 71.373 |
| warm-1 | save-current | 394.315 | 66.952 |
| verified-cold-1 | open | 269.692 | 71.704 |
| verified-cold-1 | save-with-newer-edit | 395.444 | 66.858 |
| verified-cold-1 | save-current | 392.626 | 61.117 |
| warm-2 | open | 271.559 | 72.699 |
| warm-2 | save-with-newer-edit | 395.916 | 67.082 |
| warm-2 | save-current | 393.320 | 57.032 |
| verified-cold-2 | open | 271.681 | 72.947 |
| verified-cold-2 | save-with-newer-edit | 411.264 | 65.952 |
| verified-cold-2 | save-current | 393.078 | 63.356 |
| warm-3 | open | 270.457 | 71.382 |
| warm-3 | save-with-newer-edit | 395.772 | 69.098 |
| warm-3 | save-current | 392.953 | 64.027 |
| verified-cold-3 | open | 269.150 | 70.373 |
| verified-cold-3 | save-with-newer-edit | 405.204 | 71.006 |
| verified-cold-3 | save-current | 404.312 | 56.557 |

The [result](R082ak-public-progress-result.json) binds these checks to exact source, fixture, binaries and original evidence hashes. Fresh complete current-head CI, full reference-hardware performance coverage, final integration and release acceptance remain required.
