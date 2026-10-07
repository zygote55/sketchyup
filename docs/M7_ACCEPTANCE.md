# M7 presentation acceptance procedure

The presentation checkpoint combines R061–R072 on one six-by-four-metre building
with 200 mm walls, two shared window components, a repeating floor image and
resolved six-metre, four-metre and 2.7-metre annotations. The installed example is
`examples/m7-building-study.sketchyup`. Its three saved views are a textured
perspective, a dimensioned orthographic plan and a named horizontal section at
1.4 metres. Solar location and civil time are explicit and offline.

Open the example and recall each view through Scenes. Check that the dimensions
are consistent and that the section view clips geometry and dimensions whose
attached endpoints are removed. File → Export view as PNG produces exact pixel
sizes including native annotations. The independent native test exports all three
views at 1200 × 900 and verifies camera recall, texture completion, resolved
references, unchanged model/history during image export, and exact save/reopen.
Look around and Walk use the R072 keyboard controls without editing this model.

The following built targets reproduce the headless and native checks:

```sh
ctest --test-dir build/dev -R '^m7_presentation$' --output-on-failure
SKETCHYUP_TEST_SCALE=2 scripts/test-wayland.sh build/dev/m7_presentation_input_tests
xvfb-run -a env QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=2 LIBGL_ALWAYS_SOFTWARE=1 \
  build/dev/m7_presentation_input_tests
```

Repeat native checks at scale 1 and under ASan/UBSan with leak detection. Use the
repository's private Wayland client correction where required by the current Qt/
Wayland versions, as documented in the existing platform evidence. Do not suppress
application leaks. This procedure does not alter desktop configuration.

To retain artifacts, set `SKETCHYUP_M7_EVIDENCE` to an explicit scratch directory.
The headless test saves the native study and a dimension/camera/loss report. The
native test saves three PNG views, the recalled native model and its report. Keep
native and headless output folders separate. Blender remains optional:

```sh
SKETCHYUP_BLENDER_TEST=/usr/bin/blender \
  ctest --test-dir build/dev -R '^m7_presentation_real$' --output-on-failure
```

The real test renders the three exact saved camera keyframes as a bounded PNG
sequence, using Cycles at 320 × 240 with four samples for fast acceptance. It
verifies completed frame jobs, hands the perspective to a packed `.blend`, removes
the disposable transfer package and independently reopens the saved scene. The
reopened scene must contain meshes, a perspective camera, a packed image and the
configured SUN light. All jobs leave the native document and its history unchanged.
The retained `.blend` can be opened in Blender for an independent visual check.
The low-sample acceptance images are not a rendering quality benchmark.

Native annotations are presentation overlays and are explicitly omitted from GLB,
Blender renders and handoff. The loss report records all four omitted annotations.
Native viewport styles and Blender lighting are separate renderers; matching camera,
geometry, section, materials and recorded settings does not promise pixel-identical
images. R069 covers renderer-specific lighting oracles. R070 covers durable job
storage, bounded scheduling and recovery; R071 covers portable packed assets on
both tested Blender versions. R072 additionally verifies interrupted frame exports
retain completed frames and preserve document state.

Publish the M7 milestone record only after every R061–R072 layer, these integrated
checks, installed/source-package checks and required remote CI/ordered merges pass.
M8 work may overlap on stable interfaces; it does not imply that this gate passed.
