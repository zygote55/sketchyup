# R084.h — Integrated independent interface text size

2026-10-07. Integration `d3b6fb6`; preference `d3ef651`, layout correction `89a36b6`.
Parent: [PR #214](https://github.com/zygote55/sketchyup/pull/214).

The [final native Wayland 2× regression](R084h-integrated-native.json) passes in
**4.779 s**. It checks all six text-size choices, Cancel, invalid-value fallback,
window recreation, unchanged display scale/model/history/settings, then 200% in
both themes at four logical widths. Measurements stays visible; command labels,
breadcrumbs, Outliner/Tags buttons and wrapped hints fit the enlarged font.

The retained [eight-case matrix and reviewed captures](R084h-interface-text-size.md)
pass normal **18.389 s**, ASan/UBSan **26.510 s**. Five additional native 2×
regressions cover shortcuts, measured keyboard modeling, preferences, contrast and
organization. Initial weak checks missed clipping; the recorded final checks
include stronger geometry assertions and the corrected layouts.

[Installed checks](R084h-final-installed-smoke.json) match all four executables and
installed contracts/catalogs, including ADR 0151. The [source package](R084h-final-source-package.json)
contains **210 byte-exact installed inputs**, SHA-256
`b2418e979a4e16364249dff0dc758223c7e0c4aa25399720931c423a443ca5a1`.
This covers representative widths at a 900-pixel logical height, not every panel,
dialog, text length or overlay contrast state. Follow-up audits, remote CI, ordered
merges and full R084/M9 acceptance remain open.
