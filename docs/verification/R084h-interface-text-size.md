# R084.h — Independent interface text size

2026-10-07. Preference/fixture `d3ef651`; capture fixture `4c5cd40`;
layout correction `89a36b6`. Contract: [0151](../decisions/0151-interface-text-size.md).

The [platform matrix](R084h-platform-matrix.json) passes **8/8** across native
Wayland/X11 at 1×/2×: normal **18.389 s**, ASan/UBSan **26.510 s**, with
leak detection and halt-on-error. Tests use private settings and never access the
user's provider configuration. They exercise all six choices, cancellation,
invalid-value fallback, persistence through window recreation and independent
display scaling. Model content, history and unrelated settings remain unchanged.

At 200% text size, both themes and four logical widths (640, 900, 1200, 1600)
retain Measurements and at least 300 logical pixels of viewport. Assertions check
actual font sizes and fit of command labels, breadcrumbs, Outliner/Tags buttons
and wrapped hints. Initial captures exposed clipping despite the first weaker
checks passing. The correction adds compact command labels, elided header/status
text, refreshed overlay/hint geometry and adaptive action-button grids; the final
matrix includes stronger fit assertions.

The eight [capture hashes](R084h-layout-captures.json) identify the final Wayland
2× images. Representative [compact dark](R084h-layouts/theme-2-width-640.png) and
[Outliner light](R084h-layouts/theme-1-width-1200.png) captures show the corrected
labels and reflow. Full before/after captures remain in local build evidence.
The breadcrumb's platform-default link color remains a separate contrast audit
item; this change establishes geometry, not complete overlay-state contrast.

[Regression checks](R084h-regressions.json) pass **5/5 in 19.528 s** on native
Wayland 2×: shortcut editing, keyboard modeling, legacy preferences, selected-text
contrast and organization controls. This is bounded coverage at a 900-pixel
logical window height, not acceptance of every panel/dialog or screen-reader
workflow. R084/M9 remain open.
