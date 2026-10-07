# R081.a — Generated public reference

2026-10-07. Generator contract `4783198`; integration `82522c6`;
Arch verification dependency correction `981490e`.
Parent: [PR #197](https://github.com/zygote55/sketchyup/pull/197).
Contract: [0138](../decisions/0138-generated-tool-reference.md).

Desktop/CLI targets build. Final registry drift verification passes **1/1 in 0.29 s**.
Both generated artifacts also match the actual installed CLI's read-only registries.
A [disposable drift oracle](R081a-drift-oracle.json) first accepts exact copies, then
rejects an altered JSON artifact without modifying the source references.

The generated Markdown index and exact JSON schemas cover commands, inspection,
transactions, sessions, MCP, recipes and extensions. Discovery opens no model and
contacts no provider. Generation uses Python's standard library with subprocess
deadlines; CI/CTest compare exact artifact bytes. Python is declared as an Arch
build dependency now, alongside this first Python-based verification target; the
packaged `makepkg --printsrcinfo` confirms it. It is not an application runtime
requirement. This incorporates the earlier R081.e prerequisite correction.

The [installed smoke](R081a-installed-smoke.json) checks both reference artifacts,
ten catalogs, forty-three contracts, the extension helper/sample and exchange workflows.
[Source package](R081a-source-package.json): **194 installed inputs** match byte for
byte; SHA-256 `1de4b9d50e4d933996f783bc9bd0cbb493b64c06b7e812bc30790de9488fd4bc`.
The [portable CI disk-report correction](CI-portable-disk-report.md) changes no source
archive input. Executable recipes, optional discovery and the integrated M8 workflow
follow. Complete remote CI, ordered merges and milestone acceptance remain required.
