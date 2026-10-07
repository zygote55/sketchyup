# R084.l — Integrated Linux accessibility bridge

2026-10-07. Integration `30bc9f2`; selection-description correction `674aa4e`.
Parent: [PR #218](https://github.com/zygote55/sketchyup/pull/218).

The [installed application check](R084l-integrated-native.json) passes native
Wayland 2× in **4.236 s**. The real AT-SPI bridge selects the demo slab, reads its
updated selection description, opens Entity info and reads bounds, area and volume
([report](R084l-integrated-bridge.json)). The pre-fix installed application fails
the same selection-description expectation; [four normal platform cases](R084l-linux-accessibility-bridge.md)
pass on the corrected source. No interactive speech/braille or sanitizer claim is made.

[Installed executables and contracts](R084l-final-installed-smoke.json) match,
including ADR 0155. The [source package](R084l-final-source-package.json) contains
**214 byte-exact installed inputs**, SHA-256
`71ce89b30a6e6aa3c6fac7bec73a8e7f633c91261650e92a4e774c09e4544e0f`.
The fixture only touches its own demo process, private settings and isolated D-Bus
session. Full accessibility/release acceptance and ordered merges remain open.
