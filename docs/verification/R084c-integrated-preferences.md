# R084.c — Integrated legacy preferences

2026-10-07. Integration `8d08c17`; regression `3f6748c`; fix `c31e818`.
Parent: [PR #209](https://github.com/zygote55/sketchyup/pull/209).

The [final native Wayland 2× regression](R084c-integrated-native.json) passes in
**3.485 s**. It preserves currently implemented legacy preferences and opaque
future data through four window lifetimes, exercises all theme/navigation/reduced
motion choices, and checks unchanged document content/history. It fixes the theme
menu's missing persistence write. The retained [eight-case platform/sanitizer
matrix](R084c-preference-compatibility.md) passes normal **13.241 s**, ASan/UBSan
**18.714 s**. Private settings avoid actual user/provider configuration.

[Installed checks](R084c-final-installed-smoke.json) match all four executables
and every installed contract/catalog to the verified build/source, including ADR
0146. The [source package](R084c-final-source-package.json) contains **205 byte-exact
installed inputs**, SHA-256 `366d8cb53c3c520e638c3fb01d594ec176a7874922d48272bb7ef028f7821295`.
CI retains all M7 gates alongside the new normal/sanitized preference cases. This
is window recreation, not an installed-binary upgrade claim. Shortcut migration,
full accessibility, remote CI, ordered merges and R084/M9 acceptance remain open.
