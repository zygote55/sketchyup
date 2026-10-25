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
| Instance and definition names | Body and definition names | Capped at 2,048 bytes, with a notice when truncated |
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
