# R080.c — Bounded action worker

2026-10-07. Worker contract `c1b0246`; integration `14f744c`.
Parent: [PR #195](https://github.com/zygote55/sketchyup/pull/195).
Contract: [0136](../decisions/0136-bounded-extension-worker.md).

Desktop/CLI, helper and affected test targets build. Final extension checks pass
**3/3 in 0.49 s**. Fresh dedicated combined ASan/UBSan checks pass **3/3 in 4.42 s**
(`8ea4809`), with the identical worker implementation/fixture in **2.21 s**.
Leak detection and halt-on-error are enabled.

The actual helper resolves typed public commands. Tests reject failed start, nonzero
exit, intentional SIGKILL, malformed output, stdout/stderr floods, timeout, cancellation
and forged successful replies. The parent independently recomputes the batch and
verifies response identity. Requests include no model snapshot or credential settings;
the child environment is minimal. Worker completion does not commit an edit.

The [installed smoke](R080c-installed-smoke.json) runs the packaged helper with the
installed panel manifest and verifies its hash, action and exact 2×3 m face coordinates.
It also checks eight catalogs, forty-one contracts and prior exchange workflows.
[Source package](R080c-source-package.json): **189 installed inputs** match byte for
byte; SHA-256 `7359787c52db742adc64e58fb4d713a904d6cc80b092100d123d285a795986f8`.
Native lifecycle integration follows in R080.d. Complete remote CI, ordered merges
and milestone acceptance remain required. The helper is not an OS security sandbox.
