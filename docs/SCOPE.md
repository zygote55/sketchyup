# Scope and acceptance matrix

Status: proposed; all rows are **Planned**, not implemented or verified by this PR.
Updated: 2026-10-03. See the [build plan](BUILD_PLAN.md) for gate definitions and
[AI contract](AI_MODELING.md) for automation semantics.

## Reading this matrix

- **Core:** required for the proposed native editor 1.0 release at M9.
- **Extended:** committed planning scope for later workflow parity, delivered at
  M10 in separately scoped releases; not a claim that 1.0 has full SketchUp parity.
- **Investigate:** desired compatibility or capability whose feasibility and
  distribution path must be demonstrated before a delivery commitment.
- **Excluded:** deliberately outside this product's current mandate.

The gate column is the intended first acceptance milestone. M9 revalidates all
Core rows together. Each row must acquire fixture IDs, test evidence and an
implementation reference before its status can change to Done. Acceptance below
is the minimum observable behavior, not a substitute for detailed test cases.

## Native application and document foundation

| ID | Capability | Class | Gate | Acceptance evidence |
| --- | --- | --- | --- | --- |
| N01 | Native desktop and viewport | Core | M1 | Compiled application launches on Arch/Omarchy through Wayland with no Electron/browser runtime dependency; resize and rendering work |
| N02 | Build and package | Core | M1/M9 | Clean CMake build and PKGBUILD install, upgrade and removal; app entry, icon and MIME handling work |
| N03 | Native interaction | Core | M3 | Pointer capture, keyboard layouts, shortcuts, focus and dialogs behave consistently through tool changes |
| N04 | Scaling and displays | Core | M9 | 100%, 150%, 200% and mixed-DPI multi-display tests keep picking, text and overlays aligned |
| N05 | Accessibility/preferences | Core | M9 | Keyboard-accessible controls, visible focus, labels, scalable text, configurable shortcuts and persisted preferences; documented viewport accessibility limits |
| D01 | Units and axes | Core | M1/M3 | Metric, decimal imperial and feet/inches entry convert correctly; world/local axes and precision formatting survive save/load |
| D02 | Native document format | Core | M1/M4 | Versioned round trip preserves IDs, topology, hierarchy, scene state, materials and assets; malformed/future versions fail usefully |
| D03 | Undo and transactions | Core | M1/M4 | Geometry and property changes are atomic and reversible; a new edit removes redo history; canceled operations do not dirty the document |
| D04 | Autosave and recovery | Core | M4 | Forced termination and interrupted-save fixtures recover valid work without overwriting the last explicit save |
| D05 | Model templates | Core | M8 | Choose/save unit, style and material defaults; new documents do not alter the source template |
| D06 | Limits and diagnostics | Core | M9 | Large/corrupt input returns bounded errors; memory and geometry diagnostics identify offending entities without silent loss |

## Drawing, navigation and inference

| ID | Capability | Class | Gate | Acceptance evidence |
| --- | --- | --- | --- | --- |
| G01 | Editable edges/faces | Core | M2 | Closed coplanar outlines form faces; crossing edges split; inner loops create holes; loose edges and open surfaces remain editable |
| G02 | Face healing and deletion | Core | M2 | Face/edge deletion has documented adjacency behavior; valid redraw heals a face; invalid/noncoplanar loops do not fabricate one |
| G03 | Line and freehand | Core | M3 | Connected/disconnected lines and sampled freehand paths work on arbitrary drawing planes with cancel and typed lengths |
| G04 | Rectangles and polygons | Core | M3 | Axis-aligned/rotated rectangles and regular polygons accept exact dimensions and orientation |
| G05 | Circles and arcs | Core | M3 | Circles, center/two-point/three-point arcs and pie sectors retain curve metadata with configurable segmentation and exact radius/angle entry |
| G06 | Inference engine | Core | M3 | Endpoint, midpoint, center, on-edge/on-face, intersection, axis, parallel, perpendicular and tangent fixtures select expected constraints with visible feedback |
| G07 | Inference locks/guides | Core | M3 | Axis/plane/direction locks, reference-point inference, tape/protractor and guide lines/points remain consistent across zoom levels |
| G08 | Selection | Core | M3 | Click, additive/toggle, window/crossing, connected geometry, hidden geometry and nested-context selection resolve the intended entities |
| G09 | Camera/navigation | Core | M3/M7 | Orbit, pan, zoom, fit, perspective/orthographic standard views and configurable FOV; clipping/picking remain consistent; look-around/walk navigation available |
| G10 | Numeric input | Core | M3 | Typed lengths, angles, dimensions and absolute/relative coordinates apply to the active operation without losing focus or scale |

## Editing and organization

| ID | Capability | Class | Gate | Acceptance evidence |
| --- | --- | --- | --- | --- |
| E01 | Face push/pull | Core | M2/M3 | Extruding selected faces updates adjacent topology; pushing to an opposite face can form a through opening; repeat-distance and exact numeric entry work |
| E02 | Move/rotate/scale | Core | M4 | Points, edges, faces and instances transform around explicit pivots; connected geometry follows defined rules; copy mode, flip and numeric scale work |
| E03 | Arrays and duplication | Core | M4 | Linear/radial repeated copies and equal division produce expected counts, spacing and independent/shared identity |
| E04 | Offset | Core | M6 | Planar loops including holes offset by exact distance; concave/self-intersecting/vanishing cases resolve or return actionable errors |
| E05 | Follow-me/sweep | Core | M6 | Profiles follow open/closed paths with defined orientation and corner behavior; invalid self-intersection is handled visibly |
| E06 | Intersect geometry | Core | M6 | Selected/context/model intersection modes create editable edges/faces in the intended context without unrelated changes |
| E07 | Solid operations | Core | M6 | Union, subtract, trim, intersect, split and outer shell pass fixtures; non-solid operands are identified before mutation |
| E08 | Surface appearance | Core | M6 | Reverse/orient faces, soften/smooth/hide edges and reveal hidden geometry without destroying topology; front/back materials preserved |
| E09 | Geometry inspection/repair | Core | M6 | Identify open boundaries, non-manifold regions, inverted faces and degeneracy; any repair is explicit, reportable and undoable |
| O01 | Groups and nested contexts | Core | M4 | Create/edit/explode groups, lock/hide objects, and isolate geometry merging inside the active context |
| O02 | Components and instances | Core | M4 | Shared-definition edits propagate; make-unique, replace, local axes, insertion point, and scale/mirror behavior are documented and reversible |
| O03 | Placement behaviors | Core | M6 | Face-aligned/gluing placement and opening-cutting components update their host correctly, including move, delete and definition changes |
| O04 | Outliner and tags | Core | M4 | Rename/search/select nested entities, assign tags/folders, filter visibility and lock content without modifying geometry |
| O05 | Local component library | Core | M8 | Save/reuse components with thumbnails, materials and dependency assets; missing or duplicate resources handled deterministically |
| O06 | Properties and measurements | Core | M4/M6 | Entity names, bounds, lengths, face areas and valid solid volumes update after edits with units and accessible metadata |

## Presentation and output

| ID | Capability | Class | Gate | Acceptance evidence |
| --- | --- | --- | --- | --- |
| P01 | Materials and textures | Core | M4/M7 | Solid colors, opacity, image textures, front/back assignment, UV position/scale/rotation and projected mapping; assets survive relocation |
| P02 | Styles and viewport display | Core | M7 | Shaded/textured/monochrome/wireframe/X-ray modes, edges/profiles, axes/grid, background and configurable ground/shadows |
| P03 | Scenes | Core | M7 | Named scenes recall selected camera, visibility, style and section state; interpolation and exported camera animation are documented |
| P04 | Sections | Core | M7 | Create/orient/activate section planes per defined context; clipping, section fill/edges and exported section views match |
| P05 | Dimensions, labels and text | Core | M7 | Associative dimensions update with geometry; broken references are marked; leaders, labels and editable 3D text survive round trip |
| P06 | Sun and shadows | Core | M7 | Explicit location/date/time/time-zone settings give reproducible sun direction; users can enter coordinates offline |
| P07 | Images and measured views | Core | M7/M8 | Import reference images; export raster views and scaled orthographic PDF/SVG views with correct units and stated raster/vector limits |
| P08 | Blender rendering | Core | M5/M7 | Optional Cycles render and Eevee preview presets, camera/material conversion, progress/cancel, CPU fallback and reproducibility manifest |
| P09 | Blender scene handoff | Core | M7 | Create/open a `.blend` scene with transferred camera and assets; document one-way handoff and lossy reimport behavior |

## Automation and AI

| ID | Capability | Class | Gate | Acceptance evidence |
| --- | --- | --- | --- | --- |
| A01 | Shared command API | Core | M1/M5 | UI/CLI/AI operate through the same typed commands; schemas, capability discovery, revisions, ID maps and errors are versioned |
| A02 | Queries and perception | Core | M5 | Selection, hierarchy, topology, units, measurements, semantic attributes and snapshots available with pagination/context limits |
| A03 | Transactions and preview | Core | M5 | Preview/apply/discard and direct-edit modes work; one AI task is one undo step; stale revision and duplicate request tests pass |
| A04 | Embedded assistant | Core | M5 | Natural-language creation and scoped modification work; progress, cancellation, failed provider requests and ambiguity are handled |
| A05 | Provider independence | Core | M5/M9 | At least one remote provider and one documented local configuration pass a published capability/evaluation suite; credentials stay outside documents |
| A06 | External automation | Core | M5/M8 | Documented local MCP and headless CLI interfaces use the same contracts; access and document targeting are explicit |
| A07 | Agent instructions/examples | Core | M5/M8 | Versioned guide, generated schemas and executable recipes for rooms, roofs, stairs, openings and furniture; no undocumented required tools |
| A08 | AI verification | Core | M5/M9 | Geometric postconditions, preservation of unrelated entities, undo and recovery are scored; tests cover injected instructions in model metadata |

## Interchange and extension support

| ID | Capability | Class | Gate | Acceptance evidence |
| --- | --- | --- | --- | --- |
| X01 | Formline migration | Core | M4 | `.formline` v1 boxes/cylinders import with correct Y-up to Z-up conversion, dimensions, visibility and colors; source files remain intact |
| X02 | GLB/glTF | Core | M5/M8 | Import/export tested meshes, hierarchy, materials and cameras with units/axes and per-feature loss report |
| X03 | OBJ and STL | Core | M8 | OBJ geometry/material subset and STL triangle data import/export; explicit STL units and topology validation; no false metadata fidelity claim |
| X04 | DXF exchange | Core | M8 | Documented 2D line/polyline/arc layer and unit subset imports/exports; unsupported entities are counted/reported; no full DWG claim |
| X05 | Public extension interface | Core | M8 | Versioned command/query API, sample extension, declared capabilities, lifecycle/errors and compatibility policy; extensions cannot bypass core validation |
| X06 | Native-format migration | Core | M8/M9 | Schema upgrade fixtures pass; future versions fail safely; export of authoritative document data is documented |

## Expanded parity and feasibility work

These rows keep the broader ambition visible. A 1.0 release must list them as
remaining gaps; none can disappear from scope by calling the Core list full parity.

| ID | Capability | Class | Gate | Required outcome / feasibility gate |
| --- | --- | --- | --- | --- |
| F01 | LayOut-style documentation | Extended | M10 | Multi-page sheets, title blocks, scaled linked views, dimensions, annotations, print/PDF and model-update propagation with regression fixtures |
| F02 | Configurable components | Extended | M10 | Editable parameters, constrained formulas, attribute inspector and reusable architectural/furniture assemblies; define compatibility separately from SketchUp Dynamic/Live Components |
| F03 | Terrain and site tools | Extended | M10 | Contour triangulation, terrain sculpting, drape/stamp and site coordinates with large-coordinate precision tests |
| F04 | Broader CAD/BIM exchange | Extended | M10 | Prioritize IFC and STEP use cases; publish supported entities, hierarchy/property fidelity and geometry conversion limits |
| F05 | Additional animation/output | Extended | M10 | Named camera paths, export controls and batch presentation jobs; no requirement for a general character-animation suite |
| F06 | Rich style/asset authoring | Extended | M10 | Custom edge styles, expanded material authoring, packaged content catalogs and asset dependency management |
| F07 | Reports and schedules | Extended | M10 | Component/material counts, areas/volumes and configurable CSV/tabular exports update from document semantics |
| C01 | Native `.skp` read/write | Investigate | M0 research / M10 delivery | Demonstrate a supportable Linux implementation and permitted dependency distribution, version matrix and fidelity corpus before promising support |
| C02 | Existing Ruby extensions | Investigate | M10 | Inventory required APIs/runtime behavior and test representative extensions; a new scripting API alone is not compatibility |
| C03 | SketchUp Dynamic/Live Component compatibility | Investigate | M10 | Determine format/runtime/service dependencies, update behavior and supported subsets; report differences from native parameters |
| C04 | 3D Warehouse/hosted assets | Investigate | M10 | Confirm supported access, authentication, asset permissions and offline behavior; provide importable local libraries independently |
| C05 | DWG/full CAD interoperability | Investigate | M0 research / M10 delivery | Confirm available libraries, licensing/distribution and round-trip guarantees; keep DXF subset claims distinct |
| C06 | Online geolocation/maps | Investigate | M10 | Evaluate source access, cost, coordinate systems, attribution and offline caching; manual location remains available |
| C07 | Real-time collaboration/cloud sync | Investigate | M10 | Separate multi-user transaction/conflict and service design; cannot be inferred from single-user AI support |

## Explicit exclusions from the current mandate

- Pixel-identical proprietary branding or bundling proprietary assets without a
  suitable distribution path.
- Full Blender replacement: sculpting, character rigging, compositing, physics
  simulation and general-purpose animation production.
- Full parametric mechanical CAD/history-tree or NURBS authoring system. Exchange
  adapters may tessellate such geometry and must explain the loss of editability.
- Mandatory cloud accounts, hosted storage, subscriptions or online connectivity
  for ordinary manual modeling.
- Windows/macOS/mobile/browser application support and ARM release guarantees.
  These need separate staffing, platform gates and packaging work.

## Maintaining the scope baseline

At M0, compare this inventory to the chosen SketchUp reference release and add
missing workflows explicitly. Use this row format for updates:

```text
ID | Class | Target gate | Status | Fixtures | Evidence/PR | Known limitations
```

Split a row if only a subset ships; do not mark the whole row Done. A failed or
unavailable compatibility investigation remains an explicit limitation. Move a
Core row out of 1.0 only through an intentional scope-change PR that explains the
user impact. Keep documentation and marketing aligned with that decision.
