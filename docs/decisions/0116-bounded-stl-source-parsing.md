# Bounded binary and ASCII STL parsing

R076.a, 2026-10-07. `parseStl(bytes, options)` is a pure parser. Units and Y-up/Z-up
are explicit; converted points must fit native metre-coordinate limits. It never
executes file content, resolves paths, welds vertices or repairs geometry.

Binary detection uses the exact file length and little-endian facet count, so a
binary header beginning with `solid` is handled correctly. The 80-byte header and
four-byte count precede 50-byte facets: twelve IEEE binary32 values for one normal
and three vertices, followed by a two-byte attribute word. Counts must agree with
all bytes; trailing/truncated data reject. Nonzero vendor attribute words are
retained and counted, not interpreted as portable colors.

ASCII input requires valid UTF-8 without NUL (optional BOM), bounded lines and the
complete solid/facet-normal/outer-loop/three-vertex/endloop/endfacet/endsolid
structure. Multiple solids are supported. Optional closing names must match their
opening name. Unknown tokens, wrong vertex counts, incomplete records, invalid
numbers and non-finite normals/coordinates reject. Zero normals, disagreement with
winding and degenerate facets are counted for the conversion layer's explicit
repair decision. Normal magnitudes are normalized safely, including subnormal ASCII
values; normals never determine or silently reverse facet winding.

Limits: 64 MiB input, 100,000 facets, one million ASCII lines, 4,096 characters per
line, 256 ASCII solids and 512 UTF-8 bytes per name. Finite units are 1e-6 through
1e6 metres per source unit. Source hashes, encoding, units, axes, solid/facet counts
and normal/attribute findings are returned without host paths. Empty triangle
collections reject as unsupported model input.

The binary length detection behavior can be compared with the independent
[Blender STL importer](https://github.com/blender/blender/blob/main/source/blender/io/stl/importer/stl_import.cc).
Its [binary reader](https://github.com/blender/blender/blob/main/source/blender/io/stl/importer/stl_import_binary_reader.cc)
provides an external interoperability reference; no Blender source is vendored or
copied into this implementation. Native conversion, explicit weld/repair options,
export and UI/CLI remain subsequent R076 work.
