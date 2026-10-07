# R081.c — Optional capability discovery

2026-10-07. Integration `021c202`, preserving the M7 and CI gates.
Parent: [PR #199](https://github.com/zygote55/sketchyup/pull/199).
Contract: [0140](../decisions/0140-optional-capability-discovery.md).

The standalone `--optional-capabilities` response separates compiled support,
executable presence and operational readiness. Discovery never launches helpers,
loads extensions, reads provider credentials or performs a network request. It
reports unavailable alternatives and does not disclose executable paths or user
configuration. Presence alone never claims authentication or compatible execution.

Desktop and CLI targets build. Final discovery/reference checks pass **2/2 in
0.48 s**. A relocated CLI with a private PATH verifies missing, executable and
nonexecutable helpers; marker executables prove discovery does not run them.
Mixed modes reject without creating output. Dedicated ASan/UBSan discovery
previously passed in **2.77 s** with leak detection and halt-on-error. The CI
recipe/discovery step retains the bounded instrumentation deadlines introduced
in R081.b.

The [installed smoke](R081c-installed-smoke.json) checks both real installed helper
locations, provider non-probing, mixed-mode rejection, ten catalogs, forty-five
contracts, and the existing exchange workflows. [Source package](R081c-source-package.json):
**198 installed inputs** match byte for byte; SHA-256
`3d1a56da3877125ba0d258221a7a0f822b313b9883a18b94524918c7018d4e6d`.
Complete remote CI, ordered merges and integrated milestone acceptance remain required.
