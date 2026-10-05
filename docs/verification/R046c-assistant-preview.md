# R046.c assistant viewport preview verification

Date: 2026-10-05 UTC. Local validation passed; CI pending.
[Rendering contract](../decisions/0039-assistant-viewport-preview.md).

The native GL fixture creates an immutable composed edit containing removal,
translation, visible addition and an addition behind an opaque live face. It
counts actual framebuffer colors, checks hatch coverage includes gaps, verifies
opaque occlusion and live-model picking, and checks that preview installation,
inspection and clearing preserve serialized bytes and history. Temporary hiding
removes overlay geometry; a manual edit invalidates the displayed proposal and
prevents reinstalling its stale handle. The final GL error state must be clean.

All 62 enabled development suites pass in 37.44 s (real-Blender opt-in skipped).
The final preview fixture passes X11 and isolated Wayland at DPR 1 and 2, plus
ASan/UBSan with leak detection under isolated Wayland. The sanitizer run uses the
previously documented Qt client-decoration workaround from R050; leak detection
remains enabled. Existing viewport suites pass on both X11 and Wayland, including
transparency, clipping, picking, topology and GL-context recreation.

A physical Intel/Mesa Wayland run also passes. Its framebuffer is retained at
`build/evidence/r046c/native-preview.png` and was visually inspected. That review
caught a length-label formatter error; the correction uses linear units and the
fixture now checks accessible measurement text. The final display matrix and
sanitizer run include this correction. Source packaging and installation pass.

The fixture does not simulate a live provider or claim the assistant panel/banner
controls are already integrated. Live picking remains live-model picking; preview
entity inspection is explicitly selected by the host.
