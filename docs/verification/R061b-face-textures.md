# R061.b — Persistent front/back face projections

2026-10-06 UTC. Depends on R061.a's numerical kernel.
[Storage and editing contract](../decisions/0062-face-texture-records.md).

The new `face_textures` suite passes in **0.09 seconds** in the development build
and **0.76 seconds** with ASan, UBSan and leak detection. Its independent checks
cover side-specific assignment/reset, mapping-only change reports, immutable
prepared edits, Undo/Redo, byte accounting, material replacement and face reversal.
Malformed assignments leave the original document unchanged.

Splits retain both projections. Healing incompatible mappings rejects atomically.
Reflected/sheared raw copies and consolidation preserve UV at independently
transformed points. Grouping transfers mappings and removes retired source entries;
push/pull retains the explicit projection on generated surfaces. A partial vertex
edit samples its unchanged local projection against an analytic coordinate oracle.
A bent world-space sweep retains the driving projection in source-local coordinates.
Site placement retains exact member records. Shared component edits reach both
instances, make-unique preserves the sibling, and axes changes preserve member-local
projections.

Subtraction checks both mappings at every resulting outer-loop vertex against its
source operand frame and recorded reversal provenance, including a reflected tool.
Undo restores the original immutable records. Hosted reveal regeneration retains
authored current mappings, initializes new reveals from the entry projection and
prunes retired reveals.

Inline JSON and packaged schema 16 reopen exactly. Relocation retains image bytes,
the explicit missing-resource record, material opacity and independent mappings.
Replacing the missing image preserves its asset/material identities and mapping;
Undo restores the missing state. Eleven malformed mapping-record variants reject.
Schema 15 rejects unadvertised mapping fields; proper migration invents none.
The retained actual v15 hosted fixture is
[`container-hosted-v15.sketchyup`](../../tests/fixtures/container-hosted-v15.sketchyup),
SHA-256 `3bd0ff4056ed9d56ab640afa9abbd06b90985851086370a865809736ae23d0ed`.

All **16 targeted sanitizer suites** pass in **34.06 seconds**: face textures,
materials, components/definitions, consolidation, appearance, groups, raw transforms,
edge appearance, hosted/glue persistence, document units, solid/sweep commands,
recovery and historical persistence. Their old-schema constructors explicitly remove
the new field; historical byte fixtures remain untouched.

All **102/102 development CTest suites** pass in **94.28 seconds**, including real
Blender. Native materials, components, hosted placement and the integrated M6
workflow pass on X11 and Wayland at DPR 1 and 2: **16 native checks**. The new
fixture stores a valid one-pixel RGBA PNG; its final targeted development and
sanitizer reruns pass with those bytes.

The [installed CLI smoke](R061b-installed-smoke.json) writes and relocates a schema
16 file with both explicit mappings, a packaged image and 0.5 swatch opacity.
Reopening is byte-for-byte exact. The previously installed schema 15 reader fails
explicitly (`Unsupported container field`) and creates no output. Both installed
mapping contracts match source bytes.

This is storage/modeling acceptance, not image rendering or native texture-authoring
acceptance. Those integrations and complete R061 acceptance remain pending. M7
delivery also awaits the M6 gate.
