# R084.f — Integrated native shortcut editor

2026-10-07. Integration `569dbc6`; editor `cf13656`, activation correction `bb36346`.
Parent: [PR #212](https://github.com/zygote55/sketchyup/pull/212).

The [final native Wayland 2× regression](R084f-integrated-native.json) passes in
**4.394 s**. It covers staged edits, Cancel/Save, explicit reassignment, actual
rebound key activation, text-field typing, restart/unbind persistence, reset,
unknown settings, stale-editor rejection and floating-assistant routing. The
[retained platform matrix](R084f-native-shortcut-editor.md) passes **8/8**, normal
**14.789 s**, ASan/UBSan **21.721 s**. Additional measured-keyboard and preference
regressions pass native Wayland 2×. The assistant fixture is isolated, submits no
prompt and uses no actual provider settings or credentials.

[Installed checks](R084f-final-installed-smoke.json) match all four executables and
installed contracts/catalogs, including ADR 0149. The [source package](R084f-final-source-package.json)
contains **208 byte-exact installed inputs**, SHA-256
`083fa8b196fb0cbd9e1edfbffcda4f83196c221d28c8b16728abdcee92a2d6d3`.
Package output now uses persistent build storage after the temporary-directory
quota was reached; the successful retry is the recorded artifact.

This editor handles public window commands. Panel-local shortcut reservations,
contrast and text scaling are follow-up audit layers. Remote CI, ordered merges
and full R084/M9 acceptance remain open. The fixture selects list rows/key values
programmatically but activates operations and rebound shortcuts with actual keys;
it is not claimed to be an entirely keyboard-driven editor task.
