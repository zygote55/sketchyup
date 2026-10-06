# R064.c — Shared section authoring and inspection

2026-10-06, implementation `3d7a03f`, integrated with the accepted local R064.b
fixture corrections and native catalogs. [Contract](../decisions/0074-section-commands.md).
Native drawing, section controls and export remain subsequent R064 work.

All **116 CTest suites passed in 121.40 seconds**, including real Blender. The
full development build emitted no warnings. Logs:
`build/r064c-complete-{build,ctest}.log`. Section command and inspection suites
also passed ASan/UBSan with leak detection in **4.42 seconds**, without
suppressions (`build/r064c-sanitize-{build,ctest}.log`).

Tests exercise creation/update/deletion/activation, immutable preview and staging,
reflected world frames, context ownership, unchanged authoritative geometry,
atomic rejection, one-step Undo/Redo, missing contexts, bounded pagination and
malformed identity filters. Shared component-definition scopes reject these
document operations explicitly. The existing exhaustive command and inspection
registry checks include every new operation.

Independent Draft 2020-12 validation passed **21 valid command requests and 105
invalid requests** across three command catalogs, plus nine valid and nine invalid
query requests across three inspection catalogs. All seven published catalogs
match their generated interfaces. [Schema results](R064c-schema-validation.json).

Four native Wayland checks at scale 2 passed in **16.676 seconds**: scene input,
style input, assistant preview and native MCP. These used the system Wayland
library. [Native results](R064c-native-matrix.json).

The installed six-command example creates model/body cuts with exact returned
identities. Installed checks exercise all four commands and three queries,
pagination, context filtering, local/world coefficients, ancestor scope,
deactivation/deletion, failed-batch byte preservation and exact relocated
round-trip. Authoritative body records remain unchanged. All seven installed
catalogs, the contract and desktop binary match their sources/build outputs.
[Installed results](R064c-installed-smoke.json).

The source archive includes all **112 explicit install inputs byte-for-byte**,
without build output or Git metadata. [Package results](R064c-source-package.json).
Remote CI and dependency merges remain required; the independently reproduced
Wayland proxy leak is being addressed on the earlier Styles layer.
