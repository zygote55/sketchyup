# IFC exchange subset

Status: **Proposed (2026-10-22)**, awaiting the owner decisions listed below. Roadmap
[R097](../PR_ROADMAP.md#r097) (M10, scope row [F04](../SCOPE.md)); the sibling
[R098](../PR_ROADMAP.md#r098) covers STEP. Nothing in this ADR is implemented. Each
section gives the options and trade-offs that were weighed and a recommendation; the
decisions are collected under "Owner decisions". Facts about third-party projects were read
on 2026-10-22 and are cited in the [appendix](#appendix-evidence-and-sources); anything not
verified is marked as such. The UX design for the desktop dialogs is **not** part of this
ADR; see decision 15.

## Context

F04 asks for "broader CAD/BIM exchange: prioritize IFC and STEP use cases; publish
supported entities, hierarchy/property fidelity and geometry conversion limits". R097's
Build line: select an IFC library and schema subset with distribution review; map the
supported hierarchy, properties and geometry with a fidelity report; limit claims to
demonstrated import and export directions. Its Verify line: named fixtures preserve the
claimed units, hierarchy and properties; unsupported entities are reported; converted
geometry remains inspectable. R097 requires R089 (the 1.0 gate, accepted) and R074 (the
glTF interchange work, delivered). F04 ships in its own gated minor release
([M10 acceptance](../M10_ACCEPTANCE.md), [ADR 0162](0162-release-versioning.md)); the
release also needs R098, so R097 alone does not make F04 delivered.

What the repository gives us (surveyed at `db719b0d`, the 1.3.0 release):

- **Importers are bounded pure functions.** DXF ([ADR 0120](0120-bounded-planar-dxf-source.md),
  [0121](0121-native-dxf-wire-conversion.md)), OBJ ([0112](0112-bounded-obj-mtl-parsing.md),
  [0113](0113-native-obj-conversion.md)), STL ([0116](0116-bounded-stl-source-parsing.md)) and
  glTF ([0109](0109-bounded-gltf-packages.md), [0110](0110-native-gltf-conversion.md)) each
  split into a bounded source parser (size, line, record and name limits, explicit units,
  counted omissions), a conversion that builds one unsaved native document in one edit and
  validates the whole serialization before returning, a report, then a desktop and CLI
  workflow ([0111](0111-native-gltf-import-workflow.md), [0115](0115-native-obj-interchange-workflow.md),
  [0123](0123-native-dxf-interchange-workflow.md)). Every one of them opens a **new unsaved
  document**; terrain is the only importer that adds to the open one ([ADR 0168](0168-terrain-import-and-surfaces.md)
  decision 11). Reports are JSON with `apiVersion: 1`, source SHA-256, counts, `losses`
  and `notices`; DXF also has `omittedEntities`.
- **Parser fuzzing is a release practice.** One libFuzzer target per format, seeds from
  generated fixtures, 512-mutation checks in `parser_fault_corpus_tests`, and ASan/UBSan
  campaigns ([PARSER_FUZZING](../PARSER_FUZZING.md)). A new parser without a target is not
  accepted.
- **Native model.** `Body` (`src/core/body.hpp`) is a geometry body, group, reference
  image or terrain, with a name (64 bytes in diagnostics), colour, per-face colour and
  material, a `tag`, `hidden`/`locked`, a `transform`, a `parent`, and
  `properties`: a flat map of string keys (at most 128 bytes) to bool, finite double or
  string (at most 2,048 bytes), at most 128 per body (`src/core/model.cpp`). Component
  definitions and placements exist ([ADR 0159](0159-instanced-component-storage.md)); glTF
  import turns a shared mesh into one definition with one placement per node. Faces must be
  planar within 1e-7 m; the tessellator is Clipper2; `checkPoint` refuses any coordinate
  beyond 1e6 m. Unique geometry is limited to 100,000 vertices, 100,000 faces and 10,000
  body records, and 600,000 faces materialized (`DocumentLimits`); the model JSON chunk is
  capped at 32 MiB (`NativeLimits::modelBytes`), so a large property load can break saving
  even when every geometry limit holds.
- **Site origin.** `SiteOrigin` (`src/core/site_origin.hpp`, R095.a) is a translation only:
  easting, northing, elevation in metres (each within ±1e8 m), an informational CRS label
  of at most 64 bytes, and provenance. There is **no rotation and no scale**. A document with a
  site origin is written as schema 29 and is refused by SketchyUp 1.2.x and earlier (ADR 0168
  decision 7). No PROJ dependency exists (decision 8).
- **Solid Booleans.** The R055 Manifold adapter ([ADR 0046](0046-solid-booleans.md))
  needs valid closed solids and rejects anything else with defect IDs.
- **Dependencies.** Third-party code is vendored unmodified under `third_party/` (Clipper2
  BSL-1.0, Manifold Apache-2.0, cgltf MIT, CDT MPL-2.0), linked statically; their
  licences are installed to `share/licenses/sketchyup` and collected into
  `THIRD_PARTY_NOTICES` by `scripts/release-artifacts.sh`. Application code is MIT
  ([ADR 0001](0001-baseline.md)). The package runtime depends only on Qt, OpenSSL, Wayland,
  shared-mime-info and a font; it builds from a source archive in the pinned image (Arch
  archive snapshot 2026/10/02) with official-repository packages only.
- **Automation.** Importers other than terrain are desktop and CLI workflows, not
  `noun.verb` commands. `assistantVisible()` filters the assistant's tools and
  `assistant_tool_surface_tests` pins them; the frozen provider corpus is at v5
  ([ADR 0168, R095.g notes](0168-terrain-import-and-surfaces.md#implementation-notes-r095g)).

What the fixtures show. buildingSMART's public certification datasets contain the same
"Simple-Scene" in three schemas (IFC2x3 TC1, IFC4 ADD2 TC1, IFC4.3 ADD2). I downloaded the
three `Building-Architecture.ifc` files and counted instance lines. They differ in
representation: the IFC2x3 file has 20 `IfcFacetedBrep` and 2 `IfcExtrudedAreaSolid` items;
the IFC4 and IFC4.3 files have 12 `IfcTriangulatedFaceSet` and 2 `IfcExtrudedAreaSolid`.
All three have 1 project, 1 building, 1 storey, 2 spaces, 4 walls, 3 slabs, 1 roof, building
element proxies (5, 3 and 2),
a classification reference with a URI, and psets and quantity sets; none has a door or
window. The IFC4 and IFC4.3 files carry an `IfcProjectedCRS` (`EPSG:32760`) and an
`IfcMapConversion` whose axis is rotated **60 degrees** (`XAxisAbscissa` 0.5, `XAxisOrdinate`
0.866) with `Scale` 0.001 and a millimetre project length unit. So the georeferencing
fixture buildingSMART certifies against cannot be represented by a translation-only site
origin; decision 6 exists because of this. The one door/window/opening example is the
five-file "ReferenceView_V1.2" set (a wall with an opening and a window, built from three
extrusions and one `IfcRelVoidsElement`).

## 1. Library choice and distribution review

### Requirements the choice must meet

1. A licence that SketchyUp can distribute: MIT application code, static vendored
   dependencies, system Qt, notices installed with the package.
2. Buildable in the pinned Arch image from official-repository packages and vendored
   source, without network access at build time, and packageable by the existing PKGBUILD.
3. Bounded: hard limits on bytes, entities, depth, work and memory that this project
   controls, refusing rather than exhausting (the pattern of every existing importer).
4. Fuzzable with the project's libFuzzer harness and sanitizers.
5. Able to write IFC, because export is in scope.
6. Maintained.

### Options

| | A. IfcOpenShell (full: IfcParse + IfcGeom) | A2. IfcOpenShell IfcParse only + our own geometry | B. In-house STEP-P21 reader and writer, bounded IFC4 subset | C. web-ifc (C++ core) | D. IfcOpenShell as an external worker (like Blender) |
| --- | --- | --- | --- | --- | --- |
| Licence | LGPL-3.0-or-later | LGPL-3.0-or-later | MIT (ours) | MPL-2.0 | LGPL-3.0-or-later, not linked |
| Compatible with MIT app distribution | Yes, with obligations (below) | Yes, with obligations | Yes | Yes (file-level copyleft; precedent: CDT, MPL-2.0, already vendored) | Yes (process boundary) |
| Arch packaging | AUR only (`ifcopenshell` 0.9.0-2, `ifcopenshell-stable` 0.8.5-4, `-git`); not in `extra` | Same source; no distro package of the parser alone | Nothing to package | Not packaged; vendor the source | AUR package or PyPI wheel, user-installed |
| Build weight | Heavy: OCCT 7.5.1+, CGAL 5.4+, Boost, HDF5, libxml2, Python in the AUR package | Measured: 6.4 min at `-j4` for two schemas; Boost headers needed | Small (our code) | Moderate: C++20, 75k lines, nine pinned third-party libraries | None in the build |
| Runtime weight | Shared libraries plus schema plugins plus OCCT | About 10 MB of shared objects for IFC2x3 + IFC4 (measured) | Our binary | Static library | A separate install of 52 MB (PyPI wheel) or more |
| Writes IFC | Yes | Yes (entity API) | We write it | Saves a loaded file; composing new entities is in the WASM binding (see below) | Yes, in the worker |
| Bounded by us | No: upstream limits, upstream allocation behaviour | Partly: parse limits are ours to wrap, the parser's are not | Yes | No | Yes, by the process (time, memory, output size) |
| Fuzzing | Upstream has `src/ifcfuzz` and a `BUILD_FUZZERS` option; we would fuzz foreign code with foreign limits | Same, smaller surface | Our target, our limits | No fuzz harness found in the repository | Isolated by the process |
| Maintenance | Very active (pushed the day of writing; 2,054 open issues) | Same | Ours | Active (74 open issues) | Active upstream; user-managed |
| Geometry (extrusions, booleans, swept solids) | Yes (OCCT/CGAL kernels) | No: ours to write | No: ours to write | Yes (own engine) | Yes |

**Licence and distribution, option by option.**

- **A and A2 (LGPL-3.0-or-later).** SketchyUp links everything statically except Qt. LGPL
  terms for a combined work require that the user can replace the library: either use a
  suitable shared-library mechanism or ship the application's source/object code so it can
  be relinked. SketchyUp's source is public and MIT, so a static link is possible, but it
  adds a notice, relinking information and a build step to every release. Upstream's own
  CMake says `BUILD_SHARED_LIBS` is required (`ON` by default) and loads schemas as plugins
  at run time, so in practice the package would ship `libifcopenshell.parse.so`, a plugin
  library and one schema plugin per IFC version, changing the packaging model from one
  static binary. The licence array in the PKGBUILD would gain `LGPL-3.0-or-later`. The
  Bonsai add-on and some tools in the repository are GPL-3.0-or-later; they would not be
  built. This is workable, not forbidden. I did not obtain a legal opinion; the owner
  should treat this paragraph as an engineering reading.
- **C (MPL-2.0).** Vendoring unmodified source and keeping modifications in separate files
  is the pattern already used for CDT. The core is MPL-2.0 but its CMake downloads nine
  libraries at pinned commits (fast_float, TinyNURBS, GLM, earcut.hpp, CDT, spdlog, stduuid,
  unordered_dense and a test framework) with their own licences; each would need vendoring
  and a notice, and its CDT pin differs from ours (2.0.0).
- **B.** No third-party licence. Two caveats about buildingSMART's own material. First, the
  `IFC4.3.x-development` repository's `LICENSE.md` opens with **CC BY-ND 4.0** ("no
  derivatives") and its README says formal static releases are CC BY-ND 4.0, but the same
  file's copyright notice says the technical documentation "may be photocopied, used in
  software development, or translated into another computer language without prior written
  consent ... provided that full attribution is given", with consent needed only "if changes
  are made to the technical specification". Whether mechanically generating code from the
  EXPRESS files is a derivative is therefore unclear to me, and the repository's licence
  reads as NOASSERTION to GitHub. The recommendation avoids the question: the entity attribute
  tables are written by hand from the published documentation and verified against an
  independent oracle (section 6). Second, the same file has a **trademark notice**: IFC(TM)
  and ifcXML(TM) are buildingSMART trademarks and "should be given appropriate credits"; the
  user guide and the fixture notice must credit them. I have no legal opinion; the owner
  should confirm both points.
- **D.** Nothing is linked and nothing is shipped, so no licence obligation attaches to
  SketchyUp's package. The user installs the tool. This matches how Blender is treated, but
  Blender is a rendering extra, whereas importing a user's IFC file is a core exchange
  workflow that would then need an AUR build or a 52 MB wheel to work at all.

**Measured prototype (option A2).** In `sketchyup-ci-pinned:r094a` (Arch 2026/10/02 archive,
GCC 16.2.1, CMake 4.4.3) with `boost` 1.92.0 installed from the archive, I built the
IfcOpenShell `ifcopenshell-v0.9.0` tag (`7ba70197`) with only IfcParse: geometry, OCCT,
CGAL, the converter, Python and the examples switched off, schemas `2x3;4`, Release.

| Measure | Result |
| --- | --- |
| Configure | Succeeds with only Boost added to the image |
| Build, 29 steps, `-j4`, container capped at 10 GB / 4 CPUs | **384 s** |
| Compiler memory | 620–885 MB resident per compiler process, sampled with `ps` during the build (generated schema sources: `Ifc4.cpp` 1.7 MB, `Ifc4.h` 3.3 MB, `Ifc2x3.cpp` 1.4 MB, `Ifc2x3.h` 2.3 MB); schema `4x3_add2` was not built, so its weight is unmeasured |
| Outputs | `libifcopenshell.parse.so` 1.4 MB, `libifcopenshell.plugin.so` 0.13 MB, `ifcopenshell_parse_schema_ifc2x3.so` 3.8 MB, `ifcopenshell_parse_schema_ifc4.so` 4.6 MB; build tree 24 MB |
| Run-time dependencies of the libraries | libstdc++, libm, libgcc, libc only; Boost is a build-time header dependency |

This shows IfcParse alone is buildable and light at run time. It does **not** measure
IfcGeom: I did not build OCCT or CGAL. For comparison, `extra` has `opencascade` 7.9.3
(164 MB installed, 36 MB compressed), `cgal` 6.2.1 (44 MB), `boost` 1.92.0 (158 MB) and
`hdf5` 2.2.0 (15 MB), and the PyPI wheel `ifcopenshell` 0.9.0.post1 is 52 MB per platform
(it bundles the geometry engine). The AUR `ifcopenshell` package's own dependency list
includes `blender`, `qt6-base`, `qt6-svg`, `swig`, `python-numpy` and others as build
dependencies, so it is a poor model for a lean package. A build from source for IfcGeom
means adding at least OCCT and CGAL to the pinned image, which cuts against requirement 2's
offline and reproducibility discipline.

**Not feasible.** [IfcPlusPlus](https://github.com/ifcquery/ifcplusplus) is MIT and C++ but
its README says it is "not actively developed any more" and recommends xeoIFC, which is
not open source (its README describes licence and pricing terms). `ifc-lite` (Rust, compiled
to WebAssembly; a third-party index lists MPL-2.0 and about 140 stars) is not C++ and I did
not verify its licence or maturity. STEPcode (BSD, C) is a generic STEP class generator, not
an IFC geometry library. None is recommended.

**What each option leaves for us to write.** Option A leaves almost nothing but adopts the
heaviest dependency and its failure modes. A2 leaves all geometry interpretation, which is
the larger half of the work anyway. B leaves the STEP syntax layer and the attribute tables
too, but those are the smaller half: ISO 10303-21 is a small grammar, and the supported
subset needs roughly one hundred entity layouts across three schemas. C leaves the writer and
the bounds. D leaves nothing, at the cost of the dependency being the user's.

### Fuzzing exposure and parser risk

IFC parsers are a real attack surface: Bentley's advisory
[BE-2022-0006](https://www.bentley.com/en/advisories/be-2022-0006/) lists eleven CVEs for
crafted IFC files in MicroStation (CVSS 7.8, including CVE-2022-28301 and CVE-2022-1229),
all out-of-bounds or uninitialised-memory defects. For the libraries considered, GitHub's
security advisories API returned no published advisories for IfcOpenShell or for web-ifc on
2026-10-22, and OSV returned nothing for their packages; that shows no disclosed advisories,
not the absence of defects. The project's own rule is stronger than "choose a library with
no CVEs": every parser has hard limits enforced by the project and a fuzz target in its
harness. Only B and D meet it without trusting another project's allocation behaviour, and
D meets it by isolation.

### Recommendation

**B: an in-house, bounded STEP-P21 reader and writer for a defined IFC subset**, with
geometry interpretation on the existing Clipper2 and Manifold, and with IfcOpenShell used
only as a **development-time test oracle** (section 6), never linked, shipped or required to
build. Reasons:

1. It is the only option that keeps the current distribution model (one MIT application, a
   static vendored set, official-repository build dependencies, no new runtime package).
2. It gives bounds and refusals the importers' pattern needs, with a first-party fuzz
   target. The subset, three schemas' worth of entity layouts, is small enough to hand-write
   and to verify against an oracle on real files.
3. Geometry interpretation has to be written under A2 and C as well; we already have the
   tessellator and the Boolean adapter that interpretation needs.

The cost is real: roughly the first three slices of the split below are new code, the
attribute tables need care, and an in-house reader will lag IFC as it evolves. If R097.a–c
cannot meet their Verify criteria, the fallback order is A2 (IfcParse as shared libraries,
our own geometry), then D. "Reconsideration triggers" lists the conditions.

## 2. Schema and directions

### Options

| Schema | Where it stands | Role in R097 |
| --- | --- | --- |
| IFC2x3 TC1 (`IFC2X3`) | The oldest schema still widely exchanged; its Coordination View 2.0 is described by buildingSMART's database as final and is the most widely implemented. No map-conversion entities. Geometry: faceted breps, extrusions, boolean clipping, mapped items; no tessellated face sets | Import. Export deferred |
| IFC4 ADD2 TC1 (`IFC4`) | The Reference View 1.2 (final) is a one-way coordination view whose ISO-specification examples are tessellation-based; the Design Transfer View is listed as draft. Adds `IfcTriangulatedFaceSet`, `IfcPolygonalFaceSet`, `IfcMapConversion`, `IfcProjectedCRS`. Third-party summaries map it to ISO 16739-1:2018 (not verified against the ISO catalogue) | Import and export |
| IFC4.3 ADD2 (`IFC4X3_ADD2`, "IFC 4.3.2.0") | buildingSMART states this is the ISO 16739-1:2024 version. Same building entities; adds infrastructure (alignments, linear placement, rail, road, bridge) | Import of the **building** subset only; infrastructure entities reported. Export deferred |

The accepted `FILE_SCHEMA` values are `IFC2X3`, `IFC4` and `IFC4X3_ADD2`. `IFC4X1`, `IFC4X2`,
`IFC4X3_RC*`, `IFC4X3_ADD1`, any multi-schema declaration and any unknown identifier are
refused with the schema named, so no file is interpreted with another schema's layout.
`.ifczip` (compressed) and `.ifcXML`/`.ifcJSON` are refused with their format named; there
is no zip reader in the codebase and none is proposed.

### Directions and claims

- **Import** is claimed per schema, and only for the subset in section 3, when its fixtures
  pass. Evidence exists in principle for all three schemas: the buildingSMART certification
  datasets have the same Simple-Scene in each.
- **Export** is IFC4 ADD2 TC1 only, tessellated, in the style of the Reference View (four of the
  five ISO-specification Reference View samples are named for tessellation; the fifth is a
  wall with an opening and a window). It is claimed only after an **independent consumer** reads the file back
  (section 6). Export to IFC2x3 or IFC4.3 is not claimed; decision 2 records why.
- **No view or certification claim.** Export is tessellated like the Reference View's
  examples, but R097 does **not** claim Reference View conformance or buildingSMART
  certification. The file header's `FILE_DESCRIPTION` therefore does not declare a
  `ViewDefinition`; it states `SketchyUp export, tessellated geometry`. Claiming a view would
  need the view's exchange requirements and the buildingSMART validation rules (the MIT
  `ifc-gherkin-rules` repository) to pass first; that is a later row if the owner wants it.
- **No semantic round trip is claimed.** A file exported by SketchyUp and re-imported must
  reproduce geometry, hierarchy and properties within the stated tolerances; that is a test,
  not a promise about other tools. Native history, topology, curves, guides, annotations,
  sheets and parameters do not appear in IFC (the same position as ADR 0111 and 0115).
- A direction that fails its checks at acceptance is not claimed in the support matrix, and
  the release says so. Decision 3 makes this explicit so import can ship without export.

## 3. Mapping

A guiding rule: **never guess**. An element whose placement, units or representation cannot
be interpreted is **not imported** and is listed in the report; it is never placed at the
origin, scaled by assumption or replaced by a bounding box. Every accountable entity ends up
converted, consumed as supporting data, or counted as omitted (section 4's accounting
identity).

### 3.1 Spatial hierarchy

| IFC | Native | Notes |
| --- | --- | --- |
| `IfcProject` | A top-level group, named from `Name` | The group carries `ifc.Class`, `ifc.GlobalId`, `ifc.LongName`, `ifc.Phase` and the source schema (`ifc.Schema`) as properties |
| `IfcSite` | Group under the project | May have no geometry. IFC4 files can have several sites (the Simple-Scene has two) |
| `IfcBuilding` | Group under its site | |
| `IfcBuildingStorey` | Group under its building | `ifc.Elevation` in metres as a property (storeys are groups, not levels; there is no native level concept) |
| `IfcSpace` | Geometry body (a group if it aggregates children) in its storey, on the tag `IFC Space` | Spaces are volumes; they double the faces and hide the building if shown. Decision 8 |
| `IfcElementAssembly` and `IfcRelAggregates` parts | Group containing the parts | Order follows `RelatedObjects` |
| `IfcRelContainedInSpatialStructure` | The element's parent is the container's group | An element in no container goes under the project group, counted as a notice (`uncontainedElements`) |
| `IfcRelNests`, `IfcRelDeclares`, `IfcProjectLibrary` | Not hierarchy | Counted as omitted; library type definitions are consumed only through types (below) |
| `IfcZone`, `IfcSpatialZone`, `IfcSystem`, `IfcGroup` and `IfcRelAssignsToGroup` | Not groups | Membership is recorded on each member as the property `ifc.Groups` (a comma-joined list of at most 2,048 bytes), and the zone or system is counted as converted-to-properties |
| 4.3 `IfcFacility`, `IfcBridge`, `IfcRoad`, `IfcRailway` and other infrastructure spatial elements | A plain group if they contain at least one supported element; otherwise omitted | `ifc.Class` records the class; the notice states that infrastructure semantics are not interpreted |

The group tree follows **containment and aggregation**, not the placement chain; the two
often differ (an element's `PlacementRelTo` need not be its container). Each group or body
therefore gets the native transform `inverse(parent world) × world`, where `world` is the
exact composition of its `IfcLocalPlacement` chain, so moving a storey group moves its
elements and element vertices stay in small local coordinates. World positions are re-checked
against the 1e6 m native bound after composition. An element with two parents (two
containment or aggregation relationships) takes the first in file order and gets a notice
(`multipleParents`); a cycle in containment or aggregation leaves the elements in the cycle
not imported and reported.
`IfcAxis2Placement3D` axes are derived into an orthonormal frame by the standard's rules, so
the result is a proper rotation; the import refuses a placement with a non-finite or
zero-length axis or a non-finite coordinate. A cycle in `PlacementRelTo`, a depth above 64, or an
`IfcGridPlacement` or `IfcLinearPlacement` (unsupported) leaves that element not imported and
reported by `GlobalId`.

Names: `IfcRoot.Name` becomes the body name, shortened at a UTF-8 code-point boundary to 256
bytes (the DXF importer's rule; native names allow 1,024); the full name is kept as `ifc.Name`
when it was shortened. A missing name becomes
`<Class> <first 6 characters of GlobalId>`. `GlobalId` values that are not 22 characters of
`0-9A-Za-z_$` are kept as text with a notice (`invalidGlobalId`); a second element with a
`GlobalId` already seen keeps its own value in `ifc.GlobalId` and gets a notice
(`duplicateGlobalId`); the hierarchy checks use the first.

**Tags.** One tag per IFC class family of the imported elements (`IFC Wall`, `IFC Slab`,
`IFC Space`, `IFC Opening` …), at most 128 distinct tags, the rest on `IFC Other`.
`IfcPresentationLayerAssignment` layer names are kept as the property `ifc.Layer` rather than
as tags (a layer with `LayerOn` false or `LayerFrozen` true imports its bodies **hidden**, with
a notice, so the hidden state of an export survives a re-import), because a body has one tag and class tags give the useful visibility switches
(DXF's layer-to-tag rule is the same idea applied to a format whose layers are its only
organization). Decision 8.

### 3.2 Elements

Three tiers, so the claim is honest and the report is precise.

**Tier 1, fully specified and tested (the named subset):** `IfcWall` and `IfcWallStandardCase`
(2x3), `IfcSlab`, `IfcDoor`, `IfcWindow`, `IfcBuildingElementProxy`, and the spatial elements
above.

**Tier 2, imported generically:** any other `IfcElement`, `IfcFeatureElement`
(non-opening), `IfcGeographicElement` or `IfcSpace`-like product with a supported body
representation (beams, columns, roofs, stairs, coverings, railings, furnishing, and so on).
It becomes a geometry body with `ifc.Class`, `ifc.PredefinedType` and `ifc.ObjectType`. The
report counts these under `importedGeneric` by class, so nobody reads a generic import as a
claim of class-specific semantics.

**Tier 3, not imported, always reported:** products whose representation or placement is
unsupported (below), `IfcAnnotation` and other 2D products, cameras and lights,
infrastructure alignments, `IfcVirtualElement`, and any class outside the accountable set.
Each class is counted in `omittedEntities`; individual elements are listed in `locations`.

**Door and window.** The body representation is imported as is: a panel, a lining or both as
modelled. `OverallWidth`, `OverallHeight` and, for IFC4, `PartitioningType`/`OperationType`
are kept as properties, with lengths converted to metres. There is no parametric door or
window; geometry is the file's.

**Openings.** `IfcOpeningElement` and `IfcRelVoidsElement` describe voids that most exporters
do **not** subtract from the host's body. Policy (decision 5): when the host and the opening
both convert to closed solids, subtract the opening from the host with the R055 solid Boolean
adapter and record `openingsApplied`; the opening is consumed, not imported. When either is
not a closed solid, or the adapter rejects the operands, the host stays uncut, the opening
is imported **hidden on the tag `IFC Opening`**, and the report names the pair under
`openingsNotApplied` with the adapter's reason. `IfcRelFillsElement` (a window in an opening)
needs no action: the window's own placement puts it in the opening.

**Types.** `IfcRelDefinesByType`: the type's name becomes `ifc.Type`, and the type's property
sets are copied onto each occurrence that does not define the same property itself (the
certification test asks for type-level properties to show at occurrence level). Where the
occurrence and its type share a `RepresentationMap` (an `IfcMappedItem`), the representation
becomes a component definition (section 3.3).

**Classification.** `IfcRelAssociatesClassification` becomes `ifc.Classification`
(`<system>:<reference identification> <name>`, at most 2,048 bytes) and
`ifc.ClassificationUri` (the reference location, as given).

### 3.3 Geometry

Native faces are planar polygons; IFC solids are not. **Everything is tessellated** on
import: the result is ordinary editable faces, edges and (where an extruded profile or
boolean produced them) coplanar polygon faces, never a parametric record. The report states,
per IFC class and per representation item type, how many bodies were converted and by what
method. The converted body is always an ordinary geometry body: selectable, editable and
undoable like any other.

| IFC representation item | Import | Slice |
| --- | --- | --- |
| `IfcTriangulatedFaceSet` (IFC4, 4.3) | Triangles, shared vertices per body, `IfcIndexedColourMap` to per-face colour | b |
| `IfcPolygonalFaceSet`, `IfcIndexedPolygonalFaceWithVoids` (IFC4, 4.3) | Polygons with holes through the native tessellator; non-planar faces (outside 1e-7 m) are split into triangles and counted | b |
| `IfcFacetedBrep`, `IfcFacetedBrepWithVoids`, `IfcFaceBasedSurfaceModel`, `IfcShellBasedSurfaceModel` (faces and shells of planar polygons) | As above; open shells stay open surfaces; faces with `IfcFaceBound` orientation flags and holes honoured | b |
| `IfcExtrudedAreaSolid` with `IfcRectangleProfileDef`, `IfcCircleProfileDef`, `IfcArbitraryClosedProfileDef`, `IfcArbitraryProfileDefWithVoids`; profile curves `IfcPolyline` and `IfcIndexedPolyCurve` (lines and arcs) | Profile outline (arcs segmented at the chosen resolution) swept along the extrusion direction; end caps and side faces | c |
| `IfcRevolvedAreaSolid` | Revolution segmented at the chosen resolution; angle in the project's plane-angle unit | c |
| `IfcBooleanClippingResult`, `IfcBooleanResult` (difference, union, intersection) over supported operands, `IfcHalfSpaceSolid` and `IfcPolygonalBoundedHalfSpace` | Evaluated with the Manifold adapter on closed solids; a half-space is bounded to a finite box (robustness rules below) | c |
| `IfcMappedItem` over an `IfcRepresentationMap` | One component definition per distinct map and one placement per use; a non-uniform scale in the mapping operator stays in the placement transform, as for glTF ([ADR 0110](0110-native-gltf-conversion.md)) | c |
| Other parametric profiles (`IfcIShapeProfileDef`, `IfcLShapeProfileDef`, `IfcTShapeProfileDef`, `IfcUShapeProfileDef`, `IfcRectangleHollowProfileDef`, `IfcCircleHollowProfileDef`) | Reported as unsupported until a later slice adds them; the element is not imported | later |
| `IfcExtrudedAreaSolidTapered`, `IfcSweptDiskSolid`, `IfcSurfaceCurveSweptAreaSolid`, `IfcFixedReferenceSweptAreaSolid`, `IfcSectionedSolid` | Unsupported in R097; reported by class | later |
| `IfcAdvancedBrep`, B-spline and NURBS surfaces, `IfcCsgSolid` and CSG primitives, `IfcGeometricCurveSet` and other curves, `IfcPointCloud`-like items | Unsupported; reported | not planned |

**Representation selection.** An element's `Body` representation (identifier `Body`, type
`Tessellation`, `Brep`, `SweptSolid`, `Clipping`, `CSG`, `MappedRepresentation`) in the
`Model` context is used. Where there are several, the first that converts completely in file
order wins and the others are counted as `alternativeRepresentationsSkipped`. `Axis`, `Box`,
`FootPrint`, `Annotation`, `Plan` and `Reference` representations are not imported and are
counted by identifier. An element with a `Body` representation that does not convert is not
imported; it is not replaced by its `Box`.

**Robustness rules.**

- *Planarity and weld.* Native faces must be planar within 1e-7 m and the surface welds points
  within 1e-7 m. IFC coordinates are often rounded far coarser (the fixtures use three decimals
  in millimetres, 1e-6 m), so n-gon faces from faceted breps will frequently fail planarity.
  Such a face is split into triangles and counted (`nonPlanarFaces`), which can inflate face
  counts toward the 100,000 limit; the cost is measured in R097.b, and a coplanar-patch merge
  within a looser tolerance (hidden edges) is the later remedy if it matters. Triangles that
  collapse after the weld are dropped and counted (`degenerateTriangles`).
- *Winding and mirroring.* Shell orientation follows the file (faceted breps carry outward
  orientation; `IfcFaceBound.Orientation` is honoured). A negative-determinant transform keeps
  the reflection in the placement and the tessellation winding is corrected, as for glTF.
- *Half-spaces.* A half-space is not an infinite solid: it is bounded to the other operand's
  bounding box expanded by a margin (1.5 times the extent plus 1 m), so the Manifold adapter's
  12.5 nm grid and 25 nm extent tolerance ([ADR 0046](0046-solid-booleans.md)) are not
  exceeded; operands that still exceed them are rejected by the adapter and the element is
  reported (`booleanRejected`).
- *Boolean results.* The adapter returns polygon faces with holes, not triangles. A result with
  several disconnected parts becomes one group of bodies named `<Name> (part n)` with a notice,
  so one IFC element is not silently merged or dropped.
- *Boolean and mapped-item blow-up.* Boolean tree depth, operations per import and mapped-item
  nesting are bounded (6.3); `IfcMappedItem` and `IfcRepresentationMap` nesting is at most 8,
  cycles are refused, and the **expanded** size (uses times definition size) is computed from
  counts and checked against the materialized limits before any geometry is generated. An
  element over a bound is not imported and is reported (`booleanLimit`, `mappedExpansion`).

**Tolerances.** Arcs and revolutions are segmented at 12 to 256 segments per full circle
(default 96, the DXF rule); the report gives the maximum chord deviation in metres. The
file's `Precision` (per context) is recorded in the report and does not change native
tolerance (1e-7 m). A body over the native per-body limit of 100,000 vertices or faces
(`Surface::validate`, [ADR 0168](0168-terrain-import-and-surfaces.md) Context), or a result
over the document limits, is refused with the count and the limit named, element by element
(the element is not imported; the rest are).

**Inspectable.** Each converted body records its provenance as properties: `ifc.Class`,
`ifc.GlobalId`, `ifc.Step` (the `#id` of the instance), `ifc.Representation` (the item type
converted, for example `IfcExtrudedAreaSolid`) and `ifc.Conversion` (`tessellated` or
`booleanApplied` and so on). The conversion is lossy in one direction only: nothing in the
document claims the geometry is the original parametric definition.

### 3.4 Property sets and quantities

- A property becomes `<Pset name>.<Property name>` (for example `Pset_WallCommon.FireRating`).
  An `IfcElementQuantity` becomes `<Qto name>.<Quantity name>`. IFC2x3's `BaseQuantities`
  keeps that name.
- Value types. `IfcPropertySingleValue` with a label, identifier, text, boolean, logical
  (`TRUE` and `FALSE`; `UNKNOWN` is omitted and counted), integer or real value becomes a
  string, bool or double. `IfcPropertyEnumeratedValue` becomes a string (the values joined
  with `, `). `IfcPropertyBoundedValue` becomes two properties (`.Lower`, `.Upper`).
  `IfcPropertyListValue` becomes a joined string. `IfcComplexProperty`,
  `IfcPropertyReferenceValue` and `IfcPropertyTableValue` are not mapped and are counted by
  class. `IfcQuantityLength`, `Area`, `Volume`, `Count`, `Weight` and `Time` become doubles.
- Units of numbers. Length, area and volume measures are converted to **metres, square metres
  and cubic metres** using the project's units; area and volume use the project's area and
  volume units, **not** a function of the length unit (the Simple-Scene has millimetre length,
  m² area and m³ volume, so an area quantity is already in m²). Every other measure type is
  stored as the source number and its unit is listed in the report's `unitsUnconverted`
  table; no claim is made about it.
- Escapes and encoding. STEP string escapes (`''`, `\\`, `\X\`, `\X2\…\X0\`, `\X4\…\X0\`, `\S\`,
  `\P` code pages) are decoded. ISO 10303-21 is ASCII, but real files carry raw bytes: a UTF-8
  byte-order mark is skipped; raw bytes of 0x80 and above are accepted as UTF-8 when valid and,
  when not, decoded as ISO 8859-1, each with a notice and a count (`rawNonAscii`,
  `latin1Fallback`), so a name is never blanked; text with an invalid escape or a NUL is refused
  for that value and counted.
- Caps. At most 128 properties per body (the native limit). Up to 16 slots are **reserved** for
  the `ifc.*` provenance properties (class, GlobalId, step, representation, conversion, type,
  name, layer, material, groups, classification and so on), which are written first; occurrence-level properties come
  next, then type-level, each in pset-name then property-name order; the overflow is dropped
  and counted per pset in `propertiesDropped`. Two keys that collide after shortening keep the
  first and count the second. Property keys over 128 bytes and
  strings over 2,048 bytes are shortened or dropped with counts. In aggregate, imported
  property text is bounded to **8 MiB**, and further by what the model chunk has left: ADR 0168
  measured about 200 bytes of JSON per triangle, so a 100,000-face model is about 20 MB of the
  32 MiB chunk and a full property load could otherwise fail on Save. The importer measures the
  serialized document; while it is over `min(8 MiB, chunk − geometry − 2 MiB)` it drops
  properties (type-level before occurrence-level, quantities before psets) and counts them. If
  the geometry and provenance alone exceed the chunk, the import is refused with the counts.
- Provenance properties (`ifc.*`) are the only non-IFC-named keys the importer writes; they
  count toward the 128.

### 3.5 Units

IFC carries its units, so unlike OBJ the importer does not ask. `IfcProject.UnitsInContext`
gives the length unit: `IfcSIUnit` (`LENGTHUNIT`, with an optional SI prefix such as
`.MILLI.`) or `IfcConversionBasedUnit` (inch, foot, with the factor read from its
`IfcMeasureWithUnit` chain, at most four levels). The result is `metresPerUnit` and whether it
is exact, as in the DXF report. Plane angle accepts radian and degree (conversion-based).
Any other length or plane-angle unit is refused. **A file with no length unit is refused**
(I did not verify whether each schema makes `UnitsInContext` optional; either way, guessing
would break the no-silent-scale rule that ADR 0168 and the DXF importer already follow); the user can supply
the length unit as an explicit override (`mm`, `cm`, `m`, `in`, `ft`), reported as an
override. All native coordinates are metres. Area and volume units are read for quantity
properties as described above; conversion-based area and volume units (square or cubic feet)
use the same factor chain. `IfcPropertySingleValue.Unit` and `IfcPhysicalSimpleQuantity.Unit`
override the project unit for that value: a recognised override (SI or a conversion-based unit
of the same dimension) is applied, and an unrecognised one stores the number as written and is
listed in `unitsUnconverted`, so no value is converted by assumption. A conversion factor
within 1e-6 relative of a canonical value (inch 0.0254 m, foot 0.3048 m, yard 0.9144 m, US
survey foot 1200/3937 m, degree π/180) is normalised to it with a notice; any other factor is
used as written and `metresPerUnitExact` is false. (The Reference View degree sample states
0.017453293, 2.7e-8 off π/180.) Mixed unit claims (several `IfcUnitAssignment`s in nested
contexts) are refused.

### 3.6 Georeferencing

Import reads `IfcMapConversion` and `IfcProjectedCRS` (IFC4 and 4.3) from the `Model`
context. The formula in the buildingSMART documentation is scale, then an anticlockwise
rotation about Z by `atan2(XAxisOrdinate, XAxisAbscissa)`, then translation by Eastings,
Northings and OrthogonalHeight. `IfcMapConversionScaled` (4.3) adds per-axis factors. IFC2x3
has no such entities; a community convention puts `ePset_MapConversion` and
`ePset_ProjectedCRS` on the `IfcSite`, but it is not standardised, so IFC2x3
georeferencing is **not claimed**, and a file that has those psets gets a notice that they
were read as ordinary properties.

The mapping to the R095 site origin is exact in one case:

| Condition | Result |
| --- | --- |
| θ = 0 (`XAxisAbscissa` > 0 and `XAxisOrdinate` = 0 within 1e-12, or both absent), no per-axis factors, the `IfcProjectedCRS` `MapUnit` absent or metre, the context's `WorldCoordinateSystem` the identity, exactly one `Model` context and one map conversion, and `Scale` (default 1) equal to `metresPerUnit` within 1e-9 relative | The document site origin is set: easting, northing and elevation are Eastings, Northings and OrthogonalHeight; the CRS label is the `IfcProjectedCRS.Name` (shortened to 64 bytes); provenance is `IfcMapConversion` plus the file's SHA-256. The document then needs schema 29 and **opens only in SketchyUp 1.3.0 or later**; the report says so |
| θ ≠ 0, a factor, scale or map-unit mismatch (for example a State Plane CRS in US survey feet), several contexts or conversions | Not representable by a translation-only site origin; the report gives the reason. Decision 6 |
| A non-identity `WorldCoordinateSystem` on the `Model` context | The **file is refused**, naming the context: applying or ignoring it would both be a guess, and neither fixture nor oracle covers it. A later slice may apply it |

Decision 6 offers three ways to handle a rotation: (a) keep the project axes, set no site
origin and report the whole conversion (and also record it as read-only `ifc.mapConversion.*`
properties on the project group); (b) an opt-in "grid" mode that rotates the imported model
by +θ about Z (the conversion's own rotation, so that +X is east), then sets the translation-only site origin, which is exact
for site coordinates but turns the building's walls off the model axes; (c) give the site
origin a grid rotation (and perhaps a scale), which is the faithful answer but changes R095's
core (re-anchor, `site.convert`, DXF site coordinates, Site settings, schema). The
recommendation is (a) as the default with (b) as an explicit option and (c) as a separate
later row. With the fixture buildingSMART certifies against (60 degrees, scale 0.001, mm
units), (a) imports with no site origin and a complete report, and (b) with the origin set.

Whichever way, `IfcSite` `RefLatitude` and `RefLongitude` (compound lists of degrees, minutes,
seconds and millionths of seconds) are converted to decimal degrees and kept with
`RefElevation` as properties (`ifc.RefLatitude` and so on), and a context's `TrueNorth` is kept
as `ifc.TrueNorth` (degrees from the Y axis) with a notice; the solar settings are not
changed, matching ADR 0168's out-of-scope ("deriving latitude/longitude from the site
origin"). Coordinates beyond ±1e6 m in the local frame (a model authored in absolute
projected coordinates) are refused with the extent stated; shifting them and setting a site
origin from the extent, as R095's terrain import does, is decision 7.

Export writes `IfcProjectedCRS` (the CRS label) and an `IfcMapConversion` (Eastings,
Northings and OrthogonalHeight from the site origin, axis (1, 0), `Scale` 1 against the
metre length unit) when the document has a site origin **and a CRS label that looks like an authority code**
(`EPSG:32760` form: letters, a colon, an identifier), since `IfcProjectedCRS.Name` is read by
consumers as that identifier. The label is free text in the document (ADR 0168), so a label
that does not match, or no label, means nothing is written and the report says why. A rotation is never exported, because the document has none.

### 3.7 Materials and colours

- **Colour.** Priority: the `IfcStyledItem` on the representation item
  (`IfcSurfaceStyle`, `IfcSurfaceStyleRendering.SurfaceColour`, with `Transparency` as
  1 − opacity; `DiffuseColour` if given) then the style of the associated `IfcMaterial`
  (`IfcMaterialDefinitionRepresentation`) then none. Per-face colours from
  `IfcIndexedColourMap` fill `Body.faceColors`. IFC does not define a colour space; values
  are treated as sRGB 0–1 and the report says so.
- **Materials.** An `IfcMaterial` becomes a native material of that name (shortened and made
  unique with a suffix, with a notice, as ADR 0110 does for glTF) carrying the colour and
  opacity. `IfcMaterialLayerSet`, `IfcMaterialLayerSetUsage`, `IfcMaterialConstituentSet`
  and `IfcMaterialProfileSet` are not native concepts: the element gets the name of the set
  as `ifc.Material` and the layer or constituent summary (`name:thickness`, joined) as
  `ifc.MaterialLayers`, with a notice that the layering is descriptive only.
- **Not imported:** textures (`IfcImageTexture` and the like), shininess, reflectance,
  lighting-model parameters and `IfcSurfaceStyleShading` beyond colour. Counted as losses.

### 3.8 Export (IFC4)

| Native | IFC4 |
| --- | --- |
| Document | `IfcProject` with a 3D `Model` context (`Precision` 1E-7, the native tolerance) and a `Body` sub-context (shape representations use identifier `Body` and type `Tessellation`); units: metre, square metre, cubic metre, radian; one `IfcOwnerHistory` and `IfcApplication` (SketchyUp and its version); no unit option in R097 |
| Spatial structure | If the document came from IFC, groups with `ifc.Class` of `IfcSite`, `IfcBuilding` or `IfcBuildingStorey` are written as those; otherwise one site and one building are synthesized and **no storey is invented**. Elements are contained in the building (or the nearest spatial ancestor) with `IfcRelContainedInSpatialStructure` |
| Group with children | `IfcElementAssembly` and `IfcRelAggregates` |
| Geometry body | One `IfcTriangulatedFaceSet` (native tessellation; `IfcCartesianPointList3D`, 1-based `CoordIndex`); `Closed` set only when the native solid check passes, otherwise omitted; class from `ifc.Class` when it is a supported element class, else `IfcBuildingElementProxy` with `ObjectType` from the body name. Placement is `IfcLocalPlacement` chained like the group tree |
| Component instance | Expanded geometry per placement in R097; `IfcMappedItem` (instancing) is later. The report states the expansion count |
| Colour and material | A native material becomes an `IfcMaterial` plus `IfcSurfaceStyle`/`IfcSurfaceStyleRendering` on an `IfcStyledItem` of the face set; per-face colours use `IfcIndexedColourMap`. Textures are not exported (counted) |
| Properties | `Pset.Prop` keys become `IfcPropertySet`s of `IfcPropertySingleValue` (`IfcLabel`, `IfcReal`, `IfcBoolean`); keys without a dot go into a custom set named `SketchyUp`; `ifc.*` keys drive attributes (`GlobalId`, `Name`, `PredefinedType`) and are not re-emitted as properties. Numbers are written as `IfcReal`, so no unit semantics survive; this is stated |
| Hidden bodies and tags | Exported (the OBJ rule, [ADR 0115](0115-native-obj-interchange-workflow.md)), and assigned to an `IfcPresentationLayerWithStyle` with `LayerOn` false |
| Site origin and CRS label | `IfcProjectedCRS` and `IfcMapConversion` as in 3.6 |
| Not exported, counted | Wires, edges, curves, guides, text, dimensions and annotations, sections, scenes, sheets, reference images, terrain (tiles are ordinary bodies but the terrain record is dropped and the terrain is **omitted** in R097), textures, component parameters, edge appearance and smoothing |

`GlobalId` (22-character compressed GUID, characters `0-9A-Za-z_$`). A body that was imported
keeps its `ifc.GlobalId` **if it is valid and not already used by another body in this
export**; a copy of an imported body carries the same property, so duplicates and invalid
values get a derived identifier instead and are counted (`globalIdReplaced`). Others get a
deterministic identifier from the SHA-256 of a fixed prefix, the **document identity** and the
body's stable ID. The document identity (`Document::identity()`) is a random 128-bit value
that the native format already stores (`documentId`) and keeps across Save and reopen, so
re-exporting the same document keeps its GUIDs without the export editing the document
(decision 16). Exporting twice gives the same file when the timestamp in `FILE_NAME` and the
owner history is injected, as `SOURCE_DATE_EPOCH` is for packages.

Numbers are written as the shortest round-trip form of the double, in STEP REAL syntax. Text
is escaped with `\X2\`/`\X4\` for non-ASCII characters. The writer streams entity lines to
the destination through the non-replacing publisher the other exporters use
([ADR 0114](0114-contained-obj-export-packages.md) and the DXF/STL exporters).

## 4. Fidelity report and limits

### Report shape

The report is JSON in the existing style, `apiVersion` 1, `format` `IFC`, and a `direction`.
Messages never quote more than 64 bytes of user text; the whole report is at most 64 KiB,
64 kinds, 32 locations per kind and 256 bytes per message (the terrain report's bounds,
[ADR 0168 §1](0168-terrain-import-and-surfaces.md#invalid-input-and-bounded-reports)).
Counts are complete even when locations are truncated. It contains no host paths.

```json
{
  "apiVersion": 1, "format": "IFC", "direction": "import",
  "source": {"sha256": "…", "bytes": 142325, "schema": "IFC4",
             "viewDefinition": "ReferenceView_V1.2", "application": "…"},
  "units": {"lengthUnit": "MILLIMETRE", "metresPerUnit": 0.001, "metresPerUnitExact": true,
            "override": false, "planeAngle": "RADIAN", "areaUnit": "SQUARE_METRE",
            "volumeUnit": "CUBIC_METRE"},
  "georeference": {"mapConversion": {"eastings": 729013.3488297004, "northings": 9063992.684697364,
                    "orthogonalHeight": 1.3, "rotationDegrees": 60.0, "scale": 0.001},
                   "crs": "EPSG:32760", "result": "notRepresentable", "mode": "keepAxes"},
  "spatial": {"projects": 1, "sites": 2, "buildings": 1, "storeys": 1, "spaces": 2},
  "elements": {"imported": {"IfcWall": 4, "IfcSlab": 3},
               "importedGeneric": {"IfcRoof": 1},
               "notImported": [{"class": "IfcAlignment", "reason": "unsupportedEntity", "count": 2}]},
  "geometry": {"bodies": 12, "vertices": 0, "faces": 0, "definitions": 0, "instances": 0,
               "byRepresentation": {"IfcTriangulatedFaceSet": 12, "IfcExtrudedAreaSolid": 2},
               "segmentsPerCircle": 96, "maximumChordDeviationMetres": 0.0,
               "openingsApplied": 0, "openingsNotApplied": 0},
  "properties": {"psets": 2, "properties": 6, "quantities": 25, "dropped": {}, "bytes": 0},
  "materials": {"materials": 8, "styledItems": 14, "layerSetsDescribed": 0},
  "accounting": {"entities": 371, "converted": 0, "supporting": 0, "omitted": 0},
  "omittedEntities": {"IfcZone": 1},
  "losses": {"textures": 0, "materialLayering": 1},
  "notices": [{"code": "georeferenceRotation", "count": 1, "message": "…"}],
  "locations": [{"kind": "notImported", "step": "#1234", "globalId": "…", "message": "…"}]
}
```

(The numbers in the example are illustrative; the schema, not the values, is the proposal.)

### The accounting identity

The acceptance property that makes "never silently dropped" testable: every entity instance
in the file is in exactly one of `converted` (it produced native content), `supporting`
(consumed as data of a converted entity: points, directions, placements, profiles, units,
owner history), or `omitted` (counted by class in `omittedEntities`). The three sum to
`accounting.entities`, which equals the number of instances the reader found. `omitted` is
everything left over, counted by class in `omittedEntities`: accountable instances that were not
converted (products, relationships, property definitions, materials, styles, representation
items), and also non-accountable ones that only an omitted element referenced (a point or a
profile of a skipped alignment) or that nothing referenced. An entity whose class is missing
from the reader's tables is counted under its own name (`IfcXyz`) and is never skipped. `omittedEntities` lists at most 128 kinds, with an `other` bucket past
that, as in the DXF report.

### Refusals

Refused with nothing created: size, line, entity, depth or list limits; a malformed header or
`FILE_SCHEMA`; a missing or unsupported length unit (without an override); a duplicate
instance ID; a reference to an undefined instance in a position the subset needs (an
undefined reference elsewhere is a notice and a skipped element); a non-identity world
coordinate system on the `Model` context; coordinates beyond ±1e6 m; and the document limits
for the converted whole, named with counts. Per-element failures never refuse the import:
the element is not imported and is located in the report.

## 5. Interaction surface (high level)

This section states what the workflows must provide, not how they look. **The layout of the
dialogs needs a UX design that the owner approves** (R097's UX column is empty; decision 15).

- **Desktop.** File → Import IFC and File → Export IFC. Import offers: the source, the
  length-unit override (only when the file has none), segments per circle, the
  georeference mode, and whether spaces are imported hidden, shown or skipped; it opens a
  **new unsaved document** with a fresh recovery context (decision 10). Export offers the
  scope notice (hidden geometry included; nothing clipped; what is not exported) and a
  destination that cannot replace an existing file. Both show the report (the conversion
  sheet used for the other importers) with Copy and Save. Cancel and progress are present
  from the first slice (the D-044 lesson: ADR 0168's import added progress and cancel late).
- **CLI.** `sketchyup-cli --import-ifc FILE [--output NEW.sketchyup]` with
  `--ifc-segments 12..256`, `--ifc-length-unit mm|cm|m|in|ft` (override), `--ifc-georef
  keep|grid` and `--ifc-spaces hidden|shown|skip`; without `--output` it validates and
  prints the report. `--export-ifc NEW.ifc --input doc.sketchyup` with an optional
  `--ifc-timestamp` for reproducible output. The option-isolation, non-replacement and
  mixed-mode rules of [ADR 0123](0123-native-dxf-interchange-workflow.md) and
  [0115](0115-native-obj-interchange-workflow.md) apply: unrelated options reject before
  publication, and an existing file or symlink is never replaced.
- **Automation (MCP) and the assistant.** The glTF, OBJ, STL and DXF importers are not
  `noun.verb` commands, and neither are their exporters. IFC files are routinely larger than
  the 16 MiB inline-data bound that `asset.import` and `terrain.import` carry (22,369,624
  base64 characters), so an MCP import would need a different design. **R097 adds no
  commands, no queries and no assistant tools.** Imported content is visible through existing
  queries (`ifc.*` properties are ordinary entity properties). Following ADR 0169 decision 9
  and ADR 0168 decision 10: the assistant's tool surface and its pinned baseline do not
  change; exposing any IFC command to it later needs a measured token cost and a **new corpus
  version** (v6) before it ships, and MCP exposure needs its own bounded-data design. Decision 13.
- **Classes on native bodies.** There is no IFC-class UI in R097. A user or script sets
  `ifc.Class` and `ifc.PredefinedType` through the existing entity-properties command, and
  export reads them. Anything else exports as `IfcBuildingElementProxy`.

## 6. Verification plan

### 6.1 Fixtures

All third-party data is **untrusted input** and is never executed. Fixtures live under
`tests/fixtures/ifc/` with a `NOTICE.md` holding attribution; none is installed in the
package.

| Fixture | Source and licence | Purpose |
| --- | --- | --- |
| `bsi-ifc2x3-arch.ifc`, `bsi-ifc4-arch.ifc`, `bsi-ifc43-arch.ifc` (92,542, 142,325 and 141,965 bytes) | buildingSMART International, [Certification-datasets](https://github.com/buildingSMART/Certification-datasets) `Simple-Scene/Building-Architecture.ifc`, repository commit `80d976a9` (2026-09-04). **CC BY 4.0**, from the repository's `LICENSE` ("(C) buildingSMART International Ltd. … Creative Commons Attribution 4.0"). SHA-256 values in the appendix | The three-schema certification set: hierarchy (1 project, 1 building, 1 storey, 2 spaces, 4 walls, 3 slabs, 1 roof), faceted breps in 2x3 versus tessellated sets in IFC4 and 4.3, psets and quantity sets, material association, colours, classification reference, and the 60 degree map conversion |
| `bsi-ifc4-landscaping.ifc`, `bsi-ifc4-road.ifc` (1,074,044 and 277,133 bytes) | Same repository and licence | Limits and refusal (a mid-size file); reporting of what the subset does not cover |
| `rv-wall-with-opening-and-window.ifc`, `rv-basin-tessellation.ifc`, `rv-column-straight-rectangle-tessellation.ifc`, `rv-tessellated-item.ifc`, `rv-tessellation-with-individual-colors.ifc` (12,492, 12,131, 4,594, 5,199 and 2,593 bytes) | Same repository, folder `ISO Spec - ReferenceView_V1.2`, CC BY 4.0 by the repository licence. The file headers name another author (RDF Ltd., 2011) | Wall with opening and window (extrusions, `IfcRelVoidsElement`, `IfcRelFillsElement`), tessellation, per-face colours. **Decision 11**: include only if the owner accepts the repository-level licence as covering them; otherwise write equivalent originals |
| Original fixtures (MIT, generated by the test sources or hand-written P21): `units-mm.ifc`, `units-m.ifc`, `units-ft.ifc`, `units-missing.ifc`, `mapconv-zero-rotation.ifc`, `mapconv-60deg-scale.ifc`, `extrusion-profiles.ifc`, `revolve-and-boolean.ifc`, `mapped-items.ifc`, `openings.ifc` (closed and open hosts), `psets-types-escapes.ifc`, `unsupported-all.ifc` (an advanced brep, a swept disk, a CSG solid, an alignment, an unsupported placement, a cycle in placements), `far-from-origin.ifc`, `limits-*.ifc` generators | The project, MIT ([ADR 0001](0001-baseline.md): original examples) | Every claim not covered by buildingSMART's set, and every refusal |
| `malformed/*.ifc` and fuzz seeds | The project | Truncation, a missing `ENDSEC`, unterminated string, bad escapes, deep nesting, a huge list, duplicate IDs, forward references, 1e308 coordinates, NaN text, a 128 MiB + 1 byte file |

**CC BY 4.0 duties.** `NOTICE.md` names the author and source of each file, links the licence,
states that the files are redistributed **unmodified** (any change is indicated), and carries
the IFC trademark credit. The source archive redistributes the fixtures, which CC BY 4.0
permits with attribution; the file headers name third-party products and, for the Reference
View set, RDF Ltd. (2011).

Community sample files are excluded: buildingSMART's own README says most "do not pass
validity check", and their licence is per contributor.

**Independent oracle.** The expected values (entity counts per class, attribute decodes for
every accountable entity, hierarchy by `GlobalId`, property values, volumes and areas of
extrusions and tessellated sets, and the schema validity of every export) come from
IfcOpenShell's Python package, regenerated by a script that is optional tooling, as ezdxf
is for DXF ([ADR 0120](0120-bounded-planar-dxf-source.md)):
`tests/ifc-oracle-requirements.txt` would pin the version (0.9.0.post1 is the PyPI release
today), the checked-in `oracle/*.json` hold the results, and CI and the package need no
Python package. Because CI does not run the oracle, export regressions after acceptance are
caught by **checked-in golden exports** (G6); regenerating them requires an oracle run.
IfcOpenShell's validator is not buildingSMART's validation service (which also checks view
rules); at acceptance the service is run by hand on the golden exports if the network allows,
and its result is recorded, not made a CI gate. The oracle is LGPL-3.0 but is only run by a developer and its output is
data. This also verifies the hand-written attribute tables: a table error shows up as a
mismatch with the oracle on the three certification files.

### 6.2 Checks for the Verify line

| Verify clause | Check |
| --- | --- |
| Named fixtures preserve claimed units | A1: every vertex of the mm, m and ft fixtures is within 1e-9 m of `source × metresPerUnit` (exact for m); the report states `metresPerUnit`, exactness and any override. A2: quantity values equal the oracle's with the project's area and volume units (7 area, 7 volume and 11 length quantities in the IFC4 Architecture file). A3: a missing length unit is refused, and accepted with the override, reported as one. A4: a non-SI, non-inch, non-foot length unit is refused; A5: a property or quantity with its own `Unit` is converted or listed unconverted, and a conversion-based area or volume unit converts |
| Named fixtures preserve claimed hierarchy | B1: on the three Architecture files and the Reference View wall, group tree = oracle's tree by `GlobalId` (project, site, building, storey, containment, aggregation), including both IFC4 sites; B2: counts per class equal the oracle's; B3: the element order within a parent follows the file; B4: `ifc.Groups` equals the oracle's group, system and zone membership; B5: tags per class; B6: duplicate `GlobalId` in a file, two parents and a containment cycle are noticed or reported as specified; B7: a layer with `LayerOn` false imports its bodies hidden |
| Named fixtures preserve claimed properties | C1: every `Pset.Property` and `Qto.Quantity` equals the oracle (strings, booleans, doubles) with type-level properties copied to occurrences unless overridden; C2: `\X2\` and `\S\` escapes decode; C3: the 128-per-body cap (provenance reserved), the 8 MiB aggregate cap and the chunk-remainder cap drop and count, deterministically, and a document whose geometry alone exceeds the chunk is refused; C4: `ifc.GlobalId`, `ifc.Class`, `ifc.Step`, `ifc.Representation` on every body; C5: the classification reference is retained with its URI |
| Unsupported entities are reported | D1: the accounting identity holds on every fixture (`converted + supporting + omitted = entities`); D2: `unsupported-all.ifc` lists each unsupported class, with at most 32 locations and complete counts; D3: an element with an unsupported placement, a cycle or an unsupported representation is not imported and is located; **nothing is imported at the origin**; D4: a reference to an undefined ID is a notice, not a crash |
| Converted geometry remains inspectable | E1: the whole document passes native validation (`decodeDocument(encodeDocument())`) and a save and reopen is bit-exact; E2: areas and volumes of tessellated sets and of polygonal-profile extrusions equal the oracle's to 1e-6 relative; a circular profile's area equals the inscribed polygon's analytic area at the chosen segment count to 1e-9 and is within the reported chord deviation of the true circle; E3: openings subtract the opening volume from the host (the Reference View wall's volume equals the oracle's) and a failed pair leaves the host uncut with the opening hidden on its tag and located in the report; E4: arc chord deviation is within the reported maximum; E5: a body is selectable, editable and undoable like any other (one edit makes the whole import one undo step); E6: each body's provenance properties resolve to the right source entity |
| Georeferencing (claimed only as far as shown) | F1: θ = 0 and matching scale set the site origin to the E, N and H within 1e-9 m and the label; the document is schema 29; F2: the 60 degree fixture imports with `keepAxes` and no site origin, with the full conversion in the report; with `grid`, each vertex equals the oracle's mapped coordinate minus the translation to within 1e-9 m; F3: a model beyond ±1e6 m is refused naming the extent; F4: a `MapUnit` of US survey foot, a non-identity `WorldCoordinateSystem` and two map conversions are each handled as specified (reported, or the file refused) |
| Export is demonstrated | G1: the independent oracle opens every export; **schema validation reports no errors** for IFC4; entity, hierarchy, property, unit and georeference counts equal the document's; tessellated areas and volumes agree within 1e-6 relative; G2: an export of the glTF, OBJ and DXF import fixtures and of a document built from commands re-imports to the same bodies, vertex coordinates within 1e-9 m, hierarchy and properties; G3: two exports of one document with a fixed timestamp are byte-identical; G4: the report counts every non-exported kind; G5: a CRS label of authority-code form is required for `IfcMapConversion`, and its absence or a free-text label is reported; G6: golden exports are checked in and CI compares the exporter's bytes with them; G7: duplicate and invalid `ifc.GlobalId` values (a copied body) get derived identifiers and are counted |
| Determinism | H1: two imports of every fixture give identical encodings; H2: import of an input with its instances shuffled in file order gives the same document except for `ifc.Step` and the order that file order defines (B3) |
| Limits and refusal | I1: each file-level limit (128 MiB + 1, 2,000,001 entities, an instance ID above 2^32 - 1, depth, list, record) refuses with the count and the limit named, creating no document (slice a); each geometry limit (100,001 faces, 10,001 bodies, 1,025 materials, expansion and Boolean bounds) refuses or reports per element as specified (slices b and c); I3: a sparse ID such as `#4000000000` costs one index entry, not a dense array; I2: messages ≤ 256 bytes; the report ≤ 64 KiB |
| Parsers | J1: a `parser_fuzz_ifc_step` target (the P21 reader and entity layouts; slice a) and a `parser_fuzz_ifc_import` target (bounded conversion dry run over accepted input, which must not throw for bad data, must keep the report within its bounds and must produce a document that decodes; created in slice b and extended in c and d), with seeds from the generated fixtures; J2: `parser_fault_corpus_tests` mutates the certification files 512 times each; J3: ASan/UBSan campaigns of at least five minutes per target, run at each slice that changes a target and again at R097.g; at [PARSER_FUZZING](../PARSER_FUZZING.md) settings; J4: the harness runs every input twice and compares; J5: text encoding: raw UTF-8, invalid UTF-8 (decoded as Latin-1 with a notice), a BOM and `\X2\` escapes in names are handled as specified |
| Support claims | K1: the support matrix row, user guide section and the exchange-boundaries table state exactly the claimed schemas and directions, and name the unsupported classes as listed in the report; K2: `THIRD_PARTY_NOTICES` is unchanged (nothing new is linked) and the fixture `NOTICE.md` carries the CC BY 4.0 attribution |

### 6.3 Budgets

Proposed; to be confirmed by a measurement layer (as R095's were) before they are frozen.
They are measured on the reference machine with software rendering, in the pinned image.

| Limit | Value | Reason |
| --- | --- | --- |
| File size | 128 MiB | The glTF package bound (ADR 0109); text formats are 64 MiB, but IFC carries property data, and the 100k-face model limit already caps the geometry that can be kept |
| Entity instances | 2,000,000 | A bound on index memory, not a proxy for file size: bytes per instance differ by about an order of magnitude (45 in the faceted-brep IFC2x3 Architecture file, 92,542 bytes for 2,046 instances; 380 in the tessellated IFC4 file, 142,325 bytes for 371, because coordinate lists are single large records) |
| Single record | 16 MiB; list length 1,000,000; nesting depth 32; string 64 KiB after decoding | `IfcCartesianPointList3D` holds all of a mesh's coordinates in one record |
| Distinct entity types | 1,024; reference chains (placements, units) 64 deep | |
| Profile vertices; Boolean and mapped-item bounds | 10,000 vertices per profile; Boolean tree depth 64 and 2,000 operations per import; mapped-item nesting 8 | The R055 adapter's cost. These are first estimates (Revit-style clipping chains and window-heavy buildings may need more); the over-limit policy is per element: not imported, reported |
| Per-body size | 100,000 vertices and faces | The native body limit |
| Materials, tags | 1,024 native materials (names unique); at most 128 IFC class tags | A body past the material limit keeps its own colour and the report counts `materialsOverLimit` |
| Instance IDs | At most 2^32 - 1, sparse index | |
| Geometry after conversion | The native document limits (100,000 vertices and faces, 10,000 body records, 600,000 faces materialized) | A larger file is refused with the count and the limit, or, element by element, imported partially and reported; **there is no IFC-specific widening of the modelling limits** |
| Properties | 128 per body; 8 MiB aggregate | The model chunk is 32 MiB |
| Report | 64 KiB, 64 kinds, 32 locations per kind, 256-byte messages | The terrain report |
| Parse and index a 64 MiB, about 1,000,000 entity IFC4 file | ≤ 3 s (a 128 MiB, 2,000,000-entity file ≤ 6 s), with progress and cancel | R095 parse and triangulate at 250k was 0.36–0.50 s; this is a looser first target |
| Interpret, convert and commit (the 100,000-face, 10,000-body model) | ≤ 10 s, with progress; cancel latency ≤ 100 ms | ADR 0168 D-044: cancel stopped within 55 ms at 250k |
| Refusal of an over-limit file | ≤ 1 s | The header, size and the entity count are known after the indexing pass |
| Peak resident memory during import, at the limits | ≤ 1 GiB | The [R082.w v2 budget](../verification/R082w-memory-budget-v2.md) is 1 GiB for the supported 100k families; the reader holds the file (≤ 128 MiB) plus an offset index (about 32 bytes per entity: ≤ 64 MiB) and does not materialise arguments it does not interpret |
| Added resident memory after import | Within the existing document budget; no IFC-specific cache is retained | |
| Export of the 100,000-face model | ≤ 5 s, with progress and cancel; output ≤ 256 MiB; peak ≤ 512 MiB (streamed) | The GLB exporter's bound is also 256 MiB |

Native tests follow the established matrix: an `ifc_import_input_tests` and an
`ifc_export_input_tests` suite on X11 and Wayland at 1× and 2×, with accessibility and
keyboard-reachability extensions for the dialogs.

## 7. Out of scope

For R097: `.ifczip`, `.ifcXML`, `.ifcJSON` and IFC5/IFCX; IFC4X1, IFC4X2 and 4.3 release
candidates; 4.3 infrastructure (alignments, linear placement, rail, road, bridge, tunnel,
sectioned solids); advanced breps, NURBS and CSG; parametric profiles beyond the four in 3.3
until a later slice adds them; swept disks and other swept solids until a later slice;
PROJ or any CRS transform; geographic CRS (`IfcGeographicCRS`-like) input; IFC2x3
georeferencing; export to IFC2x3 or IFC4.3; terrain and site-surface export, and conversion
of an `IfcSite` surface into a terrain record; export of instancing, textures, annotations,
sections, sheets, component parameters or curves; classification and class editing UI;
parametric doors and windows; IDS, BCF, bSDD, IfcDiff or clash tools; schedule, cost and 4D
data; round-trip identity with other tools; merging an IFC file into the open document;
referencing an IFC file as a live link; partial import by storey or class (a later row if the
model limits prove tight); IFC2x3 export by the faceted-brep route.

## 8. Risks

- **The in-house geometry is the work.** Extrusions with arbitrary profiles, half-space
  clipping, Boolean results and openings over real exporter output (Revit, ArchiCAD) vary
  more than the buildingSMART samples do. The fixtures here prove the claimed subset, not
  every exporter. The report is designed to make the failures visible.
- **Attribute tables.** Hand-written layouts for three schemas can contain errors.
  Mitigation: the oracle comparison on real files (checks A–C) and the rule that an
  unknown class is counted by name, never skipped.
- **Model limits are small for BIM.** 100,000 faces and 10,000 body records are below many
  real buildings. The Simple-Scene files are small; a typical project file will refuse or
  import partially. Honest refusal is the design; partial import by storey or class is the
  likely next row.
- **Openings.** The R055 Boolean adapter requires valid closed solids and refuses the rest;
  many exporter solids are open or self-touching, so `openingsNotApplied` could be common.
- **The 60 degree map conversion** (the certified fixture) is not representable by the
  current site origin. Decision 6 chooses how far to go; without a rotation the georeference
  claim is partial.
- **Schema 29.** A document with a site origin is unreadable by SketchyUp 1.2.x and earlier.
  The report and the user guide say so; exporting does not require it.
- **Property volume.** A full property load can exceed the 32 MiB model chunk. The 8 MiB
  aggregate cap and the serialized-size check are the guard; the cap may need tuning.
- **Licence of the schema files** (CC BY-ND 4.0 with a software-development permission, see
  section 1) leaves generating code from EXPRESS unclear. The recommendation avoids it; the
  owner should confirm. IFC trademarks need a credit.
- **Fixture licence.** The repository `LICENSE` is CC BY 4.0, but the Reference View files
  carry a different author in their headers. Decision 11.
- **Colour space.** IFC does not define one; treating values as sRGB may shift appearance.
- **Maintenance.** IFC evolves (4.3 and IFC5). An in-house reader needs periodic
  re-verification against the oracle; the schema list in 2 is deliberately short.
- **Time zones and determinism.** `FILE_NAME` and owner history carry timestamps; reproducible
  output needs an injectable time. Decision 16 covers GUID stability.
- **Weight of the fallback.** If B fails, A2 adds about 10 MB of shared objects, a Boost
  build dependency, LGPL packaging and a 6.4-minute (at `-j4`) addition to every package build.

## Decisions (proposed)

Proposed for the owner. Each has a recommendation; none is decided.

1. **Library:** an in-house bounded STEP-P21 reader and writer, with IfcOpenShell only as a
   development-time oracle.
2. **Schemas:** import IFC2x3 TC1, IFC4 ADD2 TC1 and IFC4.3 ADD2 (building subset); export
   IFC4 ADD2 TC1 only, tessellated, with no view or certification claim.
3. **Claims:** each direction is claimed only when its checks pass; import may ship without
   export.
4. **Export geometry:** tessellated `IfcTriangulatedFaceSet` only; no faceted brep, no
   reconstruction of parametric solids.
5. **Import geometry:** tessellate everything into editable faces; extrusions, revolutions,
   Booleans and mapped items in R097.c; openings are subtracted when both solids are closed,
   else reported with the host uncut and the opening hidden.
6. **Georeferencing with a rotated or scaled map conversion:** keep the project axes and
   report (default), with an opt-in grid-aligned mode; a site rotation is a separate row.
7. **Models beyond ±1e6 m:** refuse with the extent stated; shifting (with or without setting
   a site origin) is a later option.
8. **Spaces and tags:** spaces on an `IFC Space` tag that starts hidden; one tag per IFC class
   family; layers as properties.
9. **Properties:** flat `Pset.Property` keys, type-level copied to occurrences, `ifc.*`
   provenance, length, area and volume converted to SI, everything else stored as source
   numbers and reported; 128 per body and 8 MiB aggregate.
10. **Import target:** a new unsaved document, like the other importers.
11. **Fixtures:** the buildingSMART certification datasets under CC BY 4.0, with attribution;
    the Reference View samples only if the owner accepts the licence reading; no community
    samples.
12. **Oracle:** IfcOpenShell's Python package as optional regeneration tooling, results
    checked in; not shipped, not linked.
13. **Assistant and MCP:** no new commands, queries or assistant tools; assistant exposure
    needs a new corpus version.
14. **Budgets:** adopt the table in 6.3, to be confirmed by measurement before they are
    frozen.
15. **Desktop UX:** commission and approve a UX design before R097.f starts (best commissioned
    once R097.b lands and the report is real).
16. **GlobalId stability:** deterministic derivation from the document identity (already stored in
    the native file) and the body's stable ID; imported GlobalIds are kept when valid and unique.

## Implementation split

Each slice is an independently testable review unit. Slices a to d are the import
direction; e is export; f needs an approved UX design. All slices keep documents without
IFC content at their current schema (26 to 28, or 29 with terrain) and do not touch
modelling limits.

- **R097.a: STEP-P21 reader and the three entity-layout tables.** New `src/io/ifc_source.*`.
  The bounded reader (header, schema check, instance index, argument decoding, escapes),
  the layouts for the subset (about one hundred entities across IFC2x3, IFC4 and 4.3), the
  accounting counters, and `parser_fuzz_ifc_step` with seeds and a
  `parser_fault_corpus_tests` entry. **Verify:** checks D1, D4, I1 (file-level limits), I2, I3, J1 (the step target), J2, J3 and J5 at this layer;
  attribute decodes of every accountable entity in the three Architecture files equal the
  oracle's; entity counts per class equal; no allocation above the budget on the limit
  fixtures; sanitizer suites pass.
- **R097.b: Import core.** Units, spatial hierarchy, placements, tessellated and faceted
  geometry, materials and colours, property sets and quantities, types, classification, the
  report v1, new unsaved document, `--import-ifc` (without georeferencing, without solids).
  creates `parser_fuzz_ifc_import` with its seeds. **Verify:** A1–A5, B1–B7, C1–C5, D1–D4, E1,
  E2 (tessellated sets), E5, E6, H1, H2, I1 (geometry limits) and J1 (the import target) on the
  certification set and the originals; the whole import is one undo step.
- **R097.c: Solids, Booleans, mapped items and openings.** Extrusion and revolution with the
  four profiles and arcs, half-spaces and Boolean results, `IfcMappedItem` as component
  definitions, opening subtraction with fallback, segments option. **Verify:** E2 (extrusions),
  E3, E4; the Reference View wall volume equals the oracle's; unsupported solids are reported
  not imported; the import fuzz target and its campaign are extended to the new code.
- **R097.d: Georeferencing.** `IfcMapConversion` and `IfcProjectedCRS`, the site-origin
  mapping, the rotation decision, far-from-origin refusal, schema-29 notice. **Verify:** F1–F4; the import fuzz target covers georeference input.
- **R097.e: IFC4 export.** The writer, the mapping of 3.8, `GlobalId`, georeference, the
  export report, `--export-ifc`. **Verify:** G1–G7 (G3 is the deterministic-bytes check), including the
  independent-consumer schema validation and the checked-in golden exports; the claim is **not made** if G1 fails.
- **R097.f: Desktop workflow.** Import and Export dialogs, the report sheet, progress and
  cancel, the dirty-document prompt, recovery context. Requires the approved UX design.
  **Verify:** `ifc_import_input_tests` and `ifc_export_input_tests` on X11 and Wayland at 1×
  and 2×, with accessibility and keyboard checks, and source-safe Save.
- **R097.g: Acceptance record.** `docs/verification/R097g-ifc-exchange.md` with a JSON
  summary: the Verify line and every check of 6.2, the budgets measured and frozen, the
  native matrix, sanitizers and fuzz campaigns, the support-matrix row, the user-guide
  section, and the exchange-boundaries entry for F04 (IFC). It states which directions are
  claimed. F04 is not delivered until R098 also merges and the M10 gate accepts the release.

## Reconsideration triggers

- R097.a–c cannot meet their Verify criteria within budget: reconsider A2, then D.
- Fuzzing finds persistent parser defects that the bounds do not contain.
- Owners need 4.3 infrastructure, advanced breps or NURBS: the subset boundary and perhaps the
  library choice change (and R098 shares OCCT-class needs).
- A distribution package for IfcOpenShell's parser appears in an official Arch repository, or
  the project publishes a lean parser-only release with stable ABI.
- buildingSMART publishes the EXPRESS schemas under a licence that permits generated code.
- The owner chooses a site-rotation row (decision 6c): the georeference mapping widens.

## Owner decisions

1. **Library:** an in-house, bounded STEP-P21 reader and writer for a defined subset; with IfcOpenShell only as a test oracle; not IfcOpenShell (full or parser only), web-ifc or an external worker. *Recommended: in-house.*
2. **Schemas:** import IFC2x3 TC1, IFC4 ADD2 TC1 and IFC4.3 ADD2 (building entities only); export IFC4 ADD2 TC1 only, with no Reference View or certification claim. *Recommended: yes.*
3. **Claims:** a direction is claimed only when its checks pass; import may ship before or without export. *Recommended: yes.*
4. **Export geometry:** tessellated face sets only. *Recommended: yes.*
5. **Import geometry and openings:** tessellate on import; subtract openings when both solids are closed, else leave the host uncut, hide the opening and report. *Recommended: yes.*
6. **Rotated or scaled map conversion:** keep project axes and report by default, with an opt-in grid-aligned mode; defer a site rotation. Alternatives: refuse georeferencing entirely, or extend the site origin now. *Recommended: keep axes, opt-in grid mode, site rotation later.*
7. **Models far from the origin (beyond ±1e6 m):** refuse with the extent. Alternatives: shift and set a site origin, or shift by a reported translation with no site origin. *Recommended: refuse in the first release.*
8. **Spaces and tags:** spaces come in on a tag that starts hidden; one tag per IFC class family; layers become properties. This is a default-visibility choice, so it is yours. *Recommended: yes.*
9. **Properties and quantities:** the `Pset.Property` mapping, with length, area and volume converted and other numbers stored as in the file and reported. *Recommended: yes.*
10. **Import target:** a new unsaved document, not added to the open one. *Recommended: new document.*
11. **Fixtures:** buildingSMART's certification files under CC BY 4.0 with attribution; the Reference View samples only if you accept the repository licence for them, otherwise original equivalents; no community samples. *Recommended: accept, after a quick check of the attribution wording.*
12. **Test oracle:** IfcOpenShell's Python package as optional, developer-only tooling, with results checked in. *Recommended: yes.*
13. **Assistant and MCP:** no new commands, queries or assistant tools in R097. *Recommended: yes (ADR 0169 decision 9).*
14. **Budgets:** adopt 6.3 (128 MiB, 2,000,000 entities, 3 s parse, 10 s convert, 1 GiB peak) as proposed values to be confirmed by measurement. *Recommended: yes.*
15. **Desktop UX:** commission a UX design for the Import and Export IFC dialogs and the report before R097.f. *Recommended: yes, after R097.b lands, when the report is real.*
16. **GlobalId stability:** deterministic GlobalIds from the document identity (stored in every native file) and the stable body ID; imported GlobalIds are kept when valid and unique. *Recommended: yes. This needs no new UI or format field.*

## Appendix: evidence and sources

All read on 2026-10-22 unless stated. `gh`/GitHub API and PyPI JSON were used for facts about
repositories; the buildingSMART technical site returned HTTP 403 to automated fetches, so
statements about MVD status and ISO numbers rest on search summaries and third-party
pages and are marked as such.

- **IfcOpenShell.** [Repository](https://github.com/IfcOpenShell/IfcOpenShell) (GitHub licence
  LGPL-3.0; README lists the C++ libraries `ifcparse`, `ifcgeom` and others as
  LGPL-3.0-or-later, with an asterisk whose footnote is not in the README, which I could not
  resolve). Tag `ifcopenshell-v0.9.0`, published 2026-10-01 (`7ba70197`); PyPI
  [ifcopenshell 0.9.0.post1](https://pypi.org/project/ifcopenshell/), 52 MB wheels. CMake
  options read from `cmake/CMakeLists.txt` at `v0.9.0`: `WITH_OPENCASCADE` and `WITH_CGAL` on
  by default, OCCT 7.5.1 and CGAL 5.4 minimums, `BUILD_SHARED_LIBS` described as required,
  `BUILD_FUZZERS`, `SCHEMA_VERSIONS`; `src/ifcfuzz` exists. AUR: `ifcopenshell` 0.9.0-2,
  `ifcopenshell-stable` 0.8.5-4, `ifcopenshell-git`, `bonsaiviewer`
  ([AUR RPC query](https://aur.archlinux.org/rpc/v5/search/ifcopenshell?by=name-desc); the
  PKGBUILD lists `LGPL-3.0-or-later` and `GPL-3.0-or-later`). Arch `extra`: `opencascade`
  7.9.3, `cgal` 6.2.1, `boost` 1.92.0, `hdf5` 2.2.0
  ([package search API](https://archlinux.org/packages/search/json/?q=opencascade)); no
  `ifcopenshell` package was found there.
- **Prototype.** The IfcParse-only build described in section 1, commit `7ba70197`, in
  `sketchyup-ci-pinned:r094a` with `boost` 1.92.0 from the image's dated archive; command:
  `cmake -S src/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_OPENCASCADE=OFF
  -DWITH_CGAL=OFF -DBUILD_IFCGEOM=OFF -DBUILD_CONVERT=OFF -DBUILD_IFCPYTHON=OFF
  -DBUILD_GEOMSERVER=OFF -DBUILD_EXAMPLES=OFF -DCOLLADA_SUPPORT=OFF -DBUILD_BONSAIVIEWER=OFF
  -DSCHEMA_VERSIONS="2x3;4"`, then `cmake --build --parallel 4` inside a container limited to
  10 GB and 4 CPUs: 384 s, 29 build steps. No SketchyUp file was changed by it.
- **web-ifc.** [Repository](https://github.com/ThatOpen/engine_web-ifc): MPL-2.0 (GitHub and
  `LICENSE.md`), release 0.78 (2026-09-21), `package.json` 0.0.79, 74 open issues, pushed
  2026-10-22. Commit `6b8bf3f` read locally: C++ under `src/cpp` is 75,015 lines (2.3 MB,
  including a 1.27 MB generated `schema-names.h`); the CMake builds a `web-ifc-library` static
  library natively and installs headers, and the WASM targets are the primary build; nine
  dependencies are fetched at pinned commits; no fuzzing target in the repository or its
  workflows; `IfcLoader::SaveFile` saves a loaded model while `WriteLine` (composing a new
  entity) is defined in `src/cpp/wasm/web-ifc-wasm.cpp`; its schema map folds `IFC4X1`, `IFC4X2` and the 4.3 releases onto `IFC4X3` and `IFC2X_FINAL`
  onto `IFC2X3`. I did not build it.
- **Others.** [IfcPlusPlus](https://github.com/ifcquery/ifcplusplus) (MIT; README: not
  actively developed; points to xeoIFC);
  [xeoIFC](https://github.com/xeofoundry/xeoIFC) (a README that describes licence and pricing;
  no open-source licence reported by GitHub); `ifc-lite` (search results only).
- **Security.** Bentley advisory
  [BE-2022-0006](https://www.bentley.com/en/advisories/be-2022-0006/); GitHub security
  advisories API: empty for IfcOpenShell and web-ifc; OSV queries for `ifcopenshell` (PyPI)
  and `web-ifc` (npm): empty.
- **Fixtures.** [buildingSMART Certification-datasets](https://github.com/buildingSMART/Certification-datasets)
  (the former `Sample-Test-Files`; HEAD `80d976a9`, 2026-09-04): `LICENSE` CC BY 4.0,
  `import-certification.md` (the minimal import test set: models import together, units
  and georeferencing, spatial breakdown, material assignment, colours, type and occurrence
  properties with GUIDs, quantities, classification references, assemblies, groups and
  systems, spaces). The three Architecture files are headed
  `ViewDefinition [CoordinationView_V2.0]` (`IFC2X3`), `[ReferenceView_V1.2]` (`IFC4`) and
  `[ReferenceView]` (`IFC4X3_ADD2`), and name `Sketchup-IFC-manager 5.6.0 / SketchUp 2026`
  as the writing application. SHA-256 of the files downloaded (fixture names as in 6.1):
  `bsi-ifc2x3-arch.ifc` `c4db65ba847f6b369a95d6c54fa11f4750cbe6d59f021934e8923d8d578e5885`;
  `bsi-ifc4-arch.ifc` `8790a1e193e82b8e7e7f337ec2633cd40f2120590317a1443503a25b079e2e80`;
  `bsi-ifc43-arch.ifc` `96e7103812b15b65eb1fd7802d36b9962833485dce5ead4f440bbf529530881d`;
  `bsi-ifc4-landscaping.ifc` `937dc9c0e013b0dbbf56dc0fa0dc435efe16a2234cd364af820d2cf1deaa3d7f`;
  `bsi-ifc4-road.ifc` `ebc5c85f6da454de08dc63675af242b6572682692682dff1a064a71b6bcd73e8`;
  `rv-wall-with-opening-and-window.ifc` `73b0e45d931d5dc13bfee5fdc7bd80f796526445458b2de74c4168d209097832`;
  `rv-basin-tessellation.ifc` `7278769ec5ef35d388819d2f197519061c7f0f68dd43a733f333ddeb74578121`;
  `rv-column-straight-rectangle-tessellation.ifc` `58bb9b2cae96edf2c368de95f526650f3f282e4e7fc37c0065c501bdbeed00a1`;
  `rv-tessellated-item.ifc` `758974f7d558f11b8d8121816954179def32594ed7eafcd1faae70ee0a8e5946`;
  `rv-tessellation-with-individual-colors.ifc` `3ab1290207d8538da9d8ebc14826b769eeb79fce7d75aaa85c35ed7a5a8dbd92`.
  [Community-Sample-Test-Files](https://github.com/buildingsmart-community/Community-Sample-Test-Files)
  README ("NOT official ... most do not pass validity check").
- **Schema documentation.** buildingSMART
  [IFC4.3.x-development](https://github.com/buildingSMART/IFC4.3.x-development)
  (`IfcMapConversion`, `IfcMapConversionScaled`, `IfcCoordinateOperation`, `IfcProjectedCRS`
  pages; branch `ifc4.3-main` `LICENSE.md` (CC BY-ND 4.0 header, then a copyright notice that
  permits use in software development with attribution, and a trademark notice; the old
  `master` branch `LICENSE` is the bare CC BY-ND text) and `README.md`, which links the formal
  4.3.2.0 release to the ISO catalogue entry
  [iso.org/standard/84123](https://www.iso.org/standard/84123.html)). Search-derived and
  unverified against the ISO catalogue: ISO 16739-1:2018 for IFC4 ADD2 and ISO 16739-1:2024 for IFC 4.3.2;
  [Graphisoft's MVD page](https://help.graphisoft.com/AC/30/INT/Topics/121_IFC/121_IFC-47.html)
  for the roles of the Coordination, Reference and Design Transfer views;
  [the IFC2x3 georeferencing forum thread](https://forums.buildingsmart.org/t/geolocation-standards-in-ifc2x3-and-ifc4/2329)
  for the `ePset_MapConversion` convention (a proposal, not a standard).
