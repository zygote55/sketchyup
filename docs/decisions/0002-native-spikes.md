# ADR 0002: native viewport and surface experiments

Date: 2026-10-03. Roadmap: R002.a, R003.a, R004.a. Status: experimental
implementation selected; parent M0 acceptance remains open.

## Desktop and renderer

Use C++20, Qt Widgets and QOpenGLWidget for the first native spike. Qt Quick 3D
is not installed on the development host and its module is GPL/commercial,
whereas the required Qt base modules offer LGPL licensing. Qt Widgets also
provides standard menus, text inputs, accessibility metadata and native dialogs.
Application code remains MIT; dynamically linked Qt retains its own terms.
Sources: [Qt licensing](https://doc.qt.io/qt-6/licensing.html),
[Quick 3D license](https://doc.qt.io/qt-6/qtquick3d-index.html#licenses-and-attributions),
[QOpenGLWidget](https://doc.qt.io/qt-6/qopenglwidget.html).

The renderer consumes triangulated snapshots and never owns document geometry.
An OpenGL 3.3 core shader draws depth-tested faces and edges; polygon offset
prevents coincident edge flicker. Picking uses a world-space ray derived from
logical widget coordinates. No framebuffer-pixel coordinates enter tool input.
Camera changes do not increment document revision. Draw previews are hatched
and discarded on Escape. Native Wayland and X11 interaction results are in
[the evidence record](../verification/native-spike.md).

This is an adapter feasibility result, not the final rendering architecture.
The initial implementation rebuilt all meshes and uploaded them per draw. The benchmark tests
repeated-triangle instancing, not million-triangle editing, selection acceleration
or incremental caches. [R002.b](../verification/R002-viewport.md) adds persistent buffers, pixel-tested
transparency/clipping, context recreation and controlled transitions across both
physical displays. Full materials/section workflows, manual monitor dragging,
native dialog acceptance and screen-reader behavior remain open.
The UI currently uses a light palette. System/dark themes remain R006/R010 work.

## Surface representation and alternatives

Choose explicit double-precision vertices, ordered face loops with holes, and
loose wire records. Faces and vertices have stable local IDs; bodies define
isolated editing contexts. Undirected edge adjacency is derived from loops and
can hold zero, one, two or more incident faces. Stable edge IDs and persistent
split/merge maps still need R003.b/R015; derived endpoint pairs are not presented
as the final edge identity scheme.

| Candidate | Evidence | Decision |
| --- | --- | --- |
| Manifold triangle kernel as authoritative document | Installed Manifold header specifies paired halfedges for manifold meshes; our corpus includes loose edges and three faces sharing an edge | Exclude as the sole document representation; retain as a later Boolean adapter candidate |
| Manifold-only half-edge graph | Cannot represent the three-incident-face fixture without special extensions | Prefer loop records with unrestricted radial adjacency |
| Explicit loop/radial records + replaceable planar adapter | Core tests preserve wire/open/non-manifold structures, arbitrary planes, holes, positive/negative extrusion and rejected outlines | Use for the spike; add persistent edge IDs before accepting final topology |
| CGAL/Open CASCADE | Not installed or executed in this experiment | No performance, license or robustness claims; reconsider for arrangements/Booleans at the corresponding gates |

Clipper2 2.0.1 at commit `21ebba05db8894f0c7217ad35ea518080f324946`
is vendored unmodified under Boost Software License 1.0. The host's static
library exposes headers but omits triangulation symbols, so the pinned source
avoids that mismatch. Its triangulation is explicitly marked beta upstream.
All use is behind `Surface::triangulate`; invalid results and area mismatches
are rejected. Do not infer arbitrary intersection or Boolean support.

Project face coordinates onto a local basis and quantize to 10^-7 m for the
planar adapter. Reject non-finite input, positions outside ±10^6 m, repeated
vertices, invalid/nested holes and non-planarity beyond 10^-7 m. This is a bounded
spike precision policy, not a proven site-scale tolerance system. Furniture/site
stress fixtures and near-degenerate arrangements remain required before M2.
Triangles are derived; the native file stores loops and original double values.

## Document and history

Bodies and surface records allocate monotonic IDs. Undo never rewinds the
allocator; IDs created and deleted within a committed batch remain retired.
Unchanged body records are shared immutable values. History stores changed-body
before/after values, not full-scene copies. An estimated 64 MiB history budget
bounds retained changes; exact allocator accounting remains to be measured.
A single changed body is still copied, so large-mesh edits need granular deltas
before the performance gate.

Every edit validates staging, freezes caller-owned records, checks revision and
preconditions, and commits once. Undo/redo increment content revision. A local
batch uses the same operations as the UI and produces one undo item. Body IDs,
face IDs, vertex IDs and document identity survive save/load. Revision/history
are session-local in this spike. It is not the durable M5 transaction protocol.

The proposed v1 JSON file is deliberately experimental: explicit format/version,
meters, Z-up, document identity, allocators, face loops, wires, names and colors.
IDs are canonical decimal strings, avoiding JSON integer precision loss.
The initial file limit is 32 MiB; document limits are 10,000 bodies and 100,000
each of vertices, faces and wires. Both mutation and loading enforce these limits.
Future versions/coordinate systems fail safely. No scripts or asset extraction.

QSaveFile writes a sibling temporary file and atomically replaces the target;
direct-write fallback is disabled. Dirty state changes only after commit.
A forced short write verifies preservation of the prior file. This does not
claim directory-fsync/power-loss durability, autosave, recovery, assets or format
migration. Container/chunk benchmarks, journal design and durable outcome
retention are R004.b/R012/R038/R041 prerequisites, not finished by this spike.
