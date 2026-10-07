# R065.d — Native dimensions and labels

2026-10-06. Implementation `e7c4d37`, editor/example fixes `1364d25`, framebuffer
fixture `4b93002`; integrated acceptance head `f4cf2c1`. Parent: PR #146.
Contract: [0081](../decisions/0081-native-annotations.md).

The complete Debug build and **121/121 CTest suites passed in 115.99 s**, including
actual Blender acceptance. The native matrix passed **16/16 cases in 60.306 s**:
input and viewport tests on Wayland/X11 at 1×/2× scale, normal and ASan/UBSan builds.
Sanitized runs enable leak detection and halt-on-error. Wayland sanitizer runs use
the documented private client fix, without leak suppressions.
[Machine-readable native results](R065d-native.json).

Input fixtures cover selection-based dimensions/labels, explicit units and fixed
points, Unicode/multiline labels, exact untouched values, one-step Undo, save stamps,
split ambiguity, deletion/missing references, metadata-only edits of broken records,
explicit rebinding, no-op rebinding, stale drafts, reflected/scaled geometry and
persistence. Framebuffer fixtures verify overlays, changed unit text, exact Undo,
red broken-reference markers, hidden/section-clipped suppression and unchanged native
mesh build counts. Broken dimensions do not display stale numeric measurements.

The packaged panel example has exact four- and three-metre vertex dimensions and a
face-attached multiline label. Its [raw viewport framebuffer](R065d-annotated-panel.png)
and [native editor capture](R065d-editor.png) were visually reviewed. Whole-widget
Qt grabs omit OpenGL painter overlays on this platform, so the viewport evidence is
captured directly from the framebuffer.

The [installed check](R065d-installed-smoke.json) runs without display variables:
byte-exact native example import, resolved dimensions and face label, millimetre
presentation, geometry movement preserving distances and updating world text points,
label editing and byte-exact relocated save. Seven catalogs, the contract and the
installed desktop executable match their build inputs.

[Source-package verification](R065d-source-package.json): **122 installed inputs**
match byte for byte; build and Git artifacts are excluded. Archive SHA-256:
`ffae372f9b07afe5b2d1703709db71bbc5618ad86f7f8d18cb5987cf585321a8`.

R065 implementation and local acceptance are complete. Remote CI and ordered
merges remain delivery gates. Editable 3D text belongs to R066. Provider settings
and private live-provider evidence are unchanged.
