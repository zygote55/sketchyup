# R072.a–b — Walk navigation and transition timing

2026-10-07. Core math `e4f644d`, native controls `2080c0b`, trackpad correction
`9e7a860`, contract `7eb666c`, final integration/CI `820f3a7`.
Parent: [PR #164](https://github.com/zygote55/sketchyup/pull/164).
Contracts: [0104](../decisions/0104-camera-motion-and-timelines.md),
[0105](../decisions/0105-native-walk-and-scene-timing.md).

The complete Debug build and **138/138 CTest suites pass** in **153.16 s**.
Six final native Wayland 2× integration suites pass in
**25.084 s** ([matrix](R072ab-final-native-matrix.json)).
[Sixteen focused native checks](R072ab-native-sanitize-matrix.json) cover navigation
and scene recall on Wayland/X11, both scales, normal and ASan/UBSan builds.
Normal source `9e7a860`; sanitized source `daaafcb`. The sanitized camera math suite
also passes with leak detection and halt-on-error (`c78a19f`).

Tests verify fixed-eye look, horizontal travel and independent eye height, diagonal
normalization, key release, focus loss, shortcut handling, mouse/trackpad lens and
speed changes, zero-duration/reduced-motion recall, and unchanged document/history.
The initial trackpad-wheel test found generic panning consumed Look/Walk scrolling;
the final implementation routes lens/speed changes to the active navigation tool.

Core camera tests cover shortest-yaw interpolation, logarithmic distance, exact
endpoints, projection cuts and bounded frame timelines with saved-state indices.
The Scenes panel persists transition duration from zero to ten seconds. Escape
returns navigation to Select. Walking does not provide collision or terrain following.

The [installed smoke](R072ab-installed-smoke.json) verifies seven catalogs, ten
contracts, exact desktop bytes and existing relocatable lighting/export behavior.
[Source package](R072ab-source-package.json): **152 installed inputs** match byte
for byte; SHA-256 `eb8b1d7d4ea3d5e68bab82a39c6174ad85b978a8ab500e1e4a6c92a65a12c91c`.

R072.a–b are locally complete. Immutable scene capture and animation export remain
separate R072 layers. Remote CI and ordered merges remain delivery gates.
