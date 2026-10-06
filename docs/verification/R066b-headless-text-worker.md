# R066.b — Headless font worker

2026-10-06. Implementation `40b4629`, integrated acceptance head `538e28d`.
Parent: PR #148. Contract: [0083](../decisions/0083-headless-text-worker.md).

The complete Debug build and **123/123 CTest suites pass**, including actual
Blender. The dedicated worker suite passes ASan/UBSan with leak detection and
halt-on-error in **4.39 s**. No compiler warnings were emitted.

Real worker fixtures remove display variables, deliberately inherit a GTK theme,
shape accented Unicode and Arabic across multiple lines, transport native extruded
geometry exactly and reproduce it across fresh processes. The worker overrides its
platform theme before GUI initialization: an initial test exposed GTK attempting to
open a display despite the offscreen platform, and the committed fixture now covers
that inherited environment. No user settings are changed.

The client rejects missing executables, timeouts, excessive output, malformed JSON,
unsupported protocols, invalid font fingerprints, inconsistent statistics,
non-default document metadata and unsupported body state before returning geometry.
No caller document is mutated. Process isolation is bounded but is not an OS sandbox.

[Installed acceptance](R066b-installed-smoke.json) checks byte-exact helper,
desktop and contract installation. The helper shapes text without a display and
rejects six invalid requests, including oversized input and missing-font policy.
A relocated client fixture discovers the helper through `../lib/sketchyup` from an
unrelated working directory and repeats the real-worker and failure cases.

[Source-package verification](R066b-source-package.json): **124 installed inputs**
match byte for byte; build and Git artifacts are excluded. Archive SHA-256:
`bdf4bd48f15219f3ad874de7b09404d2805fc20a18f63f60db18e7ed8eecf1a8`.

Editable records and user-facing authoring remain subsequent R066 work. Remote CI
and ordered dependency merges remain delivery gates.
