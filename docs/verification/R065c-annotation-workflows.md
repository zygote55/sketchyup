# R065.c — Shared annotation workflows

2026-10-06. Code `7d3ba13`, published catalogs `d783e25`, registry coverage fixes
`f58addc` and `7dcc06a`; integrated acceptance head `07f8945`. Parent: PR #145.
Contract: [0080](../decisions/0080-annotation-workflows.md).

Regression coverage is **121 distinct suites passing across the full and focused
runs**. The full Debug run took 111.22 s: 120 suites passed (including actual Blender),
and the command catalog guard required executable cases for the three new commands.
After adding those cases, commands, inspection and annotation commands all passed
in 6.14 s. The guard exercises required/unknown fields, handlers and one-step Undo.
The corresponding inspection registry audit was also extended to execute both new
queries. These were test-registration omissions; the dedicated annotation fixtures
already exercised the commands and queries. No compiler warnings remain.

ASan/UBSan with leak detection and halt-on-error passed five distinct suites:
annotation commands (2.35 s), staging (11.39 s), transaction dispatch (9.26 s),
inspection (3.11 s after its registry fixture update), and the complete command
catalog (127.83 s after its fixture update). The initial inspection guard failure
is retained in local logs and was resolved by exercising the added queries.

[Native acceptance](R065c-native.json) passed desktop inspection, native MCP,
assistant preview and history input on Wayland at 2× scale: **4/4 in 12.852 s**.

The [independent JSON Schema validation](R065c-schema-validation.json) accepted
21 valid and rejected 96 invalid command requests across the session, MCP and
transaction catalogs; six valid and six invalid inspection requests passed their
expected outcomes across headless/session/desktop catalogs. All seven capability
artifacts were generated from the built executables. Native MCP remains read-only.

The [installed CLI](R065c-installed-smoke.json), with display variables removed,
executed the four-command packaged example, returned exact created annotation IDs,
retained a two-metre distance across an edge split and exposed the split-boundary
label as ambiguous. It exercised bounded pagination, kind filtering, both queries,
all three commands, metadata-only editing of a broken label, explicit rebinding,
unit changes without changing metre measurements, deletion, failed compound-batch
rollback, unchanged geometry and byte-exact file relocation. Installed catalogs,
contract, example and desktop binary matched the build inputs.

[Source-package verification](R065c-source-package.json): **120 installed inputs**
match byte for byte; build and Git artifacts excluded. Archive SHA-256:
`891459881fc68fd85b4e13558fac253f363753d4d2cd6612582ec9ba70736b66`.

Dedicated fixtures also cover immutable preview, staged resource reporting, exact
compound geometry/annotation Undo, stale cursors, canonical identities, caller
rejection of forged anchor state, reflected/nonuniform face coordinates, fixed and
vertex anchors, missing-reference null distances and shared-definition rejection.
Native annotation editing/drawing belongs to R065.d, not this layer. Provider
configuration and private live acceptance evidence are unchanged. Remote CI and
ordered dependency merges remain delivery gates.
