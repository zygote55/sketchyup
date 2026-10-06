# R064.e — Native section editing, drawing and selection

2026-10-06, implementation `fedac2e`, integrated/CI head `1a2b925`.
[Contract](../decisions/0076-native-sections.md). The Sections panel creates,
edits, activates, reverses and deletes context-scoped cuts through the shared
command path. Drawing, picking, selection and inference use derived geometry;
authoritative topology and triangulation remain intact.

All **116 suites passed**: 115 in the complete CTest run (**109.33 s**, with the
explicit Blender test skipped because its opt-in variable was unset), then the
actual Blender suite with `SKETCHYUP_BLENDER_TEST=/usr/bin/blender` (**7.33 s**).
The inference suite passed ASan/UBSan with leak detection in **3.47 s**. Both builds
emitted no compiler warnings. Logs: `build/r064e-complete-{build,ctest}.log`,
`build/r064e-blender-real.log`, `build/r064e-sanitize-{build,ctest}.log`.

**36 native display checks passed in 127.861 s**: section editor and viewport,
selection, inference, styles, textures and scenes on Wayland/X11 at scales 1/2;
the two section tests also ran under ASan/UBSan in all four display variants
(**eight sanitized checks, 35.120 s**). Leak detection remains enabled without
suppressions. Sanitized Wayland uses the isolated client fix documented in
[the dependency investigation](R062b-wayland-proxy.md); other variants use system
libraries. [Native results](R064e-native.json).

Coverage includes nested/mirrored three-plane cuts, sibling scope, cap colors,
CPU picking before repaint, context-only GPU cap selection, retained native edge
identity, cap occlusion of edge picking and inference, clipped guide selection,
window containment of retained geometry, independent texture coordinates,
unchanged native triangulation, Undo/Redo and exact persistence. Editor checks
cover normal normalization, units, inline errors, one-step create/activate,
relocation/deactivation, unchanged precision, stale drafts, no-ops and delete Undo.

The packaged [native example](../../examples/section-nested.sketchyup) reopens
byte-for-byte. Installed CLI checks deactivate/reactivate all three cuts while
preserving geometry and records. Seven catalogs, the contract, fixture and desktop
binary match their source/build inputs. [Installed results](R064e-installed-smoke.json).
All **115 install inputs** match the source archive byte-for-byte; no build or Git
metadata is included. [Package results](R064e-source-package.json).

Raw native evidence: [section view](R064e-sections.png),
[editor](R064e-section-editor.png). Remote CI and dependency merges remain required.
Section-aware export is the final R064 layer; R064 delivery is not yet accepted.
