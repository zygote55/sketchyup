# R085i — Physical output transitions with explicit test-window placement

The final matrix passes native Wayland and X11 transitions across both physical
Intel-driven displays. Wayland uses Qt's fullscreen output request. X11 uses an
explicit helper to move the fixture's own window to the target output's active
workspace. This qualifies that assisted transition, not automatic X11 placement
or the complete platform/release gate. The earlier Qt-only X11 failures remain
in [R085h](R085h-physical-outputs.md).

## Measured behavior

| Platform | Output | Qt effective scale | Framebuffer | Placement |
| --- | --- | --- | --- | --- |
| Wayland | eDP-1 | 2 | 2836 × 1756 | Qt fullscreen request |
| Wayland | DP-7 | 1.6 | 3805 × 2125 | Qt fullscreen request |
| X11/XWayland | eDP-1 | 1 | 2858 × 1778 | Explicit test helper |
| X11/XWayland | DP-7 | 1 | 3818 × 2138 | Explicit test helper |

Each transition verifies actual Qt screen identity, eight-bit RGB channels,
rendered pixels, aligned picking, context recreation and zero GL errors.
Document bytes remain unchanged. X11's scale of 1 does not demonstrate native
fractional-DPR behavior. The context generations advance from 3 to 5 across the
outputs. Physical monitor settings stay unchanged; only the owned test window
moves. Displays must already be awake.

## Helper and reproduction

`SKETCHYUP_TEST_OUTPUT_HELPER` is optional and used only with
`viewport_tests --screens`. The fixture passes its PID and requested screen name,
requires successful bounded completion, and records `external test helper` in
each output result. Helper errors are forwarded into the test's diagnostics.

The supplied helper selects exactly one mapped window with that PID and the exact
title `SketchyUp output test <screen>`. It validates the address and target
workspace, then uses the current [Hyprland window dispatcher](https://wiki.hypr.land/configuring/core/dispatchers/)
with `follow=false` and an explicit address. It never targets the active window,
changes configuration, switches workspaces, or wakes a display. The measured
compositor is Hyprland 0.56.2 `efb50993780079460b0cbed1363e2166a2de1d9f`.
The helper uses the Lua dispatcher interface; older legacy syntax is not its
supported reproduction path.

From an existing awake Hyprland session, under the recorded process limits:

```sh
QT_QPA_PLATFORM=xcb SKETCHYUP_TEST_OUTPUT_HELPER="$PWD/scripts/test-hyprland-output.py" \
  /path/to/viewport_tests --screens
```

## Retained attempts and validation

- R085j: two normal Wayland scale checks pass; physical Wayland passes, but the
  initial X11 helper fails. Its child error was not yet forwarded.
- R085k: two more normal scale checks pass; physical Wayland passes. X11 fails
  the earlier camera/transparency cache assertion before reaching placement.
  This separate intermittent finding is retained and remains unexplained.
- R085l: one diagnostic retry of the same binary passes Wayland and reaches the
  X11 helper. The obsolete dispatcher fails with exit 7; diagnostics are retained.
- R085m: replacing the helper's legacy command with the current Lua dispatcher
  passes both physical platform cases. The binary is unchanged from R085k.

No failed run is relabeled as passed. These four normal checks and the final
physical matrix are not sanitizer, hotplug, suspend/resume, input-method,
compositor-presentation or full reference-hardware qualification.

## Provenance and resource bounds

The final compiled fixture is `ea9b220c8da304d82c6e7567e728de079d98148b`; helper-only commit
`8792a6b10f358f2ca7e726821d7e17c1dcea365a` does not change the C++ fixture. Its stripped binary is
SHA-256 `73d4392e1ebaa9b4a8a99e617600b1fde74198cb171cc80e7eb03b67692257f1`. All 11 captured source overlays were checked
against their recorded commits. The build reused the Release package cache at
`29fb31b`; it is not a clean rebuild of the final branch. The shared-limit refactor
is not rebuilt by these overlays, and all numeric limits remain unchanged.

Build and physical workers use at most eight CPUs, 4 GiB RAM, no additional swap
and disk-backed temporary files. Builds use two outer jobs and two LTO workers
per link. Source overlays and link flags are restored afterward. Local heavy
jobs run sequentially under the shared lock.

[The manifest](R085i-assisted-output-transitions.json) contains raw report/log
hashes, exact overlay inputs, limits, attempts and final platform results.
