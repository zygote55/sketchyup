# R088.f — Core parity report at the 1.0.0 release candidate

2026-10-13. Release candidate (RC): `main` at `7b01bb7a1d3fa19b6b5cc7f1a4c27a2ad5cd03a2`
(merge of PR #307). This is the R088 Core parity report that
[M9_ACCEPTANCE.md](../M9_ACCEPTANCE.md) asks for. It links each of the **59 Core rows** of
[SCOPE.md](../SCOPE.md) to passing evidence taken at the RC, and separately to earlier
supporting evidence. It builds on the clause-level gap analysis [R088.d](R088d-core-parity-gaps.md),
which mapped rows to evidence on `main` before the RC. The clause text of every row is unchanged.

## RC evidence

Every item below ran against the RC commit or its artifacts.

- **CI**: Actions run [38435734443](https://github.com/zygote55/sketchyup/actions/runs/38435734443)
  on `7b01bb7a`. All four jobs pass: development build and native tests, Blender
  interoperability, sanitized builds and Arch package acceptance. It also runs the opt-in
  consumers: Blender 5.2.2, ezdxf 1.4.4 and Poppler 26.08.0. Every row cites it.
- **R089.d walkthrough**: [R089d-release-candidate.md](R089d-release-candidate.md) and its
  [JSON](R089d-release-candidate.json). Areas: development CTest (190/190), sanitized CTest
  (62/62, 97/97, 7/7, 1/1), the development native matrix (84 suites on X11 and Wayland at
  1× and 2×, 336 cases), the sanitized native matrix (46 suites, 184 cases), AT-SPI 4/4,
  the all-dialog keyboard audit 64/64, keyboard layouts 6/6, fractional scale 20/20,
  package acceptance, three clean machines and the M5–M8 studies.
- **R086.ca**: the [remote provider corpus at the RC](R086ca-rc-remote-evaluation.md),
  24/24, `corpusPassed: true`, no blockers. Cited for A03, A04, A05, A08 and D04.
- **R083.h**: [the sanitized 13-suite failure matrix and the coverage-guided parser fuzzing
  campaign at the RC](R083h-rc-fuzzing-and-sanitized-matrix.md). The matrix passes 13/13
  under ASan/UBSan. All ten libFuzzer targets ran for 300 s with no new finding; six stop on
  their first seed with F1 (D-022) and run to time with only that check disabled. Cited for
  D02, D04, D06 and X06.

Each named suite is tagged with where it ran at the RC. The tags come from the R089.d case
lists and CTest logs, and from the CI workflow and logs:

| Tag | Where it ran at the RC |
| --- | --- |
| C | `ctest --preset dev`, 190/190 (R089.d and CI) |
| S | Sanitized CTest: the `sanitize` preset and the CI sanitize job's lists (R089.d and CI) |
| N | Development native matrix, X11 and Wayland at 1× and 2× (R089.d; CI runs its `native.yml` lines) |
| NS | Sanitized native matrix, X11 and Wayland at 1× and 2× (R089.d; CI runs its lines) |
| H | The sanitized 13-suite failure matrix in [R083.h](R083h-rc-fuzzing-and-sanitized-matrix.md#sanitized-13-suite-matrix), `sanitize` preset build of the RC |
| F | The coverage-guided libFuzzer campaign in [R083.h](R083h-rc-fuzzing-and-sanitized-matrix.md#coverage-guided-parser-fuzzing), all ten parser targets |
| L | `scripts/test-keyboard-layout.sh` with `us`, `de` and `fr` on X11 and Wayland (R089.d and CI) |
| CI | CI run 38435734443 only. These opt-in `*_real` consumer tests skip without their tool, and the local images have none |

The **Supporting** column holds records from earlier revisions. They are supporting only and
do not show that the RC passes ([M9_ACCEPTANCE.md](../M9_ACCEPTANCE.md#release-candidate-freeze)).
A suite name shows that the suite ran and passed at the RC. It does not by itself prove every
clause; R088.d maps clauses to assertions.

Statuses:

- **Evidenced**: every clause has passing RC evidence.
- **Evidenced (owner waiver)**: every clause has passing RC evidence or is waived by
  [ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md), and at least one is waived.
  A waived clause is not claimed to pass.
- **Open**: a clause lacks passing RC evidence and is not waived.

## Row status

| Scope | Capability | Status | RC evidence at `7b01bb7a` | Supporting (earlier revisions) | Notes |
| --- | --- | --- | --- | --- | --- |
| N01 | Native desktop and viewport | Evidenced (owner waiver) | CI; `sketchyup-smoke` N; `interaction_tests` N; `viewport_tests` N; `document_limits` C·S; `document_limits_io` C; [package acceptance](R089d-release-candidate.md#re-run-at-the-rc); [clean machines](R089d-release-candidate.md#re-run-at-the-rc) | [M4](M4.md), [R085.b](R085b-native-surface-color.md), [R085.i](R085i-assisted-output-transitions.md), [R082.gg](R082gg-placement-limits.md), [R082.ii](R082ii-amd-hardware-session.md), [R082.jj](R082jj-edit-feedback.md) | Launch, resize and rendering at the RC run in isolated Weston and Xvfb. The package launches with no network on three clean machines; `packaging/arch/PKGBUILD:10` depends on no Electron or browser runtime, and the installed closure has no Blender, Python, Node.js, libsecret or provider SDK. **Thinner than R088.d:** Hyprland launch and the AMD performance campaigns are earlier-revision evidence only; no GPU, compositor or timing check ran at the RC. **Waived (ADR 0161):** Intel campaigns, sharing-off control, the manual pass on Hyprland. `interaction_tests` Wayland 2× passed on re-run (D-021). |
| N02 | Build and package | Evidenced | CI; [package acceptance](R089d-release-candidate.md#re-run-at-the-rc); [clean machines](R089d-release-candidate.md#re-run-at-the-rc) | [R087.g](R087g-clean-current-package.md), [R087.u](R087u-clean-machine-packages.md) | The RC source passes 190/190 `check()` in the package build. The `1.0.0-1` package upgrades `0.1.0-3` with preferences, launches, migrates 28 fixtures and removes cleanly; `verify-package.sh` checks the desktop entry, MIME and icon. Clean machines install it with no network on the pinned, fresh and Qt 6.8.0 reference images. Qualification: unversioned PKGBUILD dependencies (D-029). |
| N03 | Native interaction | Evidenced (owner waiver) | CI; `shortcut_input_tests` N·NS; `keyboard_modeling_input_tests` N·NS; `keyboard_outliner_input_tests` N·NS; `tool_lifecycle_tests` N; `entity_info_input_tests` N; `numeric_input_tests` N; `keyboard_layout_input_tests` L; `shortcut_bindings` C·S; `dialog_tests` N; [dialog audit 64/64](R089d-release-candidate.md#re-run-at-the-rc); [fractional 1.5/1.75 20/20](R089d-release-candidate.md#re-run-at-the-rc) | [R084.d](R084d-keyboard-modeling.md), [R084.e](R084e-shortcut-bindings.md), [R084.f](R084f-native-shortcut-editor.md), [R084.k](R084k-keyboard-outliner.md), [R084.u](R084u-all-dialog-keyboard.md), [R085.p](R085p-keyboard-layouts.md), [R085.q](R085q-keypad-decimal.md) | Layouts us/de/fr through physical keycodes on X11 and Weston, 6/6. Qualification: only three layouts; synthetic keys. **Waived (ADR 0161):** physical pointer capture, a real keyboard under Hyprland, input methods. |
| N04 | Scaling and displays | Evidenced (owner waiver) | CI; `viewport_tests` N; `inference_input_tests` N; `text_size_input_tests` N·NS; [fractional 1.5/1.75 20/20](R089d-release-candidate.md#re-run-at-the-rc) | [R085.b](R085b-native-surface-color.md), [R085.h](R085h-physical-outputs.md), [R085.i](R085i-assisted-output-transitions.md), [R085.r](R085r-hotplug-owner-attested.md) | 1× and 2× on both backends for every native suite, and emulated 1.5/1.75 on five suites. **Thinner than R088.d:** the physical mixed-DPI Wayland transitions (scales 2 and 1.6) are earlier-revision evidence only; they were not re-run at the RC. **Waived (ADR 0161):** physical 150%, Xcb hotplug, suspend/resume, automatic X11 placement. Wayland hotplug is owner-attested. |
| N05 | Accessibility/preferences | Evidenced (owner waiver) | CI; `visible_focus_input_tests` N; `keyboard_panel_tabs_tests` N; `keyboard_reachability_tests` N; `text_size_input_tests` N·NS; `theme_input_tests` N·NS; `preference_input_tests` N·NS; `shortcut_input_tests` N·NS; `accessibility_bridge_test` N; [AT-SPI 4/4](R089d-release-candidate.md#re-run-at-the-rc); [dialog audit 64/64](R089d-release-candidate.md#re-run-at-the-rc); [fractional 1.5/1.75 20/20](R089d-release-candidate.md#re-run-at-the-rc) | [R084.c](R084c-preference-compatibility.md), [R084.h](R084h-interface-text-size.md), [R084.l](R084l-linux-accessibility-bridge.md), [R084.q](R084q-visible-focus.md), [R084.s](R084s-keyboard-panel-tabs.md), [R084.u](R084u-all-dialog-keyboard.md), [R084.v](R084v-keyboard-reachability.md), [R088.e](R088e-viewport-accessibility-limits.md) | `visible_focus_input_tests` Wayland 2× timed out on the one-CPU runner and passed in CI and on six CPUs at the RC (D-028). The viewport limits are documented in the installed user guide. Qualification: focus-ring residuals (D-023). **Waived (ADR 0161):** the Orca session. |
| D01 | Units and axes | Evidenced | CI; `measurements` C; `display_precision` C; `display_precision_io` C; `document_units_io` C; `units_input_tests` N; `component_input_tests` N | [R084.r](R084r-display-precision.md) | Custom drawing planes are session state (M3 record). |
| D02 | Native document format | Evidenced | CI; `native_format` C·S; `persistence` C·H; `parser_fault_corpus` C·S; parser fuzz targets F; `saved_scenes_io` C·S; `asset_io` C; [package acceptance](R089d-release-candidate.md#re-run-at-the-rc); [clean machines](R089d-release-candidate.md#re-run-at-the-rc) | [R083.a](R083a-integrated-parser-corruption.md), [R083.e](R083e-parser-fuzzing.md), [ADR 0108](../decisions/0108-public-native-format.md) | The coverage-guided libFuzzer campaign ran at the RC with schema 26 seeds (R083.h) and found nothing new; the deterministic 5,120-input parser corpus ran normally and sanitized. F1 stays open (D-022): six targets stop on their first seed under the documented options and run to time with only that check disabled. |
| D03 | Undo and transactions | Evidenced | CI; `history` C·S; `transaction_sequence` C·S; `core` C·S; `scene` C·S; `tool_lifecycle_tests` N | [M1](M1.md), [M4](M4.md) |  |
| D04 | Autosave and recovery | Evidenced | CI; `recovery` C·H; `recovery_controller` C; `recovery_cli` C·H; `new_file_faults` C·S; `recovery_input_tests` N; `file_operation_input_tests` N·NS; [R086.ca](R086ca-rc-remote-evaluation.md) 24/24 | [M4](M4.md), [R083.b](R083b-integrated-publication-faults.md), [R083.d](R083d-local-failure-matrix.md) | R086.ca restores the committed container with `--recover-latest` on all 12 edit runs. `recovery`, `recovery_cli` and `persistence` also pass under ASan/UBSan at the RC (R083.h). Faults are injected, not physical power loss (D-014). |
| D05 | Model templates | Evidenced | CI; `library_bundle` C·S; `library_input_tests` N·NS | [R079.e](R079e-native-library.md) |  |
| D06 | Limits and diagnostics | Evidenced (owner waiver) | CI; `parser_fault_corpus` C·S; parser fuzz targets F; `document_limits` C·S; `document_limits_io` C; `incremental_validation` C·S; `diagnostics` C·S | [R082.l](R082l-resource-limits.md), [R082.gg](R082gg-placement-limits.md), [R082.w v2](R082w-memory-budget-v2.md), [R082.ii](R082ii-amd-hardware-session.md), [R083.e](R083e-parser-fuzzing.md) | **Thinner than R088.d:** the memory budget on hardware is AMD evidence at `db68cd14`, not re-run at the RC; no core, geometry or viewport source changed after R082.jj. **Waived (ADR 0161):** Intel reruns and the sharing-off control. |
| G01 | Editable edges/faces | Evidenced | CI; `topology` C·S; `arrangements` C·S; `planar` C·S | [M2](M2.md), [R016](R016-planar-faces.md) |  |
| G02 | Face healing and deletion | Evidenced | CI; `cleanup` C·S; `topology` C·S | [M2](M2.md), [R018](R018-erase-heal.md) |  |
| G03 | Line and freehand | Evidenced | CI; `drawing` C·S; `drawing_input_tests` N | [R023](R023-drawing-tools.md) |  |
| G04 | Rectangles and polygons | Evidenced | CI; `drawing` C·S; `drawing_input_tests` N | [R023](R023-drawing-tools.md) |  |
| G05 | Circles and arcs | Evidenced | CI; `curves` C·S; `curve_input_tests` N | [R024.b](R024b-native-curves.md) |  |
| G06 | Inference engine | Evidenced | CI; `inference` C·S; `constraints` C·S; `inference_equivalence` C·S; `inference_input_tests` N | [R025](R025-inference.md) |  |
| G07 | Inference locks/guides | Evidenced | CI; `constraints` C·S; `guides` C·S; `constraint_input_tests` N; `guide_input_tests` N | [R027.b](R027b-native-guides.md) |  |
| G08 | Selection | Evidenced | CI; `selection` C·S; `selection_input_tests` N | [M3](M3.md) |  |
| G09 | Camera/navigation | Evidenced | CI; `camera_motion` C·S; `navigation_input_tests` N·NS; `viewport_precision_tests` N·NS; `viewport_tests` N | [R029](R029-navigation-push-pull.md), [R072.ab](R072ab-walk-navigation.md), [R060.f](R060f-viewport-precision.md) |  |
| G10 | Numeric input | Evidenced | CI; `measurements` C; `amend` C·S; `numeric_input_tests` N | [R022](R022-numeric-amend.md) |  |
| E01 | Face push/pull | Evidenced | CI; `push_pull` C·S; `keyboard_modeling_input_tests` N·NS; `m4_workflow_tests` N | [R019](R019-push-pull.md), [R029](R029-navigation-push-pull.md), [M2](M2.md) | Limitation kept: crossing several opposite faces is excluded (M2). |
| E02 | Move/rotate/scale | Evidenced | CI; `transform_selection` C·S; `transform_input_tests` N | [R030.b](R030b-native-transforms.md) |  |
| E03 | Arrays and duplication | Evidenced | CI; `copy_array` C·S; `hosted_copies` C·S; `array_input_tests` N; `hosted_array_input_tests` N·NS | [R031](R031-copy-arrays.md) |  |
| E04 | Offset | Evidenced | CI; `offset` C·S; `offset_commands` C·S; `offset_input_tests` N; [M6 study](R089d-release-candidate.md#failures) (attempt 2) | [M6](M6.md) |  |
| E05 | Follow-me/sweep | Evidenced | CI; `sweep` C·S; `sweep_commands` C·S; `sweep_input_tests` N; [M6 study](R089d-release-candidate.md#failures) (attempt 2) | [M6](M6.md) |  |
| E06 | Intersect geometry | Evidenced | CI; `intersections` C·S; `intersection_commands` C·S; `intersection_input_tests` N; [M6 study](R089d-release-candidate.md#failures) (attempt 2) | [M6](M6.md) |  |
| E07 | Solid operations | Evidenced | CI; `booleans` C·S; `solid_operations` C·S; `solid_shells` C·S; `boolean_commands` C·S; `solid_commands` C·S; `boolean_input_tests` N; [M6 study](R089d-release-candidate.md#failures) (attempt 2) | [M6](M6.md) |  |
| E08 | Surface appearance | Evidenced | CI; `orientation` C·S; `edge_visibility` C·S; `shading_normals` C·S; `face_textures` C·S; `edge_input_tests` N·NS; `orientation_input_tests` N | [M6](M6.md) |  |
| E09 | Geometry inspection/repair | Evidenced | CI; `diagnostics` C·S; `diagnostic_inspection` C·S; `diagnostics_input_tests` N·NS | [M6](M6.md) |  |
| O01 | Groups and nested contexts | Evidenced | CI; `groups` C·S; `consolidation` C·S; `group_input_tests` N | [R032.b](R032b-native-groups.md), [R032.c](R032c-context-consolidation.md) |  |
| O02 | Components and instances | Evidenced | CI; `components` C·S; `component_records` C·S; `component_input_tests` N | [M4](M4.md) | Native Replace has command coverage only, as R088.d records. |
| O03 | Placement behaviors | Evidenced | CI; `hosted_components` C·S; `host_regeneration` C·S; `component_glue` C·S; `hosted_openings` C·S; `hosted_input_tests` N·NS; [M6 study](R089d-release-candidate.md#failures) (attempt 2) | [M6](M6.md) |  |
| O04 | Outliner and tags | Evidenced | CI; `tags` C·S; `organization_input_tests` N; `keyboard_outliner_input_tests` N·NS | [R084.k](R084k-keyboard-outliner.md) |  |
| O05 | Local component library | Evidenced | CI; `library_bundle` C·S; `library_input_tests` N·NS; [M8 study](R089d-release-candidate.md#re-run-at-the-rc) | [R079.e](R079e-native-library.md) |  |
| O06 | Properties and measurements | Evidenced | CI; `entity_measure` C·S; `entity_info_input_tests` N; `accessibility_bridge_test` N; [AT-SPI 4/4](R089d-release-candidate.md#re-run-at-the-rc) | [R035.b](R035b-native-entity-info.md), [R084.l](R084l-linux-accessibility-bridge.md) |  |
| P01 | Materials and textures | Evidenced | CI; `materials` C·S; `face_textures` C·S; `texture_mapping` C·S; `asset_io` C; `material_input_tests` N; `texture_input_tests` N·NS; `texture_viewport_tests` N·NS; CI Blender interoperability job (Cycles and Eevee) | [ADR 0061](../decisions/0061-affine-texture-mapping.md), [M7](M7-accepted.md) |  |
| P02 | Styles and viewport display | Evidenced | CI; `model_style` C·S; `style_input_tests` N·NS; `style_viewport_tests` N·NS; `solar_input_tests` N·NS | [R067.d](R067d-native-sun-and-shadows.md), [M7](M7-accepted.md) |  |
| P03 | Scenes | Evidenced | CI; `saved_scenes` C·S; `saved_scene_commands` C·S; `saved_scenes_io` C·S; `animation_capture` C·S; `animation_export` C·S; `animation_export_real` CI; `scene_input_tests` N·NS; `m7_presentation` C·S; `m7_presentation_input_tests` N·NS; [M7 study](R089d-release-candidate.md#re-run-at-the-rc) | [R063.b](R063b-scene-workflows.md), [R064.d](R064d-section-scenes.md), [R072.c](R072c-immutable-animation.md), [R072.d](R072d-animation-presentation.md) |  |
| P04 | Sections | Evidenced | CI; `section_geometry` C·S; `section_records` C·S; `section_commands` C·S; `sections_io` C·S; `section_export` C·S; `section_input_tests` N·NS; `section_viewport_tests` N·NS; CI Blender interoperability job (Cycles and Eevee) | [R064.a](R064a-section-geometry.md), [R064.f](R064f-section-export.md) |  |
| P05 | Dimensions, labels and text | Evidenced | CI; `annotation_records` C·S; `annotation_commands` C·S; `annotation_anchors` C·S; `text_source_io` C·S; `text_commands` C·S; `annotation_input_tests` N·NS; `text_input_tests` N·NS; [M7 study](R089d-release-candidate.md#re-run-at-the-rc) | [R066.d](R066d-editable-text-workflows.md) | `annotation_input_tests` Wayland 2× passed on re-run (D-021). |
| P06 | Sun and shadows | Evidenced | CI; `solar` C·S; `solar_commands` C·S; `solar_io` C·S; `solar_input_tests` N·NS; CI Blender interoperability job (Cycles and Eevee) | [R067.a](R067a-offline-solar-position.md), [R067.d](R067d-native-sun-and-shadows.md) |  |
| P07 | Images and measured views | Evidenced | CI; `reference_image` C·S; `reference_image_io` C·S; `measured_view` C·S; `measured_drawing` C·S; `measured_export` C·S; `measured_export_real` CI; `reference_image_input_tests` N·NS; `raster_export_input_tests` N·NS; `measured_input_tests` N·NS; [M8 study](R089d-release-candidate.md#re-run-at-the-rc) | [R068.de](R068de-native-images-raster.md), [R078.e](R078e-native-measured-export.md), [M8](M8.md) |  |
| P08 | Blender rendering | Evidenced | CI; `blender_real` CI; `render_queue` C·S; `render_store` C·S; `blender_job` C·S; `render_input_tests` N·NS; CI Blender interoperability job (Cycles and Eevee) | [R070.c](R070c-native-render-jobs.md) | Blender 5.2.2 in CI only; the local RC runs had no Blender. |
| P09 | Blender scene handoff | Evidenced | CI; `blender_handoff` C·S; `blender_handoff_real` CI | [R071.ab](R071ab-blender-handoff.md) | One-way handoff is documented (D-016). |
| A01 | Shared command API | Evidenced | CI; `commands` C; `tool_reference_drift` C·S; CI MCP schema validation | [R081.a](R081a-generated-reference.md) |  |
| A02 | Queries and perception | Evidenced | CI; `inspection` C·S; `inspection_session` C·S; `desktop_inspection_tests` N | [M5](M5.md) |  |
| A03 | Transactions and preview | Evidenced | CI; `staging` C·S; `transaction_dispatch` C·S; `transaction_coordinator` C·S; `assistant_preview_tests` N·NS; [M5 study](R089d-release-candidate.md#re-run-at-the-rc); [R086.ca](R086ca-rc-remote-evaluation.md) 24/24 | [M5](M5.md) | R086.ca: each AI task is one labeled undo step on all 12 edit runs. |
| A04 | Embedded assistant | Evidenced | CI; `assistant` C·S; `native_assistant_session` C·S; `openai_provider` C·S; `chatgpt_auth` C·S; `assistant_panel_tests` N·NS; `assistant_preview_tests` N·NS; [M5 study](R089d-release-candidate.md#re-run-at-the-rc); [R086.ca](R086ca-rc-remote-evaluation.md) 24/24 | [M5](M5.md), [R086.b](R086b-remote-evaluation.md), [R086.bz](R086bz-remote-evaluation-v2.md) | Live quality is probabilistic and covers one configuration (D-018). |
| A05 | Provider independence | Evidenced | CI; `openai_provider` C·S; `chatgpt_auth` C·S; `provider_release_corpus` C; `provider_release_score` C; [R086.ca](R086ca-rc-remote-evaluation.md) 24/24 | [R086.b](R086b-remote-evaluation.md), [R086.bz](R086bz-remote-evaluation-v2.md), [R086.at](R086at-provider-scope-decision.md), [R086.c](R086c-local-evaluation.md), [ADR 0160](../decisions/0160-provider-scope-for-1-0.md) | **Changed from Open.** The remote ChatGPT-plan profile (`gpt-6-astra`) passes the frozen corpus v2 at the RC, `corpusPassed: true`, no blockers. `chatgpt_auth` and `openai_provider` keep credentials in a separate credential record, not in the document, and no credential appears in R086.ca's published files. Local profiles are experimental and not gated (ADR 0160; D-001). |
| A06 | External automation | Evidenced | CI; `mcp` C·S; `automation_session` C·S; `native_mcp_tests` N·NS; CI MCP schema validation | [M5](M5.md), [M8](M8.md) |  |
| A07 | Agent instructions/examples | Evidenced | CI; `shipped_recipes` C·S; `tool_reference_drift` C·S; `recipes` C·S; `roof_recipe` C·S; `stair_recipe` C·S; `furniture_recipe` C·S; `roof_recipe_input_tests` N·NS; `stair_recipe_input_tests` N·NS; `furniture_recipe_input_tests` N·NS; [package acceptance](R089d-release-candidate.md#re-run-at-the-rc) | [R081.b](R081b-executable-recipes.md) | The package acceptance runs the installed recipes. |
| A08 | AI verification | Evidenced | CI; `provider_release_corpus` C; `provider_release_score` C; `measurement_assertions` C·S; [R086.ca](R086ca-rc-remote-evaluation.md) 24/24 | [R086.a](R086a-frozen-provider-corpus.md), [R086.by](R086by-corpus-v2.md), [R086.bz](R086bz-remote-evaluation-v2.md) | R086.ca scores postconditions, preservation, undo and recovery (24/24) and the injected-metadata task (3/3) at the RC. |
| X01 | Formline migration | Evidenced | CI; `formline` C; `formline_input_tests` N | [M4](M4.md) |  |
| X02 | GLB/glTF | Evidenced | CI; `gltf_import` C·S; `gltf_package` C·S; `glb_export` C·S; `gltf_import_real` CI; `gltf_input_tests` N·NS; CI Blender interoperability job (Cycles and Eevee); [M8 study](R089d-release-candidate.md#re-run-at-the-rc) | [R074.c](R074c-native-gltf-workflow.md) |  |
| X03 | OBJ and STL | Evidenced | CI; `obj_import` C·S; `obj_export` C·S; `obj_material` C·S; `stl_import` C·S; `stl_export` C·S; `obj_import_real` CI; `obj_export_real` CI; `stl_import_real` CI; `stl_export_real` CI; `obj_input_tests` N·NS; `stl_input_tests` N·NS; [M8 study](R089d-release-candidate.md#re-run-at-the-rc) | [R075.d](R075d-native-obj-workflow.md), [R076.d](R076d-native-stl-workflow.md), [M8](M8.md) |  |
| X04 | DXF exchange | Evidenced | CI; `dxf_import` C·S; `dxf_export` C·S; `dxf_source` C·S; `dxf_export_real` CI; `dxf_input_tests` N·NS; [M8 study](R089d-release-candidate.md#re-run-at-the-rc) | [R077.d](R077d-native-dxf-workflow.md) | `dxf_export_real` uses ezdxf 1.4.4 in CI. |
| X05 | Public extension interface | Evidenced | CI; `extension_manifest` C·S; `extension_store` C·S; `extension_worker` C·S; `extension_input_tests` N·NS; [M8 study](R089d-release-candidate.md#re-run-at-the-rc) | [R080.a](R080a-extension-contract.md), [R080.d](R080d-native-extensions.md) |  |
| X06 | Native-format migration | Evidenced | CI; `native_format` C·S; `persistence` C·H; `m4_persistence` C·H; [package acceptance](R089d-release-candidate.md#re-run-at-the-rc); [clean machines](R089d-release-candidate.md#re-run-at-the-rc) | [R073](R073-public-native-format.md), [R087.u](R087u-clean-machine-packages.md), [ADR 0108](../decisions/0108-public-native-format.md) | 28 installed migrations pass at the RC (R088.d cited 25 in R087.g). |

## RC evidence thinner than R088.d claimed

These rows keep their status, but some clauses rest on evidence from before the RC.
M9_ACCEPTANCE treats those records as supporting, not as RC evidence. The RC record
[R089.d](R089d-release-candidate.md#earlier-evidence-relied-on-unchanged) names them as
relied on unchanged.

| Row | Clause | What ran at the RC | Earlier evidence relied on |
| --- | --- | --- | --- |
| N01 | Launch on Omarchy/Hyprland; frame and edit budgets | Isolated Weston and Xvfb launches, `sketchyup-smoke`, clean-machine launches. No compositor, GPU or timing check | Hyprland launch in [M4](M4.md) and [R085.i](R085i-assisted-output-transitions.md); AMD campaigns at `db68cd14` ([R082.ii](R082ii-amd-hardware-session.md)) and [R082.jj](R082jj-edit-feedback.md). No core, geometry or viewport source changed between R082.jj (#302) and the RC |
| N04 | Mixed-DPI multi-display moves | 1×/2× on every native suite and emulated 1.5/1.75 on five suites, all in isolated sessions | Physical Wayland transitions at scales 2 and 1.6 in [R085.h](R085h-physical-outputs.md) and [R085.i](R085i-assisted-output-transitions.md) (2026-10-07) |
| D06 | Memory budget on hardware | `document_limits` (normal and sanitized) and `document_limits_io` (normal), in software | AMD acceptance of the [R082.w v2](R082w-memory-budget-v2.md) ceilings in [R082.ii](R082ii-amd-hardware-session.md) |

Two clauses listed here earlier now have RC evidence. The coverage-guided parser fuzzing
campaign (D02, D06) and the sanitized run of `geometry_sequence`, `persistence`,
`m4_persistence`, `recovery` and `recovery_cli` (D04, D02) were re-run at the RC in
[R083.h](R083h-rc-fuzzing-and-sanitized-matrix.md): 13/13 sanitized suites pass, and the
campaign has no new finding. F1 stays open ([D-022](R089a-defect-triage.md)).

The RC rows also carry three run-level findings from R089.d. Each passed at the RC on
re-run, in CI or on more CPUs: `interaction_tests` and `annotation_input_tests` on Wayland 2×
([D-021](R089a-defect-triage.md)), and `visible_focus_input_tests` on Wayland 2× under one CPU
([D-028](R089a-defect-triage.md)).

## Totals

**54 Evidenced, 5 Evidenced (owner waiver) (N01, N03, N04, N05, D06), 0 Open.**

R088.d at its last update (2026-10-13, against `main` before the RC) had 53 Evidenced, 5
Evidenced (owner waiver) and 1 Open (A05). A05 moves to Evidenced with
[R086.ca](R086ca-rc-remote-evaluation.md) under [ADR 0160](../decisions/0160-provider-scope-for-1-0.md).
No row moves down.

Extended rows F01–F07 and Investigate rows C01–C07 are outside this report. They stay
listed as gaps in [SCOPE.md](../SCOPE.md) and the [support matrix](../SUPPORT_MATRIX.md).
1.0 does not claim full SketchUp parity.
