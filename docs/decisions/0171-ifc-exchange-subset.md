# IFC exchange subset

Status: **Accepted (2026-10-22)** for decisions 1 to 16; decisions 17 to 22 (how IfcOpenShell is
packaged and run) are **proposed** and wait for the owner. Roadmap
[R097](../PR_ROADMAP.md#r097) (M10, scope row [F04](../SCOPE.md)); the sibling
[R098](../PR_ROADMAP.md#r098) covers STEP. Nothing in this ADR is implemented. Each section gives
the options that were weighed and the recommendation. Facts about third-party projects were read
on 2026-10-22 and are cited in the [appendix](#appendix-evidence-and-sources); anything not
verified is marked as such.

**Owner decision, 2026-10-22, verbatim:** "Just use IfcOpenShell. I don't want to build it. I
approve all." This settles decision 1: SketchyUp uses and ships IfcOpenShell, and writes no IFC
reader, writer or geometry interpreter of its own. It approves decisions 2 to 16 as recommended.
The first draft of this ADR (PR #412, first two commits) recommended an in-house STEP-P21 reader
instead; that analysis is kept in section 1 as history, because it explains what is and is not
being built. Everything that depends on the library choice was reworked for this revision:
linking and licence, packaging, the geometry path, fuzzing exposure, budgets, the slice plan and
the checks.

## Context

F04 asks for "broader CAD/BIM exchange: prioritize IFC and STEP use cases; publish supported
entities, hierarchy/property fidelity and geometry conversion limits". R097's Build line: select an
IFC library and schema subset with distribution review; map the supported hierarchy, properties and
geometry with a fidelity report; limit claims to demonstrated import and export directions. Its
Verify line: named fixtures preserve the claimed units, hierarchy and properties; unsupported
entities are reported; converted geometry remains inspectable. R097 requires R089 (the 1.0 gate,
accepted) and R074 (glTF interchange, delivered). F04 ships in its own gated minor release
([M10 acceptance](../M10_ACCEPTANCE.md), [ADR 0162](0162-release-versioning.md)); the release also
needs R098, so R097 alone does not make F04 delivered.

What the repository gives us (surveyed at `db719b0d`, the 1.3.0 release):

- **Importers are bounded pure functions.** DXF ([ADR 0120](0120-bounded-planar-dxf-source.md),
  [0121](0121-native-dxf-wire-conversion.md)), OBJ ([0112](0112-bounded-obj-mtl-parsing.md),
  [0113](0113-native-obj-conversion.md)), STL ([0116](0116-bounded-stl-source-parsing.md)) and glTF
  ([0109](0109-bounded-gltf-packages.md), [0110](0110-native-gltf-conversion.md)) each split into a
  bounded source parser (size, line, record and name limits, explicit units, counted omissions), a
  conversion that builds one unsaved native document in one edit and validates the whole
  serialization before returning, a report, then a desktop and CLI workflow
  ([0111](0111-native-gltf-import-workflow.md), [0115](0115-native-obj-interchange-workflow.md),
  [0123](0123-native-dxf-interchange-workflow.md)). Every one of them opens a **new unsaved
  document**; terrain is the only importer that adds to the open one
  ([ADR 0168](0168-terrain-import-and-surfaces.md) decision 11). Reports are JSON with
  `apiVersion: 1`, source SHA-256, counts, `losses` and `notices`; DXF also has `omittedEntities`.
- **Isolated helper processes have a precedent.** The Blender worker
  ([ADR 0033](0033-blender-worker.md)) runs as a child process with a fixed argv, a private
  temporary directory, a deadline, bounded output and a manifest, terminate-then-kill
  cancellation, and no live document reference; the build plan asks for "allow-listed worker
  operations and non-executable interchange data".
- **Parser fuzzing is a release practice.** One libFuzzer target per format, seeds from generated
  fixtures, 512-mutation checks in `parser_fault_corpus_tests`, and ASan/UBSan campaigns
  ([PARSER_FUZZING](../PARSER_FUZZING.md)).
- **Native model.** `Body` (`src/core/body.hpp`) is a geometry body, group, reference image or
  terrain, with a name, colour, per-face colour and material, a `tag`, `hidden`/`locked`, a
  `transform`, a `parent`, and `properties`: a flat map of string keys (at most 128 bytes) to bool,
  finite double or string (at most 2,048 bytes), at most 128 per body (`src/core/model.cpp`).
  Component definitions and placements exist ([ADR 0159](0159-instanced-component-storage.md)); glTF
  import turns a shared mesh into one definition with one placement per node. Faces must be planar
  within 1e-7 m; the tessellator is Clipper2; `checkPoint` refuses any coordinate beyond 1e6 m.
  A body is limited to 100,000 vertices and faces (`Surface::validate`); unique geometry to 100,000
  vertices, 100,000 faces and 10,000 body records, and 600,000 faces materialized
  (`DocumentLimits`); materials and tags to 1,024 each, material names unique; the model JSON chunk
  to 32 MiB (`NativeLimits::modelBytes`), so a large property load can break saving even when every
  geometry limit holds.
- **Site origin.** `SiteOrigin` (`src/core/site_origin.hpp`, R095.a) is a translation only: easting,
  northing, elevation in metres (each within ±1e8 m), an informational CRS label of at most 64
  bytes, and provenance. There is **no rotation and no scale**. A document with a site origin is
  written as schema 29 and is refused by SketchyUp 1.2.x and earlier (ADR 0168 decision 7). No PROJ
  dependency exists (decision 8).
- **Dependencies today.** Third-party code is vendored unmodified under `third_party/` (Clipper2
  BSL-1.0, Manifold Apache-2.0, cgltf MIT, CDT MPL-2.0) and linked **statically**; their licences
  are installed to `share/licenses/sketchyup` and collected into `THIRD_PARTY_NOTICES` by
  `scripts/release-artifacts.sh`. Application code is MIT ([ADR 0001](0001-baseline.md)). The
  package runtime depends on Qt, OpenSSL, Wayland, shared-mime-info and a font; it builds from a
  source archive in the pinned image (Arch archive snapshot 2026/10/02) with official-repository
  packages only; ADR 0109 states "there is no build-time download".
- **Automation.** Importers other than terrain are desktop and CLI workflows, not `noun.verb`
  commands. `assistantVisible()` filters the assistant's tools and `assistant_tool_surface_tests`
  pins them; the frozen provider corpus is at v5
  ([ADR 0168, R095.g notes](0168-terrain-import-and-surfaces.md#implementation-notes-r095g)).
- **CI.** [ADR 0170](0170-ci-tiers.md): a fast tier per pull request (compile shards when the
  change is wide, five test shards, a content-addressed ccache restored by commit, a package cache,
  a target of about five minutes) and a full tier at releases and nightly; a path map
  (`.github/ci-path-map.json`) selects areas, and `third_party/**` selects build, native and the
  slow core suites. The pinned image is `archlinux:base-devel` with a dated mirror.

What the fixtures show. buildingSMART's public certification datasets contain the same
"Simple-Scene" in three schemas (IFC2x3 TC1, IFC4 ADD2 TC1, IFC4.3 ADD2). I downloaded the three
`Building-Architecture.ifc` files and counted instance lines. They differ in representation: the
IFC2x3 file has 20 `IfcFacetedBrep` and 2 `IfcExtrudedAreaSolid` items; the IFC4 and IFC4.3 files
have 12 `IfcTriangulatedFaceSet` and 2 `IfcExtrudedAreaSolid`. All three have 1 project, 1 building,
1 storey, 2 spaces, 4 walls, 3 slabs, 1 roof, building element proxies (5, 3 and 2), a
classification reference with a URI, and psets and quantity sets; none has a door or window. The
IFC4 and IFC4.3 files carry an `IfcProjectedCRS` (`EPSG:32760`) and an `IfcMapConversion` whose axis
is rotated **60 degrees** (`XAxisAbscissa` 0.5, `XAxisOrdinate` 0.866) with `Scale` 0.001 and a
millimetre project length unit. So the georeferencing fixture buildingSMART certifies against cannot
be represented by a translation-only site origin; decision 6 exists because of this. The one
door/window/opening example is the five-file "ReferenceView_V1.2" set (a wall with an opening and a
window, built from three extrusions and one `IfcRelVoidsElement`).

## 1. Library: IfcOpenShell (decision 1)

### 1.1 What was weighed

The first draft compared five routes against six requirements (licence, Arch packaging, build
weight, bounds and fuzzing, ability to write IFC, maintenance). The owner chose the first.

| | **A. IfcOpenShell, shipped (chosen)** | B. In-house STEP-P21 reader and writer | C. web-ifc (C++ core) | D. IfcOpenShell as an external tool the user installs |
| --- | --- | --- | --- | --- |
| Licence | LGPL-3.0-or-later | MIT (ours) | MPL-2.0 | LGPL-3.0-or-later, not linked |
| Reads IFC2x3, IFC4, IFC4.3 | Yes (all with generated schema code) | Hand-written entity tables | Yes | Yes |
| Interprets geometry (extrusions, sweeps, Booleans, openings, NURBS) | Yes, with a kernel | We would write it | Yes (own engine) | Yes |
| Writes IFC | Yes | We would write it | Saves a loaded file only (composing entities is in the WASM binding) | Yes |
| Maintenance | Very active (2,054 open issues; pushed the day of writing) | Ours | Active | Active upstream |
| Fuzzing | Upstream has one libFuzzer harness, for the parser (`src/ifcfuzz`); none for geometry that I found | Our targets | None found | Isolated by process |
| Cost to us | Packaging, an isolation layer, an adapter | The largest: parser, tables and geometry | Packaging, a writer | An install the user must do |

The in-house route was recommended in the first draft because it kept the single-static-binary
distribution model and our own limits. The owner's instruction, "I don't want to build it", prefers
not writing and maintaining a reader, writer and geometry interpreter that IfcOpenShell already
provides, and accepts the packaging and dependency cost of doing so. This ADR follows that
instruction. Both IfcPlusPlus (MIT, but its README says it is not actively developed) and xeoIFC
(no open-source licence) were ruled out; `ifc-lite` and STEPcode were not verified and not
considered further.

**Prototype 1 (parser only).** In `sketchyup-ci-pinned:r094a` with `boost` 1.92.0 added, I built
IfcOpenShell `ifcopenshell-v0.9.0` (`7ba70197`) with IfcParse only (schemas `2x3;4`, Release,
`-j4`, container capped at 10 GB and 4 CPUs): 384 s, 29 build steps; libraries 1.4 MB (parse), 0.13
MB (plugin), 3.8 MB (IFC2x3 schema) and 4.6 MB (IFC4 schema); run-time dependencies libstdc++,
libm, libgcc and libc only; Boost is a build-time header dependency. Compiler processes held 620 to
885 MB resident, sampled with `ps`, on the generated schema sources (`Ifc4.cpp` 1.7 MB, `Ifc4.h`
3.3 MB).

### 1.2 What is shipped, and what is not

IfcOpenShell is a family of libraries. Only these are needed:

| Component | Needed for | Shipped |
| --- | --- | --- |
| `IfcParse` (`libifcopenshell.parse.so`) and the plugin loader (`libifcopenshell.plugin.so`) | Reading and **writing** IFC: the file model (`ifcopenshell::file`), typed entity classes per schema, writing through `operator<<(std::ostream&, const ifcopenshell::file&)`, the `hierarchy_helper<Schema>` template (project, site, building, storey, placements, owner history) and attribute traversal | Yes |
| Schema libraries `parse_schema_ifc2x3`, `parse_schema_ifc4`, `parse_schema_ifc4x3_add2` | The three schemas of decision 2. Built with `SCHEMA_VERSIONS="2x3;4;4x3_add2"`. The default builds eight, including IFC4.1, 4.2 and the 4.3 release candidates, which are refused here | Yes (three) |
| `IfcGeom`, the geometry mapping plugins for the three schemas and **one geometry kernel plugin** | Import: turning extrusions, sweeps, Booleans, mapped items and breps into triangles; subtracting openings; styles | Yes |
| The IfcConvert tool, the `serializers` library and the OBJ, SVG, IGES, STEP, TTL, XML and IFC-writer plugins | Converting to other formats | **No** (IfcConvert was a development tool for the prototype measurements) |
| CGAL kernel | An alternative kernel | **No**: Arch lists `cgal` as `GPL-3.0-or-later` and `LGPL-3.0-or-later`, and its polygon-mesh and Nef parts are GPL; linking them would make the combined work GPL |
| Python wrapper, Bonsai (GPL-3.0-or-later), glTF/USD/COLLADA output, HDF5, RocksDB, the viewer | Other front ends | **No** |

**Kernel choice.** `v0.9.0` has four kernel plugins in `src/ifcgeom/kernels`: `opencascade` (about
12,900 lines; the one IfcOpenShell is built around), `cgal` (GPL, above), `manifold` (about 2,400
lines, a Boolean helper, off by default, `WITH_MANIFOLD`) and `passthrough` (items that are already
tessellated: it supports no Boolean operations and, as far as I can see, emits no feature edges). The
recommendation is the **OpenCASCADE kernel** for everything, with `passthrough` available for files
that contain only tessellated items. OpenCASCADE (OCCT) is LGPL-2.1-or-later with the OCCT exception
(Arch's `opencascade` 7.9.3-3: `LGPL-2.1-or-later` and `LicenseRef-LGPL-exception`). IfcOpenShell
requires OCCT 7.5.1 or later; upstream OCCT is at V8.0.1 (2026-07-30), Arch has 7.9.3.

**The geometry kernel replaces our own tessellation work.** The first draft of this ADR had R097.c
write extrusion, revolution, half-space, Boolean and mapped-item interpretation on Clipper2 and the
R055 Manifold adapter. With IfcOpenShell that slice disappears: the iterator produces triangle meshes
for the elements of a file, with the opening subtractions, representation selection, placement
composition and style resolution done by the library. SketchyUp's part is the **adapter**: settings,
receiving the triangles, rebuilding planar polygon faces from them, reading what the library does not
resolve (below), the properties, the limits and the report. Decision 5 ("tessellate everything into
editable faces; subtract openings; report the ones that are not subtracted") stands; the *mechanism*
is now the library's.

**What the prototype did and did not exercise.** Every measurement in this ADR comes from
`IfcConvert`, the command-line tool, not from the iterator, writer and settings API the worker will
use; `IfcConvert` also applies its own defaults (it excludes `IfcOpeningElement` and `IfcSpace`
unless told otherwise), so the prototype's meshes contain no spaces. R097.a therefore starts with a
probe that drives the iterator and the writer through the worker and re-checks the claims below.
Things I found in the source of `v0.9.0` that the design depends on:

- The settings `ContextTypes` and `ContextIdentifiers` are documented "currently option has no
  effect"; representation selection uses `ContextIds` or the ordered `ContextPriorities`.
- `LengthUnit` is an internal setting that the library overwrites from the file's units; output in
  metres is `ConvertBackUnits` off. With a missing length unit, or other than one `IfcProject`, the
  library only logs a warning and assumes metres, so **the refusals of 3.5 are the worker's**, not the
  library's. In the prototype the Reference View wall's OBJ came out in millimetres while the
  Architecture files' OBJs came out in metres, although every file declares a millimetre length unit; I
  did not isolate the cause. The adapter therefore **verifies every mesh against the file's units**
  (check A1) instead of trusting the output unit.
- `MaxVoidsPerElement` means "no voids will be processed for elements with more voids" (0 is
  unlimited); it is a skip, not a bound on work, and it is silent apart from the log.
- `IfcIndexedColourMap` is not read by the C++ geometry code (the prototype's per-face-colour sample
  came out as one default material), so **per-face colours are read by the adapter**.
- Feature edges come only from the OCCT kernel.
- Output order from several threads differs from the single-threaded order.
- The library's only fuzz harness is `ifcparse_fuzzer` (parse and re-serialise; upstream disables leak
  detection because of a known leak).

### 1.3 Licence and LGPL-3.0 compliance

I have no legal opinion; this is an engineering reading for the owner to confirm.

- **The rule.** LGPL-3.0 §4 lets a combined work be distributed under terms of the author's choice,
  provided the combination does not restrict modification of the library and reverse engineering to
  debug such modifications, and provided the distributor (a) gives notice that the library is used and
  covered by the licence, including where the application displays copyright notices, (b) conveys a
  copy of the GPL and LGPL texts, and (c) either uses a **suitable shared-library mechanism** (a copy
  of the library already present on the user's system at run time, which still works with a modified,
  interface-compatible version) or supplies the minimal corresponding source and the application code
  in a form that allows relinking. For a "User Product" installation information is also needed; an
  Arch package on a user's own machine puts no restriction on replacing a library. Conveying the
  object code in a package also brings the source obligation (§4 and LGPL-2.1 §3 for OCCT): the source
  must accompany it or be offered in writing.
- **MIT application plus LGPL library is fine when linked dynamically.** MIT code may be combined with
  LGPL code; the LGPL terms attach to the library, not to the MIT application. Static linking would be
  possible too, since our source is public, but it needs the relinking information. Upstream CMake
  states that `BUILD_SHARED_LIBS` is **required** (default ON), so dynamic is also the only supported
  way to build it.
- **Recommended shape.** Only one new executable links IfcOpenShell: a separate helper,
  `sketchyup-ifc-worker` (decision 19). The SketchyUp application, its CLI and every other executable
  do not link it, so the LGPL attaches only to the helper and to the libraries beside it. Everything
  is installed **flat in one private directory, `/usr/lib/sketchyup/`**: the helper, `IfcParse`,
  `IfcGeom`, the plugin loader and every plugin. IfcOpenShell finds its plugins in the directory of
  `libifcopenshell.plugin.so` and checks a compiler ABI tag, so the plugins must sit beside it. The
  helper has a `RUNPATH` of `$ORIGIN` (a `RUNPATH`, not an `RPATH`, so `LD_LIBRARY_PATH` can override
  it: that is the "replace the library" path and check L2).
- **Notices and source.**
  - `share/licenses/sketchyup/IfcOpenShell-COPYING.LESSER` and `IfcOpenShell-COPYING` (the LGPL and
    GPL texts upstream ships), `OpenCASCADE-LICENSE` (the LGPL-2.1 text and the OCCT exception) and
    the packaged notices of anything else bundled are installed. `scripts/release-artifacts.sh` builds
    `THIRD_PARTY_NOTICES` only from `install(FILES ...)` lines for files **tracked in the repository**,
    so the OCCT licence texts are copied into the vendored tree (`third_party/ifcopenshell/`), because
    OCCT itself is not vendored.
  - The PKGBUILD `license` array gains `LGPL-3.0-or-later` and `LGPL-2.1-or-later`. (Bonsai and the
    GPL-3.0-or-later parts of the IfcOpenShell repository are not built.)
  - `dependencies.json` gains IfcOpenShell (version, commit, linkage `dynamic (private)`, licence,
    source, patches) and OCCT (system, or as decision 18 decides).
  - The application shows no third-party notice list today (I found none in `src/app`); if one is
    added, it names IfcOpenShell and OCCT (LGPL-3.0 §4 asks for the notice where notices are shown).
  - **Source.** The IfcOpenShell source is vendored unmodified in the source archive (1.4), which is
    also the source of every release package. `share/doc/sketchyup/IFC-SOURCE.txt` states where the
    corresponding source of the IfcOpenShell and OCCT binaries in the package is (our release archive;
    the upstream tags `ifcopenshell-v0.9.0` and `V7_9_3`; for OCCT, which is Arch's build, Arch's source
    package) and carries a **written offer** to supply it for at least three years after the last
    release that contains it.
  - Redistributing OCCT's binaries may also redistribute code OCCT embeds (I infer fast_float and
    rapidjson from Arch's build dependencies; not verified): the notices step in R097.a checks the
    shipped OCCT libraries' own `LICENSE` and third-party lists.
  - `packaging/arch/README.md` gains a section "Replacing IfcOpenShell": build the same tag with CMake,
    copy the `libifcopenshell*.so` files and the plugin libraries over those in `/usr/lib/sketchyup/`,
    and run `sketchyup-ifc-worker --probe`.
- **No upstream modification.** The vendored copy is trimmed (unneeded directories removed) but not
  edited; any needed patch goes into `third_party/ifcopenshell/PATCHES.md`, as Clipper2's does, and is
  published with the source.
- **Generated schema code and trademarks.** The shipped `Ifc*.cpp` and `.h` schema files are generated
  by IfcOpenShell from buildingSMART's EXPRESS schemas and carry only IfcOpenShell's headers.
  buildingSMART publishes the schemas and documentation under CC BY-ND 4.0 ("no derivatives"): the
  `IFC4.3.x-development` repository's `LICENSE.md` opens with it, then adds a notice that the
  technical documentation "may be photocopied, used in software development, or translated into
  another computer language without prior written consent ... provided that full attribution is given",
  with consent needed "if changes are made to the technical specification", and a trademark notice
  asking that IFC(TM) and ifcXML(TM) be credited. IfcOpenShell itself distributes the generated code
  under the LGPL; whether that settles it for us, and the trademark credit (user guide and fixture
  notice), is for the owner or counsel to confirm (question Q1).

### 1.4 Getting it into the package

| Route | Verdict |
| --- | --- |
| **Official Arch repository** | Not available: a search of `extra` for `ifcopenshell` found nothing (checked 2026-10-22). |
| **AUR `ifcopenshell` (0.9.0-2; also `ifcopenshell-stable` 0.8.5-4 and `-git`) as a dependency** | Rejected. It is the whole project (Python wrapper, Bonsai, its viewer and, as makedepends, `blender`, `qt6-base`, `qt6-svg`, `swig`, `cgal` and others), 3 votes. An official-style package cannot depend on the AUR, the pinned image and CI build only from official repositories, and the CGAL build brings GPL code. |
| **Download and build at package time** (a makepkg `source` entry or CMake `ExternalProject` with a URL) | Rejected: it breaks "build from one source archive, no build-time download" (ADR 0109, the release scripts) and the dated-mirror reproducibility. |
| **Vendor a trimmed source tree in `third_party/ifcopenshell/` and build it from there** | **Recommended (decision 17).** The pattern every other dependency already follows. The needed directories (`src/ifcparse` with three schemas, `src/ifcgeom`, `src/plugin`, `cmake`) are about **19 MB** of source (the generated schema sources are 16 MB of it; the whole upstream `src/ifcparse/schemas` for all eight schemas is 68 MB). |

**How it is built.** Upstream's `cmake/CMakeLists.txt` is a top-level project (it reads `../src/*` and
sets global options), and the root `CMakeLists.txt` already sets `BUILD_SHARED_LIBS` for the
`SKETCHYUP_SHARED_LIBS` and sanitizer builds. The sub-project is therefore a **separate configure**
(an `ExternalProject` or a second `cmake` invocation on the vendored tree, guarded by
`SKETCHYUP_BUILD_IFC`), with `BUILD_IFCGEOM=ON`, `WITH_OPENCASCADE=ON`, `WITH_CGAL=OFF`,
`BUILD_CONVERT=OFF`, `BUILD_IFCPYTHON=OFF`, `BUILD_GEOMSERVER=OFF`, `BUILD_EXAMPLES=OFF`, the upstream
test options **off** (they fetch Catch2 over the network), `COLLADA_SUPPORT=OFF` and
`SCHEMA_VERSIONS="2x3;4;4x3_add2"`, installing only the libraries and plugins of 1.2.

**Pinned version.** `ifcopenshell-v0.9.0`, commit `7ba701972f7182322e0a00db8f0e1f3de5f69840`,
published 2026-10-01; the PyPI library release is 0.9.0.post1. The tree records the commit, the trim
list and the SHA-256 of the upstream tag archive in `third_party/ifcopenshell/README.sketchyup.md`, as
`third_party/cdt` does. Upgrades are explicit changes with the checks of section 6 re-run. Upstream's
API changed between 0.8 and 0.9 (the typed entity classes, the `express` namespace and the new
`geometry` settings API are recent), so a floating dependency is not an option.

**Run-time dependency.** The libraries record `libboost_program_options` and `libboost_container`, so
the package depends on `boost-libs` (a small, common dependency; the two libraries are well under a
megabyte).

**Build dependencies in the pinned image.** `boost` (headers; 158 MB installed), `eigen` and, for the
OCCT kernel, `opencascade` headers and libraries. Arch's OCCT CMake configuration refuses to load
without VTK's own dependencies (in the prototype `nlohmann-json` was needed only to get past VTK's
configuration; whether it is still needed with the two variables below was not tested), so the build
passes `-DOCC_INCLUDE_DIR=/usr/include/opencascade -DOCC_LIBRARY_DIR=/usr/lib` (the AUR package does
the same). That shortcut skips an `--as-needed` block in upstream's `FindOpenCASCADE.cmake`, which
matters for the next paragraph. `makepkg` runs without `-s` in the acceptance job, so these packages
must be in the CI package lists, which feed the shared pacman cache key in `native.yml`; installing
`opencascade` there also installs `vtk`, `tcl` and `tk` (about 0.55 GiB) into the build image.

**OCCT at run time (decision 18).** Arch's `opencascade` package depends on `vtk`, `tcl`, `tk`,
`verdict` and `libxss`. Measured in the pinned image: installing it adds `tcl` (13 MiB), `tk` (12
MiB), `libxss`, `verdict`, `vtk` (363 MiB at the pinned snapshot, 387 MiB in the current repository)
and `opencascade` itself (156 MiB): about 105 MB to download and **about 0.53 GiB installed**, for a
feature a user may never use. That is what an `opencascade` dependency would cost users who do not
already have it. What the geometry kernel really needs is smaller, but I did not establish how much.
As I built it (no makepkg flags, and the `OCC_*` shortcut) the kernel plugin records **20 OCCT
libraries** directly (TKernel, TKMath, TKG2d, TKG3d, TKGeomBase, TKBRep, TKGeomAlgo, TKTopAlgo, TKPrim,
TKBO, TKBool, TKShHealing, TKMesh, TKFillet, TKXSBase, TKOffset, TKHLR, TKBin, TKDESTEP, TKDEIGES) and
29 with their dependencies (56.5 MB; the transitive set adds the XCAF, CAF and visualisation toolkits
and, through them, `libX11`, `fontconfig` and `freetype`). The first 17 of those, which I expect to be
the geometry's own, are **36.9 MB**; that expectation is unverified (no link or load test with only
those was run, and upstream's own CMake carries a note that it is unsure when TKBin is needed). R097.a
decides the set by linking with Arch's `makepkg` flags (which include `--as-needed`) and by loading
the kernel with only those libraries present (check L6). Decision 18 chooses between depending on
Arch's package, carrying a private copy of that set, and making OCCT optional.

**Package shape (decision 22).** The libraries add about 22 MB (three schema libraries 13.6 MB, the
mapping plugins 3.9 MB and the rest) plus the OCCT set of 37 to 57 MB, to a package whose present size
I did not measure here. Arch allows a split package from one PKGBUILD (`pkgname=(sketchyup
sketchyup-ifc)`), which keeps the default install small and confines the LGPL and OCCT obligations to
the second package; the alternative is one package that always contains it.

### 1.5 Build time and CI

**Prototype 2 (the recommended components, with converters).** In the pinned image with `boost`,
`eigen`, `nlohmann-json` and `opencascade` installed, I built the same tag with IfcParse (schemas
`2x3;4;4x3_add2`), IfcGeom, the OpenCASCADE kernel **and IfcConvert with its plugins**, no CGAL, no
Python (`-j4`, container limited to 4 CPUs and 10 GB):

| Measure | Result |
| --- | --- |
| Build, 401 compile and link steps | **916 s (15.3 minutes)** |
| Compiler memory | 620 to 885 MB resident per compiler process (sampled) |
| Staged install | 36 MB: about 22 MB of shared libraries (three schema libraries 13.6 MB, three geometry mapping plugins 3.9 MB, `IfcGeom` 1.5 MB, `IfcParse` 1.4 MB, the OCCT kernel 1.2 MB, the pass-through kernel 0.3 MB and others), about 10 MB of headers and 0.9 MB for IfcConvert |
| `RUNPATH` | `$ORIGIN/../lib` for IfcConvert (a `RUNPATH`: `LD_LIBRARY_PATH` can override it) |
| Direct run-time dependencies | `libboost_program_options` and `libboost_container`, libstdc++, libm, libgcc, libc |

The recommended configuration (no IfcConvert, no converter plugins) was **not built**, so "at most
about 15 minutes at `-j4` on a 4-CPU container" is an estimate (the schemas and mapping plugins, which
dominate, are in both). On a CI runner (4 vCPUs, two physical cores; ADR 0170 measured tests running
two to three times slower at full parallelism) a cold build should be 20 to 30 minutes. The PKGBUILD's
own comment puts the present package build at about 15 minutes. **Package acceptance builds the
package twice** (`packaging/arch/verify-package.sh` runs `makepkg` for the prior-release check at line
109 and again for the current one at line 226) with `CMAKE_BUILD_PARALLEL_LEVEL` defaulting to 1, so the
IFC cost is paid twice, and a user's default `makepkg` runs the sub-project at `-j1`: its CPU time is
bounded above by 916 s × 4 CPUs, about an hour. Package acceptance (150-minute timeout in
`native.yml`) and the package build need allowances for it, and the PKGBUILD should pass a parallelism
to the sub-project.

**How the tiers get it.** The helper is not needed by most changes, so it does not belong in every
compile.

- The sub-project builds into a **prefix** (`build/ifc-prefix`) from the vendored tree only, so its
  output depends only on the vendored files, the image's package set and the flags.
- **Fast tier.** Pull requests that do not touch `third_party/ifcopenshell/**` restore the prefix from
  an `actions/cache` entry keyed by the hash of the vendored tree, the image digest, the dated mirror
  list and the installed package set (the inputs ADR 0170's package-cache key already uses) and the
  build flags: a second cache beside ccache, because a 15- to 25-minute compile is not something to
  recover through per-object ccache hits on a cold runner. A cache miss builds it once and saves it;
  pushes to `main` and the nightly full tier keep the entry warm, and pull requests restore from
  `main`. The first pull request after a change to the tree or the image pays the cold build, which is
  well outside the five-minute target; that is accepted for a rare change. The path map gets an area
  `ifc` for `third_party/ifcopenshell/**` (rebuild the prefix; run the IFC suites) and for `src/ifc/**`
  and the IFC tests (run the IFC suites against the cached prefix). The other `src/**` rules are
  unchanged and the application binary does not depend on the prefix.
- **Full tier.** Builds the sub-project from source in the development build and in the sanitizer
  job, so a cache problem cannot hide a defect, and runs the IFC suites on X11 and Wayland. In the
  sanitizer build the worker runs **without** `RLIMIT_AS` (ASan reserves a huge shadow mapping), the
  system OCCT is not instrumented, and the sanitizer coverage of J3 is IfcOpenShell's own code.
- **Package acceptance** builds the package from the source archive in the pinned container, so the
  package build includes the sub-project, twice.
- `ci-plan.py check` gains the rule so that every new path is covered, and the line inventory gains
  the IFC test lines.

### 1.6 Process isolation (decision 19)

IfcOpenShell's parser is not bounded by us and its geometry kernel runs OCCT's Boolean and meshing
algorithms on file-controlled shapes. Upstream fuzzes the parser only. The in-house route would have
given us limits; the shipped library gives us a **process boundary** instead. The recommendation
follows ADR 0033 (`blender_job.cpp` already uses `QProcess::setChildProcessModifier` for
`PR_SET_PDEATHSIG`):

- `sketchyup-ifc-worker` is installed in `/usr/lib/sketchyup/` (not on `PATH`), run with a fixed argv
  and no shell, in a private temporary directory, in its own session (`setsid`) with
  `PR_SET_PDEATHSIG`, so it dies with the application, and the directory is removed at start-up of the
  next run if the application was killed. Its environment is scrubbed to a fixed set (the L2 test sets
  `LD_LIBRARY_PATH` itself).
- The application checks the cheap things first, in its own bounded code: the file is a regular file
  of at most 128 MiB, it ends with `END-ISO-10303-21;`, and its `FILE_SCHEMA` is one of the three
  accepted values (a bounded header scan; `.ifczip` and `.ifcXML` are refused here too). It then
  **copies the file into the private directory and hashes the copy** (or passes an open descriptor),
  so what is checked is what is read.
- The worker runs with `RLIMIT_AS` (address space), `RLIMIT_CPU` (set to four times the deadline,
  because four geometry threads use CPU time four times as fast as the wall clock) and `RLIMIT_FSIZE`,
  a wall-clock deadline enforced by the parent (terminate, then kill after one second), at most four
  geometry threads and `PR_SET_NO_NEW_PRIVS`. A crash, an out-of-memory kill or a timeout is a failed
  import that leaves the application and the current document untouched. **This contains accidents
  and resource exhaustion; it is not a sandbox.** The worker keeps the user's file and network access.
  A real sandbox (Landlock restricting it to the private directory, and seccomp denying network
  sockets) is possible on current kernels with a fallback to resource limits only; it is part of
  decision 19's recommendation as a hardening step, not a requirement of R097.b.
- **Crashes and hangs are per import, not per element.** The library has no per-element timeout (the
  iterator uses `std::async`), so one hanging or crashing Boolean ends the whole run. The worker
  reports the element it is on through a progress pipe; when it crashes or times out, the application
  restarts it once or twice with that element excluded (the iterator takes an element filter), and
  the report names the excluded elements and the reason. Failures the library reports (an exception,
  an empty result) never refuse the import; a hang or crash costs a restart, and after three restarts
  the import fails with the report so far.
- Its output is **not** IfcOpenShell's data structures: it is a bounded, versioned **IFC conversion
  package** that the application decodes with its own checked decoder (a fuzz target, section 6):
  a manifest (JSON, at most 32 MiB) with the units, georeference, spatial tree, elements, properties,
  materials and report, and a binary mesh chunk (float64 positions and uint32 indices) with a SHA-256
  in the manifest. Imports and exports both go through it.
- Progress is a short list of known phases plus the current element; cancellation is the parent
  killing the worker.

This also answers the LGPL question tidily (1.3): the MIT application is not a combined work with the
library; only the helper is.

## 2. Schema and directions

### Options

| Schema | Where it stands | Role in R097 |
| --- | --- | --- |
| IFC2x3 TC1 (`IFC2X3`) | The oldest schema still widely exchanged; its Coordination View 2.0 is described by buildingSMART's database as final and is the most widely implemented. No map-conversion entities. Geometry: faceted breps, extrusions, boolean clipping, mapped items; no tessellated face sets | Import. Export deferred |
| IFC4 ADD2 TC1 (`IFC4`) | The Reference View 1.2 (final) is a one-way coordination view whose ISO-specification examples are tessellation-based; the Design Transfer View is listed as draft. Adds `IfcTriangulatedFaceSet`, `IfcPolygonalFaceSet`, `IfcMapConversion`, `IfcProjectedCRS`. Third-party summaries map it to ISO 16739-1:2018 (not verified against the ISO catalogue) | Import and export |
| IFC4.3 ADD2 (`IFC4X3_ADD2`, "IFC 4.3.2.0") | buildingSMART states this is the ISO 16739-1:2024 version. Same building entities; adds infrastructure (alignments, linear placement, rail, road, bridge) | Import of the **building** subset only; infrastructure entities reported. Export deferred |

IfcOpenShell can also parse IFC4.1, 4.2 and the 4.3 release candidates and ADD1. The accepted
`FILE_SCHEMA` values stay `IFC2X3`, `IFC4` and `IFC4X3_ADD2`: those are the schemas with fixtures,
so those are the schemas claimed, and the other identifiers, any multi-schema declaration and any
unknown identifier are refused with the schema named. The build installs only the three schema
libraries. `.ifczip` (compressed) and `.ifcXML`/`.ifcJSON` are refused with their format named;
IfcOpenShell can read some of these, but there is no zip reader in the codebase and none is
proposed.

### Directions and claims

- **Import** is claimed per schema, and only for what the fixtures demonstrate (section 3), when
  its checks pass. The buildingSMART datasets have the same Simple-Scene in each schema.
- **Export** is IFC4 ADD2 TC1 only, tessellated. It is claimed only after the checks of section 6
  (an independent reader and a validator) pass. Export to IFC2x3 or IFC4.3 is not claimed.
- **No view or certification claim.** Export is tessellated like the Reference View's examples, but
  R097 does **not** claim Reference View conformance or buildingSMART certification. The header's
  `FILE_DESCRIPTION` states `SketchyUp export, tessellated geometry` and does not declare a
  `ViewDefinition`. Claiming a view needs the view's exchange requirements and the buildingSMART
  validation rules (the MIT `ifc-gherkin-rules` repository) to pass first; that is a later row.
- **No semantic round trip is claimed.** A file exported by SketchyUp and re-imported must
  reproduce geometry, hierarchy and properties within the stated tolerances; that is a test, not a
  promise about other tools. Native history, topology, curves, guides, annotations, sheets and
  parameters do not appear in IFC (the position of ADR 0111 and 0115).
- A direction that fails its checks at acceptance is not claimed in the support matrix, and the
  release says so. Decision 3 makes this explicit so import can ship without export.

## 3. Mapping

A guiding rule: **never guess**. An element whose placement, units or representation cannot be
interpreted is **not imported** and is listed in the report; it is never placed at the origin,
scaled by assumption or replaced by a bounding box. Every instance ends up converted, consumed as
supporting data, or counted as omitted (section 4's accounting identity). The reading is done by the
worker through IfcParse's typed entity API; the sections below say what the worker emits and what
the application builds.

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
| `IfcZone`, `IfcSpatialZone`, `IfcSystem`, `IfcGroup` and `IfcRelAssignsToGroup` | Not groups | Membership is recorded on each member as the property `ifc.Groups` (a comma-joined list of at most 2,048 bytes), and the zone or system counts as `converted` (its membership was recorded) |
| 4.3 `IfcFacility`, `IfcBridge`, `IfcRoad`, `IfcRailway` and other infrastructure spatial elements | A plain group if they contain at least one supported element; otherwise omitted | `ifc.Class` records the class; the notice states that infrastructure semantics are not interpreted |

The group tree follows **containment and aggregation**, not the placement chain; the two often
differ (an element's `PlacementRelTo` need not be its container). Each group or body therefore gets
the native transform `inverse(parent world) × world`, where `world` is the library's composition of
its `IfcLocalPlacement` chain (the worker takes it from the iterator's element transformation), so
moving a storey group moves its elements and element vertices stay in small local coordinates.
World positions are re-checked against the 1e6 m native bound. An element with two parents takes the
first in file order and gets a notice (`multipleParents`); a cycle in containment or aggregation
leaves the elements in the cycle not imported and reported. An element whose placement the library
cannot compose (`IfcGridPlacement`, `IfcLinearPlacement` in 4.3, a cycle, a non-finite value) is not
imported and is reported by `GlobalId`.

Names: `IfcRoot.Name` becomes the body name, shortened at a UTF-8 code-point boundary to 256 bytes
(the DXF importer's rule; native names allow 1,024); the full name is kept as `ifc.Name` when it was
shortened. A missing name becomes `<Class> <first 6 characters of GlobalId>`. `GlobalId` values that
are not 22 characters of `0-9A-Za-z_$` are kept as text with a notice (`invalidGlobalId`); a second
element with a `GlobalId` already seen keeps its own value in `ifc.GlobalId` and gets a notice
(`duplicateGlobalId`); the hierarchy checks use the first.

**Tags.** One tag per IFC class family of the imported elements (`IFC Wall`, `IFC Slab`,
`IFC Space`, `IFC Opening` …), at most 128 distinct tags, the rest on `IFC Other`.
`IfcPresentationLayerAssignment` layer names are kept as the property `ifc.Layer` rather than as
tags (a layer with `LayerOn` false or `LayerFrozen` true imports its bodies **hidden**, with a
notice, so the hidden state of an export survives a re-import), because a body has one tag and
class tags give the useful visibility switches. Decision 8.

### 3.2 Elements

Three tiers, so the claim is honest and the report is precise.

**Tier 1, fully specified and tested (the named subset):** `IfcWall` and `IfcWallStandardCase`
(2x3), `IfcSlab`, `IfcDoor`, `IfcWindow`, `IfcBuildingElementProxy`, and the spatial elements above.

**Tier 2, imported generically:** any other `IfcElement`, `IfcFeatureElement` (non-opening),
`IfcGeographicElement` or `IfcSpace`-like product that the library produces geometry for (beams,
columns, roofs, stairs, coverings, railings, furnishing, and so on). It becomes a geometry body with
`ifc.Class`, `ifc.PredefinedType` and `ifc.ObjectType`. The report counts these under
`importedGeneric` by class, so nobody reads a generic import as a claim of class-specific
semantics.

**Tier 3, not imported, always reported:** products for which the library yields no geometry, or
whose placement it cannot compose; `IfcAnnotation` and other 2D products, cameras and lights,
infrastructure alignments, `IfcVirtualElement`, and any class outside the accountable set. Each
class is counted in `omittedEntities`; individual elements are listed in `locations`.

**Door and window.** The body representation is imported as is: a panel, a lining or both as
modelled. `OverallWidth`, `OverallHeight` and, for IFC4, `PartitioningType`/`OperationType` are kept
as properties, with lengths converted to metres. There is no parametric door or window; geometry is
the file's.

**Openings.** `IfcOpeningElement` and `IfcRelVoidsElement` describe voids that most exporters do
**not** subtract from the host's body. IfcOpenShell's kernel subtracts them by default
(`DisableOpeningSubtractions` off). The worker records, per host, how many voids the file declares
(from `IfcRelVoidsElement`) and the library's log lines for failed subtractions (section 4), and
sets `MaxVoidsPerElement` to a bound that it reports: the library then processes **none** of the
voids of a host that has more (a skip, not a cap on work), and those hosts are listed. The application
reports `openingsDeclared`, `openingsNotApplied` (with the library's reason, bounded) and the elements
affected. The prototype did not show how a failed subtraction appears, so R097.a includes a deliberately
bad host and the worker detects "declared but not applied" by comparing declared voids with the log and,
where the file carries a `NetVolume`, with the mesh volume. An opening that is not subtracted leaves the host uncut and the opening is
imported **hidden on the tag `IFC Opening`** so the user can act on it; an applied opening is
consumed, not imported. `IfcRelFillsElement` (a window in an opening) needs no action: the window's
own placement puts it in the opening. Whether the library subtracted a void is **verified against
the file's own recorded quantities** (check E3), not assumed.

**Types.** `IfcRelDefinesByType`: the type's name becomes `ifc.Type`, and the type's property sets
are copied onto each occurrence that does not define the same property itself (the certification
test asks for type-level properties to show at occurrence level). Where the library reports that
several occurrences share one `RepresentationMap`, the representation becomes a component definition
(section 3.3).

**Classification.** `IfcRelAssociatesClassification` becomes `ifc.Classification`
(`<system>:<reference identification> <name>`, at most 2,048 bytes) and `ifc.ClassificationUri`
(the reference location, as given).

### 3.3 Geometry

Native faces are planar polygons; IFC solids are not. **Everything is tessellated** on import: the
result is ordinary editable faces and edges, never a parametric record. The work of interpreting
the representation items belongs to the library; the worker asks the iterator for the `Body`
representation of each product in the `Model` context (settings: `ContextPriorities` ordered
`Model/Body/MODEL_VIEW` first, because `ContextTypes` and `ContextIdentifiers` have no effect in
`v0.9.0`; `UseWorldCoords` off so that each element has its own local frame; `WeldVertices` on; the
opening settings of 3.2; and bounded mesher deflections), and receives, per element, a triangle mesh
with a transformation, material indices and styles, and item identifiers; with the OCCT kernel also
feature edges (the pass-through kernel emits none, so for tessellated items the adapter derives
visible edges from face-normal angles instead).

**What is claimed.** The library handles far more than this ADR can demonstrate. The claim is the
**demonstrated** set, no more, and grows by adding fixtures:

| Representation | Claimed when | Fixture |
| --- | --- | --- |
| `IfcTriangulatedFaceSet`, `IfcPolygonalFaceSet` (IFC4, 4.3) | slice b | The IFC4 and 4.3 Architecture files, the Reference View tessellation samples |
| `IfcFacetedBrep` (and shell and face-based surface models) | slice b | The IFC2x3 Architecture file (20 breps) |
| `IfcExtrudedAreaSolid` with rectangle, circle, arbitrary closed and arbitrary-with-voids profiles | slice b | The Architecture files (2 extrusions), the Reference View wall, original fixtures |
| `IfcBooleanClippingResult` and half-space clipping | slice b | Original fixture (a clipped wall) |
| `IfcMappedItem` and `IfcRepresentationMap` | slice b | Original fixture |
| `IfcRevolvedAreaSolid`, swept disks and other swept solids, parametric profiles (I, L, T, U, hollow shapes), NURBS | **not claimed** in R097 | The library may produce geometry for them; until a fixture shows it, the report says what the library returned and the support matrix does not claim it |

An element for which the kernel returns nothing (or throws) is **not imported** and is located in the
report; it is never replaced by its `Box` representation. Representation selection and the skipping
of `Axis`, `Box`, `FootPrint` and `Annotation` representations are the library's, and the report
counts what was skipped. The worker's per-item `ifc.Representation` provenance comes from the
library's item identifiers.

**What the application does with the triangles.**

- *Planar patches.* OCCT triangulates each planar BREP face into coplanar triangles. The
  application merges triangles that share an edge, whose normals agree within 1e-9 and whose
  vertices lie in one plane within 1e-7 m, into one polygon face (with holes where the outline
  requires), so a wall's side is one face, not twenty triangles, and edits behave as they do for any
  imported or drawn geometry. Edges the library reports as feature edges are kept visible; the others
  become soft, smooth interior edges, as terrain tiles do. Curved surfaces stay triangulated.
- *Planarity and weld.* Native faces must be planar within 1e-7 m and the surface welds points
  within 1e-7 m. IFC coordinates are often rounded far coarser (the fixtures use three decimals in
  millimetres, 1e-6 m), so a polygon that the merge cannot keep planar stays as triangles and is
  counted (`nonPlanarFaces`); triangles that collapse after the weld are dropped and counted
  (`degenerateTriangles`).
- *Winding and mirroring.* Orientation follows the library's output; a negative-determinant
  transform keeps the reflection in the placement and the winding is corrected, as for glTF.
- *Accuracy of curved geometry* is controlled by the mesher's linear and angular deflection, not by
  a "segments per circle" count. The worker passes the user's maximum chord deviation (default 1 mm)
  and an angular deflection of 0.5 rad (the library's default), and the report gives both. This
  replaces the DXF-style option of the first draft.
- *Shared geometry.* An `IfcMappedItem` whose representation map the library identifies as shared
  becomes one component definition and one placement per use; a non-uniform scale stays in the
  placement transform, as for glTF ([ADR 0110](0110-native-gltf-conversion.md)).
- *Limits.* A body over the native per-body limit of 100,000 vertices or faces, or a result over the
  document limits, is not imported element by element and the rest are, with the count and the limit
  named (the application enforces the document limits; the worker also stops at 600,000 triangles).

**Tolerances.** The file's `Precision` (per context) is recorded in the report and does not change
the native tolerance (1e-7 m).

**Inspectable.** Each converted body records its provenance as properties: `ifc.Class`,
`ifc.GlobalId`, `ifc.Step` (the `#id` of the instance), `ifc.Representation` (the representation type
and the item types the library reports) and `ifc.Conversion` (`tessellated`, or `tessellated, voids
subtracted`, and so on), and the report gives per-class counts and the library's messages. Nothing
in the document claims the geometry is the original parametric definition.

### 3.4 Property sets and quantities

- A property becomes `<Pset name>.<Property name>` (for example `Pset_WallCommon.FireRating`). An
  `IfcElementQuantity` becomes `<Qto name>.<Quantity name>`. IFC2x3's `BaseQuantities` keeps that
  name.
- Value types. `IfcPropertySingleValue` with a label, identifier, text, boolean, logical (`TRUE` and
  `FALSE`; `UNKNOWN` is omitted and counted), integer or real value becomes a string, bool or double.
  `IfcPropertyEnumeratedValue` becomes a string (the values joined with `, `).
  `IfcPropertyBoundedValue` becomes two properties (`.Lower`, `.Upper`). `IfcPropertyListValue`
  becomes a joined string. `IfcComplexProperty`, `IfcPropertyReferenceValue` and
  `IfcPropertyTableValue` are not mapped and are counted by class. `IfcQuantityLength`, `Area`,
  `Volume`, `Count`, `Weight` and `Time` become doubles.
- Units of numbers. Length, area and volume measures are converted to **metres, square metres and
  cubic metres** using the project's units; area and volume use the project's area and volume units,
  **not** a function of the length unit (the Simple-Scene has millimetre length, m² area and m³
  volume, so an area quantity is already in m²). Every other measure type is stored as the source
  number and its unit is listed in the report's `unitsUnconverted` table; no claim is made about it.
- Escapes and encoding. STEP string escapes are decoded by the library. ISO 10303-21 is ASCII, but
  real files carry raw bytes: the worker passes strings through a bounded UTF-8 check, and a name with
  invalid UTF-8 is decoded as ISO 8859-1 with a notice and a count (`latin1Fallback`), so a name is
  never blanked; a value that cannot be decoded at all is dropped and counted.
- Caps. At most 128 properties per body (the native limit). Up to 16 slots are **reserved** for the
  `ifc.*` provenance properties (class, GlobalId, step, representation, conversion, type, name,
  layer, material, groups, classification and so on), which are written first; occurrence-level
  properties come next, then type-level, each in pset-name then property-name order; the overflow is
  dropped and counted per pset in `propertiesDropped`. Two keys that collide after shortening keep
  the first and count the second. Property keys over 128 bytes and strings over 2,048 bytes are
  shortened or dropped with counts. In aggregate, imported property text is bounded to **8 MiB**, and
  further by what the model chunk has left: ADR 0168 measured about 200 bytes of JSON per triangle,
  so a 100,000-face model is about 20 MB of the 32 MiB chunk and a full property load could otherwise
  fail on Save. The application measures the serialized document; while it is over `min(8 MiB, chunk
  − geometry − 2 MiB)` it drops properties (type-level before occurrence-level, quantities before
  psets) and counts them. If the geometry and provenance alone exceed the chunk, the import is
  refused with the counts.
- Provenance properties (`ifc.*`) are the only non-IFC-named keys the importer writes; they count
  toward the 128.

### 3.5 Units

IFC carries its units, so unlike OBJ the importer does not ask. The worker reads
`IfcProject.UnitsInContext` through IfcParse: the length unit is an `IfcSIUnit` (`LENGTHUNIT`, with an
optional SI prefix such as `.MILLI.`) or an `IfcConversionBasedUnit` (inch, foot, with the factor read
from its `IfcMeasureWithUnit` chain, at most four levels), and the library's own unit conversion is
cross-checked against it. The result is `metresPerUnit` and whether it is exact, as in the DXF
report. Plane angle accepts radian and degree (conversion-based). Any other length or plane-angle
unit is refused. **A file with no length unit is refused** (I did not verify whether each schema makes
`UnitsInContext` optional; either way, guessing would break the no-silent-scale rule that ADR 0168
and the DXF importer already follow); the user can supply the length unit as an explicit override
(`mm`, `cm`, `m`, `in`, `ft`), reported as an override. All native coordinates are metres: the worker
asks the library for metre output (`ConvertBackUnits` off; `LengthUnit` is the library's own, not a
setting we can use) and **verifies every mesh** against `metresPerUnit` computed from the file's units,
because the library warns and assumes metres when the units are missing and the prototype's output unit
differed between files that declare the same unit (1.2); an element whose mesh disagrees by more than
1e-9 relative is not imported and is located. Area and volume units are read
for quantity properties as described above; conversion-based area and volume units (square or cubic
feet) use the same factor chain. `IfcPropertySingleValue.Unit` and `IfcPhysicalSimpleQuantity.Unit`
override the project unit for that value: a recognised override (SI or a conversion-based unit of the
same dimension) is applied, and an unrecognised one stores the number as written and is listed in
`unitsUnconverted`, so no value is converted by assumption. A conversion factor within 1e-6 relative
of a canonical value (inch 0.0254 m, foot 0.3048 m, yard 0.9144 m, US survey foot 1200/3937 m, degree
π/180) is normalised to it with a notice; any other factor is used as written and
`metresPerUnitExact` is false. (The Reference View degree sample states 0.017453293, 2.7e-8 off
π/180.) Mixed unit claims (several `IfcUnitAssignment`s in nested contexts) are refused.

### 3.6 Georeferencing

The worker reads `IfcMapConversion` and `IfcProjectedCRS` (IFC4 and 4.3) from the `Model` context.
The formula in the buildingSMART documentation is scale, then an anticlockwise rotation about Z by
`atan2(XAxisOrdinate, XAxisAbscissa)`, then translation by Eastings, Northings and OrthogonalHeight.
`IfcMapConversionScaled` (4.3) adds per-axis factors. IFC2x3 has no such entities; a community
convention puts `ePset_MapConversion` and `ePset_ProjectedCRS` on the `IfcSite`, but it is not
standardised, so IFC2x3 georeferencing is **not claimed**, and a file that has those psets gets a
notice that they were read as ordinary properties.

The mapping to the R095 site origin is exact in one case:

| Condition | Result |
| --- | --- |
| θ = 0 (`XAxisAbscissa` > 0 and `XAxisOrdinate` = 0 within 1e-12, or both absent), no per-axis factors, the `IfcProjectedCRS` `MapUnit` absent or metre, the context's `WorldCoordinateSystem` the identity, exactly one `Model` context and one map conversion, and `Scale` (default 1) equal to `metresPerUnit` within 1e-9 relative | The document site origin is set: easting, northing and elevation are Eastings, Northings and OrthogonalHeight; the CRS label is the `IfcProjectedCRS.Name` (shortened to 64 bytes); provenance is `IfcMapConversion` plus the file's SHA-256. The document then needs schema 29 and **opens only in SketchyUp 1.3.0 or later**; the report says so |
| θ ≠ 0, a factor, scale or map-unit mismatch (for example a State Plane CRS in US survey feet), several contexts or conversions | Not representable by a translation-only site origin; the report gives the reason. Decision 6 |
| A non-identity `WorldCoordinateSystem` on the `Model` context | The **file is refused**, naming the context: applying or ignoring it would both be a guess, and neither fixture nor cross-check covers it. A later slice may apply it |

Decision 6 (approved) handles a rotation as follows: (a) by default keep the project axes, set no
site origin and report the whole conversion (also recorded as read-only `ifc.mapConversion.*`
properties on the project group); (b) an opt-in "grid" mode rotates the imported model by +θ about Z
(the conversion's own rotation, so that +X is east), then sets the translation-only site origin, which
is exact for site coordinates but turns the building's walls off the model axes (the library's
`ModelRotation` and `ModelOffset` settings can do this in the worker; the application then verifies
the result against the formula); (c) giving the site origin a grid rotation, and perhaps a scale, is
the faithful answer but changes R095's core (re-anchor, `site.convert`, DXF site coordinates, Site
settings, schema) and is a separate later row. With the fixture buildingSMART certifies against (60
degrees, scale 0.001, mm units), (a) imports with no site origin and a complete report, and (b) with
the origin set.

Whichever way, `IfcSite` `RefLatitude` and `RefLongitude` (compound lists of degrees, minutes,
seconds and millionths of seconds) are converted to decimal degrees and kept with `RefElevation` as
properties (`ifc.RefLatitude` and so on), and a context's `TrueNorth` is kept as `ifc.TrueNorth`
(degrees from the Y axis) with a notice; the solar settings are not changed, matching ADR 0168's
out-of-scope ("deriving latitude/longitude from the site origin"). Coordinates beyond ±1e6 m in the
local frame (a model authored in absolute projected coordinates) are refused with the extent stated;
shifting them, with or without setting a site origin, is a later option (decision 7).

Export writes `IfcProjectedCRS` (the CRS label) and an `IfcMapConversion` (Eastings, Northings and
OrthogonalHeight from the site origin, axis (1, 0), `Scale` 1 against the metre length unit) when
the document has a site origin **and a CRS label that looks like an authority code** (`EPSG:32760`
form: letters, a colon, an identifier), since `IfcProjectedCRS.Name` is read by consumers as that
identifier. The label is free text in the document (ADR 0168), so a label that does not match, or no
label, means nothing is written and the report says why. A rotation is never exported, because the
document has none.

### 3.7 Materials and colours

The iterator resolves styles: each mesh carries material indices and, per material, a diffuse
colour, a transparency and a name, taken from the `IfcStyledItem` on the representation item
(`IfcSurfaceStyle`, `IfcSurfaceStyleRendering.SurfaceColour`, with `Transparency` as 1 − opacity)
and then from the `IfcMaterial`'s definition representation (the Architecture files' `.mtl` output in
the prototype shows this working). **Per-face colours are not resolved by the library**:
`IfcIndexedColourMap` is not read by the C++ geometry code (the prototype's sample with individual
colours produced one default material), so the worker reads the colour map itself through IfcParse and
the adapter fills `Body.faceColors` from it (check C6). IFC does not define a colour space; values are
treated as sRGB 0 to 1 and the report says so.

- **Materials.** An `IfcMaterial` becomes a native material of that name (shortened and made unique
  with a suffix, with a notice, as ADR 0110 does for glTF) carrying the colour and opacity. The
  native limit is 1,024 materials: a body past it keeps its own colour and the report counts
  `materialsOverLimit`. `IfcMaterialLayerSet`, `IfcMaterialLayerSetUsage`,
  `IfcMaterialConstituentSet` and `IfcMaterialProfileSet` are not native concepts: the element gets
  the name of the set as `ifc.Material` and the layer or constituent summary (`name:thickness`,
  joined) as `ifc.MaterialLayers`, with a notice that the layering is descriptive only. (The
  `ApplyLayerSets` setting is off, so the library does not split a wall into layers.)
- **Not imported:** textures (`IfcImageTexture` and the like), shininess, reflectance,
  lighting-model parameters and `IfcSurfaceStyleShading` beyond colour. Counted as losses.

### 3.8 Export (IFC4)

The application captures the document into a bounded **export package** (the same container as the
import package, 1.6); the worker builds the IFC4 file with IfcParse's typed entity classes and the
`hierarchy_helper<Ifc4>` template (project, site, building, storey, placements, owner history), and
writes it with `operator<<` on `ifcopenshell::file` to a private temporary file; the application checks the
size and publishes it with the non-replacing publisher the other exporters use
([ADR 0114](0114-contained-obj-export-packages.md) and the DXF/STL exporters). No geometry kernel is
used for export.

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

`GlobalId` (22-character compressed GUID, characters `0-9A-Za-z_$`). A body that was imported keeps
its `ifc.GlobalId` **if it is valid and not already used by another body in this export**; a copy of
an imported body carries the same property, so duplicates and invalid values get a derived identifier
instead and are counted (`globalIdReplaced`). Others get a deterministic identifier from the SHA-256
of a fixed prefix, the **document identity** and the body's stable ID. The document identity
(`Document::identity()`) is a random 128-bit value that the native format already stores
(`documentId`) and keeps across Save and reopen, so re-exporting the same document keeps its GUIDs
without the export editing the document (decision 16). Exporting twice gives the same file when the
timestamp in `FILE_NAME` and the owner history is injected, as `SOURCE_DATE_EPOCH` is for packages.

Numbers are written as the shortest round-trip form of the double, in STEP REAL syntax, and text with
`\X2\`/`\X4\` escapes (the library's writer does both).

## 4. Fidelity report and limits

### Report shape

The report is JSON in the existing style, `apiVersion` 1, `format` `IFC`, and a `direction`.
Messages never quote more than 64 bytes of user text; the whole report is at most 64 KiB, 64 kinds,
32 locations per kind and 256 bytes per message (the terrain report's bounds,
[ADR 0168 §1](0168-terrain-import-and-surfaces.md#invalid-input-and-bounded-reports)). Counts are
complete even when locations are truncated. It contains no host paths. It adds an `engine` block
(the library's version and commit, the kernel, the settings used, the worker's limits and whether it
ran to completion) and a bounded `kernelMessages` list (the library's warnings and errors by
category with counts, for example failed opening subtractions), so a reader can see which part of the
conversion was the library's.

```json
{
  "apiVersion": 1, "format": "IFC", "direction": "import",
  "engine": {"library": "IfcOpenShell", "version": "0.9.0", "commit": "7ba70197", "kernel": "opencascade",
             "linearDeflectionMetres": 0.001, "angularDeflectionRadians": 0.5, "completed": true},
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
               "notImported": [{"class": "IfcAlignment", "reason": "noGeometry", "count": 2}]},
  "geometry": {"bodies": 12, "vertices": 0, "faces": 0, "definitions": 0, "instances": 0,
               "byRepresentation": {"IfcTriangulatedFaceSet": 12, "IfcExtrudedAreaSolid": 2},
               "nonPlanarFaces": 0, "openingsDeclared": 0, "openingsNotApplied": 0},
  "properties": {"psets": 2, "properties": 6, "quantities": 25, "dropped": {}, "bytes": 0},
  "materials": {"materials": 8, "styledItems": 14, "layerSetsDescribed": 0},
  "accounting": {"entities": 371, "converted": 0, "supporting": 0, "omitted": 0},
  "omittedEntities": {"IfcRelDeclares": 1},
  "losses": {"textures": 0, "materialLayering": 1},
  "kernelMessages": [{"category": "openingSubtraction", "count": 0}],
  "notices": [{"code": "georeferenceRotation", "count": 1, "message": "…"}],
  "locations": [{"kind": "notImported", "step": "#1234", "globalId": "…", "message": "…"}]
}
```

(The numbers in the example are illustrative; the schema, not the values, is the proposal.)

### The accounting identity

The acceptance property that makes "never silently dropped" testable: every entity instance in the
file is in exactly one of `converted` (it produced native content), `supporting` (consumed as data of
a converted entity: points, directions, placements, profiles, units, owner history), or `omitted`
(counted by class in `omittedEntities`). The three sum to `accounting.entities`, which equals the
number of instances the library read. The worker computes it by traversing the attributes of the
converted entities with IfcParse's own traversal. `omitted` is everything left over, counted by
class in `omittedEntities`: accountable instances that were not converted (products, relationships,
property definitions, materials, styles, representation items), and also non-accountable ones that
only an omitted element referenced (a point or a profile of a skipped alignment) or that nothing
referenced. A class is counted under its own name and is never skipped; the library's parse covers
every class of the three schemas, so there is no "unknown class" case. `omittedEntities` lists at
most 128 kinds, with an `other` bucket past that, as in the DXF report.

### Refusals

Refused with nothing created: a file over 128 MiB, an unsupported `FILE_SCHEMA`, a worker that ran
out of its limits or time or crashed (reported as such, with its bounded log tail); more than the
entity or geometry limits of 6.4; a missing or unsupported length unit (without an override); a
non-identity world coordinate system on the `Model` context; coordinates beyond ±1e6 m; and the
document limits for the converted whole, named with counts. Per-element failures never refuse the
import: the element is not imported and is located in the report.

## 5. Interaction surface (high level)

This section states what the workflows must provide, not how they look. **The layout of the dialogs
needs a UX design that the owner approves** (R097's UX column is empty; decision 15).

- **Desktop.** File → Import IFC and File → Export IFC. Import offers: the source, the length-unit
  override (only when the file has none), the maximum chord deviation, the georeference mode, and
  whether spaces are imported hidden, shown or skipped; it opens a **new unsaved document** with a
  fresh recovery context (decision 10). Export offers the scope notice (hidden geometry included;
  nothing clipped; what is not exported) and a destination that cannot replace an existing file. Both
  show the report (the conversion sheet used for the other importers) with Copy and Save. Cancel and
  progress are present from the first slice (the D-044 lesson); cancel is the parent ending the
  worker.
- **CLI.** `sketchyup-cli --import-ifc FILE [--output NEW.sketchyup]` with `--ifc-deflection MM`,
  `--ifc-length-unit mm|cm|m|in|ft` (override), `--ifc-georef keep|grid` and `--ifc-spaces
  hidden|shown|skip`; without `--output` it validates and prints the report. `--export-ifc NEW.ifc
  --input doc.sketchyup` with an optional `--ifc-timestamp` for reproducible output. The
  option-isolation, non-replacement and mixed-mode rules of
  [ADR 0123](0123-native-dxf-interchange-workflow.md) and
  [0115](0115-native-obj-interchange-workflow.md) apply: unrelated options reject before
  publication, and an existing file or symlink is never replaced.
- **When IFC is unavailable.** If the helper or its libraries are missing or damaged (a package
  built without the sub-project, a deleted OCCT library), File → Import IFC reports what is missing
  and what still works (tessellated files through the pass-through kernel, which needs no OCCT;
  export, which needs no geometry kernel), and no other function is affected. The helper probes for
  the kernel before using it, because the library aborts rather than failing cleanly when a kernel
  plugin cannot load.
- **Automation (MCP) and the assistant.** The glTF, OBJ, STL and DXF importers are not `noun.verb`
  commands, and neither are their exporters. IFC files are routinely larger than the 16 MiB
  inline-data bound that `asset.import` and `terrain.import` carry (22,369,624 base64 characters), so
  an MCP import would need a different design. **R097 adds no commands, no queries and no assistant
  tools.** Imported content is visible through existing queries (`ifc.*` properties are ordinary
  entity properties). Following ADR 0169 decision 9 and ADR 0168 decision 10: the assistant's tool
  surface and its pinned baseline do not change; exposing any IFC command to it later needs a
  measured token cost and a **new corpus version** (v6) before it ships, and MCP exposure needs its
  own bounded-data design, because the worker would then be started on behalf of a remote caller.
  Decision 13.
- **Classes on native bodies.** There is no IFC-class UI in R097. A user or script sets `ifc.Class`
  and `ifc.PredefinedType` through the existing entity-properties command, and export reads them.
  Anything else exports as `IfcBuildingElementProxy`.

## 6. Verification plan

### 6.1 Fixtures

All third-party data is **untrusted input** and is never executed. Fixtures live under
`tests/fixtures/ifc/` with a `NOTICE.md` holding attribution; none is installed in the package.

| Fixture | Source and licence | Purpose |
| --- | --- | --- |
| `bsi-ifc2x3-arch.ifc`, `bsi-ifc4-arch.ifc`, `bsi-ifc43-arch.ifc` (92,542, 142,325 and 141,965 bytes) | buildingSMART International, [Certification-datasets](https://github.com/buildingSMART/Certification-datasets) `Simple-Scene/Building-Architecture.ifc`, repository commit `80d976a9` (2026-09-04). **CC BY 4.0**, from the repository's `LICENSE` ("(C) buildingSMART International Ltd. … Creative Commons Attribution 4.0"). SHA-256 values in the appendix | The three-schema certification set: hierarchy (1 project, 1 building, 1 storey, 2 spaces, 4 walls, 3 slabs, 1 roof), faceted breps in 2x3 versus tessellated sets in IFC4 and 4.3, psets and quantity sets, material association, colours, classification reference, and the 60 degree map conversion |
| `bsi-ifc4-landscaping.ifc`, `bsi-ifc4-road.ifc` (1,074,044 and 277,133 bytes) | Same repository and licence | Limits and refusal (a mid-size file); reporting of what the subset does not cover; the road file's `IfcMapConversion` has no rotation (check F1) |
| `rv-wall-with-opening-and-window.ifc`, `rv-basin-tessellation.ifc`, `rv-column-straight-rectangle-tessellation.ifc`, `rv-tessellated-item.ifc`, `rv-tessellation-with-individual-colors.ifc` (12,492, 12,131, 4,594, 5,199 and 2,593 bytes) | Same repository, folder `ISO Spec - ReferenceView_V1.2`, CC BY 4.0 by the repository licence. The file headers name another author (RDF Ltd., 2011). Decision 11 approved including them | Wall with opening and window (extrusions, `IfcRelVoidsElement`, `IfcRelFillsElement`), tessellation, per-face colours |
| Original fixtures (MIT, generated by the test sources or hand-written P21): `units-mm.ifc`, `units-m.ifc`, `units-ft.ifc`, `units-missing.ifc`, `mapconv-zero-rotation.ifc`, `mapconv-60deg-scale.ifc`, `extrusion-profiles.ifc` (rectangle, circle, arbitrary, with voids, with hand-computed volumes), `clipped-wall.ifc`, `mapped-items.ifc`, `openings.ifc` (closed and open hosts), `psets-types-escapes.ifc`, `unsupported-all.ifc` (an advanced brep, an alignment, an unsupported placement, a cycle in placements), `far-from-origin.ifc`, `limits-*.ifc` generators | The project, MIT ([ADR 0001](0001-baseline.md): original examples) | Every claim not covered by buildingSMART's set, and every refusal |
| `malformed/*.ifc` and fuzz seeds | The project | Truncation, a missing `ENDSEC`, unterminated string, bad escapes, deep nesting, a huge list, duplicate IDs, forward references, 1e308 coordinates, a 128 MiB + 1 byte file |

**CC BY 4.0 duties.** `NOTICE.md` names the author and source of each file, links the licence, states
that the files are redistributed **unmodified** (any change is indicated), and carries the IFC
trademark credit. The source archive redistributes the fixtures, which CC BY 4.0 permits with
attribution; the file headers name third-party products and, for the Reference View set, RDF Ltd.
(2011). Community sample files are excluded: buildingSMART's own README says most "do not pass
validity check", and their licence is per contributor.

### 6.2 What "independent" means now

In the first draft IfcOpenShell was the oracle. It is now the **component under test**: the library
reads the files, so checking its output against itself proves nothing. The checks below therefore use
sources that do not share its code:

1. **A raw-text scan** (`tests/ifc_text_scan.py`, run with `python -I`, regular expressions over the
   instance lines, no IFC library): entity counts per class, the unit assignment, the
   `IfcMapConversion` and `IfcProjectedCRS` values, `GlobalId`s and psets of the fixtures. It is
   crude and independent; it also sets the accounting identity's total.
2. **The published expectations and the files' own recorded values**: buildingSMART's certification
   test list (hierarchy, units, georeferencing, properties, quantities) and the quantity sets inside
   the files (`NetVolume`, `GrossArea` and the like, written by SketchUp's IFC manager in the
   Architecture files) against volumes and areas computed from our meshes.
3. **Hand-computed expectations**: original fixtures whose volumes, areas and coordinates are
   computed by hand in the fixture generator (an extruded rectangle, a circle at a stated segment
   tolerance, a wall minus a window opening, a unit conversion).
4. **Round trips**: export, then import, then compare; and the glTF, OBJ and DXF import fixtures
   exported to IFC and read back. These check our adapter, not the library.
5. **External validators, run by hand at acceptance**: the buildingSMART validation service on the
   golden exports, and a second reader if one is available (web-ifc through Node; IfcOpenShell's own
   Python `ifcopenshell.validate` is the same engine and therefore only a consistency check, and the
   wrapper is not part of the build). Results are recorded in the acceptance record, not made CI
   gates.
6. **Golden outputs**: the exports and the import reports of the fixtures are checked in, and CI
   compares bytes (with the timestamp injected), so a library or adapter regression shows as a diff
   even though CI has no second reader.

### 6.3 Checks for the Verify line

| Verify clause | Check |
| --- | --- |
| Named fixtures preserve claimed units | A1: every vertex of the mm, m and ft fixtures is within 1e-9 m of `source × metresPerUnit` (exact for m), checked against hand-computed coordinates; the report states `metresPerUnit`, exactness and any override. A2: quantity values equal the raw-text scan's with the project's area and volume units (7 area, 7 volume and 11 length quantities in the IFC4 Architecture file). A3: a missing length unit is refused, and accepted with the override, reported as one. A4: a non-SI, non-inch, non-foot length unit is refused. A5: a property or quantity with its own `Unit` is converted or listed unconverted, and a conversion-based area or volume unit converts |
| Named fixtures preserve claimed hierarchy | B1: on the three Architecture files and the Reference View wall, the group tree equals the raw-text scan's tree by `GlobalId` (project, site, building, storey, containment, aggregation), including both IFC4 sites; B2: counts per class equal the scan's; B3: the element order within a parent follows the file; B4: `ifc.Groups` equals the file's group, system and zone membership; B5: tags per class; B6: duplicate `GlobalId` in a file, two parents and a containment cycle are noticed or reported as specified; B7: a layer with `LayerOn` false imports its bodies hidden |
| Named fixtures preserve claimed properties | C1: every `Pset.Property` and `Qto.Quantity` equals the raw-text scan's (strings, booleans, doubles) with type-level properties copied to occurrences unless overridden; C2: `\X2\` and `\S\` escapes decode; C3: the 128-per-body cap (provenance reserved), the 8 MiB aggregate cap and the chunk-remainder cap drop and count, deterministically, and a document whose geometry alone exceeds the chunk is refused; C4: `ifc.GlobalId`, `ifc.Class`, `ifc.Step`, `ifc.Representation` on every body; C5: the classification reference is retained with its URI; C6: the per-face colours of `rv-tessellation-with-individual-colors.ifc` equal the file's `IfcIndexedColourMap` (read by the adapter, since the library does not) |
| Unsupported entities are reported | D1: the accounting identity holds on every fixture (`converted + supporting + omitted = entities`, with the total from the raw-text scan); D2: `unsupported-all.ifc` lists each unsupported class, with at most 32 locations and complete counts; D3: an element with an unsupported placement, a cycle or no geometry is not imported and is located; **nothing is imported at the origin**; D4: a reference to an undefined ID is a notice, not a crash |
| Converted geometry remains inspectable | E1: the whole document passes native validation (`decodeDocument(encodeDocument())`) and a save and reopen is bit-exact; E2: areas and volumes of tessellated sets and of polygonal-profile extrusions equal the hand-computed values to 1e-6 relative; a curved profile's volume is within the reported deflection of the analytic value; E3: the Reference View wall's volume equals the hand-computed value **with the opening subtracted** (and the same file with the subtraction disabled shows the larger volume, so the check is not vacuous); a host the library could not cut is left uncut with the opening hidden on its tag and located in the report; E4: coplanar triangles merge into polygon faces (a wall side is one face) and a non-planar case stays triangles and is counted; E5: a body is selectable, editable and undoable like any other (one edit makes the whole import one undo step); E6: each body's provenance properties resolve to the right source entity; E7: mesh volumes and areas agree with the files' own `NetVolume` and `GrossArea` quantities where present, within 1e-3 relative (they were computed by another program) |
| Georeferencing (claimed only as far as shown) | F1: θ = 0 and matching scale set the site origin to the E, N and H within 1e-9 m and the label; the document is schema 29 (`bsi-ifc4-road.ifc` is a ready fixture: its `IfcMapConversion` is E 729011.2258823584, N 9063960.607644705, H 0, axis (1, 0), scale 0.001, millimetre units, `EPSG:32760`); F2: the 60 degree fixture imports with `keepAxes` and no site origin, with the full conversion in the report; with `grid`, each vertex equals the hand-computed mapped coordinate minus the translation to within 1e-9 m; F3: a model beyond ±1e6 m is refused naming the extent; F4: a `MapUnit` of US survey foot, a non-identity `WorldCoordinateSystem` and two map conversions are each handled as specified (reported, or the file refused) |
| Export is demonstrated | G1: the golden exports pass the raw-text scan, re-read through the helper's own IfcParse (which type-checks attributes) and, by hand at acceptance, the buildingSMART validation service and `ifcopenshell.validate` (the Python wrapper is not built or shipped, so neither is a CI gate); entity, hierarchy, property, unit and georeference counts equal the document's; G2: an export of the glTF, OBJ and DXF import fixtures and of a document built from commands re-imports to the same bodies, vertex coordinates within 1e-9 m, hierarchy and properties; G3: two exports of one document with a fixed timestamp are byte-identical; G4: the report counts every non-exported kind; G5: a CRS label of authority-code form is required for `IfcMapConversion`, and its absence or a free-text label is reported; G6: golden exports are checked in and CI compares the bytes; G7: duplicate and invalid `ifc.GlobalId` values (a copied body) get derived identifiers and are counted |
| Determinism | H1: two imports of every fixture give identical encodings (the worker is single-threaded in tests and the library's meshing is deterministic for a given input; any difference is a defect to report upstream or pin); H2: import of an input with its instances shuffled in file order gives the same document except for `ifc.Step` and the order that file order defines (B3) |
| Limits and refusal | I1: the 128 MiB limit and an over-limit entity count refuse with the count and the limit named, creating no document; the geometry limits (100,001 faces, 10,001 bodies, 1,025 materials) refuse or report per element as specified; I2: messages ≤ 256 bytes; the report ≤ 64 KiB; I3: a worker that exceeds `RLIMIT_AS`, its CPU limit or its deadline, or is killed with SIGSEGV mid-run, is restarted with the element it was on excluded (at most three times) and then gives either a partial import that names the excluded elements or a failed import with its log tail; the application keeps running and the current document is unchanged; the generated 1,000,000-instance and 20,000-wall files of 6.4 are used to trigger the limits |
| Library packaging and licence | L1: the SketchyUp binary, the CLI and every other executable have **no** dynamic dependency on any IfcOpenShell or OCCT library (`readelf`), and only `sketchyup-ifc-worker` does; L2: replacing `libifcopenshell.parse.so` and the plugin libraries with a rebuild of the same source in a scratch directory through `LD_LIBRARY_PATH` makes the worker use them (its `--probe` prints the loaded paths), which shows the shared-library mechanism works; L3: the package contains the notices of 1.3 and the vendored tree's commit equals the pinned one; L4: `THIRD_PARTY_NOTICES`, the PKGBUILD licence array and `dependencies.json` are updated; L5: no GPL-3.0-or-later library is linked (`ldd` and `readelf` over the helper and the installed libraries); L6: the OCCT libraries the kernel plugin records are exactly the geometry subset (the build uses Arch's `--as-needed` link flags), all are in the package, and with the kernel plugin or any one of them removed the helper's probe reports "kernel unavailable" (no abort, no core dump) while export and the pass-through kernel still work |
| Parsers and fuzzing | J1: `parser_fuzz_ifc_package`, the application's decoder of the worker's output (manifest and mesh chunk), with seeds from the fixtures' packages: it must stay within its bounds and re-encode what it accepts; J2: `parser_fault_corpus_tests` mutates the certification files 512 times each through the worker's dry run **in a child process**; J3: the upstream `ifcparse_fuzzer` is built from the vendored tree with Clang and ASan/UBSan and run for at least five minutes per release, and a `worker` target (parse and geometry iteration of an input, run in the sanitizer job against the sanitizer build of the library) is run for the same time at acceptance, since upstream does not fuzz geometry; findings go upstream; J4: the harness runs every input twice and compares |
| Support claims | K1: the support matrix row, user guide section and the exchange-boundaries table state exactly the claimed schemas and directions, and name the unsupported classes as listed in the report; K2: the fixture `NOTICE.md` carries the CC BY 4.0 attribution and the IFC trademark credit |

### 6.4 Budgets

Proposed; to be confirmed by a measurement layer (as R095's were) before they are frozen. The
library's behaviour on large inputs is **not** under our control, so the budgets are enforced from
outside, by the helper's resource limits and the application's checks, and what the library does
inside them is measured and recorded.

Measured with the prototype (the library's own `IfcConvert`, on generated files, in the pinned
image; these show what the library does inside the limits):

| What | Measured |
| --- | --- |
| Parse and index 1,000,000 instances (56.6 MB) | 0.76 s, **249 MiB** resident |
| Parse and index 2,000,000 instances (114 MB) | 1.5 s, **491 MiB** resident (linear: about 245 MiB per million instances) |
| 20,000 extruded rectangular walls (220,000 instances, 11 MB, 240,000 triangles) | 11.0 s on one thread, 6.5 s on four (24 s of CPU), 178 to 182 MiB resident; the same run under an address-space limit of 512 MiB still completes |
| The five buildingSMART Reference View samples and the three Architecture files | 40 to 200 ms each |

These are **not representative of real models**: the large parse files are unreferenced
`IfcCartesianPoint` lines (the run exits with "no geometry", and the accounting traversal will touch
every instance), the walls are 20,000 identical boxes without openings, the address-space experiment
was single-threaded, and the four-thread output differed from the single-thread output in product
order. They show the order of magnitude (about 0.25 GiB per million instances; about 0.5 ms per simple
extruded element) and that the limits below have room on simple input; R097.a repeats them with the
Architecture files multiplied into a large model with openings.

Proposed limits (to be confirmed once the whole path, including the adapter, exists):

| Limit | Value | Reason |
| --- | --- | --- |
| File size | 128 MiB, checked by the application before the helper starts | The glTF package bound (ADR 0109); measured parse of 114 MB is 1.5 s |
| Entity instances | 2,000,000, counted by the worker after the parse | Measured: about 0.5 GiB resident at the limit |
| Helper resource limits | Address space 4 GiB (not in sanitizer builds); wall-clock deadline 120 s and CPU time 480 s (four threads); output file 256 MiB; four geometry threads | The library has no limits of its own; these turn exhaustion into a failed import. The 512 MiB experiment above shows the margin on ordinary files |
| Triangles from the kernel | 1,000,000 in total; 100,000 per element | The native per-body limit; the application merges coplanar triangles into far fewer faces |
| Package | Manifest at most 32 MiB; mesh chunk at most 128 MiB | Bounds the decoder |
| Geometry after conversion | The native document limits (100,000 vertices and faces, 10,000 body records, 600,000 faces materialized, 1,024 materials) | A larger file is refused with the count and the limit, or, element by element, imported partially and reported; **no IFC-specific widening of the modelling limits** |
| Properties | 128 per body; 8 MiB aggregate, and what the model chunk has left | The model chunk is 32 MiB |
| Report | 64 KiB, 64 kinds, 32 locations per kind, 256-byte messages | The terrain report |
| Parse and index a 64 MiB, 1,000,000 instance IFC4 file | ≤ 3 s (a 128 MiB, 2,000,000-instance file ≤ 6 s) | Measured 0.76 s and 1.5 s |
| Interpret, convert and commit the 100,000-face, 10,000-body model | ≤ 10 s, with progress; cancel returns control in ≤ 100 ms and ends the helper within 1 s | The measured kernel time for 240,000 triangles is 6.5 to 11 s before the adapter's merge and validation |
| Refusal of an over-limit file | ≤ 1 s for size; ≤ 3 s for the instance count | The count is known after the parse |
| Application memory during import | ≤ 1 GiB (the [R082.w v2 budget](../verification/R082w-memory-budget-v2.md)) | The decoded package plus the document being built; the helper's own memory is outside the application's process |
| Export of the 100,000-face model | ≤ 5 s, with progress and cancel; output ≤ 256 MiB | The GLB exporter's bound is also 256 MiB |

Native tests follow the established matrix: an `ifc_import_input_tests` and an
`ifc_export_input_tests` suite on X11 and Wayland at 1× and 2×, with accessibility and
keyboard-reachability extensions for the dialogs.

## 7. Out of scope

For R097: `.ifczip`, `.ifcXML`, `.ifcJSON` and IFC5/IFCX; IFC4X1, IFC4X2 and 4.3 release candidates;
4.3 infrastructure (alignments, linear placement, rail, road, bridge, tunnel, sectioned solids);
claiming advanced breps, NURBS, CSG, swept solids, revolutions and parametric profiles (the library
may produce geometry for them; they are not claimed until a fixture shows them); the CGAL and Manifold
kernels; the IfcConvert, Python and Bonsai parts of IfcOpenShell; PROJ or any CRS transform;
geographic CRS (`IfcGeographicCRS`-like) input; IFC2x3 georeferencing; export to IFC2x3 or IFC4.3;
terrain and site-surface export, and conversion of an `IfcSite` surface into a terrain record; export
of instancing, textures, annotations, sections, sheets, component parameters or curves; classification
and class editing UI; parametric doors and windows; IDS, BCF, bSDD, IfcDiff or clash tools; schedule,
cost and 4D data; round-trip identity with other tools; merging an IFC file into the open document;
referencing an IFC file as a live link; partial import by storey or class (a later row if the model
limits prove tight); MCP and assistant exposure.

## 8. Risks

- **Packaging weight and shape.** The helper, three schema libraries and a geometry kernel arrive
  with every package, plus OCCT in one of the three ways of decision 18. This is the main cost of the
  owner's choice and is quantified in section 1.
- **Build time.** An estimated 15 minutes at `-j4` (up to an hour of CPU at `-j1`) added to a package
  build that is about 15 minutes now, **paid twice** by package acceptance (estimates in 1.5). The prefix
  cache keeps it out of the fast tier; a change to the vendored tree or to the image rebuilds it, and the
  first pull request after such a change pays a cold build far beyond the five-minute target. The first
  full-tier run after this lands needs time allowances.
- **Unbounded library.** IfcOpenShell's parser and OCCT's algorithms have no limits that we set, and
  geometry is not fuzzed upstream. Mitigation: the process boundary and resource limits (1.6), restart
  with the culprit excluded, a bounded decoder in the application, and our own fuzz and crash tests. A
  defect can still crash the helper or consume its limits; it cannot, by design, touch the document. It
  is **not** a sandbox: the helper has the user's file and network access until the Landlock and seccomp
  hardening of decision 19 exists, and one hung element costs a restart.
- **The library's silent behaviours.** Found in the prototype and the source: output units that differed
  between files declaring the same unit; a warning-only fallback to metres for missing units; no
  per-face colours; the pass-through kernel returning empty geometry for an extruded wall without
  saying so; thread-dependent output order. The adapter verifies units, reads colour maps itself,
  treats empty output as "no geometry" and sorts by `#id`; each has a check. Others will exist that
  the prototype, which used `IfcConvert` rather than the API, could not show.
- **API and ABI churn.** The typed entity API and the geometry settings changed between 0.8 and 0.9;
  the schema and kernel plugins load by name at run time from a directory. The version is pinned and
  the plugin directory is private; an upgrade is a deliberate slice with the checks re-run.
- **OCCT and the Arch package graph.** Arch's `opencascade` package pulls in VTK, Tcl and Tk (about 0.53 GiB installed). Depending on it
  is simple and costs users that much; carrying a private subset (decision 18) avoids it but makes us
  distributors of OCCT (a source offer, a licence text, a rebuild when OCCT's ABI moves between
  snapshots; the pinned image makes that deterministic); making it optional leaves extrusions and
  Booleans unreadable until the user installs it. The OCCT version is also a pin: Arch has 7.9.3,
  upstream is at V8.0.1; IfcOpenShell requires 7.5.1 or later and I did not test V8.
  `IfcConvert` **aborts** instead of failing cleanly when the OCCT kernel plugin cannot be loaded
  (prototype: an uncaught `boost::wrapexcept<std::system_error>` and a core dump); whether the API the
  worker uses throws something catchable was not tested, so the worker probes for the kernel before use
  and treats its absence as a normal state.
- **Opening and Boolean failures.** OCCT Booleans fail on some real exporter output;
  `openingsNotApplied` could be common on messy models. The library's log is captured so the user is
  told, and the fixtures include a deliberately bad host.
- **Library determinism.** Meshing with threads can reorder output; tests run single-threaded and
  the application orders elements by `#id` before building the document.
- **Model limits are small for BIM.** 100,000 faces and 10,000 body records are below many real
  buildings. The Simple-Scene files are small; a typical project file will refuse or import
  partially. Honest refusal is the design; partial import by storey or class is the likely next row.
- **The 60 degree map conversion** (the certified fixture) is not representable by the current site
  origin. Without a rotation the georeference claim is partial (decision 6).
- **Schema 29.** A document with a site origin is unreadable by SketchyUp 1.2.x and earlier. The
  report and the user guide say so; exporting does not require it.
- **Property volume.** A full property load can exceed the 32 MiB model chunk. The 8 MiB cap and the
  serialized-size check are the guard; the cap may need tuning.
- **Licences.** The LGPL reading of 1.3, the redistribution of IfcOpenShell's generated schema code
  (made from buildingSMART's CC BY-ND schemas), the IFC trademark credit and the CC BY 4.0 duties of
  the fixtures need the owner's confirmation (Q1); no legal opinion is claimed.
- **Colour space.** IFC does not define one; treating values as sRGB may shift appearance.
- **Upstream health.** 2,054 open issues, a single main maintainer line and a rapid release cadence
  (0.8.5 in 2026, 0.9.0 on 2026-10-01). The pin protects us from churn; it also means security fixes
  need an explicit upgrade.

## Decisions

Decisions 1 to 16 were **approved by the owner on 2026-10-22** ("Just use IfcOpenShell. I don't want
to build it. I approve all."); decision 1 is as the owner chose it, 2 to 16 as recommended.
Decisions 17 to 22 are new, arise from shipping the library, and are proposed. Decisions 5, 12 and 14
were reworded after the blanket approval because the library changes their mechanism; question Q2 asks
the owner to re-confirm that wording.

1. **Library:** IfcOpenShell, vendored, shipped and linked dynamically; no in-house IFC reader,
   writer or geometry interpreter. *Approved 2026-10-22.*
2. **Schemas:** import IFC2x3 TC1, IFC4 ADD2 TC1 and IFC4.3 ADD2 (building subset); export IFC4 ADD2
   TC1 only, tessellated, with no view or certification claim. *Approved.*
3. **Claims:** each direction is claimed only when its checks pass; import may ship without export.
   *Approved.*
4. **Export geometry:** tessellated `IfcTriangulatedFaceSet` only; no faceted brep, no reconstruction
   of parametric solids. *Approved.*
5. **Import geometry:** tessellate everything into editable faces; openings are subtracted (now by
   the library's kernel) and reported when they are not, with the host uncut and the opening hidden.
   *Approved; wording revised for decision 1 (Q2).*
6. **Georeferencing with a rotated or scaled map conversion:** keep the project axes and report
   (default), with an opt-in grid-aligned mode; a site rotation is a separate row. *Approved.*
7. **Models beyond ±1e6 m:** refuse with the extent stated; shifting (with or without setting a site
   origin) is a later option. *Approved.*
8. **Spaces and tags:** spaces on an `IFC Space` tag that starts hidden; one tag per IFC class
   family; layers as properties. *Approved.*
9. **Properties:** flat `Pset.Property` keys, type-level copied to occurrences, `ifc.*` provenance,
   length, area and volume converted to SI, everything else stored as source numbers and reported;
   128 per body and 8 MiB aggregate. *Approved.*
10. **Import target:** a new unsaved document, like the other importers. *Approved.*
11. **Fixtures:** the buildingSMART certification datasets under CC BY 4.0, with attribution,
    including the Reference View samples; no community samples. *Approved.*
12. **Checking the result:** originally an IfcOpenShell oracle; the library is now the component
    shipped, so the checks use independent sources (6.2): a raw-text scan, published expectations and
    the files' own quantities, hand-computed fixtures, round trips, external validators by hand and
    golden outputs. *Approved; wording revised for decision 1 (Q2).*
13. **Assistant and MCP:** no new commands, queries or assistant tools; assistant exposure needs a
    new corpus version. *Approved.*
14. **Budgets:** adopt the table in 6.4, to be confirmed by measurement before they are frozen.
    *Approved; wording revised for the library (Q2).*
15. **Desktop UX:** commission and approve a UX design before R097.f starts (best commissioned once
    R097.b lands and the report is real). *Approved.*
16. **GlobalId stability:** deterministic derivation from the document identity (already stored in
    the native file) and the body's stable ID; imported GlobalIds are kept when valid and unique.
    *Approved.*
17. **How IfcOpenShell gets into the package:** vendor a trimmed, pinned source tree in
    `third_party/ifcopenshell/` and build it with our CMake as private shared libraries; not an AUR
    dependency, not a build-time download. *Proposed.*
18. **OCCT (the geometry kernel's own dependency):** carry a **private copy of the OCCT libraries the kernel needs** (the geometry toolkits, about 37 to
    57 MB, copied from Arch's `opencascade` package at package-build time and installed beside the
    IfcOpenShell libraries), with OCCT's licence text and a written source offer; not a dependency on Arch's
    `opencascade` package (about 0.55 GiB installed for users, because it pulls VTK, Tcl and Tk), and not
    an optional dependency like Blender (which would leave extrusions and Booleans unreadable until the
    user installs it). *Proposed.*
19. **Process isolation:** run IfcOpenShell only inside a separate helper process
    (`sketchyup-ifc-worker`) with resource limits, restart-on-crash with the culprit element excluded,
    and a bounded package format; a Landlock and seccomp sandbox as a later hardening where the kernel
    allows it. *Proposed.*
20. **Build time and CI:** accept the extra package-build time; give the fast tier a cached prefix
    keyed by the vendored tree; build from source in the full tier and the package job. *Proposed.*
21. **Version policy:** pin `ifcopenshell-v0.9.0`; upgrade only by an explicit change that re-runs
    the checks; the project maintains the trim of the vendored tree. *Proposed.*
22. **Package shape:** build IFC in by default in release packages (`SKETCHYUP_BUILD_IFC` on; off for
    developers who do not need it), shipped as a **separate optional package `sketchyup-ifc`** from the
    same PKGBUILD, so the default install stays small and the LGPL and OCCT obligations attach only to
    it. *Proposed.*

## Implementation split

Each slice is an independently testable review unit. The slice list is shorter than the first
draft's: parsing, geometry interpretation and writing come from the library. Slices keep documents
without IFC content at their current schema (26 to 28, or 29 with terrain) and do not touch modelling
limits.

- **R097.a: Library, helper skeleton and packaging.** Vendor the trimmed tree with the pin and
  notices; the `SKETCHYUP_BUILD_IFC` CMake sub-project (three schemas, one kernel, shared libraries,
  `RUNPATH`, flat private directory); `sketchyup-ifc-worker --probe` (versions, loaded libraries, kernel
  availability), and a **probe of the iterator, settings and writer API** that re-checks every library
  claim of 1.2 (units, voids, colours, per-element failure); the PKGBUILD (and the split package of
  decision 22),
  `dependencies.json`, notices, packaging README section and CI integration (prefix cache, path-map
  area, image packages, line inventory). **Verify:** L1–L5 and the kernel-probe part of L6 (the export
  and pass-through parts of L6 run when slices d and b land), the prefix builds in the pinned image and
  in the fast tier from the cache, the package acceptance job builds and installs the package with the
  helper, nothing else changes (every existing test passes), and the build-time and size numbers are
  recorded.
- **R097.b: Import.** The package format and its bounded decoder (`parser_fuzz_ifc_package`); the
  worker's import (units, spatial tree, placements, iterator settings, properties and quantities,
  materials, the accounting traversal, library messages) with its resource limits and deadline; the
  application's conversion (planar-patch merge, tags, properties caps, new unsaved document in one
  edit, native validation); the report v1; `--import-ifc`. **Verify:** A1–A5, B1–B7, C1–C5, D1–D4,
  E1–E7, H1, H2, I1–I3, J1, J2 on the certification set and the originals, including the crash-and-
  restart path of 1.6; the whole import is one
  undo step; the claimed representations of 3.3 each have a passing fixture.
- **R097.c: Georeferencing.** `IfcMapConversion` and `IfcProjectedCRS` in the worker, the site-origin
  mapping, the rotation decision and grid mode, far-from-origin refusal, schema-29 notice. **Verify:**
  F1–F4.
- **R097.d: IFC4 export.** The export package, the worker's writer, the mapping of 3.8, `GlobalId`,
  georeference, the export report, `--export-ifc`, the golden exports. **Verify:** G1–G7 including the
  validators; the claim is **not made** if G1 fails.
- **R097.e: Desktop workflow.** Import and Export dialogs, the report sheet, progress and cancel, the
  dirty-document prompt, recovery context, the "unavailable" states. Requires the approved UX design.
  **Verify:** `ifc_import_input_tests` and `ifc_export_input_tests` on X11 and Wayland at 1× and 2×,
  with accessibility and keyboard checks, and source-safe Save.
- **R097.f: Acceptance record.** `docs/verification/R097f-ifc-exchange.md` with a JSON summary: the
  Verify line and every check of 6.3, the budgets measured and frozen, the native matrix, sanitizers
  and the fuzz campaigns of J3, the external validator results, the support-matrix row, the user-guide
  section, and the exchange-boundaries entry for F04 (IFC). It states which directions are claimed.
  F04 is not delivered until R098 also merges and the M10 gate accepts the release.

## Reconsideration triggers

- OCCT cannot be provided acceptably (decision 18) or the helper cannot meet its resource limits on
  the fixtures: reconsider the kernel, or the in-house route of the first draft for the tessellated
  and extruded subset.
- Upstream changes its licence, drops the OCCT kernel, or a security defect needs a fix that the pin
  cannot take without an API break.
- A package for IfcOpenShell appears in an official Arch repository: switch to a system dependency.
- Owners need 4.3 infrastructure, advanced breps or NURBS: the claimed set grows with fixtures.
- The owner chooses a site-rotation row (decision 6c): the georeference mapping widens.

## Owner decisions

1. **Library:** IfcOpenShell, vendored, shipped, linked dynamically; no in-house IFC code. *Decided 2026-10-22: "Just use IfcOpenShell. I don't want to build it. I approve all."*
2. **Schemas:** import IFC2x3 TC1, IFC4 ADD2 TC1 and IFC4.3 ADD2 (building entities only); export IFC4 ADD2 TC1 only, with no Reference View or certification claim. *Decided 2026-10-22: approved as recommended.*
3. **Claims:** a direction is claimed only when its checks pass; import may ship before or without export. *Decided 2026-10-22: approved.*
4. **Export geometry:** tessellated face sets only. *Decided 2026-10-22: approved.*
5. **Import geometry and openings:** tessellate on import; the library subtracts openings, and a failure leaves the host uncut, the opening hidden and the pair reported. *Decided 2026-10-22: approved (mechanism is the library's).*
6. **Rotated or scaled map conversion:** keep project axes and report by default, with an opt-in grid-aligned mode; defer a site rotation. *Decided 2026-10-22: approved.*
7. **Models far from the origin (beyond ±1e6 m):** refuse with the extent. *Decided 2026-10-22: approved.*
8. **Spaces and tags:** spaces come in on a tag that starts hidden; one tag per IFC class family; layers become properties. *Decided 2026-10-22: approved.*
9. **Properties and quantities:** the `Pset.Property` mapping, with length, area and volume converted and other numbers stored as in the file and reported. *Decided 2026-10-22: approved.*
10. **Import target:** a new unsaved document. *Decided 2026-10-22: approved.*
11. **Fixtures:** buildingSMART's certification files under CC BY 4.0 with attribution, including the Reference View samples; no community samples. *Decided 2026-10-22: approved.*
12. **Checking the result:** independent sources instead of an IfcOpenShell oracle (the library is now the shipped component). *Decided 2026-10-22: approved (revised for decision 1).*
13. **Assistant and MCP:** no new commands, queries or assistant tools in R097. *Decided 2026-10-22: approved.*
14. **Budgets:** adopt 6.4, to be confirmed by measurement. *Decided 2026-10-22: approved (revised for the library).*
15. **Desktop UX:** commission a UX design for the Import and Export IFC dialogs and the report before R097.e. *Decided 2026-10-22: approved.*
16. **GlobalId stability:** deterministic GlobalIds from the document identity and the stable body ID; imported GlobalIds kept when valid and unique. *Decided 2026-10-22: approved.*
17. **Packaging:** vendor a trimmed pinned source tree and build it ourselves; not the AUR, not a build-time download. *Proposed.*
18. **OCCT:** carry a private copy of the OCCT libraries the kernel needs (about 37 to 57 MB) instead of depending on Arch's `opencascade` (about 0.53 GiB installed for users, because it pulls VTK, Tcl and Tk) or making it optional. *Proposed.*
19. **Process isolation:** IfcOpenShell runs only in a separate helper process with resource limits, restarts after a crash with the culprit element excluded, and a sandbox (Landlock, seccomp) as a later hardening. *Proposed.*
20. **Build time and CI:** accept the extra package-build time (about 15 minutes or more, paid twice by package acceptance) and use a cached prefix in the fast tier. *Proposed.*
21. **Version policy:** pin 0.9.0; upgrade only by an explicit change that re-runs the checks; the project maintains the trim. *Proposed.*
22. **Package shape:** IFC built into release packages by default, shipped as a separate optional package `sketchyup-ifc`. *Proposed.*

## Questions for the owner

- **Q1. Licence confirmation.** Please confirm, with counsel if you wish, three points this ADR could
  not settle: that shipping IfcOpenShell as dynamically linked LGPL libraries beside an MIT
  application, with a written source offer, satisfies LGPL-3.0 and, for the bundled OCCT libraries,
  LGPL-2.1; that redistributing IfcOpenShell's generated schema code (made from buildingSMART's
  CC BY-ND EXPRESS schemas) is acceptable; and that crediting IFC and ifcXML as buildingSMART
  trademarks in the user guide and fixture notice is enough. Also the CC BY 4.0 fixtures.
- **Q2. Re-confirm reworded decisions.** Decisions 5 (openings are now subtracted by the library, and
  how a failure shows is still to be proven), 12 (the checks no longer use an IfcOpenShell oracle) and
  14 (the budgets now describe a helper process) were reworded after your blanket approval. Please
  confirm the new wording.
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
- **Prototype 1 (parser only).** The IfcParse-only build described in section 1, commit `7ba70197`, in
  `sketchyup-ci-pinned:r094a` with `boost` 1.92.0 from the image's dated archive; command:
  `cmake -S src/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_OPENCASCADE=OFF
  -DWITH_CGAL=OFF -DBUILD_IFCGEOM=OFF -DBUILD_CONVERT=OFF -DBUILD_IFCPYTHON=OFF
  -DBUILD_GEOMSERVER=OFF -DBUILD_EXAMPLES=OFF -DCOLLADA_SUPPORT=OFF -DBUILD_BONSAIVIEWER=OFF
  -DSCHEMA_VERSIONS="2x3;4"`, then `cmake --build --parallel 4` inside a container limited to
  10 GB and 4 CPUs: 384 s, 29 build steps. No SketchyUp file was changed by it.
- **Prototype 2 (recommended components).** Same tag and image, with `boost`, `eigen`,
  `nlohmann-json` and `opencascade` 7.9.3-3 installed from the image's dated archive:
  `cmake -S src/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_OPENCASCADE=ON -DWITH_CGAL=OFF
  -DOCC_INCLUDE_DIR=/usr/include/opencascade -DOCC_LIBRARY_DIR=/usr/lib
  -DEIGEN_DIR=/usr/include/eigen3 -DBUILD_IFCGEOM=ON -DBUILD_CONVERT=ON -DBUILD_IFCPYTHON=OFF
  -DBUILD_GEOMSERVER=OFF -DBUILD_EXAMPLES=OFF -DCOLLADA_SUPPORT=OFF -DBUILD_BONSAIVIEWER=OFF
  -DSCHEMA_VERSIONS="2x3;4;4x3_add2"` (without the two `OCC_*` variables Arch's OCCT CMake
  configuration fails to load, because it refers to VTK's own dependencies), then
  `cmake --build --parallel 4` in a container limited to 10 GB and 4 CPUs: 916 s, 401 steps.
  Results used in sections 1 and 6: staged install 36 MB; `RUNPATH $ORIGIN/../lib`; the OCCT
  kernel records 20 OCCT libraries directly and 29 with their dependencies (56.5 MB; the 17 I expect to
  be the geometry's own are 36.9 MB, unverified); `IfcConvert`
  on the buildingSMART samples (OBJ output): the Reference View wall gives a wall volume of exactly
  1.5e9 mm³, which is the hand-computed 3000 × 300 × 2000 − 1000 × 300 × 1000, and 1.8e9 mm³ with
  `--disable-opening-subtractions`, so the library subtracts the opening; the window is 2e8 mm³; the
  seven `NetVolume` quantities written in the IFC4 Architecture file (6.4375, 1.26926, 1.78562,
  4.23088, 0.164702, 6.72034 and 9.36351 m³) equal the mesh volumes computed from the OBJ; the
  pass-through kernel, with no OCCT installed, reproduces a tessellated sample's mesh and returns
  nothing for the extruded wall; the OCCT kernel run **without** OCCT installed aborts with an uncaught
  `boost::wrapexcept<std::system_error>` and a core dump; the timings and memory of section 6.4. The
  generator, volume script and run scripts were kept outside the repository; they can be recreated
  from the description.
- **Packaging facts.** Arch `extra`, 2026-10-22 (package search API): `opencascade` 7.9.3
  (156 MiB installed, 34 MiB compressed; licences `LGPL-2.1-or-later` and
  `LicenseRef-LGPL-exception`; depends on `fontconfig freetype2 libglvnd libx11 tcl tk vtk` and the
  C libraries); `vtk` 9.7.1 (387 MiB installed; BSD-3-Clause); `tcl` 13 MiB; `tk` 12 MiB; `cgal` 6.2.1
  (44 MB; licences `GPL-3.0-or-later` and `LGPL-3.0-or-later`; depends on `boost eigen gmp mpfr`);
  `boost` 1.92.0 (BSL-1.0). At the pinned snapshot the packages `opencascade` adds to the image were
  `tcl`, `libxss`, `tk`, `verdict`, `vtk` (362.6 MiB) and `opencascade`: about 105 MB to download.
  OCCT upstream: V8.0.1 released 2026-07-30 (LGPL-2.1 per GitHub).
- **Licence texts.** LGPL-3.0 §4 (combined works, shared-library mechanism, installation
  information) is read from the licence text; the IfcOpenShell `COPYING.LESSER` is LGPL-3.0 and
  `COPYING` is the GPL; this is not legal advice.
- **CI and packaging scripts read.** [ADR 0170](0170-ci-tiers.md), `.github/ci-path-map.json`,
  `.github/workflows/native.yml` (ccache keyed by commit, package cache, the pinned image digest),
  `packaging/arch/PKGBUILD`, `Dockerfile.build`, `scripts/release-artifacts.sh`.
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

