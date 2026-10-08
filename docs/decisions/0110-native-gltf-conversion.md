# Native glTF mesh conversion and explicit losses

R074.b, 2026-10-07. Conversion consumes an immutable [bounded package](0109-bounded-gltf-packages.md)
and constructs one unsaved, undoable native edit. Native serialization and decoding
validate the complete result before it is presented. Source files are never written.

The default scene is selected, falling back to the first scene; with no scenes,
parentless nodes are roots. Additional scenes are reported as omitted. Nodes become
groups with local transforms and preserved names. Each shared mesh becomes one
component definition, with a separate instance under each referencing node. Mesh
children stay outside component ownership. Reflection and nonuniform scaling stay
in placement transforms. Singular transforms reject. Hierarchy depth is at most 64.
The combined canonical and instantiated conversion budget is 100,000 vertices,
100,000 faces and 10,000 records; native world-coordinate and linked-record limits
also apply. This conservative limit can reject a file within the parser's bounds.

Metres remain metres. The proper rotation C(x,y,z)=(x,-z,y) maps Y-up glTF to native
Z-up without changing triangle winding. Triangle lists, strips and fans become
native triangle faces; degenerates are counted and omitted. Indexed vertex seams
remain distinct. Where normals exist, shared edges are smoothed, but native normals
are recomputed and explicitly reported as an approximation. Point/line primitives
are omitted with counts; a selected scene without supported triangles rejects.
Skinning, skin attributes, morph targets, compressed geometry and unstable transforms
reject explicitly instead of presenting an incorrectly deformed mesh.

Base color factors convert linear RGB to native sRGB swatches. BLEND factors and
image alpha are retained; OPAQUE images have alpha removed in a managed PNG copy
when necessary. MASK rejects because native filtering cannot reproduce its cutoff.
PNG/JPEG images become immutable managed assets. Each triangle's UVs become an
affine pinned mapping with the same top-left origin and downward V direction.
`KHR_texture_transform` offset, rotation, scale and coordinate-set override apply
before pinning. Texture addressing must repeat; clamp/mirror reject. Nearest
filtering becomes bilinear with an explicit notice. Existing image decode and
asset byte limits apply to original and derived payloads. Native affine mappings
require finite, independent, nonzero UV gradients; collapsed or numerically unstable
texture mappings reject explicitly. Repeated material names receive unique suffixes
and a conversion notice, since native material names must be unique.

Perspective and orthographic cameras become ordered named scene views. Eye and
view direction come from the complete world transform. Perspective vertical FOV
must fit the native 5–120 degree range; orthographic vertical extent becomes native
orbit distance. Native camera roll, aspect ratio and clipping differences are
reported. Camera-only files are outside this mesh-import workflow.

The version-1 report records node/triangle/definition/instance/material/image/camera
counts, package hashes, `losses` counts, human-readable `notices`, and omitted optional
extension names. Counts refer to each converted canonical primitive/material or
selected node, not every expanded copy. Losses include custom normals, tangents,
vertex colors, back-face culling, metallic/roughness, normal/occlusion/emission maps,
unlit shading, punctual lights, animation tracks, additional scenes and optional
extension fallbacks. Unknown required extensions reject at package capture.
No animation, arbitrary PBR shading or custom-normal fidelity is claimed.

Tests independently check metre coordinates, transformed mirrored instances,
shared definitions, UV transforms, transparency, camera pose, strip winding,
expanded geometry budgets, native round trips and undo/redo with monotonic revision.
An actual Blender export supplies a separate hierarchy, mirrored mesh, managed
image, UVs and camera for cross-application acceptance. The native GLB exporter is
also exercised as a producer. Conversion does not reconstruct lost native topology,
constraints, annotations, sections or component semantics from previous exports.
