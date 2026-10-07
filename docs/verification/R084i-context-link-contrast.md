# R084.i — Painted context-link contrast

2026-10-07. Regression fixture `d60f127`; correction `51d99d5`.
Contract: [0152](../decisions/0152-context-link-contrast.md).

The R084.h visual review found blue context links despite the application's themed
QPalette::Link. The [before report](R084i-contrast-before.json) measures actual
painted blue (#0000ff), **1.742:1** against the dark surface. Explicit anchor styling
uses the theme accent: [after](R084i-contrast-after.json), dark **8.052:1**, light
**4.648:1**. Both exceed 4.5:1. Component scope links use the same styling path;
application-authored markup and escaped model names remain separate from theme
attributes. [Updated dark capture](R084i-dark-context-link.png).

The [platform matrix](R084i-platform-matrix.json) passes **8/8** across Wayland/X11
at 1×/2×, normal **11.395 s**, ASan/UBSan **15.349 s**, leak detection and
halt-on-error. The fixture checks painted link pixels as well as the existing
input, selected-input, button and hint palette contrast. Model/history preservation
is asserted. [Text-size and organization regressions](R084i-regressions.json) pass
native Wayland 2×, **2/2 in 8.621 s**; refreshed enlarged captures pass as well.

This resolves the observed breadcrumb color defect without claiming every possible
component-banner state or assistive-technology workflow. R084/M9 remain open.
All tests use private settings and do not access actual provider configuration.
