# CI tiers: a fast blast-radius tier per change, the full tier before a release

Date: 2026-10-21. Status: **Accepted** (owner directive of 2026-10-21: cut CI time by a
factor of ten). Workflow: `.github/workflows/native.yml`; map:
`.github/ci-path-map.json`; scripts: `scripts/ci-plan.py`, `scripts/ci-run-lines.py`,
`scripts/ci-line-inventory.py`.

## Context

Every pull request and every push to `main` ran all ten jobs of `native.yml`. Over the
60 runs from 2026-10-19 to 2026-10-21 a run took 41–45 minutes of wall time at the
median and about two hours at worst, with about 2.7 hours of runner time per run. There
was no queueing: the critical path was the sanitize job (47 minutes median, 116 worst),
then the development build (28 median, 60 worst), whose own critical path was a
compile of 750 objects at `-O0 -g` on a 4-vCPU runner (9–35 minutes depending on how
many headers the change touched), linking about 330 statically linked test
executables, and a serial `ctest` of 13 minutes.

The owner asked for at most about 5 minutes from push to verdict for a typical pull
request, about 10 minutes for the worst realistic one (a core geometry change), no
build for documentation, and at least ten times less weekly compute; reduced testing
until a release is acceptable.

## Decision

### Two tiers in one workflow

A **Plan** job (`scripts/ci-plan.py plan`, about 15 seconds, no container) chooses the
tier and what runs, and checks the test-line inventory and the path map.

**Full tier**: every job that existed before, unchanged in its lines (the inventory
proves it: the full-tier lines equal the previous workflow's lines). It runs on

- tags `v*` and pushes to `release/*` branches;
- pull requests from or into a `release/*` branch;
- pull requests labelled **`full-ci`** (read live at plan time; add the label, then push
  or re-run the workflow);
- the **nightly schedule** (06:23 UTC) against `main`, which is non-blocking and
  catches regressions before release time;
- `workflow_dispatch` with `tier=full` (the default):
  `gh workflow run native.yml --ref <branch> -f tier=full`.

**Fast tier**: every other pull request and push to `main` (and `workflow_dispatch`
with `tier=fast`). It is sized by the changed paths:

| Job | Runs when | What |
| --- | --- | --- |
| Plan | always | inventory and map checks; tier and areas |
| Fast tier: compile shard 1–10 | `build` and a *wide* change | each shard compiles a tenth of the objects of `all` and exports only the compiler-cache entries it added |
| Fast tier: build and tests | `build` | restores the newest cache plus the shards' entries, builds everything (`ci-fast` preset), runs `ctest -j4` without the slow suites the change does not reach, and every repository check of the old development job (MCP schema, shipped recipes, generated reference and API schemas, glTF validator, DXF and PDF consumers, headless recipes) |
| Fast tier: X11 1x | `native` | builds the native fixtures and runs the full tier's X11 1x lines, four at a time, each with its own home, XDG directories and display range |
| Blender interoperability | `blender` | the full-tier job |
| Arch package acceptance | `package` | the full-tier job |
| **CI verdict** | always | fails if any job failed or was cancelled, or if a job the plan selected did not succeed |

**CI verdict is the single check a merge waits on.** A docs-only change runs Plan and
CI verdict only.

### Blast radius

`.github/ci-path-map.json` maps path globs to areas; a changed path selects the union
of every matching rule. Areas: `build`, `native`, `blender`, `package` and three slow
CTest groups. In summary:

| Paths | Areas |
| --- | --- |
| `docs/**`, root `*.md`, `LICENSE`, editor and ignore files | none: no build |
| `docs/api/**`, `docs/TOOL_REFERENCE.md` | build, native (tests read the schemas; the generators check them) |
| `src/core/**`, `src/geometry/**`, `src/sketchyup/**`, `src/solar/**`, `third_party/**` | build, native and every slow suite |
| other `src/**`, `resources/**`, `examples/**`, `scripts/**`, `.github/**`, `CMakeLists.txt`, `CMakePresets.json` | build, native |
| terrain sources and tests | adds the slow terrain suites |
| assistant and provider sources, corpus and scorer | adds the provider release corpus |
| `tests/**` | build; native fixture sources (`sketchyup_native_fixture()` in CMakeLists) and `tests/*.hpp` add native |
| `src/integrations/**`, the Blender and glTF verifiers, the export fixtures | adds Blender interoperability |
| `packaging/**`, package and release scripts | Arch package acceptance; `packaging/arch/mirrorlist` (it decides every package) selects all |
| anything no rule covers | build, native and every slow suite (and `ci-plan.py check` fails until a rule covers it) |

The slow suites (at least about 15 seconds each at `-O0`) carry 560 of the 765 seconds
of the serial CTest run: `slow-terrain` (terrain_history, terrain_stamp,
terrain_drape, terrain_sculpt, terrain_source_edit, terrain_replay, terrain_edit,
terrain_edit_input), `slow-core` (incremental_validation, document_limits,
variant_edit_cost, component_parameters) and `slow-assistant`
(provider_release_corpus). Every other CTest test runs on every code change.

`ci-plan.py check` keeps the map honest on every run: every tracked file matches a rule;
every slow test exists in CMakeLists.txt and its group is selected by some rule; the
examples, taken from merged PRs #364–#393, give their recorded areas; and every
document CMakeLists.txt installs exists (a renamed ADR would otherwise fail only in
package acceptance).

### Every test still runs in some tier

`scripts/ci-line-inventory.py check` now tags each inventory line with its job's tier
(`tier=full`, `tier=build`, `tier=native`, …) and fails unless a full-tier line runs the
whole CTest suite (`ctest --preset dev`) and every `sketchyup_native_fixture()` target
is named by a full-tier line or a script one calls. Three fixtures are manual tools that
no tier ran before either: `native_provider_trial`, `terrain_budget_benchmark` and
`real_model_benchmark`. A new CTest test therefore always runs in the full tier and,
unless it joins a slow group, in the fast tier; a new native fixture fails the check
until a full-tier line runs it, and its source file selects the fast X11 job.

### A fast build

- **Preset `ci-fast`** (binary directory `build/dev`, so every CI line applies): the dev
  build without debug information (`-g0`), with the project libraries linked as shared
  objects (`SKETCHYUP_SHARED_LIBS`, as the sanitize build already did) and linked by
  mold. The 330 test executables then link in seconds, not minutes. Measured on single
  objects, `-g0` saves about 20 % of compile time; precompiled headers saved about
  15 % and Clang about 30–40 %, but Clang could change the floating-point bits the
  pinned fixture digests check, so it was not adopted.
- **Compiler cache keyed by commit.** Fast-tier runs that are not pull requests save
  `ccache-fast-v1-<commit>-<run>`; pull requests restore the newest one from their base
  branch. The plan compares that commit with the tested one: when headers, the build
  definition, vendored code or the mirror changed in between, or more than 24 sources,
  the change is **wide** and ten shards compile first. Each shard lists the cache files
  before and after its build and uploads only the new ones; ccache is
  content-addressed, so merging entries can only add hits, never change an object.
  Otherwise the change is **narrow** and the test jobs compile their few misses
  themselves.
- **No build-tree transfer**: each fast job builds from the cache instead of packing and
  downloading the 37 GB tree.
- **Parallel tests**: `ctest --parallel 4`, and the X11 lines four at a time.

A prebuilt CI image on GHCR was considered: package installation from the existing
pacman cache takes about 45 seconds per job, and pulling a 2–3 GB image would recover
only part of that while adding an image-publishing pipeline. It is left for later.

### Removed

Nothing is removed from the full tier. The development job's inventory check moved to
the Plan job.

## Release rule

**A release needs the full tier green at the exact release-candidate commit** — the
`v*` tag push or a `release/*` branch runs it, or `workflow_dispatch` with `tier=full`.
[M10 acceptance](../M10_ACCEPTANCE.md) and [M9 acceptance](../M9_ACCEPTANCE.md) read
"every CI job at each merged head" as the CI verdict per merge plus the full tier at the
RC. A red nightly run is fixed before the next release candidate.

## Measurements

MEASUREMENTS_PLACEHOLDER

## Consequences

- A regression that only the full tier sees (a sanitizer finding, Wayland, 2× or
  fractional scaling, the accessibility audit, M5 recordings, the Blender worker, or a
  slow suite outside the changed areas) lands on `main` and is caught by the nightly run
  or at the release candidate instead of before merge. Label a risky PR `full-ci`.
- The fast tier builds `-g0` with shared libraries and mold; the full tier keeps the
  static `-g` dev build, so link-order or static-initialization problems specific to
  static linking surface in the full tier.
- When `main`'s cache is behind (a run is still in flight after a merge), a pull request
  can be planned wide although its own change is narrow. That costs minutes, never
  correctness.
