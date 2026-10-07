# R081.f — Final integrated M8 regression

2026-10-07. Integration `0217acb`; build-option correction `53bb8b9`.
Parent: [PR #201](https://github.com/zygote55/sketchyup/pull/201).

The complete development build succeeds. [All 175 CTest checks](R081f-final-ctest.txt)
pass in **194.95 s**, with no skipped tests. Source path in the published log is
normalized to `$SOURCE`. All eleven real consumer checks execute: Blender rendering,
packed handoff, animation, M7 study, glTF, OBJ import/export, STL import/export,
ezdxf DXF export and Poppler measured output. The normal suite includes the combined
M8 study, every shipped recipe, discovery and generated-reference drift checks.

The [final native integration matrix](R081f-final-native-matrix.json) passes all
**eight Wayland 2× workflows in 32.717 s**: glTF, OBJ, STL, DXF, measured output,
libraries, extensions and native MCP. Dedicated feature matrices on Wayland/X11 at
1×/2× and ASan/UBSan remain recorded in their feature evidence; this integration
run supplements those checks. It does not claim live-provider revalidation.

Desktop ON / CLI OFF configures on the final source. The separately retained
[build-option tests](R081f-build-configurations.md) pass all four import suites
without a CLI binary and all five CLI-enabled import/measured suites. Only CLI
subprocess assertions depend on the optional executable; core conversion coverage
continues in both configurations. CI now probes the previously failing combination.
The Python build dependency was already published in PR #198; this layer retains
its local verification record without repeating that product change.

The [installed smoke](R081f-final-installed-smoke.json) checks the actual desktop
and CLI, both helpers, ten catalogs, forty-five contracts, the M8 gate procedure
and exchange workflows. [Source package](R081f-final-source-package.json): **199
byte-exact installed inputs**, SHA-256
`bb9943e54f5a7ab7a94d7ef4657e3725b8adf25752a11de4b02a578ff8ee12a1`.
Generated Arch metadata explicitly lists Python as a build dependency.

Host: Qt 6.11.2, GCC 16.2.1, Blender 5.2.1, Poppler 26.08.0, ezdxf 1.4.4,
Mesa 26.2.2. Native matrix: isolated Weston/Wayland with Mesa 26.2.3 software GL.
No provider credentials or actual user preferences were used in these checks.

This completes local M8 integration verification. M7 acceptance, complete remote
CI, ordered merges and the explicit M8 acceptance record remain required. M9
performance, hardware, accessibility, provider and release-package work is separate.
