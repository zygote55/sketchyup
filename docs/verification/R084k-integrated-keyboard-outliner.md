# R084.k — Integrated keyboard Outliner workflow

2026-10-07. Integration `be7e464`; focus correction `c647d7b`.
Parent: [PR #217](https://github.com/zygote55/sketchyup/pull/217).

The [final native Wayland 2× check](R084k-integrated-native.json) passes in **4.340 s**.
Actual F6, arrows, F2, typed names, lock/visibility keys, Enter/Escape and undo/redo
edit a nested hierarchy while preserving an untouched neighbor. No direct focus
assignment or pointer operation follows initial fixture setup. Organization dialogs
restore parent activation and panel focus after closing, fixing the X11 failure.

The [retained eight-case matrix](R084k-keyboard-outliner.md) passes normal and
ASan/UBSan at both scales and backends. [Installed executables and contracts](R084k-final-installed-smoke.json)
match, including ADR 0154. The [source package](R084k-final-source-package.json)
contains **213 byte-exact installed inputs**, SHA-256
`35a3db767490375f81c911d2b91f481b1983c5d3e38260b5fce2999f9f305fb0`.
All forms and assistive workflows are not covered; full R084/M9 acceptance remains open.
