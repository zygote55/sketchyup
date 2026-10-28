# R102.e — SketchUp (.skp) import: acceptance record

2026-10-28. Branch `docs/r102e-skp-acceptance`, based on `origin/main` at `fafc0431` (the merge of PR #510). Every check below ran on `fafc0431`, the product code of `main` today. The PR changes documents only: this record, its JSON, and the "Implementation notes (R102.e)" section of [ADR 0176](../decisions/0176-skp-import.md). No file under `src/`, `tests/`, `scripts/` or the build changed. [Summary](R102e-skp-import.json).

The owner's file, `BanyanHarbor.skp`, was read in place from the git-ignored `.local/skp-corpus/`. This record holds its counts, sizes, timings and SHA-256 only. No part of it is committed.

**Status: Delivered, with open findings.** Every check that ran passes, except the findings in §6. Three checks cannot run here, and one of them (S5, textures against a SketchUp screenshot) needs the owner. The claims in §5 that rest on synthetic files or on one real file are narrower than the matrix and guide say, and the parent decides the wording. This record does not set "Accepted".

## 1. Check map

Each row names the test or command, its result today, and the code it ran on (`fafc0431`). The CTest names are those in `CMakeLists.txt` and the CI `skp` job. "Corpus" means the run with `SKETCHYUP_SKP_CORPUS` set to the owner's directory.

| Check (ADR 0176 §7 unless noted) | Test or command that proves it | Result today | Note |
| --- | --- | --- | --- |
| **S1** counts equal the parser's | `skp_mapping` (`checkCounts`, all 11 fixtures); corpus: `skp_mapping_real`; scratch fidelity run on the owner's file (§3) | PASS, 11 fixtures and the owner's file | Owner: 7 of 7 count checks pass |
| **S2** bounding box within 1e-6 m | `skp_mapping` (`checkBox`, `scaled_instance` S2 test); corpus; scratch run | PASS | The owner's placed box is 0 m off. The package's own `bounding` block is not the placed box (F3) |
| **S3** an edited component changes all its instances | `skp_mapping` S3 (`component_instances`, 3 of 3); scratch run on the imported model | PASS, fixture 3 of 3; owner 1 of 1 as imported, 3 of 3 with two placements added in the scratch document | §3 explains the single-instance file |
| **S4** hidden tags hidden; soft and smooth edges kept | `skp_mapping` S4 (`hidden_tag`, `soft_smooth_edges`, `face_with_hole`); scratch run | PASS | Owner: 23 soft, 23 smooth, 142 hidden edges kept; no hidden tag in the file |
| **S5** textures on the right faces, against a SketchUp screenshot | `skp_mapping` S5 (`two_materials`, `textured_face` at four points, `mirrored_instance`) | **Not run** against a screenshot. The tests pass but check the exporter's UV formula on a synthetic PNG only | Needs an owner file with textures and a screenshot from SketchUp (§7) |
| **S6** every refusal of §5 | See the refusal rows below | PASS for the refusals the CLI and sandbox tests reach; see the gaps | |
| **S7** package decoder fuzzed; helper under ASan and UBSan | Fuzz: `parser_fuzz_skp_package`, Clang 23.1.3, ASan and UBSan, 200,000 runs. Sanitizer: `ctest` on `build/skp-sanitize` (`skp_package`, `skp_import`, `skp_mapping`, `skp_dc`, `skp_sandbox`) | PASS: 200,000 runs with no sanitizer report; 5 of 5 sanitizer tests | The fuzz seeds need stdout to a file (F2) |
| §5 refusal: not a `.skp` (`notSkp`) | `skp_import` (CLI `--import-skp`: the code, and nothing written) | PASS | |
| §5 refusal: over the size budget (`tooLarge`) | `skp_import` with `--size-limit-bytes 1024` | PASS, **by the override only**. The 256 MiB default is never exercised (§6, F5) | |
| §5 refusal: unreadable input (`unreadable`) | `skp_import` (missing input) | PASS | |
| §5 refusal: parser failure (`parse`) | `skp_import` (truncated fixture) | PASS | |
| §5 refusal: before 2013 (`pre2013`) | `skp_import` (header patched to 12 refused; 13 not refused) | PASS, on a synthetic header only | No real file |
| §5 refusal: helper missing (`workerMissing`) | `skp_import` (CLI with a missing helper) | PASS | |
| §5 refusal: sandbox unavailable | `skp_sandbox` (no Landlock, no seccomp, a stand-in helper exiting 70) | PASS | |
| §5 refusal: sandbox violation (`sandboxViolation`) | `skp_sandbox` (helper killed by SIGSYS) | PASS | |
| §5 refusal: helper crash (`workerFailed`) | `skp_sandbox` (helper killed by SIGABRT) | PASS | |
| §5 refusal: helper past its deadline | Desktop dialog suite `skp_import_dialog` (the deadline case) | **Not run** here (desktop not built). The CLI bound on `--skp-deadline` passes | §9 |
| §5 refusal: helper over its address-space, CPU or output limit | None in this build | **Not tested** (code only: `src/io/skp_worker.cpp:77` to `:79`) | F5 |
| §6 input file 256 MiB | Code: `src/skp/worker_main.cpp:42`, `src/io/skp_worker.cpp:253` and `:281` | Code only | F5 |
| §6 helper address space 4 GiB | Owner run: report `run.addressSpaceBytes` = 4,294,967,296 | PASS (report) | |
| §6 helper deadline 120 s | Owner run: report `run.deadlineSeconds` = 120; `--skp-deadline 0` rejected | PASS | |
| §6 helper CPU 480 s | Code: `RLIMIT_CPU` = 4 × deadline (`src/io/skp_worker.cpp:78`) | Code only | F5 |
| §6 helper output 512 MiB | Code: `RLIMIT_FSIZE` (`src/io/skp_worker.cpp:79`) | Code only | F5 |
| §6 package limits | Constants, `src/io/skp_package.hpp:18` to `:38`. The decoder refusals in `skp_package`: SHA-256 mismatch, truncation, bad magic, reserved word, trailing bytes, manifest over 32 MiB, index and loop range, counts over 5,000,000 | PASS for the refusals listed. Geometry and texture chunks over 256 MiB, a texture over 16 MiB and over 4096 px: no test | F5 |
| §6 application memory during import ≤ 1 GiB | Scratch harness peak RSS after the owner import | PASS: 24 MiB (the harness, not the desktop app) | §3 |
| §6 parse time | Helper `--probe` and `--import` on the owner's file, 5 runs each | Measured (§3) | No budget in §6 to compare with |
| R109 S1: the helper's sandbox policy | `sandbox` (the library), `skp_sandbox` (every fixture identical sandboxed and unsandboxed; attacks refused; socket refused with EPERM) | PASS | Found F2 by a run with stdout on `/dev/null` |
| Packaging: notices (OpenSKP and miniz licences, `SKP-SOURCE.txt`) | `skp_library_install` (L1 and L3 in the build tree); package mode on a local stripped staged install (L1 to L3) | PASS | The Arch package itself was not built (§9) |
| Packaging: linkage (only the helper links the parser) | `skp_library_install` (install mode); package mode (L1 by DT_NEEDED, L2 by `ldd`) | PASS | |
| Packaging: source pins and install lines | `skp_library_source` (`scripts/verify-skp-library.py source`) | PASS | |
| Vendored source equals the pinned archives | `python3 scripts/vendor-openskp.py fetch` then `check`, and `pin` | PASS: "30 files, 640791 bytes"; "Pins agree: OpenSKP 23a15e87, miniz 77d0dce8" | Downloaded the two archives to `.local/`, read in memory |

## 2. The tested version matrix

Each row is a SketchUp version as the file's header string gives it (`{NN.x.y}`). "Real" means a file the owner saved in SketchUp. "Synthetic" means a file OpenSKP's own writer made, or a header edited by a test.

| SketchUp version | Container | Real files | Synthetic fixtures | What it shows |
| --- | --- | --- | --- | --- |
| 2024 (`{24.0.553}`) | VFF | **1**: `BanyanHarbor.skp` (145,795 bytes, SHA-256 `75ae2786…c739ed`) | none | Import, S1 to S4, S3 on the owner's model, the timings |
| 2021, 2022, 2023 | VFF | none | none | Not tested |
| 2025 and later | VFF | none | none | Not tested. The "newer than tested" notice has no test (F1) |
| 2017 (`{17.0.1}`, what the fixtures say) | MFC (legacy) | none | **15 fixtures** from OpenSKP's writer v17 (`tests/fixtures/skp/SKP-SOURCE.txt`): the box, face-with-hole, soft and smooth edges, two materials, component instances, scaled and mirrored instances, a hidden tag, nested components, named attributes, the textured face, attribute reporting, and three Dynamic Component fixtures | The legacy path and its refusals work on synthetic input. The writer always writes v17, so no fixture is a real 2013 to 2020 file |
| 2013 to 2016, 2018 to 2020 | MFC (legacy) | none | none | Not tested on any real file |
| Before 2013 (V3 to V8) | MFC | none | the box fixture with its header patched to 12 | Refused; tested on the synthetic header only |

**Synthetic fixtures and what made them.** All 15 are OpenSKP's own writer at the pinned commit `23a15e87` (`generate.cpp`, `generate_dc.cpp`). Their bytes are the reference. The component fixtures are not byte-reproducible (random GUIDs and timestamps), as `SKP-SOURCE.txt` records. Real VFF files are 2021 and later; the synthetic fixtures are 2017 legacy, so the VFF code path is exercised only by the owner's file.

## 3. Fidelity on the owner's file

Scratch check `.local/fidelity/owner_fidelity.cpp`, built against this worktree's Release libraries and run once today. It reads the file in place, decodes the helper's package of it, and deletes the package. It is not committed. It prints counts and PASS or FAIL lines only.

**Parser (OpenSKP's own reading, C++ at pin `23a15e87`, through the helper's `--probe`).** Version `{24.0.553}`, container VFF, units Inches. Definitions 2 (the root and one component), vertices 1,261, edges 1,422, faces 159, holes 5, instances 1, materials 18, tags 1 (the default layer).

| Count | Parser | Native import | Check |
| --- | --- | --- | --- |
| Faces | 159 | 158 kept + 1 dropped (`invalidFaces`) | S1 PASS |
| Holes | 5 | 4 kept + 1 dropped (`invalidHoles`) | S1 PASS |
| Edges | 1,422 | 1,422 (153 of them wires) | S1 PASS |
| Definitions | 2 | 1 component + the root group | S1 PASS (the root is the one group) |
| Instances | 1 | 1 | S1 PASS |
| Materials | 18 | 18 | S1 PASS |
| Tags | 1 (default layer) | 0 tags; the default layer maps to Untagged | S1 PASS |
| Soft edges | 23 | 23 | S4 PASS |
| Smooth edges | 23 | 23 | S4 PASS |
| Hidden edges | 142 | 142 | S4 PASS |
| Hidden tags | 0 | 0 | S4 PASS (vacuous: the file has no hidden tag) |
| Hidden faces, instances, bodies | 0 | 0 | |
| Textures | 0 | 0 | S5 cannot be checked (§7) |

**S2, the bounding box.** The native world box (metres) is 13.438901 × 8.27065144 × 2.4384, with its minimum at (−0.383301042, −0.993551443, 0). The parser's placed box, which applies each placement to the parser's vertices, is identical. The worst axis difference is 0 m, against the tolerance of 1e-6 m. The package's `bounding` block is not this box: it is over each definition's own vertices with no placement applied. Its minimum corner differs from the placed box by 0.152 m in x and 0.994 m in y (F3).

**S3, on the imported model.** The file has one placement, so "every instance" is one instance. The check edits the imported definition and finds 1 of 1 instances changed. The check then adds two placements of the same definition in the scratch document and edits again: 3 of 3 changed. The fixture `component_instances.skp` gives the same 3 of 3 in the test suite.

**S4, hidden tags.** The file has no hidden tag, so the hidden-tag half of S4 is not exercised on this file. The fixture `hidden_tag.skp` covers it.

**S5.** Not checkable here. The owner's file has no textures, so there is nothing to compare, and the check needs a SketchUp screenshot. The owner has to supply a textured file and the screenshot.

**Time and memory (helper, 5 runs each, the machine's load average about 3 to 4).**

| Measure | Result |
| --- | --- |
| Helper `--probe`, wall clock | median 11.1 ms (min 8.7, max 13.2) |
| Helper `--import`, wall clock | median 15.6 ms (min 14.2, max 16.4) |
| In-process import (helper, decode and mapping), three single runs | 28 to 31 ms |
| Helper peak RSS (`--import`, the largest of the 5 runs) | 13.1 MiB |
| Application-side peak RSS after the import (the scratch harness) | 24 MiB against the 1 GiB budget |
| Package size | 64,299 bytes (the test's copy; R102.d recorded 59,342 before the attribute follow-up; the difference was not traced) |

**File integrity.** The owner's file's SHA-256 is `75ae2786188d7c95022774783a1e8fc77f6c1bbfb2323dbb7395748ea7c739ed` before and after every run in this record.

## 4. Per-feature losses

What the import drops or changes, by feature. Each row names the evidence for today's behaviour. "Owner" is the numbers from §3.

| Feature | What the import does | Owner file | Evidence |
| --- | --- | --- | --- |
| Single-use group (ADR §4) | Becomes a component definition with one instance. The file does not mark a group, so the import cannot tell one from a component | The one component is a component. Only the root is a group | S1 definitions check; `src/io/skp_import.cpp` and the R102.b note on `legacy.cpp` |
| Faces the native validator refuses | Dropped and counted (`invalidFaces`), with samples | 1 face (a repeated outer-loop vertex) and its 1 hole | §3 counts; the report's notice |
| Soft, smooth, hidden edges | Kept exactly | 23 soft, 23 smooth, 142 hidden | S4 |
| Loose edges | Become wires | 153 wires | S1 |
| Layers | Become tags of the same name; hidden stays hidden; the default layer becomes Untagged | 1 default layer, no tags | S1 |
| Scenes, styles, section planes, text, dimensions | Not imported; counted when present | Styles 2. Scenes, section planes, text and dimensions 0 | Report notices |
| Camera and guides (construction lines and points) | Not imported; counted | Camera 1, construction lines 6 | Report notices |
| Attributes | String, number and yes/no values become `skp.<dictionary>.<key>`; other values are counted and dropped | 35 values: 2 imported, 33 not carried (23 definition-level, 10 model-level) | Report accounting; `skp_mapping` |
| Dynamic Component formulas and `onClick` | Not run; kept as text. A `Copies` placement becomes one placement. Dynamic attributes are read-only | No Dynamic Component values in the file (0 instance, 0 definition) | `skp_dc` (self-authored fixtures only) |
| Textures | PNG and JPEG kept; other formats re-encoded to PNG; over 4096 by 4096 px refused | No textures in the file | `textured_face` (PNG only); the JPEG and conversion paths have no test |
| Materials | Colour and opacity kept; textures as above | 18 materials, colour and opacity | S1 |
| UV transforms | Stored transform mapped directly; a perspective transform gets the default projection with a notice | No textures, no perspective matrix | R102.b note; no file exercises the perspective case |
| Scaled or mirrored placement (w ≠ 1) | The translation rule (divide by w) is kept as written; the `scaledPlacement` notice is defined | The one instance has w = 1 | R102.b note. No test exercises the rule |
| Names | Capped at 1,024 bytes; `truncatedNames` when cut | None cut | `skp_mapping` |
| Units | Geometry converted from inches to metres; the unit label kept in the report | Inches; the box is 13.44 m, the file's 529 in | Report `units` |
| Native caps (faces, edges per body; geometry records; placements; materials over 1,024; nesting over 64) | Refused by element with a notice; the rest imports (`src/io/skp_import.cpp`) | None reached (158 faces, 18 materials, one level of nesting) | Code only; no `skp` test |

## 5. Claims

For each claim in the support matrix's `.skp` row (`docs/SUPPORT_MATRIX.md`) and the user guide's `.skp` page (`docs/USER_GUIDE.md`, "Importing SketchUp (.skp) files"), this table gives the verdict and the narrower wording the evidence backs. Neither file is edited here; the parent decides.

"Backed" means a real file or a passing test shows it. "Synthetic" means only OpenSKP-written or header-edited fixtures show it. "Code only" means the code says it and no test or run exercises it.

| # | Claim (where) | Verdict | Evidence | Recommended wording (for the parent) |
| --- | --- | --- | --- | --- |
| C1 | "SketchUp 2021 and later supported (tested with a 2024 model)" (matrix); "2021 and later: Supported. Tested with a SketchUp 2024 model" (guide) | **Not backed** beyond 2024 | One real VFF file (2024). Nothing from 2021 to 2023 or 2025 and later | "SketchUp 2024 files import; one 2024 model was tested. Files from other versions use the same container and have not been tested." |
| C2 | "2013 to 2020 best effort, labelled in the report and refused when unreadable" (matrix); the guide's 2013 to 2020 row | **Partly backed.** The best-effort path and the refusal work on synthetic input. No real 2013 to 2020 file exists in the evidence | 15 v17 fixtures from OpenSKP's writer; the truncated-file refusal. The label is in the desktop sheet (`src/app/ifc_sheets.cpp:801`), which is not run here; the CLI JSON has no label | "2013 to 2020 files are read on a best-effort basis, and the desktop report says so. These files have not been tested on real SketchUp files; some may be refused." |
| C3 | "Before 2013 refused, with the advice to save again from SketchUp 2021 or later" (guide, matrix) | **Partly backed** (synthetic) | The header patched to 12 is refused | Keep. The evidence is a patched header |
| C4 | "Faces with holes, edges and their flags, components, tags, materials and textures, names and attributes come across" (matrix) | **Partly backed.** Faces, holes, edges, flags, components, materials and names are backed by the owner's file. Hidden tags and textures are synthetic. Attributes are synthetic plus two real values | §3; S4 and S5 fixtures; `named_attributes`, `attribute_reporting` | "...come across. Tested on one 2024 model (geometry, components, materials, edge flags) and on synthetic files (hidden tags, textures, attributes)." |
| C5 | "A Dynamic Component's attributes come across as read-only properties" (matrix); the guide's Dynamic Component bullet | **Synthetic only.** The owner's file has no Dynamic Component values | `skp_dc` on self-authored fixtures | "...are read as read-only properties. Tested on synthetic files written by OpenSKP's writer; no SketchUp file with Dynamic Components has been tested." |
| C6 | "Formulas and the onClick script come across as text, never run" (guide) | Backed by the source scan and by synthetic fixtures | `skp_dc` (`formulaNotEvaluated`, `copiesNotReproduced`); the no-network source scan | Keep, with the synthetic-file note as in C5 |
| C7 | "Groups import as components used once" (matrix, guide) | Backed by code and by the owner's counts | S1 definitions; the file has one group (the root) | Keep |
| C8 | "Scenes, styles, section planes, text, dimensions, guides and the camera do not come across" (matrix, guide) | Backed | Code (`notCarried`); the owner's styles, camera and guides are counted | Keep |
| C9 | "Live Components are not identified as service-dependent (the file does not mark them)" (matrix) | Backed | The file has no such mark; no code identifies one | Keep |
| C10 | "The import makes no call to any Trimble service" (guide) | Backed | `skp_dc` source scan for network code in `src/skp/` and `src/io/skp_*` | Keep |
| C11 | "A file that cannot be imported is refused and nothing is created" (guide) | Backed for the refusals in §1 that the CLI and sandbox tests reach. The desktop sheet is not run | `skp_import`, `skp_sandbox` | Keep |
| C12 | "The file is limited to 256 MiB" (matrix, guide) | **Code only.** The default is in code; no test runs at the limit | `src/skp/worker_main.cpp:42`; `src/io/skp_worker.cpp:253` | Keep, or add a test at the limit (F5) |
| C13 | "The helper: 4 GiB of address space, 480 s of CPU, 512 MiB of output, a 120 s deadline by default" (matrix, guide) | 4 GiB and 120 s **backed** by the report. CPU and output **code only** | Owner run report; `src/io/skp_worker.cpp:77` to `:79` | Keep. Add tests for the CPU and output limits (F5) |
| C14 | "Textures: a PNG or JPEG image is kept; any other format is converted to PNG" (guide) | **Partly backed.** PNG is tested (synthetic). JPEG and the conversion have no test | `textured_face`; `src/io/skp_import.cpp:522` | "A PNG texture is kept (tested on a synthetic file). JPEG and other formats are kept or converted as the code states; not tested." |
| C15 | "A texture over 4,096 by 4,096 pixels is not imported, and the report says so" (guide) | **Code only** | `textureTooLarge` in `src/io/skp_import.cpp:547` | Keep, marked code only, or add a test (F5) |
| C16 | "Components over 100,000 faces, 100,000 vertices and 300,000 edges are left out; 10,000 geometry records, 600,000 faces after placement, 1,024 materials; nesting over 64 levels" (guide) | **Code only** | `bodyLimit`, `placementLimit`, `materialsOverLimit`, `nestingTooDeep` in `src/io/skp_import.cpp` (`:606`, `:792`, `:993`) | Keep, marked code only, or add tests (F5) |
| C17 | "A version newer than the ones tested is read and labelled 'newer than tested'" (guide); "a file from a newer SketchUp than the corpus covers … labelled" (ADR §2) | **Contradicted by the code** (F1) | The notice is set only for a major version above 25 (`src/skp/import.cpp:371`). The corpus covers 24. A 2025 file is read with no notice | Either change the threshold to 24 (the code, owner's decision), or write "A file from SketchUp 2026 or later is read and labelled…" |
| C18 | "Layers as tags of the same name; hidden layers stay hidden; Layer0 becomes Untagged" (guide) | Backed | S1 (untagged count); S4 on the hidden-tag fixture | Keep |
| C19 | "Edges with hidden, soft and smooth flags; a loose edge comes across as a wire" (guide) | Backed | §3 (23, 23, 142, 153 wires) | Keep |
| C20 | "Instances of one component share one definition, so editing the component changes every instance" (guide) | Backed | S3: 1 of 1 on the owner's file, 3 of 3 with two added placements; the fixture 3 of 3 | Keep |
| C21 | "Geometry is converted from inches to metres; the file's unit setting is shown in the report and does not change the size" (guide) | Backed | Owner's report `units` = Inches; the box is 13.44 m (529 in) | Keep |
| C22 | "The .skp file itself is never changed" (guide) | Backed for this record's runs (the SHA-256 is unchanged). The app works on a private copy | §3, file integrity | Keep |
| C23 | "Only text, number and yes/no values come across" (guide, attributes) | Backed, synthetic plus two real values | `named_attributes`, `attribute_reporting`; the owner's 35 values | Keep |

## 6. Defects and contradictions found

Status of each finding after R102.f: section 11.

No product code was changed. Each item gives the steps that reproduce it.

**F1. The "newer than tested" notice fires one version later than the ADR and the guide say (contradiction, low).**
- Code: `src/skp/import.cpp:371`, `if (major > 25) report.notice("newerThanTested");`.
- ADR 0176 §2 says "a file from a newer SketchUp than the corpus covers … labelled 'newer than tested'". The corpus covers 2024 (major 24). The user guide says the same.
- A 2025 file (major 25) is imported with no `newerThanTested` notice. A 2026 file gets it.
- No test covers the notice, and no real file can reproduce it here. Patching a fixture's header to 25 switches the reader to the VFF container, and the fixture (legacy) then fails with "No ZIP container found" (`--probe` on a copy with the header patched). The evidence is the code.
- Close it: the owner decides the threshold. Then either change the code to `> 24` and add a test, or change the wording.

**F2. The helper is killed by its own sandbox when stdout is a character device that is not a terminal (defect, low).**
- Steps: `build/release/src/skp/sketchyup-skp-worker --probe tests/fixtures/skp/box.skp > /dev/null` exits 159 (SIGSYS, "Bad system call"). The same with `> /dev/zero`. With stdout to a file or a pipe (`| cat`) it works, and so does a pseudo-terminal (`script -q -c …`).
- Cause: `gdb` traces the last syscall before the kill as `ioctl(1, 0x802c542a, …)`, which is `TCGETS2`. glibc's stdio issues it for a character device to check for a terminal. `src/skp/sandbox_policy.cpp` has no `ioctl` rule, so the default kill applies (`src/sandbox/seccomp.cpp`). The ADR 0183 layer supports a value list for `ioctl`, but this policy does not use it.
- Impact: the application and the CLI run the helper with pipes, and so does every test. The developer probe run with `> /dev/null` is affected, and so is any caller that sends stdout to `/dev/null`. This record found it only because the fuzz seeds were written to `/dev/null`. The CI fuzz step writes to a file or a pipe, so it does not hit it.
- Close it: add a `TCGETS2` value rule for `ioctl` to the SKP policy (or make the probe's stdio avoid the check), and add a test that runs the helper with stdout on `/dev/null`.

**F3. The package's `bounding` block is not the placed world box (ambiguity, low).**
- Code: `src/skp/import.cpp:580` to `:601` takes the bounding over each definition's own vertices, with no placement applied.
- ADR 0176 §7 S2 says "the bounding box in metres within 1e-6 m of the parser's × 0.0254". Read literally, it is this block, and it differs from the placed box on the owner's file by 0.152 m (x) and 0.994 m (y) at the minimum corner. The mapping test checks the placed box, which is the right quantity.
- The block is not shown in the CLI or desktop report, so no user sees it.
- Close it: amend ADR 0176 §7 S2 to "the placed box (the placements applied to the parser's vertices)", and either rename the block or document it as definition-local.

**F4. The R102.b notes are out of date on `missingUv` (stale note, low).** The R102.b note says the owner's file gave a `missingUv` notice on 159 faces. Today the notice fires only for a textured side with no mapping (`src/skp/import.cpp:682` to `:690`), and the owner's file raises none. The `skp_mapping` test asserts that. The R102.e notes record the change; the R102.b text is not edited here.

**F5. Limits and claims with no test (test gap, not a defect).** These are in the code and in the user guide, and no test exercises them:
- the 256 MiB input default (only the override is tested);
- the helper's address-space, CPU and output refusals (`RLIMIT_AS`, `RLIMIT_CPU`, `RLIMIT_FSIZE`);
- the package's geometry chunk over 256 MiB, a texture over 16 MiB, and a texture over 4096 px;
- the native caps on the skp path (bodies, placements, materials, nesting) and `uvPerspective`;
- the JPEG path of the texture rule.
Each is a claim in §5 marked "code only". The parent decides whether to add tests.

**F6. The S-labels in the tests are not the ADR's (naming, low).** Tests in `tests/skp_mapping_tests.cpp` and `tests/skp_import_tests.cpp` use S5, S6 and S7 for other things (materials, texture size, refusals, the fuzz). The ADR §7 uses S5 for textures against a screenshot, S6 for refusals and S7 for the decoder and the sanitizers. Read the test by its content, not its label.

## 7. Open items and what would close them

| Item | What is open | What closes it |
| --- | --- | --- |
| S5 textures against a screenshot | No textured owner file. The check needs a SketchUp screenshot | One owner file with textures, nested or not, and a screenshot of the same view from SketchUp 2024 |
| Versions 2021 to 2023 and 2025 and later | One real VFF file (2024) | Owner files saved by SketchUp 2021, 2022, 2023 and 2025 (and 2026 if the owner has one). Each needs a corpus run of S1 to S4 and the fidelity check in §3 |
| 2013 to 2020 (best effort) | No real file. The legacy path is synthetic only. Issue 442 is not reproduced here | Owner files from SketchUp 2013 to 2020, at least one per version group, each with the fidelity check. The ADR §7 corpus list also asked for textured, nested, scaled and mirrored components, hidden tags, and one large model for timing |
| Dynamic Components (C5, C6) | No real file with Dynamic Components | One owner file with Dynamic Components, to check the read-only properties and the `Copies` count |
| F1 (the notice threshold) | Owner's decision | The decision, then the code or the wording, and a test |
| F2 (the sandbox and `/dev/null`) | A defect | A policy rule for `ioctl` `TCGETS2` and a test with stdout on `/dev/null` |
| F3 (the S2 wording) | ADR text | The ADR amendment in §6 |
| F5 (the limit tests) | No tests for the §5 and §6 limits | Tests at each limit, or a narrower user-guide claim |
| Desktop dialog suite (§9) | Not built here | The CI native tier on the PR head (`x11-1x`, `x11-2x`, `wayland-1x`), or a local desktop build |

## 8. Test runs

All on `fafc0431`, from the worktree's `build/release` (Release, BFD linker, desktop off, CLI on), except where noted. Memory caps as the worker rules require: test binaries under `ulimit -v 8000000` and a 600 s timeout; builds and the ASan and fuzz runs under `systemd-run --user --scope -p MemoryMax=10G -p MemorySwapMax=0`.

| Run | Result |
| --- | --- |
| Configure `build/release` (`-DCMAKE_BUILD_TYPE=Release -DCMAKE_LINKER_TYPE=BFD -DSKETCHYUP_BUILD_DESKTOP=OFF -DSKETCHYUP_BUILD_CLI=ON`) | OK |
| Build the skp targets (`sketchyup-skp-worker`, `sketchyup-cli`, the extension and text workers, `sketchyup_assemblies`, `skp_package_tests`, `skp_import_tests`, `skp_mapping_tests`, `skp_dc_tests`, `sandbox_tests`, `skp_sandbox_tests`) | OK, 313 steps |
| `ctest -j2` on the 11 CI skp tests, without the corpus | 8 passed; 3 skipped (`skp_import_real`, `skp_mapping_real`, `skp_dc_real`, which exit 77 without the variable) |
| `skp_import --real`, `skp_mapping --real`, `skp_dc --real` with `SKETCHYUP_SKP_CORPUS` | All pass (exit 0); the owner's counts as in §3 |
| `sketchyup-cli --import-skp` on the owner's file (with `--skp-worker`) | Imported; report as in §3 |
| Helper `--probe` on the owner's file | The counts in §3 |
| Timing and peak memory (`measure.py`, 5 runs each) | §3 |
| `skp-sanitize` configured with `-DCMAKE_LINKER_TYPE=BFD`; built the five test targets and the helper | OK, 301 steps |
| `ctest -j2` on `build/skp-sanitize`: `skp_package`, `skp_import`, `skp_mapping`, `skp_dc`, `skp_sandbox` (`ASAN_OPTIONS=detect_leaks=0`) | 5 of 5 passed, no sanitizer report |
| `skp-fuzz` configured as the CI fuzz step does (Clang 23.1.3, RelWithDebInfo, `SKETCHYUP_BUILD_FUZZERS=ON`); built `sketchyup-skp-worker` and `parser_fuzz_skp_package` | OK, 275 steps |
| Five seed packages from the fixtures `box`, `face_with_hole`, `two_materials`, `textured_face`, `nested_component` (1,591 to 2,426 bytes) | Written, with stdout to a file (F2) |
| `parser_fuzz_skp_package -runs=200000 -max_len=65536 -seed=1` | `Done 200000 runs in 25 second(s)`; coverage 1,764; corpus 189 entries (354 KB); RSS 489 MB; no sanitizer report |
| `vendor-openskp.py fetch` and `check`, `pin` | PASS (§1) |
| Stripped local install (`cmake --install --component Unspecified` with `DESTDIR`, `--prefix /usr`; `sketchyup-cli` and the helper stripped) and `verify-skp-library.py package --prefix` | "Checks L1 to L3 passed" |
| Header-patched copies of the box fixture with versions 25 and 26 (F1) | Refused (`parse`, no ZIP container). No notice is reachable this way |
| Header-patched box copy as a 2013 file (13) | Not refused as pre-2013 (in `skp_import`) |

## 9. Not run

- **The desktop application and `skp_import_dialog_tests`.** The worker and the CLI were built with `SKETCHYUP_BUILD_DESKTOP=OFF`. The dialog suite covers the refusal sheets, the report, Cancel and the deadline (the `skp_import_dialog` cases at `tests/skp_import_dialog_tests.cpp:481` to `:517`). It runs in CI's X11 and Wayland jobs. The reason for not building it here: this record checks the helper, the mapping and the packaging, and the desktop build is a large additional build on a machine shared with other workers. The PR's CI is the evidence for the dialog suite at this code.
- **The Arch package.** Not built. The package mode ran on a local stripped install with the same files, not on the package.
- **S5 against a screenshot** (§7).
- **Full-tier jobs on the PR.** The sanitizer and fuzz lines are full-tier in CI; this record ran them locally (§8), so the CI full tier is not needed for them to pass here.
- **The pin re-check against upstream.** `vendor-openskp.py check` and `pin` pass. Upstream issue #442 was not re-read for this record. The ADR's R102.d note records its status at 2026-10-25.

## 10. Commits

- `fafc0431`: `origin/main` when this work started. Every check ran on this code.
- The head of `docs/r102e-skp-acceptance` after this PR: the record, its JSON and the ADR notes. No product code changed.

## 11. R102.f: closing the findings

R102.f is branch `fix/r102f-skp-findings`, from `origin/main` at `e12b3a45`. It changes product code, tests and
documents. Commits: `00e44a34` (the helper's policy, the threshold and the hash), `c3237b5a` (the tests and the
labels), `ae84c075` (the chunk test), and the commit that adds this section. The status stays **Delivered, with open
findings**: S5 and the real-file matrix have no new evidence.

The owner's file was read in place for the runs in §11.3. It was not copied, committed or uploaded. Its SHA-256 is
`75ae2786…c739ed` before and after.

### 11.1 Findings

| Finding | Status | Fix | Test | Commit |
| --- | --- | --- | --- | --- |
| F1, the notice fires one version late | Closed | `kNewestTestedMajor` = 24 and `isNewerThanTested` (`src/skp/import.hpp`, `import.cpp`). The label applies above 24, as ADR 0176 section 2 says. F1 lists the threshold as the owner's decision; R102.f applies section 2's 24, as the parent asked, and the owner has not reviewed it | `skp_import`: the decision at 24, 23, -1, 25 and 26 | `00e44a34`, `c3237b5a` |
| F2, the helper is killed with stdout on `/dev/null` | Closed | SKP policy: `ioctl` with request TCGETS or TCGETS2, as a value list (`argumentOneOf`, ADR 0183 S3), the style of the text and extension policies. Any other request is EPERM | `skp_sandbox`: the policy's answers in process, and the real helper with stdout on `/dev/null` in `--probe`, `--import` and `--dry-run` | `00e44a34`, `c3237b5a` |
| F3, S2 wording | Closed (ADR text) | ADR 0176 section 7 S2 amended in place, marked "(amended in R102.f, parent decision)" | none (wording) | the R102.f record commit |
| F4, stale `missingUv` note | Closed (ADR text) | Dated correction under the R102.b note in ADR 0176 | `skp_mapping` asserts no `missingUv` on an untextured model, and the owner's file raises none | the R102.f record commit |
| F5, limits with no test | Partly closed: the limits listed in 11.2 are tested; the rest are listed with the reason | See 11.2 | `skp_sandbox`, `skp_mapping`, `skp_package` | `c3237b5a`, `ae84c075` |
| F6, S5 to S7 reused in test labels | Closed | `skp_mapping_tests` labels the material, mirror and texture-size checks as `materials`, `mirror` and `R100.b2`. S1 to S7 now mean section 7 only | none (labels) | `c3237b5a` |
| F7, found by the F5 test: the helper's hash was quadratic | Closed | `Sha256::update` hashes each block in place and erases the consumed part once. Before, each 64-byte block was erased from the front of a buffer that held the whole input | `skp_package`: the FIPS 180-4 vectors for the 448-bit message and for a million a's | `00e44a34`, `c3237b5a` |

F7 is not in the R102.e list. It is in the helper's import path: `runImport` hashes the whole input, so a large `.skp`
took quadratic time and could reach the 120 s deadline. The owner's file imports in the time §3 recorded (§11.3).

### 11.2 Limits

Tested now:

- The 256 MiB input default: a sparse file of 256 MiB + 1 bytes is refused as `tooLarge` before the helper starts
  (`skp_sandbox`, `testLimits`). The file has no SketchUp header, so a size check after the header check would refuse
  it as `notSkp`.
- The helper's address-space limit (`--rlimit-as`): a stand-in applies the limit the application passes (256 MiB) and
  asks for 512 MiB. The host reports `workerFailed` with the memory reason and the log tail (`skp_sandbox`).
- The helper's CPU limit (`--rlimit-cpu`): a stand-in ends itself with SIGXCPU after two seconds of CPU. The host
  reports a crash (`skp_sandbox`).
- The helper's output limit (`--rlimit-fsize`): a stand-in ends itself with SIGXFSZ. The host reports a crash. The real
  helper given `--rlimit-fsize 1000` is ended by SIGXFSZ, and the file it leaves is no larger than 1000 bytes.
- A texture over 4096 pixels: a PNG whose header says 4097 by 1 is refused by its header, and the import keeps no image
  (`skp_mapping`, built in memory from the fixture's own package).
- A texture over 16 MiB: the decoder refuses 16 MiB + 1 bytes, and reads exactly 16 MiB (`skp_package`).
- A geometry chunk or a texture chunk over 256 MiB: refused from the 32-byte header (`skp_package`).

Not tested, and why:

- The real helper's own address-space and CPU limits. A real helper reaches its CPU limit only after about a second
  of CPU, and the address-space limit depends on the libraries mapped, so a fixed size would be fragile on other
  machines. The stand-ins test the host's reaction to a limit, not the helper's `setrlimit` calls.
- The default 512 MiB output limit, which the application passes. It needs a 512 MiB package. The mechanism is tested
  with 1000 bytes.
- An input of exactly 256 MiB, which is accepted. It would copy 256 MiB on every run.
- The native caps on the `.skp` path: 100,000 faces or edges per body, 100,000 vertices per body, 10,000 geometry
  records, 600,000 faces after placement, 1,024 materials, and nesting over 64 levels. Each needs a package or a model
  the fixtures do not contain.
- `uvPerspective`. No fixture has a perspective UV transform.
- The JPEG texture path. The repository has no JPEG fixture.

### 11.3 Runs

Release tree `build/release`: Release, `CMAKE_LINKER_TYPE=BFD`, `SKETCHYUP_BUILD_DESKTOP=OFF`, `SKETCHYUP_BUILD_CLI=ON`.
Sanitized tree `build/skp-sanitize`: the `skp-sanitize` preset with BFD. Both were built with `-j4` under a 10 GB cgroup.

| Run | Result |
| --- | --- |
| `ctest -j2 -R '^(skp_\|sandbox)'` on `build/release`, no corpus variable | 9 pass. The three `_real` tests skip (no corpus variable). `sandbox_available` passes |
| `ctest -R '^skp_(import\|mapping\|dc)_real$'` with `SKETCHYUP_SKP_CORPUS` set to the owner's directory | 3 of 3 pass. `skp_mapping_tests --real` prints the owner's notices, unchanged from §3: `definitionAttributesNotCarried` 23, `invalidFaces` 1, `modelAttributesNotCarried` 10, `omittedCamera` 1, `omittedConstructionLines` 6, `omittedStyles` 2. No `newerThanTested` |
| `ctest` on `build/skp-sanitize` (ASan and UBSan, `ASAN_OPTIONS=detect_leaks=0`, under a 10 GB cgroup): `skp_package`, `skp_import`, `skp_mapping`, `skp_dc`, `skp_sandbox` | 5 of 5 pass |
| Mutation: the `ioctl` rule removed from the SKP policy, then `skp_sandbox` | Fails: the in-process check, and the real helper in all three modes is ended by SIGSYS (signal 31). The rule is restored, and the suite passes |
| `skp_package` run time | 148 s before the hash fix, 0.15 s after |
| The owner's file, helper, five runs each on this machine (load average 1.3) | `--import`: 14 to 19 ms, median 17 ms (§3 recorded 15.6 ms). `--probe`: 11 to 15 ms, median 14 ms (§3 recorded 11.1 ms). The package is 64,299 bytes, as in §3 |

The timing difference from §3 is within this machine's noise. It does not show that the hash fix changed the owner's
file's import time.

### 11.4 Not done

- The desktop dialog suite, `skp_import_dialog_tests`. It is not built here (§9). Its CI jobs are the evidence.
- S5 against a SketchUp screenshot, and the real-file matrix (§2, §7). No new file was available.
- The IFC policy. It allows TCGETS2 only, and it is not changed here. `ifc_sandbox` was not run, because the IFC worker
  is not built on this host. ADR 0176's R102.f notes explain.
- `docs/RELEASE_NOTES.md`, `docs/SUPPORT_MATRIX.md`, `docs/USER_GUIDE.md` and `docs/PR_ROADMAP.md`. The parent changes
  them after the owner's decisions on the claims. The claims in section 5 stand as written there.
- The full CI tier. The record's runs above are local.

### 11.5 Contradictions

- C17 (section 5) and F1 (section 6): the code now matches ADR 0176 section 2 and the user guide's wording, with the
  threshold at 24. The user guide and the support matrix are unchanged here, so the parent should check their wording
  against the label.
- ADR 0183, section 5. The SKP row's measured calls did not include the `/dev/null` run, which found `ioctl`. The
  R102.f note in ADR 0183 records the addition.
