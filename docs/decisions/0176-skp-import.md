# 0176 — SketchUp (.skp) import

**Status: Accepted (2026-10-25).** The owner approved decisions 2 to 9 as recommended on
2026-10-25; decision 1 (the reader) was his own choice earlier that day. This is the design
for [R102](../PR_ROADMAP.md#r102) ("Deliver SKP support only after feasibility passes"),
scope row C01, import direction only. For **reading** `.skp`, it supersedes
[ADR 0003](0003-compatibility-gaps.md)'s "unsupported" stance. Writing stays unsupported. The
owner asked for `.skp` import on 2026-10-25 ("We need a way to import .skp files"). No
implementation has started.

## Context

- **ADR 0003 (2026-10-03) found no route.**
  - Trimble's SketchUp C SDK ships only for Windows and macOS. There is no Linux library.
  - Its redistribution terms are unclear. Forum guidance points to the developer terms and
    to asking Trimble.
  - The project had no `.skp` corpus.
  - R102 was therefore conditional on feasibility.
- **What changed: an open reader now exists.** [OpenSKP](https://github.com/iamahsanmehmood/openskp)
  is an MIT-licensed reader built by reverse engineering the format, with no Trimble SDK.
  - It reads both containers: the VFF container (SketchUp 2021 and later) and the MFC
    `CArchive` container (2013–2020).
  - It is written in Python, TypeScript, .NET, Dart and **C++17**.
  - It is active: release 1.3.0 on 2026-09-18, 763 commits, latest commit `23a15e8` on
    2026-10-22.
  - It is not affiliated with or endorsed by Trimble.
- **Alternatives considered:**

  | Route | Licence | Versions read | Verdict |
  |---|---|---|---|
  | OpenSKP C++ (vendored, in a helper) | MIT | 2021+ VFF; 2013–2020 MFC (weaker in C++, below) | **Chosen (decision 1)** |
  | Trimble C SDK under Wine | Trimble developer terms; redistribution unclear | All | Rejected: Windows-only binary, Wine dependency, unclear right to ship |
  | [hew3d/openskp](https://github.com/hew3d/openskp) (Rust, clean room) | GPL-3.0-only | 2017 and 2026 only | Rejected: GPL. SketchyUp is MIT, and ADR 0171 already ruled out GPL code |
  | Documentation-only workflow (export glTF, IFC or STEP from SketchUp) | — | — | Rejected by the owner |

- **The spike (2026-10-25, scratch only, nothing committed).**
  - **Build:** the C++ package (`packages/cpp`) built in `sketchyup-ci-pinned:r097a-ifc`.
    It needed miniz, tinygltf and flatbuffers fetched ahead of time, because the image has
    no git.
  - **Bundled samples:** all 32 unique `.skp` files in the repository parse. Most are
    synthetic, and a few are real models.
  - **An owner-supplied real model**, `BanyanHarbor.skp` (SketchUp 2024, `{24.0.553}`),
    parses in 0.16 s:
    - root: 174 vertices, 278 edges and 101 faces, 2 of them with holes;
    - one component definition, "Teddy": 1,087 vertices and 58 faces, 3 with holes, plus
      one instance placement;
    - 18 named colour materials.
  - **Faces:** every face's edge loops chain closed, and every face is planar to
    8.9e‑13 in.
  - **Visual check:** the library's own GLB export, rendered in Blender, shows the expected
    floor plan: 8 ft walls, 13.4 × 8.3 m overall, with the scale figure upright and coloured.
  - **The file stays local.** The owner said not to commit it. It is kept in the git-ignored
    `.local/skp-corpus/` for local fidelity checks only.
- **Known weakness: the C++ reader of older files.** Upstream
  [issue #442](https://github.com/iamahsanmehmood/openskp/issues/442), open since
  2026-10-21, reports these failures for the TypeScript, .NET, Dart and C++ readers on a
  real multi-version project:
  - V4–2016 files fail with "text delimiter not found";
  - 2017–2020 files fail with "expected a string record";
  - 2021 and 2025 (VFF) files parse in every reader.

  The Python reader parses all of them except V3. A partial port of the fixes landed in
  `23a15e8`.
- **The native model already matches SketchUp's model closely**, so the import can be
  nearly one to one, not a mesh conversion like IFC and STEP:
  - Faces are planar polygons with an outer loop and inner loops (holes):
    `Surface::Face::loops` in `src/geometry/surface.hpp`. SketchUp faces are the same.
  - Loose edges are wires.
  - Edges carry hidden, soft and smooth flags (`EdgeAppearance`).
  - Faces carry front and back materials (`MaterialSides`) and per-side texture mappings.
  - Materials have a colour, an opacity and an optional image asset (`MaterialRecord`).
  - Tags have visibility and folders (`src/core/tags.hpp`).
  - Groups and component definitions with instances exist (R089, `src/core/components.hpp`).
  - Native caps: 100,000 faces and 100,000 vertices per body, 300,000 edges per body,
    10,001 bodies, 10,000 unique geometry records and 600,000 materialized faces.
- **What OpenSKP's C++ API exposes** (`include/openskp/model.hpp`):
  - `parse_skp(ByteBuffer)` returns `SkpModel`, with the model's units, layers (name,
    colour, hidden) and materials (name, RGBA, transparency, optional embedded texture
    bytes with their size).
  - Definitions hold vertices, edges (soft, smooth, hidden) and faces: loops of oriented
    edges, a normal, front and back material, front and back UV transforms, hidden, layer.
  - Each instance has a name, a GUID, a definition reference, a 13-value placement, a
    layer, a material and a hidden flag.
  - Also exposed: section planes, texts, dimensions, construction lines and points,
    pages (scenes), styles, the camera and attribute dictionaries.
  - **Placement layout:** a row-major 3×3 matrix in values 0–8, the translation in inches
    in values 9–11, and a scale factor *w* in value 12. The library's own point transform
    ignores *w*; it was 1 in every file seen so far.

## 1. The reader and where it runs (decision 1, owner-chosen; decision 2)

- **Vendor OpenSKP's C++ parser, pinned to one commit,** under `third_party/openskp/`, with
  a `VENDOR.txt` naming the commit and its SHA-256 sums, as `third_party/ifcopenshell` does.
  - Keep only the parser sources (the model, the VFF and MFC containers, attribute
    decoding) and miniz (MIT) for the VFF zip container.
  - Leave out the exporters, writer and scene baker, and their tinygltf and flatbuffers
    dependencies. The C++ package's CMake builds them all together, so the vendor script
    needs its own source list. Proving that the parser compiles alone is the first task of
    R102.a.
  - Moving the pin is a reviewed change, re-run against the corpus.
- **The parser runs only in a helper process** (decision 2). It is a reverse-engineered
  parser of untrusted binary input, so the application never links it:
  - The new helper is `sketchyup-skp-worker`.
  - It reuses ADR 0171 §1.6's isolation: a private 0700 copy, a scrubbed environment,
    `setsid` and `PR_SET_PDEATHSIG`, `RLIMIT_AS`, `RLIMIT_CPU` and `RLIMIT_FSIZE`, a
    wall-clock deadline, cooperative cancel, and the `P`/`R` line protocol.
  - It writes a bounded package (§3) that the application decodes with a checked decoder.
- **Packaging.** The helper ships in the **main package**, not in the optional
  `sketchyup-ifc` component.
  - It needs no OpenCASCADE or IfcOpenShell: its only dependencies are the vendored
    parser, miniz and the C++ runtime.
  - `.skp` import therefore works in every installation.
  - `THIRD_PARTY_NOTICES` gains OpenSKP (MIT) and miniz (MIT).
  - `share/doc/sketchyup/SKP-SOURCE.txt` records the pin.

## 2. Versions (decision 3)

| Saved by | Container | Import |
|---|---|---|
| SketchUp 2021 and later | VFF | **Supported.** Verified on the corpus (§7) |
| SketchUp 2013–2020 | MFC `CArchive` | **Best effort**, labelled as such. A parse failure is a clear refusal ("This SketchUp 2017 file could not be read; save it from SketchUp 2021 or later and try again"), never a partial model |
| Before 2013 (V3–V8) | MFC | Refused with the same advice. They can be accepted later if the C++ reader matches the Python one (upstream #442) |

- The version comes from the file's header string (`{24.0.553}`), and the report records it.
- A file from a newer SketchUp than the corpus covers is attempted and labelled "newer than
  tested".

## 3. The package: polygon faces, not triangles (decision 4)

The IFC package (`SKIFCP01`) carries triangle meshes and feature edges. For `.skp` that
would throw away what makes the import worth having: whole faces with holes, soft and smooth
edges, back materials and UVs. The coplanar merge would then have to guess the faces again.

- **Proposal: a new, separately versioned package, `SKSKPP01`.**
  - It has the same envelope as `SKIFCP01`: a magic number, the manifest length, the
    geometry chunk length, a JSON manifest at most 32 MiB long, a geometry chunk at most
    256 MiB long, and the chunk's SHA-256 in the manifest.
  - **Per definition** (the root model is definition 0):
    - vertices: f64 ×3, in **metres** (inches × 0.0254 exactly);
    - edges: two vertex indices plus a flag byte (soft, smooth, hidden, wire);
    - faces: a material for each side, a hidden flag, a tag index, and the loops (outer
      first) as vertex index lists, each in SketchUp's orientation;
    - UVs for each side: the 3×3 UV transform as stored, or per-vertex UVs once resolved;
      §4 decides which.
  - **Instances:** definition index, a 4×4 placement in metres, name, GUID, tag, material
    and hidden.
  - **The manifest's tables:** tags (name, colour, hidden), materials (name, RGBA,
    opacity, texture reference), definitions (name, GUID), and textures. Texture images go
    in a separate bounded chunk as PNG or JPEG bytes, each at most 16 MiB and at most
    4096 × 4096 after decoding.
- **A checked decoder in `sketchyup_io`,** with its own fuzz target
  `parser_fuzz_skp_package`, mirrors `decodeIfcPackage`. Every index is range-checked,
  every loop needs at least 3 vertices, and every count is bounded before allocation.
- **Rejected alternative:** reusing `SKIFCP01` and triangulating in the helper. It is
  simpler, but it loses face identity and SketchUp's edge flags.

## 4. Mapping to the native model (decision 5)

| SketchUp | SketchyUp | Notes |
|---|---|---|
| Root model geometry | A top-level group named after the file | The import adds one undoable entry, "Import SketchUp" |
| Face (outer loop + holes) | A native face with the same loops | Loops are kept as they are. A face the native validator rejects (self-intersecting, collapsed, or non-planar beyond 1e‑7 m after conversion) is dropped and counted (`invalidFaces`), with samples |
| Edge flags soft / smooth / hidden | `EdgeAppearance` | Exact |
| Loose edge | Wire | Exact |
| Construction lines and points | Guides | When the native guide kinds match; otherwise counted and omitted |
| Front and back material | `MaterialSides` | The default material (none) stays none |
| Material colour, opacity, texture | `MaterialRecord` with an image asset | Texture bytes are re-encoded to PNG when they are not PNG or JPEG |
| Face UV transform | Native texture mapping | **To verify in R102.b:** whether OpenSKP's stored transform maps directly to the native `TextureMapping` or must be resolved to per-vertex UVs. Untextured faces are unaffected |
| Component definition and instances | A native component definition and instances | Shared definitions stay shared, so editing one edits all |
| Group | A native group | SketchUp stores a group as a one-instance definition. A definition used once and flagged as a group becomes a group |
| Placement (3×3 + translation + *w*) | Native `Transform` | Translation × 0.0254. *w* ≠ 1 divides the translation and is checked against fixtures with scaled instances before release. A mirrored placement keeps face orientation |
| Layer / tag | A native tag of the same name | Hidden tags import hidden. "Layer0" / "Untagged" maps to Untagged |
| Instance and face hidden flags | `hidden` | Exact |
| Model units | The document's display units | The geometry is always converted inches → metres; display units are a preference |
| Instance and definition names | Body and definition names | Capped at 1,024 bytes (the native cap on body, definition, tag and material names), with a `truncatedNames` notice when cut. (First written as 2,048, which the native validator rejects; corrected in the R102.b review.) |
| Attribute dictionaries | Properties `skp.<dictionary>.<key>` | Only string, number and boolean values, within the property caps. Dynamic-component behaviour is **not** imported |
| Scenes, styles, section planes, texts, dimensions, the camera | Not imported (R102.a/b) | Counted in the report. Scenes and section planes are candidates for a later slice |

- **Caps.** The native caps apply. A definition over 100,000 faces is refused by element
  with a notice, the IFC pattern: the rest of the model imports, and the report names what
  was left out.
- **Accounting identity:** imported + omitted = counted, for faces, edges, instances and
  materials.

## 5. Errors and the report

The report reuses the IFC report shape (`apiVersion` 1, ≤ 64 KiB, messages ≤ 256 bytes, 32
sample locations per kind). It records:
- the format version and container;
- the source file's SHA-256;
- the model units;
- the counts for §4's accounting;
- the notices (`invalidFaces`, `truncatedNames`, `omittedScenes` …);
- a plain-language line for anything best-effort ("SketchUp 2017 files are read best-effort").

**Refused with nothing created:**
- a file that isn't `.skp`, checked before the helper starts by its header string;
- a file over the size budget;
- a version outside §2;
- a parser failure;
- a helper that exceeds its limits.

## 6. Budgets (decision 6)

| | Budget | Rationale |
|---|---|---|
| Input file | **256 MiB**, checked before the helper starts | Real `.skp` files with photo textures exceed IFC's 128 MiB |
| Helper | 4 GiB address space, 120 s deadline, CPU 480 s, output 512 MiB | The IFC limits, with more room for output because of textures |
| Package | Manifest ≤ 32 MiB; geometry ≤ 256 MiB; textures ≤ 256 MiB in total, 16 MiB each | |
| Parse time | To be measured in R102.a on the corpus | The spike: 145 KiB in 0.16 s |
| Application memory during import | ≤ 1 GiB | The R082.w budget |

## 7. Verification and corpus (decision 7)

- **Committed fixtures** (CI) are synthetic only:
  - files made with OpenSKP's own writer (MIT; the generator script is committed with
    them);
  - hand-built packages for the decoder.

  A fixture written and read by the same library proves regressions, not fidelity, so
  fidelity is checked against real files.
- **The owner's real files** live in the git-ignored `.local/skp-corpus/` and are never
  committed. A local fidelity script compares each import's counts (faces, holes,
  definitions, instances, materials, tags) and bounding box against OpenSKP's own reading
  and a reference export. Its results are recorded in the implementation notes; only the
  numbers are recorded, never the files.
- **Checks:**
  - **S1:** the face, hole, edge, definition, instance, material and tag counts equal
    the parser's;
  - **S2:** the bounding box in metres within 1e‑6 m of the parser's × 0.0254;
  - **S3:** a component edited once changes all its instances;
  - **S4:** hidden tags hidden, soft and smooth edges kept;
  - **S5:** textures shown on the right faces (visual check against a SketchUp
    screenshot, once textured files are in the corpus);
  - **S6:** every refusal of §5;
  - **S7:** the package decoder fuzzed, and the helper under ASan and UBSan in the
    sanitizer shard.
- The corpus wanted from the owner: files from SketchUp 2013–2020, textured models,
  nested, scaled and mirrored components, hidden tags, and one large model for timing.

## 8. Licence, naming and wording (decision 8)

- OpenSKP and miniz are MIT, compatible with SketchyUp's MIT licence. Their notices ship
  with the package.
- The feature is "Import SketchUp (.skp)…". The user guide says that SketchUp is a
  trademark of Trimble Inc., that SketchyUp is not affiliated with or endorsed by Trimble,
  and that the reader is an independent open-source implementation, so some files may not
  import exactly.

## 9. Implementation split

- **R102.a — Reader and package.**
  - Vendor the parser subset.
  - Build `sketchyup-skp-worker`: the `--probe` and `--import FILE --output PKG` modes,
    the limits and the report.
  - Add the `SKSKPP01` package and its checked decoder, plus the fuzz target.
  - Add the synthetic fixtures and their generator.
  - Verify: S1, S2 and S6 on fixtures; parse budgets measured on the corpus.
- **R102.b — Native mapping.**
  - Faces with loops, edges and flags, wires, groups and components, tags, materials and
    textures, UVs, names, attributes, as in §4.
  - Verify: S1–S5 on fixtures and locally on the owner's corpus.
- **R102.c — Application and CLI.**
  - File › Import SketchUp (.skp)… reuses the R097.e1 import screens: the options are
    minimal (none required), with progress, cancel and the report.
  - Add `sketchyup-cli --import-skp`, with `--skp-deadline` and `--skp-worker`.
  - Verify: the dialog suite (as `step_import_dialog_tests`) and the CLI tests.
- **R102.d — Hardening and publication.**
  - Packaging (PKGBUILD, notices, `SKP-SOURCE.txt`) and the sanitizer and fuzz lines.
  - The user-guide page with the version table and the limits; the support-matrix row.
  - The pin re-checked against upstream fixes for #442.

## Owner decisions

1. **Reader:** vendor OpenSKP's C++ parser (MIT), pinned. *Chosen by the owner on
   2026-10-25.*
2. **Isolation and packaging:** run the parser only in a new `sketchyup-skp-worker` helper
   (sandboxed as in ADR 0171 §1.6), shipped in the main package, not the optional IFC
   component. *Approved 2026-10-25.*
3. **Versions:** 2021+ supported; 2013–2020 best effort with a clear refusal on failure;
   before 2013 refused. *Approved 2026-10-25.*
4. **Package:** a new polygon-face package `SKSKPP01`, not triangles in `SKIFCP01`.
   *Approved 2026-10-25.*
5. **Mapping:** native faces with holes, components, groups, tags, materials and textures
   as in §4. Scenes, styles, section planes, texts and dimensions are not imported in R102.
   *Approved 2026-10-25.*
6. **Budgets:** a 256 MiB input file and the §6 limits. *Approved 2026-10-25.*
7. **Corpus:** CI uses synthetic fixtures only. The owner's files stay local
   (`.local/skp-corpus/`) and are never committed. *Approved 2026-10-25 (the owner's rule for BanyanHarbor.skp).*
8. **Wording:** "Import SketchUp (.skp)…", with the trademark and non-affiliation note.
   *Approved 2026-10-25.*
9. **Import only:** no `.skp` export. OpenSKP writes only the pre-2021 container, so an
   export would produce old files. *Approved 2026-10-25.*

## Implementation notes (R102.a)

R102.a is implemented on the branch `feat/r102a-skp-reader` (the slice of the PR that this
ADR waits on). Findings and measured numbers, 2026-10-26.

**The vendored subset** (`third_party/openskp/`, ADR 0176 decision 2): 30 files at OpenSKP
commit `23a15e8742e936694bf14aa029cb0e2862dce5b9` (archive
`5b304cc024b2d72504ac5a6676968166fdd300f180d3f1078efe8b959951e1a5`) and miniz commit
`77d0dce8627735138c51770d1799a1ef48f2117d` (archive
`baa14349e7c5efb4a493a03af7d2f29d0ef45d87d488567fe5360edb33f8b7ba`): the six public headers,
the eleven parser sources (the model, the VFF and MFC containers, the TLV and page/
dimension decoding, the attributes and transforms), and the thirteen miniz files the build
uses, plus the two licences and CMake's miniz export template. Excluded, per the ADR: the
exporters, the writer, the scene baker, tinygltf and flatbuffers. `VENDOR.txt` records the
SHA-256 of every file, and `scripts/vendor-openskp.py check` re-creates the tree from the
two commit archives and fails on any drift; the CI `skp` job runs it on every selected run.

**The patches** (two; `patches/`, `PATCHES.md`):

- `src/parser.cpp` drops the four scene-builder wrappers whose bodies call the excluded
  scene baker; `parse_skp` remains the single entry point.
- `src/internal.hpp` replaces the umbrella `openskp.hpp` include (the exporters' headers
  need tinygltf and flatbuffers) with the five public headers the sources actually use.

**The helper** (`src/skp/`, `sketchyup-skp-worker`, no Qt, the only target that links the
parser): `--probe` (version, container, units, counts) and `--import` (the package), with
the IFC helper's conventions - the `P`/`R` protocol, exit 0 on success and 3 on a refusal,
`RLIMIT_AS` 4 GiB (not in sanitizer builds), `RLIMIT_CPU` 480 s, `RLIMIT_FSIZE` 512 MiB and
the 120 s deadline. The refusals, each with a code and a short message: `notSkp` (the
header's first bytes), `tooLarge` (over the 256 MiB bound, or the `--size-limit-bytes`
override), `pre2013` (the version from the header string; the gate runs before parsing,
because a pre-2013 file the parser cannot read must still refuse cleanly), and `parse`
(a parser failure). A version newer than 25 is imported with a `newerThanTested` notice,
not refused. Raw coordinates are always inches; the conversion to metres is the fixed
0.0254, the same scale OpenSKP's own exporters apply.

**The package** (`SKSKPP01`; the helper writes it, `sketchyup_io` decodes it): a 32-byte
header (the magic, the manifest length, a reserved field, the geometry and texture chunk
lengths), the JSON manifest (at most 32 MiB), the geometry chunk (at most 256 MiB) and the
texture chunk (at most 256 MiB, each image at most 16 MiB); the file is exactly the sum,
and the manifest carries the SHA-256 of each chunk. The decoder range-checks every index
and loop, bounds every count before it allocates, verifies the chunk SHA-256s on a second
pass, and checks the report's accounting identity (imported + omitted = the source
totals). The report is the IFC shape: the format version, the container, the source
SHA-256, the units, the counts, the notices, the omitted kinds and the source totals, plus
the bounding boxes in metres and in the parser's raw inches. A face's UV transforms are
carried as stored; the mapping is R102.b.

**The fixtures** (`tests/fixtures/skp/`): a box; a face with a hole; soft and smooth
edges; two materials; a component with 3 instances; a hidden tag; and a nested component.
They are generated by OpenSKP's own writer (`generate.cpp`, built against the pinned
commit; `SKP-SOURCE.txt` records the command and each file's SHA-256). The writer writes
only the pre-2021 legacy container, so every fixture is the legacy format; the VFF path is
exercised by the owner's file (below) and by OpenSKP's own parser tests at the pinned
commit.

**Measured numbers** (this machine, 2026-10-26, the worker of this branch):

- The owner's file (never committed or copied; the spike's numbers): 0.07 s to parse and
  package; 2 definitions, 1261 vertices, 1422 edges, 159 faces, 5 holes, 1 instance,
  18 materials and 1 tag; the package is 59,138 bytes. The counts agree with the spike's
  (root 174/278/101 plus the "Teddy" definition 1,087 vertices / 58 faces / 1 instance /
  18 materials / 1 layer).
- The seven fixtures import in well under a second each (the largest, `nested_component.skp`
  at 7,640 bytes, produces a 2,199-byte package).
- `parser_fuzz_skp_package` (Clang 23, ASan and UBSan): 20,000 runs in 3 s, no crash; it
  decodes twice, with and without the digest check, and the two agree.

**The *w* finding**: the corpus' only instance has *w* = 1, and OpenSKP's `transform_point`
ignores value 12, so the ADR's rule (divide the translation by *w* when *w* ≠ 1, with a
`scaledPlacement` notice) has not fired on a real file yet; the R102.b fixtures settle it.

## Implementation notes (R102.b)

R102.b is implemented on the branch `feat/r102b-skp-mapping`: the mapping of a decoded package to one
native document (§4), the application runner `importSkpFile` (§1.6 and §9), and the checks S1 to S5 (§7).
Findings, 2026-10-25.

**The import.** `importSkpPackage` (`src/io/skp_import.cpp`) builds one unsaved document in one edit
labelled "Import SketchUp". The root's geometry sits in a top-level group named after the file (without
its extension). Faces keep their loops (outer first). Each definition becomes a component definition, with
one geometry body per tag and visibility, and each placement an instance; a nested placement has its own
binding, as the native projection requires. `importSkpFile` (`src/io/skp_worker.cpp`) runs the helper on a
private copy, with the cheap checks, the limits and the deadline, and decodes what it wrote.

**UVs: direct, not per-vertex.** Every UV transform in the fixtures and in the owner's file has the affine
form (third column (0, 0, 1)), so the stored 3×3 maps onto `TextureMapping` directly. The exporter's formula
(openskp `face_groups.cpp`, `compute_face_uv` with `face_uv_basis`) gives
u = (x·inv[0] + y·inv[3] + inv[6]) / tileW and v = (x·inv[1] + y·inv[4] + inv[7]) / tileH, where x and y are
the point's coordinates on the face's basis in inches, and tileW and tileH are the texture's x_scale and
y_scale (1 inch when unset). The importer writes these as the native gradients (repeats per metre) and
offset. The basis comes from the face's normal, which is the Newell normal of the outer loop (below). A
perspective matrix (third column not (0, 0, 1)) has no native form: the side takes the default projection,
with the `uvPerspective` notice. No file seen has one. Texture v is not flipped, as OpenSKP's glTF export
does not flip it. S5 checks the textured fixture's mapping against the exporter's formula at four points.

**Loop orientation: no conversion.** For every face of the owner's file (159) and of every fixture, the
parser's stored face normal agrees with the Newell normal of the outer loop, which is the native
`Surface::normal` convention. The loops are kept as they are (§4).

**w: the writer always writes 1, and its meaning is not settled.** OpenSKP's writer writes 1.0 in the
thirteenth value for every instance, the owner's file has 1 for its only instance, and the parser's own
point transform ignores the value. The writer can make a scaled and a mirrored instance, because the scale
and the mirror live in the 3×3 (`matrix3x3`): `scaled_instance.skp` (a shear with non-uniform scale) and
`mirrored_instance.skp` (a mirror in X). Both import with the 3×3 applied and w = 1, and the world bounding
box matches the placement arithmetic to 1e-6 m. The R102.a rule (divide the translation by w when w ≠ 1, with
the `scaledPlacement` notice) is kept unchanged, and no fixture exercises it. The other reading, where a
homogeneous 4×4 divides the whole 3×3 and the translation by w, is a different rule and is not adopted without
evidence. A file with w ≠ 1 from the owner's corpus would settle it.

**A layout finding in R102.a.** The package's 4×4 keeps the 3×3 row-major in its top-left block, but the
translation sits in its bottom row (values 12 to 14), not its last column. The native `Transform` is
column-major (translation in 12 to 14, `m[c * 4 + r] = M[r][c]`), so the importer transposes the 3×3. The
header comments now say so.

**Groups are components (a gap against §4).** §4 maps a definition used once and flagged as a group to a
native group. OpenSKP's model has no group flag: its legacy reader reads `CGroup` and `CComponentInstance`
the same way (`legacy.cpp`), and `Definition` has no field for it. So every definition imports as a component
definition, and a single-use group imports as a one-instance component. This is not a heuristic: a
definition's use count does not say it is a group.

**Edges and wires.** A package edge that no kept face uses is a wire (the worker never sets the wire flag).
The native edge count equals the package's: 1,422 on the owner's file, and 4 to 12 on each fixture.

**A face the native validator refuses.** The owner's file has one such face, a face of "Teddy" whose outer
loop repeats vertices (a pinched outline, which the tessellator reports as "Repeated loop vertex"). It is
dropped and counted (§4, `invalidFaces`), with its one small hole. Loops are read as the exporter reads them
(its `loop_vertices`: a vertex repeated in a row is one vertex) before validation. Splitting pinched outlines
into simple faces would change the face count, so it is not done here. Whether to split them is the owner's
decision.

**Not carried by the package.** Scenes, styles, section planes, texts, dimensions, the camera, and the
construction lines and points are not in SKSKPP01. R102.a did not carry them, so the report lists them under
`notCarried` instead of counting them. Guides (§4) are not imported.

**Names and attributes.** R102.a's package carried neither instance names nor attributes, and §4 needs both.
The manifest's definitions gain two optional arrays, aligned with the instances: `instanceNames` and
`instanceAttributes`. A package without them decodes as before. An attribute becomes the property
`skp.<dictionary>.<key>` when it is a string, a number or a boolean; a Length, a vector or a nested value is
not imported. The native caps hold: 128 properties a body, keys of at most 128 bytes, strings of at most 2,048
bytes.

**Caps and refusals.** The native caps refuse elements with a notice and a count: `bodyLimit` (100,000 faces
or vertices, or 300,000 edges, in one body), `documentLimit` (the unique geometry), `placementLimit` (the
materialized geometry), `materialsOverLimit` (1,024 materials), and `nestingTooDeep` (a nesting beyond 64
levels, or a cycle). A texture is kept as PNG or JPEG when the native decoder reads it; any other image is
decoded and re-encoded as PNG; an image over 4096 by 4096 pixels is refused (`textureTooLarge`).

**The owner's file (local only; the numbers only).** Two definitions (the root and "Teddy"), one placement, 158
faces kept and one dropped (159 in the file), 4 holes kept and one dropped (5 in the file), 1,422 edges (153 of
them wires), 18 materials, and one layer (the default layer, so no tag). The world bounding box is
13.4389 × 8.27065 × 2.4384 m. The import through `importSkpFile` took 194 to 239 ms over five runs, on a machine
under load (load average about 30), including the helper's parse. The file has no textures.

**Fixtures and provenance.** Three fixtures (`scaled_instance.skp`, `mirrored_instance.skp`,
`textured_face.skp`) and `named_attributes.skp` come from OpenSKP's writer at the pinned commit. The component
fixtures, including the two R102.a ones, are not byte-reproducible: the writer stamps a random GUID and a
timestamp on each definition. `SKP-SOURCE.txt` records the committed bytes as the reference.

**Tests and CI.** `skp_mapping_tests` runs S1 to S5 on every fixture, the S3 edit, one undo with the label
"Import SketchUp", and the report's accounting. With `SKETCHYUP_SKP_CORPUS` it also checks the owner's file.
The `skp` job runs `skp_mapping` (and `skp_mapping_real`, skipped without the corpus), the `skp` preset builds
its four targets, and the CI line inventory is regenerated (its one changed line is the job's test selection).

## Implementation notes (R102.c)

R102.c is implemented on the branch `feat/r102c-skp-import-ui`: File › Import SketchUp (.skp)…, its
progress, report and refusal screens, and `sketchyup-cli --import-skp` (§9). Findings, 2026-10-25.

**The choices.** §9 leaves the options screen open, and the brief asked for one of two shapes. The
import has **no options dialog**: the file picker leads straight to the progress sheet. The file's
facts (version, container, size) and the 2013–2020 best-effort line are in the report's "How it was
read" block, not on a sheet before the import, so there is nothing to confirm. The unsaved-changes
prompt comes before the helper starts and again at the commit, as the IFC and STEP imports do.

**The progress steps.** Five steps: check the file, read the file, convert geometry, build the model,
create the new model. The application checks the file first (the initial step). The helper's
`read` and `parse` phases are "read the file"; its `package` phase is "convert geometry". The
application's own phases are `build` (the package is decoded) and `create` (the document is made from
it). The helper reports phases, not a running count, so the bar is busy until the run ends.

**Cancel.** Cancel and Escape end the run. The helper's run checks the cancel as before, and the
conversion now checks it too (R102.b passed no cancel hook, so a Cancel during the decode or the
conversion committed the model). The sheet closes only after the runner has finished.

**The report.** Its identity is per kind, not the IFC's entity identity: for faces, edges, instances
and materials, `counted = imported + omitted` (§5). The attention count is the omitted total over those
four kinds. The banner shows no restart count, since the helper has no restart protocol (§1). The
trademark note (§8) is a footnote under the grouped list, with the wording of §8.

**Refusals.** `notSkp` and `tooLarge` are checked by the application before the helper runs. `pre2013`
and `parse` come from the helper, and each gives the advice to save the file again from SketchUp 2021 or
later. A helper stop (crash, memory, error, start) and a deadline (the helper's own `deadline` refusal, or
the application's timeout) are "helper stopped" refusals. `workerMissing` is the availability sheet.

**The deadline.** The default is 120 s, as §6. The window has a test hook for it
(`setSkpDeadlineSeconds`); the CLI's `--skp-deadline` is the public option.

**R102.b corrections, found by the screens.**
- The report carried none of the helper's own notices. `newerThanTested` and `scaledPlacement` were
  decoded into the package and then dropped, so §5's notice list was not in the report. Fixed: the
  package's notices are merged into the report.
- A dropped face was counted and listed as a location, but not as the `invalidFaces` notice that §5
  names. Fixed: each dropped face counts toward the `invalidFaces` notice.
- The cancel did not reach the decode or the conversion (see above). Fixed.

**The owner's file (local only; the numbers only, through the CLI on this machine).** SketchUp
`{24.0.553}`, VFF. One component definition ("Teddy") and one placement. 158 of 159 faces kept (the
dropped face is the "Teddy" face with a repeated outer-loop vertex, as the R102.b notes record), 4 of 5
holes kept, 1,422 edges (153 of them wires), 18 materials, no tags. Notices: `invalidFaces` 1 and
`missingUv` 159 (faces with no texture mapping, which the file has no textures for). The helper's
package is about 59 KB. `skp_mapping_tests --real` and `skp_import_tests --real` pass against the file.

**Tests.** `skp_import_dialog_tests` (offscreen, a stand-in helper that copies a package written by the
helper's own writer) passes all of its flows: the File menu's order, a missing helper, each refusal, a
full import with the report, the attention count, Cancel and Escape, the prompts, and the chip's
lifetime. `skp_import_tests` passes, with the CLI cases through the real helper. The regression suites
`step_import_dialog_tests`, `ifc_import_dialog_tests`, `ifc_export_dialog_tests`,
`narrow_layout_input_tests` and `skp_mapping_tests` pass offscreen.

**Where the ADR and the implementation disagree.**
- §5 and §2 give the parse-failure line with the year ("This SketchUp 2017 file could not be read").
  The helper refuses a file its parser cannot read before it has a version to report, so the refusal
  says "could not read this file" and gives the 2021 advice, without the year.
- The refusal sheet's words, the progress step names and the report's notice wording are the
  application's own choices. The owner should review them with the other screens' wording.

**Not run here.** The screens were not run in the desktop application against the owner's file (the
local check used the CLI). The dialog suite was not run under the sanitizers, and the X11 and Wayland
display lines run only in CI.

## Implementation notes (R102.d)

R102.d is implemented on the branch `feat/r102d-skp-publish`: the notices and the source record, the
linkage check, the sanitizer and fuzz lines, the user-guide page, the support-matrix row and the
exchange-boundaries entry, and the pin re-check. Findings, 2026-10-25.

**Packaging.** The helper already installed into the main package, because its install rule has no
component. R102.d adds the notices (`OpenSKP-LICENSE` and `miniz-LICENSE`, both MIT, through install
lines beside the other third-party licences) and `share/doc/sketchyup/SKP-SOURCE.txt`, which records the
pinned commits, the archive digests, the licences, the vendored file list, the two patches and the
non-affiliation. The pin check had been failing since R102.a: `dependencies.json` had no OpenSKP entry,
and `PATCHES.md` did not name the patch files. Both are recorded now. The pin itself did not move.

**The linkage check** (`scripts/verify-skp-library.py`, ADR section 1 and 9). makepkg strips the
installed binaries, which hides the parser's symbols, so the check has two halves. The install mode reads
the unstripped install tree, with symbols: only `sketchyup-skp-worker` carries OpenSKP or miniz symbols
(or names the parser library in a shared build), and its DT_NEEDED is the C and C++ runtime. The package
mode reads the installed package by DT_NEEDED and `ldd`, and checks the notices and the source record.
Package acceptance runs both, the install mode on the build tree the package was made from. The source
mode checks the pins, the install lines and the helper's install rule.

**A deviation from ADR section 2, corrected.** The gate refused a header major below 15, so SketchUp 2013
and 2014 files were refused as "from before 2013" before the parser ran. Section 2 reads 2013 to 2020 as
best effort and refuses only before 2013 (V3 to V8). The gate is now below 13 in the probe and the import
paths. Test S6 writes major 12 (refused with the advice) and major 13 (not refused as pre-2013). The UI's
own container line already said "SketchUp 2013-2020 ... best-effort".

**The pin re-check (not moved).** On 2026-10-25 upstream `main` is still `23a15e8` (PR #443, 2026-10-22),
so nothing has landed after the pin: the compare with the pin is "identical". Issue #442 is open (created
2026-10-21, no comments, no linked PR). Its two causes, against the pinned C++ reader:

- The 4, 5 and 2019 layout fixes are in the pin (PR #443 ports them to C++: the texture DIB, the camera
  and thumbnail, the material record width and the component definition's base block). That is the
  "partial port" that the Context describes.
- The first cause in #442, the CText arrow type, is **not** fixed in C++. `legacy.cpp` still requires
  arrow type 3 exactly, so a text label of another type stops the reader with "text delimiter not found"
  (V4 to 2016 files, per the issue).
- The second cause, "expected a string record" (2017 and later), is not understood upstream and is not
  fixed in the pinned reader.

Consequence for the import: a 2013 to 2020 file with such a label is refused by the helper's parse
refusal, with the advice to save again from SketchUp 2021 or later. It is never imported in part. The
user guide and the support-matrix row say so. Recommendation: no pin change now, because no newer upstream
commit exists. Move the pin only after #442's CText and 2017+ items close upstream, and then re-run the
owner's corpus, the fixtures and the L1 check.

**Verification on this machine (2026-10-25; the numbers, not the owner's files).**

- **Shared build** (the `skp` preset, shared libraries): `skp_package`, `skp_import`, `skp_import_real`,
  `skp_mapping`, `skp_mapping_real`, `skp_library_install` and `skp_library_source` pass (7 of 7). The
  install check takes the shared-parser branch: the worker names `libsketchyup_openskp.so`, and its
  closure is left to the static package.
- **Static release build** (Release, shared libraries off, the same targets): the same 7 pass. The helper's
  DT_NEEDED is `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1` and `libc.so.6` only, and it carries 317
  OpenSKP and miniz symbols; `sketchyup-cli` carries none. A scratch install, stripped as makepkg strips,
  passes package mode: L1 by dynamic linkage, L2 by `ldd`, and L3 (the notices and the source record).
- **Sanitized build** (`skp-sanitize`, ASan and UBSan): `skp_package`, `skp_import` and `skp_mapping` pass
  (3 of 3). The test binaries and the helper carry the sanitizer runtime, and no report appears in the logs.
- **Package decoder fuzz smoke** (Clang 23, `RelWithDebInfo -O1`, ASan and UBSan, `parser_fuzz_skp_package
  -runs=200000 -seed=1 -max_len=65536`): seeds are the five packages the helper writes for the box,
  face-with-hole, two-materials, textured-face and nested-component fixtures (1,560 to 2,363 bytes). The
  run finished 200,000 executions in 30 s with no crash and no sanitizer report: 1,719 covered edges,
  4,881 features, 198 corpus entries (357 KB), 6,666 executions a second, 498 MB resident.
- **The owner's file** (`skp_import_tests --real`, `skp_mapping_tests --real`, through
  `SKETCHYUP_SKP_CORPUS`): the package is 59,342 bytes. The R102.a record gave 59,138 before the instance
  names and attributes were added to the manifest; I did not trace the difference further. The probe
  reports 2 definitions (the root and "Teddy"), 1,261 vertices, 1,422 edges, 159 faces, 5 holes, 1
  instance, 18 materials and 1 tag (the default layer). The mapping keeps 158 faces and drops 1 (the
  `invalidFaces` notice: "Teddy", a repeated outer-loop vertex), keeps 4 of 5 holes (`invalidHoles` 1),
  and imports 1,422 edges (153 of them wires), 18 materials, no tags (the default layer maps to Untagged),
  and one component definition plus the root geometry. The world bounding box is 13.4389 × 8.27065 ×
  2.4384 m. Five timed imports on the idle machine took 24 to 29 ms (R102.b measured 194 to 239 ms under a
  load of about 30).

**Faults found by running the CI lines, fixed in this PR.**

- The sanitized helper ran out of address space: the application's supervisor passes `--rlimit-as` of
  4 GiB, which overrode the helper's own exemption, and ASan maps more than that. The exemption is now one
  definition (`SKP_WORKER_NO_ADDRESS_LIMIT`) for every sanitizer and fuzzer build, and the supervisor's
  default follows it. The fuzz build generates its seeds with the helper, so it needs the same exemption.
- `skp_library_install` failed because `cmake --install` needs every installed target built. The `skp`
  preset builds the two workers that ship in the main package and the assembly bundles.
- The Clang fuzz build rejected a narrowing in the helper's CPU limit, which GCC accepts. The first field is
  now cast.
- The fuzz step ran the helper from the wrong path. It now runs `build/skp-fuzz/src/skp/sketchyup-skp-worker`.

**Not run here.**

- The CI jobs themselves, and the Arch package acceptance in the pinned container. The package-acceptance
  lines (install mode on `/work/package/src/build`, package mode and source mode) are not executed.
- The desktop application and `skp_import_dialog_tests`. The header change to `skp_worker.hpp` is compiled
  by the CLI and the import library built here, but not by the desktop targets.
- The full `dev` test tree.

## Sources

- OpenSKP: <https://github.com/iamahsanmehmood/openskp> (README, `packages/cpp`, issue
  [#442](https://github.com/iamahsanmehmood/openskp/issues/442)); PyPI
  <https://pypi.org/project/openskp/>.
- hew3d/openskp (GPL-3.0 Rust reader): <https://github.com/hew3d/openskp>.
- SketchUp C SDK platforms: <https://extensions.sketchup.com/sketchup-sdk>; the forum's
  "Linux version of the SketchUp C SDK?"
  <https://forums.sketchup.com/t/linux-version-of-the-sketchup-c-sdk/37842>.
- SDK licence questions: <https://forums.sketchup.com/t/is-the-sketchup-sdk-free-to-use-for-me/220079>,
  <https://forums.sketchup.com/t/exporter-dlls-redistributable/41874>.
- The spike's notes: `.local/briefs/skp-research.md` (local).
