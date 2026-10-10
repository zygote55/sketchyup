# R082.f — Maximum-size texture fixture

2026-10-07. Benchmark source `c018449641e3178315157f19e1acf2279c7b2351`
adds `textures` to the [real-model fixture contract](../decisions/0141-real-model-performance-fixtures.md).
Two deterministic 4096 × 4096 RGBA checker images supply front and back materials
on the repeated prism component. Their combined decoded size is 128 MiB, exactly
the existing image-cache budget. No image, asset or document limit changes.

All **five native AMD runs pass**: the unchanged 1,000-instance repeated control,
a 25-instance texture smoke, and three 1,000-instance / 100,000-triangle texture
runs. Both texture generations must finish without a missing-image, budget or
mapping fallback before sampling and at completion. Every run retains the exact
unclipped 1920 × 1080 framebuffer, geometry-hit checks, cache reuse, 100 edits with
undo/redo, bounded body-cache capacity and zero final GL error.
[Artifact, environment and individual report hashes](R082f-large-textures.json).

The three full texture runs share canonical fixture hash
`370b777e583aa817345ff5bae0e1e0f6c1b33299066f833178b5fcf8b413d84c`.
Scene p95 is **2.271–2.606 ms**, pick p95 **0.067–0.078 ms**, and edit plus
readback p95 **18.482–20.464 ms**. Peak process RSS is 587,071,488–588,423,168 bytes.
The reported texture RGBA bytes describe fixture inputs, not observed GPU
allocation or complete cache accounting. Scene timing still ends at glFinish
and excludes painter overlays and compositor presentation.

The build overlays only the benchmark source on the completed Release package
cache; runtime, third-party and CMake inputs remain byte-identical to `29fb31b`.
The original cached benchmark source is restored and compared after compilation.
Package archives are untouched. Local compilation uses the eight-CPU ceiling,
two jobs, 4 GiB memory, no extra swap and disk-backed temporary files.

Each sequential remote run verifies one CPU, affinity, 4 GiB memory, zero swap,
128 processes and a 300-second deadline on the same CachyOS QEMU guest / AMD
Navi 21 system. The earlier board-name ambiguity remains. Accelerated GL and
awake-display preflight pass; guest load is recorded, hypervisor load unknown.
No desktop changes or display wake are needed.

These exploratory results extend coverage; they do not accept R082 or release
performance. The 10,000-instance fixture, inference timing, complete frame
feedback, full memory accounting, current integrated reference runs and final
release acceptance remain open.
