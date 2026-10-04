# R036.c: native material rendering

Date: 2026-10-04. Local checks passed; CI pending.
Requires [PR #46](https://github.com/zygote55/sketchyup/pull/46).

The viewport uses persisted front/back swatches and opacity. Mixed opaque and
translucent sides enter separate depth/blend passes, and reflected placements
preserve physical material sides. CPU ray picking and GPU selection pass through
zero-opacity sides. Only dependent appearance buffers change when a swatch is
edited. The [rendering decision](../decisions/0012-material-rendering.md) states
edge visibility and the remaining intersecting-transparency limitation.

The new framebuffer test exercises independent sides, mixed opacity depth,
layer blending, reflected geometry, immediate CPU picking, GPU selection,
selective cache invalidation, undo/redo and save/reopen. CI runs it on X11 and
isolated Weston at scale 1 and 2.

Validation: development suite 35/35 passed. Native material framebuffer tests
passed on X11 and isolated Weston at DPR 1 and 2. Existing X11 viewport/topology
and selection input suites passed. The core-only sanitizer code is unchanged
from R036.b's 28/28 passing run.

The shared-instance check initially found redundant uploads for empty roots
retaining default material references. Caches now depend only on materials used
by actual faces; the regression requires exactly two visible-instance uploads.

These are implementation-agent checks. Native swatches, resource controls and
paint/sample tools remain R036.d; image texture mapping remains later work.
