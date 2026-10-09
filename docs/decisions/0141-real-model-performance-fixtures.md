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
