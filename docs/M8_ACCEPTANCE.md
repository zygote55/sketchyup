# M8 exchange and extensibility acceptance

This procedure defines the gate; it is not an acceptance result. M7 must be
accepted and R073–R081 must pass CI and merge before recording accepted M8 status.
Local overlap on stable interfaces does not waive those prerequisites.

## Combined study

Build `m8_exchange_tests` and execute it with an offscreen Qt platform. Supplying
a new directory retains the outputs for review; an existing directory rejects.

```sh
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=generic QT_IM_MODULE=compose \
  build/dev/m8_exchange_tests build/m8-study
```

The study creates a native 2 × 3 × 4 m component with material, captures a complete
template and a component library bundle, and verifies fresh template identity and
one-step component insertion undo/redo. It exchanges the same measured model
through glTF, OBJ, binary STL and ASCII STL. Every imported version must retain
12 triangles, a 2 × 3 × 4 m envelope and 52 m² surface area. Explicit exact STL
welding additionally reconstructs a closed 24 m³ solid. The separate XY footprint
passes DXF import/export with a 10 m perimeter.

An orthographic view at 1:50 must measure 40 × 60 mm on the page and publish actual
PDF/SVG content. The shipped extension resolves through its real helper, applies a
6 m² panel using public commands, and undoes without changing original template
content. Revision and allocator counters remain monotonic by design. The source
native document and its history must survive every exchange path unchanged.

Keep the retained native file, library bundles, interchange packages and measured
outputs. Independently open/review them. This study supplements the actual Blender
producer/consumer, ezdxf and Poppler checks; it does not replace those or the
format-specific malformed-input, bounds, cancellation and precision regressions.

## Required evidence

- Complete regression suite and dedicated ASan/UBSan tests, without suppressed
  application findings.
- Real external producer/consumer tests for glTF/OBJ/STL, DXF and measured output.
- Native import/export/library/extension workflows on Wayland and X11 at 1×/2×,
  including cancellation, source/overwrite preservation, dirty-document prompts,
  stale extension results, native editing and undo.
- All shipped recipe geometry/reload checks against source and installed CLIs;
  generated reference drift check and optional dependency discovery.
- Byte-exact installed guide/catalogs/examples/contracts and source-package inputs;
  extension/text helper discovery in the installed layout.
- Explicit format losses and remaining limitations. No opaque exchange result,
  external render or unavailable dependency counts as an editable native feature.

The accepted record must identify source commits, actual tool versions, fixture
artifacts, timing scope and unresolved limits. No credential or private provider
artifact belongs in public evidence. Release performance, hardware, accessibility,
provider revalidation and packaging gates remain M9 work.
