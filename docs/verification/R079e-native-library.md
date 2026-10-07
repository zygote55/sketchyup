# R079.e — Native template and library workflows

2026-10-07. Native contract `df55189`; integration `f61a404`.
Parent: [PR #192](https://github.com/zygote55/sketchyup/pull/192).
Contract: [0133](../decisions/0133-native-template-and-library-workflows.md).

Desktop/CLI and affected native targets build. Final bundle/catalog/insertion tests
pass **1/1 in 0.11 s**; dedicated final ASan/UBSan core verification passed **1/1 in
0.71 s**, with leak detection and halt-on-error. Four final Wayland 2× regressions
pass: library, saved scenes, navigation and native MCP (**15.766 s** total).
The dedicated library [platform matrix](R079e-platform-matrix.json) passes all eight
normal/sanitized Wayland/X11 1×/2× combinations: **12.854 s normal**, **18.264 s
ASan/UBSan**. The native library dialog and fixture sources match this integration.

Native browsing supports folder selection, search, thumbnails, metadata and bounded
errors. Keyboard search/list activation creates a fresh template or inserts a component
in destination units as one undoable edit. Template default cameras apply immediately.
Dirty-document cancellation, changed catalog entries, malformed bundles, options/file
cancellation, real publication and existing/source protection are verified. The native
dialog was visually reviewed in the dedicated matrix. Saving captures the current-view
thumbnail and leaves model records, identity, history and camera unchanged.

The [installed smoke](R079e-installed-smoke.json) checks eight catalogs, thirty-eight
contracts and existing exchange workflows. [Source package](R079e-source-package.json):
**185 installed inputs** match byte for byte; SHA-256
`aa525ec93694b987f1adfe74898d0d4fc55224494dfe5bbaaf6d1961a385b02a`.
[Final native regressions](R079e-native-matrix.json) retain individual timings.

R079.a–e are locally implemented and verified. Complete remote CI, ordered merges
and milestone acceptance remain required before delivery is accepted.
