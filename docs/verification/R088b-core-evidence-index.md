# R088.b — Core evidence index and remaining acceptance gaps

2026-10-07. This indexes **all 59 Core scope rows** against retained passing
regression fixtures. It is a coverage aid, **not a completed parity report or
release sign-off**. A passing named test proves its fixture assertions, not every
acceptance clause in the corresponding scope row.

The common baseline is [R085.b](R085b-native-surface-color.md), source
`c09035b81f1c9e484a183214960b7063613e44e2`: 181/181 tests, no skips, including
eleven real-consumer checks. Each test name below links to that retained result.
Native interactions have separate evidence; headless core tests do not establish
physical display, keyboard-layout, pointer-capture or accessibility acceptance.

M0–M6 have [accepted gate records](../PR_ROADMAP.md). M7/M8 implementations and
local evidence remain in review pending CI and ordered merges. Every row still
requires final release-candidate integration under R089. Additional open gates
include R082 performance/limits, R083 data-loss validation, R084 full UI audit,
R085 physical platforms, R086 live providers and R087 final packages/upgrades.

| Scope | Capability | Regression evidence | Additional evidence / explicit limitation |
| --- | --- | --- | --- |
| [N01](../SCOPE.md) | Native desktop and viewport | [desktop](R085b-full-ctest.txt) | [Native backend and corrected fractional cases; physical transitions open](R085b-native-surface-color.md) |
| [N02](../SCOPE.md) | Build and package | [desktop](R085b-full-ctest.txt) | [Development lifecycle passed; clean-machine, historical migration and final gate open](R087a-package-lifecycle.md) |
| [N03](../SCOPE.md) | Native interaction | [shortcut_bindings](R085b-full-ctest.txt) | [Native keyboard workflow; full platform/input coverage open](R084m-integrated-keyboard-shortcuts.md) |
| [N04](../SCOPE.md) | Scaling and displays | [desktop](R085b-full-ctest.txt) | [Partial scale matrix; physical mixed-DPI transitions open](R085b-native-surface-color.md) |
| [N05](../SCOPE.md) | Accessibility/preferences | [shortcut_bindings](R085b-full-ctest.txt) | [Linux accessibility bridge; complete control/dialog audit open](R084l-integrated-accessibility-bridge.md) |
| [D01](../SCOPE.md) | Units and axes | [document_units](R085b-full-ctest.txt), [document_units_io](R085b-full-ctest.txt) | Scope gate M1/M3; final integration remains open |
| [D02](../SCOPE.md) | Native document format | [native_format](R085b-full-ctest.txt), [persistence](R085b-full-ctest.txt), [parser_fault_corpus](R085b-full-ctest.txt) | Scope gate M1/M4; final integration remains open |
| [D03](../SCOPE.md) | Undo and transactions | [history](R085b-full-ctest.txt), [transaction_sequence](R085b-full-ctest.txt), [prepared_edit](R085b-full-ctest.txt) | Scope gate M1/M4; final integration remains open |
| [D04](../SCOPE.md) | Autosave and recovery | [recovery](R085b-full-ctest.txt), [recovery_controller](R085b-full-ctest.txt), [new_file_faults](R085b-full-ctest.txt) | Scope gate M4; final integration remains open |
| [D05](../SCOPE.md) | Model templates | [library_bundle](R085b-full-ctest.txt) | Scope gate M8; final integration remains open |
| [D06](../SCOPE.md) | Limits and diagnostics | [parser_fault_corpus](R085b-full-ctest.txt), [diagnostics](R085b-full-ctest.txt) | [Million-triangle fixture exceeds present limits; performance gate open](R082a-real-model-baseline.md) |
| [G01](../SCOPE.md) | Editable edges/faces | [topology](R085b-full-ctest.txt), [arrangements](R085b-full-ctest.txt), [planar](R085b-full-ctest.txt) | Scope gate M2; final integration remains open |
| [G02](../SCOPE.md) | Face healing and deletion | [cleanup](R085b-full-ctest.txt) | Scope gate M2; final integration remains open |
| [G03](../SCOPE.md) | Line and freehand | [drawing](R085b-full-ctest.txt) | [Native drawing fixtures](R023-drawing-tools.md) |
| [G04](../SCOPE.md) | Rectangles and polygons | [drawing](R085b-full-ctest.txt) | [Native shape fixtures](R023-drawing-tools.md) |
| [G05](../SCOPE.md) | Circles and arcs | [curves](R085b-full-ctest.txt) | [Native curve fixtures](R024b-native-curves.md) |
| [G06](../SCOPE.md) | Inference engine | [inference](R085b-full-ctest.txt), [constraints](R085b-full-ctest.txt) | [Native inference fixtures](R025-inference.md) |
| [G07](../SCOPE.md) | Inference locks/guides | [guides](R085b-full-ctest.txt), [constraints](R085b-full-ctest.txt) | [Native guide fixtures](R027b-native-guides.md) |
| [G08](../SCOPE.md) | Selection | [selection](R085b-full-ctest.txt) | [Native selection fixtures](R028b-native-selection.md) |
| [G09](../SCOPE.md) | Camera/navigation | [camera_motion](R085b-full-ctest.txt), [scene](R085b-full-ctest.txt) | [Native navigation fixtures](R029-navigation-push-pull.md) |
| [G10](../SCOPE.md) | Numeric input | [document_units](R085b-full-ctest.txt), [amend](R085b-full-ctest.txt) | [Native numeric entry fixtures](R022-numeric-amend.md) |
| [E01](../SCOPE.md) | Face push/pull | [push_pull](R085b-full-ctest.txt) | [Native push/pull fixtures](R029-navigation-push-pull.md) |
| [E02](../SCOPE.md) | Move/rotate/scale | [transform_selection](R085b-full-ctest.txt) | [Native transform fixtures](R030b-native-transforms.md) |
| [E03](../SCOPE.md) | Arrays and duplication | [copy_array](R085b-full-ctest.txt), [hosted_copies](R085b-full-ctest.txt) | [Native array fixtures](R031-copy-arrays.md) |
| [E04](../SCOPE.md) | Offset | [offset](R085b-full-ctest.txt), [offset_commands](R085b-full-ctest.txt) | Scope gate M6; final integration remains open |
| [E05](../SCOPE.md) | Follow-me/sweep | [sweep](R085b-full-ctest.txt), [sweep_commands](R085b-full-ctest.txt) | Scope gate M6; final integration remains open |
| [E06](../SCOPE.md) | Intersect geometry | [intersections](R085b-full-ctest.txt), [intersection_commands](R085b-full-ctest.txt) | Scope gate M6; final integration remains open |
| [E07](../SCOPE.md) | Solid operations | [booleans](R085b-full-ctest.txt), [solid_operations](R085b-full-ctest.txt), [boolean_commands](R085b-full-ctest.txt) | Scope gate M6; final integration remains open |
| [E08](../SCOPE.md) | Surface appearance | [orientation](R085b-full-ctest.txt), [edge_visibility](R085b-full-ctest.txt), [shading_normals](R085b-full-ctest.txt) | Scope gate M6; final integration remains open |
| [E09](../SCOPE.md) | Geometry inspection/repair | [diagnostics](R085b-full-ctest.txt), [diagnostic_inspection](R085b-full-ctest.txt) | Scope gate M6; final integration remains open |
| [O01](../SCOPE.md) | Groups and nested contexts | [groups](R085b-full-ctest.txt), [consolidation](R085b-full-ctest.txt) | [Native nested-context fixtures](R032b-native-groups.md) |
| [O02](../SCOPE.md) | Components and instances | [components](R085b-full-ctest.txt), [component_scope](R085b-full-ctest.txt), [component_records](R085b-full-ctest.txt) | Scope gate M4; final integration remains open |
| [O03](../SCOPE.md) | Placement behaviors | [component_glue](R085b-full-ctest.txt), [hosted_components](R085b-full-ctest.txt), [host_regeneration](R085b-full-ctest.txt) | Scope gate M6; final integration remains open |
| [O04](../SCOPE.md) | Outliner and tags | [tags](R085b-full-ctest.txt), [selection](R085b-full-ctest.txt) | Scope gate M4; final integration remains open |
| [O05](../SCOPE.md) | Local component library | [library_bundle](R085b-full-ctest.txt) | Scope gate M8; final integration remains open |
| [O06](../SCOPE.md) | Properties and measurements | [entity_measure](R085b-full-ctest.txt), [measurements](R085b-full-ctest.txt), [inspection](R085b-full-ctest.txt) | Scope gate M4/M6; final integration remains open |
| [P01](../SCOPE.md) | Materials and textures | [materials](R085b-full-ctest.txt), [face_textures](R085b-full-ctest.txt), [texture_export](R085b-full-ctest.txt) | Scope gate M4/M7; final integration remains open |
| [P02](../SCOPE.md) | Styles and viewport display | [model_style](R085b-full-ctest.txt), [render_environment](R085b-full-ctest.txt) | Scope gate M7; final integration remains open |
| [P03](../SCOPE.md) | Scenes | [saved_scenes](R085b-full-ctest.txt), [animation_export_real](R085b-full-ctest.txt) | Scope gate M7; final integration remains open |
| [P04](../SCOPE.md) | Sections | [section_geometry](R085b-full-ctest.txt), [section_export](R085b-full-ctest.txt) | Scope gate M7; final integration remains open |
| [P05](../SCOPE.md) | Dimensions, labels and text | [annotations_io](R085b-full-ctest.txt), [text_worker](R085b-full-ctest.txt), [text_commands](R085b-full-ctest.txt) | Scope gate M7; final integration remains open |
| [P06](../SCOPE.md) | Sun and shadows | [solar_io](R085b-full-ctest.txt), [solar_commands](R085b-full-ctest.txt) | Scope gate M7; final integration remains open |
| [P07](../SCOPE.md) | Images and measured views | [reference_image_io](R085b-full-ctest.txt), [measured_export_real](R085b-full-ctest.txt) | Scope gate M7/M8; final integration remains open |
| [P08](../SCOPE.md) | Blender rendering | [blender_real](R085b-full-ctest.txt), [m7_presentation_real](R085b-full-ctest.txt) | Scope gate M5/M7; final integration remains open |
| [P09](../SCOPE.md) | Blender scene handoff | [blender_handoff_real](R085b-full-ctest.txt) | Scope gate M7; final integration remains open |
| [A01](../SCOPE.md) | Shared command API | [commands](R085b-full-ctest.txt), [tool_reference_drift](R085b-full-ctest.txt) | Scope gate M1/M5; final integration remains open |
| [A02](../SCOPE.md) | Queries and perception | [inspection](R085b-full-ctest.txt), [inspection_session](R085b-full-ctest.txt) | Scope gate M5; final integration remains open |
| [A03](../SCOPE.md) | Transactions and preview | [staging](R085b-full-ctest.txt), [transaction_dispatch](R085b-full-ctest.txt), [outcome_store](R085b-full-ctest.txt) | Scope gate M5; final integration remains open |
| [A04](../SCOPE.md) | Embedded assistant | [assistant](R085b-full-ctest.txt), [native_assistant_session](R085b-full-ctest.txt) | [21 remote tasks and three offline controls pass; local gate remains open](R086b-remote-evaluation.md) |
| [A05](../SCOPE.md) | Provider independence | [ollama_provider](R085b-full-ctest.txt), [openai_provider](R085b-full-ctest.txt), [chatgpt_auth](R085b-full-ctest.txt) | [Remote corpus passes; frozen local corpus fails seven live-task thresholds](R086c-local-evaluation.md) |
| [A06](../SCOPE.md) | External automation | [mcp](R085b-full-ctest.txt), [automation_session](R085b-full-ctest.txt) | Scope gate M5/M8; final integration remains open |
| [A07](../SCOPE.md) | Agent instructions/examples | [shipped_recipes](R085b-full-ctest.txt), [tool_reference_drift](R085b-full-ctest.txt) | Scope gate M5/M8; final integration remains open |
| [A08](../SCOPE.md) | AI verification | [measurement_assertions](R085b-full-ctest.txt), [transaction_sequence](R085b-full-ctest.txt) | [Remote injection/preservation scoring passes; local and release gates remain open](R086b-remote-evaluation.md) |
| [X01](../SCOPE.md) | Formline migration | [formline](R085b-full-ctest.txt) | Scope gate M4; final integration remains open |
| [X02](../SCOPE.md) | GLB/glTF | [gltf_import_real](R085b-full-ctest.txt), [glb_export](R085b-full-ctest.txt) | Scope gate M5/M8; final integration remains open |
| [X03](../SCOPE.md) | OBJ and STL | [obj_import_real](R085b-full-ctest.txt), [obj_export_real](R085b-full-ctest.txt), [stl_import_real](R085b-full-ctest.txt), [stl_export_real](R085b-full-ctest.txt) | Scope gate M8; final integration remains open |
| [X04](../SCOPE.md) | DXF exchange | [dxf_import](R085b-full-ctest.txt), [dxf_export_real](R085b-full-ctest.txt) | Scope gate M8; final integration remains open |
| [X05](../SCOPE.md) | Public extension interface | [extension_manifest](R085b-full-ctest.txt), [extension_store](R085b-full-ctest.txt), [extension_worker](R085b-full-ctest.txt) | Scope gate M8; final integration remains open |
| [X06](../SCOPE.md) | Native-format migration | [native_format](R085b-full-ctest.txt), [parser_fault_corpus](R085b-full-ctest.txt) | Scope gate M8/M9; final integration remains open |

Extended F01–F07 and Investigate C01–C03 remain in [scope](../SCOPE.md).
Native `.skp`, existing Ruby extensions and Dynamic/Live Component compatibility
are not delivered by the new extension API or this Core index. No broad SketchUp
parity claim follows from these results.
