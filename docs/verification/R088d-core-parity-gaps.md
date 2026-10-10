# R088.d — Remaining Core parity gaps against main

2026-10-10. Source revision `8534bb67bd1f003c140b6f5f7df6d6385fbc5dc7` (`main`).
This is a **gap analysis against `main`**, not the release-candidate parity report,
an RC walkthrough or a release sign-off. It splits each of the **59 Core scope rows**
into the individual clauses of its [acceptance text](../SCOPE.md) and records which
clauses lack passing evidence. It extends the [R088.b evidence index](R088b-core-evidence-index.md),
which maps fixtures to rows without checking clauses.

Evidence means a test on `main` that asserts the clause, or an accepted gate record
([M1](M1.md)–[M6](M6.md), [M7](M7-accepted.md), [M8](M8.md)). Every native
`*_input_tests` fixture except the accessibility audit, provider trials and
benchmarks runs in the [native workflow](../../.github/workflows/native.yml) on
Xvfb/X11 and isolated Weston. Those runs do not establish physical-device,
compositor or assistive-technology behaviour. A test named here proves its own
assertions, not the whole row.

Clause classes:

- **Evidenced**: a cited assertion or gate record covers the clause.
- **Open**: evidence is missing or incomplete, and the missing part is already on
  an M9 entry's open list ([R082](../PR_ROADMAP.md#r082)–[R088](../PR_ROADMAP.md#r088)).
- **Partial**: some evidence exists; the missing part is not tracked by an M9 entry.
- **Gap**: no evidence, and no M9 entry tracks it.
- **Limitation**: a decision or the support matrix narrows the clause.

A row takes its most severe clause status, in the order Gap, Partial, Open,
Evidenced. Release-candidate revalidation of every row under
[R089](../PR_ROADMAP.md#r089) applies to all rows and is not repeated below.

## Row status

| Scope | Capability | Status | Missing or qualified clauses |
| --- | --- | --- | --- |
| N01 | Native desktop and viewport | Open | Clause text evidenced: Hyprland launch in [M4](M4.md) and [R085.i](R085i-assisted-output-transitions.md); no Electron/browser in `packaging/arch/PKGBUILD:9` depends; resize in `tests/interaction_tests.cpp:186`; rendering smoke `glError: 0` in [R085.b](R085b-native-surface-color.md). **Open:** R082 frame/edit budgets and the R085 supported GPU/driver matrix both list N01. |
| N02 | Build and package | Open | Clean build, install, upgrade, removal, desktop entry, MIME and icon evidenced by `packaging/arch/verify-package.sh:64-99,179-180` in CI and [R087.g](R087g-clean-current-package.md). **Open (R087):** current/reference Arch clean-machine checks. |
| N03 | Native interaction | **Gap** | Shortcuts ([R084.e](R084e-shortcut-bindings.md)/[f](R084f-native-shortcut-editor.md)/[j](R084j-panel-shortcut-conflicts.md)/[m](R084m-filtered-shortcut-keyboard.md)), focus ([R084.d](R084d-keyboard-modeling.md), [R084.k](R084k-keyboard-outliner.md)) and tool-change lifecycle (`tool_lifecycle_tests`) evidenced. **Gap — keyboard layouts:** no test or record uses a non-US layout; ADR 0156 and the [support matrix](../SUPPORT_MATRIX.md) disclaim it. Only the build plan's platform matrix names it, and R085 lists only "input methods". **Partial — pointer capture:** [R002.c](R002-desktop.md) and [R021](R021-tool-lifecycle.md) cover synthetic out-of-bounds, grab-loss and focus-loss events only. Physical cross-window capture is untracked by M9. **Open (R084):** all-dialog access ([R084.o](R084o-native-dialog-controls.md)/[p](R084p-dialog-keyboard.md) cover named dialogs). **Open (R085):** input methods. |
| N04 | Scaling and displays | Open | Client factors 1.5/1.75 are emulated in [R085.b](R085b-native-surface-color.md), with CI at 1×/2×. Physical Wayland outputs at 2 and 1.6 pass in [R085.h](R085h-physical-outputs.md)/[i](R085i-assisted-output-transitions.md); X11 reaches only scale 1, through a helper. Picking alignment is asserted (`tests/viewport_tests.cpp:229`). Overlay/text alignment is asserted only in individual fixtures (e.g. `tests/inference_input_tests.cpp:97`). **Open (R085):** a physical 150% output, automatic X11 placement, hotplug and final acceptance. |
| N05 | Accessibility/preferences | Open | Labels ([R084.b](R084b-accessible-regions.md)/[n](R084n-assistant-preference-labels.md)/[o](R084o-native-dialog-controls.md)), scalable text ([R084.h](R084h-interface-text-size.md)), configurable shortcuts and persisted preferences ([R084.c](R084c-preference-compatibility.md)) evidenced. **Open (R084):** complete keyboard reachability. **Open (R084), likely implementation — visible focus:** [R084.a](R084a-local-accessibility-inventory.md) still lists it; no test asserts it. The stylesheet in `src/app/window.cpp:930-935` defines `:focus` only for line edits, trees, lists, toolbars and push buttons, not for tool buttons, combo boxes, check boxes or spin boxes. **Open (R088):** the viewport accessibility limits are documented only in the design doc ([UX_DESIGN §10](../UX_DESIGN.md)), not in the [user guide](../USER_GUIDE.md). |
| D01 | Units and axes | **Gap** | Metric, fractional feet/inches, locale and coordinate entry evidenced (`tests/measurement_tests.cpp:26-48`). Unit persistence evidenced (`document_units_io`). Component local axes survive save/reopen (`tests/component_input_tests.cpp:171-193`). **Gap — decimal imperial entry:** no assertion of e.g. `2.5in` or `1.5'`. The parser accepts `in`/`ft` suffixes (`src/core/transform.cpp:90-104`), so this probably needs evidence only. **Limitation, inconsistent with tier — precision formatting:** [ADR 0018](../decisions/0018-document-units.md) fixes display precision, and `DisplayUnit` offers only `m`/`mm`/`ft-in` (`src/core/units.hpp:5`). No precision setting exists to persist, although UX_DESIGN §10 lists a "units and precision" preference. Custom drawing planes are session state (M3 record). |
| D02 | Native document format | Evidenced | `native_format`, `persistence`, `parser_fault_corpus`, `saved_scenes_io`, `asset_io`; [ADR 0108](../decisions/0108-public-native-format.md) rejects future versions. R083/R087 revalidation pending. |
| D03 | Undo and transactions | Evidenced | Atomic batches (`history`, `transaction_sequence`). A new edit clears redo (`tests/core_tests.cpp:113`, `tests/scene_tests.cpp:134-137`). Cancel does not dirty (`tests/tool_lifecycle_tests.cpp:43-44,92`). |
| D04 | Autosave and recovery | Evidenced | [M4](M4.md) killed-writer recovery with the explicit save byte-identical; `recovery`, `recovery_controller`, `new_file_faults`; [R083.b](R083b-integrated-publication-faults.md). |
| D05 | Model templates | Evidenced | `tests/library_bundle_tests.cpp:69-85` (units, style, materials retained; the new model never mutates the template). Native save/open in `tests/library_input_tests.cpp:106-122,206`. |
| D06 | Limits and diagnostics | **Gap** | Bounded corrupt/oversize errors evidenced (`parser_fault_corpus`, [R082.l](R082l-resource-limits.md), [R083.a](R083a-integrated-parser-corruption.md)). Geometry diagnostics name entities (`diagnostics`, E09). **Gap — memory diagnostics identifying offending entities:** limit rejections are generic strings, e.g. "Component placement exceeds document editing limits" (`src/core/components.cpp:233`) and "Document complexity exceeds editing limits" (`src/core/model.cpp:156`). They name neither the limit nor the entity. **Open (R082):** the memory budget campaign, and the 1M-triangle/10k-instance fixture, which exceeds public document limits ([R082.a](R082a-real-model-baseline.md)). |
| G01 | Editable edges/faces | Evidenced | [M2](M2.md): `topology`, `arrangements`, `planar`; [R016](R016-planar-faces.md). |
| G02 | Face healing and deletion | Evidenced | [M2](M2.md), [R018](R018-erase-heal.md), `cleanup`. |
| G03 | Line and freehand | Evidenced | [R023](R023-drawing-tools.md); `drawing`, `drawing_input_tests` (chained/disconnected, freehand cancel, tilted planes, typed lengths). |
| G04 | Rectangles and polygons | Evidenced | `tests/drawing_input_tests.cpp:101-121` (rotated and tilted rectangles); polygon count/radius in [R023](R023-drawing-tools.md). |
| G05 | Circles and arcs | Evidenced | `tests/curve_tests.cpp:73-115` (two-/three-point arcs), `curve_input_tests`, [R024.b](R024b-native-curves.md) (segmentation, radius/angle entry). |
| G06 | Inference engine | Evidenced | `inference`/`constraints` assert endpoint, midpoint, center, on-edge/face, intersection, axes, parallel, perpendicular and tangent; markers in [R025](R025-inference.md). |
| G07 | Inference locks/guides | Evidenced | `tests/constraint_input_tests.cpp:58-62` (zoom levels), `:222-226` (plane hold); `tests/guide_input_tests.cpp:115-130` (protractor, reference point); [R027.b](R027b-native-guides.md). |
| G08 | Selection | Evidenced | `tests/selection_tests.cpp:39-87` (toggle, connected, hidden, locked contexts); `tests/selection_input_tests.cpp:93-127` (Shift, double/triple click, crossing). |
| G09 | Camera/navigation | Evidenced | [R029](R029-navigation-push-pull.md) (views, FOV), [R072.ab](R072ab-walk-navigation.md) (walk/look-around), [R060.f](R060f-viewport-precision.md) (clipping/picking). |
| G10 | Numeric input | Evidenced | `tests/measurement_tests.cpp:43-48` (absolute/relative), [R022](R022-numeric-amend.md), `numeric_input_tests`. |
| E01 | Face push/pull | Evidenced | [R019](R019-push-pull.md), [R029](R029-navigation-push-pull.md) (repeat distance). Limitation: [M2](M2.md) excludes crossing several opposite faces. This is consistent with the clause ("an opposite face"). |
| E02 | Move/rotate/scale | Evidenced | [R030.b](R030b-native-transforms.md); `transform_selection`, `transform_input_tests`. |
| E03 | Arrays and duplication | Evidenced | [R031](R031-copy-arrays.md); `copy_array`, `hosted_copies`, `array_input_tests`. |
| E04 | Offset | Evidenced | `tests/offset_tests.cpp:57-91` (holes, concave, vanishing, neck split); `OFFSET_COLLAPSED` in `offset_command_tests`. |
| E05 | Follow-me/sweep | Evidenced | `sweep_tests` (open/closed caps, mitred corners, `SWEEP_SELF_INTERSECTION`); `sweep_input_tests`. |
| E06 | Intersect geometry | Evidenced | `tests/intersection_input_tests.cpp:136-183` (selected/context/model modes); `intersection_commands`. |
| E07 | Solid operations | Evidenced | `tests/boolean_input_tests.cpp:147-329` (all six operations); `BOOLEAN_INVALID_SOLID` before mutation in [M6](M6.md). |
| E08 | Surface appearance | Evidenced | `orientation`, `edge_visibility`, `shading_normals`, `edge_input_tests`; front/back sides in `face_textures`. |
| E09 | Geometry inspection/repair | Evidenced | `tests/diagnostics_input_tests.cpp:144-204` (open boundary, inverted shells, explicit repair preview); `diagnostics`. |
| O01 | Groups and nested contexts | Evidenced | [R032.b](R032b-native-groups.md), [R032.c](R032c-context-consolidation.md); `groups`, `consolidation`. |
| O02 | Components and instances | Evidenced | `tests/component_tests.cpp:23-68` (mirror/scale, make-unique, replace); `component_input_tests` (axes, origin, persistence). Native Replace has no native fixture; command coverage only. |
| O03 | Placement behaviors | Evidenced | [M6](M6.md); `hosted_components`, `host_regeneration`, `component_glue`, `hosted_input_tests`. |
| O04 | Outliner and tags | Evidenced | `tests/organization_input_tests.cpp:130-209` (search, rename, tag folders); [R084.k](R084k-keyboard-outliner.md). |
| O05 | Local component library | Evidenced | `library_bundle` (thumbnails, dependency closure, relocation, duplicate reuse/rename, one-step undo); [R079.e](R079e-native-library.md). |
| O06 | Properties and measurements | Evidenced | `entity_measure`, [R035.b](R035b-native-entity-info.md); measured Entity info over AT-SPI in [R084.l](R084l-linux-accessibility-bridge.md). |
| P01 | Materials and textures | Evidenced | `materials`, `face_textures`, `texture_mapping` ([ADR 0061](../decisions/0061-affine-texture-mapping.md) projection); relocation in `tests/asset_io_tests.cpp:55-63`. |
| P02 | Styles and viewport display | Evidenced | `model_style`, `style_input_tests`, `style_viewport_tests` (all modes, profiles, axes, grid, background, ground); shadows in [R067.d](R067d-native-sun-and-shadows.md). |
| P03 | Scenes | Evidenced | [R063.b](R063b-scene-workflows.md), [R064.d](R064d-section-scenes.md), [R072.c](R072c-immutable-animation.md)/[d](R072d-animation-presentation.md). |
| P04 | Sections | Evidenced | [R064.a](R064a-section-geometry.md)–[f](R064f-section-export.md). |
| P05 | Dimensions, labels and text | Evidenced | `tests/annotation_record_tests.cpp:85-110` (broken references marked); `text_source_io`; [R066.d](R066d-editable-text-workflows.md). |
| P06 | Sun and shadows | Evidenced | [R067.a](R067a-offline-solar-position.md)–[d](R067d-native-sun-and-shadows.md). |
| P07 | Images and measured views | Evidenced | [R068.de](R068de-native-images-raster.md), [R078.e](R078e-native-measured-export.md); independent 1:50 measurement in [M8](M8.md). |
| P08 | Blender rendering | Evidenced | `blender_real`, Eevee interop in `scripts/ci-blender-interop.sh:27-35`, CPU fallback (fake launcher) in `tests/render_queue_tests.cpp:161-167`, [R070.c](R070c-native-render-jobs.md). |
| P09 | Blender scene handoff | Evidenced | `blender_handoff_real`, [R071.ab](R071ab-blender-handoff.md). |
| A01 | Shared command API | Evidenced | `commands`, `tool_reference_drift`, [R081.a](R081a-generated-reference.md). |
| A02 | Queries and perception | Evidenced | `inspection`, `inspection_session`. |
| A03 | Transactions and preview | Evidenced | `STALE_PROPOSAL` in `staging`; duplicate requests in `transaction_dispatch`/`transaction_coordinator`; preview and direct modes live in [M5](M5.md). |
| A04 | Embedded assistant | Evidenced | [M5](M5.md), [R086.b](R086b-remote-evaluation.md), [R086 inspection batches](R086as-openai-inspection-batches.md) (remote). R086 release review pending. |
| A05 | Provider independence | Open | Remote passes ([R086.b](R086b-remote-evaluation.md)). **Open (R086), likely implementation:** no local configuration passes. The frozen profile fails 0/3 on every live task ([R086.c](R086c-local-evaluation.md)), and a later local-guidance profile also fails ([review](R086al-local-guidance-failure-review.json)). |
| A06 | External automation | Evidenced | `mcp`, `native_mcp_tests`, `automation_session`. |
| A07 | Agent instructions/examples | Evidenced | `shipped_recipes`, `tool_reference_drift`; room, hosted-room, roof, stair, table and cabinet recipes in `examples/`. |
| A08 | AI verification | Partial | Postconditions, preservation and injected-metadata tasks scored ([R086.a](R086a-frozen-provider-corpus.md)). **Partial — undo and recovery scoring:** the frozen corpus and `scripts/score-provider-release.py` score neither. Coverage comes from deterministic tests (`transaction_sequence`, `outcome_store`) and single M5 live Undo/Redo trials. **Open (R086):** the local profile never reached hostile metadata. |
| X01 | Formline migration | Evidenced | `formline`, `formline_input_tests`. |
| X02 | GLB/glTF | Evidenced | Cameras and hierarchy in `tests/gltf_import_tests.cpp:119-120`; loss manifest in `tests/glb_export_tests.cpp:191-195`; [R074.c](R074c-native-gltf-workflow.md). |
| X03 | OBJ and STL | Evidenced | [R075.d](R075d-native-obj-workflow.md), [R076.d](R076d-native-stl-workflow.md); independent measurement in [M8](M8.md). |
| X04 | DXF exchange | Evidenced | [R077.d](R077d-native-dxf-workflow.md); `dxf_export_real` (ezdxf). |
| X05 | Public extension interface | Evidenced | [R080.a](R080a-extension-contract.md)–[d](R080d-native-extensions.md). |
| X06 | Native-format migration | Evidenced | [R073](R073-public-native-format.md); 25 installed migrations in [R087.g](R087g-clean-current-package.md); [ADR 0108](../decisions/0108-public-native-format.md) documents the stored data. |

Totals: **50 Evidenced, 5 Open (N01, N02, N04, N05, A05), 1 Partial (A08),
3 Gap (N03, D01, D06).** The Evidenced count means every clause has a cited
assertion on `main`. It does not mean the row is accepted.

## Implementation gaps

Behaviour that is probably absent or incomplete in `src/`:

- **D01 — display precision.** No precision setting exists (ADR 0018). A configurable,
  persisted precision is needed, or a scope-change PR that narrows the clause.
- **D06 — limit diagnostics.** Limit rejections need to name the limit and the
  offending entity or definition.
- **N05 — visible focus (R084).** Several widget classes have no focus style. This
  needs confirmation by an audit before any styling change.
- **A05 — local provider (R086).** No local configuration meets the frozen thresholds.
- **N01/D06 — large-model capacity (R082).** The 1M-triangle/10k-instance build-plan scenario
  exceeds public document limits ([R082.a](R082a-real-model-baseline.md)), so meeting
  it needs document/instancing work.

## Evidence-only gaps

Behaviour that probably works but has no qualifying evidence:

- **N03 — keyboard layouts.** Run shortcut, numeric and lock fixtures under at least
  one non-US layout.
- **N03 — pointer capture.** Physical cross-window drag and release on the primary
  compositor; current tests inject synthetic events only.
- **D01 — decimal imperial entry.** Parser and native assertions for decimal inch and
  foot input.
- **A08 — undo and recovery scoring.** Undo and recovery postconditions in the
  provider corpus (a new corpus version) or an explicit release scoring step.
- **N05 — user-guide limits (R088).** Viewport accessibility limits in the installed
  user guide.
- **N04 — physical 150% (R085).** A physical 150% output plus the open placement and
  hotplug cases.

Extended F01–F07 and Investigate C01–C07 rows are outside this analysis and remain
in [scope](../SCOPE.md). No SketchUp parity claim follows from these results.
