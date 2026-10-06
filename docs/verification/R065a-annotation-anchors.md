# R065.a — Associative annotation anchors

2026-10-06, implementation `0824388`. [Contract](../decisions/0078-annotation-anchors.md).
This is the geometry-reference foundation; persistent annotations and native
controls are subsequent R065 layers.

All **118 CTest suites passed in 112.57 seconds**, including actual Blender.
The new anchor suite passed **ASan/UBSan with leak detection in 0.13 seconds**.
Both builds emitted no compiler warnings. Logs:
`build/r065a-complete-{build,ctest}.log` and
`build/r065a-sanitize-{build,ctest}.log`.

Tests cover fixed/vertex/edge/face references, unique split descendants, shared
split-boundary ambiguity, holes and off-plane rejection, barycentric deformation,
combined split/deformation, nested nonuniform reflection, last-world-position
retention after deletion, explicit broken states, restoration of original anchors,
site-coordinate stability, invalid IDs/weights/fields, duplicate lineage and
byte-exact support preservation through appearance-only edits.

The installed contract and desktop binary match their source/build inputs.
[Installed results](R065a-installed-smoke.json). Generated install prefixes were
moved from the temporary filesystem onto the project disk after `/tmp` hit its
quota; their old paths remain usable via symlinks. No user settings were changed.
All **117 install inputs** match the source archive byte-for-byte, with build/Git
metadata excluded. [Package results](R065a-source-package.json).

No native UI, automation command or stored file format changes in this layer.
Remote CI and dependency merges remain required. R065 delivery remains in progress.
