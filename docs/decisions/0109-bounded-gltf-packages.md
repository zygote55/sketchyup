# Bounded glTF 2.0 package capture

R074.a, 2026-10-07. `GltfPackage` captures GLB or glTF JSON plus every declared
buffer and image before conversion. It owns the parsed structure and immutable
bytes; deleting or changing the original sidecars cannot change a captured import.
No parser filesystem callback or network loader is enabled.

The parser is [cgltf](https://github.com/jkuhlmann/cgltf), pinned to commit
`85cd62382dfea638278962690cf515023f33ed00`, with its original MIT license and
unmodified header. The repository builds its small implementation wrapper locally;
there is no build-time download. The format reference is the
[Khronos glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).

Source files and aggregate buffers are limited to 128 MiB, JSON to 16 MiB, and parser
allocations to 128 MiB. Images are limited to 16 MiB each and 64 MiB in aggregate.
At most 10,000 nodes/meshes/primitives, 1,024 images/materials, 64 buffers, 65,536
accessors/views and 256 scenes are accepted. Accessors have at most one million
elements individually and two million collectively. Primitive validation has a
one-million-element work budget; JSON has a one-million-value budget.

GLB length, aligned JSON/BIN chunks and the absence of trailing chunks are checked.
Buffers must match their declared lengths (with only the permitted GLB padding).
Checked range arithmetic precedes parser validation: views, component alignment,
strides, counts and sparse indices must fit captured bytes. Sparse indices must be
strictly increasing, unique and in range. Hierarchies are acyclic and depth limited.

Sidecars use relative UTF-8 paths contained in the canonical source folder.
Traversal, absolute paths, URL schemes, queries, fragments, malformed percent
escapes and symlinks escaping that folder are rejected. Canonical base64 data URIs
accept only buffer bytes or PNG/JPEG images. Embedded images need a supported MIME
type. Image decoding and mesh representability are later conversion gates.

Unknown required extensions reject the package. `KHR_texture_transform` is the
only supported required extension; the mesh converter must apply it. Optional
extensions are reported by conversion when their base fallback is used. This
reader deliberately accepts a smaller subset than the full glTF specification;
for example, unknown GLB chunks and non-PNG/JPEG image formats reject explicitly.

The capture report includes source/buffer/image SHA-256 hashes and byte counts,
sidecar count, and peak parser allocation. It does not disclose absolute paths.
Tests cover immutable sidecars, base64, path confinement, malformed headers/ranges,
sparse accessors, cycles, unknown required extensions and native-exported GLB.
