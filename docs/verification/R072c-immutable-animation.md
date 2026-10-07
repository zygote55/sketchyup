# R072.c — Immutable saved-scene frame capture

2026-10-07. Capture implementation `0a85d6c`, saved lighting/section coverage and
contract `87a4878`, final integration and CI `12fff4e`.
Parent: [PR #165](https://github.com/zygote55/sketchyup/pull/165).
Contract: [0106](../decisions/0106-immutable-animation-capture.md).

The complete Debug build and **139/139 CTest suites pass** in **149.93 s**.
Four final native Wayland 2× integration suites pass in
**17.514 s** ([matrix](R072c-final-native-matrix.json)).
The camera/capture/export sanitizer group passes **3/3 in 2.28 s** with ASan,
UBSan, leak detection and halt-on-error. Export-specific assertions belong to the
following layer; this layer retains the same capture implementation.

Tests freeze ordered scene IDs, saved cameras, frame timing and document records.
Worker-thread frame preparation survives later edits and deleted scene records.
Private recall applies body/tag/transient visibility, named sections, styles and
solar state without editing the live document or its history. Omitted scene
properties start from the original frozen model, not the previously recalled frame.
GLB and manifest provenance retain the original document identity/revision and
saved scene ID even when private recall changes a copy's revision.

Free clipping, show-hidden mode, missing scene references and requests outside the
bounded timeline fail explicitly. Exact camera endpoints, outgoing visibility
through transitions, projection cuts and finite resource limits retain the prior
camera math contract.

The [installed smoke](R072c-installed-smoke.json) verifies seven catalogs, eleven
contracts, exact desktop bytes and existing relocatable lighting/export behavior.
[Source package](R072c-source-package.json): **153 installed inputs** match byte
for byte; SHA-256 `f364e9e034f1c0a32b300af2e760df12894a998396f0adfb7bec072b98b64ac0`.

R072.c is locally complete. The animation worker/UI and integrated presentation
study follow in R072.d. Remote CI and ordered merges remain delivery gates.
