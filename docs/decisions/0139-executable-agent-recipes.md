# Executable shipped agent recipes

R081.b, 2026-10-07. The packaged agent guide describes the actual version-1
interfaces and links to the generated registry reference. It replaces provisional
tool names with discovery, explicit targeting, private staging, revision checks,
sealed commit, durable outcome reconciliation and honest completion reporting.

The ten shipped recipes cover room/window/roof/stairs/furniture/site/material
workflows. The material recipe creates a measured panel and applies the returned
material ID to both sides inside the same draft. No example guesses created IDs.
The verifier executes every example through the real CLI, checks all responses,
asserts physical dimensions and analytic solid volumes where applicable, validates
and reloads saved models, repeats target measurements and checks source bytes are
unchanged. Material acceptance samples both front and back. All `*recipe*.json`
files require explicit verification cases; adding an unchecked example fails CI.

Python's standard library is sufficient at verification time. The same script can
run against an installed CLI and installed examples. A temporary directory owns
all output models and durable outcome directories; user documents are never used.
CTest/CI run both recipe acceptance and registry drift. Dedicated engine tests
retain detailed host, component, negative geometry, undo and failure coverage.
These deterministic examples are not evidence of live language-model success.

The guide distinguishes headless/editor capabilities, compiled support/runtime
availability and queued/completed render work. Optional operations have documented
alternatives without pretending the missing operation succeeded. Release provider
and platform acceptance remain separate roadmap gates.
