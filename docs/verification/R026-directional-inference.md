# R026: directional inference and locks

Date: 2026-10-03. Local verification and both CI jobs passed.
PR #26 merged as `071d145`.

World-space constraints carry an origin, unit direction, kind and source IDs.
Projection solves the screen-space infinite line with perspective-correct world
coordinates. Coincident directions break sub-millionth-pixel ties by kind and
stable source identity, avoiding camera-dependent ordering from floating-point
roundoff. Automatic acquisition uses eight logical pixels; modeling tolerance
remains 1e-7 m. World axes and reference directions must lie in the construction
plane. Explicit Line axis locks can leave it; their polyline command derives a
containing plane. Continued chains translate the plane along its normal while
preserving its grid origin within the plane.

Hovering a canonical point/edge for 450 ms arms a reference. Parallel and
perpendicular directions use world-transformed edges. Analytic curve tangents
are computed at an on-curve anchor or from an external anchor, with arc sweep
filtering; interior anchors supply no tangent. Affine/mirrored contexts preserve
analytic tangency. The editable curve still consists of chords; an analytic
tangent is not a claim of smooth editable topology or a general curve boolean.
Vertex references consider the first 64 incident edges in stable ID order;
hover a specific edge to disambiguate high-valence junctions. Curve associations
are evaluated once per reference, then cached until the operation anchor changes.

Tab cycles point and direction alternatives. Canonical points/edges take priority
over directions; directions take priority over generic face hits. Only an explicit
Tab choice persists across same-position queries: an automatic axis must yield to
a newly available canonical endpoint when background preparation completes. Shift holds the
selected direction or point, or the hovered face's construction plane. Right,
Left and Up toggle world X/Y/Z. Down cycles reference parallel, perpendicular and
off. Camera navigation preserves world lock state. Repeated keydown events do not
toggle locks; Shift release/focus loss clears temporary holds. Explicit arrow
locks survive focus transfer to Measurements. Canonical points lying on the
locked line remain eligible for acquisition around the constrained pointer;
off-line candidates cannot override a lock, including when Line leaves its
initial construction plane. Commit, cancel, tool change,
document change and window deactivation clear constraints and references.

Lengths intersect the locked line with a sphere about the operation anchor and
choose the solution closest to the preview. Unreachable lengths and contradictory
coordinates reject without mutation. This also supports offset reference lines.
End-on direction views report the need to orbit or enter a length. Planar shapes
reject incompatible axes. Reference bands, source highlights, point markers,
text labels and a drawn lock glyph expose acquisition without relying on color.
The viewport lists the applicable Shift/arrow/Tab/hover controls; A activates Arc.

`geometry.infer` optionally accepts `anchor`, `reference: {body, edge}` and
`fromPoint`, returning a `directions` array with constraint origin/direction,
source IDs, projected point and pixel distance. It uses the same core solver and
never changes document state. See `examples/directional-inference-query.json`.
No persistence schema change is needed for transient inference state.

Continuous directional coordinates replace the old grid-only result when a
constraint is acquired. Pointer replay must allow the background inference index
to become ready before expecting canonical endpoint closure. The drawing replay
now waits for that state and measures pointer-created area within integer-pixel
resolution. Pointer curve fixtures compare their projected points within the
integer input resolution; typed dimensions, bulges, axis angles and geometric
tolerance tests remain exact. During
index preparation the existing grid/axis fallback remains available, with a
visible preparation hint, and does not promise canonical endpoint acquisition.

Validation:

- All 19 development suites and 14 ASan/UBSan suites pass. The constraints
  suite was rerun in both builds after the final stable-ranking adjustment.
- Core fixtures cover logical-pixel projection, perspective correction, tilted
  planes, invalid cameras, numeric reachability, persistent/temporary lock state,
  parallel/perpendicular references, external/on-curve/interior tangent cases,
  mirrored nonuniform transforms, arc sweep filtering and coincident ranking.
- Native constraint replay passes on Wayland at device scale 1.6 and pinned
  Arch/Xvfb at scales 1 and 2. It covers three zoom levels, actual middle-drag
  orbit, Shift/focus loss, arrow repeats/toggles, reference arming, Tab alternatives,
  tangent acquisition, numeric rejection/undo, elevated face-plane hold, Z-axis
  endpoint snapping and document-change invalidation.
- Point-inference, drawing, curve, numeric-entry and lifecycle regressions pass
  on Wayland. Drawing and curve replay also pass in pinned Xvfb. The viewport
  pixel/depth/transparency/clipping/context-recreation suite passes in Xvfb.
- The CLI directional query recipe returns the red-axis point `[4,0,0]` with a
  three-logical-pixel distance and leaves the empty document at revision zero.
- The [reviewed native capture](R026-lock.png) shows the armed reference, dotted
  reference band, bold locked line, lock glyph, label and modifier hints.

Two initial local build steps exited with signal 9 while other host workloads
were active. Serial retries passed; the process cgroup reported no OOM kills,
so the cause is not established. This did not require a source workaround.

```sh
ctest --preset dev
ctest --preset sanitize
QT_QPA_PLATFORM=wayland build/dev/constraint_input_tests
build/dev/sketchyup-cli --query-file examples/directional-inference-query.json
```

