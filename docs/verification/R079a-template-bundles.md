# R079.a — Self-contained templates

2026-10-07. Template contract `f1397d1`; integration `40cda821`.
Parent: [PR #188](https://github.com/zygote55/sketchyup/pull/188).
Contract: [0129](../decisions/0129-self-contained-template-bundles.md).

Desktop/CLI targets build. Final template verification passes **1/1 in 0.05 s**;
dedicated ASan/UBSan verification passed **1/1 in 0.31 s**, with leak detection
and halt-on-error. Tests cover embedded resources, component bindings, saved scene
defaults, independent template instances, relocation, source protection and rejected
malformed lengths, hashes, metadata, versions, missing assets and oversized thumbnails.

A single bounded `.sketchylib` file carries strict metadata, native model and PNG.
Instantiation gives a fresh document identity, no history and an unsaved state while
retaining model defaults and embedded resources. Bundles resolve no external paths
and execute no scripts. Publication never replaces an existing file.

The [installed smoke](R079a-installed-smoke.json) checks eight catalogs, thirty-four
contracts and existing exchange workflows. [Source package](R079a-source-package.json):
**181 installed inputs** match byte for byte; SHA-256
`cc14869da0d411de4f361a026077adcf5a5da3bc747f1ee9434e56ef6cfd24c0`.
Component insertion, catalog search and native workflows follow in R079.b–e.
Complete remote CI, ordered merges and milestone acceptance remain required.
