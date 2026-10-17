# SketchyUp 1.2 support matrix

SketchyUp 1.2.0. This is the support declaration for the 1.2.0 release. It is the 1.1.x
declaration (1.1.1) plus configurable components, Extended row F02, and the provider
profile evaluated with corpus version 3; no other supported configuration changed. The
1.1.x declaration is the 1.0.x declaration (1.0.0, 2026-10-13) plus documentation
sheets, Extended row F01.
**Supported** means the configuration has release evidence. **Untested** means no
evidence exists for this release; it is neither claimed to work nor known to fail.

- **Supported:** Arch Linux x86_64 with the `sketchyup` 1.2.0-1 package, Qt 6.8.0
  through 6.12.0 and an OpenGL 3.3 core-profile driver. Omarchy/Hyprland on Wayland
  is the primary desktop; the Qt X11 path is also tested. Ordinary modeling needs
  no network, account, provider or Blender. The assistant's supported provider is
  OpenAI through a ChatGPT-plan sign-in. Documentation sheets (F01) and configurable
  components (F02) are supported with the limits listed under
  [Exchange boundaries](#exchange-boundaries).
- **Untested**, by owner waiver ([ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md)):
  Intel graphics performance, X11 output hotplug, suspend/resume, a physical 150%
  output, physical pointer capture, input methods, automatic X11 output placement,
  D-020 on Hyprland and an interactive Orca session.
- **Experimental:** the local Ollama assistant adapter ([ADR 0160](decisions/0160-provider-scope-for-1-0.md)).
- **Not in this release:** the Extended rows F03–F07 and the Investigate rows of the
  [scope matrix](SCOPE.md), including native SKP/DWG, Ruby extensions, SketchUp
  Dynamic Components and full SketchUp parity.

The 1.0 release-candidate evidence and the M0–M9 gate record are in
[verification/M9.md](verification/M9.md). The F01 release gate is
[M10 acceptance](M10_ACCEPTANCE.md); its record, with the 1.1.0 release-candidate
evidence, is [verification/F01-1.1.0.md](verification/F01-1.1.0.md). The F02 release
uses the same gate; its record, with the 1.2.0 release-candidate evidence, will be
`docs/verification/F02-1.2.0.md`. The [release notes](RELEASE_NOTES.md) list
versions, upgrade and rollback notes and known limitations. The
[roadmap](PR_ROADMAP.md) traces each Core row to its work.

## Runtime and services

| Area | Current boundary | Evidence / open work |
| --- | --- | --- |
| Linux desktop | Native Qt Widgets/OpenGL application; Arch/Omarchy target; Wayland and X11 paths | [Current integration](verification/R085b-native-surface-color.md); physical Wayland output moves between stable outputs pass (R085.h); Wayland hotplug is owner-attested with a partial automated record (R085.r). **Untested** ([ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md)): X11 hotplug, suspend/resume, a physical 150% output, physical pointer capture, input methods and automatic X11 output placement |
| Qt/OpenGL | Build requires Qt 6.8 or later and an OpenGL 3.3 core profile. CI and the release package build use Qt 6.11.2 | Clean machines run the package on Qt 6.8.0 through 6.12.0 ([R087.u](verification/R087u-clean-machine-packages.md)). Qt releases outside that range are untested; [surface-color policy](decisions/0157-native-surface-color.md) requests eight-bit channels |
| Hardware | Recorded AMD discrete-GPU campaigns, earlier Intel integrated-GPU exploratory measurements and isolated llvmpipe checks | [Real-model baseline](verification/R082a-real-model-baseline.md). **AMD discrete-GPU results are recorded** (R082.ii, 2026-10-11, RX 6800 XT, Mesa 26.3.0-devel, Qt 6.11.2): the 100k families, history and 100 MB file campaigns have no miss, and the 10,000-placement fixture stays within the 1.25 GiB memory class (peak RSS 616–759 MB) with complete-frame p95 17.3–21.7 ms; edit feedback at 10,000 placements, which missed the 100 ms target for the deep and textures families (D-024), now measures 36–41 ms p95 for every family after R082.jj. **Intel campaigns are untested** at the current revision ([ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md)); the earlier one-CPU Intel frame miss on 100k scenes (D-004) and the unremeasured Intel/Wayland open-progress gap (D-005) stay recorded. The unshared (sharing-off) memory control was not run on hardware |
| Scaling/accessibility | Tested keyboard construction, Outliner, shortcut editor, contrast/text-size cases and real AT-SPI bridge operations | [R084 inventory and follow-ups](verification/R084a-local-accessibility-inventory.md); these do not establish every dialog, keyboard layout or interactive screen-reader workflow. No Orca session has been run and input methods are untested ([ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md)). The 3D viewport is a rendered canvas; its limits and keyboard alternatives are in the [user guide](USER_GUIDE.md#accessibility-and-keyboard-use) |
| OpenAI (supported provider for 1.0) | ChatGPT-plan sign-in (the same ChatGPT subscription that includes Codex, through SketchyUp's own browser sign-in) and a separate API-key adapter; OS credential storage; gpt-6-astra passes the frozen remote corpus | [M5](verification/M5.md), [M6](verification/M6.md), [24-check remote evaluation](verification/R086b-remote-evaluation.md). The supported profile (gpt-6-astra over ChatGPT-plan sign-in) was evaluated at the 1.0 release candidate with [corpus v2](verification/R086by-corpus-v2.md) (results in [verification/M9.md](verification/M9.md)) and again at the 1.1.0 release candidate (results in the [1.1.0 release record](verification/F01-1.1.0.md)); v2 is the 1.1.x corpus. 1.2.0 changed the ChatGPT-plan limits and the assistant instructions, so from 1.2.0 the profile (524,288 tokens per task, no per-reply output cap) is evaluated at the release candidate with [corpus v3](verification/R086d-corpus-v3.md), and the results are in the 1.2.0 release record. By [owner decision](decisions/0160-provider-scope-for-1-0.md), this remote connection is the 1.0 provider requirement for scope row A05 |
| Local assistant (experimental, not a 1.0 gate) | Explicit numeric-loopback Ollama adapter; retained Qwen CPU profile fails all seven frozen live-task thresholds and remains experimental. A passing local configuration is not a 1.0 requirement ([ADR 0160](decisions/0160-provider-scope-for-1-0.md)) | [Measured profile and failures](verification/R086c-local-evaluation.md); no quality guarantee, automatic download or cloud fallback |
| Blender | Optional Blender 5.2 LTS rendering/handoff, with runtime/device checks; the worker rejects other Blender versions | CI runs Blender 5.2.2. [M7 procedure](M7_ACCEPTANCE.md); CPU/actual consumer evidence does not validate every GPU/driver combination |
| Ordinary modeling | No provider, account, browser runtime or Blender required | Shared native/core operations; [combined exchange study](M8_ACCEPTANCE.md) also exercises offline editing and undo |
| Packaging | Arch package `sketchyup` 1.2.0-1 (x86_64), MIT application code plus retained dependency notices. Upgrades from 1.1.1-1; 1.1.1-1 upgraded from 1.1.0-1 ([F01.a](verification/F01a-1.1.1-patch.md)); 1.1.0-1 upgraded from 1.0.1-1; 1.0.1-1 upgraded from 1.0.0-1, and 1.0.0-1 from development package 0.1.0-3 | [Clean current-source lifecycle and prior-application upgrade](verification/R087g-clean-current-package.md); [current and reference Arch clean machines](verification/R087u-clean-machine-packages.md) (Qt 6.8.0 through 6.12.0). PKGBUILD dependencies carry no versions, so pacman installs a package built against newer Qt on an older system where it cannot start (R087.u). Install the package built for your repository snapshot |

Physical display scale and **View → Interface text size** are separate controls.
Qt client scale factors in isolated tests emulate fractional sizing; they do not
prove physical mixed-DPI transitions. The R085 record retains an observed
RGB565/odd-stride compositor failure; the eight-bit surface correction and its
revalidation must be reviewed without deleting that history.

## Exchange boundaries

| Format / workflow | Implemented direction | Preservation and limits |
| --- | --- | --- |
| Native `.sketchyup` | Open/save, validate, migrate to a new path | Document schema 28 for documents with component parameters, 27 for documents with sheets and no parameters, and 26 otherwise, container 2; reads 1 through 28; embedded assets and explicit limits; unknown future versions reject. SketchyUp 1.1.x and 1.0.x refuse schema 28 unchanged, and 1.0.x refuses schema 27 unchanged. [Contract](decisions/0108-public-native-format.md) |
| Documentation sheets (F01) | Pages inside the native document: linked views of saved scenes at a declared scale, page items, title blocks; PDF export with preflight and a reconciliation report; print at 100%; CLI `--export-sheets` and the drawing-sheet operations and queries | One model per sheet set. Edge splits and Make component break paper dimensions; Reattach repairs them. Raster (perspective and rendered) views use the viewport's visibility, not the scene's, and the CLI cannot render them. The PDF page box is rounded to whole points (within 0.18 mm); measured scale is exact. 256 sheets, 1,024 views and 4,096 items per document. [ADR 0163](decisions/0163-documentation-sheets.md), [ADR 0164](decisions/0164-sheet-layout-interface.md), [R090.e](verification/R090e-drawing-sheets.md), [R091.h](verification/R091h-sheet-layout.md) |
| Configurable components (F02) | Parameters and drivers on component definitions with per-placement values; Entity info parameters, definition authoring and previews in the desktop; the `component_parameter.*` operations and `component_parameters.*` queries over MCP, the headless session and the CLI; six bundled assemblies, installed in `/usr/share/sketchyup/assemblies`, placed with values from the Components tab | One-way formulas over the definition's own parameters; no geometric constraint solving; nested placements take their values from the parent. The in-app assistant cannot change component parameters. **SketchUp Dynamic Components are not imported or converted**, and their formulas are not compatible. 128 parameters and 256 drivers per definition, 1,024 configurable definitions and 4,096 distinct variants per document. Variant rendering is measured in software only. [ADR 0165](decisions/0165-constrained-component-parameters.md), [ADR 0166](decisions/0166-configurable-component-interface.md), [R093.e](verification/R093e-component-parameters.md), [R094.g](verification/R094g-variant-instancing.md) |
| Formline v1 | Import | Editable boxes and segmented cylinders with conversion notices; source unchanged. [Contract](decisions/0014-formline-import.md) |
| GLB/glTF | Import both; export GLB package | Editable converted meshes, hierarchy/instances and supported appearance/cameras; native history/topology/analytical semantics do not round-trip. Import and export size bounds differ. [Contract](decisions/0111-native-gltf-import-workflow.md) |
| OBJ/MTL | Import/export package | Polygons, wires, groups and supported diffuse appearance; explicit units/up axis and contained resources. Export includes hidden geometry without section clipping; losses are reported. [Contract](decisions/0115-native-obj-interchange-workflow.md) |
| STL | Binary/ASCII import/export | Triangle geometry with explicit units/axis; optional import welding. No material or hierarchy fidelity. [Contract](decisions/0119-native-stl-interchange-workflow.md) |
| DXF | Documented 2D import/export subset | Lines, supported polylines, arcs, circles and layers; world-XY export and explicit units. This is not DWG compatibility. [Contract](decisions/0123-native-dxf-interchange-workflow.md) |
| PDF/SVG | Measured orthographic export | Physical page/scale; technical vectors or identified raster appearance; print at actual size. [Contract](decisions/0127-measured-export-cli.md) |
| PNG / Blender scene | Rendered image, frame sequence, one-way `.blend` handoff | Immutable model capture and transfer notices; Blender edits do not flow back into native records. [Procedure](M7_ACCEPTANCE.md) |
| Templates/components | Native library bundles | Fresh model from template; editable component insertion with native undo. Bundles and templates keep component parameters, and are schema 28 when they have any. [Combined verification procedure](M8_ACCEPTANCE.md) |
| Extensions/agent recipes | Versioned native interfaces | Public commands and bounded helper execution; not Ruby extension compatibility. [Agent guide](AI_MODELING.md) and [generated reference](TOOL_REFERENCE.md) |

Interchange is conversion, not native semantic identity. Check each report before
using imported dimensions/materials or presenting an exported drawing. Native
SKP/DWG support and other conditional compatibility features remain governed by
their explicit investigation gates; an available conversion path does not count
as native support.

## Release gate and untested configurations

The [59 Core rows](SCOPE.md) are the 1.0 acceptance scope, not a claim of full
SketchUp parity. The M9 gate requires performance, accessibility, recovery,
provider, package and documentation evidence at the release candidate, plus the
aggregate M0–M9 audit. That record is [verification/M9.md](verification/M9.md). No
screenshot, source archive, passing subset or external render substitutes for it.

The owner waived the remaining physical and hardware checks and the manual walkthrough
on the installed release candidate for 1.0 ([ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md)).
Those checks are **untested**, not passed: Intel campaigns, X11 hotplug, suspend/resume,
a physical 150% output, physical pointer capture, input methods, D-020 on Hyprland,
automatic X11 output placement and an interactive screen-reader session. The owner tests them in
daily use. The automated release-candidate checks stay required.

Documentation sheets (F01) are gated separately by [M10 acceptance](M10_ACCEPTANCE.md),
at the 1.1.0 release candidate. The same waiver applies there: the checks listed above
stay untested, and the automated checks, including the sheet suites on X11 and Wayland
at 1× and 2×, stay required.

Configurable components (F02) are gated the same way at the 1.2.0 release candidate,
with the component suites on X11 and Wayland at 1× and 2×. The waiver applies again;
in particular the variant-rendering budgets were measured in software only
([R094.g](verification/R094g-variant-instancing.md)). No other Extended row is claimed.

Performance results exist for an AMD discrete GPU only
([R082.ii](verification/R082ii-amd-hardware-session.md), [R082.jj](verification/R082jj-edit-feedback.md)).
The 10,000-placement fixture fits the public limits since
[R082.gg](verification/R082gg-placement-limits.md) stopped counting placements as
unique geometry. The unique-geometry limits and the performance targets are unchanged. The Intel one-CPU frame miss
(D-004) and the Intel/Wayland open-progress gap (D-005) stay recorded and untested
at 1.0. Follow the [R082 baseline](verification/R082a-real-model-baseline.md) for
the fixture caveats.
