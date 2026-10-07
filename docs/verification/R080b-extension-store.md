# R080.b — Persistent extension lifecycle

2026-10-07. Store contract `a62eb78`; integration `89bc7dc`.
Parent: [PR #194](https://github.com/zygote55/sketchyup/pull/194).
Contract: [0135](../decisions/0135-persistent-extension-lifecycle.md).

Desktop/CLI targets build. Final manifest/store checks pass **2/2 in 0.44 s**.
Fresh dedicated combined ASan/UBSan checks pass **3/3 in 4.42 s** (`8ea4809`),
including the identical store implementation/fixture in **0.90 s**. Leak detection
and halt-on-error are enabled.

Installation retains exact validated source bytes and starts disabled. Enable,
disable, persisted error, explicit recovery and removal survive registry reloads.
Tests reject duplicate packages, corrupt records, symlinks, stale concurrent writes
and failed publication while preserving old state. Incompatible retained packages
remain inspectable and cannot execute. The registry uses bounded records, owner-only
permissions, locking and atomic replacement; loading it runs no extension action.

The [installed smoke](R080b-installed-smoke.json) checks the sample, eight catalogs,
forty contracts and prior exchange workflows. [Source package](R080b-source-package.json):
**188 installed inputs** match byte for byte; SHA-256
`e43685ebe1de9d0f15e7527ed8965058b519e8ff3d58d563a271cc373268400d`.
Worker and native management follow in R080.c–d. Complete remote CI, ordered merges
and milestone acceptance remain required.
