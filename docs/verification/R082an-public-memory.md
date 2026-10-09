# Public rendered history and process memory

The public manual benchmark adds an optional rendered-history cycle count and own-process DRM buffer accounting. Its original default fixture, renderer, three-argument timings and public capacity limits stay unchanged. Five cycles perform 500 edits and 1,000 rendered undo/redo transitions, with 65 checkpoints for current RSS, peak RSS, history and retained geometry cache capacity. The original first completed edit-frame and inference oracles remain in place.

The DRM parser deduplicates driver/device/client descriptors, handles byte units and resident-counter aliases, and reports incomplete or corrupt accounting as unknown. Its resident sum is a conservative upper bound over potentially overlapping regions; it is separate from process RSS and excludes driver internals outside fdinfo. The [kernel interface specification](https://www.kernel.org/doc/html/latest/gpu/drm-usage-stats.html) defines these counters. The [prospective memory budget](../decisions/0141-forward-memory-budget.json) retains its exact earlier bytes and late-calibration rationale.

Exact public source `60bda06` qualifies all 478 runtime and 303 test inputs. Twelve software controls preserve canonical fixtures across default/five-cycle families and Wayland scales 1/2. The actual normal parser and source-identical ASan/UBSan parser pass with leak detection. The retained earlier private parser fails the missing-resident-counter assertion. This sanitizer qualification covers the parser, not the full application. Source/cache restoration is verified.

Fresh physical Intel/Wayland and AMD/X11 runs complete ten supported controls apiece: five 25-placement smoke families and five 1,000-placement/100k-triangle families. All twenty retain frozen canonical hashes, exact unclipped 1920x1080 framebuffer, five cycles and every geometry/history/inference oracle. All 1,340 DRM observations are complete; every supported case meets the prospective process/history/growth/DRM ceilings. Each measurement job uses one CPU, 4 GiB, no swap and disk scratch, preserving the earlier memory-campaign resource policy. The actual physical displays are checked awake; no configuration or wake action is performed.

All AMD supported timings meet the original complete-frame, selection, inference and first edit-feedback targets. The Intel 100k complete-frame measurements miss the 16.7 ms target in all five families under these declared single-CPU controls; its selection/inference/edit feedback checks pass. These results do not establish performance under a different CPU/driver configuration.

| Platform | 100k family | Full viewport p95 ms | First edit frame p95 ms | Peak RSS MiB | Own DRM resident upper bound MiB |
| --- | --- | ---: | ---: | ---: | ---: |
| Intel | repeated-1000-1 | 18.529 | 45.021 | 471.3 | 386.8 |
| Intel | unique-1000-1 | 17.315 | 30.262 | 476.7 | 387.1 |
| Intel | deep-1000-1 | 19.552 | 88.441 | 471.6 | 426.4 |
| Intel | far-1000-1 | 18.737 | 45.944 | 471.2 | 386.2 |
| Intel | textures-1000-1 | 18.393 | 63.727 | 624.3 | 600.9 |
| AMD | repeated-1000-1 | 2.374 | 6.010 | 445.3 | 295.0 |
| AMD | unique-1000-1 | 2.339 | 3.800 | 451.3 | 295.0 |
| AMD | deep-1000-1 | 2.275 | 16.044 | 445.7 | 286.3 |
| AMD | far-1000-1 | 2.343 | 6.195 | 445.1 | 295.0 |
| AMD | textures-1000-1 | 2.432 | 9.730 | 572.6 | 542.9 |

Both public campaigns stop at the first 10,000-placement/1M attempt because the unchanged editing limit rejects component placement. No count is reduced and neither 13-case campaign is marked complete. Earlier successful private 1M sizing used explicitly higher private document/native capacity headers; it does not demonstrate public 1M support. The public capacity, Intel timing, full file responsiveness, final source integration and release gates remain open.

The [bound result](R082an-public-memory-result.json) retains original result/freeze/report hashes, all twenty supported measurements and explicit failures. Original failed campaigns are unchanged. Fresh full current-head CI remains required.
