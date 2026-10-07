# R070.c — Native retained render Jobs

2026-10-07. Final implementation `e488d8d`; integrated acceptance `8a1dc44`.
Parent: [PR #162](https://github.com/zygote55/sketchyup/pull/162).
Contract: [0101](../decisions/0101-native-render-jobs.md).

The complete Debug build and **135/135 CTest suites pass** in **183.93 s**,
including real Blender rendering. Four final Wayland 2× integration suites pass in
**19.597 s** ([matrix](R070c-native-matrix.json)).
An initial full run exhausted the user tmpfs quota; retained logs identify the
storage errors. The successful rerun uses project-disk temporary storage. Obsolete
local diagnostic binaries were copied and hash-verified on project disk before
removing their temporary copies. No product workaround masks the failure.

The [eight-case matrix](R070c-final-native-matrix.json) covers normal and
ASan/UBSan runs, Wayland and X11, both display scales (`b4b739f`). A subsequent
CPU-fallback label refinement passes normal and sanitized Wayland 2× checks
([two-case matrix](R070c-label-native-matrix.json), `a26d959`). The final source
adds README clarification without further code changes.

Tests cover two running jobs plus queued work, cancellation and queue advancement,
retry of immutable captures, source revision/staleness, bounded logs, explicit
cleanup, closing/reopening an image and restoration after destroying/recreating
the native window. The status chip counts all running and queued jobs. Two decoded
result tabs bound UI image memory; retained results remain on disk until removal.
Model geometry, history and provider configuration remain independent of Jobs.

The [Jobs screenshot](R070c-jobs.png) and [window screenshot](R070c-window.png)
show two workers and one queued capture. The Jobs screenshot was visually inspected
for readable state/provenance and accessible selected-job and cleanup controls.

The [installed smoke](R070c-installed-smoke.json) verifies exact desktop bytes,
seven catalogs, six contracts, explicit Eevee settings and relocated captured HDR
assets. [Source package](R070c-source-package.json): **148 installed inputs** match
byte for byte. SHA-256: `14bc23acebf64e1cc30db7f632c7d4a5d7b91a5160e68efaa83a87b65e6683e5`.

R070 is locally complete. Remote CI and ordered merges remain delivery gates.
