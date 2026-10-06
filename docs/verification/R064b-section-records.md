# R064.b — Persisted context section planes

2026-10-06, implementation `4a5a268`, legacy fixture correction `f69d850`.
[Contract](../decisions/0073-section-records.md). This layer stores named planes
and activation; native visualization and export remain subsequent R064 work.

All **115 CTest suites passed in 111.06 seconds**, including real Blender. The
initial run exposed two synthetic historical documents which retained new fields
after changing their version number. Correcting those fixtures restored the
full run; strict legacy decoding remains unchanged. Build output had no warnings.
Logs: `build/r064b-complete-build.log`, `build/r064b-legacy-fixtures-build.log`
and `build/r064b-final-ctest.log`.

Nine ASan/UBSan suites passed with leak detection in **21.74 seconds**, without
suppressions: section records, section geometry, prepared edits, saved scenes,
components, transaction coordination, staging and both section/scene persistence
suites. Logs: `build/r064b-sanitize-{build,ctest}.log`.

Tests cover immutable publication, stale edits, one-step compound Undo/Redo,
prepared/amended edits, shared component isolation, retained missing contexts,
exact uint64 identity and retired floors, strict malformed input, recovery and
eight-plane ancestor limits. World transforms include reflected contexts.

Four native Wayland checks at scale 2 passed in **16.900 seconds**: scene input,
style input, assistant preview and native MCP. [Results](R064b-native-matrix.json).
These guard existing workflows; this layer adds no section UI.

Five actual historical fixtures (three schema-16 textured models, schema-17
styles and schema-18 scenes) passed installed CLI migration checks. Each retains
root/body/missing-context planes, activation, IDs above 2^53, retired floors and
saved scenes exactly across relocation. The schema-18 reader rejects the new
format explicitly. [Installed results](R064b-installed-smoke.json).

The source archive contains all **110 explicit install inputs byte-for-byte**,
without build output or Git metadata. The installed contract matches its source.
[Package results](R064b-source-package.json).
