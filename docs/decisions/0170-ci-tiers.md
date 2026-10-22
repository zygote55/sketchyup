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
proves it: the full-tier lines equal the previous workflow's lines), plus the Qt 6.8
reference compile job `qt68` (see the follow-ups). It runs on

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
| Fast tier: compile shard 1–12 | `build` and a *wide* change | each shard installs only the compile packages, generates Qt's moc sources, runs the compile commands of a twelfth of the objects of `all` directly (through ninja, an object first waits for every library its target links, so each shard would recompile all libraries) and exports only the compiler-cache entries it added |
| Fast tier: tests 1/5–5/5 | `build` | each restores the newest cache plus the shards' entries and builds everything (`ci-fast` preset; hits and links, about 25 seconds), then runs its fifth of CTest at `--parallel 2`, without the slow suites the change does not reach. `scripts/ci-ctest-shard.py` deals the tests longest-first by their known serial times (`test_seconds` in the map), so every test lands in exactly one shard. Shard 1 also runs `mcp` with the pinned MCP schema validation and every other repository check of the old development job (shipped recipes, generated reference and API schemas, glTF validator, DXF and PDF consumers, headless recipes) |
| Fast tier: X11 1x 1/2–2/2 | `native` | builds the native fixtures and runs half of the full tier's X11 1x lines each, three at a time, each line with its own home, XDG directories and display range |
| Blender interoperability | `blender` | the full-tier job |
| Arch package acceptance | `package` | the full-tier job |
| any other full-tier job | `job:<id>` | that job (and the development build it consumes) |
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
| `src/core/**`, `src/geometry/**`, `src/sketchyup/**`, `src/solar/**`, `third_party/**` | build, native, `slow-core` |
| other `src/**`, `resources/**`, `examples/**`, `scripts/**`, `.github/**`, `CMakeLists.txt`, `CMakePresets.json` | build, native |
| terrain sources and tests | adds the slow terrain suites |
| assistant and provider sources, corpus and scorer | adds the provider release corpus |
| `tests/**` | build; the sources of the executables the X11 1x lines run, `tests/*.hpp` and `tests/fixtures/**` add native |
| a slow test's own sources (derived from CMakeLists.txt) | add that slow group |
| `src/integrations/**`, the exporter inputs the Blender verifiers check (sections, textures, materials, smoothing, solar), the Blender and glTF verifiers, the export fixtures | adds Blender interoperability |
| `packaging/**`, package and release scripts, the desktop entry, MIME, icon and bundled assemblies | Arch package acceptance; `packaging/arch/mirrorlist` (it decides every package) selects all |
| harnesses most full-tier jobs share: `scripts/test-wayland.sh`, `scripts/ci-pack-build.sh` | `full`: the run becomes the full tier |
| harnesses one full-tier job runs: the accessibility bridge and `scripts/test-keyboard-layout.sh` (audit), `scripts/verify-m5-offline.sh` (fractional), `scripts/ci-wayland-client.sh` with its patch and probe (sanitize) | `job:<id>`: that full-tier job runs in the fast tier, with the development build when it needs one |
| anything no rule covers | build, native and every slow suite (and `ci-plan.py check` fails until a rule covers it) |

The slow suites (at least about 15 seconds each at `-O0`) carry 560 of the 765 seconds
of the serial CTest run: `slow-terrain` (terrain_history, terrain_stamp,
terrain_drape, terrain_sculpt, terrain_source_edit, terrain_replay, terrain_edit,
terrain_edit_input), `slow-core` (incremental_validation, document_limits,
variant_edit_cost, component_parameters) and `slow-assistant`
(provider_release_corpus). Every other CTest test runs on every code change.

`ci-plan.py check` keeps the map honest on every run: every tracked file matches a rule;
every slow test exists in CMakeLists.txt, has source files and its group is selected by
some rule; the examples, taken from merged PRs, give their recorded areas; and every
document CMakeLists.txt installs or `scripts/package-source.sh` copies exists (a renamed
document would otherwise fail only in package acceptance). The fast tier's `ctest` calls
use `--no-tests=error`, so a renamed test cannot pass by selecting nothing.

Edits to `native.yml` itself stay in the fast tier: the fast X11 job runs the same X11 1x
lines as the full tier, so a new native fixture runs on its PR at 1x on X11; its other
platforms and scales run nightly. Adding a label does not start a run (the workflow does
not listen to `labeled`, so that other labels cannot restart or cancel runs); add
`full-ci`, then push or re-run.

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
  the change is **wide** and twelve shards compile first. Each shard lists the cache files
  before and after its build and uploads only the new ones; ccache is
  content-addressed, so merging entries can only add hits, never change an object.
  Otherwise the change is **narrow** and the test jobs compile their few misses
  themselves.
- **No build-tree transfer**: each fast job builds from the cache instead of packing and
  downloading the 37 GB tree.
- **Tests spread over runners, not threads.** A 4-vCPU runner has two physical cores: under
  `ctest --parallel 4` every test ran two to three times slower (terrain_history 95 s
  serially, 312 s at `--parallel 4`; the whole suite 765 s serially, 590 s in parallel).
  CTest therefore runs in five jobs at `--parallel 2`, balanced by known test times, and
  the X11 lines in two jobs, three at a time.

A prebuilt CI image on GHCR was considered: package installation from the existing
pacman cache takes about 45 seconds per job (the compile shards install only the
compile packages, about 10 seconds), and pulling a 2–3 GB image would recover only part
of that while adding an image-publishing pipeline. It is left for later (see
Measurements). A static `ci-fast` build was also measured: the same test times, a
9.7 GB tree and twice the link time, so the shared libraries stay.

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

Measured on GitHub (pull-request runs into the PR #397 branch, whose cache stood in for
`main`'s; wall time from push to the CI verdict):

| Change | Run | Plan | Wall | Runner time |
| --- | --- | --- | --- | --- |
| Before: any change (60 runs, 2026-10-19…21) | — | all ten jobs | 41–45 min median, about 2 h worst | 143 min per run on average (228 runs, 545 runner-hours, 2026-10-14…21) |
| Docs only (`docs/USER_GUIDE.md`) | [39707528249](https://github.com/zygote55/sketchyup/actions/runs/39707528249) | no build | **16 s** | 0.2 min |
| Small `src/app` change (one `.cpp`) | [39707529595](https://github.com/zygote55/sketchyup/actions/runs/39707529595) | narrow; build, native | **3 min 52 s** | 20 min |
| Core geometry header (`src/geometry/planar.hpp`, included by 580 of 837 objects) | [39712817330](https://github.com/zygote55/sketchyup/actions/runs/39712817330) | wide; build, native, slow-core | **7 min 51 s** | 51 min |
| Narrow change selecting every slow suite (dispatch fallback) | [39704346633](https://github.com/zygote55/sketchyup/actions/runs/39704346633) | narrow; everything fast | 4 min 52 s | 27 min |
| Full tier | [39707993131](https://github.com/zygote55/sketchyup/actions/runs/39707993131) (dispatch, `tier=full`) | full | 70 min 35 s, all green (sanitize is the critical path) | 197 min |

Wall time drops about 10× for documentation and narrow changes and about 5.5× for a core
header change. Runner time drops less, about 2.5× per run: applying the map to the 80
pull requests merged before this change gives about 55 runner-minutes per run (wide
compiles 51, narrow 20, docs nothing; the five that changed a shared harness run the
full tier, about 230), plus the nightly full run (about 27 runner-hours a week), against
545 runner-hours a week before.

What still costs most, and the next levers, in order:

- **Header fan-out.** `src/core/model.hpp` pulls in 36 record headers, so 565–667 of the
  837 objects include every core header and almost every feature PR recompiles about 570
  objects (about 20 runner-minutes of compiling, spread over the shards). Narrowing what
  `model.hpp` includes is the largest remaining saving in both wall and runner time.
- **Per-job setup.** Container start and package installation take about a minute in each
  of the 21 jobs of a wide run. A prebuilt CI image would save part of it; fewer compile
  shards would trade wall time for runner time.

Follow-ups (2026-10-22):

- **Header fan-out, first pass** (PR #404). `core/body.hpp` no longer includes the terrain
  records: `Body::terrain` is an `IndirectOptional<TerrainRecord>` (std::optional's value
  semantics, heap-held, so the header needs only a declaration), and the site origin, tile
  key, terrain enumerations, component parameter aliases and assembly placement types moved
  to small headers. `app/viewport.hpp` no longer includes the terrain display, edit-mesh and
  preview-worker headers or the component library. Objects that recompile when a header
  changes (of 757 in `all`): terrain records 530 → 73, terrain edit 221 → 52, terrain
  operations 234 → 68, terrain commands 207 → 37, terrain import 210 → 40, component
  parameter records 524 → 225. `core/model.hpp` and `core/body.hpp` themselves stay at
  about 520. Over the 48 of the last 80 PRs that changed a `src` header, the median
  recompile falls from 221 to 171 objects and the mean from 273 to 211.
- **Prebuilt CI image: measured, rejected.** A 6.4 GB image of the pinned base plus every
  CI package, pulled from this repository's GHCR, took 1 min 46 s to 1 min 48 s in
  "Initialize containers" on each of three runners, against about 65 s for today's base
  image plus the cached package installation (about 25 s in the compile shards).
- **Slow suites.** The terrain suites dominate CTest; the follow-up [PR #403](https://github.com/zygote55/sketchyup/pull/403) trims duplicated work in them.
- **A Qt 6.8 compile job (implemented).** Every other CI job builds on the pinned Qt 6.11.2, so a
  source that compiles only through Qt 6.11's transitive includes reaches a release candidate:
  the 1.3.0 RC2 did not build on Qt 6.8.0, the declared minimum, because a test file lacked
  `#include <QJsonDocument>` ([D-047](../verification/R089a-defect-triage.md), found by the RC's
  Qt 6.8 reference build). The full-tier job **Qt 6.8 reference compile** (`qt68`) builds
  every target (`all` plus the six `EXCLUDE_FROM_ALL` benchmark and trial targets; not the
  Clang fuzzers, which are off at configure) with `ninja -k 0` (so one run lists every break) on the Qt 6.8 reference snapshot
  (`archlinux:base-devel-20241013.0.269705`, 2024-10-15 archive, GCC 14.2.1, Qt 6.8.0), with the
  `ci-fast` preset, and runs no tests. It has its own package and compiler caches. Package
  signatures are not checked (`SigLevel = Never`): the 2024 packager keys have expired, and the
  archive is fetched over HTTPS by date. CI verdict requires it whenever the full tier runs.

- **IFC library and helper (R097.a, [ADR 0171](0171-ifc-exchange-subset.md) section 1.5).** The
  vendored IfcOpenShell tree is built by its own job, not by `ci-fast`: area `job:ifc` (a changed
  path under `third_party/ifcopenshell/`, `src/ifc/`, `tests/ifc*`, `tests/fixtures/ifc/`, the two
  IFC scripts, `dependencies.json` or `CMakePresets.json`) runs `IFC library and helper`, and only such a path
  selects it (the vendored tree alone selects nothing else; `src`, `scripts` and `tests` paths still select
  `build` and `native` through their generic rules), so an ordinary pull request plans, builds and tests
  exactly what it did before. The job installs `CI_IFC_PACKAGES` (it adds
  `opencascade`, which pulls VTK, Tcl and Tk, 0.55 GiB, so it has its own package-cache entry and no
  other job's key or install changed), restores the prefix `build/ifc-prefix` from a cache keyed by the
  image's packages, the mirror, the vendored tree and `src/ifc/IfcOpenShellPrefix.cmake`, builds the helper
  (seconds) and runs `ctest -R '^ifc_'`: the API probe and checks L1 to L6 of ADR 0171 on a scratch
  install. A miss builds the prefix from source (13 minutes with four jobs on four CPUs in the pinned
  image; see the R097.a notes) and saves it. The full tier skips the cache restore, builds the prefix from source,
  saves it when run on `main` (the nightly run keeps the entry warm), and runs the helper again under ASan
  and UBSan. `third_party/**` no longer selects `build`, `native` and `slow-core` for the IFC tree (only the
  libraries the application links do), and its headers, sources and CMake files do not make a later plan
  wide (`NOT_IN_FAST_BUILD` in `scripts/ci-plan.py`). Arch package acceptance installs `boost`, `eigen`,
  `opencascade` and `patchelf` on top of `CI_PACKAGES` (the PKGBUILD's makedepends) and builds
  IfcOpenShell once: the preceding package, which only tests install and upgrade, skips it.

## Test cleanup

Coverage-preserving trims of the slowest suites, each keeping its assertion:

- `terrain_history_tests` `explode`: the over-limit refusal uses a 235 x 235 grid (110,450
  faces) instead of 353 x 353; the 250,000-triangle create/regenerate/worst-case cases
  (ADR 0168) are unchanged.
- `incremental_validation_tests`: the full-validation oracle is off for the ballast setup
  in every build (was sanitized only) and for the 59 setup placements of the 600,000-vertex
  boundary; the placement that reaches the limit and every later edit still run under it.
- `terrain_stamp_tests`: `hostile` runs the comb stamp once (refusal, no change and timing
  together); `triangleLimit` takes its report from the one refusal that is checked.
- `terrain_drape_tests` `triangleLimit`: one refusal run serves the no-change check and the
  located-report assertions.
- `terrain_sculpt_tests`: the 19,999-corner comb is refused once, not twice; the brute-force
  star check uses 600 probes instead of 1,500 (about 225 inside, against the `> 150` bound).
- `provider_release_corpus`: the 24 replayed `provider_trial` runs execute on four threads
  (independent report paths, retained files and journals); results are checked serially in
  task order and the run count is unchanged.

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
  correctness. The first run on `main` after this change has no cache and compiles
  everything in the shards (about 10 minutes).
- Many checks appear as "skipped" on a pull request; only **CI verdict** decides.
