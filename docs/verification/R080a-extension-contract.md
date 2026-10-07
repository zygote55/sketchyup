# R080.a — Versioned extension contract

2026-10-07. Manifest contract `fd44a92`; integration `a044209`.
Parent: [PR #193](https://github.com/zygote55/sketchyup/pull/193).
Contract: [0134](../decisions/0134-versioned-extension-manifests.md).

Desktop/CLI targets build. Final manifest verification passes **1/1 in 0.11 s**.
Fresh ASan/UBSan extension checks pass **3/3 in 4.42 s** in the dedicated combined
integration (`8ea4809`); manifest cases take **1.31 s**. The manifest implementation
matches this layer; that later fixture additionally checks the capability catalog.
Leak detection and halt-on-error are enabled.

The sample uses only declared public `geometry.face` commands: measured area,
undo/redo and atomic failure are verified. Strict manifest/API/execution versions,
typed parameters, capability declarations, nested/dynamic selectors, source-byte
authority and bounded expansion reject incompatible or malformed actions.
Packages contain declarative command data and run no extension-supplied code.
The format does not claim compatibility with native/Ruby/script plugins or an OS sandbox.

The [installed smoke](R080a-installed-smoke.json) checks the packaged sample,
eight catalogs, thirty-nine contracts and existing exchange workflows.
[Source package](R080a-source-package.json): **187 installed inputs** match byte for
byte; SHA-256 `fc67242ad2cef261632c1ee9897afb1046a6025652bd5c49222cbdefe321c5d2`.
Persistent lifecycle, bounded worker and native management follow in R080.b–d.
Complete remote CI, ordered merges and milestone acceptance remain required.
