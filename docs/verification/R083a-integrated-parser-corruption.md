# R083.a — Integrated parser-corruption corpus

2026-10-07. Integration `7a6d444`; corpus implementation `88e2991`.
Parent: [PR #203](https://github.com/zygote55/sketchyup/pull/203).

The final corpus builds and passes **1/1 in 1.32 s**, exercising **5,120 mutations
across ten formats**. Its complete JSON result, including canonical seed hashes and
accepted/rejected counts, exactly matches the retained [corpus](R083a-parser-corpus.json)
from both normal and dedicated ASan/UBSan runs. The latter passed in **21.54 s**
with leak detection and halt-on-error; [local evidence](R083a-local-parser-corruption.md)
records the deterministic seed normalization and coverage boundaries.

Accepted inputs must retain structural/native roundtrip invariants and bounded
finite geometry; rejected inputs preserve original source content. Allocation
failures and failures after successful parsing do not count as expected rejection.
This deterministic corpus is bounded corruption testing, not continuous
coverage-guided fuzzing or a declaration that every parser failure is covered.

The application code is unchanged. [Installed verification](R083a-final-installed-smoke.json)
confirms byte-identical desktop, CLI and helpers against the prior verified install,
all installed catalog/contract files, and the new ADR 0142. The
[source package](R083a-final-source-package.json) contains **201 byte-exact installed
inputs**, SHA-256 `3d99495615d5bcfc5bbcc7c7c27645756aaef2af9e035d8e84ba48f200b10045`.
Publication faults, transaction sequences and broader hardening follow. R083/M9
acceptance, remote CI and ordered prerequisite merges remain open.
