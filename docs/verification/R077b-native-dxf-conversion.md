# R077.b — Native DXF wire conversion

2026-10-07. Conversion contract `24b33ed`; complete integration `9ba2dff`.
Parent: [PR #180](https://github.com/zygote55/sketchyup/pull/180).
Contract: [0121](../decisions/0121-native-dxf-wire-conversion.md).

The complete Debug build passes **160/160 CTest suites in 174.46 s**, including
existing actual Blender interchange. Dedicated DXF conversion passes **1/1 in
0.07 s** and ASan/UBSan **1/1 in 0.36 s**, with leak detection and halt-on-error.
Independent ezdxf-produced fixtures verify metre conversion, layer state and analytic
arcs/circles/bulges. Five final native Wayland 2× checks pass in
**22.919 s** ([matrix](R077b-native-matrix.json)).

Each supported source entity becomes an editable wire body in one undoable edit.
Layers become native tags, including hidden/frozen state; locked entities stay
locked. Curves retain analytic metadata and bounded editable chords, with maximum
chord deviation reported. The conversion rejects empty or invalid expanded geometry
and passes the authoritative native persistence validator before publication.
No automatic face filling, intersection splitting or cross-entity welding occurs.

A subsequent test/CI-only correction `157e5bb` publishes queue-test readiness
atomically and reclaims completed CI build artifacts before the unchanged full
package gate. The affected queue test passes again on this integration (**0.65 s**)
and ten consecutive normal plus ten sanitizer runs on the same correction. The
native matrix above ran after that correction; application behavior is unchanged.

The [installed smoke](R077b-installed-smoke.json) verifies eight catalogs,
twenty-six contracts, existing STL/OBJ/glTF workflows and desktop/license bytes.
DXF native conversion is exercised by its dedicated core suite; its native/CLI
workflow follows in R077.d. [Corrected source package](R077b-source-package.json):
**172 installed inputs** match byte for byte; SHA-256 `6921ac2544bf1288d2666ff1e8218214f480ee84cc08d0f4d07e3b6c77388975`.

Remote CI and ordered merges remain delivery gates; M7/M8 acceptance is not claimed.
