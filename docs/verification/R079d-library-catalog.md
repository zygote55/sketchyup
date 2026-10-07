# R079.d — Bounded local catalog

2026-10-07. Catalog contract `5ac515c`; integration `3c95648`.
Parent: [PR #191](https://github.com/zygote55/sketchyup/pull/191).
Contract: [0132](../decisions/0132-bounded-local-library-catalog.md).

Desktop/CLI targets build. Final library/catalog verification passes **1/1 in 0.50 s**;
dedicated ASan/UBSan verification passed **1/1 in 0.65 s**, with leak detection and
halt-on-error. Tests cover mixed template/component libraries, multi-term label and
description search, empty/unmatched queries, invalid bytes, symlinks and missing
folders alongside the existing relocation, insertion and resource-preservation cases.

A flat selected folder is scanned within explicit entry, byte and thumbnail budgets.
Each selectable item undergoes full bundle/native validation; malformed entries retain
bounded errors and cannot be inserted. Search matches all terms case-insensitively.
The catalog retains metadata and thumbnails rather than all decoded documents.
It neither follows file links nor fetches remote resources.

The [installed smoke](R079d-installed-smoke.json) checks eight catalogs, thirty-seven
contracts and existing exchange workflows. [Source package](R079d-source-package.json):
**184 installed inputs** match byte for byte; SHA-256
`86bcdec1a0a070d83def17139670302b110bfb24c645d0ad25adcec358d4cc9c`.
Native workflows follow in R079.e. Complete remote CI, ordered merges and milestone
acceptance remain required.
