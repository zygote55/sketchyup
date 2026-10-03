# ADR 0001: release baseline and decision register

Date: 2026-10-03. Roadmap: R001. Status: accepted baseline.

The owner selected MIT for application code. Third-party code retains its
license. We dynamically link Qt base modules; no Qt Quick 3D, proprietary SDK,
geometry library or prototype dependency is authorized by this decision.

## Reference and scope

Freeze SketchUp Desktop 2026.0 (Windows 26.0.429, Mac 26.0.428) as the behavior
reference, rather than chasing the latest release. This is a documented workflow
baseline, not a claim that proprietary binaries were executed on this machine.
Source: [official release notes](https://help.sketchup.com/en/release-notes/sketchup-desktop-20260).
The 73 rows of [SCOPE.md](../SCOPE.md) remain the release inventory.

| Reproducible workflow | Scope | Acceptance fixture |
| --- | --- | --- |
| Draw precise room outlines, form faces, extrude walls and cut an opening | G01–G10, E01 | geometry/room-opening |
| Edit a window definition, then make one instance unique | O01–O06, E02–E03 | organization/window-instances |
| Save, reopen, undo, interrupt a save, recover unsaved work | D01–D06, X06 | document/recovery |
| Preview an AI edit; intervene manually; reject stale commit; reconcile lost response | A01–A08 | automation/room-window |
| Render and cancel without modifying the model; run without Blender | P08–P09 | integration/blender-room |
| Style, annotate, section and export a building study | P01–P07, X02–X04 | presentation/building-study |
| Install, launch and operate with keyboard and mixed display scales | N01–N05 | platform/native-desktop |
| Expanded workflows and proprietary integrations | F01–F07, C01–C07 | Separate M10 gates; no implicit support |

Fixture names use `area/scenario`, with small deterministic inputs, expected
measurements, units/axes, rejection cases and provenance. Never copy proprietary
models into the corpus. Test fixtures created here are original MIT examples.

## Native definition and measured host

Native means compiled C++20, a native Wayland window and offline document editing,
without Electron, Chromium or a web service. X11 remains a fallback test target.
The core builds without Qt UI, an AI provider or Blender.

Host inventory observed on 2026-10-03: x86-64 Omarchy 4.0.4, Linux
7.2.5-3-omarchy, Intel Core Ultra 7 155H, 30 GiB RAM, GCC 16.2.1,
Qt base/Wayland 6.11.2, Ninja 1.13.2. Displays: 2880×1800 at 200% and
3840×2160 at 160%. This is one development host, not the release support matrix.

## Decision register

| Decision | Owner role | Evidence required | Status |
| --- | --- | --- | --- |
| Own-code license | Product owner | Explicit MIT selection | Accepted |
| UI and viewport adapter | Desktop | Wayland spike, depth, picking, scaling and timings | R002 |
| Surface topology and algorithms | Geometry | Open/non-manifold, holes and degeneracy fixtures | R003 |
| IDs, transactions, persistence | Core | Round-trip, stale revision, failure and memory evidence | R004 |
| SKP/DWG support or gap | Interchange | Current vendor platform/distribution evidence | R005 |
| Input, recovery and assistant states | Desktop/Core | C1–C6 state scenarios | R006 |

Each later ADR records date, roadmap/scope IDs, context, options, decision,
evidence, limitations and reconsideration trigger. A dependency is approved only
for its recorded module/version/license and purpose. Distribution must include
applicable notices and satisfy dependency terms; MIT does not replace them.

## Prototype provenance

The uncommitted Formline files remain untouched in the original checkout.
They provide interaction reference only. No Electron/Three.js code or asset has
been imported into the native runtime. The observed format is `formline`, v1,
with Y-up boxes/cylinders, numeric transforms, hex colors and visibility.
R037 will use original synthetic data to verify the right-handed conversion
`(x, y, z) -> (x, -z, y)`, normals, winding and rotation. Screenshot/HTML assets
already in the planning PR remain illustrations, not native test evidence.
