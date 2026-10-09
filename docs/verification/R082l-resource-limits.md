# R082.l — Shared document and native storage limits

2026-10-07. Document admission, component projection, persistence, recovery,
library bundles, scene capture and capability declarations now reference shared
limit constants. Previously the same values appeared as independent literals,
and aggregate edge admission was coupled to a geometry-kernel constant.
**Every numeric limit and existing capability field remains unchanged.**
Per-body geometry-kernel limits remain separate from aggregate document limits.
Legacy JSON has its own named model-byte bound, equal to its previous value.
The maximum container payload is statically required to fit the file-byte limit.

The aggregate profile remains 10,000 bodies, 100,000 vertices/faces/wires,
300,000 edges and 10,000 curves/guides. Native storage remains a 128 MiB file
bound, 32 MiB model and legacy-model bounds, 1 MiB manifest, and the existing
64 MiB total asset bound. The container bound is their existing 97 MiB plus
16-byte header. This refactor does not admit the private million-triangle
experiment or claim expanded supported-model capacity.

[All 36 focused suites pass](R082l-resource-limits.json): eighteen normal and
the same eighteen ASan/UBSan suites with leak detection enabled. They cover
document snapshots, components/placement/gluing/records, scenes, assets,
persistence, native format, library bundles, recovery/CLI, saved-scene IO and
commands, public commands and transaction dispatch. Both generated reference
artifacts remain byte-exact with the live registries. No new behavior-mirroring
tests were added for replacing literals with the same shared values.

The two cached Debug variants build and run sequentially on the local machine
under a shared work lock, an eight-CPU quota/affinity, two compiler jobs,
4 GiB RAM, zero additional swap and disk-backed temporary storage. The sanitizer
command suite took 169 seconds and the worker reached the 4 GiB cap, but all
checks completed successfully. This is not a clean-package or release acceptance
run. The earlier private widened-cap experiment remains separate.
