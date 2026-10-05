Manifold 3.4.1, upstream commit `31afd71d17c7a94cfaeada83f657d7b42628ead6`.
Source: https://github.com/elalish/manifold/tree/v3.4.1
Unmodified src/ and include/ trees. Apache License 2.0; see LICENSE and AUTHORS.
Upstream has no NOTICE file. CMakeLists.upstream.txt retains its source list.
Our CMakeLists.txt builds the serial static core offline, without cross-section,
bindings, TBB, downloads, or upstream install rules. It preserves upstream's
floating-point contraction/excess-precision flags. Native SketchyUp surfaces
remain authoritative; Manifold is used only by the solid Boolean adapter.
