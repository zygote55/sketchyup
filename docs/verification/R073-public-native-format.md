# R073 — Public native format and non-replacing migration

2026-10-07. Implementation `f6e7530` through `228f1bd`, integrated source
`a376444`. Parent: [PR #167](https://github.com/zygote55/sketchyup/pull/167).
Contract: [0108](../decisions/0108-public-native-format.md);
[machine catalog](../api/native-format-v1.json).

The complete Debug build passes **144/144 CTest suites in 165.22 s**.
Four final native Wayland 2× integration suites pass in
**20.765 s** ([matrix](R073-native-matrix.json)).
Focused native-format, persistence and M4 persistence suites pass **3/3 in 3.22 s**;
the corresponding ASan/UBSan run passes **3/3 in 21.50 s**, with leak detection
and halt-on-error. The native-format sanitizer suite itself takes 18.80 s.

The first public contract names existing document version 24/container version 2;
no existing file is renumbered. Inspection and validation fully decode and check
semantics, checksums, assets, required features, references and allocator floors
without modifying input. Fixtures cover every raw version 1–24 and all committed
historical native files. Future formats, unknown required records/chunks/features,
corrupt bytes and invalid references reject explicitly.

Migration validates a copy, encodes the current container, decodes it again and
compares complete records before synchronized atomic non-replacing publication.
Existing files, source aliases, symlinks and competing writers cannot be replaced.
Failure after publication reports uncertain durability while retaining the complete
copy. Ordinary saving retains its separate backup policy. Tests compare originals
byte for byte and exercise standalone CLI option isolation.

The [installed CLI check](R073-installed-format.json) inspects and validates the
historical v11 complete fixture, migrates to a new current file, rejects overwrite
and unrelated modes, and confirms source bytes unchanged. The
[installed smoke](R073-installed-smoke.json) checks eight exact catalogs, thirteen
contracts, desktop bytes and existing relocatable lighting/export behavior.
[Source package](R073-source-package.json): **158 installed inputs** match byte for
byte; SHA-256 `226524d4ff2aead66d818a021feadf8dccb81a2d76004e1fcfd7f2b568b9d5bf`.

R073 is locally complete. Required remote CI and ordered merge remain delivery
gates. M8 overlap on the public storage boundary does not claim M7 acceptance.
