# R082.gg — Public placement limits

2026-10-11. Branch `build/r082gg-placement-limits`, based on `origin/main` after
R082.dd (#289). This is the last R082 component-instancing layer. It splits the
public document limits so that placements no longer count as unique geometry, and
it makes the 10,000-placement, million-triangle fixture
([ADR 0141](../decisions/0141-real-model-performance-fixtures.md)) public.
[Raw summaries and hashes](R082gg-placement-limits.json). This is not release or
hardware acceptance.

## Limit model

| Class | Counts | Limits |
| --- | --- | --- |
| Unique geometry (values unchanged) | Records outside every placement plus each definition's members once; placement roots and projected members excluded | 10,000 bodies; 100,000 each of vertices, faces, wires; 300,000 edges; 10,000 each of curves, guides |
| Placement records | Materialized component instances, nested ones included | 20,000 |
| Materialized totals | Every record after expanding all placements | 40,000 bodies; 600,000 each of vertices, faces, wires; 900,000 edges; 40,000 each of curves, guides |

Before this layer a single set of limits (the unique values) applied to the
expanded document, so the fixture was rejected at placement 1,924. It now uses
10,000 of 20,000 placements, 20,000 of 40,000 materialized bodies, 520,000 of
600,000 vertices, 280,000 of 600,000 faces and 780,000 of 900,000 edges. Its single
definition counts 2 bodies and 52 vertices of unique geometry.

The materialized values bound the worst document they admit to a modeled
2.67 GiB peak RSS, below the new 3 GiB ceiling, even if every placement draws its
own world-space mesh ([R082.w v2](R082w-memory-budget-v2.md) gives the arithmetic).

### Enforcement

- `placeComponent` and component publication (create, make unique, replace, edit)
  preflight the materialized totals and placement count before building records.
- `Document::apply` checks the materialized totals with the changed records, the
  placement count before validating bindings, and unique geometry after it. The
  incremental scheme uses running totals and running per-definition placement
  counts; the full-validation oracle recounts both and must report the same
  message. `verifyIncrementalState` and the oracle's state comparison include the
  placement counts.
- Undo and redo maintain the counts; they restore states that were valid.
- Load/decode bounds stored body records, stored instance records, running stored
  vertices, faces, wires and guides, and the placement expansion before allocating
  it. `restore` then repeats the full check, including unique geometry.
- Definition validation bounds the definitions' members together against the
  unique limits and each definition's expansion against the materialized limits.
- The defensive instance-count guards in binding validation now use the placement
  limit instead of a separate hard-coded 10,000.

### Native file bounds

The container manifest records allocator floors for every materialized body,
about 25 bytes each. Saving failed above about 42,000 bodies with
`Container manifest exceeds 1 MiB` (seen at 55,000 and 77,000). The manifest bound
rises to 4 MiB; the container bound becomes 16 B + 4 MiB + 32 MiB + 64 MiB, still
below the 128 MiB file bound. The schema and the public format contract
(`native-format-v1.json`, which does not list the manifest bound) are unchanged.
A new test saves and reloads a document at exactly 20,000 placements and 40,000
materialized bodies: its manifest is 940,405 bytes and its container 3,041,390 bytes,
and it saves identically after reloading. The 10,000-placement fixture's container
is 5,859,283 bytes, within the 32 MiB model bound.

### D06 diagnostics

Every limit rejection now names the operation, the limit class, the first exceeded
quantity (in the order placements, bodies, vertices, faces, wires, edges, curves,
guides), its attempted and maximum values with locale-independent separators, and
the offending definition, placement or body:

```text
Component placement exceeds document placement limits: vertices would be 610,000 of 600,000 (placing definition 'Boundary' #1 into the model)
Document edit exceeds document unique geometry limits: bodies would be 10,001 of 10,000 (body #10012)
Document load exceeds document placement limits: placements would be 20,001 of 20,000 (stored component instances)
Component definition exceeds document unique geometry limits: vertices would be 100,400 of 100,000 (definition 'Chair' #3)
```

Apply names the changed record that grows the exceeded quantity, chosen from the
edit alone so that the incremental and full schemes agree. Definition names are
the only user content; control characters are replaced, names are truncated to
64 UTF-8-safe bytes with an ellipsis, and messages are capped at 256 bytes.
The earlier generic strings (`Component placement exceeds document editing limits`,
`Document complexity exceeds editing limits`, `Component expansion exceeds document
editing limits`, `Too many bodies`, `Too many component records`, `Too many
component member bindings`) are replaced. Definition nesting deeper than 128
levels now reports `Component nesting exceeds 128 levels (definition …)`.
Kernel per-body limits (`Surface exceeds editing limits`) are unchanged.

### Capability output

`limits` keeps `bodies`, `vertices` and `guides` at the unique values and adds
`placements`, `materializedBodies`, `materializedVertices` and
`materializedGuides`. Scene visibility lists accept up to 40,000 bodies. The MCP,
headless-session, transaction and tool-reference contracts were regenerated from
the live registries.

## Measurements

Phase 1 measured source `387787b6` (R082.dd merged with `origin/main`) with a
test-only Release build that passed `-DSKETCHYUP_BENCHMARK_LIMIT_SCALE=8` through
`CMAKE_CXX_FLAGS`. The benchmark ran in `sketchyup-native-fonts-check:20261008`
with one CPU, 4 GiB and no swap, under `xvfb-run` at 1920 × 1080 with llvmpipe.
Peak RSS is the benchmark's own `getrusage` maximum, as in R082.w; in software,
GPU buffers are process memory. These are software measurements, not hardware
evidence. Timings are from a shared host and labelled as software.

| Run | Peak RSS B | Body vectors / GPU payload B | Viewport p95 ms | Edit + readback p95 ms | Result |
| --- | ---: | --- | ---: | ---: | --- |
| repeated 1k, 5 cycles | 288,276,480 | 89,266 / 34,656 | 374.1 | 1,405.9 | pass |
| unique 1k, 5 cycles | 560,132,096 | 89,088,000 / 34,656,000 | 236.9 | 616.3 | pass |
| deep 1k, 5 cycles | 297,476,096 | 89,266 / 34,656 | 270.9 | 620.8 | pass |
| far 1k, 5 cycles | 294,277,120 | 89,266 / 34,656 | 211.8 | 505.7 | pass |
| textures 1k, 5 cycles | 561,713,152 | 89,266 / 34,656 | 410.4 | 906.7 | pass |
| **repeated 10k, 5 cycles** | **613,515,264** | 89,266 / 34,656 | 2,512.5 | 5,592.2 | pass; growth 6,627,328 B, history 1,636,500 B |
| repeated 10k, 1 cycle | 606,810,112 | 89,266 / 34,656 | 1,542.4 | 5,638.4 | pass |
| deep 10k, 1 cycle | 624,054,272 | 89,266 / 34,656 | 2,436.8 | 7,123.9 | pass |
| textures 10k, 1 cycle | 883,724,288 | 89,266 / 34,656 | 3,738.1 | 8,392.8 | pass |
| repeated 10k, sharing off | 1,928,605,696 | 890,880,000 / 346,560,000 | 2,936.6 | 7,418.5 | pass |
| far 1,200–10k, sharing on and off | — | — | — | — | fixture defect, see below |

R082.w v1's pre-instancing 10,000-placement runs peaked at 3.18 GB on AMD hardware.

A host probe (Release, same source) built the identical fixtures and measured heap
(`mallinfo2`), per-phase peak RSS (`/proc/self/clear_refs` 5, then `VmHWM`), the
production inference index and native save/load:

| Fixture | Core heap | Inference index | Construction | Save (file, peak RSS) | Load (time, peak RSS) |
| --- | ---: | ---: | ---: | --- | --- |
| repeated 1k | 17.4 MiB | 0.81 MiB | 0.52 s | 585,100 B, 47.6 MB | 0.14 s, 46.6 MB |
| repeated 10k | 173.3 MiB | 7.9 MiB | 5.8 s | 5,859,283 B, 269.0 MB | 3.25 s, 264.6 MB |
| unique 1,923 (unique-limit worst case) | 30.6 MiB | 87.5 MiB | 3.3 s | 11,855,798 B, 239.0 MB | 0.81 s, 211.6 MB |

Core memory is about 18.2 KB per prism placement: about 1.29 KB per materialized
body plus 0.30 KB per materialized vertex, fitted from the prism and a ten-body
definition. Load time rises somewhat faster than linearly (2k 0.27 s, 4k 0.56 s,
8k 1.71 s, 10k 3.25 s). Inference updates at 10,000 placements take 100 ms at
p95 (10 ms at 1,000), linear in placements. 10,000 unique prisms (520,000 unique
vertices) remain outside the unique limits and could not be saved anyway: their
JSON is 61 MB, above the 32 MiB model bound.

### Far family at 10,000 placements: not yet measured

`real_model_benchmark` failed for the far family at 1,200, 1,500, 2,000, 4,000 and
10,000 placements with `Inference probe intersects rendered geometry`, with
placement mesh sharing on and off. The cause was a fixture defect, fixed in
R082.hh: the history loop called `doc.transform(editing, T * base)` without the
parent argument, which detached the edited placement from its group. The product
is unaffected. Far at 10,000 placements is not yet measured, so the family stays
excluded from the R082.w v2 1M class.

The same defect means the deep and far edit and inference-update timings above
(and in earlier R082 records) measured a detached placement and are not
comparable with a fixed fixture. Peak RSS, cache and frame figures are unaffected
by which placement is edited.

## Verification

Phase 2 source on this branch (merged `origin/main` `80545acc`):

- `cmake --preset dev && cmake --build --preset dev --parallel 3`: no warnings.
- `ctest --preset dev -j3`: 190 of 190 pass; the 11 opt-in `*_real` consumer tests skip as usual. The first run failed only the four contract-drift checks (`mcp`, `automation_session`, `transaction_dispatch`, `tool_reference_drift`) until the scene-visibility bound was regenerated.
- `incremental_validation` with the full-validation oracle: passes in 135 s; three seeds reach the unique limit 12, 14 and 16 times with 987 incremental validations, all matching the full scheme.
- `python3 scripts/generate-tool-reference.py --cli build/dev/sketchyup-cli --root . --check`:
  2 generated reference artifacts match.
- `python3 scripts/verify-installed-migrations.py --cli build/dev/sketchyup-cli --fixtures tests/fixtures --output OUT`:
  28 historical fixtures migrated; all preservation checks passed.
- ASan/UBSan (`cmake --preset sanitize -DSKETCHYUP_BUILD_CLI=ON`): 16 of 16 suites pass with no sanitizer report: `document_limits`, `document_limits_io`, `incremental_validation`, `components`, `component_records`, `component_placement`, `instanced_storage`, `native_format`, `mcp`, `automation_session`, `transaction_dispatch`, `saved_scenes_io`, `persistence`, `recovery`, `document_snapshot` and `history`. Because CI runs every sanitized core test serially, both long tests detect
  ASan (`__SANITIZE_ADDRESS__` or `__has_feature(address_sanitizer)`) and run a
  reduced set that prints its mode. `document_limits` places the public fixture
  1,000 times instead of 10,000 and keeps every other check: 98 s under ASan, down
  from 604 s. `incremental_validation` runs one seed of 120 steps instead of three
  of 220, builds the ballast without the oracle, and checks the materialized
  40,000-body boundary under the oracle in place of the 600,000-vertex boundary,
  whose fill costs about 0.2 ms per materialized vertex under ASan in any
  definition shape (at least 125 s): 75 s, down from 1,048 s. Unsanitized builds run
  the full set (dev ctest: 53 s and 138 s, identical seed results).
- Public Release build (no scale macro), repeated 10k in the same container: passes through `scripts/run-container-check.sh` with 10,000 placements, 20,000 bodies and one million triangles, fixture hash `5a0d89c3…` as in phase 1, peak RSS 612,790,272 B, one placement mesh, 89,266 B of body vectors and 34,656 B of GPU payload (binary `3c6f94b9…`).

New tests:

- `document_limits` (core): diagnostics format and bounds, the public path placing
  10,000 prism instances (one million triangles; 1,000 under ASan), an over-limit
  placement count through nested placements, an oversized unique-geometry edit and
  unique copy, and definitions together exceeding the unique limits.
- `document_limits_io`: save and reload at 20,000 placements and 40,000 bodies, an
  over-limit instance count and an oversized unique-geometry file rejected on load.
- `incremental_validation`: the materialized boundary (sixty 10,000-vertex
  placements), the unique body boundary beyond the old 10,000-body total, and
  randomized sequences that press the unique vertex limit. A locked ballast group
  keeps random erases from releasing that pressure; materialized limits are covered
  by the deterministic boundary, which random sequences would need about 1,250
  placements to reach. The old message assertions were replaced because D06
  requires the new bounded diagnostics.

## Remaining

- Intel and AMD reruns of the five 100k families and the repeated, deep and
  textures 10k families, plus the sharing-off control, per
  [M9_HARDWARE_SESSION.md](../M9_HARDWARE_SESSION.md) section 1.
- Far at 10,000 placements once the R082.hh fixture fix lands.
- Core still expands placements in memory (about 18 KB each); load time and
  inference updates grow with placements.
