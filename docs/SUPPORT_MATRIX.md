# Development support and compatibility matrix

Snapshot: 2026-10-07. This describes the implementation and bounded local evidence
in the review stack. **It is not a 1.0 support declaration.** M0–M6 have accepted
gate records; M7/M8 implementations still require remote CI and ordered merges.
M9 release acceptance remains open. See the [roadmap](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/PR_ROADMAP.md) and
[scope matrix](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/SCOPE.md) for required Core, Extended and Investigate work.

## Runtime and services

| Area | Current boundary | Evidence / open work |
| --- | --- | --- |
| Linux desktop | Native Qt Widgets/OpenGL application; Arch/Omarchy development target; Wayland and X11 paths | [Current integration](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/verification/R085b-native-surface-color.md); physical output moves, suspend/resume and input methods remain open |
| Qt/OpenGL | Build declares Qt 6.8+ and OpenGL 3.3; current local checks use Qt 6.11.2 | The declared minimum is not a claim of complete testing on every Qt release; [surface-color policy](decisions/0157-native-surface-color.md) requests eight-bit channels |
| Hardware | Intel integrated-GPU, AMD discrete-GPU exploratory measurements and isolated llvmpipe checks exist | [Real-model baseline](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/verification/R082a-real-model-baseline.md); a CachyOS/XFCE X11 system with an accelerated AMD Navi 21 GPU and 16 GiB VRAM is now available; controlled reference runs, the full large-model corpus and memory budgets remain open |
| Scaling/accessibility | Tested keyboard construction, Outliner, shortcut editor, contrast/text-size cases and real AT-SPI bridge operations | [R084 inventory and follow-ups](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/verification/R084a-local-accessibility-inventory.md); these do not establish every dialog, keyboard layout or interactive screen-reader workflow |
| OpenAI | ChatGPT-plan and separate API-key adapters; OS credential storage; gpt-6-astra passes the frozen remote corpus | [M5](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/verification/M5.md), [M6](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/verification/M6.md), [24-check remote evaluation](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/verification/R086b-remote-evaluation.md); local-provider and final release acceptance remain gated |
| Local assistant | Explicit numeric-loopback Ollama adapter; retained Qwen CPU profile fails all seven frozen live-task thresholds and remains experimental | [Measured profile and failures](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/verification/R086c-local-evaluation.md); no quality guarantee, automatic download or cloud fallback |
| Blender | Optional Blender 5.2-family rendering/handoff, with runtime/device checks | [M7 procedure](M7_ACCEPTANCE.md); CPU/actual consumer evidence does not validate every GPU/driver combination |
| Ordinary modeling | No provider, account, browser runtime or Blender required | Shared native/core operations; [combined exchange study](M8_ACCEPTANCE.md) also exercises offline editing and undo |
| Packaging | Experimental Arch package `0.1.0-3`, MIT application code plus retained dependency notices | [Clean current-source lifecycle and prior-application upgrade](verification/R087g-clean-current-package.md); current/reference Arch clean-machine and final release-candidate acceptance remain open |

Physical display scale and **View → Interface text size** are separate controls.
Qt client scale factors in isolated tests emulate fractional sizing; they do not
prove physical mixed-DPI transitions. The R085 record retains an observed
RGB565/odd-stride compositor failure; the eight-bit surface correction and its
revalidation must be reviewed without deleting that history.

## Exchange boundaries

| Format / workflow | Implemented direction | Preservation and limits |
| --- | --- | --- |
| Native `.sketchyup` | Open/save, validate, migrate to a new path | Document schema 24, container 2; embedded assets and explicit limits; unknown future versions reject. [Contract](decisions/0108-public-native-format.md) |
| Formline v1 | Import | Editable boxes and segmented cylinders with conversion notices; source unchanged. [Contract](decisions/0014-formline-import.md) |
| GLB/glTF | Import both; export GLB package | Editable converted meshes, hierarchy/instances and supported appearance/cameras; native history/topology/analytical semantics do not round-trip. Import and export size bounds differ. [Contract](decisions/0111-native-gltf-import-workflow.md) |
| OBJ/MTL | Import/export package | Polygons, wires, groups and supported diffuse appearance; explicit units/up axis and contained resources. Export includes hidden geometry without section clipping; losses are reported. [Contract](decisions/0115-native-obj-interchange-workflow.md) |
| STL | Binary/ASCII import/export | Triangle geometry with explicit units/axis; optional import welding. No material or hierarchy fidelity. [Contract](decisions/0119-native-stl-interchange-workflow.md) |
| DXF | Documented 2D import/export subset | Lines, supported polylines, arcs, circles and layers; world-XY export and explicit units. This is not DWG compatibility. [Contract](decisions/0123-native-dxf-interchange-workflow.md) |
| PDF/SVG | Measured orthographic export | Physical page/scale; technical vectors or identified raster appearance; print at actual size. [Contract](decisions/0127-measured-export-cli.md) |
| PNG / Blender scene | Rendered image, frame sequence, one-way `.blend` handoff | Immutable model capture and transfer notices; Blender edits do not flow back into native records. [Procedure](M7_ACCEPTANCE.md) |
| Templates/components | Native library bundles | Fresh model from template; editable component insertion with native undo. [Combined verification procedure](M8_ACCEPTANCE.md) |
| Extensions/agent recipes | Versioned native interfaces | Public commands and bounded helper execution; not Ruby extension compatibility. [Agent guide](AI_MODELING.md) and [generated reference](TOOL_REFERENCE.md) |

Interchange is conversion, not native semantic identity. Check each report before
using imported dimensions/materials or presenting an exported drawing. Native
SKP/DWG support and other conditional compatibility features remain governed by
their explicit investigation gates; an available conversion path does not count
as native support.

## Release work still required

The [59 Core rows](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/SCOPE.md) are the acceptance scope, not a count of completed
features. Final performance/hardware evidence, broad accessibility coverage,
release-candidate recovery checks, repeated provider evaluation, current/reference
package lifecycle checks, verified documentation and the aggregate M0–M9 gate
must pass before a release claim or tag. No screenshot, source archive, passing
subset or external render substitutes for those gates.

The current 100k-triangle real-model fixture has exploratory timings; the larger
instancing fixture exceeds present public aggregate bounds. Those limits have not
been raised or the performance targets relaxed to manufacture a pass. Follow the
[R082 baseline](https://github.com/zygote55/sketchyup/blob/6a5a7d1cd43df585bf940facf83b08f138e1f1a9/docs/verification/R082a-real-model-baseline.md) for the exact caveats.
