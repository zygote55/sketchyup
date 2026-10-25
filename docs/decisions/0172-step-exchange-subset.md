# 0172 — STEP exchange subset

**Status: Accepted (2026-10-23).** Owner decisions 1 to 10 approved as recommended on 2026-10-23. This is the design for [R098](../PR_ROADMAP.md#r098)
("Deliver a documented STEP exchange subset"), the second exchange format of scope row
F04, and it follows on from the accepted
[ADR 0171](0171-ifc-exchange-subset.md) (the IFC exchange subset, R097). No
implementation has started.

## Context

- R098, as written in [PR_ROADMAP.md](../PR_ROADMAP.md) (Planned, Track
  Interchange, Scope F04, requires R089 and R073):
  - **Build:** select a STEP adapter and define tessellation/solid conversion
    policy, units and supported directions; explain loss of parametric/NURBS
    editability.
  - **Verify:** known solids meet dimensional/tessellation error tolerances;
    invalid or unsupported surfaces are reported without silently changing scale.
- Scope row F04, milestone M10: "Broader CAD/BIM exchange. Prioritize IFC and
  STEP use cases; publish supported entities, hierarchy/property fidelity and
  geometry conversion limits."
- State of the repository (at `origin/main 34910469`): ADR 0171 is accepted
  (2026-10-22; the owner's recorded decision: "Just use IfcOpenShell. I don't
  want to build it. I approve all."), R097.a–R097.d are merged (import, export,
  check, IFC4), R097.e–f are not. `sketchyup-ifc-worker` runs in a helper
  process with `RLIMIT_AS`/`RLIMIT_CPU`/`RLIMIT_FSIZE`, a wall-clock deadline,
  a private directory, restarts and a checked package decoder (ADR 0171 §1.6).
  The `sketchyup-ifc` package component carries the worker, the vendored
  IfcOpenShell libraries and plugins, and a **private copy of 37 to 57 MB of
  OpenCASCADE Technology 7.9.3 libraries** (`libTK*.so`, one flat directory,
  `RUNPATH $ORIGIN`, copied from Arch's `opencascade` package at package-build
  time). CI's `job:ifc` builds the IfcOpenShell prefix — which builds the whole
  OCCT 7.9.3 — from source in the full tier and restores it from the cache in
  the fast tier (ADR 0170 tiers).
- The native model (R073: public v24 / container-v2 contract). `Body`
  (`src/core/body.hpp`) is a geometry body, group, reference image or terrain,
  with a name, colour, per-face colour, material, a `tag`, `hidden`/`locked`.
  Groups are first class; properties are bounded strings (a property of at most
  2,048 bytes, a model of at most 32 MiB); there is **no first-class layer
  concept** — the IFC import keeps a layer's name as the property `ifc.Layer`
  and imports a layer that is off or frozen as a **hidden** body (ADR 0171
  §4.3). Native caps: 100,000 faces and 100,000 vertices per body, 10,001
  bodies, 10,000 unique geometry records, 600,000 materialized faces.
- STEP in one paragraph (ISO 10303-21). A STEP file is a physical file with an
  `ISO-10303-21;` header and `END-ISO-10303-21;` trailer; its `FILE_SCHEMA`
  name identifies the schema (here: AP203, AP214, AP242). Geometry is
  boundary-representation (B-rep) faces that are planar, analytic (plane,
  cylinder, cone, sphere, torus) or NURBS/BSpline; assemblies are product
  structures, and instances are `INSTANCE_OF_ITEM` /
  `ITEM_DEFINITION_WITH_TRANSFORMATION` entries carrying a local transformation.
  The length unit is declared in the shape representation (it need not be
  declared, which matters for the unit policy in §2).

## 1. Selecting the STEP adapter

### Option A — OCCT STEP translation in the shared helper (recommended)

The STEP code path uses **only OpenCASCADE**, not IfcOpenShell: OCCT's STEP
file reader and the XDE STEP translator, which the OCCT 7.9 user guide
documents as translating "the names, colours, layers, validation properties and
other data associated with shapes and assemblies" into the XDE document. A
`--import-step` / `--export-step` mode is added to the existing
`sketchyup-ifc-worker` (or to a renamed `sketchyup-exchange-worker`, decision
2); the STEP code links the OCCT toolkits `TKDESTEP`, `TKDEIGES`, `TKXCAF` and
their dependency closure, and IfcOpenShell is never on the STEP path, so the
LGPL/OCCT isolation is exactly the helper-process boundary ADR 0171 already
built.

The key fact (verified): the IfcOpenShell OCCT kernel plugin that the IFC
helper already links **already links the STEP toolkits**. The vendored
`third_party/ifcopenshell/cmake/FindOpenCASCADE.cmake` appends `TKDESTEP` and
`TKDEIGES` to the kernel's library list for OpenCASCADE 7.8.0 and newer (the
pinned version is 7.9.3), and the kernel plugin links that list. The private
OCCT set that `src/ifc/InstallOcct.cmake` installs and
`scripts/verify-ifc-library.py` verifies is the `DT_NEEDED` closure of the
kernel plugin and its dependent libraries, so it already carries the
transitive toolkits of `TKDESTEP` (`TKDE`, `TKBRep`, `TKernel`, `TKMath`,
`TKXSBase`, `TKTopAlgo`, `TKG2d`, `TKCAF`, `TKCDF`, `TKLCAF`, `TKG3d`,
`TKXCAF`, `TKShHealing`, per the `EXTERNLIB` files at tag `V7_9_3`) and of
`TKXCAF` (`TKV3d`, `TKVCAF`, `TKService`, …). R098.a confirms this with
`readelf` (and extends the verify script to assert the STEP closure); if
anything is missing it is a handful of extra `libTK*.so` in the same flat
private directory, the same `RUNPATH`, the same package.

- **Pros:** no second 37-to-57 MB OCCT runtime; no second optional package; no
  new OCCT build in CI (the IFC job already builds all of OCCT 7.9.3); the
  whole helper boundary — rlimits, deadline, restarts, private directory,
  package decoder, report shape — is reused; one exchange surface (IFC and
  STEP) behind one install decision.
- **Cons:** the worker binary grows by a few MB of OCCT STEP code; the name
  still says "ifc" (decision 2).

### Option B — a separate `sketchyup-step` helper and package

A new `src/step/`, a new `sketchyup-step-worker`, a new optional package with
its **own private copy of the OCCT libraries (37 to 57 MB duplicated)**, its
own CI job (or a shared OCCT prefix), new path rules and new packaging.

- **Pros:** clean separation; if IFC is ever dropped, STEP survives on its
  own; a smaller worker binary.
- **Cons:** a second 37-to-57 MB OCCT runtime and a second optional package; a
  user who wants both IFC and STEP — the two use cases F04 names — installs
  roughly twice the runtime; the limit/deadline/restart machinery is built
  twice (two binaries, two package decoders); the CI surface doubles. Since
  both formats need the same OCCT 7.9.3 runtime, the separation buys little
  and costs a lot.

### Option C — non-OCCT alternatives

- **Hand-written P21 parser and own tessellation.** Rejected on the owner's
  explicit decision against building the interchange code (ADR 0171, recorded:
  "I don't want to build it").
- **IfcOpenShell as the STEP adapter.** It has no STEP file reading (STEP is
  for it an IFC export format only, and internally OCCT again); it is not an
  adapter for STEP input.
- **OCP (the CadQuery OCCT binary, AGPL).** A third-party binary runtime
  instead of Arch's `opencascade` 7.9.3; AGPL conflicts with the accepted
  packaging posture of ADR 0171 (a private LGPL-with-OCCT-exception runtime);
  it is pinned to an older OCCT, not the system's.
- **Mesh-only libraries (trimesh, Manifold, and similar).** No B-rep or NURBS
  interpretation, so "convert B-rep, NURBS and analytic surfaces to a planar
  mesh with a deflection tolerance" is not met.
- **FreeCAD / OpenSCAD (AGPL).** Whole applications, not a library; wrong
  licence class for a runtime dependency.

**Not viable.**

| | A: OCCT in the shared helper | B: separate `sketchyup-step` | C: non-OCCT |
|---|---|---|---|
| OCCT runtime cost | 0 new (the IFC helper's private set already links the STEP toolkits) | 37–57 MB duplicated | 0 or a third-party binary (OCP) |
| Optional packages | one (`sketchyup-ifc`, extended) | two | one, plus OCP's AGPL posture |
| CI | reuses `job:ifc`'s OCCT build | a new job or a shared prefix | varies |
| Helper boundary | reuses the IFC helper (rlimits, deadline, restarts, decoder) | rebuilt for a second binary | varies |
| Owner precedent (ADR 0171) | matches ("use the library, don't build it") | matches, at double cost | violates |

**Recommendation: option A.**

## 2. Directions and schemas

**Import (read).** AP214 (Conformance Class 2, CD and DIS schema versions) and
AP203, plus the part of AP242 that OCCT 7.9 reads (the OCCT 7.9 user guide,
verified; see the Appendix). The AP242 read is **best-effort**: what the
OCCT 7.9.3 translator reads is read; the entities it does not (largely PMI and
GD&T content) are counted, reported with locations and **not imported**
(§5). AP214's handling of simple GD&T is obsolete in OCCT (the guide says to
use AP242 instead), so AP242 files with PMI beyond the read subset are
expected to report, not to convert.

**Export (write).** AP214 Conformance Class 2, DIS schema version (the default;
an IS-extension option exists), and AP203 (an option). **AP242 writing is not
supported**: the OCCT 7.9 user guide documents AP203 and AP214 (CC2, CD/DIS)
writer output and deprecates AP214's GD&T in favour of AP242, but it does not
document full AP242 writing, and this design does not claim it (Appendix,
unverified). A request to write AP242 is reported and refused. Export writes
millimetres (the document's unit), so there is no scale decision on export at
all.

**Units (import).** The file's declared length unit is authoritative.

- **Declared unit** → an explicit conversion to the document's millimetres,
  recorded in the report (`units`, with the factor), so the user can see it
  happened.
- **No declared unit** → the `--step-length-unit` option (`mm`, `cm`, `m`,
  `in` or `ft`, mirroring the IFC `--ifc-length-unit`) or a refusal with a
  notice.
- **Never silently changed scale.** A mismatch is reported (the import refuses
  with the declared unit named; the document is left in millimetres) and the
  user fixes it with the explicit option. No unit is ever guessed, and no
  conversion is ever applied without appearing in the report. This is the
  R098 Verify line, made mechanical.

## 3. Geometry conversion (tessellation policy)

Every imported solid is converted to a **planar triangle mesh**:

1. The worker asks OCCT to tessellate each B-rep face — planar and analytic
   (plane, cylinder, cone, sphere, torus) and NURBS/BSpline alike — with
   `BRepMesh` at the deflection and angle below.
2. The worker ships the planar triangle mesh in the package (the same package
   and decoder the IFC import uses, ADR 0171 §1.6/§3.2).
3. The application welds the mesh and merges coplanar triangles into planar
   polygon faces (the R089 glTF mechanism; ADR 0171 §3.2–3.3), so a wall's
   side is one face, not twenty triangles, and edits behave as they do for
   any native face. Faces the merge cannot keep planar stay triangles and are
   counted (`nonPlanarFaces`); collapsed triangles are dropped and counted
   (`degenerateTriangles`).

- **Defaults:** deflection **1 mm** (the same default as the IFC
  `chordDeviationMm`), crease angle **30°** (the IFC tessellation default),
  **4** geometry threads (the IFC default).
- **Per-file options:** `--step-deflection-mm` (maximum chord deviation),
  `--step-angle-deg` (crease angle), `--step-length-unit` (only when the file
  declares no length unit), `--step-schema` (export only: `ap214` default or
  `ap203`), `--step-deadline` and `--step-worker` (mirroring
  `--ifc-deadline` / `--ifc-worker`).
- **Entities that are not solids** (surfaces without volume, edges, wires) are
  counted and reported as not imported, with locations; the accounting
  identity (converted + supporting + omitted = counted) is kept, as in ADR
  0171 §3.4.

**Loss of editability, in plain words (published with the feature).**
Imported geometry is a mesh of planar polygons, not B-splines or parametric
surfaces. A NURBS/BSpline surface **cannot be edited parametrically in
SketchyUp**: the user edits the merged faces, the way they edit any
tessellated or glTF geometry. And a STEP file exported from the document
**writes the mesh only** — the file's original NURBS/parametric definition is
not preserved in the round trip. The import report and the import dialog say
this in the plain wording (mirroring the IFC user note, ADR 0171 §7).

## 4. Mapping assemblies and instances

- **The tree.** The STEP product structure becomes the native group tree,
  following containment and order; a top-level product becomes a top-level
  group named from the product name. An element with no parent goes under the
  top-level group, counted as a notice (the IFC `uncontainedElements`
  pattern).
- **Instances.** `INSTANCE_OF_ITEM` / `ITEM_DEFINITION_WITH_TRANSFORMATION`
  (an instance plus its local transformation) map to **native component
  instances** (the R089 component library): repeated instances of the same
  item become several instances of one component, with the file's local
  transformation applied as the instance placement.
- **Names.** The product or shape name becomes the group or body name
  (2,048-byte cap; over → truncated with a notice, as IFC does for
  properties).
- **Colours.** The XDE colour mode gives per-face colour; an unstyled product
  gets `DefaultMaterial` (0.7 grey) with a notice (the IFC pattern).
- **Layers.** The XDE layer becomes the property **`step.Layer`** on the body;
  a layer that is off or frozen imports its bodies **hidden** with a notice,
  so the state survives a re-import. No first-class layer concept is added
  (mirrors IFC §4.3 and the native model).
- **Tag and provenance.** Imported bodies carry the tag family `STEP` and the
  provenance properties `step.Entity` (the source entity's label),
  `step.Schema` and `step.Source` (the source file's SHA-256), so a body
  resolves back to its origin, as the IFC import does.
- **Caps.** The native caps only: 100,000 faces per body, 100,001 bodies,
  10,000 unique records, 600,000 materialized; no new caps.

## 5. Error reporting

Invalid or unsupported surfaces and entities are **reported, with locations**
(the IFC `Report` shape, reused):

- A location is the shape or entity label plus the parent assembly path, an
  OCCT transfer-status code and a message of at most 256 bytes. Sample
  locations are stored at most 32 per kind (the totals stay complete), exactly
  as `src/ifc/report.hpp` does.
- **The report** (checked by the application, ≤ 64 KiB, the IFC bound) carries
  `apiVersion` 1, the source file's SHA-256, the `FILE_SCHEMA` read, the
  declared unit and the conversion factor (when one was applied), the counts
  (shapes counted, converted, supporting, omitted, unsupported), the notices
  (each with a count), the loss map, and the kernel messages (category, code,
  severity, a sample, a count).
- **Refused with nothing created** (the IFC §6.2 pattern): a file over 128 MiB
  (checked by the application before the helper starts, together with the
  `ISO-10303-21` header and a `FILE_SCHEMA` in {AP203, AP214, AP242}); a
  worker that exceeds its `RLIMIT_AS`, its CPU limit or its deadline, or is
  killed mid-run (restarted with the element it was on left out, at most
  three times, and then either a partial import that names the excluded
  elements, or a failed import with its log tail; the application keeps
  running and the current document is unchanged); and a file with no declared
  unit and no `--step-length-unit` option.
- **Never silently changed scale** (§2): every conversion is explicit and
  appears in the report; a mismatch is a refusal, not a rescale.

## 6. Budgets

The library's behaviour on large inputs is not under our control, so the
budgets are enforced from the application side, as in ADR 0171 §6.4:

| | Budget | Rationale |
|---|---|---|
| Input file | 128 MiB, checked by the application before the helper starts (size, regular file, `ISO-10303-21` header, a `FILE_SCHEMA` in {AP203, AP214, AP242}) | The IFC precedent; the STEP lex/yacc parse is linear in the file |
| Helper resource limits | Address space 4 GiB (not in sanitizer builds); wall-clock deadline 120 s; CPU time 480 s (four threads); output file 256 MiB; four geometry threads | `src/io/ifc_worker.hpp` (120 s, 4 GiB, 4 threads, 3 restarts, 2,000,000 entities); the STEP mode takes the same limits |
| Triangles | 1,000,000 in total; 100,000 per element | The native per-body limit; the application merges coplanar triangles into far fewer faces |
| Package | Manifest at most 32 MiB; mesh chunk at most 128 MiB | Bounds the decoder (ADR 0171 §1.6) |
| Report / messages | Report ≤ 64 KiB; message ≤ 256 bytes | The IFC bounds |
| Application memory during import | ≤ 1 GiB | The R082.w v2 budget; the decoded package plus the document being built |
| Parse and transfer a 64 MiB, 1,000,000-instance file | ≤ 3 s (a 128 MiB, 2,000,000-instance file ≤ 6 s) | To be measured in R098.a before the budget is frozen (the IFC precedent: 0.76 s / 1.5 s for the same shapes) |
| Tessellate the fixture model (≤ 1,000,000 triangles) | ≤ 10 s, with progress; cancel returns control in ≤ 100 ms and ends the helper within 1 s | To be measured; the IFC kernel measured 6.5–11 s for 240,000 triangles on four threads (ADR 0171 §6.4) |
| Export of the 1,000,000-triangle model | ≤ 5 s, with progress and cancel; output ≤ 256 MiB | The IFC export precedent (ADR 0171 §6.4) |

## 7. Verification (the R098 Verify line)

The R098 Verify line — *known solids meet dimensional/tessellation error
tolerances; invalid or unsupported surfaces are reported without silently
changing scale* — is checked by:

| | Check |
|---|---|
| E1: dimensional | The self-generated corpus (§8, with analytic ground truth) imports with mesh volumes and areas within the reported deflection of the analytic values (1e-3 relative for analytic primitives; within the deflection bound for curved profiles); a curved profile's volume is within the reported deflection of the analytic value |
| E2: tessellation | Coplanar triangles merge into polygon faces (a wall's side is one face); a non-planar case stays triangles and is counted; collapsed triangles are dropped and counted; per-body and total triangle caps are enforced (100,001 faces and 1,000,001 triangles refuse or report per element, creating no partial document beyond the reported omissions) |
| E3: units | A file that declares inches imports converted, with the factor in the report; a file with no declared unit and no option refuses with the notice, creating nothing; geometry size changes **only** through a declared unit (the no-silent-scale check); an explicit `--step-length-unit` is used only when the file declares no unit |
| E4: invalid/unsupported | A fixture with broken or unsupported entities (a bad face, an entity the translator refuses, a PMI-only AP242 file) reports with locations and counts, converts the rest, and changes no scale; the accounting identity (converted + supporting + omitted = counted) holds |
| E5: round trip | Export (AP214, then AP203) of the corpus, then re-import: geometry within the reported deflection, names, colours, `step.Layer`, hidden state and the assembly/instance structure all survive |
| E6: limits and refusal | The 128 MiB + 1-byte file refuses with the count and the limit named, creating no document; a worker that exceeds `RLIMIT_AS`, its CPU limit or its deadline, or is killed with SIGSEGV mid-run, is restarted with the element it was on excluded (at most three times) and then gives either a partial import that names the excluded elements or a failed import with its log tail; the application keeps running and the current document is unchanged |
| E7: native | The whole imported document passes native validation (`decodeDocument(encodeDocument())`) and a save and reopen is bit-exact (R073); one edit makes the whole import one undo step; each body's provenance properties (`step.Entity`, `step.Schema`, `step.Source`) resolve to the right source entity |
| E8: report | Messages ≤ 256 bytes; the report ≤ 64 KiB; the counts are complete past the 32-per-kind location bound |

**Test fixtures.**

1. **Primary: a self-generated corpus** (no third-party licence question).
   Parametric primitives with analytic ground truth (box, cylinder, cone,
   sphere, torus), NURBS/BSpline solids and blends, and a multi-body assembly
   with names, colours, layers and repeated instances — generated in a
   documented one-off step with the **same OCCT 7.9.3 writer that ships**
   (AP214 and AP203), and committed to `tests/resources/step/` with a
   `STEP-SOURCE.txt` recording the generating code, the schema, the OCCT
   version and each file's SHA-256.
2. **Round trip:** the same corpus through the application's export (E5) —
   the writer's own output, so a failure is unambiguously ours.
3. **Third-party freely licensed corpus:** a small (3–5 file) set, finalised
   in R098.a with **named sources and licences** recorded in `STEP-SOURCE.txt`.
   Candidates, with their verification status at design time: FreeCAD test
   data (the project is LGPL-2.1-or-later; whether it ships `.step` test
   files usable here is **not verified**); NIST CAD interoperability test
   files (hosting **not verified**); OCCT's own test resources — **not in the
   OCCT GitHub repository** (verified: tag `V7_9_3`'s `tests/` has no `step`
   directory and there is no `resources/` directory), distributed separately
   and **not verified**. If none can be licensed cleanly, the corpus stands on
   1 and 2, which E1–E8 do not depend on.

## 8. CLI, packaging and CI

- **Application CLI** (mirroring `--import-ifc` / `--export-ifc` in
  `src/automation/main.cpp`): `--import-step <file>` and `--export-step
  <file>`, with `--step-deflection-mm`, `--step-angle-deg`,
  `--step-length-unit`, `--step-schema`, `--step-deadline` and
  `--step-worker`. The interactive import dialog mirrors the IFC import
  dialog (progress, cancel in ≤ 100 ms, the report view with the loss note).
- **Worker:** the existing `sketchyup-ifc-worker` gains the `--import-step`
  and `--export-step` modes on top of its `--import` / `--export` / `--check`
  ones, reusing its `--rlimit-as` / `--rlimit-cpu` / `--rlimit-fsize`
  machinery, the private directory, the restarts and the checked package
  decoder (the mesh chunk and manifest bounds of §6). The STEP mode links
  `TKDESTEP`, `TKDEIGES`, `TKXCAF` and their closure; IfcOpenShell is not on
  the STEP path.
- **Packaging:** the `sketchyup-ifc` component is extended: the worker (now
  with STEP), the IfcOpenShell libraries and plugins, and the private flat
  OCCT set (verified to carry the STEP closure; any missing `libTK*.so` added
  to the same directory, the same `RUNPATH $ORIGIN`, the same
  `--as-needed` posture, so nothing shipped records a Boost dependency). No
  new optional package (decision 9). `share/doc/sketchyup/` gains
  `STEP-SOURCE.txt` beside the existing `IFC-SOURCE.txt`.
- **CI:** no new OCCT build — `job:ifc` already builds OCCT 7.9.3 as part of
  the IfcOpenShell prefix (fast tier restores the cached prefix; the full
  tier builds it). The path map gains nothing structural: the STEP code lives
  in `src/ifc/**` (already `job:ifc`) and the corpus in
  `tests/resources/step/`, consumed by the IFC job's test shards; the
  sanitizer shards run the STEP path through the helper (a separate process,
  as the IFC import is), with the application side sanitised as usual.

## 9. Implementation split

- **R098.a** — Fixtures and import. The self-generated corpus and
  `STEP-SOURCE.txt`; the worker's `--import-step` mode (read AP203, AP214 and
  the best-effort AP242; the unit policy of §2; `BRepMesh` tessellation; the
  §5 report; the application's `--import-step` and dialog); the `readelf`
  verification of the STEP closure in the private set and the verify-script
  update. Verify: E1–E4, E6 (import half), E7, E8; the §6 budgets measured
  and frozen.
- **R098.b** — Mapping. Assembly/instance, name, colour, layer, tag and
  provenance mapping (§4), the caps, the loss note and the report's unit
  fields. Verify: E4, E5 (structure half), E7 (provenance).
- **R098.c** — Export. `--export-step` (AP214 default, AP203 option, mesh
  write in millimetres, the AP242 refusal) and the E5 round-trip check.
  Verify: E5, E3 (export half), E8.
- **R098.d** — Hardening and publication. Packaging and CI hardening (the
  PKGBUILD closure check, the `STEP-SOURCE.txt` install, the path map); the
  third-party corpus finalised with named sources and licences; the published
  documentation (the F04 "supported entities, hierarchy/property fidelity and
  geometry conversion limits" note, the plain wording of the editability loss
  in the user guide and the import dialog, the support-matrix row). Verify:
  the R098 Verify line in full; the M10 gate rule.

## Implementation notes (R098.a skeleton, 2026-10-23)

The worker skeleton landed first of R098.a: `--probe-step` (the §2 read report
plus the write round trip), `--write-step` (the §7 corpus) and `--import-step`
(the §5 metadata: schema family, units, entity count, roots, per-shape bounds
in mm, status and refusal codes). The `BRepMesh` tessellation and the §4 mapping
remain in R098.b/c; this is the verified boundary they build on.

- **Empirical (the probe on the pinned OCCT 7.9.3 build, recorded 2026-10-23):**
  the writer's default schema is AP214IS — the header's `FILE_SCHEMA` reads
  `AUTOMOTIVE_DESIGN { 1 2 10303 214 0 1 1 1 }` (the §2 "no static `GetID()`":
  the schema is walked from the `HeaderSection_FileSchema`); the `WriteUnit`
  default is `UnitsMethods_LengthUnit_Millimeter` (the file declares millimetre,
  0.001 m, angle RADIAN); the AP242 option is
  `DESTEP_Parameters::WriteSchema = WriteMode_StepSchema_AP242DIS` and its output
  reads back as `AP242_MANAGED_MODEL_BASED_3D_ENGINEERING_MIM_LF {1 0 10303 442 1 1 4 }`
  — the standard identifier of the AP242 schema: the AP242 MIM is published as
  **part 442** of ISO 10303, so the classifier maps 442 → `ap242` — with 350
  entities and the same geometry (the box's bounding box (0,0,0)-(10,20,30),
  6 faces; the cylinder 3 faces; the assembly 2 solids; 350 entities in both). `STEPControl_Reader::ReadFile`
  returns the `IFSelect_ReturnStatus` (there is no `Status()` method in 7.9.3),
  and the read flags live on `DESTEP_Parameters` (not the ADR-era
  `StepData_ConfParameters`). The 7.9.3 `STEPControl_StepModelType` enum replaces
  the ADR-era `STEPControl_ASO`/`STEPControl_ASOAll`; `TKDESTEP` carries both the
  basic `STEPControl` and the XDE `STEPCAFControl` layers (both in its PACKAGES).
  The §2 decision — AP242 writing is refused by the application and reported —
  stands: the library exposes the write path but the 7.9 user guide documents
  AP214 (default) and AP203 only.
- **The private set grows by 11 libraries, 18,427,712 B (17.6 MiB
  uncompressed): TKDESTEP 7,858,320 · TKV3d 3,110,976 · TKXSBase 2,001,072 ·
  TKService 1,300,528 · TKHLR 1,148,976 · TKXCAF 915,504 · TKLCAF 866,360 ·
  TKCAF 505,904 · TKCDF 351,328 · TKVCAF 223,288 · TKDE 145,456** (measured with
  readelf on the real build; `TKDESTEP` records no TKDEIGES, so the set is
  `TKDESTEP + TKXCAF + their transitives`). The 26-library total (the kernel's
  15 + these 11) is what `InstallOcct.cmake` now installs — the closure is the
  **union of the kernel plugin's and the worker's** `libTK*` `DT_NEEDED` (the
  worker records 9 of them directly: TKernel, TKMath, TKBRep, TKMesh, TKTopAlgo,
  TKPrim, TKDESTEP, TKXSBase, TKXCAF; the rest arrive transitively). `TKV3d`
  pulls the X11/font stack, so the ifc package's `depends` gains zlib, bzip2,
  libpng, brotli, expat, libx11, libxcb, libxau, libxdmcp, fontconfig and
  freetype2 (L5's `allowed_owners` carries the same list) — this ADR expected
  the ifc package's `depends` to be `sketchyup`, `gcc-libs` and `glibc` only;
  the X11/font list is the measured correction. Nothing records a Boost
  library (the `--as-needed` link and L5 check it); no VTK, Tcl or Tk.
- **A contradiction with ADR 0171, accepted:** because the worker now links the
  OCCT libraries itself, a missing private `libTK*` stops the **whole worker
  from starting** (exit 127, the loader naming the library): the UI shows
  "Broken" instead of ADR 0171's "Limited" (the helper missing, the IFC
  conversion available), and IFC import is lost along with STEP. Before, the
  worker linked only the IfcOpenShell libraries, and a missing OCCT library
  kept the worker running with the kernel reported unavailable (exit 0). The
  verify script's L6 now accepts the new behaviour (a library the worker links
  stops the worker; a kernel-only library still reports the kernel unavailable).
  Accepted trade-off: all 26 libraries ship in the same package, so a missing
  one means a damaged install; and the R097.e1 "Limited" state stays reachable
  when the kernel **plugin** is missing (the worker still starts, the kernel is
  reported unavailable, the pass-through kernel works).
- **Verification:** `tests/ifc_step_tests.cpp` (C++, through the worker) and
  `tests/ifc_step_worker_tests.py` (Python) cover the corpus, the round trip and
  the refusals; the three corpus files are committed under `tests/fixtures/step/`
  with `STEP-SOURCE.txt` (the CTest entries `ifc_step`/`ifc_step_worker`; the
  verify script's L6 checks the 26-library union on the installed tree).
- **The unit policy and what a plain OCCT read really does (review round 2, 2026-10-24):**
  `STEPControl_Reader::FileUnits` resolves the unit the file's unit context assigns
  to the shapes: it walks each root's `ShapeDefinitionRepresentation` to the shape
  representation's `ContextOfItems`, and `findUnits` only accepts the combined context
  wrappers (`GEOMETRIC_REPRESENTATION_CONTEXT_AND_GLOBAL_UNIT_ASSIGNED_CONTEXT` and the
  uncertainty variant) - a bare `GLOBAL_UNIT_ASSIGNED_CONTEXT` entity is not resolved.
  A conversion-based file resolves the conversion unit (its name as the file wrote it,
  e.g. `INCH`; the `ValueComponent` of the `LENGTH_MEASURE_WITH_UNIT` as the factor),
  not the base unit the raw text's first `LENGTH_UNIT` declares. And the transfer is the
  unit conversion: `TransferRoots` normalizes the geometry to the cascade length unit
  (millimetre by default: a metre-declaring box of 10 x 20 x 30 comes out
  10000 x 20000 x 30000, an inch-declaring one 254 x 508 x 762) - a file the reader
  resolves no unit for comes out in the file's own units. A plain `ReadFile` never
  sets the model's `LocalLengthUnit` (it stays 1.0; the DESTEP XCAF provider is the
  only thing that sets it, and `SystemLengthUnit` reads it), so the probe resolves
  the factor from the `FileUnits` name (the SI unit as `getSiName` spells it - a
  lowercase prefix, the base `metre` - with the `ConvertSiPrefix` set; plus inch
  0.0254 and foot 0.3048 m). A conversion-based file then imports at the conversion
  unit: the committed `tests/fixtures/step/inch-units.step` (the corpus box, the
  writer's geometry and header, with the context's `GLOBAL_UNIT_ASSIGNED_CONTEXT`
  replaced by a hand-written `CONVERSION_BASED_UNIT('INCH')` unit block - the writer
  writes the combined context wrappers the reader follows) comes out of the reader at
  254 x 508 x 762 mm (the import's geometry scale is 1.0; the measured box is
  -1e-7 to 254.0000001 - the file's 1e-7 uncertainty and float noise ride the box,
  so the test asserts it at the 1e-6 default tolerance), the raw text's millimetre
  base stays visible in the report (`units.length`), and a notice says the file
  applies the conversion unit. A file with a conversion unit but no unit context the
  reader resolves (a shapeless sample) imports at the raw text's first declaration
  (1000 x its factor in metres, flagged); a file the raw text cannot convert and the
  model resolves no unit for is refused (a unit is never guessed - decision 4). The
  raw-text scan is kept for the `unitsDeclared` fact and the raw name report only (its
  factor is the raw text's first declaration, the import's fallback); the `unitIn`
  scan's `find` checks were inverted before this (a present `SI_UNIT` was treated as
  absent and vice versa), so the raw report of a conversion-based entity was wrong.

## Implementation notes (R098.a2, 2026-10-24)

The tessellation and the conversion package land in the same helper: `--import-step`
gains `--output PKG` (plus `--step-deflection-mm`, `--step-angle-deg`,
`--step-max-triangles`, `--step-max-total-triangles`; `--step-length-unit` as in the
skeleton). The unit policy of the skeleton stands, and the transfer is the unit
conversion: the reader's geometry is in the unit the file applies, the package
coordinates are in metres, and the only scale step is `metresPerUnit = mmPerUnit
* 0.001`. Each transferred solid is tessellated with
`BRepMesh_IncrementalMesh(solid, deflection, Standard_False, angle, Standard_False)`
(the constructor performs the meshing; not parallel, so the output is deterministic)
and collected per face (`BRep_Tool::Triangulation` with the face's
`TopLoc_Location`; the winding flipped when exactly one of the face's `TopAbs_REVERSED`
orientation and the location's mirror handedness applies — a reversed face under a
mirror location is reversed twice and faces out — so the solid's triangles face out); a
collapsed triangle is counted as
degenerate, never silently dropped. The package is the existing writer: one mesh per
transferred solid, one `IfcStepSolid` node per solid flat under one `IfcStepAssembly`
root (the assembly and instance mapping is R098.b), every triangle's material the
package's `none` (STEP colour and material mapping is R098.b), no edges, no colours.
A solid beyond the package's 1e6 m coordinate bound refuses the whole file (as the
IFC import's `farFromOrigin`), not a clipped mesh. The caps of decision 6 are the
options' defaults (100,000 per element, 1,000,000 total): a solid over the per-element
cap is omitted and reported, a file over the total cap stops converting and says so
in the report (`triangleCapApplied` / `triangleBudgetApplied`), and a file in which
nothing converted is refused with nothing written (`nothingImported`). Solid-less
shapes are omitted and reported (`no-solid`); their free faces are reported as not
imported (`free-surface`), so the accounting identity holds with the solids as the
counted entities.

- **Verified (the pinned OCCT 7.9.3 build, the worker's own packages, the
  application's checked decoder from `src/io/ifc_package.cpp`):** the three corpus
  packages decode — box 1 mesh, 24 vertices, 12 triangles, volume 6e-6 m^3 and area
  2.2e-3 m^2, both exact to the analytic values; cylinder 92 triangles, volume
  1.55291e-6 m^3 (0.989x the 1.5708e-6 analytic) and area 7.81817e-4 m^2 (0.995x
  the 7.85398e-4 analytic); the assembly 2 meshes, 104 triangles, the sum of the two.
  The committed `inch-units.step` comes out 0.254 x 0.508 x 0.762 m (volume
  0.0983224 m^3, exact): the reader's 25.4 mm-per-unit conversion stands, and the
  declared unit wins over a mismatching `--step-length-unit` (`units.applied` is
  `declared`). A file the reader resolves no unit for and no option for is refused
  (`units`, a unit is never guessed); with the option it imports at the option's
  unit (`units.applied` is `option`). A deflection of 0 refuses (`deflection`); a
  solid beyond 1e6 m refuses (`farFromOrigin`); the per-element cap refuses a file
  in which the only solid would exceed it (`nothingImported`, nothing written), and
  the total cap omits the excess solids with the report's notice. Without `--output`
  the import is the skeleton's metadata, unchanged. `tests/ifc_step_tests.cpp` and
  `tests/ifc_step_worker_tests.py` cover it (E1: the three corpus packages through
  the application's decoder against the analytic volume and area — 1e-3 for the box
  and the inch box, the deflection's tolerance for the cylinder — and the inch box
  at 0.254 x 0.508 x 0.762 m; the caps; the unit policy; the metadata unchanged).
- **Section 6, measured (the pinned build, single threaded, 8 GiB address space,
  2026-10-24, re-measured the same day against a generator that wires each copy as a
  distinct product):** the fixture model (the 3-file corpus, 104 triangles) tessellates in
  0.03 s wall (read included) — the budget of 10 s with progress holds with a wide
  margin. A generated 79.8 MiB file — 5,000 products, each a fresh
  `PRODUCT`/`PRODUCT_DEFINITION_SHAPE`/`SHAPE_DEFINITION_REPRESENTATION` pair on its own
  `AXIS2_PLACEMENT_3D` (the box solid 500 m away from the previous copy), 1,690,012
  entities — parses and transfers in 13.89 s (peak 799 MiB), and packages the 5,000
  transferred solids (60,000 triangles) in 18.08 s with no `ERR` from the reader and
  5,000 of 5,000 converted. **The parse budget of 3 s (64 MiB) is missed** (13.89 s at
  79.8 MiB, about 11 s at 64 MiB scaled linearly); the 128 MiB case cannot be measured at
  all (a 10,000-copy file is 140.5 MiB, over the 128 MiB input refusal). The IFC
  precedent (0.76 s for 1,000,000 instances) is a different format's cost curve, and this
  measurement is the STEP one the budget line asked to be measured before it is frozen.
  An earlier 78.3 MiB measurement (6.41 s parse) on a first draft of the generator is
  discarded: that draft left the repeated solid blocks unreferenced by any
  `PRODUCT_DEFINITION_SHAPE`, so the reader deduplicated them to one shape and the number
  measured the parse, not the transfer; the wired 5,000-product file above is the honest
  one.
- **The mirrored solid (the review's winding question):** a placement in the step subset
  is a proper isometry — a det -1 placement is not expressible (`AXIS2_PLACEMENT_3D`
  carries a right-handed frame), and a degenerate one is normalised away by the reader —
  so a mirror-image solid reaches the tessellator only as a valid B-rep, and its winding
  is the face's own orientation (the `TopAbs_REVERSED` flag) under an identity location;
  the mirror-location branch of the flip rule stands as the safety net for a shape that
  carries one. Two fixtures put the box's coordinates at a negative x (the hand-crafted
  `mirror-box.step` with its 3D points negated, and the OCCT-writer-generated
  `box-negative.step` translated to x in [-10, 0]); both transfer as one solid, six faces,
  and tessellate to the box's exact analytic volume (6e-6 m^3, positive signed volume) and
  area (2.2e-3 m^2) — the triangles face out on the mirrored layout.
- **The node names the application sees (R098.b input):** the package nodes are the
  alphanumeric classes `IfcStepAssembly` (root) and `IfcStepSolid` (one per solid), and the
  application's import names an unnamed body after its node class and tags it `Step
  Solid` — so an R098.a2 import shows every body as `IfcSolid` with the `Step Solid`
  tag, not the file's product name. R098.b maps the STEP product name to the body name
  and chooses the tag naming deliberately (the `Ifc`-prefixed class name is deliberate for
  R098.a2: it must be an `Ifc`-prefixed alphanumeric name the app's tree accepts, and it
  keeps the STEP tree distinguishable from an IFC import).
- **The 7.9.3 tessellation API, as the code uses it (the ADR-era names verified
  against the `V7_9_3` headers):** `BRepMesh_IncrementalMesh`'s constructor performs
  the meshing (no separate `Perform` call is made), with the linear deflection in the
  cascade unit (millimetre) and the angular deflection in radians; `Standard_True` /
  `Standard_False` are macros in `Standard_TypeDef.hxx` (there is no `Standard_True.hxx`);
  `TopoDS_Face`'s orientation is the inherited `TopoDS_Shape::Orientation` (a face's own
  class does not define it); `Poly_Triangulation::Node` is 1-based and `Poly_Triangle`
  node access is 1-based as well (the two index spaces are the common 7.9 trap);
  `TopExp_Explorer` is the shape traversal (the `TopoDS_Shape` of 7.9.3 has no
  convenient child iteration); and `TopoDS::Face` needs `TopoDS.hxx` (the `TopoDS`
  free-function header), not the shape header alone. The worker records no TBB in its
  `DT_NEEDED` (the pinned OpenCASCADE 7.9.3 libraries are built without it), so the
  tessellation runs single threaded (the engine report says so) — decision 5's 4 threads
  is the IFC kernel's own pool, which OCCT's `BRepMesh` does not use; a build that
  ships the TBB runtime is where a parallel `BRepMesh` would pick up the pool.

## Implementation notes (R098.b, 2026-10-25)

The §4 mapping lands in the same helper and the application, on the R098.a2 package:

- **Worker (`--import-step` with `--output`).** The flat `IfcStepSolid`-per-solid layout of
  R098.a2 becomes the §4 tree: the XDE read (the XCAF document the XDE STEP translator
  fills) walks the product structure; a product with a shape becomes a node (an assembly
  `IfcStepAssembly`, a part `IfcStepSolid`), one mesh is tessellated once and every
  instance of it references the same mesh (the shared-mesh design of the placement-carrying
  node transform, R098.a's commit `27ac62cc`), the instance's placement rides the node's
  transform and the basic reader's instanced bounding box comes out chained (a 10 x 20 x 30
  mm part at (50, 0, 0), (0, 50, 0) and (0, 0, 50) reaches (60, 70, 80)). The names, the
  colours, the layers and the visibility come from the XDE document (`XCAFDoc_ShapeTool`
  name, `XCAFDoc_ColorTool` colour and `IsVisible`), the provenance per §4
  (`step.Entity` the shape's tag, `step.Schema` the file's schema as written, `step.Source`
  the file's SHA-256, on the bodies; `step.Entities` the entity count on the root), and an
  unstyled product takes the package's `DefaultMaterial` (0.7 grey) with a notice.
- **Application (`importStepFile`).** The same helper boundary the IFC import uses:
  a cheap source check first (regular file, ≤ 128 MiB, `ISO-10303-21;`, one `FILE_SCHEMA`
  in {AP203, AP214, AP242}, classified by the `10303-XXX` number the worker's own
  `classifySchema` uses — the schema name carries its version, e.g. `AUTOMOTIVE_DESIGN
  { 1 0 10303 214 1 1 1 1 }`), then the private copy **keeps the file's base name** (the
  conversion names the root after it), `sketchyup-ifc-worker --import-step` under the same
  rlimits and deadline (the worker's `--import-step` gains the IFC side's `--rlimit-*`
  machinery), a whole-file retry (the conversion is one kernel transfer, so a stopped run
  is retried on the file, at most `maxRestarts`, not a per-element exclusion), a refusal
  (exit 2 or 3 with the JSON's `status` `refused`) surfaced as a small report with the
  source's `sha256` / `bytes` / `schema`, and the package decoded and converted through
  the existing `Converter`, which for a STEP package takes the tag family `STEP` (the class
  name with the `IfcStep` prefix off and CamelCase spased, `STEP Other` at the 128-family
  cap), the edit label `Import STEP`, the STEP notice wordings, and — for a shared-mesh
  instance — the part's provenance on the canonical member (the placed bodies are its exact
  copies by the document's component invariant, so the `step.*` keys ride there, and a
  placed body that diverged is a validation error the import refuses, not a silent import).
- **Verified (the pinned OCCT 7.9.3 build, the shared prefix cache):** the self-generated
  corpus in `tests/fixtures/step/` (the R098.a box, the instanced assembly through the XDE
  writer, the named/coloured/layered corpus through it) imports with the §4 facts — the
  one shared mesh and three instance transforms, the 2,048-byte name clip with the
  `nameTruncated` notice and the class-name fallbacks, the `DefaultMaterial` and colour
  mapping with `colorAlphaDropped`, the `step.Layer` property and the hidden state (the
  layer's visibility does not round-trip the 7.9.3 XDE writer/reader, so the hidden-state
  evidence is the shape's `IsVisible`, and the writer's `PRESENTATION_LAYER_ASSIGNMENT`
  writes `visible` regardless of the source layer's state — recorded, the shape visibility
  is what the mapping uses), and the application-side check: the instanced assembly through
  `importStepFile` (save and reopen bit-exact, one history entry labelled `Import STEP`,
  one component definition and three instances, the `STEP Assembly` / `STEP Solid` tags and
  no `IFC` tag, the `step.*` provenance, the report ≤ 64 KiB) and the unit refusals (no
  declared unit and no option refuses with `units`, a nonsense option refuses with
  `option`, a declared unit stands over a nonsense option). `tests/ifc_step_tests.cpp`
  (C++, through the worker) and `tests/ifc_import_tests.cpp` (the application) cover it;
  the local sets run green (the CI IFC set 6/6, the sanitize set 5/5 under ASan, where the
  tests run without `ulimit -v` — ASan's shadow reservation needs more address space than
  an 8 GiB virtual limit gives, and the docker memory cap bounds the run).

1. **The adapter (options A/B/C):** **option A** — OCCT STEP translation in
   the shared helper, on the IFC helper's private OCCT set. *Rationale: it
   costs no second 37–57 MB OCCT runtime, no second optional package, and
   reuses the whole IFC helper boundary the owner already accepted.*
2. **The helper's name:** keep `sketchyup-ifc-worker` and add the STEP modes
   (renaming it `sketchyup-exchange-worker` is the alternative). *Rationale:
   R097 already shipped the name; renaming reworks packaging paths, CI and
   documentation for no functional gain.*
3. **Schemas:** import AP203 + AP214 (+ best-effort AP242, the rest reported);
   export AP214 (default) + AP203; no AP242 writing. *Rationale: it is the
   documented envelope of the OCCT 7.9 writers, and AP242 is mostly PMI,
   which a mesh model does not carry anyway.*
4. **Units:** no declared unit → the explicit option or a refusal; a declared
   unit → converted and reported; nothing ever guessed. *Rationale: it makes
   the R098 Verify line — "without silently changing scale" — mechanical.*
5. **Default tolerances:** deflection 1 mm, crease angle 30°, 4 threads.
   *Rationale: the same defaults the user already has for IFC, so one mental
   model covers both formats.*
6. **Caps:** 1,000,000 triangles total / 100,000 per element; 128 MiB input;
   256 MiB output. *Rationale: the native per-body caps and the IFC
   precedent, so there are no new caps to learn.*
7. **Editability loss:** mesh import with the plain-language note; re-export
   writes the mesh only. *Rationale: the native model is mesh-based (the R089
   glTF precedent); saying so plainly is the R098 Build line.*
8. **Layers:** the property `step.Layer` plus hidden state, mirroring IFC.
   *Rationale: the native model has no first-class layers; one mechanism
   serves both formats.*
9. **Packaging:** extend `sketchyup-ifc`; no new optional package.
   *Rationale: one exchange runtime behind one install decision, and F04 names
   IFC and STEP as the pair.*
10. **Fixtures:** the self-generated corpus (analytic ground truth) as the
    primary verification basis, plus a small third-party corpus with named
    sources and licences, finalised in R098.a. *Rationale: the core checks
    carry no licence risk; real-world files are added on top, and none of
    E1–E8 depends on them.*

## Appendix: evidence and sources

**Verified** (against `origin/main 34910469` or the named upstream source):

- The worker's CLI and its `--rlimit-as` / `--rlimit-cpu` / `--rlimit-fsize`
  flags: `src/ifc/worker_main.cpp` (`--import`, `--export`, `--check`,
  `--threads`, `--chord-deviation-mm`, `--length-unit`, `--entity-limit`,
  `--max-voids`, `--exclude-ifc`, `--diagnose-from`, `--diagnose-seconds`).
- The budgets: `src/io/ifc_worker.hpp` (deadline 120 s, address space 4 GiB,
  four threads, `maxRestarts` 3, `maxEntities` 2,000,000) and
  `src/io/ifc_worker.cpp` (128 MiB input check, `RLIMIT_CPU` at four times the
  deadline, output 256 MiB via `--rlimit-fsize`, the restart loop).
- The report shape: `src/ifc/report.hpp` (counts, notices, losses, at most 32
  locations per kind with complete totals, kernel messages) and
  `src/ifc/import.cpp` (the JSON: `apiVersion`, `sha256`, `entities` /
  `converted` / `supporting` / `omitted`, `losses`, `notices`,
  `unitsUnconverted`).
- The private OCCT set: `src/ifc/InstallOcct.cmake` (records the `libTK*`
  closure by `DT_NEEDED` from the kernel plugin and its dependent
  libraries); `scripts/verify-ifc-library.py`
  (`LIBRARY_PATTERNS` = `libifcopenshell.`, `ifcopenshell_`, `libTK`);
  `packaging/arch/PKGBUILD` (the flat private `libTK*.so` directory,
  `RUNPATH $ORIGIN`, copied from Arch's `opencascade` package at
  package-build time; the package depends on `sketchyup=$pkgver`, `gcc-libs`
  and `glibc`; linked libraries use `--as-needed`); `dependencies.json`
  (OpenCASCADE Technology 7.9.3, Arch `opencascade 1:7.9.3-3`, licence
  `LGPL-2.1-or-later WITH OCCT-exception-1.0`, optional package
  `sketchyup-ifc`); ADR 0171 (37 to 57 MB; 55 MB installed measured).
- The IFC kernel plugin already links the STEP toolkits:
  `third_party/ifcopenshell/cmake/FindOpenCASCADE.cmake` (the base list —
  `TKernel`, `TKMath`, `TKBRep`, `TKGeomBase`, `TKGeomAlgo`, `TKG3d`,
  `TKG2d`, `TKShHealing`, `TKTopAlgo`, `TKMesh`, `TKPrim`, `TKBool`, `TKBO`,
  `TKFillet`, `TKXSBase`, `TKOffset`, `TKHLR`, `TKBin` — plus `TKDESTEP` and
  `TKDEIGES` appended for OpenCASCADE ≥ 7.8.0; the pinned version is 7.9.3)
  and `third_party/ifcopenshell/src/ifcgeom/kernels/CMakeLists.txt` (the
  kernel plugin links `OpenCASCADE_LIBRARIES`, filtering out `TKXMesh`).
- OCCT at tag `V7_9_3` (upstream GitHub): `adm/MODULES` lists the
  `DataExchange` toolkits including `TKDESTEP`, `TKDEIGES`, `TKXCAF` and
  `TKXSBase`; `src/StepFile` and `src/DE` exist; `src/XSTEP` does **not**
  (verified: HTTP 404). The `EXTERNLIB` files (V7_9_3 spells the file
  `EXTERNLIB`, not `EXTERNLIB.cmake`): `TKDESTEP` → `TKDE`, `TKBRep`,
  `TKernel`, `TKMath`, `TKXSBase`, `TKTopAlgo`, `TKG2d`, `TKCAF`, `TKCDF`,
  `TKLCAF`, `TKG3d`, `TKXCAF`, `TKShHealing`; `TKXCAF` → `TKBRep`, `TKernel`,
  `TKMath`, `TKService`, `TKG2d`, `TKTopAlgo`, `TKV3d`, `TKCDF`, `TKLCAF`,
  `TKG3d`, `TKCAF`, `TKVCAF`.
- The OCCT 7.9 STEP user guide (dev.opencascade.org, `occt-7.9.0`): the
  basic translator reads files produced according to AP214 (Conformance Class
  2, CD and DIS schema versions), AP203 and some parts of AP242; the basic
  writer's output conforms to AP203 or AP214 (CC2, CD or DIS) depending on
  the user's option; the XDE STEP translator translates "the names, colours,
  layers, validation properties and other data associated with shapes and
  assemblies"; AP214's handling of simple GD&T, tolerances and datums without
  presentations or modifiers is obsolete (use AP242); writing exposes
  `SetColorMode` / `SetNameMode` and a `Transfer` with a
  `STEPControl_StepModelType` (i.e. the `STEPControl_Writer` family); physical
  reading uses `step.lex` / `step.yacc` in `StepFile`; user-defined attributes
  are read for shapes at part/product level, component instances and geometry
  level. The OCCT 7.9 reorganization returns an option for the IGES/STEP
  toolkits to avoid XCAF data storage (so `TKDESTEP`/`TKDEIGES` can be linked
  without the CAF stack at runtime — the STEP mode here links `TKXCAF`
  deliberately, for the name/colour/layer transfer).
- CI: `.github/ci-path-map.json` (`job:ifc`, the "IFC library and helper" job
  of ADR 0171) and `.github/workflows/native.yml` (the IFC job; the fast tier
  restores the cached IfcOpenShell prefix, the full tier builds it from
  source).
- The application CLI: `src/automation/main.cpp` (`--import-ifc`,
  `--export-ifc`, `--ifc-deflection`, `--ifc-length-unit`, `--ifc-spaces`,
  `--ifc-georef`, `--ifc-deadline`, `--ifc-worker`).
- The native model: `src/core/body.hpp` (a `Body` is a geometry body, group,
  reference image or terrain, with a name, colour, per-face colour, material,
  a `tag`, `hidden`/`locked`); ADR 0171 §2.2/§4.3 (the group tree, the
  `ifc.Layer` property, the hidden state of an off or frozen layer, the
  2,048-byte property cap, the 32 MiB model cap).
- The roadmap: the R098 row (Planned, Track Interchange, Scope F04, requires
  R089 and R073), the F04 scope row, R073 ("Stabilize the native format and
  migration tooling", Delivered; public v24 / container-v2 contract), and the
  R097 status (R097.a–d merged, e–f not; the owner's recorded decision: "Just
  use IfcOpenShell. I don't want to build it. I approve all.").
- No STEP test fixtures in the OCCT GitHub repository: the GitHub API at tag
  `V7_9_3` shows `tests/` with no `step` directory and no `resources/`
  directory (HTTP 404).

**Not verified** (flagged; resolved during implementation):

- **AP242 writing in OCCT 7.9.** The user guide deprecates AP214's GD&T in
  favour of AP242 but does not document full AP242 writing; this design
  does not claim AP242 output (decision 3).
- **The exact STEP writer class names of OCCT 7.9.3.** `src/XSTEP` and the
  `namespaceXSTEP` documentation page do not exist at `V7_9_3` (verified:
  HTTP 404); the 7.9.0 user guide refers to the `STEPControl_Writer` family
  (`STEPControl_StepModelType`, `SetColorMode`, `SetNameMode`, `Transfer`) and
  the XDE (`STEPCAFControl`) variant. The exact 7.9.3 C++ API names are
  pinned in R098.a against the `V7_9_3` source, not the 7.9.0 documentation.
- **The extra file count/size of the STEP toolkits relative to the already
  shipped private set.** The evidence above indicates zero (or a handful of)
  extra `libTK*.so`; the `DT_NEEDED` check is done in R098.a and recorded in
  `STEP-SOURCE.txt`.
- **The third-party STEP fixtures.** NIST CAD interoperability test files:
  hosting not verified. FreeCAD test data: whether it ships usable `.step`
  test files not verified (an unauthenticated code search was not available at
  design time). OCCT's own test resources: not in the GitHub repository
  (verified), distributed separately, not verified.
