# cgltf

Vendored from https://github.com/jkuhlmann/cgltf at commit
`85cd62382dfea638278962690cf515023f33ed00` (2026-02-02).
The unmodified header and original MIT license are retained. `cgltf.cpp` is the
local compilation wrapper. Builds use this local copy and never fetch code.
SketchyUp supplies bounded input bytes and its own restricted sidecar loader;
cgltf's unrestricted filesystem loading helpers are not used.
