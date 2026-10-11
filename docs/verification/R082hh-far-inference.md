# R082.hh — Far-family inference probe failure above 1,000 placements

2026-10-11. Base `80545acc5ca470f50b766bda97be843f37f179a7`. This is a
benchmark-fixture defect. Product source, limits, inference and picking are unchanged.
This evidence does not accept R082 or M9. Raw results are in
[R082hh-far-inference.json](R082hh-far-inference.json).

## Symptom

`real_model_benchmark N far` failed with `Inference probe intersects rendered geometry`
at 1,200, 1,500, 2,000, 4,000 and 10,000 placements. It failed with viewport
placement-mesh sharing on and off, and passed at 1,000. 1,200 placements fit the
default limits, and a normal Release build reproduces the failure at 1,200.

## Root cause

The history loop moved the edited placement out of its group:

```cpp
// tests/real_model_benchmark.cpp:380 (base)
doc.transform(editing, Transform::translation({0, 0, .01 * (cycle * 100 + i + 1)}) * base);
```

`Document::transform(Id, Transform local, Id parent = 0)`
(`src/core/model.hpp:206`) replaces both the local transform and the parent. The
call left out `parent`, so the first edit reparented `instances.front()` to the
top level. Its local transform is the identity, so in the `far` family it moved from
about (900000, −900000, 900001) to (0, 0, 1). That is 1,558,845 m from the
fixture. The undo/redo checks compare only the local transform, so they passed.

The post-history probe `infer(editing)` (`:428`) then aimed at the detached prism's
top. That point lies far beyond the far clip plane (2,561 m at 1,000 placements,
2,805 m at 1,200). It also lies 0.26° from the view axis, because the 900 km offset
nearly matches the default yaw −45°, pitch 35° camera direction. As a result it
projected to about (959.9, 546.0), near the centre of the frame. Picking casts an
unbounded ray, so the probe reported whatever lay at that pixel:

- **1,000 placements (32×32 grid):** the pixel happened to fall on an unrelated
  prism. The check passed, but it validated the wrong geometry.
- **1,200 or more placements (35×35 grid):** the pixel fell in the 2 m gap between
  prisms. Within ±1 px of the pixel, nothing was hit. The nearest hit was body 1,226,
  2 px to the left.

The `deep` family had the same detachment, but its 32 groups have identity
transforms, so the prism stayed in place and the probe still hit it. In both
families, the first history edit also reparented the placement and every later edit measured a top-level placement
instead of a nested one.

## Bisect

The defect is older than R082.cc–ff. The edit loop has omitted `parent` since the
far and deep families were added (`aac9a65b`). The probe that exposes the defect
arrived in `f89e62d5` (R082.g). Unmodified fixture, Release, offscreen:

| Commit | Contents | far 1,000 | far 1,200 |
| --- | --- | --- | --- |
| `f89e62d5` | adds the post-history probe; before R082.cc | pass | fails (probe) |
| `44eca11e` | parent of #284; before R082.cc/ff/ee/dd | pass | fails (probe) |
| `a34e1a7f` | parent of #287 (R082.ee) | pass | fails (probe) |
| `80545acc` | main with R082.cc/dd/ee/ff | — | fails (probe) |

## User impact

None. The only product callers of `Document::transform` pass the parent explicitly
(`src/automation/commands.cpp:1363`, which defaults it to the current parent). The failure came from the fixture's own call.

## Fix

- The history loop passes the placement's original parent, so edits stay in the
  group and at the far offset.
- A new check after history confirms that the parent is unchanged and that the
  placement's top is at its fixture position plus the expected lift. Before the fix,
  this check fails for far at 1,000 and 1,200 and for deep at 1,000.
- `infer()` now requires each probe to lie inside the viewport clip volume, so it
  can never land on unrelated geometry again. With only this check and no fix, far
  fails at both 1,000 and 1,200 with `Inference probe lies inside the viewport clip
  volume`.

No existing check, tolerance or measurement was weakened. Fixture hashes are
computed before the edit loop and do not change. Far final framebuffer hashes and
the deep/far edit and inference-update timings now measure a nested, in-place
placement. They are not comparable with earlier deep/far reports.

## Verification

Container `sketchyup-native-fonts-check:20261008` (one CPU, 4 GiB, `xvfb-run`, xcb,
llvmpipe), run through `scripts/run-container-check.sh`, with placement sharing on.
All runs passed:

| Build | Run | Bodies | Inference primitives | Query p95 ms | Update p95 ms |
| --- | --- | --- | --- | --- | --- |
| Release | repeated 1,000 | 2,000 | 360,000 | 0.842 | 5.150 |
| Release | unique 1,000 | 1,000 | 360,000 | 1.106 | 3.755 |
| Release | deep 1,000 | 2,032 | 360,000 | 0.865 | 16.720 |
| Release | far 1,000 | 2,001 | 360,000 | 2.008 | 6.701 |
| Release | textures 1,000 | 2,000 | 360,000 | 0.844 | 6.073 |
| Release | far 1,200 | 2,401 | 432,000 | 0.961 | 6.562 |
| Limit scale 8 | far 1,200 | 2,401 | 432,000 | 1.755 | 10.900 |
| Limit scale 8 | far 2,000 | 4,001 | 720,000 | 1.427 | 14.671 |

The far runs exercise the shared two-level inference index (R082.ee). Each run has
one local index per definition member and N placements. In every run, at least 32
candidates included the viewport-picked body. The far 1,200 fixture and final
framebuffer hashes match between the Release and scale-8 builds.

The normal headless (Debug) build also passed:

- `transform_selection`, `selection`, `constraints`, `inference` and
  `inference_equivalence`: 5/5.
- Full `ctest -j3`: 61/61.
