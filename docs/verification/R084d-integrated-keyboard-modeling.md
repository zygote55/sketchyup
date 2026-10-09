# R084.d — Integrated keyboard-only measured modeling

2026-10-07. Integration `8f6c202`; implementation `822d3e7` / `5ddd1ec`.
Parent: [PR #210](https://github.com/zygote55/sketchyup/pull/210).

The [final native Wayland 2× workflow](R084d-integrated-native.json) passes in
**4.087 s**: actual keys select Rectangle, route focus with F6, type coordinates
and units, find a face through the command palette and push/pull to a 2×3×4 m
solid. It checks 24 m³ and exact modeled content through undo/redo. Initial focus
is fixture setup; subsequent modeling uses keys. The fix begins extrusion for an
already selected ready face before accepting distance, and restores parent-window
activation before palette callbacks.

The retained [eight-case normal/sanitizer matrix](R084d-keyboard-modeling.md)
passes normal **12.788 s**, ASan/UBSan **16.814 s**; separate Measurements and tool
lifecycle regressions pass native Wayland 2×. The final integration retains M7
camera/navigation code and gates.

[Installed checks](R084d-final-installed-smoke.json) match all four executables and
installed contracts/catalogs, including ADR 0147. The [source package](R084d-final-source-package.json)
contains **206 byte-exact installed inputs**, SHA-256
`9fe76e7ce2ce4fa9b2bb465c1c08f52e109b80defb9e0de2f685fa4f5843869e`.
This representative modeling task does not accept every Outliner, dialog or
screen-reader workflow. Remote CI, ordered merges and full R084/M9 acceptance remain open.
