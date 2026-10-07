# R084.i — Integrated context-link contrast

2026-10-07. Integration `64b851d`; correction `51d99d5`.
Parent: [PR #215](https://github.com/zygote55/sketchyup/pull/215).

The [final native Wayland 2× check](R084i-integrated-native.json) passes in **3.032 s**,
including actual painted link pixels in both themes and model/history preservation.
The [retained eight-case normal/sanitized matrix](R084i-context-link-contrast.md)
passes, with text-size and organization regressions. Painted breadcrumb contrast
improves from 1.742:1 to 8.052:1 on the dark surface; light contrast is 4.648:1.

[Installed checks](R084i-final-installed-smoke.json) match all four executables and
installed catalogs/contracts, including ADR 0152. The [source package](R084i-final-source-package.json)
contains **211 byte-exact installed inputs**, SHA-256
`f849b6945799df44d6ffb1bf9f34c7295b0dde31f3c3d6fa8f0f7b3affe3bcb2`.
Full accessibility acceptance, remote CI and ordered merges remain open.
