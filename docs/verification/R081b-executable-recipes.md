# R081.b — Executable agent recipes

2026-10-07. Recipe/guide contract `490a615`; integration `47dd12a`, including the
instrumented-execution deadline correction from `2f3a804`.
Parent: [PR #198](https://github.com/zygote55/sketchyup/pull/198).
Contract: [0139](../decisions/0139-executable-agent-recipes.md).

Desktop/CLI targets build. Final recipe and reference checks pass **2/2 in 14.99 s**.
All ten recipes also pass against the actual installed CLI and installed examples:
[installed results](R081b-installed-recipes.txt). Dedicated ASan/UBSan execution
previously completed all ten in **354.34 s**, with leak detection and halt-on-error.
The verifier retains finite deadlines (180 s per CLI call, 900 s for the suite),
allowing instrumentation overhead; these bounds are included in this first recipe PR.

Room, window, roof, stairs, table, cabinet, site, hosted and material workflows assert
physical dimensions/areas/volumes, save and reload native files, repeat measurements
and preserve source bytes. The material example resolves its created material ID
inside the draft and checks both face sides. Every shipped recipe filename requires
an explicit acceptance case. These deterministic executions do not claim live-model
success. The agent guide documents actual versioned interfaces, private staging,
revision checks, durable outcome reconciliation and truthful capability reporting.

The [installed smoke](R081b-installed-smoke.json) verifies the guide/new example,
ten catalogs, forty-four contracts, helper and existing exchange workflows.
[Source package](R081b-source-package.json): **197 installed inputs** match byte for
byte; SHA-256 `a6df30b82e96097301ab46e619b048328d5206c32b27c3cf25f6dce867825f86`.
Optional discovery, build configurations and integrated M8 acceptance follow.
Complete remote CI, ordered merges and milestone acceptance remain required.
