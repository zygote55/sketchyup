# R077.d — Native and standalone DXF workflows

2026-10-07. Workflow contract `e7fd6f6`; integration `a906ed6`.
Parent: [PR #182](https://github.com/zygote55/sketchyup/pull/182).
Contract: [0123](../decisions/0123-native-dxf-interchange-workflow.md).

The desktop, CLI and affected integration targets build successfully. Final source,
conversion/CLI, export and independent ezdxf checks pass **4/4 in 1.06 s**.
Five final native Wayland 2× checks, including DXF import/export interaction, pass
in **22.655 s** ([integration matrix](R077d-native-matrix.json)).
These are targeted checks of this workflow layer; the parent's complete 162-suite
run is recorded separately and fresh complete remote CI remains required.

Earlier dedicated CLI/export/reference checks pass **3/3 in 1.70 s**, and
ASan/UBSan conversion/CLI checks **1/1 in 5.41 s**, with leak detection and
halt-on-error. The [eight-case platform matrix](R077d-platform-matrix.json) passes
Wayland/X11 at 1×/2× in normal (**12.917 s**) and sanitized (**17.103 s**) builds,
including cancellation, invalid input, reports, source-safe Save and wire edit/undo.
Exact matrix implementation identities are retained in each row.

File-menu import chooses units and chord resolution, validates privately and
preserves current work on cancellation or failure. Accepted input opens as an
unsaved native model. Export chooses explicit units and a new path with a disclosed
world-XY scope. Both CLI modes use the same bounded conversion/export services;
missing units, invalid resolution and unrelated/mixed options reject before output.
Neither workflow replaces source or existing output files.

The [installed smoke](R077d-installed-smoke.json) verifies actual DXF import,
native save/validation, export and reimport retaining three analytic curves,
source/overwrite protection, eight catalogs and twenty-eight contracts. Prior
STL/OBJ/glTF and render-package smoke checks also pass. [Source package](R077d-source-package.json):
**174 installed inputs** match byte for byte; SHA-256 `0e6c746b690ec53656acfcc2960e67b49e47f1ca3c4c1f777df09d3403fa2eb5`.

R077 implementation and local verification are complete. Remote CI, ordered merges
and the M7/M8 milestone gates remain open; this report does not claim acceptance.
