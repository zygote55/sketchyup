# R084.m — Integrated filtered shortcut keyboard workflow

2026-10-07. Product correction `4d10182`; final fixture `fe232ff`.
Parent: [PR #219](https://github.com/zygote55/sketchyup/pull/219).

The [final Wayland 2× check](R084m-integrated-native.json) passes in **6.216 s**.
Actual keys discover the editor, filter Rectangle, assign and save Ctrl+Alt+R,
activate the rebound tool, then restore defaults. The pre-fix filtering failure
and [eight-case normal/sanitized matrix](R084m-filtered-shortcut-keyboard.md) are
retained. No command outside the filtered list remains a keyboard destination.

[Installed executables and contracts](R084m-final-installed-smoke.json) match,
including ADR 0156. The [source package](R084m-final-source-package.json) contains
**215 byte-exact installed inputs**, SHA-256
`ce90883fe8972778758f1745321ebc7ec2399359e3441231e2f2350db3bf6819`.
Remote CI, ordered merges and full R084/M9 acceptance remain open.
