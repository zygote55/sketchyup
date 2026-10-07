# Contained OBJ export packages

R075.c, 2026-10-07. `exportObj(document, options)` converts a read-only document
snapshot to bounded OBJ/MTL and captured image bytes. Options explicitly specify
metres per output unit and Y-up or Z-up. Geometry uses world coordinates; all model
geometry is included, including hidden bodies. Section planes and editor visibility
do not clip exchange geometry. The report states this behavior.

Each geometry body becomes one generated `body-ID` object/group. Original names
remain data in the JSON manifest, never executable directives or sidecar paths.
Nested hierarchy and shared components flatten to independently editable world
geometry, with counted losses. Distinct native vertices at the same position keep
separate OBJ position indices. Normals use the inverse-transpose transform and
reflected geometry reverses winding. Native derived shading normals are written;
edge flags, native metadata and exact authored-normal provenance are not promised.

Simple concave polygons retain their ordered loops. Faces with holes or more than
256 corners triangulate with an explicit report. Derived triangle points must map
back to original face vertices within native tolerance; unsupported cases reject.
Loose wires remain `l` elements. Face UVs come from the explicit front mapping or
the native implicit metre projection. OBJ V is bottom-up. Back-face material or
mapping differences are reported and omitted. The material subset emits linear
Kd, d, and relative map_Kd; native diffuse colors convert from sRGB.

Supported managed PNG/JPEG bytes are copied intact to generated texture filenames.
Missing/unsupported images use color fallback with notices. No document name,
asset name or property controls filesystem paths. The exported OBJ references only
`materials.mtl`; that MTL references packaged textures. The manifest records units,
axis, source identity/revision, original object names, file hashes and explicit
losses for instances, hierarchy, annotations, reference images, scenes, sections,
text/curve metadata, guides, visibility, styles and lighting.

Limits: 256 MiB native snapshot, 100,000 positions and combined faces/wire segments,
500,000 corners, 300,000 UV/normal records, 1,024 materials/images, 64 MiB OBJ text,
4 MiB MTL and 16 MiB manifest. Images retain the native 16 MiB per-image/64 MiB
aggregate byte limits and 4096²/64 MiB per decoded image; aggregate decoded bytes
are bounded to 256 MiB. Native import also has group/used-state expansion limits;
a very large exported hierarchy can exceed those rather than claiming unrestricted
self-round-trip. OBJ does not carry native parametric metadata.

`writeObjExport` verifies every artifact against its manifest before writing into
a new directory with an existing parent. Existing directories, symlinks, unsafe
texture filenames or mismatched bytes reject. Package files use complete atomic
writes and the manifest publishes last; caught failures remove only newly created
package artifacts. A crash can leave an incomplete folder without a manifest;
consumers should accept only complete hash-verified packages. This is not a promise
of whole-directory crash durability.

Acceptance checks concavity, holes, wires, coincident identities, millimetres/Y-up,
nonuniform reflected transforms, normals, opacity, UVs, managed textures, relocation,
missing-image reports, source/history preservation and non-replacing publication.
An independent Blender 5.2.1 consumer verifies the generated concave reflected mesh,
world coordinates, normals, UVs, texture resolution and alpha. Native/CLI dialogs
and end-to-end acceptance remain the following R075 layer.
