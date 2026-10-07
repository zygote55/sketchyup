# R084.e — Integrated versioned shortcut policy

2026-10-07. Integration `6d2774d`; implementation `b515980` / `0104607`.
Parent: [PR #211](https://github.com/zygote55/sketchyup/pull/211).

The final policy regression passes **1/1 in 0.03 s**. It covers persistent explicit
unbinds and reassignment, saved-choice precedence over new defaults, duplicate
saved-binding deactivation/notices, reserved/multi-step key rejection, unknown
entry/envelope preservation, reset and malformed/future-version rejection. The
retained [ASan/UBSan gate](R084e-shortcut-bindings.md) passes in **0.05 s**. This
layer is the bounded binding policy; the native editor follows separately.

[Installed checks](R084e-final-installed-smoke.json) match all four executables and
every installed contract/catalog to the build/source, including ADR 0148. The
[source package](R084e-final-source-package.json) contains **207 byte-exact installed
inputs**, SHA-256 `8dd06b834535e9fbf269fd0be6dc8143a1b439e828143031c15daa11db1e9280`.
The CI merge preserves M7 presentation coverage alongside the shortcut sanitizer
case. Native persistence/routing, remote CI, ordered merges and full R084/M9
acceptance remain open.
