# R093.e — Constrained component parameters acceptance

2026-10-16. Recorded commit `16a51681181f9d751a7fda5d895c8e9aa627a9f7` on
`build/r093e-acceptance`, branched from `f02/integration` at `ddf7016e`; the commit that
adds this record changes only documentation. `f02/integration` holds R093.a
([PR #339](https://github.com/zygote55/sketchyup/pull/339)) and R093.b
([PR #341](https://github.com/zygote55/sketchyup/pull/341)) from `main`, plus R093.d
(commands, inspection and MCP), R093.c (schema 28) and corpus v3 merged into it. By the
owner's decision, CI runs once at the end of F02, so **this record is local
verification, not CI evidence**. [Summary JSON](R093e-component-parameters.json).

**Result: pass.** The R093 Verify line holds at the recorded commit in `dev` and
`sanitize`. The 300-second campaign over five fuzz formats found nothing, and the measured
budgets are now asserted in CI with ceilings ten times the measured values. The 1.1.0-1
and 1.0.1-1 packages refuse schema 28 without changing the file.

## Environment

Omarchy (Arch), Linux 7.2.5, 22 logical CPUs, GCC 16.2.1, clang/compiler-rt 22.1.8,
Qt 6.11.2, CMake 4.4.3, Ninja 1.13.2. Another worker shared the host. Its load average
was about 30 for most of the run and about 7–10 near the end. Builds used at most six
jobs. The sanitized desktop step ran in the local image `sketchyup-ci-pinned:r094a`. That
image is the pinned CI base `archlinux:base-devel@sha256:51dd3d24…` with the CI package
set. Its toolchain matches the host: qt6-base 6.11.2-3, gcc 16.2.1, wayland 1.26.0,
weston 15.0.1.

## Integration of R093.c and R093.d

R093.c and R093.d were developed in parallel. This run is the first that exercises them
together. No production code needed a change. Two test follow-ups were made, each in its
own commit:

| Commit | Cause | Change |
| --- | --- | --- |
| `cce5f079` F02: compare registered commands by their real encoding | R093.d's `command_tests` compared parametric documents by revision, history and a canonical parameter dump, because the writer refused parameter records before schema 28 | Every registered-command case now compares `encodeDocument` output; the fallback is deleted |
| `7541c985` F02: cover parameter commands through save and reopen | No test ran commands, then `saveDocument`, then `loadDocument` | `component_parameter_command_tests` `persistence`: the table built by commands, an overridden placement and a second placement with its own override are saved and reopened. `component_parameters.values` and `.describe`, the regenerated geometry and `encodeDocument` match. A reset after reopen and a second save and reopen keep every value. |

R093.d left the `--inspect-native` counts to R093.c. They are in `src/io/native_format.cpp`
(`componentParameters`, `componentDrivers`, `componentParameterOverrides`).
`component_parameter_io_tests` covers them: 2, 7 and 2 for the schema-28 file, and 0 for a
v26 fixture. The generated documentation is current: `generate-api-schemas.py --check`
reports "10 generated API schemas match the live registries", and
`generate-tool-reference.py --check` reports 2 artifacts matching. Nothing was
regenerated.

## Test runs

| Preset / CI step | Command | Result |
| --- | --- | --- |
| `dev` (desktop and CLI) | `cmake --build --preset dev -j6`, `ctest --preset dev -j6` | 207 of 207 pass at `16a51681` (434 s); 12 `*_real` consumer tests skipped by design (opt-in external consumers) |
| `headless` | `cmake --build --preset headless`, `ctest --preset headless` | 68 of 68 pass at `16a51681` |
| sanitize: "Sanitized core without Qt" | `cmake --preset sanitize`, build, `ctest --preset sanitize` | 68 of 68 pass (1,498 s) |
| sanitize: "Sanitized bounded inspection and CLI" | the step's exact target list and `-R` regex, including `component_parameter_commands` and `component_parameter_io` | 107 of 107 pass (1,603 s) |
| sanitize: "Sanitized shipped recipes and discovery" | the step's targets and regex | 7 of 7 pass, including `parser_fault_corpus` and `tool_reference_drift` |
| sanitize: "Verify private Wayland client reference fix" and "Sanitized native MCP inspection" | `scripts/ci-wayland-client.sh`, then the step's configure, build, 46 `scripts/test-wayland.sh` runs and `texture_cache` (pinned image) | All 46 Wayland runs and `texture_cache` pass; see the note below |

**Sanitized desktop step.** The private Wayland client built, and its `meson test` and
both proxy probes passed. On the first pass, the 16 Wayland runs before
`text_input_tests` passed, and so did `texture_cache`. `text_input_tests` then reached
the 60-second `test-deadline.sh` limit. The stacks show the main thread doing CPU-bound
document validation inside a text edit (`TextPanel::edit` → `executeBatch` →
`Document::restore`), not a deadlock. At the time, a full `dev` ctest at `-j6` and another
worker's build were running on the host. The remaining 30 runs were repeated with
`text_input_tests` first, and all passed. `text_input_tests` took 59 s in that pass, then
27 s and 32 s alone at load average 9–10. On `main` CI (run 38946741700, which includes
R093.b) it takes about 31 s. R093.c and R093.d do not touch the text path. Their only
per-batch addition is the three report fields `createdComponentDrivers`, `regenerated` and
`parameterWarnings`, which do linear passes over the definition and placement tables.

`dev` and `headless` ran in full at `16a51681` (the full `dev` suite twice: once before
the ceilings below were set, and once after). The first three sanitize steps ran on the
same tree, except that the three budget ceilings were still placeholders. Once the
ceilings were set, the three suites that hold them were re-run in the sanitized build and
passed. The sanitized desktop step ran at `16a51681`.

## Verify line

"Changing width drives dependent geometry deterministically; invalid/cyclic formulas
cannot partially commit or execute host code." The clauses below follow the
[ADR 0165 §8](../decisions/0165-constrained-component-parameters.md#8-verification-plan)
mapping. Check counts are from the recorded commit and are the same in `dev` and
`sanitize`.

| Clause | Tests | Checks |
| --- | --- | --- |
| Width drives dependent geometry | `component_parameter_tests`: `table` (width 0.9 to 1.2 m stretches the top and moves only the right legs by 0.3 m), `shelves` (0–4, stable copy IDs), `rebaseline`, `sharing`, `nested` (childParameter), `hosted` (a driven width re-cuts the host opening) | 420 in the suite |
| …deterministically | `component_parameter_tests` `determinism`: ten repetitions, shuffled insertion, undo and redo, and the golden digest asserted in both builds. Save and reopen: `component_parameter_io_tests` and the new `persistence` case. | See below |
| Cannot partially commit | `component_parameter_tests` `fail-closed`, `one-placement`, `cycles`, `locks` (pointer-identical records, revision, history and dirty state); `component_parameter_command_tests` `atomicBatches`, `refusals` (every refusal code leaves `Records` identical); `component_parameter_io_tests` (141 refusal cases, including stored cycles, unknown names and non-canonical formulas, each leaving the file byte-identical) | 420; 193 commands; 870 io |
| Cannot execute host code | `parameter_expression_tests` `grammarClosure` (shell, script and path text, `$()`, backticks, `;`, `__proto__`, `import`, unknown functions give `PARAMETER_SYNTAX`/`PARAMETER_NAME`); `parameter_expression_symbols` (no process, file, network or loader symbol in the library); `parameter_expression_fuzz`; the libFuzzer campaign below | 2,672 expression checks |
| Compatibility | `component_parameter_io_tests` with `SKETCHYUP_PRIOR_CLI` (below); `parser_fault_corpus` (19 entries, 9,728 mutations, sources preserved) | 888 with 1.1.0; 885 with 1.0.1 |

### Determinism across `dev` and `sanitize`

The two builds were compared directly:

- **Table golden digest.** `component_parameter_tests --print-golden` prints
  `64f78ccba287b01c57d9cc337ca6d0764c32a878f404d6ce8332e48eba5def6f` in both builds. This is
  the SHA-256 of the canonical encoding after the width change, and the `determinism` suite
  asserts it.
- **Expression goldens.** The 26 lines from `parameter_expression_tests --print-golden`
  (value bits of each golden expression) are byte-identical between the builds.
- **In-ctest fuzz.** `parameter_expression_fuzz` reports the same summary in both builds:
  20,000 generated formulas, 9,600 seed mutations (1,016 accepted), digest
  `6ed984df689bd758`.
- **Writer output.** `component_parameter_io_tests --write-fixture` from each build matches
  `tests/fixtures/component-parameters-v28.sketchyup` exactly once the random `documentId`
  and the chunk hashes that cover it are masked (normalized SHA-256 `a87a1ecf…07b7`).

## Fuzz campaign

The campaign followed the [R083.e procedure](R083e-parser-fuzzing.md) and
[PARSER_FUZZING.md](../PARSER_FUZZING.md):

- **Build.** A Clang `-O1 -g` RelWithDebInfo build with `SKETCHYUP_BUILD_FUZZERS=ON`, from
  the recorded commit's fuzz sources (unchanged since `ddf7016e`).
- **Seeds.** `parser_fuzz_seeds` reported 32 valid seeds and one rejected seed,
  `component-parameter-cycle` (native-json, which must be refused).
- **Runs.** Each run lasted 300 s, two at a time, with
  `-max_len=1048576 -timeout=10 -rss_limit_mb=2048 -detect_leaks=1`, `ASAN_OPTIONS=detect_leaks=1:abort_on_error=1`
  and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
- **Sanitizer options.** The four JSON-based targets add `alloc_dealloc_mismatch=0` for the
  known Qt `QJsonDocument::fromJson` report (R083.e). `parameter-expression` is Qt-free and
  ran without it.

Corpora, logs and binaries are in `.local/fuzz-r093e`, outside the repository. The
seed-set hash is the SHA-256 of the sorted per-file `sha256sum` lines.

| Format | Seeds | Seed-set SHA-256 | Executions | Exec/s | cov | ft | Peak RSS | Corpus | Findings |
| --- | --: | --- | --: | --: | --: | --: | --: | --: | --- |
| parameter-expression | 12 | `7c19885c…c248d0` | 586,988 | 1,950 | 2,124 | 6,832 | 479 MB | 621 | none |
| native-json | 6 | `33678a44…26a8c0` | 234,901 | 780 | 17,266 | 58,068 | 516 MB | 504 | none |
| native-container | 5 | `e753e2b8…79b94` | 582,458 | 1,935 | 16,234 | 41,605 | 524 MB | 87 | none |
| component | 2 | `068025fd…e85ec` | 674,025 | 2,239 | 16,215 | 36,496 | 559 MB | 42 | none |
| template | 2 | `c8568e56…cba20` | 969,572 | 3,221 | 14,038 | 27,879 | 613 MB | 38 | none |

Every run ended "Done … in 301 second(s)" with exit status 0. The logs contain no crash,
leak, timeout, out-of-memory, sanitizer report or harness invariant failure, and every
artifact directory is empty. A finite campaign gives measured coverage, not proof.

## Budgets (ADR 0165 §7)

No earlier slice asserted the §7 budgets: `component_parameter_tests` only printed the
time of its document-limit fixture. R093.e adds the assertions in commit `16a51681`. Each
ceiling is ten times the largest value measured on this host, separately for `dev` (the
ceiling also applies to `headless`) and `sanitize` (selected by `__SANITIZE_ADDRESS__`).

The samples vary about fivefold with host load. The ceilings therefore use the maxima,
which were taken while another worker held the load average near 30. The quieter samples
were taken at load 7–10.

| Budget | What is timed | `dev` samples | `dev` ceiling | `sanitize` samples | `sanitize` ceiling |
| --- | --- | --- | --: | --- | --: |
| Parse and check of a 1,024-byte expression | `parseParameterExpression` + `checkParameterExpression` of 127 names plus a literal (length type), mean of 200 runs | 1,863 / 4,151 / 3,167 / 3,057 µs loaded; 721 µs quieter | 42,000 µs | 17,508 / 14,699 / 11,838 µs loaded; 5,173 / 4,068 µs quieter | 180,000 µs |
| Evaluation of the worst accepted definition | `evaluateComponentParameters`: 128 parameters and 256 visibility drivers, each formula 256 nodes (98,049 visits), best of 20 | 17.1 / 33.4 / 17.3 / 34.0 / 17.0 ms loaded; 6.9 ms quieter | 340 ms | 104.2 / 76.8 / 67.3 ms loaded; 28.5 / 33.3 ms quieter | 1,050 ms |
| Load of the worst accepted document | `decodeContainer` of (a) a 20,000,000-visit document (630 placements, 457,099 bytes) and (b) a 4,096-variant document (2,462,844 bytes) | (a) 9.51 / 5.42 / 3.42 s; (b) 4.27 / 3.07 / 1.85 s | 96 s | (a) 48.2 / 12.5 s; (b) 17.3 / 5.7 s | 490 s |

Both load fixtures sit at a document limit: exactly 20,000,000 evaluation visits, and
exactly 4,096 distinct variants. A load evaluates every placement in the reader and again
in restore. Rejection stays linear because over-long or over-deep input is refused before
tokenizing; `parameter_expression_tests` `limits` covers that at n and n+1.

## Previous releases

R093.c recorded the refusal in the
[ADR 0165 R093.c notes](../decisions/0165-constrained-component-parameters.md#implementation-notes-r093c).
The published packages were quick to fetch, so the check was re-run. The packages are
`sketchyup-1.1.0-1-x86_64.pkg.tar.zst` (SHA-256
`e718b8fb0e52bb5ec40b6817b99c92a824242f4a20907a02f50f47e3f0c0d00a`, the GitHub release
asset digest) and `sketchyup-1.0.1-1-x86_64.pkg.tar.zst` (SHA-256 `91e80785…869f5`, pinned
in `verify-package.sh`). Each was unpacked privately, without installing.

- `sketchyup-cli --validate-native tests/fixtures/component-parameters-v28.sketchyup`
  exits 1. Both releases write exactly
  `{"code":"INVALID_OPERATION","error":"Unsupported container field","status":"failed"}`
  to stderr. The file's SHA-256 stays
  `df46f73d5ce1c68b95995be0e9881f7e5478ff49d8bc3b7db010a9138da9fd92`.
- `component_parameter_io_tests` was run with `SKETCHYUP_PRIOR_CLI` set to each release
  (`dev` build). It confirms that the refusal leaves a schema-28 save unchanged and that
  each release opens parameter-free saves from this build: 1.1.0 at schema 26 and 27
  (888 checks), and 1.0.1 at schema 26 (885 checks).

## Native matrix

R093 adds no GUI suite and changes no desktop code, so no new `sketchyup_native_fixture`
cell exists. The sanitized Wayland step above ran the existing suites unchanged. The
X11/Wayland 1× and 2× matrices, the package job and every CI job run at the end of F02.
This record claims no CI result.

## Not verified here

- **CI.** No CI ran on these commits (owner decision for F02).
- **Ceilings on CI runners.** The ceilings come from this host. Slower runners have the
  tenfold margin.
- **Desktop.** No desktop or GUI behaviour exists for parameters before R094.
