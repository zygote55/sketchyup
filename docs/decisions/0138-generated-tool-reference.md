# Generated public tool reference

R081.a, 2026-10-07. The release includes `TOOL_REFERENCE.md` and
`api/tool-reference-v1.json`, generated from the actual CLI's command, bounded
inspection, transaction, session, MCP, recipe and extension registries. The JSON
retains exact schemas, including bounds, required fields and rejection of unknown
properties. The Markdown indexes names, purposes and required parameters; it
links to the full schemas rather than duplicating constraints in prose.

`scripts/generate-tool-reference.py --cli build/dev/sketchyup-cli --root .`
regenerates both artifacts. `--check` compares exact UTF-8 bytes without writing;
CTest and CI fail on drift. The generator uses only Python's standard library,
spawns explicit read-only discovery modes with deadlines, never opens a model,
and never contacts providers. Python is a verification dependency, not an
application runtime dependency. Artifacts are included in installed documentation
and the source archive. Changing a registry requires intentional regeneration.

Reference version 1 is independent of command API version 1 and the advertised
MCP protocol version. No registry entry proves that an external helper is present,
a provider is configured, or a particular operation is appropriate for the current
model. Callers must check runtime capabilities and document preconditions. Core
geometry validation remains authoritative after schema validation. Legacy local
queries are not confused with the bounded transport query schemas.

R081.b supplies executable geometry recipes and the updated agent guide;
optional dependency discovery follows separately. This reference alone does not
satisfy the complete R081 gate.
