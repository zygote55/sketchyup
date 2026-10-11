# R082.w v2 — Memory budget with public placement limits

Version two of the [prospective memory calibration](R082w-memory-budget-v1.md). Version
one stays in force for its own evidence and is not edited. This version follows
R082.gg, which splits the public document limits into unique geometry, placement
records and materialized totals
([evidence](R082gg-placement-limits.md), [ADR 0005](../decisions/0005-document-protocol.md)).

The [versioned declaration](R082w-memory-budget-v2.json), frozen at
`2026-10-11T16:58:39+00:00` with SHA-256
`89cc564068f2cfbba6d5c433cc2ca493d5ba17c01ce084833f105c6f47470dd8`, sets:

| Budget | v1 | v2 | Calibration (software) |
| --- | ---: | ---: | --- |
| Supported 100k, five families (repeated, unique, deep, far, textures) | 1 GiB | 1 GiB | 288–562 MB |
| Supported 1M (repeated, deep, textures at 10,000 placements) | 3.5 GiB, experimental | 1.25 GiB | 614 / 624 / 884 MB |
| Modeled worst case at the public limits | — | 3 GiB | 2.67 GiB modeled; 1.93 GB sharing-off control |
| Own-process DRM resident upper bound | 2 GiB | 2 GiB | unavailable in software |
| Sampled resident growth over five cycles | 64 MiB | 64 MiB | 6.6 MB at 10,000 placements |
| Retained history | 64 MiB | 64 MiB | 1.6 MB |

`publicModelLimitsChanged` is now true. The million-triangle fixture is public and
moves from the experimental class into the supported class. The far family is
excluded from the 1M claim: far at 10,000 placements is not yet measured. The
earlier `Inference probe intersects rendered geometry` failure from 1,200
placements was a benchmark fixture defect (the history loop detached the edited
placement from its group), fixed in R082.hh; the product is unaffected. The far
family remains in the 100k class, where it passes.

**These ceilings come from software measurements.** They were calibrated with
llvmpipe in a one-CPU, 4 GiB container, where GPU buffers are process memory. In
v1, hardware RSS at 1,000 placements was about 150 MB above software. Acceptance
requires fresh Intel and AMD reruns following the
[M9 hardware session runbook](../M9_HARDWARE_SESSION.md), section 1 (1d for the
10,000-placement case). Until then no ceiling is accepted.

## Modeled worst case

The materialized limits are set so that the worst document they admit stays below
3 GiB, even when every placement draws its own world-space mesh (an active section
through every placement, a view-only opacity override, and similar fallbacks):

| Term | Bytes |
| --- | ---: |
| Base, including 1,000 shared placements (repeated 1k, five cycles) | 288,276,480 |
| 19,000 more placements × 36,138 B (larger than 40,000 bodies × 10.3 KB) | 686,622,000 |
| 600,000 materialized vertices × 2,542 B unshared viewport payload | 1,525,200,000 |
| 100,000 unique vertices × 917 B inference index | 91,700,000 |
| Maximum texture fixture (textures 1k − repeated 1k) | 273,436,672 |
| **Total** | **2,865,235,152 (2.67 GiB)** |

The per-placement term already includes each placement's core vertices, so those
are counted twice and the bound is conservative. The 2,542 B term is the measured
difference between the 10,000-placement fixture with sharing off (1,928,605,696 B)
and on (606,810,112 B) divided by its 520,000 vertices. With sharing on, the same
worst-case document models at about 1.48 GiB, since only its 100,000 unique vertices draw unshared. The unshared 10,000-placement control
must stay at or below the 3 GiB ceiling on hardware.

All other v1 rules continue unchanged: fresh forward runs with frozen identities,
complete DRM accounting at every checkpoint, no retrospective pass, the unchanged
five-cycle campaign, and the 4 GiB operational job cap as a separate constraint.
A finite five-cycle sample does not prove unlimited-session boundedness.
