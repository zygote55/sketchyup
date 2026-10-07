# Real-model performance fixtures

R082.a, 2026-10-07. This benchmark is an overlapping M9 fixture/profiling spike,
not a release acceptance result. `real_model_benchmark [instance-count]` builds a
real document through public component creation/placement and canonical persistence
validation. Each 26-sided prism contributes exactly 100 visible triangles; repeated
placements use a 4 m grid. A fixed document identity and container hash identify the
fixture. Default count is 1,000; the accepted argument range is 1..10,000. Requested
counts are never silently reduced when public editing limits reject them.

An optimized build renders the ordinary textured/edge style in the actual native
Viewport. The benchmark requires a 1920 × 1080 framebuffer before warmup and every
sample. A fixed child widget and bounded scale-settling wait handle compositor tiling
and late fractional scaling; the report discloses host dimensions and clipping.
OpenGL rendering targets the complete child framebuffer even if its host clips the
visible window. Timings cover the scene through glFinish, excluding painter overlays
and compositor presentation. They are not end-to-end input/display latency.

Ten warmup frames precede fifty measured frames and real geometry picks. One hundred
instance transforms include refresh and framebuffer readback; one hundred undo and
redo operations verify retained history allocation. Stationary frames must not
rebuild meshes or upload geometry. Repeated edits must retain fixed body-cache
capacity, and every pick must hit geometry. Reports include fixture construction,
peak process RSS, history, selected body-vector capacities and GPU vertex payload.
The cache figures exclude maps, textures, overlays, driver and allocator overhead;
they must not be described as total cache or GPU memory. Timing is opt-in and off in
the application. This executable is a manual benchmark, not a variable-host CI gate.

R082 remains open: the 10,000-instance fixture must fit public document limits;
full frame feedback, inference, cold/warm native persistence, unique geometry,
textures, deep nesting, distant coordinates and long sessions need separate
measurements. Reference integrated/discrete reports and frozen memory budgets remain
acceptance requirements. Exploratory timings under concurrent compilation do not
freeze a reference budget or establish release performance.

## Native file-size fixture

R082.c adds the opt-in, default-build-excluded `native_persistence_benchmark`.
`--prepare NEW_FILE` produces a fixed-identity 100–101 decimal MB container with
1,000 square faces, bounded metadata and four 16 MiB opaque local assets.
`--measure INPUT NEW_FILE warm|advised-cold` times synchronous core load/validation
and durable save in a fresh process and verifies byte-exact native round-trip.
It refuses existing outputs. The deterministic fixture hash and exact byte count
must accompany every comparison; no document limits are raised to fit it.

Warm reads pre-read the full file. Advised-cold requests only per-file Linux
`POSIX_FADV_DONTNEED`; success is not proof of cache eviction. Global caches and
host settings are untouched. Fixture creation and final hash checks are outside
the measured interval. This metadata/asset stress fixture does not cover image
loading, complex geometry, asynchronous UI responsiveness or end-to-end rendering.
No variable-host timings become CTest gates or release acceptance automatically.

## Additional scene scenarios

R082.e extends the manual executable to
`real_model_benchmark [placement-count] [repeated|unique|deep|far|textures]`. Omitting the
scenario retains the original repeated fixture, allocation/revision order and
canonical bytes. The repeated control must retain its frozen hash before new
scenarios are measured. No accepted instance count, public document limit or
performance budget changes.

All scenarios retain exactly 100 triangles per placement, the 1920 × 1080
framebuffer, ten warmups, fifty frame/pick samples, and one hundred edit/undo/redo
operations. The new scenarios are explicit version-2 fixture families:

- `unique`: independent authoritative 26-sided prisms whose radius is
  `1 + 0.2 * placementIndex / placementCount`; there are no component bindings.
- `deep`: the original repeated placements enclosed by 32 ordinary groups.
- `far`: the original repeated placements enclosed by one group translated by
  `(900000, -900000, 900000)` metres, still within existing coordinate limits.

Picking probes use each placement's actual world transform. Reports distinguish
placement count from actual component-instance count, and include scenario,
outer-group count and coordinate offset. Canonical hashes are retained per
scenario and count; consecutive full runs must agree. Initial qualification is
one unchanged repeated control followed by a 25-placement smoke and three
1,000-placement runs per added scenario. This does not replace or satisfy the
separate unsupported 10,000-instance requirement. These remain exploratory
scene/pick/edit measurements; inference timing, large textures, complete frame
feedback and full memory accounting remain separate work.

## Large textures

R082.f adds `textures`: the original repeated geometry with two deterministic
4096 × 4096 RGBA PNGs assigned to front/back materials before component capture.
Their combined 128 MiB decoded input fits the existing cache budget exactly.
The fixture waits up to 15 seconds from window show for decoding, consumes the
published generation, and rejects any texture fallback before sampling and at
completion. Reports retain input dimensions/bytes and show-to-ready duration;
these are not total cache or observed GPU-memory measurements. Qualification
uses an unchanged repeated control, a 25-instance smoke and three 1,000-instance
runs. This scenario does not change the separate 10,000-instance requirement.
