# Bounded binary and ASCII STL export

R076.c, 2026-10-07. Export flattens every world-space surface into triangles,
including hidden objects, without section clipping. Nested/nonuniform transforms
are applied and reflected triangle winding is corrected. Native names never
become STL directives. Facet normals are derived from final represented geometry.
Holes and concavity use native triangulation; wires and reference images are
omitted and counted. Reports explicitly disclose loss of hierarchy, instances,
materials, textures, annotations and parametric metadata. No print-readiness or
closed-solid guarantee is made and export does not repair geometry.

Units and Y/Z up axis are explicit options, external to STL. Binary output uses an
80-byte generated header, little-endian count and 50-byte facets with zero attribute
words. ASCII uses 17 significant digits and generated solid names. Both forms
are bounded to 100,000 facets, 64 MiB output and a 256 MiB native snapshot.
Nonfinite, collapsed and sub-tolerance world triangles reject. Converted binary32
coordinates must stay within one micrometre Euclidean distance of the original;
the maximum error is reported. Rounding may not collapse or reverse triangles.
ASCII is available when binary precision is insufficient. Export leaves document
bytes and history unchanged.

`publishNewFile` shares the native migration publisher: write and fsync a temporary
file in the canonical existing parent, atomically hard-link to a new destination,
then fsync the directory. Existing files, directories, symlinks and racing writers
are never replaced. A post-publication sync failure retains the complete output
and reports uncertain durability. The report's hash and byte count are verified
before publication. The report is returned separately; STL itself does not carry
units or provenance. Native migration regression tests cover the shared publisher.

Fixtures verify both encodings and axes, millimetres, reflected/nonuniform placement,
closed-box volume, hidden geometry, holed-face area, ASCII precision fallback,
source/history preservation and existing/dangling output protections. Independent
Blender import verifies both encodings, extents, manifold edges and signed volume.
