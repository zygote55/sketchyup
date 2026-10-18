# CDT

Vendored from https://github.com/artem-ogre/CDT, tag `2.0.0`, commit
`c888b30d83312114d3ff332855a97fd95ce95f17`. The source archive
`https://github.com/artem-ogre/CDT/archive/refs/tags/2.0.0.tar.gz` has SHA-256
`81ed0c7a8cbdc059b26948a160c3ce68c7a147976334093fe3175c8fa161a447`.

`include/CDT/` holds the eleven headers of upstream `CDT/include`, byte for byte, with
their MPL-2.0 file notices intact. Nothing else is vendored: not the compiled-library
sources, the tests, `extras`, the documentation or the visualizer. Builds use this local
copy in header-only mode (`CDT_USE_AS_COMPILED_LIBRARY` is never defined) and never fetch
code. Only `src/geometry/terrain_triangulation.cpp` includes it
([ADR 0168](../../docs/decisions/0168-terrain-import-and-surfaces.md) section 3).

Licences:

- `LICENSE` is upstream's `CDT/LICENSE`, the Mozilla Public License 2.0, which covers the
  library. MPL-2.0 is file-level copyleft: these files stay available under it, and any
  change to one of them would have to be published under it. SketchyUp does not modify them.
- `LICENSE-predicates.txt` is the BSD-3-Clause notice at the top of `include/CDT/predicates.h`
  (Lenthe's adaptive exact predicates), extracted verbatim so that it ships in binary
  distributions. Upstream has no separate file for it.
- `include/CDT/portable_nth_element.hpp` is copied from LLVM libc++ under Apache-2.0 with
  the LLVM exception; its notice stays in the file. The exception covers compiled object
  code, so no further notice ships.
