# R084.g — Integrated selected-text contrast

2026-10-07. Integration `587ddf8`; correction `41dbd13`.
Parent: [PR #213](https://github.com/zygote55/sketchyup/pull/213).

The [final native Wayland 2× palette check](R084g-integrated-native.json) passes in
**3.006 s**. Dark-theme selected Measurements text increases from **1.859:1** to
**8.052:1** with an explicit theme-specific foreground; light selection remains
**4.995:1**. Input, button and secondary text also exceed 4.5:1. The retained
[before/after reports and eight-case matrix](R084g-selected-text-contrast.md) pass
normal **11.351 s**, ASan/UBSan **15.123 s**, preserving model/history.

[Installed checks](R084g-final-installed-smoke.json) match all four executables and
installed contracts/catalogs, including ADR 0150. The [source package](R084g-final-source-package.json)
contains **209 byte-exact installed inputs**, SHA-256
`e3420165bea4a43c7fd1eed6420b2372e0b503338cfa44aa0a3ce292817c29d0`.
This bounded active-control check does not accept every dialog, disabled state,
rich-text link or overlay. Those audit layers, remote CI, ordered merges and full
R084/M9 acceptance remain open. All settings are private to the fixture.
