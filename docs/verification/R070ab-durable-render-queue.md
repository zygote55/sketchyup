# R070.a–b — Durable render capture, queue and process recovery

2026-10-06. Storage implementation `f514e85`; scheduler/storage-failure acceptance
`515dd57`; integrated acceptance `d32ce5d`.
Parent: [PR #161](https://github.com/zygote55/sketchyup/pull/161).
Contracts: [0099](../decisions/0099-retained-render-jobs.md),
[0100](../decisions/0100-render-queue-lifecycle.md).

The complete Debug build and **135/135 CTest suites pass** in **291.27 s**.
This includes actual Blender Cycles/Eevee lifecycle and the new render_store and
render_queue suites. Focused worker/store/queue verification passes in 2.43 s;
ASan/UBSan with leak detection and halt-on-error passes all three in 9.16 s.
Four final native Wayland 2× integration cases pass in
**19.652 s** ([matrix](R070ab-native-matrix.json)).

The store owns a private locked directory, immutable source packages and atomic
versioned records. Tests destroy the original capture and edit the live document,
then verify retained output against its original source. Reopening revalidates
queued sources and completed images. Corrupt images never retain a trusted completed
state. Queue order is persisted independently of wall-clock timestamps.

Scheduler tests start two workers concurrently, queue additional work, cancel before
and after launch, retry the same capture after executable/storage repair, verify an
explicit CPU fallback and remove only finished work. A blocked image destination
fails without publication. A blocked job record prevents launch and exposes failure
even when the error record cannot itself be saved.

A disposable application process starts a worker and persists its log. The test
force-kills the application, checks that Linux parent-death handling stops the child,
then reopens the store: the job is interrupted, its bounded log remains, and owned
scratch files are cleaned. Graceful owner destruction is also covered. Retained
records, queue admission, disk budget, attempt history and output bytes are bounded;
there is no silent result eviction or model/history mutation.

The [installed smoke](R070ab-installed-smoke.json) checks relocated captured lighting
and Eevee settings, invalid-input rejection, seven catalogs, five installed contracts
and a matching desktop binary. [Source package](R070ab-source-package.json):
**147 installed inputs** match byte for byte. SHA-256:
`65185b5df83c8f3552ff826a256d63eb04c04d3525ea5f984bd397c76e1dea20`.

This layer is the persistent storage/scheduling foundation. R070.c adds the native
Jobs list, concurrent capture controls and reopening retained results. Remote CI and
ordered merges remain delivery gates.
