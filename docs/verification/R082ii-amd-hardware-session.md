# R082.ii — AMD discrete GPU hardware session

2026-10-11/12. Source revision `db68cd14bcc1ee93ad9adaa4bb981bc63984b46a` (`main` after #299).
Section 2 of the [M9 hardware session runbook](../M9_HARDWARE_SESSION.md), run on the
discrete GPU. Machine-readable results are in
[R082ii-amd-hardware-session.json](R082ii-amd-hardware-session.json). This is
supporting evidence at a named revision, not release-candidate evidence. It does not
accept R082 or M9 (`releaseAcceptance` is false in every report).

The owner waived the rest of the runbook, including all Intel campaigns, on
2026-10-12 ([ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md)). No Intel
result exists at this revision.

## Conditions

| Item | Value |
| --- | --- |
| Machine | `sketchyup-gpu`, CachyOS QEMU guest (kernel 7.2.9-1-cachyos), XFCE on X11 (`QT_QPA_PLATFORM=xcb`) |
| GPU, driver | AMD Radeon RX 6800 XT (radeonsi, navi21, ACO, DRM 3.64); Mesa 26.3.0-devel (git-8ea6e9dd41); OpenGL 4.6 |
| Toolkit | Qt 6.11.2 (`qt6-base 6.11.2-3`) |
| Build | Host Release build under the runbook's resource caps. Binary hashes: [JSON](R082ii-amd-hardware-session.json) `binarySha256` (`real_model_benchmark` `9cc7999c…`) |
| Frame, history, 10k controls | 4 GiB, no swap, `CPUQuota=100%`, `TasksMax=128`, 900 s, fullscreen host, viewport 1920×1080 px, `QT_SCALE_FACTOR=1` |
| File controls | 4 GiB, no swap, `CPUQuota=800%`, CPUs 0-7, 90 s, Fusion style, `QT_IM_MODULE=compose` |
| Hypervisor load | Not measured |

The campaign script change made for the session is part of this record's pull request:
`scripts/session-benchmark-campaign.sh` now also runs `deep`, `textures` and `far` at
10,000 placements (five history cycles, one run each) in the `capacity-10k` list, as the
runbook said to do. The script still flags `viewportGpuCompleteP95Ms` above the 100k
target of 16.7 ms for every case; at 10,000 placements the target is 33.3 ms, so the
flag on those cases is not a miss (see below).

## Results

Targets are from [BUILD_PLAN](../BUILD_PLAN.md#initial-performance-budgets). All 42
cases exit 0.

### 100k families (frame and history campaigns): no MISS

Frame: 5 smokes at 25 placements and 15 runs at 1,000 placements (three per family);
history: 5 smokes and 5 runs at 1,000 placements, five cycles. Values are p95 ms,
ranges over the 1,000-placement runs. Viewport is the complete `paintGL` including
overlays and `glFinish`; edit is the first GPU-complete viewport after the edit.
The 16.7 ms frame target and the 100 ms edit target are met with a wide margin in
every family.

| Family | Runs | Viewport | Edit to viewport | Edit and readback | Inference update | Pick | Peak RSS MB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| repeated | 3 | 2.2-2.6 | 5.4-5.6 | 16.2-16.3 | 0.6-0.7 | 0.04 | 221 |
| unique | 3 | 2.2-2.6 | 3.6-3.8 | 16.6 | 0.3-0.4 | 0.04-0.05 | 426-427 |
| deep | 3 | 2.1-2.3 | 16.0-16.4 | 18.8-19.7 | 3.5-3.8 | 0.13-0.14 | 222-223 |
| far | 3 | 2.1-2.2 | 5.8-5.9 | 16.2 | 0.7 | 0.04 | 222 |
| textures | 3 | 2.1-2.3 | 9.1-9.3 | 16.2-16.3 | 0.6-0.7 | 0.04-0.05 | 356-357 |

The history runs (one per family, five cycles) agree: viewport 2.1-2.3 ms, edit to
viewport 3.6-16.2 ms (deep is the highest), inference update at most 3.7 ms, peak RSS
221-428 MB (below the 1 GiB v2 class for 100k scenes). Inference query p95 is at most
0.39 ms in every 100k run. Deep at 1,000 placements takes 16 ms to the first edited frame,
up from the 3.6-9.3 ms of the other families; the deep edit is now a real nested edit since
the [R082.hh](R082hh-far-inference.md) fixture fix, so older deep figures are not comparable.

### 100 MB native file (file campaign): no MISS

Fixture: 100,671,486 B, SHA-256 `d35ce94908ad4e7768009dff76e8029e9ce0a4c3e13beeaa2ee6d1a7bff4fb5a`
(schema 26; [R082.m](R082m-native-file-timing.md) used a 100,671,415 B file). Three warm and
three verified-cold cases, all `passed: true` (load, save and stale-save fences).

| Operation | Time ms | Maximum heartbeat gap ms |
| --- | ---: | ---: |
| open | 168.3-183.8 | 19.7-29.2 |
| save with newer edit | 337.7-367.5 | 6.0-7.6 |
| save current | 328.5-396.6 | 20.9-32.4 |

Every open and save is far below five seconds, and every heartbeat gap is below 50 ms
(the [R082.bb](R082bb-public-tooltip-progress.md) AMD bar). This is the X11 file result;
the Intel/Wayland open gap of D-005 was not remeasured.

### 10,000 placements, 1,000,000 triangles (capacity-10k): memory within v2; edit feedback misses

Six cases (repeated three times; deep, textures and far once), five history cycles
each, public build, no limit raised. Fixture hashes match the software run in
[R082.gg](R082gg-placement-limits.md) for repeated, deep and textures.

| Case | Fixture SHA-256 (prefix) | Peak RSS MB | Viewport p95 ms | Edit to viewport p95 ms | Edit and readback p95 ms | Inference update p95 ms | Pick p95 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| repeated 1 | `5a0d89c38bcff4ff` | 618.4 | 17.3 | 68.2 | 86.5 | 11.3 | 0.43 |
| repeated 2 | `5a0d89c38bcff4ff` | 616.5 | 17.8 | 68.9 | 87.2 | 11.4 | 0.47 |
| repeated 3 | `5a0d89c38bcff4ff` | 616.5 | 18.3 | 67.2 | 86.3 | 11.1 | 0.39 |
| deep | `3c834c6986914f06` | 654.2 | 18.3 | **209.3** | **229.4** | 48.2 | 0.77 |
| textures | `69c547f1e2663559` | 758.7 | 21.7 | **108.9** | **128.8** | 11.1 | 0.54 |
| far | `a0467383e0b8c9a9` | 656.2 | 20.8 | 73.8 | 93.1 | 12.5 | 0.65 |

Full hashes are in the JSON. Hashes at 1,000 and 25 placements are also recorded per
case; the 1,000-placement hashes match R082.gg for all five families.

Against the [R082.w v2](R082w-memory-budget-v2.md) ceilings, all cases pass:

| Check | Measured (worst case) | Ceiling |
| --- | ---: | ---: |
| Own-process peak RSS (`finalPeakRssBytes`) | 758,738,944 B (textures) | 1.25 GiB (1,342,177,280 B) |
| Sampled resident growth over 65 checkpoints | 3,289,088 B | 64 MiB |
| Retained history | 1,636,500 B | 64 MiB |
| DRM resident upper bound (any checkpoint) | 468,561,920 B (textures) | 2 GiB |
| DRM accounting | `available`, not `partial`, no read failures at every checkpoint, all six cases | required |

Hardware RSS is 3-5 MB above the software figure for repeated (613.5 MB), 30 MB above it for
deep (624 MB) and 125 MB below it for textures (884 MB). Frame p95 of 17.3-21.7 ms is under the 33.3 ms
target of the 1M-triangle row (the script's `MISS` flag compares with 16.7 ms and does
not apply). Picking and inference queries stay under 1 ms.

Far at 10,000 placements, which [R082.hh](R082hh-far-inference.md) made measurable, now
has a recorded run and passes the memory and frame checks. The v2 declaration still
excludes far from the 1M class; this record does not amend it.

Not run: the by-hand sharing-off control (3 GiB ceiling). It is waived by ADR 0161.

## Finding: edit feedback misses 100 ms at 10,000 placements (D-024)

The BUILD_PLAN row is "Routine primitive edit: p95 feedback below 100 ms".

| Family | Edit to viewport p95 ms | Edit and readback p95 ms |
| --- | ---: | ---: |
| repeated (3 runs) | 67.2-68.9 | 86.3-87.2 |
| far | 73.8 | 93.1 |
| textures | **108.9** | **128.8** |
| deep | **209.3** | **229.4** |

Deep and textures miss on both metrics. Deep spends 48.2 ms of p95 in the inference
update (11-13 ms in the other families) on top of rebuilding the nested placement;
textures adds about 40 ms over repeated for the textured path. In the 100k families
the same edits take 5-16 ms. Deep and textures ran once each at 10,000 placements, so their run-to-run spread is
unknown (repeated's three runs vary by 1.7 ms); deep crosses the 100 ms line by a wide
margin. It is classified Major in [R089.a](R089a-defect-triage.md), D-024.

## Not run

Everything else in the runbook: Intel sections, the sharing-off control, suspend/resume,
physical scale, pointer capture, input methods, D-020 on Hyprland, Orca and X11
placement are waived by [ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md);
the Wayland hotplug is recorded in [R085.r](R085r-hotplug-owner-attested.md).

## Evidence retention

Raw reports, stderr, limits and screenshots stay outside the repository in
`~/sketchyup-verification/m9-hardware-session/amd/`. The JSON keeps every case's
metrics, hashes and cgroup memory peaks.
