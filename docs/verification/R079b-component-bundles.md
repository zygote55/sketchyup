# R079.b — Contained component bundles

2026-10-07. Component contract `7262c03`; integration `59a6163`.
Parent: [PR #189](https://github.com/zygote55/sketchyup/pull/189).
Contract: [0130](../decisions/0130-contained-component-bundles.md).

Desktop/CLI targets build. Final template/component verification passes **1/1 in
0.07 s**; dedicated ASan/UBSan verification passed **1/1 in 0.46 s**, with leak
detection and halt-on-error. Nested references, exact resource closure, unrelated
missing assets, source/history preservation, strict bundle-kind separation,
relocation and non-replacing publication are covered.

Capture packages the chosen component's recursive canonical definitions and only
referenced materials, embedded images, tags and their folders. It excludes unrelated
geometry and document-specific hosted relationships, scenes, sections and annotations.
The reader rejects extra roots, unrelated resources and malformed reference graphs.
Geometry remains in metres; source display units do not rescale the component.

The [installed smoke](R079b-installed-smoke.json) checks eight catalogs, thirty-five
contracts and existing exchange workflows. [Source package](R079b-source-package.json):
**182 installed inputs** match byte for byte; SHA-256
`27b3904b61f863663e38bab671d6ce82417a627861c5f1529e9936a13dd0eeab`.
Insertion, catalog and native workflows follow in R079.c–e. Complete remote CI,
ordered merges and milestone acceptance remain required.
