# R049 optional Blender worker verification

Date: 2026-10-05 UTC. Local acceptance passed; CI/merge acceptance pending.
[Worker contract](../decisions/0033-blender-worker.md).

The full development CTest run passed in 34.00 s: 59 suites passed and the explicit real-Blender case skipped as designed without its opt-in environment. The real case was run separately below.

The lifecycle fixture exercises actual child processes, with deliberately fake
render results: executable absent, unsupported version, abnormal exit, missing
PNG, invalid PNG bytes, hash/dimension/source/device/preset mismatches, fractional
version, excessive stdout, timeout, cancellation at launch and during asynchronous
verification, failed GPU with one successful/failed CPU retry, concurrent job bound,
destruction and owner-thread rejection. Reusing the same captured input across
jobs preserves its bytes and isolates output directories. These tests passed in
0.99 s and with ASan/UBSan plus leak detection enabled in 2.69 s.

The separate opt-in real test ran installed Blender **5.2.1 LTS**, build
`9e2066aef7ef`, on this Linux/Intel Ultra 7 host. It probed CPU capabilities, rendered
a 128 × 128 image at four samples with four CPU threads, fully decoded a nonuniform
scene image and verified the original captured revision after editing the source
document. An explicitly unavailable METAL device failed and retried once on CPU;
disabling fallback produced a real failure. Cancellation terminated another actual
Blender process without publishing an image. All checks passed. GPU rendering on
a supported physical GPU is not claimed.

Run `SKETCHYUP_BLENDER_TEST=/usr/bin/blender ctest --test-dir build/dev -R
'^blender_real$' --output-on-failure` for real validation. Ordinary CTest skips that
case when no explicit executable is supplied; editing/building never requires
Blender. CI installs Blender for interoperability and explicitly runs this test.
`SKETCHYUP_RENDER_EVIDENCE=/absolute/path/prefix` optionally retains the verified
CPU PNG and result manifest from the real test.

The native render setup/result UI is R050 and is not part of this acceptance.
