# R084.a — Integrated native accessibility inventory

2026-10-07. Integration `512c130`; fixture `3504bab` with host-mode clarification
`f6f578f`. Parent: [PR #207](https://github.com/zygote55/sketchyup/pull/207).

The final [native Wayland inventory](R084a-integrated-matrix.json) passes at 1× and
2× in **6.589 s**. Each captures **24 panel/theme contexts**, **152 public actions**
and **12 actual F6 routing steps**, preserving document content. The action count
reflects the integrated feature stack. The [original inventory](R084a-local-accessibility-inventory.md)
retains detailed naming findings and the actual fractional-scale host inventory;
the latter explicitly did not exercise keyboard routing because activation was
not granted. Unnamed controls are triage findings, not an accessibility pass.

[Installed checks](R084a-final-installed-smoke.json) confirm unchanged application
and helper binaries, byte-exact installed contracts/catalogs and ADR 0145. The
[source package](R084a-final-source-package.json) contains **204 byte-exact installed
inputs**, SHA-256 `e277a27459b6db258843959b965cae11bbff7acc017452979b97d1ade46f0094`.
This adds an audit fixture; naming fixes, keyboard workflows, preferences, contrast,
text scaling and assistive-technology acceptance follow separately. R084/M9 remain
open. No real provider settings or credentials are used.
