# M6 advanced-modeling acceptance

M6 combines roof, stair, joinery and furniture fixtures with explicit site placement,
solid prerequisites, material/entity provenance and hosted components. The integrated
[result](verification/M6.md) distinguishes local evidence from CI/dependency merges.
The prior [M5 gate](verification/M5.md) remains the accepted assistant/native baseline.

## Reproduce deterministic evidence

```sh
cmake --preset dev
cmake --build --preset dev --parallel 2
scripts/verify-m6-offline.sh build/dev /absolute/new/evidence-directory /usr/bin/blender
```

The runner needs Python 3, Xvfb/xauth, FFmpeg with X11 capture/H.264 and an optional
absolute Blender executable. Omit Blender for the worker fixture; that does not
claim real Blender acceptance. Use a new evidence directory. The runner retains
all CTest output, five executable recipe results, measured native before/after
models, a GLB snapshot/manifest, actual native captures/videos, render evidence,
source hashes and an artifact inventory. It always leaves `m6GatePassed` and
`liveProviderTested` false: these require separately reviewed evidence.

For an already configured disposable container with this repository mounted at
`/work` and matching Qt/runtime dependencies, set `SKETCHYUP_M6_NATIVE_CONTAINER` to
its name. CTest and CLI checks run on the host; the native recordings run on an
isolated X server inside that container. Output goes to a fresh owned temporary
directory, is copied back, then removed. The build directory must be inside the
repository in this mode. No user desktop or existing model is recorded.

## Independently measured study

The starting study has an explicitly adopted room with two hosted windows, the
verified gable roof, straight stairs, table and cabinet, plus unrelated geometry.
Two crossed 400 × 60 × 80 mm stock members receive complementary 40 mm half-lap
notches. Two Trim operations and explicit cutter cleanup publish together as one
Undo task. Every returned face mapping resolves against the retained source model;
normal parity independently verifies physical front/back material assignments.

Each member retains `0.4 × 0.06 × 0.08 − 0.06 × 0.06 × 0.04 = 0.001776 m³`.
The actual notch floor/ceiling has 60 × 60 mm area at Z=40 mm with opposite normals.
Intersecting the two finished members gives no material overlap. Open stock reports
its solid prerequisite failure before mutation. A later edge-appearance edit keeps
mapped topology/materials, and placing the full study preserves all local records,
shared definitions and hosted relationships. The native workflow moves one member
up 120 mm at the distant coordinates, exposes both notches, and undoes it exactly.

The roof remains 4.554 m³ and the stair 4.368 m³. Table/cabinet envelopes remain
1.2 × 0.8 × 0.75 m and 0.9 × 0.4 × 1.2 m, with separately validated summed member
volumes 0.0455 m³ and 0.059705856 m³. Site placement uses explicit millimetres, world
frame and π/6 yaw, producing [100000.125, 200000.25, 12.5] m. Native persistence is
byte-for-byte; camera-relative rendering keeps that distant study usable.

## Live assistant checks

Use the already configured provider account through the OS credential facility.
No key belongs in commands, models, reports or chat. Only synthetic fixtures are
sent by these opt-in development executables:

```sh
build/dev/provider_trial --chatgpt gpt-6-astra advanced /private/new/advanced.json
build/dev/provider_trial --chatgpt gpt-6-astra site /private/new/site.json
```

`advanced` preserves an adopted room and requests the four default assemblies at
explicit positions, shared furniture components, private measurements and a preview.
`site` requests the explicit distant placement of the complete existing study.
The harness acts as reviewer only for its disposable model, applies a sealed preview,
saves it, and independently measures actual geometry and preservation.

A process exiting successfully is not a provider-quality pass. Require
`geometryVerified`, `unrelatedPreserved`, a `preview-ready` proposal and an applied
completed result; verify the staged measurement receipts. The legacy
`inspectionVerified` field checks the separate 6 m² measurement-only trial and is
not the acceptance predicate for these editing trials. Keep full provider payloads
and correlation data private. Published evidence contains measured summaries and
artifact hashes, not raw provider responses or account metadata.

## Gate closure

The gate requires the complete advanced regression/native/sanitizer checks, saved
fixtures and material/mapping evidence, real-provider checks above, installed-build
checks, and successful exact-head CI and dependency merges. Record unsupported
geometry explicitly. Neither a screenshot nor a provider's explanation proves the
requested geometry. M7 may prepare stable interfaces while this gate awaits CI;
it is not accepted as delivered before M6 closes.
