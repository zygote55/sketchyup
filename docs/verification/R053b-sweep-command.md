# R053.b — Shared profile sweep command

Date: 2026-10-05 UTC. Depends on R053.a; native acceptance remains outstanding.
[Command contract](../decisions/0044-profile-sweep.md).

`geometry.sweep` creates a separate editable result while retaining the selected
profile, its source context and unrelated records. `sweep_command_tests` checks
read-only preview with exact publication mappings, one-entry Undo/Redo, source
selection, front/back material inheritance, native persistence, stale revision,
locked source, invalid geometry and late-batch atomic rollback. Generated face
receipts omit faces erased by later commands in the same batch.

A nested, rotated, mirrored, nonuniformly scaled profile is swept in world space:
the independent world-solid oracle verifies 4.32 m³. A unique-instance sweep
resolves source/output mappings to scene IDs and retains the sibling and original
definition. The ordinary elbow verifies 0.48 m³. The standalone CLI example
`examples/profile-sweep.json` saves and reopens successfully, retaining both the
profile and generated result.

Validation:

```sh
ctest --preset dev -R '^(sweep_commands|offset_commands|transaction_dispatch|automation_session|mcp|assistant)$'
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build/inspection-sanitize/sweep_command_tests
build/dev/sketchyup-cli --script examples/profile-sweep.json --output /tmp/profile-sweep.sketchyup
build/dev/sketchyup-cli --input /tmp/profile-sweep.sketchyup
```

All six selected suites pass (1.43 s). Command fixtures pass ASan/UBSan/leak
checks. Installed transaction, headless-session and MCP discovery artifacts are
regenerated and verified against the live registry. The assistant routine command
allowlist uses the same bounded command; no live-provider quality claim is added.
