# R081 local agent-distribution and M8 study checks

2026-10-07. Local implementation/verification complete; integration, remote CI,
ordered merges and milestone prerequisites remain open. This is not M8 acceptance.

- Generated reference `4783198`: two artifacts match live command/query,
  transaction, session, MCP, recipe and extension registries. A deliberately
  changed reference is rejected by the drift check.
- Executable guide/recipes `490a615`: all ten shipped recipes pass physical
  geometry and save/reload checks, including both material sides and exact world
  placement. The guide describes actual version-1 interfaces and failure handling.
- Optional discovery `78db3de`: relocated CLI verifies missing/present helpers,
  adjacent/installed locations, no helper execution, no path disclosure, and mixed
  mode rejection. It does not inspect provider credentials or contact the network.
- Normal combined recipe/reference/discovery gate: **3/3 in 15.27 s**. Sanitized
  discovery and drift pass in **2.77 s** and **1.99 s**. The complete sanitized
  recipe suite passes in **354.34 s** after `2f3a804` gives instrumented runs an
  appropriate verification deadline. The initial 180-second harness timeout had
  passed eight recipes before stopping; it was not a sanitizer finding. Leak
  detection and halt-on-error remain enabled.
- Combined M8 study `f93c6a7`: **1/1 normal in 0.19 s**, **1/1 ASan/UBSan in
  1.46 s**. Native/library/glTF/OBJ/STL geometry measures 2 × 3 × 4 m and 52 m²;
  exact STL welding reconstructs 24 m³. DXF perimeter is 10 m. Measured PDF/SVG
  span 40 × 60 mm at 1:50. A real extension helper adds a 6 m² panel; undo restores
  all template content while revision/ID allocation remain monotonic. The retained
  PDF was rasterized with actual Poppler and visually reviewed.

[Installed CLI checks](R081-local-installed-smoke.json) execute all ten installed
recipes, reload outputs, verify guide/reference/example bytes, and discover the
packaged helpers. This is a headless installation check, not final desktop package
acceptance. [Source-package verification](R081-local-source-package.json) matches
all **196 installed file inputs** byte for byte at the local study source.
[Retained synthetic artifacts](R081-local-study-artifacts.json) record exact bytes
and hashes without private filesystem paths or provider data.

The [M8 procedure](../M8_ACCEPTANCE.md) also requires complete integration tests,
external consumer oracles, native matrices and accepted prerequisites. R082–R089
release performance, hardware, provider and packaging gates remain separate.
