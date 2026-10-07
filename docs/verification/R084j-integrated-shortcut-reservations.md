# R084.j — Integrated panel shortcut reservations

2026-10-07. Integration `c7ec041`; guard `5592c6b`, default collision fix `4fa28ee`.
Parent: [PR #216](https://github.com/zygote55/sketchyup/pull/216).

The final policy check passes **1/1 in 0.06 s**. [Native Wayland 2× checks](R084j-integrated-native.json)
pass shortcut conflicts in **5.445 s** and the text workflow in **8.233 s**, including
opening text creation with its actual new Ctrl+Alt+Shift+T binding. Ctrl+Shift+T
continues to toggle the model panel. Every public default remains active.

The [retained eight-case native matrix](R084j-panel-shortcut-conflicts.md) passes
normal and ASan/UBSan with leak detection. Global assignments cannot shadow panel
keys; invalid saved assignments remain serialized but inactive with a notice until
repaired. Viewport-only assignments can share disjoint panel keys.

[Installed executables and contracts](R084j-final-installed-smoke.json) match,
including ADR 0153. The [source package](R084j-final-source-package.json) contains
**212 byte-exact installed inputs**, SHA-256
`d278b324542da43b3a7ebecfa225847b59a520691e270d65659c588f5c2d479c`.
Compositor shortcuts and complete R084/M9 acceptance remain outside this check.
