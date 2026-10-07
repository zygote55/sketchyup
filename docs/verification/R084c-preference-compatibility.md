# R084.c — Existing preferences and theme persistence

2026-10-07. Regression fixture `3f6748c`; fix `c31e818`.
Contract: [0146](../decisions/0146-legacy-preference-compatibility.md).

The new native fixture failed before the fix with **Selected theme survives window
restart**. Startup read `theme`, but the menu action never wrote the changed value.
The action now persists its selected mode in the existing application namespace.

All eight [normal/sanitized Wayland/X11 1×/2× combinations](R084c-preference-platform-matrix.json)
pass after the fix: **13.241 s normal**, **18.714 s ASan/UBSan**, with leak detection
and halt-on-error. Each exercises four window lifetimes, all three theme choices,
mouse/trackpad and reduced motion, effective legacy units/field-of-view/recovery,
unrelated settings and opaque future data, and unchanged document content/history.
The fixture uses private config/data directories and never opens provider preferences.
CI runs the four normal combinations and sanitized Wayland 2× regression.

This establishes preservation of currently implemented preferences and fixes a
restart bug. Shortcut editing/migration, text scaling, assistive-technology usability
and full R084/M9 acceptance remain open. It is not an installed-binary upgrade test.
