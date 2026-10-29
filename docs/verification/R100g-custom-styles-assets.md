# R100.g — Custom styles and asset authoring: acceptance record

2026-10-28. Branch `docs/r100g-styles-assets-acceptance`, based on `origin/main` at `e3b45be6` (the merge of PR #490, R100.f2). No file under `src/` changed. The record adds tests, two manual measurement tools and one opt-in measurement hook (§10). The checks are those of [ADR 0174 §10](../decisions/0174-custom-styles-asset-authoring.md#10-verification-plan) (the Verify line and the eight check rows), §9 (the budgets) and the Implementation split. [Summary](R100g-custom-styles-assets.json).

**Status.** The Verify line is **met**. Its four clauses and the eight §10 check rows pass, each with its test, its CI run or a scripted run (§1). The native matrix passes in every cell that CI runs for these suites (§4). The sanitizers pass in CI (§5), and the four local fuzz campaigns find nothing (§5).

Budgets (§6): every §9 row has a measured value and a verdict. Two rows are misses or partial misses, and both are findings for the owner: the import's progress and cancel are not built (F-2), and a catalog dialog selection on a 128 MiB catalog stalls the UI thread for 2.1 s (F-1, and F-3 for its memory). **Owner decisions of 2026-10-28:** the import's progress and Cancel (F-2, OD-5) are not in 1.4.0 and are planned for 1.5; the Styles UX gaps (OD-11) are accepted for 1.4.0 and fixed in 1.5 (§9.2). The edge stroke's frame cost has no §9 row, and the software measurements cannot be judged against the BUILD_PLAN reference (F-4).

The roadmap row's status is therefore "Accepted (R100.g)", with the findings and gaps of §7 and §8 carried. §9 lists the decisions this record made and the owner decisions still open. F06 is released only through the M10 gate, so this record does not claim it for a release.

## 1. Check map

Each row names the test and the assertion that prove the clause, the CI run whose job covers it, and the scripted run made for this record. "Not run" always comes with the reason.

Run identifiers (all in `gh run`, repository `zygote55/sketchyup`):

- **P490**: PR #490's full-tier run `40653485697` (head `86b5f333`, the branch head that merged as `e3b45be6`). CI verdict job `122749613902`, success. It carries the R100.f2 Styles tab and the whole R100 code path.
- **P482**: PR #482's full-tier run `40567744599` (head `342de6b6`). CI verdict `122480739254`, success.
- **P485**: PR #485's full-tier run `40604131920` (head `4c444c4d`). CI verdict `122583564251`, success.
- **P488**: PR #488's fast-tier run `40591430010` (head `6f9125bf`). It has no full-tier run: the PR carried no `full-ci` label. CI verdict `122500479969`, success.
- **M482**, **M488**, **M485**: the main push runs for the merges of #482 (`40587173992`), #488 (`40592819876`, fast tier) and #485 (`40617677853`). The last one's CI verdict failed on the IFC helper (§8, F-7); its native cells passed.
- **N**: the nightly full tier on main, `40503622529` (`61b2018b`). It predates the four merges, so it covers none of their code.

### 1.1 Verify line (ADR 0174, R100 row)

Verify: "A style/catalog relocates cleanly, preserves appearance and reports missing dependencies; assets are never substituted silently."

| Clause | Proof: test and assertion | CI job (run) | Scripted run (this record) | Result | Gap |
| --- | --- | --- | --- | --- | --- |
| **Relocates cleanly (style)** | `style_bundle_tests` `noExternalDependency` (`tests/style_bundle_tests.cpp`, line 512): the file moved to a new folder reads with its texture present and no path in its manifest; `libraryFolder` (line 540) lists the style folder | P490 `Development build and checks` (`122699391017`): `style_bundle` passed in ctest; M482 and P482 the same job | S13: the style exported in `/home` and imported from `/dev/shm`, a tmpfs mount, gives an identical model (`cmp`) | **Pass** | The desktop Import style from another mount is not run; the CLI runs the same reader |
| **Relocates cleanly (catalog)** | `catalog_tests` relocation (`tests/catalog_tests.cpp`, lines 102 to 111): a moved catalog reads back identically with the same entries and payload bytes; `catalog_import_tests` `relocation` (line 347) | P490 `Development build and checks`: `catalog` and `catalog_import` passed in ctest; `catalog`, `catalog_import` and `catalog_fuzz` passed in the sanitized job (`122699391021`) | S7 and S13: the listing from `/dev/shm` and from `/home` is identical (`cmp`); importing `Oak` from each gives an identical model; S14: the library form gives the file form's model | **Pass** | None |
| **Preserves appearance** | `catalog_import_tests` `exportThenImport` (line 255): the exported style, material and component return with their records, and the material and the style share one texture (line 270); `style_bundle_tests` `roundTrip` (line 185): a schema-31 style re-encodes bit for bit | P490 `Development build and checks` and `Sanitized builds` (`style_bundle`, `catalog_import`) | S1 to S3: a style exported from the schema-31 model, applied to another model and exported again is byte-identical (`cmp`); S10: a catalog exported from the model and imported back keeps the style | **Pass** | None |
| **Reports missing dependencies** | `catalog_import_tests` `missingDependencies` (line 419): the refusal names the entry and the missing asset id (9 in `missing-dependency`), and the other entries stay importable; `style_catalog_input_tests` `refusedInsertReport` (desktop report with both row actions) | P490 `Development build and checks`; P490 `Native tests: X11 1x`, `X11 2x`, `Wayland 1x`, `Wayland 2x` (`style_catalog_input_tests`: "Asset catalogs checks pass") | S9: the three refusals (missing asset 9, a tampered asset, a tampered entry) exit 1 and name the entry and the dependency; S7: `catalog.list` marks the tampered asset `missing` and the tampered entry `importable: false` | **Pass** | The report's keyboard rows and accessible names are not tested (R100.f3 gap; §7) |
| **Assets are never substituted silently** | `catalog_import_tests` `equalHashDifferentPayload` (new in this PR, after `missingDependencies`): an asset whose recorded hash is another asset's hash does not verify, its entry is refused by its own hash, and the valid entry keeps its own bytes; `style_bundle_tests` `missingAndMismatchedTextures` (the apply and export refusals name the texture) | P490 `Development build and checks`; the new test runs in CI when this PR is open (`catalog_import`) | S11: a style whose texture byte is damaged is refused with `hashMismatch` and "no texture was substituted"; no output file (S11, `cmp`); S7: `assetStatus: missing` for the tampered texture | **Pass** | `document.describe` does not report the edge texture's state (R100.f2 gap 5; §7) |

### 1.2 §10 check rows (ADR 0174 §10)

| §10 check | Proof: test and assertion | CI job (run) | Scripted run or local run (this record) | Result | Gap |
| --- | --- | --- | --- | --- | --- |
| A style relocates cleanly and preserves appearance (move, re-scan, apply; record and texture hash bit for bit) | `style_bundle_tests` `noExternalDependency`, `libraryFolder` and `applyAndExport` (line 437) | P490 `Development build and checks` | S1, S2, S3, S13 (above). The cross-mount run uses `/dev/shm` (tmpfs), a different mount from `/home` (ext root) | **Pass** | Real removable media not used |
| A catalog relocates cleanly; an inserted entry's records and assets equal a decode in place | `catalog_tests` relocation (lines 102 to 111); `catalog_import_tests` `relocation` (line 347) | P490 `Development build and checks` and `Sanitized builds` | S8 and S13: the imported model from `/dev/shm` equals the one from `/home` (`cmp`); S14 for the library form | **Pass** | None |
| The missing-asset and tampered-hash fixtures: the entry is not imported, the report names the entry and the dependency id, the destination is unchanged | `catalog_import_tests` `missingDependencies` and `refusalsLeaveDocumentUnchanged` (line 509), `requireUnchanged` | P490 `Development build and checks`; desktop refusal in P490 X11 1x and X11 2x | S9: three refusals, each named, with no output file | **Pass** | None |
| An equal-hash, different-payload asset is refused (ADR 0131); no fallback colour or texture for a missing style texture; `assetStatus` and `missingAssets` reflect the state | `catalog_import_tests` `equalHashDifferentPayload` (new); `style_bundle_tests` `missingAndMismatchedTextures`; `style_catalog_input_tests` `missingTextureState` (line 1171) | P490 X11 1x (`style_catalog_input_tests` `missingTextureState`), Development build | S7 (`assetStatus` per asset); S11 (`hashMismatch`, nothing substituted) | **Pass** | `missingAssets` is not named per entry in `catalog.list`, which lists per asset (ADR §5 asked for the entry; carried, §7) |
| Refusals leave document, history, revision and encoding unchanged (invalid style field, missing dependency, hash mismatch, over limit) | `catalog_import_tests` `refusalsLeaveDocumentUnchanged` (line 509) and `exportRefusals` (line 538); `style_bundle_tests` `commands` (line 572: "refused without a change") | P490 Development build and `Sanitized builds` | S5 (existing output refused), S9 and S11 (no output file, the folder listing before and after compared); the over-limit refusal is not in the scripted set, and the desktop refusals are `style_catalog_input_tests` `importRefusals` (texture over 16 MiB) and `openCatalogFile` (a file over 130 MiB) | **Pass** | The 130 MiB over-limit refusal is the desktop test's (`openCatalogFile`), not a CLI run |
| Each import is one undo step; save and reopen is bit for bit, including the `edge` sub-record and its assets | `catalog_import_tests` `oneUndoStep` (line 325) and the save and reopen check (lines 313 to 321); `style_bundle_tests` `applyAndExport` (line 437: apply is one undoable edit) and `roundTrip` (line 185) | P490 `Development build and checks` (`catalog_import`, `style_bundle`) | Not scripted (the CLI writes new files and has no undo) | **Pass** | None |
| Determinism: the same import on two copies gives identical ids, records and hashes; hashes pinned per fixture | `catalog_tests` line 66 (identical bytes for the same inputs) and its fixture SHA-256 pins; `style_bundle_tests` `styleFileFormat` (line 312) | P490 Development build | S8 and S13 (identical models from two copies on two mounts); S14 (library and file forms identical) | **Pass** | None |
| Fuzzing: a libFuzzer target for the `edge` sub-record decoder and the catalog manifest and entry decoder, each run twice per input | `parser_fuzz_edge_style` and `parser_fuzz_catalog` (the harness decodes each input twice and compares, `tests/fuzz/parser_harness.hpp`); `catalog_fuzz_tests` (ctest) | P490 `Development build` (`catalog_fuzz`, ctest #88) and `Sanitized builds` (`catalog_fuzz` passed). **No CI job runs a libFuzzer step for these formats** (R100.c gap) | Local campaign, §5: 6,478,855 runs (edge), 507,127 runs (catalog), 5,811,905 runs (material texture size), no finding | **Pass** (locally; not in CI) | CI runs no libFuzzer campaign for these formats (§7) |

### 1.3 Other R100.g checks

- **Released 1.3.0 CLI refuses schema 31 and 33.** The installed `/usr/bin/sketchyup-cli` (`sketchyup-cli 1.3.0`; its package `sketchyup-1.3.0-1-x86_64.pkg.tar.zst` has SHA-256 `5f5c0cf8…a833b`, which is the value in the release `SHA256SUMS`) refuses `edge-styles-v31.sketchyup` and `material-texture-size-v33.sketchyup`. Each refusal is exit 1 with `{"code":"INVALID_OPERATION","error":"Unsupported container field","status":"failed"}`, and each copy is unchanged (`cmp`). This holds for `--validate-native`, `--inspect-native` and `--migrate-native` (S12, and the `--validate-native` runs in the scratch folder `refusal`). The 1.2.0 CLI (package SHA-256 `b95fc189…31295d`, extracted, not installed) gives the same result. The in-repo tests `style_bundle_tests` and `material_texture_size_tests` (`priorRelease`) pass with `SKETCHYUP_PRIOR_CLI` set to each of the two (§2).
- **Wording.** The refusal text is "Unsupported container field", not the "newer-format error" that ADR §2 and §6 expect. R100.a and R100.b2 recorded the same difference. This record keeps the measured text (§9, RL-2).

## 2. Test runs

| Run | Where | What ran | Result |
| --- | --- | --- | --- |
| Local, Release (`build/release`: GCC 16.2.1, `CMAKE_BUILD_TYPE=Release`, Qt 6.12.0, BFD linker, desktop and CLI on) | this record | `ctest -R` on the 23 R100 suites: `model_style`, `materials`, `catalog`, `catalog_fuzz`, `catalog_import`, `style_bundle`, `material_texture_size`, `texture_presets`, `texture_commands`, `texture_export`, `texture_image`, `face_textures`, `assistant_tool_surface`, `native_format`, `mcp`, `inspection`, `commands`, `model_style_commands`, `model_style_io`, `style_catalog_cli`, `edge_appearance`, `edge_visibility`, `texture_mapping`, under `ulimit -v 8000000`, `-j2` | **23 of 23 passed** (final state, after this PR's test additions) |
| Local, Release, prior-release checks | this record | `style_bundle_tests` and `material_texture_size_tests` with `SKETCHYUP_PRIOR_CLI` set to `/usr/bin/sketchyup-cli` (1.3.0), and `style_bundle_tests` with the 1.2.0 CLI | all passed; each prints "prior release refusal: … Unsupported container field" |
| Local, Release, offscreen (`QT_QPA_PLATFORM=offscreen`, software GL), scale 1 and 2 | this record | `style_catalog_input_tests`, `style_input_tests`, `style_viewport_tests`, `edge_stroke_viewport_tests` | all passed at both scales |
| Local, Release, offscreen, the same scales | this record | `material_input_tests` | **fails** at its last check, "Material controls retain live renderer" (`view->rendererReady()`), at both scales. Offscreen creates no GL context, as R100.b recorded. The check passes in CI (§4) |
| Local, Release, offscreen, the audit groups | this record | `native_accessibility_audit --dialogs-keyboard=catalogs` and `=styles` at both scales | `catalogs` passed at both scales. `styles` failed at both scales in `reference-import` and `style-import` (Escape and Enter did not close the dialog; the focus check failed). These two cases pass in CI at X11 2x and Wayland 1x (§4). Offscreen has no window activation, so this is not counted as a defect (F-5) |
| Local, Release, the dialog timing hook | this record | `style_catalog_input_tests` with `SKETCHYUP_R100G_CATALOG` set to the 128 MiB catalog (§6) | passed; the two timings are in §6 |
| Local, Release, the edge stroke benchmark | this record | `edge_stroke_benchmark 60` (four runs) and `100` (two runs), offscreen | §6 |
| Local, Debug and sanitizer builds | none | not run here | CI runs them (§5). The fuzz build (clang, ASan and UBSan) is the local sanitizer run for the decoders (§5) |

## 3. Scripted runs

The CLI runs use the Release `build/release/sketchyup-cli`, with `HOME` and `XDG_DATA_HOME` in a scratch folder so that the user's catalog library is untouched. Inputs are the repository's fixtures, copied to scratch. Each command's exit status and output are in `.local/r100g-logs/cli-scripted.log` (not committed). Cited sizes and hashes are from that log.

| # | Command (short form) | Result | Clause |
| --- | --- | --- | --- |
| S1 | `--input v31 --export-style edge.sketchystyle --name "Edge style"` | exit 0, 946 bytes, texture `present`; the source is unchanged (`sha256sum -c`) | style relocation, appearance |
| S2 | `--input plain --import-style edge.sketchystyle --output applied.sketchyup` | exit 0, `status: applied` | appearance |
| S3 | `--input applied --export-style reexport.sketchystyle --name "Edge style"` | exit 0; `cmp` identical to S1's file (`sha256` `b68de3d5…`) | appearance |
| S4 | the same style imported again | exit 0, `style.status: unchanged` (the neutral result; a copy is written, as R100.e's decision 6 says) | R100.e decision 6 |
| S5 | an existing `--output` path | exit 1, `INVALID_REQUEST`, "existing files are never replaced" | refusal |
| S6 | `fixture-style` (`edge-style-v1.sketchystyle`) imported | exit 0 | — |
| S7 | `--list-catalog` on the five catalog fixtures | exit 0 each; `tampered-asset` lists its texture `assetStatus: missing`; `tampered-entry` lists `importable: false` with the reason "entry hash does not match its bytes" | reporting |
| S8 | `--import-catalog edge-texture.sketchycat --entries Oak --output oak.sketchyup` | exit 0; one material entry | relocation, determinism |
| S9 | three refusals: `missing-dependency` (`Oak`), `tampered-asset` (`Oak`), `tampered-entry` (`Plain edges`) | exit 1 each. The messages name the entry and the dependency: "missing dependency asset 9"; "dependency asset 1 does not match its hash"; "entry hash does not match its bytes". The folder listing before and after is identical, so no file was published | missing dependencies, refusals |
| S10 | `--input v31 --export-catalog from-v31.sketchycat --catalog-style --name "From v31"`, then listing and import of `Document style` | exit 0; the catalog lists one importable style entry named "Document style" and its texture `present`; the import reports `styleChanged: true` | appearance, round trip |
| S11 | a style whose texture byte is damaged (`damaged.sketchystyle`) | exit 1, `INPUT_ERROR`, `hashMismatch`, "nothing was imported and no texture was substituted"; no output file | no substitution |
| S12 | the 1.3.0 and 1.2.0 CLIs: `--inspect-native` and `--migrate-native` on `v31` and `v33` | exit 1 for every run, `Unsupported container field`; each copy unchanged (`cmp`) | released-reader refusal |
| S13 | relocation: the style and the catalog copied to `/dev/shm` (tmpfs) and to `/home` (the root mount): listings `cmp`-identical; the imports `cmp`-identical | exit 0 each | relocation, determinism |
| S14 | the library form (`--list-catalog edge-texture`, `--import-catalog edge-texture`) against the file form | exit 0; the model is `cmp`-identical to the file form's; a missing name gives `NOT_FOUND` | library, determinism |

The `--validate-native` runs of both released CLIs on `v31` and `v33` are in the scratch folder `refusal` (not in this log); they gave the same refusal and `cmp`-identical copies (§1.3).

**The 128 MiB catalog through the CLI** (`cli-budget.sh`, §6): the listing takes 0.26 to 0.30 s and 223 MB peak; the import of one entry takes 0.59 to 0.61 s and 414 to 432 MB peak. The file form and the library form give identical models (`cmp`).

## 4. Native matrix

ADR 0174's Implementation split names X11 and Wayland at 1× and 2×. The support matrix supports Linux x86_64 only, so Windows and macOS are not run. The cells come from P490 (the full tier at the merged head). Each cell passed (the job concluded success, and the workflow runs each command under `set -e`, so a failing suite fails the job). The logs were read for the suite lines.

| Cell (job) | `style_catalog_input_tests` | `material_input_tests` | Dialog audit groups `catalogs`, `styles` | Notes |
| --- | --- | --- | --- | --- |
| X11 1× (`122716668616`) | passed: "Asset catalogs checks pass … style checks pass" | passed: "Native swatches, retained errors, precision, side assignment … passed; DPR=1" | not run here: the audit runs at X11 2× and Wayland 1× only (`native.yml`) | |
| X11 2× (`122716668745`) | passed, DPR 2 | passed, DPR 2 | passed: `[audit] asset-catalogs`, `export-asset-catalog`, `style-*`, `scene-*`; no "Case aborted" line | |
| Wayland 1× (`122716668661`) | passed, DPR 1 | passed, DPR 1 | passed (the same groups) | |
| Wayland 2× (`122716669445`) | passed, DPR 2 | passed, DPR 2 | not run: `native.yml` runs the audit at X11 2× and Wayland 1× only; its comment names the four-way audit as local evidence (R084.u) | |

Other P490 cells: the sanitized desktop suites (§5) and the audit job (`122716668660`) are green. Earlier PRs ran the same cells: P482 (`122435787211`, `122435787442`, `122435787563`, `122435787387`) and P485 (`122565209700`, `122565209609`, `122565209662`, `122565209664`) concluded success for all four display cells.

**Not run in CI, named:** the audit at X11 1× and at Wayland 2× (not in the workflow); Windows and macOS (not supported); the Qt 6.8 reference compile runs, and it passed (`122699391069`, compile only), so the byte pins of `texture_preset_tests` were not run on Qt 6.8 (R100.b gap). The 200 % text check is in the desktop suite (`largeText`), not a separate cell.

**Not run locally:** Xvfb and Weston are not installed here, so the native cells were not repeated here. The offscreen runs of §2 are supplementary and are not counted as native cells.

## 5. Sanitizers and fuzzing

**Sanitizers (CI).** The `Sanitized builds` job of P490 (`122699391021`, concluded success) ran, under ASan and UBSan:

- the bounded inspection and CLI set (144 of 144 passed): `catalog`, `catalog_fuzz`, `catalog_import`, `style_catalog_cli`, `style_bundle`, `material_texture_size`, `texture_presets`, `native_format`, `model_style_commands`, `model_style_io`, `assistant_tool_surface` and `mcp`;
- the desktop suites on Wayland at 2× (`scripts/test-wayland.sh build/sanitize/...`): `style_input_tests`, `style_viewport_tests`, `edge_stroke_viewport_tests` and `style_catalog_input_tests`, and `texture_input_tests`;
- the core without Qt (74 of 74).

The job's log has no `AddressSanitizer`, `LeakSanitizer` or `runtime error` line. **Not sanitized:** `material_input_tests` (it is not in the sanitize list; §7).

**Fuzzing (local campaigns; CI has none).** The three R100 formats have libFuzzer targets (`tests/fuzz/parser_harness.hpp`, formats `edge-style`, `catalog` and `material-texture-size`). They were built with clang 23.1.3 (`-fsanitize=fuzzer-no-link,address,undefined`, `-fno-omit-frame-pointer`, RelWithDebInfo with `-O1 -g`, no desktop, no CLI, `SKETCHYUP_BUILD_FUZZERS=ON`). Each ran for 240 seconds, `-seed=1 -max_len=1048576 -timeout=10 -rss_limit_mb=3072`, from the seed corpora that `parser_fuzz_seeds` writes, under a 10 GB cgroup cap (ASan cannot run under `ulimit -v`):

| Target | Seeds | Runs in 241 s | Exec/s | Corpus after | Peak RSS | Findings |
| --- | --- | --- | --- | --- | --- | --- |
| `parser_fuzz_edge_style` | 12 | 6,478,855 | 26,883 | 116 units | 546 MB | none |
| `parser_fuzz_catalog` | 7 | 507,127 | 2,104 | 138 units | 615 MB | none |
| `parser_fuzz_material_texture_size` | 8 | 5,811,905 | 24,115 | 93 units | 561 MB | none |

Each harness decodes each input twice and requires equal results, and it checks that an accepted input re-encodes canonically. No artifact file was written. These are finite campaigns of 4 minutes each, so they give measured coverage, not proof (PARSER_FUZZING.md). The catalog target is the slowest, at about 2,100 inputs a second. Its campaign is the shortest in executions.

**Gap.** CI has no libFuzzer campaign for these three formats (the only libFuzzer job is the SKP package decoder). That is R100.c's gap and R099.e's, carried as a CI-time decision (§9, OD-13).

## 6. Budgets (ADR 0174 §9)

Measured in the Release build of §2, on the machine with 22 CPU threads and 30 GB RAM. Other workers were building during some runs, and the 1-minute load average at the start of each series is given. Raw output is in `.local/r100g-logs/` (not committed). Budgets are quoted from ADR 0174 §9 and not changed. The verdicts are frozen in the ADR's §9 table.

**The catalog measured here.** `style_catalog_benchmark write` (in `tests/style_catalog_benchmark.cpp`) writes a 134,107,758-byte file (127.895 MiB) that the envelope accepts: four shared textures of 16 MiB less 16 KiB each, referenced by four material entries, and four components, each with a slab carrying its own 16 MiB less 16 KiB texture. The textures are random bytes labelled `image/png`. The catalog reader does not decode a texture, and the components' payloads decode as native containers, so the measurement covers the file's validation and the decode of its records, not an image decode. An image decode of a 16 MiB texture is not in any §9 row.

| §9 row | Budget (ADR 0174 §9) | Measured (Release) | Runs and load | Verdict |
| --- | --- | --- | --- | --- |
| Apply a style (one labelled style edit) | No measurable cost beyond the existing style edit | `style.apply` against the equal `document.style` edit, on a 10,000-face model (the body limit; 100,000 faces is refused by the document limit): **apply p50 4.45 ms, p95 5.55 ms, max 11.2 ms; edit p50 4.03 ms, p95 4.14 ms, max 4.33 ms**. The extra 0.42 ms at the median is the `.sketchystyle` decode and the texture reuse; the record change is the same | 40 each, alternating two styles; load 0.59 | **Marginal.** The edit costs the same. The apply adds 0.4 ms (10 %) at the median and 1.4 ms at p95, which is the file's decode |
| Decode and validate one 130 MiB `.sketchystyle` or `.sketchycat` | Bounded by the 130 MiB envelope; reported | `decodeCatalog` on the whole file, every entry and asset hash checked: **p50 200.5 ms, p95 203.1 ms** (5 runs; 127.9 MiB). `loadCatalog` (read and decode): p50 245.5 ms. The CLI `--list-catalog`: 0.27 s wall, 223 MB peak RSS | 5 in process, 3 CLI; load 0.75 and 0.68 | **Reported.** The file is inside the bound (127.9 MiB of 130 MiB) |
| Catalog scan (one file, full decode) | Reported; within the existing 256 MiB read allowance | `scanCatalogLibrary` on a folder holding the file: **p50 256.3 ms, p95 258.6 ms** | 5; load 0.75 | **Pass.** 128 MiB is inside the 256 MiB allowance |
| Import one catalog entry (one composed edit); ≤ 3 s, with progress and cancel, for a 130 MiB source (*proposed*) | ≤ 3 s, with progress and cancel | Time: the CLI `--import-catalog` of one entry, 0.59 to 0.61 s wall (file and library forms), 414 to 432 MB peak. In process, `loadCatalog` plus `importCatalogEntries` is 245 + 41 ms. **There is no progress and no cancel**: the import is one synchronous batch | 3 CLI runs per form; 5 in process; load 0.68 to 0.87 | **Pass (R100.i, PR #521).** The R100.g verdict was **Partly met** (progress and cancel not built, F-2). R100.i builds them; the 3 s bound still holds with a wide margin (§ R100.i below) |
| Style or catalog relocation re-scan | Reported | Not run on the 128 MiB file: a move is a rename, and its re-scan costs what the listing and the library scan cost above (CLI listing 0.27 s; `scanCatalogLibrary` 256 ms). The small style and catalog fixtures moved to `/dev/shm` re-scan with identical listings (S13) | 3; load 0.68 | **Reported** (the 128 MiB move itself is not run) |
| Added resident memory (decoded catalog) | Within the existing memory rows (R082) | CLI peak RSS: 223 MB for the listing, 414 to 432 MB for the import. **Desktop, the catalog dialog with the 128 MiB catalog open** (`style_catalog_input_tests` with the hook of §6.1): the test process peaks at **1,632,964 kB (1.63 GB)**, against 265,808 kB (266 MB) for the same process without the catalog, so the catalog adds about 1.37 GB at its peak. The R082.w v2 supported class is 1 GiB for the 100k family | one desktop pair (with and without the catalog); 3 CLI runs each | **Pass for the CLI. Miss for the desktop dialog path** (1.37 GB added, against a 1 GiB supported ceiling). The dry run copies the file several times (§6.1, F-3) |
| *Not a §9 row, measured because R100.f3 left it unmeasured:* the catalog dialog's dry run on each selection change, on a large catalog | None in §9. The nearest is the import row (3 s) | Selection of "Big 1" in the real dialog, five changes each: **opened with Open catalog file…, p50 2,130.6 ms, max 2,149.7 ms; in the library folder, p50 304.4 ms, max 306.4 ms**. The engine's share, replayed: the base64url encode of the file 199 ms, the base64url decode of the request's bytes 1,155 ms, the catalog decode 200 ms; the whole dry run 1,967 ms | 5 each; load 0.57 | **Finding F-1.** The selection is not debounced and runs on the UI thread. BUILD_PLAN gives 100 ms as the routine feedback budget |
| *Not a §9 row:* the edge stroke's frame cost (R100.f2 measured it in CI's software GL; this is the Release build) | None in §9. BUILD_PLAN's frame budget is 16.7 ms p95 at 1920 × 1080 for 100k visible triangles on reference hardware | `edge_stroke_benchmark` (offscreen, llvmpipe, a 1200 × 900 window). **3,600 quads (14,400 edges), 4 runs, mean of 30 frames after warm-up: no stroke 25.87 ms; solid 25.93 (+0.06); dashed 26.06 (+0.19); dotted 27.56 (+1.70, +6.6 %); dash-dot 25.91 (+0.04).** The dotted upload is 14,072,312 bytes, the others 10,427,960. **10,000 quads (40,000 edges), 2 runs: 72.98 to 73.73 ms for every setting (within ±0.6 %)**; each edge is shorter than one dash period, so every setting draws one run per edge | 4 and 2 runs; load 0.5 to 0.7 | **Not decided.** The software GL cannot be judged against the reference-hardware budget. The measured dotted cost is recorded (F-4) |

### 6.1 The desktop measurement

`style_catalog_input_tests` gained `selectionBudget`. It runs only when `SKETCHYUP_R100G_CATALOG` names a catalog, so no CI run does it. It opens the named file with **Open catalog file…** and times five selections of "Big 1" (each a `setSelected(true)` that runs the dialog's refresh), then does the same with the file in the library folder. It is a measurement hook, not a check. Its results are those of the table above.

### 6.2 The edge stroke benchmark and CI

`edge_stroke_benchmark` is the R100.f2 manual tool. Its numbers are software GL in a Release build, and they are not CI numbers. The R100.f2 CI numbers (Debug, Xvfb, Mesa llvmpipe) were 29.8 ms with no stroke and 34.8 ms for dotted at 3,600 quads. The relative cost of dotted is the same order here (+6.6 % against +17 % there), so the two runs agree that dotted is the expensive pattern.

## 7. Gaps

Each gap that an R100 implementation note recorded is listed here. "Resolved" names the evidence in this record. "Carried" names the owner decision in §9 or the work that is left.

| Note | Gap as recorded | Here |
| --- | --- | --- |
| R100.a | Schema-30 migration fixture and migration notice (ADR §2, §6): a schema-30 document has no edge fields to drop | **Resolved as a record decision (RL-3).** Nothing to migrate; the ADR text is contradicted, and the notes say so |
| R100.a | Read range 1–30 (ADR §6) and "≤ 29" (ADR §2) | **Resolved by RL-3.** The read range is 1–31, as the code and `docs/api/native-format-v1.json` say |
| R100.a | `assetStatus` and `missingAssets` not extended to name the entry (ADR §5) | Partly: `catalog.list` names each asset's status and the entry's reason. The per-entry `missingAssets` is **carried** |
| R100.a | The edge stroke key name `edgeStroke` and the `.sketchystyle` layout ("owner: confirm") | **Carried to the owner** (OD-2, OD-3) |
| R100.a | The libFuzzer target was not built | **Resolved** by the local campaign (§5) |
| R100.a | Budgets not measured; no frame or screenshot evidence | **Resolved** by §6 (budgets) and the edge stroke measurement |
| R100.b | The repeat size does not reach the model | **Closed** by R100.b2 (schema 33) |
| R100.b | Byte stability on Qt 6.8 not shown | **Carried.** The Qt 6.8 compile passes in CI (compile only); the pins were not run on 6.8 |
| R100.b | Q1 open questions 1 to 5 (ambientCG, one tint per preset, Glass opacity, tile sizes on screen, mipmap aliasing at 1× and 2×) | **Carried to the owner** (OD-12). The aliasing check (question 5) was not measured |
| R100.b | `material_input_tests` has a failing check offscreen | **Resolved**: CI passes it at all four display cells (§4). The offscreen failure is the known one (§2) |
| R100.c | Component definitions not deduplicated (a texture shared by two components is stored twice) | **Carried** (OD-8): a native-format decision with a schema question |
| R100.c | No libFuzzer smoke step in CI | **Carried** (OD-13). The local campaign closes the evidence for this record |
| R100.c | `catalog.list` inline bytes capped near 12 KiB | **Resolved** by R100.d's library reference |
| R100.d | Templates from a catalog cannot be opened as a model | **Carried** (OD-9) |
| R100.d | Placement at the origin only (no transform parameter) | **Carried** (OD-10) |
| R100.d | Component-definition deduplication (the R100.c gap) | **Carried** (OD-8) |
| R100.d | `catalog.library` and `--to-library` see only the default folder | **Carried** (UX §18.8 gap 4; the CLI and MCP read the default folder by design) |
| R100.d | Allocator floors not checked after undo | **Carried** (minor; the ids stay advanced, as the note says) |
| R100.e | `--import-catalog` into a new document or a template, as ADR §7 wrote | **Carried** (OD-9); the CLI imports into `--input` and writes a new file, which the note records |
| R100.e | No style library option | **Carried** (UX §18.8 gap 3) |
| R100.e | Sanitizer job, Qt 6.8 compile, package acceptance not run locally | **Resolved** in CI: the sanitized suites (§5), the Qt 6.8 compile and the Arch package acceptance (`122699391120`) passed in P490 |
| R100.f1 | Stroke-off path not compared with a pre-change image | **Carried.** It needs the pre-R100.f1 binary; the stroke-off quads are the same by construction, as the note says |
| R100.f1 | Dash and texture phase start at each edge's visible start | **Carried** (a design limit, noted in R100.f1) |
| R100.f1 | A texture that is present but refused draws hatched with no chip | **Carried** (a UX choice; the viewport's "Some images are unavailable" summary covers it) |
| R100.f1 | No window-level test of the chip | **Carried** |
| R100.f1 | Qt 6.8, sanitizers and Wayland run only in CI | **Resolved** (P490: Wayland cells, sanitized `edge_stroke_viewport_tests`) |
| R100.f1 | The dash budget unmeasured | **Resolved** as far as software GL allows (§6); the reference hardware is carried (F-4) |
| R100.f2 | Choose folder… not exercised by the suite | **Carried.** The preference it writes is read by the scan, which is tested |
| R100.f2 | No texture tile in the library row or the Import preview (§18.2 gap 2) | **Carried** |
| R100.f2 | 200 % check shallow | **Carried** |
| R100.f2 | Row menu not tested through the popup | **Carried** |
| R100.f2 | The 4,096-entry scan stop not tested | **Resolved**: `style_bundle_tests` `libraryFolder` now scans 4,097 files and checks the stop and its notice (this PR) |
| R100.f2 | Save to library's refusal for a missing texture says "Save refused:" | **Fixed (R100.h, PR #520)** |
| R100.f2 | Dash cost measured on software GL only | **Partly resolved** (§6, Release); the reference hardware is **carried** (F-4) |
| R100.f2 | `document.describe` does not report the edge texture's status (gap 5) | **Carried** (the engine's MCP surface is pinned) |
| R100.f2 | No fixed library name for MCP or the CLI (gap 3) | **Carried** |
| R100.f3 | The neutral result (the model already has the entries) written, untested | **Carried** (desktop; the engine's no-op refusal is tested in the batch path) |
| R100.f3 | The engine's document-level refusal "Insert is off: …" untested | **Carried** |
| R100.f3 | The report's keyboard rows and accessible names not checked | **Carried** |
| R100.f3 | The narrow-dialog stacking rule not tested | **Carried** |
| R100.f3 | **Save report…** not built (the mock has it) | **Fixed (R100.h, PR #520)** |
| R100.f3 | The bar fills in three colours, not solid, hatched and cross-hatched (UX §18.7) | **Fixed (R100.h, PR #520)** |
| R100.f3 | Templates listed and refused | **Carried** (OD-9) |
| R100.f3 | Placement at the origin only | **Carried** (OD-10) |
| R100.f3 | Component definitions stored in each entry | **Carried** (OD-8) |
| R100.f3 | An insert of a large (130 MiB) catalog and its dry run unmeasured | **Resolved** for the insert (the CLI runs the same engine path, 0.59 s) and for the dry run (§6, F-1) |
| R100.b2 | The `.sketchylib` with a sized material untested | **Resolved**: `material_texture_size_tests` `catalogFileCarriesSize` (this PR) exports a sized material with `catalog.export`, writes it to a `.sketchycat` file, reads it back, imports it, and checks the size, the file bytes and a second export's bytes |
| R100.b2 | The viewport's repeat not seen on a GL context | **Carried** |
| R100.b2 | The Qt 6.8 minimum not run | **Carried** (as R100.b) |
| R100.b2 | Budgets not measured; fuzz format built only with clang | **Resolved** (§6, §5) |
| R100.b2 | glTF and OBJ import leave the size unset; no test | **Carried** (no test added; it is by construction) |
| R100.b2 | "Empty edit" refusal for an import whose entries are all reused (pre-existing) | **Carried** (unchanged behaviour) |
| R100.b2 | `material.describe` (brief) against `materials.describe` (engine); the brief said style files carry the size | **Resolved by RL-6**: the engine's names stand, and style files carry no materials |

Gaps the brief added (from ADR 0174 and the R100.g brief):

- **The per-selection dry run on a large catalog** (R100.f3): measured, F-1.
- **The edge stroke's frame cost** (R100.f1, R100.f2): measured in Release, §6, F-4.
- **Sanitizers on `material_input_tests`**: not in the sanitize list. **Carried** (OD-14, the CI time directive).
- **The #488 full tier**: PR #488 had no `full-ci` label, so it has no full-tier run of its own. Its code is in P490's full run (the head `86b5f333` includes `6f9125bf`'s files and the merge base). **Resolved by RL-7**: the native evidence for #488 is P490's.

## 8. Findings

Each finding is reported, not fixed here. The brief allows a fix only when it is obvious and behaviour-preserving, and none is.

- **F-1. The catalog dialog's dry run stalls the UI on a large catalog (defect, performance).** On each selection change the dialog runs `refreshInsert` on the UI thread: it base64url-encodes the whole catalog into the batch when the catalog is not in the library folder, and the engine decodes the bytes again. On a 128 MiB catalog opened with **Open catalog file…**, one selection takes **2.13 s** (p50, five runs), against 0.30 s for the library form. Measured on the engine's path: the base64url encode 0.20 s, the base64url decode of the request's bytes 1.15 s, the catalog decode 0.20 s, and the rest of the dry run about 0.4 s. The cause of the 1.15 s decode was not investigated (Qt's base64url decoder on a 179 MB string). **Options:** (a) pass the catalog by library name or an in-process handle, so the bytes never enter the batch (the library form is 0.30 s); (b) run the dry run on a worker thread, as the Quantities panel does (R101.e2); (c) debounce the refresh. The R100.f3 gap said the same. **Owner decision OD-4.**

**Resolved 2026-10-28** on branch `fix/catalog-dialog-dry-run` (PR open for review, not merged). The dialog no longer encodes the opened file into its batch. It passes the catalog it listed, decoded when the file was opened, to the dry run and the insert, under a key that only the dialog can send. The engine still decides every verdict. Option (a) was taken, with an in-process handle in place of the bytes. Option (b) was not needed, because the selection now costs less than the library form does. Same 128 MiB catalog, Release, offscreen, five selections of "Big 1" per run, two runs each, machine load 12 to 17. Before: opened file p50 5,349 and 4,763 ms (max 6,664 and 5,374 ms); library folder p50 847 and 647 ms. After: opened file p50 90 and 106 ms (max 92 and 109 ms); library folder p50 629 and 742 ms. The target was within twice 0.30 s; the opened form is now about 0.1 s. With five selections, p95 and max are the same value, so the max is given. Details: [ADR 0174, Implementation notes (R100.g F-1, F-3)](../decisions/0174-custom-styles-asset-authoring.md).
- **F-2. The catalog import has no progress and no cancel (spec miss, ADR §9).** The row asks for "≤ 3 s with progress and cancel". The time is met (0.59 s in the CLI for a 128 MiB file; 0.29 s in process). The desktop insert and the CLI are one synchronous batch. **Owner decision OD-5, decided 2026-10-28: not in 1.4.0; planned for 1.5.** **Fixed (R100.i, [PR #521](https://github.com/zygote55/sketchyup/pull/521)).** The insert runs on a worker, with a progress sheet and a Cancel that changes nothing (§ R100.i below).
- **F-3. Memory of the desktop dialog path (miss against the R082 supported class).** With the 128 MiB catalog open, the desktop test process peaks at 1.63 GB, 1.37 GB above the same process without it. The dry runs copy the file's bytes several times (the batch string, its JSON copy, the decoded package), so this is the same root as F-1. **Owner decision OD-4** covers both.

**Resolved 2026-10-28**, by the change in F-1. With the 128 MiB catalog open, the test process peaks at 496 MB (before: 1,538 MB), and the same binary without a catalog peaks at 169 MB. The catalog therefore adds about 327 MB, against 1.37 GB before, and the process is within the R082 1 GiB class. The copies that F-3 counted (the base64url string, its JSON copy and the engine's decoded string) are no longer made on a dry run.
- **F-4. The edge stroke's cost on reference hardware is unknown.** The software numbers show dotted at +6.6 % (+1.7 ms) at 3,600 quads and no difference at 10,000 quads. The BUILD_PLAN frame budget needs the reference hardware (the AMD discrete GPU of R082, or the Intel graphics the waiver names as untested). **Owner decision OD-7.**
- **F-5. Offscreen-only failures (not defects in CI).** `material_input_tests` fails its `rendererReady` check offscreen, as R100.b recorded. The `styles` audit fails `reference-import` and `style-import` offscreen (Escape and Enter do not close the dialogs). CI passes all of them on Xvfb and Wayland (§4). Offscreen has no window activation, which explains the focus failures, but this was not verified, so it is not called a platform bug.
- **F-6. Naming in the catalog export (observation).** `--name` names the catalog. The style entry that `--catalog-style` adds is always named "Document style", whatever the catalog is called (S10). This is consistent with R100.e's note, and the entry name is what a person sees in the dialog. Not changed.
- **F-7. Main's CI verdict failed at the #485 merge (not R100 code).** The merge commit `3f9234c0` (run `40617677853`) failed `CI verdict` (`122619185993`): in the IFC job, `ifc_worker_probe` was killed with SIGSYS (exit −31) under the sanitizer IFC build. R100.b2 changed no IFC code. The next fix, #496 (`a40aa0af`, "sched_yield in the IFC helper's sanitizer-only calls"), passed: its push run `40619272531` concluded success. Recorded as a CI incident.

## 9. Decisions

### 9.1 Record-level decisions (made by this record)

- **RL-1. Status.** The roadmap row is "Accepted (R100.g)". The Verify line and the eight §10 rows all hold (§1). The §9 misses are findings, not blockers, which matches R101.f's treatment of its budget findings. The owner can reopen it.
- **RL-2. Refusal wording.** The record keeps the measured text, "Unsupported container field", in every table, and it does not change the product.
- **RL-3. Schema-30 and the read range.** ADR 0174 §2 and §6 describe a schema-30 migration with an edge notice, and a read range of 1–30. Neither holds: schema 30 is `presentation-v1` and has no edge fields, and the read range is 1–31. Nothing is migrated. The ADR's implementation notes say so.
- **RL-4. Budget verdicts.** Verdicts use these words: **Pass**; **Marginal** (met, with a measured excess the row does not cover); **Reported** (the row asks for a report, with no bound); **Partly met** (one bound holds and another clause of the row does not); **Miss**; **Not decided** (no row, or no hardware to judge the row against). The §9 table in the ADR carries the values, and they are frozen; no later measurement changes a value.
- **RL-5. Budget file.** The §6 catalog is a generated file with random texture bytes, which the catalog reader accepts. Its composition is stated in §6, so a later reader can rebuild it with `style_catalog_benchmark write`.
- **RL-6. Engine names.** The brief said `material.describe` and that style files carry the size. The engine's name is `materials.describe`, and a style file carries no material, as R100.b2 recorded. Nothing changes.
- **RL-7. Native evidence for #488.** PR #488 has no full-tier run. Its code is in P490's full run, and the record cites that run for F06's native cells.
- **RL-8. Local campaigns are finite.** Four minutes per target, `seed=1`, no minimisation. The record calls them measured coverage.
- **RL-9. Offscreen results** are local evidence only. The native cells come from CI.
- **RL-10. Test additions.** Three checks were added to existing test binaries (`catalog_import_tests` `equalHashDifferentPayload`; `material_texture_size_tests` `catalogFileCarriesSize`; `style_bundle_tests` `libraryFolder`'s 4,096-entry scan). Two manual tools were added (`style_catalog_benchmark`, a manual tool with no CI line, and the opt-in hook in `style_catalog_input_tests`). None of them changes a product path.
- **RL-11. Fuzz formats in CI.** The record does not add a CI fuzz step, because the owner's CI time directive (2026-10-21) cut CI time. The local campaigns are the evidence.

### 9.2 Owner decisions (still open)

- **OD-1. Accept the record's status** (RL-1) and the F06 release gate (the M10 gate is still required before any release claims F06).
- **OD-2. The `edgeStroke` key name** (R100.a asked; implemented as `edgeStroke`).
- **OD-3. The `.sketchystyle` layout** (a 24-byte header and a manifest, with the texture as the payload; no model; R100.a asked).
- **OD-4. The catalog dialog's per-selection dry run** (F-1, F-3): choose option (a), (b) or (c), or accept the current stall. Taken as option (a) by branch `fix/catalog-dialog-dry-run` (§8); the owner can confirm or reopen it.
- **OD-5. Progress and cancel for catalog import** (F-2): build them, or waive the row's "with progress and cancel" clause. **Decided 2026-10-28: not in 1.4.0; planned for 1.5.** A 128 MiB catalog imports in about 0.6 s.
- **OD-6. The reference-hardware budgets** for the edge stroke (F-4), and the R082 1 GiB ceiling for a desktop session with a large catalog open (F-3).
- **OD-7. Reference-hardware runs** for the edge stroke frame cost (F-4): when, and on which machine.
- **OD-8. Component-definition deduplication** in catalogs (R100.c, R100.d, R100.f3): a native-format change (a schema question and an ADR addendum), or accept the duplicates.
- **OD-9. Templates from a catalog** (R100.d, R100.e, R100.f3): an open-as-new-model command, or keep them refused.
- **OD-10. Placement of a catalog component** (R100.d, R100.f3; UX §18.9 question 5): add a position field, or keep the origin.
- **OD-11. The UX deviations** (R100.f3 and R100.f2): build **Save report…**, the hatched bar, the "Save refused:" wording, or accept the deviations. **Decided 2026-10-28: accepted for 1.4.0; fixed in 1.5.**
- **OD-12. The Q1 open questions** (ambientCG, the tint, Glass opacity, tile sizes on screen, mipmap aliasing at 1× and 2×).
- **OD-13. A CI fuzz step** for the three R100 formats (R100.c, this record §5): add it, given the CI time directive.
- **OD-14. `material_input_tests` in the sanitize job**: add it, given the CI time directive.
- **OD-15. The refusal wording of the released readers** (RL-2): accept "Unsupported container field", or ask for a newer-format message in a later release and amend ADR §2 and §6.

## 10. Publication, commits and tests

- **ADR 0174.** The status line names this record and its verdict. The §9 table carries the measured values and the verdicts. "Implementation notes (R100.g)" records the measurements, the findings, the contradictions and the decisions.
- **Support matrix.** The row "Custom styles and asset catalogs (F06, not in the 1.3.0 declaration)" in Exchange boundaries. It states what is claimed and the limits. F06 is released only through the M10 gate.
- **User guide.** "Custom styles and asset catalogs", after "Presentation batches".
- **Roadmap.** The R100 row's status becomes "Accepted (R100.g)" and names this record.
- **CHANGELOG.** No entry here. The release that carries F06 writes it (the R101 decision 6 precedent).

**Commits.** `e3b45be6` is `origin/main` at the start of this work, the product code this record ran at. The commit that adds this record and its publication is the head of PR `docs/r100g-styles-assets-acceptance`. The test and tool changes are in the same PR, as listed in RL-10.

**No product code under `src/` changed.** Local tests, the measurement tools and the record are the only changes. `build/`, `.local/` scratch and the CI logs are not committed.

## R100.h: the Styles and asset-catalog UX gaps (1.5 follow-up 3)

Closes OD-11's three rows, decided 2026-10-28 as fixed in 1.5. PR #520 on `feat/r100h-styles-gaps`, based on `origin/main` at `26e949f1`. There is no schema, command or file-format change.

- **Save report….** The catalog report has a **Save report…** button beside **Copy report**. It writes the report's text (the text Copy report copies) to a `.txt` file the person picks. The system dialog confirms an overwrite, and the report is not stored in the model. The file writer is `writeReportBytes` (`report_sheet.hpp`), which the terrain report's save now also uses.
- **The accounting bar.** The bar draws its parts as a solid fill (Inserted), a hatched fill (Refused) and a cross-hatched fill (Not inserted). The legend names each fill, so colour is not the only carrier (UX §18.7).
- **The refusal wording.** A refusal's first line says what did not happen, and the reason follows on its own lines (UX §18.6, §18.7). "Save refused:" became "Nothing was saved." with "Save refused: …" on the next line. The other refusals changed to the same shape: Export style (file exists; missing texture, now "Nothing was written."), every Import style refusal ("Nothing was applied."), Import style's apply failure, every catalog export refusal ("Nothing was written."), and the catalog's unreadable-file row ("Nothing is listed."). Left as designed: the "Insert is off: …" line, and the apply error from the library, which already opened "Nothing was applied.".

**Tests.** `style_catalog_input_tests` drives each change through the UI: the library save's refusal and taken name (`saveToLibraryRefusal`, `saveToLibrary`), the Import style refusals (`importRefusals`), the Export style refusals (`exportStyle`), the catalog export refusal (`exportAssetCatalog`), the unreadable-file row (`openCatalogFile`), the bar's brush styles and painted pixels at 1x and 2x (`reportBarFills`), and Save report… with the overwrite prompt answered No and then a new name (`reportSaveToFile`). The suite and `terrain_import_dialog_tests` pass on X11 1x and 2x and Wayland 1x and 2x, as the native tier runs them (the terrain test on X11 1x and Wayland 2x; the rest of the matrix was not run here).

**Not changed.** The IFC import, IFC export and STEP export reports use the same colour-only bar (`addSegmentBar`). The IFC import report has no **Save report…** in code, although UX §16.5 describes one. Both are outside OD-11 and are left as follow-ups.

## R100.i: progress and Cancel for the catalog insert (1.5 follow-up 2)

Closes F-2 (OD-5, decided 2026-10-28 for 1.5). [PR #521](https://github.com/zygote55/sketchyup/pull/521) on `feat/r100i-catalog-progress`, based on `origin/main` at `52a2b36a` (the merge of PR #520). No format, schema, fixture or batch-schema change. The R100.g findings above stand as recorded; this section is the new evidence for F-2 and for the row-4 verdict in §6.

**What changed.** The Asset catalogs insert runs on a worker of the shared ExchangeRunner. The worker reads each catalog the selection uses, verifies its hashes and entries, and composes the insert against a snapshot of the model. The UI thread commits the composed edit as one labelled undo step, as before. A progress sheet (ExchangeProgressSheet) names the phases (read, verify, insert) and the catalog. The engine has a cancel check (`CatalogCancelCheck`) in the reader's byte read and hash verification, between entries, and once more after the edit is composed.

**Measurements.** Release build in the pinned CI image (GCC, Qt 6.11.2), offscreen, the 128 MiB catalog of `style_catalog_benchmark write` (134,107,758 bytes), one entry ("Big 1") inserted from the library folder, load average about 3 from other workers. Four runs of the default-delay insert and four of the sheet-from-start insert:

| Measure | Result |
| --- | --- |
| Insert one entry, default delay (no sheet opens) | 342 to 357 ms wall; the UI thread's longest stop 11.6 to 16.5 ms |
| Insert one entry, sheet from the start | 398 to 403 ms; longest stop 13.1 to 18.6 ms. The total includes the sheet's 150 ms finished state, which the shared sheet shows before it closes |
| Cancel 0 ms into the insert (sheet closed after the click) | 4.6 to 4.8 ms |
| Cancel 50 ms into the insert | 5.8 to 6.1 ms |
| Cancel 150 ms into the insert | 22.1 to 30.6 ms |
| Cancel 250 ms into the insert | the insert had finished and was committed in both runs; the composed edit was complete before Cancel took effect |
| Cancel under load (Debug, Xvfb, eight CPU-bound processes, 20 runs of the new case) | 5.6 to 13.4 ms |
| Cancel on Xvfb at scale 1, full suite (Debug) | 2.4 and 3.3 ms |
| Cancel on weston at scale 2, full suite (Debug), focus given before the click | 0.8 and 1.9 ms |
| Cancel on weston at scale 2, one case, focus given before the click | 118 ms |
| Cancel on weston at scale 2, one case, clicked before the sheet had focus | 263 ms: the Settled focus rule (R091.h) waits up to 300 ms |

Against the budget (§ 9 row 4, 3 s for a 130 MiB source), one entry takes about 0.35 s, and the sheet opens only when the insert outlasts one second. The R100.g in-process figure was 245 ms load and 41 ms insert, 286 ms in all; the worker adds a little over 50 ms for the composition's snapshot and the sheet's plumbing.

**The delay.** One second, from UX_DESIGN 19.3 ("after about a second", the Quantities panel's computing line). No existing progress sheet had a delay: the IFC, STEP, SKP and export sheets open at once, so the delay is taken from the panel that already shows one. A sub-second insert shows no sheet.

**Cancel.** Cancel sets the worker's flag. The worker stops at its next check: between the steps, inside the reader (the file is read in 8 MiB chunks, and each asset and entry hash is checked as it is verified), between entries, and after the edit is composed. A cancelled insert changes nothing: the undo history, the definitions, materials, assets, style and the library folder are as they were, and the dialog stays open with Insert on again and a line that says nothing was inserted. A Cancel that arrives after the composed edit is complete is too late, and the insert commits, as a finished insert does; this is the 250 ms row above.

**The CLI.** `sketchyup-cli --import-catalog` stays synchronous. Ctrl+C is its cancel: it has no signal handler, so SIGINT ends the process, and it writes nothing until the end. The import happens in memory, and the output is published last, through a temporary file that is then linked into place (`publishNewFile`). Checked with a 128 MiB catalog: interrupted at 0.2 s and at 0.45 s, the run left no output file and no temporary file; the uninterrupted run wrote its output.

**Tests.** `style_catalog_input_tests`: `insertProgressAndCancel`. The slow insert is held by a test gate (the worker waits, bounded to 20 s, until the test has seen the sheet and clicked Cancel, so no check depends on a timing window). It checks that the sheet opens for a slow insert, names the three phases with the first working, names the catalog and has a busy bar; that Cancel closes it; that the history count, the definitions, the materials, the style and the library folder are unchanged; and that the dialog says the insert was cancelled and Insert is on again. A quick insert, with a delay longer than the insert, makes no sheet, and a completed insert is one labelled undo step. `insertBudget` runs the measurements above only when `SKETCHYUP_R100I_CATALOG` names a catalog. `SKETCHYUP_CATALOG_CASE` runs one case alone, which the load runs use. `catalog_import`: `cancellation`. A cancelled reader and a cancelled insert throw and change nothing; a cancel between entries publishes nothing; and a check that never cancels gives the same listing bytes, the same import report, and the same record counts, style and history as no check at all. The two documents' bytes are not compared whole, since each has its own identity. The existing report tests now drive the report with a polling helper (`modalAfter`), because a refused insert opens its report when the worker has finished.

**Runs.** Debug (the dev preset) in `sketchyup-ci-pinned:r097a-ifc`: `style_catalog_input_tests` on X11 at scale 1 and on Wayland at scale 2 (the full suite, exit 0 in each); `catalog`, `catalog_fuzz`, `catalog_import`, `style_catalog_cli`, `material_texture_size`, `style_bundle`, `commands` and `mcp` (8 of 8); the new case 20 of 20 under load. Not run here: X11 at scale 2 and Wayland at scale 1 (CI runs them), the sanitizers, and the Qt 6.8 build.

**Not done.** The desktop dry run on each selection still runs on the UI thread (R100.g F-1 was fixed by the snapshot path, and about 0.1 s remains for the 128 MiB opened catalog). The failure path runs the dry run again once, after the insert, to word the refusal. The CLI remains synchronous, by design.
