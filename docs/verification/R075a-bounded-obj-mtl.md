# R075.a — Bounded OBJ and MTL parsing

2026-10-07. OBJ `8509488` and MTL `bc6b1b0` development series, contract `ab0628f`;
final integration `4572bb9`. Parent: [PR #171](https://github.com/zygote55/sketchyup/pull/171).
Contract: [0112](../decisions/0112-bounded-obj-mtl-parsing.md).

The complete Debug build passes **149/149 CTest suites in 164.18 s**.
The four final native Wayland 2× integration checks pass in
**18.216 s** ([matrix](R075a-native-matrix.json)).
Dedicated parser checks also pass normal and ASan/UBSan builds: OBJ 0.10/0.12 s,
MTL 0.02/0.06 s, with leak detection and halt-on-error enabled.

The parsers capture bounded UTF-8 OBJ geometry, index references, groups,
materials, smoothing and MTL diffuse color/opacity/image declarations without
executing source commands or loading external resources. Fixtures cover positive
forward and relative indices, malformed/mixed corner forms, finite explicit unit
and axis conversion, parser budgets and MTL option/fallback reports. Unsupported
appearance remains explicit. Native conversion and import/export workflows follow
in R075.b–d; this parser slice does not expose a partial File-menu importer.

The [installed smoke](R075a-installed-smoke.json) verifies eight catalogs,
seventeen contracts, installed glTF import, existing/source-file protection,
desktop/license bytes and installed/relocated export behavior.
[Source package](R075a-source-package.json): **163 installed inputs** match byte
for byte; SHA-256 `390dc250265703d2350799026cc7df8dea8ef1bb5f9d57c091cf846962ac83dd`.

Remote CI and ordered merges remain delivery gates. M7 acceptance is not claimed
while its prerequisite PRs remain open.
