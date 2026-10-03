# R020: geometry regression and seeded edit gate

Date: 2026-10-03. Local gate checks passed; pinned CI and merge pending.

The default deterministic corpus runs 48 seeds with 24 edit attempts each. It
mixes edge subdivision, face push/pull, transforms, face erase, explicit healing,
coincident cleanup, planar insertion and invalid operations. Every successful edit
is undone and redone, checking exact scene/topology records, monotonic allocator
floors, adjacency consistency and an untouched neighboring body. Rejections must
preserve the original immutable record, revision and history. Diagnostics are
bounded. Seed and operation trace print on failure.

[Default run](R020-seeded.json): 1152 attempts, 729 commits, 290 rejections and
133 no-ops. This is an invariant corpus with generated edit sequences, not a proof
of every geometric configuration. The [extended run](R020-extended.json) exercises
256 seeds with 48 attempts each: 7678 commits, 3209 rejections and 1401 no-ops.
ASan/UBSan runs the default corpus in CI.

Twenty-four additional seeded scenarios execute split → extrude → inset → through
cut → preview/commit → durable save → reopen → undo/redo → backup. They check signed
volume, closed radial incidence, stable identities, prospective/committed lineage,
exact reopened records, and clean/dirty state transitions. Existing R015–R019
fixtures cover non-manifold incidence, holes, overlaps, reversed loops and invalid
healing/intersections. All suites run as CTest entries, including package builds.

## Tolerance contract and regression found

The [machine-readable report](R020-tolerance.json) records local-meter linear
tolerance 1e-7 and coordinate bounds ±1e6. Face area uses squared linear tolerance;
point acquisition in logical pixels never changes this value. The corpus covers
squares from 1 micrometer to 100 kilometers, a 900-kilometer world origin,
sub-tolerance coordinates, near-planarity, degeneracy, touching holes, reversed
winding and non-manifold sweep rejection. This does not promise reliable editing
of arbitrary geometry exactly on the tolerance threshold.

The corpus found that `Surface::normal` compared a Newell area vector against a
linear tolerance, rejecting valid 0.1 mm squares. It now checks twice the squared
linear tolerance before normalization. Regression cases include 1 µm, 10 µm,
0.1 mm and 1 mm squares; below-tolerance 0.01 µm geometry still rejects atomically.

## Reproduce and minimize

```sh
ctest --preset dev
ctest --preset headless
ctest --preset sanitize
build/dev/geometry_fuzz_tests --seed 37 --steps 24
build/dev/geometry_fuzz_tests --seed 1 --count 256 --steps 48
python3 scripts/minimize-geometry-failure.py build/dev/geometry_fuzz_tests \
  --seed 37 --steps 24 --output /tmp/geometry-failure.json
```

Use the minimizer only with an actually failing seed. It finds the shortest failing
prefix and writes the exact replay command, exit status and trace. It refuses a
passing seed. The reducer itself was checked with a synthetic process that first
fails at step 7; it returned 7. It does not claim polygon-coordinate minimization.
Keep the original seed, compiler/sanitizer output and revision, then convert the
shortened case into an explicit fixture before fixing a defect. Distinguish a
product failure from host/process failure; do not weaken invariants or silently
skip rejected corpus cases to make a regression pass.
