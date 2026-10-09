# R084.b — Integrated accessible region names

2026-10-07. Integration `0b94a3b`; original naming fix `651ba21`;
integrated scene-timing follow-up `4449f8f`.
Parent: [PR #208](https://github.com/zygote55/sketchyup/pull/208).

The final [Wayland 1×/2× inventory](R084b-integrated-matrix.json) passes in
**6.584 s**. Both inspect **24 panel/theme contexts**, **152 public actions** and
**12 F6 transitions**, with **zero unnamed visible enabled tab-focusable controls**.
Document content remains unchanged. The native History interaction regression
also passes at Wayland 2×.

The first integrated rerun found an additional unnamed line editor in the newer
camera-transition duration control, beyond the original eight region findings.
The spin box and its internal editor now both expose a name including milliseconds.
This preserves the control's existing behavior while making its purpose explicit.
The final inventory includes that correction; the earlier failed assertion was
not accepted as a pass.

[Installed checks](R084b-final-installed-smoke.json) match all four application/helper
executables and every installed contract/catalog to the verified build/source.
The [source package](R084b-final-source-package.json) contains **204 byte-exact
installed inputs**, SHA-256 `3f360c120a0f77bde37b3d663f5360a1f6ed6bb8301564e49f8dc06846aedc4f`.
This bounded naming/focus inventory does not prove screen-reader usability or all
keyboard workflows. Remote CI, ordered merges and full R084/M9 acceptance remain open.
