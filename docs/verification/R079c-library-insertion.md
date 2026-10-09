# R079.c — Atomic library insertion

2026-10-07. Insertion contract `99c2e32`; integration `88ddb41`.
Parent: [PR #190](https://github.com/zygote55/sketchyup/pull/190).
Contract: [0131](../decisions/0131-atomic-library-insertion.md).

Desktop/CLI targets build. Final library verification passes **1/1 in 0.10 s**;
dedicated ASan/UBSan verification passed **1/1 in 0.60 s**, with leak detection
and halt-on-error. Tests cover recursive persistence, mirrored placement, one-step
resource undo/redo, source and existing-record preservation, exact resource reuse,
conflicting names, independent repeated insertions and atomic rejection for locked
or missing parents and invalid transforms.

Insertion privately remaps recursive component definitions, tags, materials and
assets, then publishes one validated edit. Identical embedded resources can be reused;
conflicting names receive bounded suffixes. Existing resources are never overwritten.
Repeated insertions get independent definition identities, while destination display
units and presentation defaults remain unchanged.

The [installed smoke](R079c-installed-smoke.json) checks eight catalogs, thirty-six
contracts and existing exchange workflows. [Source package](R079c-source-package.json):
**183 installed inputs** match byte for byte; SHA-256
`e9ac89096c9aa8c3b0c3600bacf4c040fe2e9c6fe957b59a57741b339f6345e4`.
Catalog and native workflows follow in R079.d–e. Complete remote CI, ordered merges
and milestone acceptance remain required.
